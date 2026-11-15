# PB59 exact storage insertion seam — Part 11, 2026-11-15

Owner: CosmicCuddle. Core draft #37; Playerbots draft #29 at f9217b3a3cffc4905f4acefe0eb2041a1b035c8d. No activation or deployment.

## Independent authority identifiers
Test model fix 16924670157283c5a3c2f4625312645577957679 (Coordination #13 Claude review 6896138217). IP epoch and Core incarnation independently nonzero; equality is not required. Fresh capture on reconnect may be valid but a previously captured command must fail final comparison. Login/logout missing-lifetime windows fail closed. Bracketed copy is a historical coherent snapshot, not a lease authorizing mutation. Native sender owner-context binding, independently current IP and Core stamps, and final authority boundary are not wired yet.

Exact header blob 99d4a5880ed0c8d209b87a29bd4051d9288a2465, fixture 1febcce0e7468cdd62ce6dd228b1d603b71f454d: 30 assertions, GCC/Clang C++20 -Wall -Wextra -Werror and Clang ASan/UBSan PASS.

## Native private insertion seam
05d428d2f2d10a99e50609a97cd8f906fe9aca5a adds optional exact reservation to private Player::_StoreItem. Existing callers use the unchanged null default; no new caller. Exact mode refuses clone, mismatch of original GUID/count/position/owner, non-detached item, trade/loot, missing or replaced bag, invalid backpack/bag slot or occupied destination. Final occupancy check directly guards native merge/delete branch. It never selects alternative destinations. Restrict to non-bag combat gear; actual CanStoreItem full-stack/specialized-bag preflight remains required; not a general inventory API.

Exact helper blob 5c3a19561395b1a45384c089491d942fe4483e75, fixture 4cd6b16cc61f8e80749bd6a2009a6a19df056359: 18 cases, GCC/Clang C++20 -Wall -Wextra -Werror and Clang ASan/UBSan PASS. Source integration reviewed, not native-compiled. Reproduce from Core root:
```sh
g++ -std=c++20 -Wall -Wextra -Werror -Itests/pb59 tests/pb59/test_combined_authority.cpp -o /tmp/pb59-bind && /tmp/pb59-bind
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/server/game/Entities/Player tests/pb59/test_exact_storage_gate.cpp -o /tmp/pb59-exact && /tmp/pb59-exact
```
Repeat with clang++ and -fsanitize=address,undefined. Hashes independently matched local Git blob hashes before pushing.

## Actual source risks and remaining movement contract
Exact Core parent 82d340c1: PlayerStorage.cpp CanUnequipItem around 2062 calls script first; CanEquipItem around 1887 invokes script; ordinary SwapItem around 3655 caches pointers and invokes validation, RemoveItem, StoreItem/EquipItem and auto-offhand/Titan Grip. _StoreItem around 2719 can clone, merge and mark original ITEM_REMOVED. RemoveItem around 3017 invokes obtain-spell, aura/mod, visible slot hooks and writes detached fields; SetVisibleItemSlot invokes OnPlayerAfterSetVisibleItemSlot around 2989. Bag::StoreItem writes slot unconditionally; exact gate is upstream of that call, not a global protection. EquipItem around 2842 applies stats, binding, achievements, cooldown and scripts, and occupied branch merges. An exact-empty check alone cannot protect lifetime, callback side effects or return pointer after callbacks. The added gate is not a complete MoveExact implementation.

Next native work: (1) serialized bot owner-context transaction guard and lifecycle reentrancy rules; validate all fallible script policy and revalidate source/destination after callbacks; (2) prove detached item ownership and safe recovery through RemoveItem, never retain unowned raw pointers across callback boundaries; (3) safe exact-empty equip restoration without merge/auto-offhand side effects; (4) prove no callback can mutate reservations or invalidate detached owner lifetime; account for stats/auras/enchant durations/tradeability/visible slot and save queue; (5) inspect exact GUID/count/bag identity after each move, rollback original in reverse order; distinguish recovery-required and never destroy original; (6) new-item creation/retained-possession and socket/unique constraints, full candidate collection and final authority; (7) native build with/no IP, injected failure at each native seam, persistence/relog/crash recovery and gameplay. Do not wire standalone rollback coordinator to SwapItem or call _StoreItem exact on an unproven detached pointer. Keep capped command refusing until integrated acceptance.

Rollback: revert 05d428d2 and 16924670 on this draft (and this doc); no schema/config/client/production changes. Existing isolated PB and Core branches and installed baseline preserved. No new hosted Actions or production thresholds.
