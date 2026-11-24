#!/usr/bin/env bash
# Local runner for the CS02 provisional four-outcome executor fault controls. Not wired to CI.
#   tests/pb59/cs02_move_rollback/core74/run_core74_executor_controls.sh <core74-checkout@fa674825>
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
c74="$(cd "${1:?usage: $0 <core74-checkout>}" && pwd)"
PIN_HEAD=fa67482573e9426b701adb9b546a44a6d6113237
declare -A PIN_BLOB=(
  [src/server/game/Entities/Player/PB59EffectBoundary.h]=008c93c4957335d0e20033303618b87d5917c2cf
  [src/server/game/Entities/Player/PB59TransactionAdmission.h]=6c3bf34b767f58c72232145c143263cc66d60fdd
  [src/server/game/Entities/Player/PB59InventoryBatchPlan.h]=cb5574ab902334431ac42067013a8feb6d2fe57b
  [tests/pb59/test_effect_boundary.cpp]=410477aa95653f454fe8278611dee1a3bf20a1b6
)
head="$(git -C "$c74" rev-parse HEAD)"
[[ $head == "$PIN_HEAD" ]] || { echo "Core74 head $head != pinned $PIN_HEAD"; exit 2; }
for f in "${!PIN_BLOB[@]}"; do
  b="$(git -C "$c74" hash-object "$c74/$f")"
  [[ $b == "${PIN_BLOB[$f]}" ]] || { echo "$f blob $b != pinned ${PIN_BLOB[$f]}"; exit 2; }
done
git -C "$c74" diff --quiet HEAD -- src tests || { echo "Core74 checkout has local edits"; exit 2; }
echo "Core74 pinned: head $head; EffectBoundary.h 008c93c4; TransactionAdmission.h 6c3bf34b; InventoryBatchPlan.h cb5574ab; (CS01 test 410477aa present, not re-run here)"
out="${CS02_OUT:-${TMPDIR:-/tmp}/cs02-core74-exec}"
mkdir -p "$out"
status=0
for cxx in ${CXX_LIST:-g++ clang++}; do
  for mode in plain asan-ubsan; do
    flags=(-O2)
    [[ $mode == asan-ubsan ]] && flags=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    tag="$(basename "$cxx")-$mode"
    "$cxx" -std=c++20 -Wall -Wextra -Werror -Wswitch-enum "${flags[@]}" -pthread -I"$c74/src/server/game/Entities/Player" -I"$here/.." \
      "$here/test_core74_executor_controls.cpp" -o "$out/controls-$tag"
    if ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$out/controls-$tag" > "$out/$tag.log" 2>&1; then
      echo "$tag: $(tail -1 "$out/$tag.log")"
    else
      echo "$tag: FAILED"; cat "$out/$tag.log"; status=1
    fi
  done
done
exit $status
