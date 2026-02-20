/*
Client-side benchmark for mini_http_server.cpp.

It opens many concurrent clients to /work and measures latency + throughput.

Build:
  g++ -O2 -std=c++20 -pthread mixed_bench.cpp -o mixed_bench

Run:
  ./mixed_bench 127.0.0.1 8080 200 5000 200 64 10

Args:
  host port cpu1_us io_us cpu2_us concurrency duration_s

Notes:
  - This intentionally does a "simple" client (one request per TCP connection).
    For OS-level metrics (context switches, CPU util) that’s usually fine.
  - If you want to reduce connect() overhead, adapt it to persistent keep-alive
    connections per worker.
*/

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static bool send_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static bool recv_until(int fd, std::string& out, const char* needle) {
    out.clear();
    out.reserve(4096);
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        out.append(buf, buf + n);
        if (out.find(needle) != std::string::npos) return true;
        if (out.size() > 2 * 1024 * 1024) return false;
    }
}

static int connect_tcp(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }

    ::freeaddrinfo(res);
    return fd;
}

static double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = q * (v.size() - 1);
    size_t i = (size_t)idx;
    size_t j = std::min(i + 1, v.size() - 1);
    double frac = idx - i;
    return v[i] * (1.0 - frac) + v[j] * frac;
}

int main(int argc, char** argv) {
    if (argc < 8) {
        std::cerr << "Usage: ./mixed_bench host port cpu1_us io_us cpu2_us concurrency duration_s\n";
        return 2;
    }

    const std::string host = argv[1];
    const uint16_t port = (uint16_t)std::stoi(argv[2]);
    const int cpu1_us = std::stoi(argv[3]);
    const int io_us = std::stoi(argv[4]);
    const int cpu2_us = std::stoi(argv[5]);
    const int conc = std::max(1, std::stoi(argv[6]));
    const int duration_s = std::max(1, std::stoi(argv[7]));

    const std::string path = "/work?cpu1=" + std::to_string(cpu1_us) +
                             "&io=" + std::to_string(io_us) +
                             "&cpu2=" + std::to_string(cpu2_us);

    const std::string req =
        "GET " + path + " HTTP/1.1\r\n" +
        "Host: " + host + "\r\n" +
        "Connection: close\r\n" +
        "\r\n";

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> ok{0};
    std::atomic<uint64_t> fail{0};

    std::mutex lat_m;
    std::vector<double> lat_ms;
    lat_ms.reserve(200000);

    auto t_start = Clock::now();
    auto t_end = t_start + std::chrono::seconds(duration_s);

    auto worker = [&] {
        std::string resp;
        while (!stop.load(std::memory_order_acquire)) {
            auto t0 = Clock::now();

            int fd = connect_tcp(host, port);
            if (fd < 0) {
                fail.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            bool s_ok = send_all(fd, req.data(), req.size());
            bool r_ok = false;
            if (s_ok) {
                r_ok = recv_until(fd, resp, "\r\n\r\n");
            }
            ::close(fd);

            auto t1 = Clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (s_ok && r_ok) {
                ok.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(lat_m);
                lat_ms.push_back(ms);
            } else {
                fail.fetch_add(1, std::memory_order_relaxed);
            }

            if (Clock::now() >= t_end) {
                stop.store(true, std::memory_order_release);
                break;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(conc);
    for (int i = 0; i < conc; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    double total_s = std::chrono::duration<double>(Clock::now() - t_start).count();
    uint64_t ok_n = ok.load();
    uint64_t fail_n = fail.load();

    // Copy latency vector under lock
    std::vector<double> lats;
    {
        std::lock_guard<std::mutex> lk(lat_m);
        lats = lat_ms;
    }

    const double thr = (total_s > 0.0) ? (double)ok_n / total_s : 0.0;

    std::cout << "Benchmark: " << host << ":" << port << path << "\n";
    std::cout << "Concurrency: " << conc << " | Duration(s): " << total_s << "\n";
    std::cout << "OK: " << ok_n << " | Fail: " << fail_n << " | Throughput(req/s): " << thr << "\n";

    if (!lats.empty()) {
        double p50 = pct(lats, 0.50);
        double p95 = pct(lats, 0.95);
        double p99 = pct(lats, 0.99);
        double avg = std::accumulate(lats.begin(), lats.end(), 0.0) / (double)lats.size();
        std::cout << "Latency ms: avg=" << avg
                  << " p50=" << p50
                  << " p95=" << p95
                  << " p99=" << p99
                  << " (n=" << lats.size() << ")\n";
    }

    return 0;
}
