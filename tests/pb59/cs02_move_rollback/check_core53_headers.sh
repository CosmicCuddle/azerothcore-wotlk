#!/usr/bin/env bash
# Compile-only probe of core53_api_shape_probe.cpp against this checkout's REAL
# headers (Player.h, ScriptMgr.h and their includes). Not a worldserver build.
#   tests/pb59/cs02_move_rollback/check_core53_headers.sh [extra -isystem dirs for boost/mysql...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
core="$(cd "$here/../../.." && pwd)"
includes=()
while IFS= read -r d; do includes+=("-I$d"); done < <(find "$core/src/common" "$core/src/server/game" "$core/src/server/shared" "$core/src/server/database" -type d)
for d in recastnavigation/Detour/Include recastnavigation/Recast/Include g3dlite/include fmt/include utf8cpp SFMT; do
  includes+=("-I$core/deps/$d")
done
for d in "$@"; do includes+=("-isystem" "$d"); done
status=0
for cxx in ${CXX_LIST:-g++ clang++}; do
  for mode in plain asan-ubsan; do
    flags=()
    [[ $mode == asan-ubsan ]] && flags=(-fsanitize=address,undefined)
    if "$cxx" -std=gnu++20 -fsyntax-only "${flags[@]}" "${includes[@]}" "$here/core53_api_shape_probe.cpp"; then
      echo "$(basename "$cxx")-$mode: Core53 real-header API shape probe PASS (compile-only)"
    else
      echo "$(basename "$cxx")-$mode: Core53 real-header API shape probe FAIL"; status=1
    fi
  done
done
echo "core: $(git -C "$core" rev-parse HEAD 2>/dev/null || echo unknown)"
exit $status
