#!/usr/bin/env bash
# End-to-end checks for the multi-light shadow pipeline.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p out

pass=0
check() { # check <description> <condition...>
  desc="$1"; shift
  if "$@"; then pass=$((pass+1)); else echo "FAIL: $desc"; exit 1; fi
}

# 1. Legacy config stays byte-identical to the pre-change GSF1 output.
./build/app demo.cfg > out/legacy.out
grep -q '^ok span=27.889$' out/legacy.out
actual=$(sha256sum out/state.bin | awk '{print $1}')
check "legacy state.bin byte-identical" test "$actual" = "7f8c16008a6ea5a6c6814450b9af6775ab1f9ab2be05877a4eb0fdec602f41db"
check "legacy state still 32-byte GSF1" test "$(stat -c %s out/state.bin)" -eq 32

# 2. Three lighting rigs.
for cfg in A_two_balanced B_two_weighted C_three; do
  ./build/app "tests/configs/$cfg.cfg" > "out/$cfg.out"
  check "$cfg writes 96-byte GSF2" test "$(stat -c %s out/state.bin)" -eq 96
  grep -q '^ok-multi ' "out/$cfg.out"
  grep -q '^brightness ' "out/$cfg.out"
done

# 3. Edge: zero lights -> deterministic full darkness.
./build/app tests/configs/D_zero_lights.cfg > out/D.out
grep -q 'zero lights -> full darkness' out/D.out
grep -q 'brightness mean=0.000000 min=0.000000 max=0.000000 lit=0/576' out/D.out
grep -q 'lights=0 ' out/D.out

# 4. Edge: negative weight clamped to zero.
./build/app tests/configs/E_negative_weight.cfg > out/E.out
grep -q '1 negative weight(s) clamped to 0' out/E.out
# Only the positive light (L2 at x=15) remains, so points L2 blocks are dark.
grep -q ' min=0.000000 ' out/E.out

# 5. Edge: coincident lights behave as one, no double darkening seam.
./build/app tests/configs/F_coincident.cfg > out/F.out
grep -q '1 coincident light pair(s)' out/F.out
grep -q 'brightness .* max=1.000000 ' out/F.out

# 6. Determinism: same config twice produces identical state bytes.
./build/app tests/configs/A_two_balanced.cfg >/dev/null
h1=$(sha256sum out/state.bin | awk '{print $1}')
./build/app tests/configs/A_two_balanced.cfg >/dev/null
h2=$(sha256sum out/state.bin | awk '{print $1}')
check "deterministic state across runs" test "$h1" = "$h2"

echo "integration: $pass checks passed"
