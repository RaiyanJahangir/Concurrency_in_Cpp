#!/usr/bin/env bash
set -euo pipefail

# Run mixed workload benchmark across all server modes (including coroutine mode).
# Usage:
#   ./run_mixed_all_pools.sh
#   ./run_mixed_all_pools.sh <port> <host>
# Example:
#   ./run_mixed_all_pools.sh 8080 127.0.0.1

PORT="${1:-8080}"
HOST="${2:-127.0.0.1}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_BIN="$ROOT_DIR/mini_http_server"
BENCH_BIN="$ROOT_DIR/mixed_bench"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/results/mixed_bench_$TIMESTAMP"
CSV_FILE="$OUT_DIR/summary.csv"

mkdir -p "$OUT_DIR"

build_if_needed() {
  if [[ ! -x "$SERVER_BIN" || \
        "$ROOT_DIR/mini_http_server.cpp" -nt "$SERVER_BIN" || \
        "$ROOT_DIR/thread_pool.cpp" -nt "$SERVER_BIN" || \
        "$ROOT_DIR/thread_pool.h" -nt "$SERVER_BIN" || \
        "$ROOT_DIR/coro_runtime.h" -nt "$SERVER_BIN" ]]; then
    echo "[build] mini_http_server"
    g++ -O2 -std=c++20 -pthread "$ROOT_DIR/mini_http_server.cpp" "$ROOT_DIR/thread_pool.cpp" -o "$SERVER_BIN"
  fi

  if [[ ! -x "$BENCH_BIN" || "$ROOT_DIR/mixed_bench.cpp" -nt "$BENCH_BIN" ]]; then
    echo "[build] mixed_bench"
    g++ -O2 -std=c++20 -pthread "$ROOT_DIR/mixed_bench.cpp" -o "$BENCH_BIN"
  fi
}

start_server() {
  local mode="$1"
  local log_file="$2"

  case "$mode" in
    classic)
      "$SERVER_BIN" classic "$PORT" 8 >"$log_file" 2>&1 &
      ;;
    coro)
      "$SERVER_BIN" coro "$PORT" 8 >"$log_file" 2>&1 &
      ;;
    ws)
      "$SERVER_BIN" ws "$PORT" 8 >"$log_file" 2>&1 &
      ;;
    elastic)
      "$SERVER_BIN" elastic "$PORT" 4 32 >"$log_file" 2>&1 &
      ;;
    advws)
      "$SERVER_BIN" advws "$PORT" 4 32 50 >"$log_file" 2>&1 &
      ;;
    *)
      echo "Unknown mode: $mode" >&2
      exit 1
      ;;
  esac

  SERVER_PID=$!

  # Wait until the server is ready (best effort).
  for _ in {1..50}; do
    if command -v nc >/dev/null 2>&1; then
      if nc -z "$HOST" "$PORT" >/dev/null 2>&1; then
        return 0
      fi
    else
      if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
        break
      fi
    fi
    sleep 0.1
  done

  echo "Server failed to start for mode=$mode. See $log_file" >&2
  return 1
}

stop_server() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  SERVER_PID=""
}

extract_metric() {
  local file="$1"
  local key="$2"
  awk -v k="$key" '
    {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ "^" k "=") {
          split($i, a, "=")
          gsub(/[^0-9.]/, "", a[2])
          print a[2]
          exit
        }
      }
    }
  ' "$file"
}

build_if_needed

echo "mode,preset,cpu1_us,io_us,cpu2_us,concurrency,duration_s,ok,fail,throughput,avg_ms,p50_ms,p95_ms,p99_ms,raw_log" > "$CSV_FILE"

declare -a MODES=(classic coro ws elastic advws)
# 5 workload presets: cpu1 io cpu2 concurrency duration
# You can tweak these values as needed.
declare -a PRESETS=(
  "p1_light_cpu_heavy_io 100 8000 100 16 8"
  "p2_balanced 200 5000 200 32 10"
  "p3_cpu_heavy 1200 200 1200 32 10"
  "p4_io_heavy_high_conc 100 10000 100 64 12"
  "p5_stress_mix 600 3000 600 96 12"
)

trap 'stop_server' EXIT

for mode in "${MODES[@]}"; do
  for preset in "${PRESETS[@]}"; do
    read -r preset_name cpu1 io cpu2 conc dur <<< "$preset"

    server_log="$OUT_DIR/server_${mode}_${preset_name}.log"
    bench_log="$OUT_DIR/bench_${mode}_${preset_name}.log"

    echo "[run] mode=$mode preset=$preset_name cpu1=$cpu1 io=$io cpu2=$cpu2 conc=$conc dur=$dur"

    stop_server
    start_server "$mode" "$server_log"

    set +e
    "$BENCH_BIN" "$HOST" "$PORT" "$cpu1" "$io" "$cpu2" "$conc" "$dur" >"$bench_log" 2>&1
    rc=$?
    set -e

    stop_server

    if [[ $rc -ne 0 ]]; then
      echo "[warn] benchmark failed for mode=$mode preset=$preset_name (exit=$rc). See $bench_log" >&2
      echo "$mode,$preset_name,$cpu1,$io,$cpu2,$conc,$dur,,,,,,,,$bench_log" >> "$CSV_FILE"
      continue
    fi

    ok="$(awk -F'[:|]' '/^OK:/ {gsub(/ /,"",$2); print $2; exit}' "$bench_log")"
    fail="$(awk -F'[:|]' '/^OK:/ {gsub(/ /,"",$4); print $4; exit}' "$bench_log")"
    thr="$(awk -F'[:|]' '/^OK:/ {gsub(/ /,"",$6); print $6; exit}' "$bench_log")"
    avg="$(extract_metric "$bench_log" "avg")"
    p50="$(extract_metric "$bench_log" "p50")"
    p95="$(extract_metric "$bench_log" "p95")"
    p99="$(extract_metric "$bench_log" "p99")"

    echo "$mode,$preset_name,$cpu1,$io,$cpu2,$conc,$dur,${ok:-},${fail:-},${thr:-},${avg:-},${p50:-},${p95:-},${p99:-},$bench_log" >> "$CSV_FILE"
  done
done

echo "Done. Summary: $CSV_FILE"
echo "Logs dir: $OUT_DIR"
