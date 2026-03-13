/*
Mini HTTP server variant for mixed workloads where the CPU phases are
matrix-multiplication tasks instead of busy waits.

Endpoint:
  GET /work?cpu1=2&io=5000&cpu2=2

Meaning:
  cpu1: number of matrix-multiplication iterations before I/O
  io:   blocking wait to simulate I/O (microseconds)
  cpu2: number of matrix-multiplication iterations after I/O

Optional environment variables:
  MIXED_MATMUL_N=64
  MIXED_MATMUL_BS=32

Build:
  g++ -O2 -std=c++20 -pthread mini_http_server_matmul.cpp thread_pool.cpp -o mini_http_server_matmul

Run:
  ./mini_http_server_matmul classic 8080 8
  ./mini_http_server_matmul coro    8080 8
  ./mini_http_server_matmul ws      8080 8
  ./mini_http_server_matmul elastic 8080 4 32
  ./mini_http_server_matmul advws   8080 4 32 50
*/

#include "thread_pool.h"
#include "coro_runtime.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

static inline size_t ridx(size_t n, size_t r, size_t c) { return r * n + c; }

struct MatmulConfig {
    size_t n;
    size_t bs;
    std::vector<double> a;
    std::vector<double> b;
};

static size_t parse_env_size_t(const char* name, size_t def) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return def;
    try {
        size_t value = (size_t)std::stoull(raw);
        return value > 0 ? value : def;
    } catch (...) {
        return def;
    }
}

static MatmulConfig make_matmul_config() {
    MatmulConfig cfg;
    cfg.n = parse_env_size_t("MIXED_MATMUL_N", 64);
    cfg.bs = parse_env_size_t("MIXED_MATMUL_BS", 32);
    if (cfg.bs == 0) cfg.bs = 32;

    cfg.a.resize(cfg.n * cfg.n);
    cfg.b.resize(cfg.n * cfg.n);

    std::mt19937_64 rng(123456789ull);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (double& x : cfg.a) x = dist(rng);
    for (double& x : cfg.b) x = dist(rng);
    return cfg;
}

static const MatmulConfig& matmul_config() {
    static const MatmulConfig cfg = make_matmul_config();
    return cfg;
}

static void matmul_once(const MatmulConfig& cfg, std::vector<double>& c) {
    std::fill(c.begin(), c.end(), 0.0);

    const size_t n = cfg.n;
    const size_t bs = cfg.bs;
    for (size_t i0 = 0; i0 < n; i0 += bs) {
        const size_t i_max = std::min(i0 + bs, n);
        for (size_t j0 = 0; j0 < n; j0 += bs) {
            const size_t j_max = std::min(j0 + bs, n);
            for (size_t k0 = 0; k0 < n; k0 += bs) {
                const size_t k_max = std::min(k0 + bs, n);
                for (size_t i = i0; i < i_max; ++i) {
                    for (size_t k = k0; k < k_max; ++k) {
                        const double aik = cfg.a[ridx(n, i, k)];
                        const size_t b_row = ridx(n, k, 0);
                        for (size_t j = j0; j < j_max; ++j) {
                            c[ridx(n, i, j)] += aik * cfg.b[b_row + j];
                        }
                    }
                }
            }
        }
    }
}

static double checksum_sparse(const std::vector<double>& c) {
    double s = 0.0;
    const size_t step = std::max<size_t>(1, c.size() / 32);
    for (size_t i = 0; i < c.size(); i += step) {
        s += c[i];
    }
    return s;
}

static double run_matmul_iters(int iters) {
    if (iters <= 0) return 0.0;
    const MatmulConfig& cfg = matmul_config();
    std::vector<double> c(cfg.n * cfg.n);
    double checksum = 0.0;
    for (int i = 0; i < iters; ++i) {
        matmul_once(cfg, c);
        checksum += checksum_sparse(c);
    }
    return checksum;
}

static std::optional<int> parse_int(std::string_view s) {
    if (s.empty()) return std::nullopt;
    int sign = 1;
    size_t i = 0;
    if (s[0] == '-') {
        sign = -1;
        i = 1;
    }
    int v = 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + (c - '0');
    }
    return sign * v;
}

