#!/usr/bin/env bash
set -euo pipefail

# Run matrix_chain_bench across all pool modes and collect:
# - benchmark-reported runtime metrics (Best/Avg)
# - /usr/bin/time -v metrics
# - perf stat metrics (if perf is available)
#
# Usage:
#   ./run_matrix_chain_all_perf_stats.sh
#
# Optional env overrides:
#   THREADS_LIST="1 2 4 8 16"
#   THREADS=8   (backward-compatible single value if THREADS_LIST is unset)
#   WARMUP=1
#   REPS=3
#   MODES="classic coro ws elastic advws"
#   PERF_EVENTS="task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BASE="$ROOT_DIR/results"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="$OUT_BASE/matrix_chain_perf_$TIMESTAMP"
CSV_FILE="$OUT_DIR/summary.csv"

THREADS_LIST="${THREADS_LIST:-${THREADS:-8}}"
WARMUP="${WARMUP:-1}"
REPS="${REPS:-3}"
MODES_STRING="${MODES:-classic coro ws elastic advws}"
PERF_EVENTS="${PERF_EVENTS:-task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses}"

MATRIX_CHAIN_BIN="$ROOT_DIR/matrix_chain_bench"

mkdir -p "$OUT_DIR"

build_if_needed() {
  if [[ ! -x "$MATRIX_CHAIN_BIN" || \
        "$ROOT_DIR/matrix_chain_bench.cpp" -nt "$MATRIX_CHAIN_BIN" || \
        "$ROOT_DIR/thread_pool.cpp" -nt "$MATRIX_CHAIN_BIN" || \
        "$ROOT_DIR/thread_pool.h" -nt "$MATRIX_CHAIN_BIN" || \
        "$ROOT_DIR/coro_runtime.h" -nt "$MATRIX_CHAIN_BIN" ]]; then
    echo "[build] matrix_chain_bench"
    g++ -O3 -std=c++20 -pthread "$ROOT_DIR/matrix_chain_bench.cpp" "$ROOT_DIR/thread_pool.cpp" -o "$MATRIX_CHAIN_BIN"
  fi
}

extract_after_prefix() {
  local file="$1"
  local prefix="$2"
  awk -v p="$prefix" '
    {
      line = $0
      sub(/^[[:space:]]+/, "", line)
      if (index(line, p) == 1) {
        line = substr(line, length(p) + 1)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
        print line
        exit
      }
    }
  ' "$file"
}

extract_best_s() {
  local file="$1"
  awk '/^Best:/ {print $2; exit}' "$file"
}

extract_avg_s() {
  local file="$1"
  awk '/^Avg :/ {print $3; exit}' "$file"
}

extract_checksum() {
  local file="$1"
  awk '/^Checksum:/ {print $2; exit}' "$file"
}

extract_gflops() {
  local file="$1"
  awk '/^Effective GFLOP\/s \(best\):/ {print $4; exit}' "$file"
}

