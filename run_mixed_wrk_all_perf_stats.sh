#!/usr/bin/env bash
set -euo pipefail

# Run the mixed /work benchmark with wrk across all server modes and collect:
# - per-run wrk latency / throughput metrics
# - per-run wall-clock runtime
# - server-side perf stat counters (if perf is available and usable)
# - aggregate best/average runtime across repeated runs
#
# Usage:
#   ./run_mixed_wrk_all_perf_stats.sh
#   ./run_mixed_wrk_all_perf_stats.sh <port> <host>
#
# Optional env overrides:
#   WRK_THREADS=4
#   WRK_TIMEOUT=15s
#   MODES="classic coro ws elastic advws"
#   THREADS_LIST="1 2 4 8 16"
#   REPS=3
#   PERF_EVENTS="task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses"

PORT="${1:-8080}"
HOST="${2:-127.0.0.1}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_BIN="$ROOT_DIR/mini_http_server"
WRK_SCRIPT="$ROOT_DIR/wrk_workload.lua"

WRK_THREADS="${WRK_THREADS:-4}"
WRK_TIMEOUT="${WRK_TIMEOUT:-15s}"
MODES_STRING="${MODES:-classic coro ws elastic advws}"
THREADS_LIST="${THREADS_LIST:-1 2 4 8 16}"
REPS="${REPS:-3}"
PERF_EVENTS="${PERF_EVENTS:-task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses}"
PERF_ATTACH_PAD_S="${PERF_ATTACH_PAD_S:-2}"
ADVWS_IDLE_MS="${ADVWS_IDLE_MS:-50}"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$ROOT_DIR/results/mixed_wrk_perf_$TIMESTAMP"
RUNS_CSV="$OUT_DIR/runs.csv"
SUMMARY_CSV="$OUT_DIR/summary.csv"

mkdir -p "$OUT_DIR"

SERVER_PID=""
PERF_PID=""

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

detect_perf() {
  if ! command -v perf >/dev/null 2>&1; then
    HAS_PERF=0
    return
  fi

  local probe_log="$OUT_DIR/perf_probe.log"
  if perf stat --no-big-num -x, -e task-clock -o "$probe_log" -- true >/dev/null 2>&1; then
    HAS_PERF=1
  else
    HAS_PERF=0
    echo "[warn] perf exists but cannot be used in this environment. See $probe_log" >&2
  fi
}

start_server() {
  local mode="$1"
  local threads="$2"
  local log_file="$3"

  case "$mode" in
    classic)
      "$SERVER_BIN" classic "$PORT" "$threads" >"$log_file" 2>&1 &
      ;;
    coro)
      "$SERVER_BIN" coro "$PORT" "$threads" >"$log_file" 2>&1 &
      ;;
    ws)
      "$SERVER_BIN" ws "$PORT" "$threads" >"$log_file" 2>&1 &
      ;;
    elastic)
      "$SERVER_BIN" elastic "$PORT" "$threads" "$threads" >"$log_file" 2>&1 &
      ;;
    advws)
      "$SERVER_BIN" advws "$PORT" "$threads" "$threads" "$ADVWS_IDLE_MS" >"$log_file" 2>&1 &
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

  echo "Server failed to start for mode=$mode threads=$threads. See $log_file" >&2
  return 1
}

start_perf_attach() {
  local perf_log="$1"
  local duration="$2"

  if [[ "${HAS_PERF:-0}" != "1" ]]; then
    printf "perf unavailable\n" > "$perf_log"
    PERF_PID=""
    return 0
  fi

  local duration_s="${duration%s}"
  local perf_window=$((duration_s + PERF_ATTACH_PAD_S))

  perf stat --no-big-num -x, -e "$PERF_EVENTS" -p "$SERVER_PID" -o "$perf_log" \
    sleep "$perf_window" >/dev/null 2>&1 &
  PERF_PID=$!
  sleep 0.2
}

stop_perf_attach() {
  if [[ -n "${PERF_PID:-}" ]] && kill -0 "$PERF_PID" >/dev/null 2>&1; then
    kill -INT "$PERF_PID" >/dev/null 2>&1 || true
    wait "$PERF_PID" 2>/dev/null || true
  fi
  PERF_PID=""
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

extract_perf_value() {
  local file="$1"
  local event="$2"
  awk -F',' -v e="$event" '
    $3 == e {
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $1)
      print $1
      exit
    }
  ' "$file"
}

