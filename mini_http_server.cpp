/*
Mini HTTP server backed by your ThreadPool.

Endpoint:
  GET /work?cpu1_mm=2&io=5000&cpu2_mm=2&mm_n=1024&mm_bs=64

Meaning:
  cpu1_mm: number of matrix multiplies before I/O
  io:   blocking wait to simulate I/O (sleep)
  cpu2_mm: number of matrix multiplies after I/O
  mm_n/mm_bs: matrix size and blocking size used by each multiply

Build (Linux/macOS):
  g++ -O2 -std=c++20 -pthread mini_http_server.cpp thread_pool.cpp -o mini_http_server

Run:
  ./mini_http_server classic 8080 8
  ./mini_http_server ws      8080 8
  ./mini_http_server elastic 8080 4 32
  ./mini_http_server advws   8080 4 32 50

Where:
  classic/ws:   <kind> <port> <threads>
  elastic:      elastic <port> <min_threads> <max_threads>
  advws:        advws  <port> <min_threads> <max_threads> <idle_ms>

Notes:
  - This server intentionally uses a *blocking* sleep for the I/O phase so you can
    observe thread blocking, context switches, and oversubscription effects.
  - If you later add a coroutine version, keep the exact same CPU->wait->CPU shape,
    but make the wait truly async (timerfd/epoll or Asio timer).
*/

#include "thread_pool.h"
#include "coro_runtime.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
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

static inline size_t ridx(size_t N, size_t r, size_t c) {
    return r * N + c;
}

static std::atomic<uint64_t> g_matmul_sink{0};

static void run_matrix_multiply_reps(int reps, int n_in, int bs_in) {
    if (reps <= 0) return;

    const size_t N = (n_in > 0) ? static_cast<size_t>(n_in) : 1024u;
    const size_t BS = (bs_in > 0) ? static_cast<size_t>(bs_in) : 64u;

    thread_local std::vector<double> A;
    thread_local std::vector<double> B;
    thread_local std::vector<double> C;
    thread_local size_t cap_n = 0;

    if (cap_n != N) {
        A.assign(N * N, 0.0);
        B.assign(N * N, 0.0);
        C.assign(N * N, 0.0);
        cap_n = N;

        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N; ++j) {
                A[ridx(N, i, j)] = static_cast<double>((i * 131 + j * 17) % 23) * 0.1;
                B[ridx(N, i, j)] = static_cast<double>((i * 29 + j * 73) % 19) * 0.2;
            }
        }
    }

    uint64_t local_sink = 0;
    for (int rep = 0; rep < reps; ++rep) {
        std::fill(C.begin(), C.end(), 0.0);
        for (size_t i0 = 0; i0 < N; i0 += BS) {
            const size_t i_max = std::min(i0 + BS, N);
            for (size_t j0 = 0; j0 < N; j0 += BS) {
                const size_t j_max = std::min(j0 + BS, N);
                for (size_t k0 = 0; k0 < N; k0 += BS) {
                    const size_t k_max = std::min(k0 + BS, N);
                    for (size_t i = i0; i < i_max; ++i) {
                        for (size_t k = k0; k < k_max; ++k) {
                            const double aik = A[ridx(N, i, k)];
                            const size_t b_row = ridx(N, k, 0);
                            for (size_t j = j0; j < j_max; ++j) {
                                C[ridx(N, i, j)] += aik * B[b_row + j];
                            }
                        }
                    }
                }
            }
        }
        local_sink += static_cast<uint64_t>(C[0] * 1000.0) ^ static_cast<uint64_t>(C[N * N - 1] * 1000.0);
    }
    g_matmul_sink.fetch_add(local_sink, std::memory_order_relaxed);
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

