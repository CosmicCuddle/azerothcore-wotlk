# PB59 native equipment capture — development register

Owner: CosmicCuddle. Maintenance CS01. Coordination #59. Core base 21ff1973bc75216f8a0d0f3d7731eb3e8894ff97; native hooks 68fe65a13c77547a262b65d6d24b7afccec99bda; raw equality 3af1c9805f021039d65092857c0bc92d64e76f5a. Paired Playerbots head 54babc6fdded0b24ae339d95113ec3daae887ef7, based on Codex 6ab8e7bcf6a173f8bd928323449afd7d64584f0e.

## Intended behavior and boundaries

Core owns a per-Player owner-context-only capture state with process-local incarnation and monotonic equipment revision. Nested RAII guards invalidate at outer entry and prevent capture throughout mutations; scope exit does not publish. No mutex held across callbacks, no per-operation allocations, no foreign worker read of Player or capture state. SetPlayer attaches fresh incarnation, early logout and reconnect detach; SetSession invalidates. LoadFromDB / _LoadInventory guard prevents premature capture during load; capture checks current session pointer, loading/logout/headless/in-world/teleport and verifies stamp after reading. This incarnation is NOT IP's authoritative registry epoch, and not the separate AH #36 token. Both IP and Core session identities must be bound and validated before command authorization.

Native paths: Player.h/.cpp/PlayerStorage.cpp, Unit.cpp SetLevel, WorldSession.cpp, CharacterHandler.cpp. Guards cover equip/quick equip/visualize/remove/destroy/swap/split/store/move/new item/load; level, both dual-wield setters, Titan Grip, parry/block, talent/spell/spec/SetSkill/ModifySkillBonus; AddToWorld/RemoveFromWorld. Conservative invalidation also on bag operations and no-op calls: safe but may require recapture. No equipment behavior, progression, DB or configuration changes.

Capture returns 19 raw slots (GUID, entry, item level, quality, class/subclass, inventory type, scaling fields), player level/class/race/spec and dual-wield/Titan Grip, stamp and full GUID. This is not a computed cap, not an IP snapshot, not a live published authority. No cross-thread use. Actual owner execution boundary and complete direct-writer/module callback coverage remain review gates. Skills/spell eligibility and item data/reload version must be incorporated into authoritative policy/capture; raw data compare helps detect changes but is not proof of complete invalidation. No production slot weights, caps or era rules selected.

Playerbots factory uses paired Core capture under __has_include, no direct IP dependency. Older Core returns Unknown for the unused arithmetic sample path, no unsafe fallback. Native adapter binds explicit policy revision/mask and compares full copied facts; heirloom/scaling Unknown. Legacy arithmetic wrapper uses local revision1 solely for averaging, not authorization. New deterministic complete-set planner uses copied domains and explicit cap/score/mandatory mask/weapon capabilities; whole-set unique/category and weapon constraints, exclusive mainhand staff/polearm/fishing pole, bounded candidate/node budgets. Only complete validated universe with exhausted search can produce NoSet; partial/invalid never strips. Real installed item universe/class policy, item acquisition/storage transaction, final matching IP/equipment B, command routes and native/gameplay all open.

## Executed local focused evidence

GCC and Clang C++20 -Wall -Wextra -Werror; Clang ASan/UBSan. Exact helper initial 103d40dfe019342b0d5e8d6ac3a0e990c0f9c198, test 084896a56ffd7a2bb4b8414c7743ec1f2f688b34, PASS all three. Revised Core equality header 4c6b0bf35df70194ca722209a727419fe5f1087b GCC state test + paired adapter tests PASS. Adapter 30bae1c90f897ab99fb27f88ae7f526e79d617c8, fixture c86235255aa783da85492637579295413f8264f5, existing sample 97195b335b9cd51080cb8542ff3ed142cad36cc8: GCC/Clang/ASan+UBSan PASS. Solver 9152083e97193f714f2efdbeaac319a98017f6d9, fixture e09ee96d86b729e6c6a74a5ba5a46cd403005506, weapon domain 6bb89db872cfab9aecdfa780290e77809ece5dbd: GCC/Clang/ASan+UBSan PASS. Reproduce from paired checkouts:
```sh
CORE=/path/to/azerothcore-wotlk
PB=/path/to/mod-playerbots
g++ -std=c++20 -Wall -Wextra -Werror -I"$CORE/src/server/game/Entities/Player" "$CORE/tests/pb59/test_native_equipment_capture_state.cpp" -o /tmp/pb59-state && /tmp/pb59-state
g++ -std=c++20 -Wall -Wextra -Werror -I"$CORE/src/server/game/Entities/Player" -I"$PB/src/Bot" "$PB/tests/pb59_autogear_native_authority.cpp" -o /tmp/pb59-authority && /tmp/pb59-authority
g++ -std=c++20 -Wall -Wextra -Werror -I"$PB/src/Bot" "$PB/tests/pb59_autogear_set_solver.cpp" -o /tmp/pb59-solver && /tmp/pb59-solver
```
Repeat clang++; sanitizer add -fsanitize=address,undefined. No native Worldserver/full module build or gameplay test executed here. Test state helper is actual runtime header; tests exercise local copied facts, not actual Player/WorldSession hook invocation.

## Native gate and rollback

Build exact Core+PB tuple with current approved IP snapshot and Character Services, and no-IP tuple separately. Review same-character reconnect, failed load, early logout, transfer, nested mutation callbacks, full caller/writer inventory, no-IP linkage, existing normal uncapped factory behavior and performance. Then wire sender owner capture plus IP authority, complete candidate source and safe reversible transaction; never activate capped command until all gates pass. AH #36/F14 experimental/release other branches not implicitly included. Rollback discard/revert isolated Core/PB drafts; no migration or live data change. No merge/deploy/config/SQL/feature activation.
