#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Config {
    fs::path project_root;
    fs::path build_dir;
    fs::path results_dir;
    fs::path output_csv;

    int trials = 5;
    int threads = 8;
    int warmup = 1;
    int reps = 3;

    int matrix_n = 1024;
    int matrix_bs = 64;

    int fib_n = 44;
    int fib_tasks = 8;
    int fib_split_threshold = 32;

    int fib_single_n = 44;
    int fib_single_split_threshold = 30;

    int fib_fast_n = 90;
    int fib_fast_tasks = 8;
};

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::optional<int> parse_int(const std::string& s) {
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) {
            return std::nullopt;
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parse_double(const std::string& s) {
    try {
        size_t idx = 0;
        double v = std::stod(s, &idx);
        if (idx != s.size()) {
            return std::nullopt;
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

int get_env_int(const char* name, int fallback) {
    const char* p = std::getenv(name);
    if (!p || *p == '\0') {
        return fallback;
    }
    auto v = parse_int(p);
    return v.value_or(fallback);
}

std::string get_env_str(const char* name, const std::string& fallback) {
    const char* p = std::getenv(name);
    if (!p || *p == '\0') {
        return fallback;
    }
    return std::string(p);
}

std::string timestamp_for_file() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string timestamp_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tmv);
    std::string s(buf);
    if (s.size() >= 5) {
        s.insert(s.size() - 2, ":");
    }
    return s;
}

std::string shell_quote(const std::string& s) {
    if (s.empty()) {
        return "''";
    }
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\"'\"'";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::string join_cmd_raw(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) {
            oss << ' ';
        }
        oss << argv[i];
    }
    return oss.str();
}

std::string join_cmd_shell(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) {
            oss << ' ';
        }
        oss << shell_quote(argv[i]);
    }
    return oss.str();
}

int run_system(const std::string& cmd) {
    int rc = std::system(cmd.c_str());
    if (rc == -1) {
        return 127;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    if (WIFSIGNALED(rc)) {
        return 128 + WTERMSIG(rc);
    }
    return 127;
}

bool run_or_fail(const std::string& cmd, const std::string& label) {
    const int ec = run_system(cmd);
    if (ec != 0) {
        std::cerr << "Failed: " << label << " (exit " << ec << ")\n";
        return false;
    }
    return true;
}

std::vector<std::string> read_lines(const fs::path& p) {
    std::ifstream in(p);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string read_stderr_snippet(const fs::path& p, int max_lines = 6) {
    std::ifstream in(p);
    std::string line;
    std::ostringstream oss;
    int count = 0;
    while (count < max_lines && std::getline(in, line)) {
        if (count > 0) {
            oss << ' ';
        }
        oss << trim(line);
        ++count;
    }
    std::string out = oss.str();
    out = std::regex_replace(out, std::regex("\\s+"), " ");
    return out;
}

std::map<std::string, std::string> parse_kv_file(const fs::path& p) {
    std::map<std::string, std::string> kv;
    for (const auto& line : read_lines(p)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string val = trim(line.substr(pos + 1));
        kv[key] = val;
    }
    return kv;
}

std::string format_fixed(double x, int digits) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(digits) << x;
    return oss.str();
}

std::string safe_div_str(const std::string& n_s, const std::string& d_s, int digits = 9) {
    auto n = parse_double(n_s);
    auto d = parse_double(d_s);
    if (!n || !d || *d == 0.0) {
        return "NA";
    }
    return format_fixed((*n) / (*d), digits);
}

std::string quantile_linear(std::vector<double> xs, double p) {
    if (xs.empty()) {
        return "NA";
    }
    std::sort(xs.begin(), xs.end());
    if (xs.size() == 1) {
        return format_fixed(xs[0], 9);
    }
    const double idx = p * static_cast<double>(xs.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = std::min(lo + 1, xs.size() - 1);
    const double frac = idx - static_cast<double>(lo);
    const double q = xs[lo] * (1.0 - frac) + xs[hi] * frac;
    return format_fixed(q, 9);
}

std::string or_na(const std::string& s) {
    return s.empty() ? "NA" : s;
}

std::string parse_perf_event_value(const fs::path& perf_out, const std::string& event) {
    std::ifstream in(perf_out);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            f.push_back(trim(tok));
        }
        if (f.empty()) {
            continue;
        }
        for (size_t i = 1; i < f.size(); ++i) {
            if (f[i] == event) {
                return f[0];
            }
        }
    }
    return "NA";
}