static int get_q_int(std::string_view target, std::string_view key, int def) {
    const size_t qpos = target.find('?');
    if (qpos == std::string_view::npos) return def;
    std::string_view qs = target.substr(qpos + 1);

    size_t pos = 0;
    while (pos < qs.size()) {
        size_t amp = qs.find('&', pos);
        if (amp == std::string_view::npos) amp = qs.size();
        std::string_view pair = qs.substr(pos, amp - pos);

        const size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            std::string_view k = pair.substr(0, eq);
            std::string_view v = pair.substr(eq + 1);
            if (k == key) {
                if (auto iv = parse_int(v)) return *iv;
                return def;
            }
        }
        pos = amp + 1;
    }
    return def;
}

static bool read_until_headers_end(int fd, std::string& out) {
    out.clear();
    out.reserve(2048);

    char buf[2048];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        out.append(buf, buf + n);
        if (out.find("\r\n\r\n") != std::string::npos) return true;
        if (out.size() > 64 * 1024) return false;
    }
}

static std::string_view first_line(std::string_view s) {
    const size_t p = s.find("\r\n");
    if (p == std::string_view::npos) return s;
    return s.substr(0, p);
}

static bool parse_request_target(std::string_view req, std::string_view& method,
                                 std::string_view& target) {
    std::string_view line = first_line(req);
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return false;
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;
    method = line.substr(0, sp1);
    target = line.substr(sp1 + 1, sp2 - (sp1 + 1));
    return true;
}

static bool send_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static std::string make_http_response(int status, std::string_view content_type, std::string body) {
    std::ostringstream oss;
    if (status == 200) {
        oss << "HTTP/1.1 200 OK\r\n";
    } else if (status == 404) {
        oss << "HTTP/1.1 404 Not Found\r\n";
    } else {
        oss << "HTTP/1.1 400 Bad Request\r\n";
    }
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

static std::string build_work_body(int cpu1_iters, int io_us, int cpu2_iters) {
    const uint64_t t0 = now_ns();
    const double checksum1 = run_matmul_iters(cpu1_iters);
    if (io_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(io_us));
    }
    const double checksum2 = run_matmul_iters(cpu2_iters);
    const uint64_t total_us = (now_ns() - t0) / 1000ull;

    const MatmulConfig& cfg = matmul_config();
    std::ostringstream body;
    body << "{";
    body << "\"endpoint\":\"/work\",";
    body << "\"cpu1_iters\":" << cpu1_iters << ',';
    body << "\"io_us\":" << io_us << ',';
    body << "\"cpu2_iters\":" << cpu2_iters << ',';
    body << "\"matrix_n\":" << cfg.n << ',';
    body << "\"block_size\":" << cfg.bs << ',';
    body << "\"checksum\":" << (checksum1 + checksum2) << ',';
    body << "\"total_us\":" << total_us;
    body << "}\n";
    return body.str();
}

