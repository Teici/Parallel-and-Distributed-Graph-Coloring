#!/usr/bin/env bash
set -euo pipefail

BIN="./build/main"
DATA="data"
OUT="results"

mkdir -p "$DATA" "$OUT"

echo "[1/4] Build"
rm -rf build
mkdir -p build
cmake -S . -B build
cmake --build build -j

echo "[2/4] Generate graphs (sizes chosen to finish in reasonable time)"

$BIN --mode gen --type grid --rows 30 --cols 30 --out "$DATA/grid_30x30.txt"

$BIN --mode gen --type random --n 220 --p 0.03  --seed 1 --out "$DATA/rand_220_003_s1.txt"
$BIN --mode gen --type random --n 240 --p 0.035 --seed 2 --out "$DATA/rand_240_035_s2.txt"

$BIN --mode gen --type bipartite --left 250 --right 250 --p 0.015 --seed 7 --out "$DATA/bip_250_250_015_s7.txt"

$BIN --mode gen --type complete --n 45 --out "$DATA/k45.txt"

RUNS=5
THREADS=8
SPLIT=5
NP=4
MAX_SEC=50

cases=(
  "$DATA/grid_30x30.txt 2"
  "$DATA/rand_220_003_s1.txt 4"
  "$DATA/rand_240_035_s2.txt 4"
  "$DATA/bip_250_250_015_s7.txt 2"
  "$DATA/k45.txt 10"
)

echo "[3/4] Bench (saving CSV to $OUT)"

run_mpi_csv() {
  local file="$1"
  local k="$2"
  local base="$3"
  local out_csv="$OUT/${base}_k${k}_mpi_p${NP}_s${SPLIT}.csv"

  echo "run,time,success,nodes,backtracks" > "$out_csv"

  for r in $(seq 0 $((RUNS-1))); do
    # Capture rank-0 output (only rank 0 prints in your program)
    # Expected tokens:
    #   success=true/false time=...s nodes=... backtracks=...
    local line
    line=$(mpirun -np "$NP" "$BIN" --mode mpi --graph "$file" --k "$k" --split "$SPLIT" --max_sec "$MAX_SEC" \
      | tr '\n' ' ')

    # Parse output robustly
    local success time nodes backs
    success=$(echo "$line" | sed -n 's/.*success=\(true\|false\).*/\1/p')
    time=$(echo "$line" | sed -n 's/.*time=\([0-9.eE+-]*\)s.*/\1/p')
    nodes=$(echo "$line" | sed -n 's/.*nodes=\([0-9]*\).*/\1/p')
    backs=$(echo "$line" | sed -n 's/.*backtracks=\([0-9]*\).*/\1/p')

    # Fallbacks if parsing fails
    [[ -z "${success:-}" ]] && success=false
    [[ -z "${time:-}" ]] && time=0
    [[ -z "${nodes:-}" ]] && nodes=0
    [[ -z "${backs:-}" ]] && backs=0

    local sflag=0
    [[ "$success" == "true" ]] && sflag=1

    echo "$r,$time,$sflag,$nodes,$backs" >> "$out_csv"
  done
}

for case in "${cases[@]}"; do
  file=$(echo "$case" | awk '{print $1}')
  k=$(echo "$case" | awk '{print $2}')
  base=$(basename "$file" .txt)

  echo "  -> $base (k=$k)"

  # Serial bench (single process)
  "$BIN" --mode bench --solver serial --graph "$file" --k "$k" --runs "$RUNS" --max_sec "$MAX_SEC" \
    > "$OUT/${base}_k${k}_serial.csv"

  # Threads bench (single process)
  "$BIN" --mode bench --solver threads --graph "$file" --k "$k" --runs "$RUNS" \
    --threads "$THREADS" --split "$SPLIT" --max_sec "$MAX_SEC" \
    > "$OUT/${base}_k${k}_threads_t${THREADS}_s${SPLIT}.csv"

  # MPI bench (SAFE): one mpirun per run
  run_mpi_csv "$file" "$k" "$base"
done

echo "[4/4] Done. Results in $OUT/"