// Very small query parser for ints like "?cpu1_mm=2&io=5000&cpu2_mm=2".
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
        // protect against very large headers
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
    // Expected: METHOD SP TARGET SP HTTP/1.1
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

    // Route
    if (method != "GET") {
        auto resp = make_http_response(400, "text/plain", "GET only\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    const bool is_work = (target.rfind("/work", 0) == 0);
    if (!is_work) {
        auto resp = make_http_response(404, "text/plain",
                                       "Try /work?cpu1_mm=2&io=5000&cpu2_mm=2&mm_n=1024&mm_bs=64\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    // Mixed workload params: fixed matrix-work units + blocking I/O.
    int cpu1_mm = get_q_int(target, "cpu1_mm", get_q_int(target, "cpu1", 2));
    int io_us = get_q_int(target, "io", 5000);
    int cpu2_mm = get_q_int(target, "cpu2_mm", get_q_int(target, "cpu2", 2));
    int mm_n = get_q_int(target, "mm_n", 1024);
    int mm_bs = get_q_int(target, "mm_bs", 64);
    mm_n = std::max(8, mm_n);
    mm_bs = std::max(4, mm_bs);

    const uint64_t t0 = now_ns();

    // CPU(matmul units) -> (blocking) I/O -> CPU(matmul units)
    run_matrix_multiply_reps(cpu1_mm, mm_n, mm_bs);
    if (io_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(io_us));
    }
    run_matrix_multiply_reps(cpu2_mm, mm_n, mm_bs);

    const uint64_t total_us = (now_ns() - t0) / 1000ull;

    // Response body
    std::ostringstream body;
    body << "{";
    body << "\"endpoint\":\"/work\",";
    body << "\"cpu1_mm\":" << cpu1_mm << ',';
    body << "\"io_us\":" << io_us << ',';
    body << "\"cpu2_mm\":" << cpu2_mm << ',';
    body << "\"mm_n\":" << mm_n << ',';
    body << "\"mm_bs\":" << mm_bs << ',';
    body << "\"total_us\":" << total_us;
    body << "}\n";

    auto resp = make_http_response(200, "application/json", body.str());
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
            auto resp = make_http_response(404, "text/plain",
                                           "Try /work?cpu1_mm=2&io=5000&cpu2_mm=2&mm_n=1024&mm_bs=64\n");
            (void)send_all(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            co_return;
        }

        int cpu1_mm = get_q_int(target, "cpu1_mm", get_q_int(target, "cpu1", 2));
        int io_us = get_q_int(target, "io", 5000);
        int cpu2_mm = get_q_int(target, "cpu2_mm", get_q_int(target, "cpu2", 2));
        int mm_n = get_q_int(target, "mm_n", 1024);
        int mm_bs = get_q_int(target, "mm_bs", 64);
        mm_n = std::max(8, mm_n);
        mm_bs = std::max(4, mm_bs);

        const uint64_t t0 = now_ns();

        run_matrix_multiply_reps(cpu1_mm, mm_n, mm_bs);
        co_await coro::sleep_for(std::chrono::microseconds(io_us), sched);
        run_matrix_multiply_reps(cpu2_mm, mm_n, mm_bs);

        const uint64_t total_us = (now_ns() - t0) / 1000ull;

        std::ostringstream body;
        body << "{";
        body << "\"endpoint\":\"/work\",";
        body << "\"cpu1_mm\":" << cpu1_mm << ',';
        body << "\"io_us\":" << io_us << ',';
        body << "\"cpu2_mm\":" << cpu2_mm << ',';
        body << "\"mm_n\":" << mm_n << ',';
        body << "\"mm_bs\":" << mm_bs << ',';
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

    // Match the constructor patterns used in your benchmarks:
    //   classic: ThreadPool pool(threads);
    //   ws:      ThreadPool pool(threads, ThreadPool::PoolKind::WorkStealing);
    //   elastic: ThreadPool pool(min_threads, max_threads);
    //   advws:   ThreadPool pool(min_threads, max_threads, PoolKind::AdvancedElasticStealing, idle_timeout);

    if (kind == "classic") {
        size_t threads = (size_t)std::stoul(argv[3]);
        return ThreadPool(threads);
    }
    if (kind == "coro") {
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
                      << "  ./mini_http_server classic <port> <threads>\n"
                      << "  ./mini_http_server coro    <port> <threads>\n"
                      << "  ./mini_http_server ws      <port> <threads>\n"
                      << "  ./mini_http_server elastic <port> <min_threads> <max_threads>\n"
                      << "  ./mini_http_server advws   <port> <min_threads> <max_threads> <idle_ms>\n";
            return 2;
        }

        const std::string kind = argv[1];
        const uint16_t port = (uint16_t)std::stoi(argv[2]);
        ThreadPool pool = make_pool_from_args(argc, argv);
        coro::PoolScheduler sched(pool);

        int listen_fd = make_listen_socket(port);
        std::cout << "Listening on 0.0.0.0:" << port
                  << " | endpoint: /work?cpu1_mm=2&io=5000&cpu2_mm=2&mm_n=1024&mm_bs=64\n";

        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int cfd = ::accept(listen_fd, (sockaddr*)&client, &len);
            if (cfd < 0) {
                // accept can be interrupted
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
