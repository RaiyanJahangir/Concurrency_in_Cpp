/*
Mini HTTP server backed by your ThreadPool.

Endpoint:
  GET /work?cpu1=200&io=5000&cpu2=200

Meaning (microseconds):
  cpu1: CPU busy work before I/O
  io:   blocking wait to simulate I/O (sleep)
  cpu2: CPU busy work after I/O

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

using Clock = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

static void burn_cpu_us(int us) {
    if (us <= 0) return;
    const uint64_t start = now_ns();
    const uint64_t end = start + (uint64_t)us * 1000ull;
    volatile uint64_t x = 1469598103934665603ull;
    while (now_ns() < end) {
        x ^= (x << 13);
        x ^= (x >> 7);
        x ^= (x << 17);
        x *= 1099511628211ull;
    }
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

// Very small query parser for ints like "?cpu1=200&io=5000&cpu2=200".
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
                                       "Try /work?cpu1=200&io=5000&cpu2=200 (microseconds)\n");
        (void)send_all(client_fd, resp.data(), resp.size());
        ::close(client_fd);
        return;
    }

    // Mixed workload params (us)
    int cpu1_us = get_q_int(target, "cpu1", 200);
    int io_us   = get_q_int(target, "io",   5000);
    int cpu2_us = get_q_int(target, "cpu2", 200);

    const uint64_t t0 = now_ns();

    // CPU -> (blocking) I/O -> CPU
    burn_cpu_us(cpu1_us);
    if (io_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(io_us));
    }
    burn_cpu_us(cpu2_us);

    const uint64_t total_us = (now_ns() - t0) / 1000ull;

    // Response body
    std::ostringstream body;
    body << "{";
    body << "\"endpoint\":\"/work\",";
    body << "\"cpu1_us\":" << cpu1_us << ',';
    body << "\"io_us\":" << io_us << ',';
    body << "\"cpu2_us\":" << cpu2_us << ',';
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
                                           "Try /work?cpu1=200&io=5000&cpu2=200 (microseconds)\n");
            (void)send_all(client_fd, resp.data(), resp.size());
            ::close(client_fd);
            co_return;
        }

        int cpu1_us = get_q_int(target, "cpu1", 200);
        int io_us = get_q_int(target, "io", 5000);
        int cpu2_us = get_q_int(target, "cpu2", 200);

        const uint64_t t0 = now_ns();

        burn_cpu_us(cpu1_us);
        co_await coro::sleep_for(std::chrono::microseconds(io_us), sched);
        burn_cpu_us(cpu2_us);

        const uint64_t total_us = (now_ns() - t0) / 1000ull;

        std::ostringstream body;
        body << "{";
        body << "\"endpoint\":\"/work\",";
        body << "\"cpu1_us\":" << cpu1_us << ',';
        body << "\"io_us\":" << io_us << ',';
        body << "\"cpu2_us\":" << cpu2_us << ',';
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
                  << " | endpoint: /work?cpu1=200&io=5000&cpu2=200 (us)\n";

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
