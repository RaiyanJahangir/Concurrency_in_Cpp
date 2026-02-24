#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#define main mini_http_server_main
#include "mini_http_server.cpp"
#undef main

class TestSuite {
public:
    using TestFn = std::function<void()>;

    void add(std::string name, TestFn fn) {
        tests_.push_back({std::move(name), std::move(fn)});
    }

    int run() const {
        int failures = 0;
        for (const auto& t : tests_) {
            try {
                t.second();
                std::cout << "[PASS] " << t.first << "\n";
            } catch (const std::exception& ex) {
                ++failures;
                std::cout << "[FAIL] " << t.first << ": " << ex.what() << "\n";
            } catch (...) {
                ++failures;
                std::cout << "[FAIL] " << t.first << ": unknown exception\n";
            }
        }
        std::cout << "Summary: " << (tests_.size() - static_cast<size_t>(failures))
                  << "/" << tests_.size() << " passed\n";
        return failures == 0 ? 0 : 1;
    }

private:
    std::vector<std::pair<std::string, TestFn>> tests_;
};

namespace {

void expect_true(bool cond, const std::string& msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

void expect_eq(int got, int expected, const std::string& msg) {
    if (got != expected) {
        throw std::runtime_error(msg + " (got=" + std::to_string(got) +
                                 ", expected=" + std::to_string(expected) + ")");
    }
}

void expect_contains(std::string_view haystack, std::string_view needle, const std::string& msg) {
    if (haystack.find(needle) == std::string_view::npos) {
        throw std::runtime_error(msg + " (missing: " + std::string(needle) + ")");
    }
}

std::string read_all_from_fd(int fd) {
    std::string out;
    char buf[1024];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            throw std::runtime_error("recv failed while reading response");
        }
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

std::string run_request_through_handler(const std::string& req) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw std::runtime_error("socketpair failed");
    }

    const int client_fd = fds[0];
    const int server_fd = fds[1];

    if (!send_all(client_fd, req.data(), req.size())) {
        ::close(client_fd);
        ::close(server_fd);
        throw std::runtime_error("failed to write test request");
    }
    ::shutdown(client_fd, SHUT_WR);

    handle_connection(server_fd);

    const std::string resp = read_all_from_fd(client_fd);
    ::close(client_fd);
    return resp;
}

class MiniHttpServerTests {
public:
    static void register_all(TestSuite& suite) {
        suite.add("parse_int parses signed integers", parse_int_signed);
        suite.add("parse_int rejects invalid strings", parse_int_invalid);
        suite.add("get_q_int reads value and defaults", get_q_int_value_and_default);
        suite.add("parse_request_target parses valid request line", parse_request_target_valid);
        suite.add("parse_request_target rejects malformed line", parse_request_target_invalid);
        suite.add("make_http_response sets status and length", make_http_response_headers);
        suite.add("handle_connection rejects non-GET", handle_non_get_returns_400);
        suite.add("handle_connection returns 404 for unknown route", handle_unknown_route_returns_404);
        suite.add("handle_connection returns work JSON", handle_work_returns_json_200);
    }

private:
    static void parse_int_signed() {
        auto v1 = parse_int("123");
        auto v2 = parse_int("-45");
        expect_true(v1.has_value(), "parse_int should parse positive integer");
        expect_true(v2.has_value(), "parse_int should parse negative integer");
        expect_eq(*v1, 123, "parse_int positive mismatch");
        expect_eq(*v2, -45, "parse_int negative mismatch");
    }

    static void parse_int_invalid() {
        expect_true(!parse_int("").has_value(), "empty string should be invalid");
        expect_true(!parse_int("abc").has_value(), "alphabetic value should be invalid");
        expect_true(!parse_int("7x").has_value(), "mixed value should be invalid");
    }

    static void get_q_int_value_and_default() {
        const std::string_view target = "/work?cpu1=20&io=30&cpu2=x";
        expect_eq(get_q_int(target, "cpu1", 99), 20, "cpu1 should parse from query");
        expect_eq(get_q_int(target, "io", 99), 30, "io should parse from query");
        expect_eq(get_q_int(target, "cpu2", 99), 99, "invalid integer should return default");
        expect_eq(get_q_int("/work", "cpu1", 77), 77, "missing query should return default");
    }

    static void parse_request_target_valid() {
        std::string_view method;
        std::string_view target;
        const bool ok = parse_request_target("GET /work?cpu1=1 HTTP/1.1\r\nHost: x\r\n\r\n", method, target);
        expect_true(ok, "valid request line should parse");
        expect_true(method == "GET", "method mismatch");
        expect_true(target == "/work?cpu1=1", "target mismatch");
    }

    static void parse_request_target_invalid() {
        std::string_view method;
        std::string_view target;
        const bool ok = parse_request_target("BROKENLINE\r\n\r\n", method, target);
        expect_true(!ok, "malformed request line should be rejected");
    }

    static void make_http_response_headers() {
        const std::string body = "hello";
        const std::string resp = make_http_response(200, "text/plain", body);
        expect_contains(resp, "HTTP/1.1 200 OK\r\n", "status line missing");
        expect_contains(resp, "Content-Type: text/plain\r\n", "content type missing");
        expect_contains(resp, "Content-Length: 5\r\n", "content length mismatch");
        expect_contains(resp, "\r\n\r\nhello", "body missing");
    }

    static void handle_non_get_returns_400() {
        const std::string req =
            "POST /work HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";
        const std::string resp = run_request_through_handler(req);
        expect_contains(resp, "HTTP/1.1 400 Bad Request\r\n", "expected 400 for non-GET");
        expect_contains(resp, "GET only\n", "expected GET-only message");
    }

    static void handle_unknown_route_returns_404() {
        const std::string req =
            "GET /unknown HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";
        const std::string resp = run_request_through_handler(req);
        expect_contains(resp, "HTTP/1.1 404 Not Found\r\n", "expected 404 for unknown route");
        expect_contains(resp, "Try /work?cpu1=200&io=5000&cpu2=200", "expected help message");
    }

    static void handle_work_returns_json_200() {
        const std::string req =
            "GET /work?cpu1=0&io=0&cpu2=0 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";
        const std::string resp = run_request_through_handler(req);
        expect_contains(resp, "HTTP/1.1 200 OK\r\n", "expected 200 for /work");
        expect_contains(resp, "Content-Type: application/json\r\n", "expected JSON response type");
        expect_contains(resp, "\"endpoint\":\"/work\"", "missing endpoint field");
        expect_contains(resp, "\"cpu1_us\":0", "missing cpu1_us field");
        expect_contains(resp, "\"io_us\":0", "missing io_us field");
        expect_contains(resp, "\"cpu2_us\":0", "missing cpu2_us field");
        expect_contains(resp, "\"total_us\":", "missing total_us field");
    }
};

}  // namespace

int main() {
    TestSuite suite;
    MiniHttpServerTests::register_all(suite);
    return suite.run();
}