static void handle_connection(int client_fd) {
    std::string req;
    if (!read_until_headers_end(client_fd, req)) {
        ::close(client_fd);
        return;
    }

    std::string_view method, target;
    if (!parse_request_target(req, method, target)) {
        auto resp = make_http_response(400, "text/plain", "Bad Request\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    if (method != "GET") {
        auto resp = make_http_response(400, "text/plain", "GET only\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    const bool is_work = (target.rfind("/work", 0) == 0);
    if (!is_work) {
        auto resp = make_http_response(
            404,
            "text/plain",
            "Try /work?cpu1=2&io=5000&cpu2=2 where cpu1/cpu2 are matmul iterations\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    int cpu1_iters = get_q_int(target, "cpu1", 2);
    int io_us = get_q_int(target, "io", 5000);
    int cpu2_iters = get_q_int(target, "cpu2", 2);

    auto body = build_work_body(cpu1_iters, io_us, cpu2_iters);
    auto resp = make_http_response(200, "application/json", body);
    (void)send_all(client_fd, resp.data(), resp.size());
    ::close(client_fd);
}

static coro::DetachedTask handle_connection_coro(int client_fd, coro::PoolScheduler sched) {
    co_await sched.schedule();
    try {
        std::string req;
        if (!read_until_headers_end(client_fd, req)) {
            ::close(client_fd);
            co_return;
        }

        std::string_view method, target;
        if (!parse_request_target(req, method, target)) {
            auto resp = make_http_response(400, "text/plain", "Bad Request\n");
            (void)send_all(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            co_return;
        }

        if (method != "GET") {
            auto resp = make_http_response(400, "text/plain", "GET only\n");
            (void)send_all(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            co_return;
        }

        const bool is_work = (target.rfind("/work", 0) == 0);
        if (!is_work) {
            auto resp = make_http_response(
                404,
                "text/plain",
                "Try /work?cpu1=2&io=5000&cpu2=2 where cpu1/cpu2 are matmul iterations\n");
            (void)send_all(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            co_return;
        }

        int cpu1_iters = get_q_int(target, "cpu1", 2);
        int io_us = get_q_int(target, "io", 5000);
        int cpu2_iters = get_q_int(target, "cpu2", 2);

        const uint64_t t0 = now_ns();
        const double checksum1 = run_matmul_iters(cpu1_iters);
        if (io_us > 0) {
            co_await coro::sleep_for(std::chrono::microseconds(io_us), sched);
        }
        const double checksum2 = run_matmul_iters(cpu2_iters);
        const uint64_t total_us = (now_ns() - t0) / 1000ull;

        const MatmulConfig& cfg = matmul_config();
        std::ostringstream body;
        body << "{";
        body << "\"endpoint\":\"/work\",";
        body << "\"cpu1_iters\":" << cpu1_iters << ',';
        body << "\"io_us\":" << io_us << ',';
        body << "\"cpu2_iters\":" << cpu2_iters << ',';
        body << "\"matrix_n\":" << cfg.n << ',';
        body << "\"block_size\":" << cfg.bs << ',';
        body << "\"checksum\":" << (checksum1 + checksum2) << ',';
        body << "\"total_us\":" << total_us;
        body << "}\n";

        auto resp = make_http_response(200, "application/json", body.str());
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
    } catch (...) {
        ::close(client_fd);
    }
}

static int make_listen_socket(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed");
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind() failed (port in use?)");
    }

    if (::listen(fd, 1024) < 0) {
        ::close(fd);
        throw std::runtime_error("listen() failed");
    }

    return fd;
}

static ThreadPool make_pool_from_args(int argc, char** argv) {
    if (argc < 4) {
        throw std::runtime_error("usage: <kind> <port> <threads> ...");
    }

    std::string kind = argv[1];
    if (kind == "classic" || kind == "coro") {
        size_t threads = (size_t)std::stoul(argv[3]);
        return ThreadPool(threads);
    }
    if (kind == "ws") {
        size_t threads = (size_t)std::stoul(argv[3]);
        return ThreadPool(threads, ThreadPool::PoolKind::WorkStealing);
    }
    if (kind == "elastic") {
        if (argc < 5) {
            throw std::runtime_error("usage: elastic <port> <min_threads> <max_threads>");
        }
        size_t min_t = (size_t)std::stoul(argv[3]);
        size_t max_t = (size_t)std::stoul(argv[4]);
        return ThreadPool(min_t, max_t);
    }
    if (kind == "advws") {
        if (argc < 6) {
            throw std::runtime_error("usage: advws <port> <min_threads> <max_threads> <idle_ms>");
        }
        size_t min_t = (size_t)std::stoul(argv[3]);
        size_t max_t = (size_t)std::stoul(argv[4]);
        int idle_ms = std::stoi(argv[5]);
        return ThreadPool(min_t,
                          max_t,
                          ThreadPool::PoolKind::AdvancedElasticStealing,
                          std::chrono::milliseconds(idle_ms));
    }

    throw std::runtime_error("unknown kind: " + kind + " (use classic/ws/elastic/advws/coro)");
}

int main(int argc, char** argv) {
    try {
        if (argc < 4) {
            std::cerr << "Usage:\n"
                      << "  ./mini_http_server_matmul classic <port> <threads>\n"
                      << "  ./mini_http_server_matmul coro    <port> <threads>\n"
                      << "  ./mini_http_server_matmul ws      <port> <threads>\n"
                      << "  ./mini_http_server_matmul elastic <port> <min_threads> <max_threads>\n"
                      << "  ./mini_http_server_matmul advws   <port> <min_threads> <max_threads> <idle_ms>\n";
            return 2;
        }

        const std::string kind = argv[1];
        const uint16_t port = (uint16_t)std::stoi(argv[2]);
        ThreadPool pool = make_pool_from_args(argc, argv);
        coro::PoolScheduler sched(pool);

        int listen_fd = make_listen_socket(port);
        const MatmulConfig& cfg = matmul_config();
        std::cout << "Listening on 0.0.0.0:" << port
                  << " | endpoint: /work?cpu1=2&io=5000&cpu2=2"
                  << " | matrix N=" << cfg.n
                  << " BS=" << cfg.bs << "\n";

        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int cfd = ::accept(listen_fd, (sockaddr*)&client, &len);
            if (cfd < 0) {
                continue;
            }

            if (kind == "coro") {
                handle_connection_coro(cfd, sched);
            } else {
                pool.submit([cfd] { handle_connection(cfd); });
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
