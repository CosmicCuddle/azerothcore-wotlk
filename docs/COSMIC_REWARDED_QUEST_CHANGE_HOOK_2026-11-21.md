# Rewarded-quest projection notifications — 21 November 2026

Owner: CosmicCuddle. Isolated development on `feature/codex-rewarded-quest-change-hooks-20261121`, parent `21ff1973bc75216f8a0d0f3d7731eb3e8894ff97` (current fetched Core master). The installed/live build and flags are not established by this checkout. Coordination: https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/51

## Contract

`COSMIC_REWARDED_QUEST_CHANGE_HOOK_VERSION = 1` exports two neutral PlayerScript/ScriptMgr notifications: `OnPlayerBeforeRewardedQuestChange(Player*, uint32 questId)` and `OnPlayerAfterRewardedQuestChange(Player*, uint32 questId)`. The new hook IDs are appended before `PLAYERHOOK_END`, preserving previous IDs. They cannot veto the quest operation and contain no race, progression, paid, database or visibility policy.

The callbacks run synchronously on the initiating operation's thread. Observers must follow that thread's object-ownership rules, must not retain Player pointers, and should not throw. Notifications can nest. No event is a commit of character database state; it only describes in-memory quest projection changes. All Core/modules must be rebuilt against matching headers; mixing old module binaries with the new virtual interface is unsupported.

- `SetRewardedQuest` brackets new rewarded membership and its existing save flag; duplicate calls still set the original dirty flag but do not notify.
- `RemoveRewardedQuest` brackets removal/save flag; absent removals keep existing update behavior without notifying. Erasing by quest ID avoids retaining an invalidated iterator across a nested observer edit.
- `SetQuestStatus` and `RemoveActiveQuest` bracket active-entry changes because `GetQuestStatus` checks active entries before rewarded storage. Status no-ops keep their existing dirty/update semantics. Active removals also re-resolve the key after callbacks.
- Stack-only guards in `AddQuest` and `RewardQuest` span their projection changes, including nested setters, early returns and exception unwinding after construction. They retain the current Player only until that synchronous call ends; nothing crosses a tick.
- `_LoadQuestStatusRewarded` remains unchanged: pre-login storage hydration is covered by a consumer's login lifecycle. The raw mutable quest-map accessors also remain unchanged; consumers must not claim those direct writes are intercepted by these notifications. Modules writing progression IDs through a mutable map must use an observed mutator or join the observer contract.

All existing quest writer bodies are retained apart from notifications/guards and key-based erasure. No reward, SQL, schema, configuration, packet policy or gameplay rule is changed here. With no interested listener, data/save/update behavior is the inherited one.

## Paired F14 consumer

The IP child of Draft PR189 (`fabff319d8ff38ea8c3e06ced5f22e3b435f165d`) consumes the two hooks only when version 1 is present. Hidden stage-quest mutations maintain a nested global in-flight count; snapshots cannot be read or published while one is active, and deferred publication is retried. Both halves count even if the runtime option changes; reload does not discard an active pair. Underflow/overflow fail closed. No Player/group lease is retained.

Only the paired API, both existing session hooks and the owner's Paid-OFF configuration permit borrowing an unearned Wrath stage. Older Core retains the earlier fence. Paid receipt mutations are a separate lifecycle and remain outside this increment; no paid enablement is authorized.

## Executed developer evidence

Compiler: Clang20.1.2 via the previously checksum-verified workspace-only Zig0.15.2. No full configure/build/install was performed.

- `tests/cosmic_rewarded_quest_change/run_rewarded_quest_change_test.py`: actual Core set/remove/status/active-removal/getter functions, actual dispatch and guard compiled against explicit script/container/template doubles: **35/35 PASS** in C++20 and C++17/NDEBUG. Covers no observers, no-ops, save/update flags, nested same-key edits, status masking/reveal, aura/save-mask side effects and guard unwinding.
- Paired IP runner with this Core: **67, 69, 69, 71 checks PASS** for `(session, quest)` hook combinations `(0,0), (0,1), (1,0), (1,1)`, repeated C++20 and C++17/NDEBUG. Includes forced publication inside mutations, nested edits, config/option transitions, unrelated quests, status masking/reveal, malformed pair fail-closed and Paid-ON refusal. Core writer/getter/dispatch and IP adapter/query/sync/access functions are compiled from actual sources; engine objects are doubles.
- IP snapshot policy C++20/NDEBUG: **3,906/3,906 PASS**. Standalone IP legacy runner without a Core checkout: **49/51 PASS** for absent/present session hooks.
- Core source contracts: **8 PASS**; inherited/new IP source and patch checks: **58 PASS**.
- `git diff --check`: PASS in both repositories. Core SQL style checker: PASS (no SQL changes).
- Full Core C++ style checker: **FAIL on four inherited diagnostics** in unchanged `BattlegroundAV.h` (56, 797), `MoveSpline.cpp` (224), `Spell.cpp` (3896). Those files were verified identical to `origin/master`; no diagnostic concerns the four changed C++ paths. No unrelated formatting cleanup was performed.

Reproduction from the Core root:

```text
python -B -m unittest discover -s tests/cosmic_rewarded_quest_change -p test_rewarded_quest_change_source.py
python -B tests/cosmic_rewarded_quest_change/run_rewarded_quest_change_test.py --compiler clang++
python -B tests/cosmic_rewarded_quest_change/run_rewarded_quest_change_test.py --std c++17 --ndebug --compiler clang++
```

From the paired IP root, run `python -B tests/run_f14_group_view_adapter_test.py --core <this-Core-checkout> --compiler clang++`; add `--std c++17 --ndebug` for the release-compatible variant. Zig uses `--compiler <zig.exe> c++`.

GCC, sanitizer matrix, native Worldserver/Core-module linking and actual gameplay are **NOT RUN**. Whole `AddQuest`/`RewardQuest` engine execution is not reproduced by the doubles; their guard placement is a source contract. No hosted Actions requested. Fixture success is not production acceptance, concurrency proof or installed-classifier verification.

## Safety and rollback

No merge, deployment, host/config/DB/DBC/client change. Keep F14/Paid/AutoGear defaults OFF; distribution/live settings are not changed. Native integration must verify module hook masks, ownership/order, any direct progression-map writers, mixed/offline parties, title persistence, Hunter/Naxx40/Onyxia40 and bot login/reset performance before an owner-approved release. Existing custom source remains intact.

Rollback proposal: keep the F14 pilot OFF; revert the paired IP child and this Core source increment, then rebuild matching Core and modules. Do not mix headers/binaries or casually toggle a gameplay pilot during testing. There is no SQL/data migration to reverse. Production work still requires CosmicCuddle's approval.
