#!/usr/bin/env bash
# G0: baseline + authors' sPGM (black box) on history_200M, 3 runs each, under /usr/bin/time -v.
# Each sPGM config gets its own binary so each process holds one index -> per-config peak RSS.
# Run from the repo root: bash results/g0/run_g0.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=results/g0
DATA=./data/history_200M_uint64
LOOKUPS=./data/history_200M_uint64_equality_lookups_10M
EPS=256
SPGM_H=benchmarks/benchmark_spgm.h
mkdir -p "$OUT" build/g0bin

# Build one binary per I. The USER block is restored to the 4-config version afterwards.
cp "$SPGM_H" "$OUT/benchmark_spgm.h.orig"
for I in 1 16 64 256; do
  awk -v I="$I" -v E="$EPS" '
    /#ifdef USER/ && !done {print; print "  benchmark.template Run<sPGM<uint64_t, " E ", " I ", 4, 1>>();"; skip=1; next}
    skip && /#endif/ {skip=0; done=1}
    !skip {print}' "$OUT/benchmark_spgm.h.orig" > "$SPGM_H"
  (cd build && make -j4 benchmark > /dev/null)
  cp build/benchmark "build/g0bin/benchmark_spgm_I$I"
done
cp "$OUT/benchmark_spgm.h.orig" "$SPGM_H" && rm "$OUT/benchmark_spgm.h.orig"
(cd build && make -j4 benchmark > /dev/null)
cp build/benchmark build/g0bin/benchmark_bs

run() {  # $1=label $2=binary $3=only-tag $4=run
  local log="$OUT/${1}_run$4.log"
  echo "== $1 run $4"
  /usr/bin/time -v "$2" "$DATA" "$LOOKUPS" --only "$3" > "$log" 2>&1
}
for r in 1 2 3; do
  run BS build/g0bin/benchmark_bs BS "$r"
  for I in 1 16 64 256; do
    run "sPGM_eps${EPS}_I$I" "build/g0bin/benchmark_spgm_I$I" sPGM "$r"
  done
done

# summary.csv from the RESULT line (name,variant,size_bytes,build_ns,ns_per_lookup) and time -v.
S="$OUT/summary.csv"
echo "index,eps,I,run,build_time_ns,index_size_bytes,avg_lookup_ns,peak_rss_mb" > "$S"
for log in "$OUT"/*_run*.log; do
  base=$(basename "$log" .log); run=${base##*_run}
  res=$(grep '^RESULT:' "$log" | head -1 | sed 's/^RESULT: //')
  IFS=, read -r name variant size build avg <<< "$res"
  rss_kb=$(grep 'Maximum resident set size' "$log" | awk '{print $NF}')
  if [[ $base == BS_* ]]; then eps=NA; I=NA; else
    eps=$(sed -E 's/.*eps([0-9]+)_I([0-9]+).*/\1/' <<< "$base"); I=$(sed -E 's/.*_I([0-9]+).*/\1/' <<< "$base"); fi
  echo "$name,$eps,$I,$run,$build,$size,$avg,$(awk -v k="$rss_kb" 'BEGIN{printf "%.1f", k/1024}')" >> "$S"
done
echo "done -> $S"
