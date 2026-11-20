#!/usr/bin/env bash
# Checks that web/js/flasher-core.js sends exactly what the Python loader sends.
#   test/compare.sh DIST_DIR LANG_DIR [DONOR.bin]
set -euo pipefail
cd "$(dirname "$0")"
dist=$1; langs=$2; donor=${3:-}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
run() { # model firmware lang donor
  python3 python_reference.py "$@" > "$tmp/py.txt"
  node js_reference.mjs "$@" > "$tmp/js.txt"
  local n; n=$(grep -c '^write' "$tmp/py.txt")
  if [ "$n" -gt 0 ] && cmp -s "$tmp/py.txt" "$tmp/js.txt"; then r=identical; else r=DIFFERENT; fail=1; fi
  echo "$1 $(basename "$2") lang=$(basename "$3") donor=$([ -n "${4:-}" ] && echo yes || echo no): $n blocks, $r"
}
for d in "" ${donor:+"$donor"}; do
  run MD-UV380 "$dist/OpenGD77_MDUV380.bin" "$langs/Polish.gla" $d
  run MD-UV380 "$dist/OpenGD77_MDUV380_UV380_PLUS_10W.bin" - $d
  run DM-1701 "$dist/OpenGD77_RT84_DM1701_DM1701.bin" "$langs/German.gla" $d
done
exit $fail
