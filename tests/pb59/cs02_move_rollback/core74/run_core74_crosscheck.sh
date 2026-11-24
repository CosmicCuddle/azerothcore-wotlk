#!/usr/bin/env bash
# Local runner for the CS02 Core74 cross-check. Not wired to any CI workflow.
#   tests/pb59/cs02_move_rollback/core74/run_core74_crosscheck.sh <core74-checkout> [sysinc dirs for the probe...]
# The Core74 checkout must be at the pinned head and blobs below (read-only use).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
c74="$(cd "${1:?usage: $0 <core74-checkout> [sysinc...]}" && pwd)"; shift
PIN_HEAD=27b6135909826434d9d5856237a68e7bfc6612f2
declare -A PIN_BLOB=(
  [src/server/game/Entities/Player/PB59InventoryBatchPlan.h]=cb5574ab902334431ac42067013a8feb6d2fe57b
  [src/server/game/Entities/Player/PB59TransactionAdmission.h]=6c3bf34b767f58c72232145c143263cc66d60fdd
)
head="$(git -C "$c74" rev-parse HEAD)"
[[ $head == "$PIN_HEAD" ]] || { echo "Core74 head $head != pinned $PIN_HEAD"; exit 2; }
for f in "${!PIN_BLOB[@]}"; do
  b="$(git -C "$c74" hash-object "$c74/$f")"
  [[ $b == "${PIN_BLOB[$f]}" ]] || { echo "$f blob $b != pinned ${PIN_BLOB[$f]}"; exit 2; }
done
git -C "$c74" diff --quiet HEAD -- src || { echo "Core74 checkout has local source edits"; exit 2; }
echo "Core74 pinned: head $head; PB59InventoryBatchPlan.h cb5574ab; PB59TransactionAdmission.h 6c3bf34b"
out="${CS02_OUT:-${TMPDIR:-/tmp}/cs02-core74}"
mkdir -p "$out"
status=0
for cxx in ${CXX_LIST:-g++ clang++}; do
  for mode in plain asan-ubsan; do
    flags=(-O2)
    [[ $mode == asan-ubsan ]] && flags=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    tag="$(basename "$cxx")-$mode"
    # Core74 Player dir FIRST so the real Core74 PB59 headers win over Core53 copies.
    "$cxx" -std=c++20 -Wall -Wextra -Werror "${flags[@]}" -I"$c74/src/server/game/Entities/Player" -I"$here/.." \
      "$here/test_core74_crosscheck.cpp" -o "$out/crosscheck-$tag"
    if ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$out/crosscheck-$tag" > "$out/$tag.log" 2>&1; then
      echo "$tag: $(tail -1 "$out/$tag.log")"
    else
      echo "$tag: FAILED"; cat "$out/$tag.log"; status=1
    fi
  done
done
if [[ $# -gt 0 ]]; then
  includes=()
  while IFS= read -r d; do includes+=("-I$d"); done < <(find "$c74/src/common" "$c74/src/server/game" "$c74/src/server/shared" "$c74/src/server/database" -type d)
  for d in recastnavigation/Detour/Include recastnavigation/Recast/Include g3dlite/include fmt/include utf8cpp SFMT; do includes+=("-I$c74/deps/$d"); done
  for d in "$@"; do includes+=("-isystem" "$d"); done
  for cxx in ${CXX_LIST:-g++ clang++}; do
    for mode in plain asan-ubsan; do
      flags=(); [[ $mode == asan-ubsan ]] && flags=(-fsanitize=address,undefined)
      if "$cxx" -std=gnu++20 -fsyntax-only "${flags[@]}" "${includes[@]}" "$here/core74_api_shape_probe.cpp"; then
        echo "$(basename "$cxx")-$mode: Core74 real-header API shape probe PASS (compile-only)"
      else echo "$(basename "$cxx")-$mode: Core74 probe FAIL"; status=1; fi
    done
  done
fi
exit $status