elapsed_to_seconds() {
  local raw="$1"
  awk -v t="$raw" '
    BEGIN {
      n = split(t, a, ":")
      if (n == 2) {
        printf "%.6f", (a[1] * 60.0 + a[2])
      } else if (n == 3) {
        printf "%.6f", (a[1] * 3600.0 + a[2] * 60.0 + a[3])
      }
    }
  '
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

run_one() {
  local preset="$1"
  local mode="$2"
  local n="$3"
  local bs="$4"
  local chain_len="$5"
  local threads="$6"

  local bench_log="$OUT_DIR/bench_matrix_chain_${mode}_${preset}_t${threads}.log"
  local time_log="$OUT_DIR/time_matrix_chain_${mode}_${preset}_t${threads}.log"
  local perf_log="$OUT_DIR/perf_matrix_chain_${mode}_${preset}_t${threads}.log"

  local -a args=("$mode" "$n" "$bs" "$chain_len" "$threads" "$WARMUP" "$REPS")

  echo "[run] mode=$mode preset=$preset N=$n BS=$bs chain_len=$chain_len threads=$threads warmup=$WARMUP reps=$REPS"

  set +e
  if [[ "$HAS_PERF" == "1" ]]; then
    /usr/bin/time -v -o "$time_log" \
      perf stat --no-big-num -x, -e "$PERF_EVENTS" -o "$perf_log" -- \
      "$MATRIX_CHAIN_BIN" "${args[@]}" >"$bench_log" 2>&1
    rc=$?
  else
    printf "perf not found in PATH\n" > "$perf_log"
    /usr/bin/time -v -o "$time_log" -- \
      "$MATRIX_CHAIN_BIN" "${args[@]}" >"$bench_log" 2>&1
    rc=$?
  fi
  set -e

  local best=""
  local avg=""
  local checksum=""
  local gflops=""
  local user_s=""
  local sys_s=""
  local cpu_pct=""
  local elapsed_raw=""
  local elapsed_s=""
  local max_rss=""
  local vol_cs=""
  local invol_cs=""
  local minor_pf=""
  local major_pf=""

  local perf_task_clock=""
  local perf_context_switches=""
  local perf_cpu_migrations=""
  local perf_page_faults=""
  local perf_cycles=""
  local perf_instructions=""
  local perf_branches=""
  local perf_branch_misses=""
  local perf_cache_refs=""
  local perf_cache_misses=""

  if [[ -f "$bench_log" ]]; then
    best="$(extract_best_s "$bench_log")"
    avg="$(extract_avg_s "$bench_log")"
    checksum="$(extract_checksum "$bench_log")"
    gflops="$(extract_gflops "$bench_log")"
  fi

  if [[ -f "$time_log" ]]; then
    user_s="$(extract_after_prefix "$time_log" "User time (seconds):")"
    sys_s="$(extract_after_prefix "$time_log" "System time (seconds):")"
    cpu_pct="$(extract_after_prefix "$time_log" "Percent of CPU this job got:")"
    elapsed_raw="$(extract_after_prefix "$time_log" "Elapsed (wall clock) time (h:mm:ss or m:ss):")"
    if [[ -n "$elapsed_raw" ]]; then
      elapsed_s="$(elapsed_to_seconds "$elapsed_raw")"
    fi
    max_rss="$(extract_after_prefix "$time_log" "Maximum resident set size (kbytes):")"
    vol_cs="$(extract_after_prefix "$time_log" "Voluntary context switches:")"
    invol_cs="$(extract_after_prefix "$time_log" "Involuntary context switches:")"
    minor_pf="$(extract_after_prefix "$time_log" "Minor (reclaiming a frame) page faults:")"
    major_pf="$(extract_after_prefix "$time_log" "Major (requiring I/O) page faults:")"
  fi

  if [[ "$HAS_PERF" == "1" && -f "$perf_log" ]]; then
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

  echo "matrix_chain_bench,$preset,$mode,$n,$bs,$chain_len,$threads,$WARMUP,$REPS,$rc,${best:-},${avg:-},${checksum:-},${gflops:-},${elapsed_s:-},${elapsed_raw:-},${user_s:-},${sys_s:-},${cpu_pct:-},${max_rss:-},${vol_cs:-},${invol_cs:-},${minor_pf:-},${major_pf:-},$HAS_PERF,${perf_task_clock:-},${perf_context_switches:-},${perf_cpu_migrations:-},${perf_page_faults:-},${perf_cycles:-},${perf_instructions:-},${perf_branches:-},${perf_branch_misses:-},${perf_cache_refs:-},${perf_cache_misses:-},$bench_log,$time_log,$perf_log" >> "$CSV_FILE"
}

build_if_needed

if command -v perf >/dev/null 2>&1 && perf stat -e task-clock -- true >/dev/null 2>&1; then
  HAS_PERF=1
else
  HAS_PERF=0
  echo "[warn] perf unavailable or not permitted; perf columns in CSV will be blank." >&2
fi

echo "bench,preset,mode,n,block_size,chain_len,threads,warmup,reps,exit_code,bench_best_s,bench_avg_s,checksum,gflops_best,time_elapsed_s,time_elapsed_raw,time_user_s,time_sys_s,time_cpu_pct,time_max_rss_kb,time_voluntary_cs,time_involuntary_cs,time_minor_pf,time_major_pf,perf_available,perf_task_clock,perf_context_switches,perf_cpu_migrations,perf_page_faults,perf_cycles,perf_instructions,perf_branches,perf_branch_misses,perf_cache_references,perf_cache_misses,bench_log,time_log,perf_log" > "$CSV_FILE"

declare -a MODES=($MODES_STRING)
declare -a THREAD_COUNTS=($THREADS_LIST)

# preset format:
#   name N BS chain_len
declare -a CHAIN_PRESETS=(
  "mc_light 384 32 4"
  "mc_mid 640 64 5"
  "mc_heavy 896 64 6"
)

for threads in "${THREAD_COUNTS[@]}"; do
  for mode in "${MODES[@]}"; do
    for preset in "${CHAIN_PRESETS[@]}"; do
      read -r name n bs chain_len <<< "$preset"
      run_one "$name" "$mode" "$n" "$bs" "$chain_len" "$threads"
    done
  done
done

echo "Done. Summary: $CSV_FILE"
echo "Logs dir: $OUT_DIR"