min_numeric_list() {
  awk '
    NF {
      for (i = 1; i <= NF; ++i) {
        v = $i + 0
        if (!seen || v < best) {
          best = v
          seen = 1
        }
      }
    }
    END {
      if (seen) {
        printf "%.6f", best
      }
    }
  ' <<< "$1"
}

max_numeric_list() {
  awk '
    NF {
      for (i = 1; i <= NF; ++i) {
        v = $i + 0
        if (!seen || v > best) {
          best = v
          seen = 1
        }
      }
    }
    END {
      if (seen) {
        printf "%.6f", best
      }
    }
  ' <<< "$1"
}

avg_numeric_list() {
  awk '
    NF {
      for (i = 1; i <= NF; ++i) {
        sum += ($i + 0)
        count++
      }
    }
    END {
      if (count > 0) {
        printf "%.6f", sum / count
      }
    }
  ' <<< "$1"
}

build_if_needed
detect_perf

if ! command -v wrk >/dev/null 2>&1; then
  echo "wrk is required but was not found in PATH." >&2
  exit 1
fi

if [[ ! -f "$WRK_SCRIPT" ]]; then
  echo "Missing wrk script: $WRK_SCRIPT" >&2
  exit 1
fi

if ! command -v /usr/bin/time >/dev/null 2>&1; then
  echo "/usr/bin/time is required but was not found." >&2
  exit 1
fi

if [[ "${HAS_PERF:-0}" != "1" ]]; then
  echo "[warn] perf metrics will be blank." >&2
fi

echo "mode,threads,preset,run,cpu1_us,io_us,cpu2_us,connections,duration,wrk_threads,exit_code,runtime_s,lat_avg,lat_stdev,lat_max,req_sec_avg,req_sec_stdev,req_sec_max,p50,p75,p90,p99,requests,total_read,requests_sec,transfer_sec,socket_errors,perf_available,perf_task_clock,perf_context_switches,perf_cpu_migrations,perf_page_faults,perf_cycles,perf_instructions,perf_branches,perf_branch_misses,perf_cache_references,perf_cache_misses,server_log,wrk_log,perf_log,time_log" > "$RUNS_CSV"
echo "mode,threads,preset,cpu1_us,io_us,cpu2_us,connections,duration,wrk_threads,reps,successful_runs,best_runtime_s,avg_runtime_s,best_requests_sec,avg_requests_sec,best_latency_avg,avg_latency_avg,run_logs_dir" > "$SUMMARY_CSV"

declare -a MODES=($MODES_STRING)
declare -a THREAD_COUNTS=($THREADS_LIST)
declare -a PRESETS=(
  "p1_light_cpu_heavy_io 100 8000 100 16 8s"
  "p2_balanced 200 5000 200 32 10s"
  "p3_cpu_heavy 1200 200 1200 32 10s"
  "p4_io_heavy_high_conc 100 10000 100 64 12s"
  "p5_stress_mix 600 3000 600 96 12s"
  "p6_ultra_io_bound 50 20000 50 64 12s"
)

trap 'stop_perf_attach; stop_server' EXIT