std::string csv_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

void write_csv_row(std::ofstream& out, const std::vector<std::string>& fields) {
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) {
            out << ',';
        }
        out << csv_escape(fields[i]);
    }
    out << '\n';
}

struct BenchParsed {
    std::string run_times = "NA";
    std::string p50 = "NA";
    std::string p95 = "NA";
    std::string p99 = "NA";
    std::string best = "NA";
    std::string avg = "NA";
    std::string checksum = "NA";
    std::string expected_checksum = "NA";
    std::string fib_value = "NA";
    std::string spawned_internal_nodes = "NA";
    std::vector<double> runs;
};

BenchParsed parse_bench_output(const fs::path& p) {
    BenchParsed b;
    const std::regex re_run(R"(^Run [0-9]+:\s+([^\s]+))");
    const std::regex re_best(R"(^Best:\s+([^\s]+))");
    const std::regex re_avg(R"(^Avg :\s+([^\s]+))");
    const std::regex re_checksum(R"(^Checksum:\s+([^\s]+))");
    const std::regex re_expected(R"(^Expected checksum:\s+([^\s]+))");
    const std::regex re_fib(R"(^Fib\([0-9]+\):\s+([^\s]+))");
    const std::regex re_spawned(R"(^Spawned internal nodes:\s+([^\s]+))");

    std::smatch m;
    std::vector<std::string> run_s;

    for (const auto& line : read_lines(p)) {
        if (std::regex_search(line, m, re_run)) {
            const std::string v = m[1];
            run_s.push_back(v);
            if (auto d = parse_double(v)) {
                b.runs.push_back(*d);
            }
            continue;
        }
        if (std::regex_search(line, m, re_best)) {
            b.best = m[1];
            continue;
        }
        if (std::regex_search(line, m, re_avg)) {
            b.avg = m[1];
            continue;
        }
        if (std::regex_search(line, m, re_checksum)) {
            b.checksum = m[1];
            continue;
        }
        if (std::regex_search(line, m, re_expected)) {
            b.expected_checksum = m[1];
            continue;
        }
        if (std::regex_search(line, m, re_fib)) {
            b.fib_value = m[1];
            continue;
        }
        if (std::regex_search(line, m, re_spawned)) {
            b.spawned_internal_nodes = m[1];
            continue;
        }
    }

    if (!run_s.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < run_s.size(); ++i) {
            if (i) {
                oss << ';';
            }
            oss << run_s[i];
        }
        b.run_times = oss.str();
        b.p50 = quantile_linear(b.runs, 0.50);
        b.p95 = quantile_linear(b.runs, 0.95);
        b.p99 = quantile_linear(b.runs, 0.99);
    }

    return b;
}

