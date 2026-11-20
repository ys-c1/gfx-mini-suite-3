#!/usr/bin/env bash
# Timing for the three lighting rigs: N runs each, report median time_ms.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out
N="${1:-21}"
printf '%-18s %8s %8s %8s %8s\n' config median mean min max
for cfg in A_two_balanced B_two_weighted C_three; do
  vals=()
  for i in $(seq 1 "$N"); do
    ms=$(./build/app "tests/configs/$cfg.cfg" | sed -n 's/^span=.* time_ms=//p')
    vals+=("$ms")
  done
  printf '%s\n' "${vals[@]}" | sort -n | awk -v c="$cfg" '
    {v[NR]=$1; s+=$1}
    END{
      med=(NR%2)?v[(NR+1)/2]:(v[NR/2]+v[NR/2+1])/2;
      printf "%-18s %8.4f %8.4f %8.4f %8.4f\n", c, med, s/NR, v[1], v[NR]
    }'
done
