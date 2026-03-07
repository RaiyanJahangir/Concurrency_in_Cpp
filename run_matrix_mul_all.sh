bash <<'BASH'
set -euo pipefail

# Build
echo "[build] matrix_mul_bench"
g++ -O3 -std=c++20 -pthread matrix_mul_bench.cpp thread_pool.cpp -o matrix_mul_bench

TS=$(date +%Y%m%d_%H%M%S)
OUT="results/matrix_mul_perf_$TS"
mkdir -p "$OUT"

# Config
MODES=(classic coro ws elastic advws)
THREADS=(1 2 4 8 16)
NS=(1024 2048 4096)   # light, mid, heavy
BS=64
WARMUP=1
REPS=3

TOTAL=$(( ${#MODES[@]} * ${#THREADS[@]} * ${#NS[@]} ))
RUN=0

echo "preset,mode,n,block_size,threads,warmup,reps,exit_code,duration_s,best_s,avg_s,checksum,log" > "$OUT/summary.csv"

for N in "${NS[@]}"; do
  case "$N" in
    1024) preset="light" ;;
    2048) preset="mid" ;;
    4096) preset="heavy" ;;
  esac

  for mode in "${MODES[@]}"; do
    for t in "${THREADS[@]}"; do
      RUN=$((RUN+1))
      PCT=$((RUN * 100 / TOTAL))
      log="$OUT/matrix_mul_${preset}_${mode}_t${t}.log"

      echo "[run $RUN/$TOTAL | ${PCT}%] preset=$preset mode=$mode N=$N BS=$BS threads=$t warmup=$WARMUP reps=$REPS"
      start=$(date +%s)

      set +e
      ./matrix_mul_bench "$mode" "$N" "$BS" "$t" "$WARMUP" "$REPS" > "$log" 2>&1
      rc=$?
      set -e

      end=$(date +%s)
      dur=$((end - start))

      best=$(awk '/^Best:/ {print $2; exit}' "$log")
      avg=$(awk '/^Avg :/ {print $3; exit}' "$log")
      checksum=$(awk '/^Checksum:/ {print $2; exit}' "$log")

      if [[ "$rc" -eq 0 ]]; then
        echo "[done $RUN/$TOTAL] OK  ${dur}s  log=$log"
      else
        echo "[done $RUN/$TOTAL] FAIL(rc=$rc) ${dur}s  log=$log"
      fi

      echo "$preset,$mode,$N,$BS,$t,$WARMUP,$REPS,$rc,$dur,${best:-},${avg:-},${checksum:-},$log" >> "$OUT/summary.csv"
    done
  done
done

echo "All runs complete."
echo "Saved: $OUT"
echo "Summary: $OUT/summary.csv"
BASH