Config load_config(int argc, char** argv) {
    Config cfg;
    cfg.project_root = fs::path(get_env_str("PROJECT_ROOT", fs::current_path().string()));
    cfg.build_dir = fs::path(get_env_str("BUILD_DIR", (cfg.project_root / "build").string()));
    cfg.results_dir = fs::path(get_env_str("RESULTS_DIR", (cfg.project_root / "results").string()));

    cfg.trials = get_env_int("TRIALS", cfg.trials);
    cfg.threads = get_env_int("THREADS", cfg.threads);
    cfg.warmup = get_env_int("WARMUP", cfg.warmup);
    cfg.reps = get_env_int("REPS", cfg.reps);

    cfg.matrix_n = get_env_int("MATRIX_N", cfg.matrix_n);
    cfg.matrix_bs = get_env_int("MATRIX_BS", cfg.matrix_bs);

    cfg.fib_n = get_env_int("FIB_N", cfg.fib_n);
    cfg.fib_tasks = get_env_int("FIB_TASKS", cfg.threads);
    cfg.fib_split_threshold = get_env_int("FIB_SPLIT_THRESHOLD", cfg.fib_split_threshold);

    cfg.fib_single_n = get_env_int("FIB_SINGLE_N", cfg.fib_single_n);
    cfg.fib_single_split_threshold = get_env_int("FIB_SINGLE_SPLIT_THRESHOLD", cfg.fib_single_split_threshold);

    cfg.fib_fast_n = get_env_int("FIB_FAST_N", cfg.fib_fast_n);
    cfg.fib_fast_tasks = get_env_int("FIB_FAST_TASKS", cfg.threads);

    if (argc >= 2) {
        cfg.output_csv = argv[1];
    } else {
        cfg.output_csv = cfg.results_dir / ("cpu_workload_metrics_" + timestamp_for_file() + ".csv");
    }
    if (argc >= 3) {
        if (auto v = parse_int(argv[2])) {
            cfg.trials = *v;
        }
    }
    return cfg;
}

bool compile_benchmarks(const Config& cfg) {
    const fs::path matrix_src = cfg.project_root / "matrix_mul_bench.cpp";
    const fs::path fib_src = cfg.project_root / "fib_bench.cpp";
    const fs::path fib_single_src = cfg.project_root / "fib_single_bench.cpp";
    const fs::path fib_fast_src = cfg.project_root / "fib_fast_bench.cpp";
    const fs::path pool_src = cfg.project_root / "thread_pool.cpp";

    std::cout << "Compiling CPU benchmarks into " << cfg.build_dir << " ...\n";

    const std::string common = "g++ -O3 -std=c++20 -pthread ";
    if (!run_or_fail(common + shell_quote(matrix_src.string()) + " " + shell_quote(pool_src.string()) + " -o " +
                         shell_quote((cfg.build_dir / "matrix_mul_bench").string()),
                     "matrix_mul_bench")) {
        return false;
    }
    if (!run_or_fail(common + shell_quote(fib_src.string()) + " " + shell_quote(pool_src.string()) + " -o " +
                         shell_quote((cfg.build_dir / "fib_bench").string()),
                     "fib_bench")) {
        return false;
    }
    if (!run_or_fail(common + shell_quote(fib_single_src.string()) + " " + shell_quote(pool_src.string()) + " -o " +
                         shell_quote((cfg.build_dir / "fib_single_bench").string()),
                     "fib_single_bench")) {
        return false;
    }
    if (!run_or_fail(common + shell_quote(fib_fast_src.string()) + " " + shell_quote(pool_src.string()) + " -o " +
                         shell_quote((cfg.build_dir / "fib_fast_bench").string()),
                     "fib_fast_bench")) {
        return false;
    }
    return true;
}

bool detect_perf_enabled() {
    const long pid = static_cast<long>(::getpid());
    const fs::path probe_err = fs::path("/tmp") / ("perf_probe_err." + std::to_string(pid));
    const std::string cmd = "perf stat -e task-clock -- true >/dev/null 2>" + shell_quote(probe_err.string());
    const int ec = run_system(cmd);
    std::error_code ignored;
    fs::remove(probe_err, ignored);
    return ec == 0;
}

std::string tmp_base_name() {
    const long pid = static_cast<long>(::getpid());
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << "/tmp/cpuwl." << pid << "." << now;
    return oss.str();
}

struct RunContext {
    std::string workload;
    std::string pool;
    int trial = 1;
};

