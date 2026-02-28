#!/usr/bin/env bash
set -euo pipefail

# Run the mixed /work benchmark with wrk across all server modes.
# Usage:
#   ./run_mixed_wrk_all_pools.sh
#   ./run_mixed_wrk_all_pools.sh <port> <host>
#
# Optional env overrides:
#   WRK_THREADS=4
#   WRK_TIMEOUT=15s
#   MODES="classic coro ws elastic advws"

PORT="${1:-8080}"
HOST="${2:-127.0.0.1}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_BIN="$ROOT_DIR/mini_http_server"
WRK_SCRIPT="$ROOT_DIR/wrk_workload.lua"

WRK_THREADS="${WRK_THREADS:-4}"
WRK_TIMEOUT="${WRK_TIMEOUT:-15s}"
MODES_STRING="${MODES:-classic coro ws elastic advws}"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/results/mixed_wrk_$TIMESTAMP"
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

extract_field_after_colon() {
  local file="$1"
  local prefix="$2"
  awk -v p="$prefix" '
    index($0, p) == 1 {
      sub(p, "", $0)
      gsub(/^ +| +$/, "", $0)
      print $0
      exit
    }
  ' "$file"
}

extract_latency_pct() {
  local file="$1"
  local pct="$2"
  awk -v target="$pct" '
    $1 == target {
      gsub(/^ +| +$/, "", $2)
      print $2
      exit
    }
  ' "$file"
}

extract_socket_errors() {
  local file="$1"
  awk '
    /^  Socket errors:/ {
      line = $0
      sub(/^  Socket errors: /, "", line)
      gsub(/, /, ";", line)
      print line
      exit
    }
  ' "$file"
}

build_if_needed

if ! command -v wrk >/dev/null 2>&1; then
  echo "wrk is required but was not found in PATH." >&2
  exit 1
fi

if [[ ! -f "$WRK_SCRIPT" ]]; then
  echo "Missing wrk script: $WRK_SCRIPT" >&2
  exit 1
fi

echo "mode,preset,cpu1_us,io_us,cpu2_us,connections,duration,wrk_threads,lat_avg,lat_stdev,lat_max,req_sec_avg,req_sec_stdev,req_sec_max,p50,p75,p90,p99,requests,total_read,requests_sec,transfer_sec,socket_errors,raw_log" > "$CSV_FILE"

declare -a MODES=($MODES_STRING)
declare -a PRESETS=(
  "p1_light_cpu_heavy_io 100 8000 100 16 8s"
  "p2_balanced 200 5000 200 32 10s"
  "p3_cpu_heavy 1200 200 1200 32 10s"
  "p4_io_heavy_high_conc 100 10000 100 64 12s"
  "p5_stress_mix 600 3000 600 96 12s"
)

trap 'stop_server' EXIT

for mode in "${MODES[@]}"; do
  for preset in "${PRESETS[@]}"; do
    read -r preset_name cpu1 io cpu2 connections duration <<< "$preset"

    server_log="$OUT_DIR/server_${mode}_${preset_name}.log"
    wrk_log="$OUT_DIR/wrk_${mode}_${preset_name}.log"

    echo "[run] mode=$mode preset=$preset_name cpu1=$cpu1 io=$io cpu2=$cpu2 conns=$connections duration=$duration"

    stop_server
    start_server "$mode" "$server_log"

    set +e
    wrk -t"$WRK_THREADS" -c"$connections" -d"$duration" \
      --timeout "$WRK_TIMEOUT" --latency \
      -s "$WRK_SCRIPT" "http://$HOST:$PORT" -- "$cpu1" "$io" "$cpu2" \
      >"$wrk_log" 2>&1
    rc=$?
    set -e

    stop_server

    if [[ $rc -ne 0 ]]; then
      echo "[warn] wrk failed for mode=$mode preset=$preset_name (exit=$rc). See $wrk_log" >&2
      echo "$mode,$preset_name,$cpu1,$io,$cpu2,$connections,$duration,$WRK_THREADS,,,,,,,,,,,,,,,$wrk_log" >> "$CSV_FILE"
      continue
    fi

    lat_line="$(awk '$1 == "Latency" {print $2 "," $3 "," $4; exit}' "$wrk_log")"
    req_line="$(awk '$1 == "Req/Sec" {print $2 "," $3 "," $4; exit}' "$wrk_log")"

    IFS=',' read -r lat_avg lat_stdev lat_max <<< "${lat_line:-,,}"
    IFS=',' read -r req_avg req_stdev req_max <<< "${req_line:-,,}"

    p50="$(extract_latency_pct "$wrk_log" "50%")"
    p75="$(extract_latency_pct "$wrk_log" "75%")"
    p90="$(extract_latency_pct "$wrk_log" "90%")"
    p99="$(extract_latency_pct "$wrk_log" "99%")"
    requests="$(awk '/ requests in / {print $1; exit}' "$wrk_log")"
    total_read="$(awk '/ requests in / {gsub(/,/, "", $4); print $4; exit}' "$wrk_log")"
    requests_sec="$(extract_field_after_colon "$wrk_log" "Requests/sec:")"
    transfer_sec="$(extract_field_after_colon "$wrk_log" "Transfer/sec:")"
    socket_errors="$(extract_socket_errors "$wrk_log")"

    echo "$mode,$preset_name,$cpu1,$io,$cpu2,$connections,$duration,$WRK_THREADS,${lat_avg:-},${lat_stdev:-},${lat_max:-},${req_avg:-},${req_stdev:-},${req_max:-},${p50:-},${p75:-},${p90:-},${p99:-},${requests:-},${total_read:-},${requests_sec:-},${transfer_sec:-},${socket_errors:-},$wrk_log" >> "$CSV_FILE"
  done
done

echo "Done. Summary: $CSV_FILE"
echo "Logs dir: $OUT_DIR"
