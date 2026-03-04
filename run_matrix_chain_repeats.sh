#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Build once
g++ -O3 -std=c++20 -pthread matrix_chain_bench.cpp thread_pool.cpp -o matrix_chain_bench

# Repeat full sweep multiple times (each run creates a new timestamped results dir)
REPEATS="${REPEATS:-3}"

for i in $(seq 1 "$REPEATS"); do
  echo "=== Sweep $i / $REPEATS ==="

  THREADS_LIST="${THREADS_LIST:-1 2 4 8 16}" \
  WARMUP="${WARMUP:-1}" \
  REPS="${REPS:-3}" \
  MODES="${MODES:-classic coro ws elastic advws}" \
  ./run_matrix_chain_all_perf_stats.sh
done

echo "Done. Check: results/matrix_chain_perf_*"