std::vector<std::string> make_workload_cmd(const Config& cfg, const std::string& workload, const std::string& pool) {
    if (workload == "matrix") {
        return { (cfg.build_dir / "matrix_mul_bench").string(), pool,
                 std::to_string(cfg.matrix_n), std::to_string(cfg.matrix_bs),
                 std::to_string(cfg.threads), std::to_string(cfg.warmup),
                 std::to_string(cfg.reps) };
    }
    if (workload == "fib") {
        return { (cfg.build_dir / "fib_bench").string(), pool,
                 std::to_string(cfg.fib_n), std::to_string(cfg.threads),
                 std::to_string(cfg.warmup), std::to_string(cfg.reps),
                 std::to_string(cfg.fib_tasks), std::to_string(cfg.fib_split_threshold) };
    }
    if (workload == "fib_single") {
        return { (cfg.build_dir / "fib_single_bench").string(), pool,
                 std::to_string(cfg.fib_single_n), std::to_string(cfg.threads),
                 std::to_string(cfg.warmup), std::to_string(cfg.reps),
                 std::to_string(cfg.fib_single_split_threshold) };
    }
    return { (cfg.build_dir / "fib_fast_bench").string(), pool,
             std::to_string(cfg.fib_fast_n), std::to_string(cfg.threads),
             std::to_string(cfg.warmup), std::to_string(cfg.reps),
             std::to_string(cfg.fib_fast_tasks) };
}