for threads in "${THREAD_COUNTS[@]}"; do
  for mode in "${MODES[@]}"; do
    for preset in "${PRESETS[@]}"; do
      read -r preset_name cpu1 io cpu2 connections duration <<< "$preset"

      case_dir="$OUT_DIR/${mode}_t${threads}_${preset_name}"
      mkdir -p "$case_dir"

      runtime_values=""
      requests_sec_values=""
      lat_avg_values=""
      success_runs=0

      for run_idx in $(seq 1 "$REPS"); do
        server_log="$case_dir/server_run${run_idx}.log"
        wrk_log="$case_dir/wrk_run${run_idx}.log"
        perf_log="$case_dir/perf_run${run_idx}.log"
        time_log="$case_dir/time_run${run_idx}.log"

        echo "[run] mode=$mode threads=$threads preset=$preset_name run=$run_idx/$REPS cpu1=$cpu1 io=$io cpu2=$cpu2 conns=$connections duration=$duration"

        stop_perf_attach
        stop_server
        start_server "$mode" "$threads" "$server_log"
        start_perf_attach "$perf_log" "$duration"

        set +e
        /usr/bin/time -f "%e" -o "$time_log" \
          wrk -t"$WRK_THREADS" -c"$connections" -d"$duration" \
            --timeout "$WRK_TIMEOUT" --latency \
            -s "$WRK_SCRIPT" "http://$HOST:$PORT" -- "$cpu1" "$io" "$cpu2" \
            >"$wrk_log" 2>&1
        rc=$?
        set -e

        stop_perf_attach
        stop_server

        runtime_s=""
        if [[ -f "$time_log" ]]; then
          runtime_s="$(tr -d '[:space:]' < "$time_log")"
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

        perf_task_clock=""
        perf_context_switches=""
        perf_cpu_migrations=""
        perf_page_faults=""
        perf_cycles=""
        perf_instructions=""
        perf_branches=""
        perf_branch_misses=""
        perf_cache_refs=""
        perf_cache_misses=""

        if [[ "${HAS_PERF:-0}" == "1" && -f "$perf_log" ]]; then
          perf_task_clock="$(extract_perf_value "$perf_log" "task-clock")"
          perf_context_switches="$(extract_perf_value "$perf_log" "context-switches")"
          perf_cpu_migrations="$(extract_perf_value "$perf_log" "cpu-migrations")"
          perf_page_faults="$(extract_perf_value "$perf_log" "page-faults")"
          perf_cycles="$(extract_perf_value "$perf_log" "cycles")"
          perf_instructions="$(extract_perf_value "$perf_log" "instructions")"
          perf_branches="$(extract_perf_value "$perf_log" "branches")"
          perf_branch_misses="$(extract_perf_value "$perf_log" "branch-misses")"
          perf_cache_refs="$(extract_perf_value "$perf_log" "cache-references")"
          perf_cache_misses="$(extract_perf_value "$perf_log" "cache-misses")"
        fi

        echo "$mode,$threads,$preset_name,$run_idx,$cpu1,$io,$cpu2,$connections,$duration,$WRK_THREADS,$rc,${runtime_s:-},${lat_avg:-},${lat_stdev:-},${lat_max:-},${req_avg:-},${req_stdev:-},${req_max:-},${p50:-},${p75:-},${p90:-},${p99:-},${requests:-},${total_read:-},${requests_sec:-},${transfer_sec:-},${socket_errors:-},${HAS_PERF:-0},${perf_task_clock:-},${perf_context_switches:-},${perf_cpu_migrations:-},${perf_page_faults:-},${perf_cycles:-},${perf_instructions:-},${perf_branches:-},${perf_branch_misses:-},${perf_cache_refs:-},${perf_cache_misses:-},$server_log,$wrk_log,$perf_log,$time_log" >> "$RUNS_CSV"

        if [[ $rc -eq 0 ]]; then
          success_runs=$((success_runs + 1))
          if [[ -n "${runtime_s:-}" ]]; then
            runtime_values="${runtime_values} ${runtime_s}"
          fi
          if [[ -n "${requests_sec:-}" ]]; then
            requests_sec_values="${requests_sec_values} ${requests_sec}"
          fi
          if [[ -n "${lat_avg:-}" ]]; then
            lat_avg_values="${lat_avg_values} ${lat_avg}"
          fi
        else
          echo "[warn] wrk failed for mode=$mode threads=$threads preset=$preset_name run=$run_idx (exit=$rc). See $wrk_log" >&2
        fi
      done

      best_runtime_s=""
      avg_runtime_s=""
      best_requests_sec=""
      avg_requests_sec=""
      best_latency_avg=""
      avg_latency_avg=""

      if [[ $success_runs -gt 0 ]]; then
        if [[ -n "${runtime_values// }" ]]; then
          best_runtime_s="$(min_numeric_list "$runtime_values")"
          avg_runtime_s="$(avg_numeric_list "$runtime_values")"
        fi
        if [[ -n "${requests_sec_values// }" ]]; then
          best_requests_sec="$(max_numeric_list "$requests_sec_values")"
          avg_requests_sec="$(avg_numeric_list "$requests_sec_values")"
        fi
        if [[ -n "${lat_avg_values// }" ]]; then
          best_latency_avg="$(min_numeric_list "$lat_avg_values")"
          avg_latency_avg="$(avg_numeric_list "$lat_avg_values")"
        fi
      fi

      echo "$mode,$threads,$preset_name,$cpu1,$io,$cpu2,$connections,$duration,$WRK_THREADS,$REPS,$success_runs,${best_runtime_s:-},${avg_runtime_s:-},${best_requests_sec:-},${avg_requests_sec:-},${best_latency_avg:-},${avg_latency_avg:-},$case_dir" >> "$SUMMARY_CSV"
    done
  done
done

echo "Done. Summary: $SUMMARY_CSV"
echo "Per-run data: $RUNS_CSV"
echo "Logs dir: $OUT_DIR"
