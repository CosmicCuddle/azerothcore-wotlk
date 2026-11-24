#!/usr/bin/env bash
# Local runner for the CS02 move-rollback harness. Not wired to any CI workflow.
# Builds with each compiler, plain and with ASan+UBSan, against the REAL Core53
# headers in src/server/game/Entities/Player (header-only PB59 value types).
#   CXX_LIST="g++ clang++" tests/pb59/cs02_move_rollback/run_local.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="${CS02_OUT:-${TMPDIR:-/tmp}/cs02-move-rollback}"
mkdir -p "$out"
CXX_LIST="${CXX_LIST:-g++ clang++}"
status=0
for cxx in $CXX_LIST; do
  for mode in plain asan-ubsan; do
    flags=(-O2)
    [[ $mode == asan-ubsan ]] && flags=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    tag="$(basename "$cxx")-$mode"
    bin="$out/harness-$tag"
    "$cxx" -std=c++20 -Wall -Wextra -Werror "${flags[@]}" -I"$here" -I"$root/src/server/game/Entities/Player" \
      "$here/test_move_rollback_harness.cpp" -o "$bin"
    if ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$bin" > "$out/$tag.log" 2>&1; then
      echo "$tag: $(tail -1 "$out/$tag.log")"
    else
      echo "$tag: FAILED"; cat "$out/$tag.log"; status=1
    fi
  done
done
exit $status