void append_run_row(std::ofstream& csv, const Config& cfg, bool perf_enabled, const RunContext& rcx) {
    const auto cmd_argv = make_workload_cmd(cfg, rcx.workload, rcx.pool);
    const std::string cmd_raw = join_cmd_raw(cmd_argv);
    const std::string cmd_shell = join_cmd_shell(cmd_argv);

    const std::string base = tmp_base_name();
    const fs::path bench_out = base + ".bench.out";
    const fs::path bench_err = base + ".bench.err";
    const fs::path time_out = base + ".time.out";
    const fs::path perf_out = base + ".perf.out";

    const std::string time_fmt =
        "elapsed_s=%e\\n"
        "user_s=%U\\n"
        "sys_s=%S\\n"
        "cpu_pct=%P\\n"
        "max_rss_kb=%M\\n"
        "avg_rss_kb=%t\\n"
        "voluntary_cs=%w\\n"
        "involuntary_cs=%c";

    std::string command;
    if (perf_enabled) {
        command =
            "/usr/bin/time -f " + shell_quote(time_fmt) +
            " -o " + shell_quote(time_out.string()) +
            " perf stat -x, -e " + shell_quote("task-clock,context-switches,cpu-migrations,cycles,instructions,cache-misses") +
            " -o " + shell_quote(perf_out.string()) +
            " -- " + cmd_shell +
            " >" + shell_quote(bench_out.string()) +
            " 2>" + shell_quote(bench_err.string());
    } else {
        command =
            "/usr/bin/time -f " + shell_quote(time_fmt) +
            " -o " + shell_quote(time_out.string()) +
            " " + cmd_shell +
            " >" + shell_quote(bench_out.string()) +
            " 2>" + shell_quote(bench_err.string());
    }

    const int ec = run_system(command);
    std::string status = ec == 0 ? "ok" : ("exit_" + std::to_string(ec));

    const BenchParsed bp = parse_bench_output(bench_out);
    const auto tkv = parse_kv_file(time_out);

    auto get_t = [&](const std::string& k) {
        auto it = tkv.find(k);
        return it == tkv.end() ? std::string("NA") : or_na(it->second);
    };

    std::string elapsed_s = get_t("elapsed_s");
    std::string user_s = get_t("user_s");
    std::string sys_s = get_t("sys_s");
    std::string cpu_pct = get_t("cpu_pct");
    if (!cpu_pct.empty() && cpu_pct.back() == '%') {
        cpu_pct.pop_back();
    }
    std::string max_rss_kb = get_t("max_rss_kb");
    std::string avg_rss_kb = get_t("avg_rss_kb");
    std::string vol_cs = get_t("voluntary_cs");
    std::string invol_cs = get_t("involuntary_cs");

    std::string perf_task_clock_ms = "NA";
    std::string perf_context_switches = "NA";
    std::string perf_cpu_migrations = "NA";
    std::string perf_cycles = "NA";
    std::string perf_instructions = "NA";
    std::string perf_cache_misses = "NA";
    std::string perf_cpus_utilized = "NA";

    if (perf_enabled) {
        perf_task_clock_ms = parse_perf_event_value(perf_out, "task-clock");
        perf_context_switches = parse_perf_event_value(perf_out, "context-switches");
        perf_cpu_migrations = parse_perf_event_value(perf_out, "cpu-migrations");
        perf_cycles = parse_perf_event_value(perf_out, "cycles");
        perf_instructions = parse_perf_event_value(perf_out, "instructions");
        perf_cache_misses = parse_perf_event_value(perf_out, "cache-misses");

        auto task_ms = parse_double(perf_task_clock_ms);
        auto elapsed = parse_double(elapsed_s);
        if (task_ms && elapsed && *elapsed > 0.0) {
            perf_cpus_utilized = format_fixed((*task_ms / 1000.0) / (*elapsed), 6);
        }
    }

    std::string throughput = "NA";
    std::string gflops = "NA";

    if (bp.avg != "NA") {
        if (rcx.workload == "fib") {
            throughput = safe_div_str(std::to_string(cfg.fib_tasks), bp.avg);
        } else if (rcx.workload == "fib_fast") {
            throughput = safe_div_str(std::to_string(cfg.fib_fast_tasks), bp.avg);
        } else if (rcx.workload == "fib_single") {
            throughput = safe_div_str("1", bp.avg);
        } else if (rcx.workload == "matrix") {
            const int tiles_per_dim = (cfg.matrix_n + cfg.matrix_bs - 1) / cfg.matrix_bs;
            const int tile_tasks = tiles_per_dim * tiles_per_dim;
            throughput = safe_div_str(std::to_string(tile_tasks), bp.avg);
            auto avg = parse_double(bp.avg);
            if (avg && *avg > 0.0) {
                const double n = static_cast<double>(cfg.matrix_n);
                const double val = (2.0 * n * n * n) / (*avg) / 1e9;
                gflops = format_fixed(val, 6);
            }
        }
    }

    std::string matrix_n = "NA", matrix_bs = "NA";
    std::string fib_n = "NA", fib_tasks = "NA", fib_split_threshold = "NA";
    std::string fib_single_n = "NA", fib_single_split_threshold = "NA";
    std::string fib_fast_n = "NA", fib_fast_tasks = "NA";

    if (rcx.workload == "matrix") {
        matrix_n = std::to_string(cfg.matrix_n);
        matrix_bs = std::to_string(cfg.matrix_bs);
    } else if (rcx.workload == "fib") {
        fib_n = std::to_string(cfg.fib_n);
        fib_tasks = std::to_string(cfg.fib_tasks);
        fib_split_threshold = std::to_string(cfg.fib_split_threshold);
    } else if (rcx.workload == "fib_single") {
        fib_single_n = std::to_string(cfg.fib_single_n);
        fib_single_split_threshold = std::to_string(cfg.fib_single_split_threshold);
    } else if (rcx.workload == "fib_fast") {
        fib_fast_n = std::to_string(cfg.fib_fast_n);
        fib_fast_tasks = std::to_string(cfg.fib_fast_tasks);
    }

    const std::string stderr_snippet = read_stderr_snippet(bench_err);

    write_csv_row(csv, {
        timestamp_iso(), rcx.workload, rcx.pool, std::to_string(rcx.trial), status, cmd_raw,
        std::to_string(cfg.threads), std::to_string(cfg.warmup), std::to_string(cfg.reps),
        matrix_n, matrix_bs, fib_n, fib_tasks, fib_split_threshold, fib_single_n, fib_single_split_threshold, fib_fast_n, fib_fast_tasks,
        or_na(bp.run_times), or_na(bp.p50), or_na(bp.p95), or_na(bp.p99), or_na(bp.best), or_na(bp.avg),
        or_na(throughput), or_na(gflops),
        or_na(bp.checksum), or_na(bp.expected_checksum), or_na(bp.fib_value), or_na(bp.spawned_internal_nodes),
        or_na(elapsed_s), or_na(user_s), or_na(sys_s), or_na(cpu_pct), or_na(max_rss_kb), or_na(avg_rss_kb), or_na(vol_cs), or_na(invol_cs),
        perf_enabled ? "1" : "0", or_na(perf_task_clock_ms), or_na(perf_context_switches), or_na(perf_cpu_migrations), or_na(perf_cycles), or_na(perf_instructions), or_na(perf_cache_misses), or_na(perf_cpus_utilized),
        stderr_snippet
    });

    std::error_code ignored;
    fs::remove(bench_out, ignored);
    fs::remove(bench_err, ignored);
    fs::remove(time_out, ignored);
    fs::remove(perf_out, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    ::setenv("LC_ALL", "C", 1);

    const Config cfg = load_config(argc, argv);

    std::error_code ec;
    fs::create_directories(cfg.build_dir, ec);
    if (ec) {
        std::cerr << "Failed to create build dir: " << cfg.build_dir << " (" << ec.message() << ")\n";
        return 1;
    }
    fs::create_directories(cfg.output_csv.parent_path(), ec);
    if (ec) {
        std::cerr << "Failed to create output dir: " << cfg.output_csv.parent_path() << " (" << ec.message() << ")\n";
        return 1;
    }

    if (!compile_benchmarks(cfg)) {
        return 1;
    }

    const bool perf_enabled = detect_perf_enabled();

    std::ofstream csv(cfg.output_csv);
    if (!csv) {
        std::cerr << "Failed to open CSV: " << cfg.output_csv << "\n";
        return 1;
    }

    write_csv_row(csv, {
        "timestamp", "workload", "pool", "trial", "status", "command",
        "threads", "warmup", "reps",
        "matrix_n", "matrix_bs", "fib_n", "fib_tasks", "fib_split_threshold", "fib_single_n", "fib_single_split_threshold", "fib_fast_n", "fib_fast_tasks",
        "run_times_s", "latency_p50_s", "latency_p95_s", "latency_p99_s", "app_best_s", "app_avg_s",
        "throughput_tasks_per_s", "gflops",
        "checksum", "expected_checksum", "fib_value", "spawned_internal_nodes",
        "elapsed_s", "user_s", "sys_s", "cpu_pct", "max_rss_kb", "avg_rss_kb", "voluntary_cs", "involuntary_cs",
        "perf_enabled", "perf_task_clock_ms", "perf_context_switches", "perf_cpu_migrations", "perf_cycles", "perf_instructions", "perf_cache_misses", "perf_cpus_utilized",
        "stderr"
    });

    const std::vector<std::string> pools = {"classic", "ws", "elastic", "advws"};
    const std::vector<std::string> workloads = {"matrix", "fib", "fib_single", "fib_fast"};

    std::cout << "Perf enabled: " << (perf_enabled ? 1 : 0) << "\n";
    std::cout << "Writing metrics to: " << cfg.output_csv << "\n";
    std::cout << "Workloads: matrix fib fib_single fib_fast\n";
    std::cout << "Pools: classic ws elastic advws\n";
    std::cout << "Trials per workload+pool: " << cfg.trials << "\n";

    for (const auto& workload : workloads) {
        for (const auto& pool : pools) {
            for (int trial = 1; trial <= cfg.trials; ++trial) {
                std::cout << "Running workload=" << workload
                          << " pool=" << pool
                          << " trial=" << trial << "/" << cfg.trials << "\n";
                append_run_row(csv, cfg, perf_enabled, RunContext{workload, pool, trial});
            }
        }
    }

    std::cout << "Done. CSV saved at: " << cfg.output_csv << "\n";
    return 0;
}
