# PB59 bot retirement producer admission — 15 November 2026

Owner: CosmicCuddle. Part 12 continued. Core #37 code head fa88794652d26825f7def5d395d5b835d1144867; paired Playerbots #29 33760f35b53c914c7d8cf09650dc4ad2739fd8f8. Draft-only, no activation or native Begin caller.

## Implementation

- Core TransactionAdmission::RetirementLease is move-only and owns a small independent token, not Player/AI/session or a reference into a destroyed Player. Idle/no pending save or logout/no external scope/no seal required. While held, Begin and another retirement are denied. Independent generic Logout/Redirect requests reject, Destroy retains; ordinary idle saves remain admitted. Atomic weak-token publication excludes simultaneous competing retirement reservations; the rest of the ledger and Player access remain serialized owner-context only. No claim of general thread safety or native lifetime pin. Allocation failure rejects before producer side effects. No token reset shortcut for recovery. Public Player::TryAcquirePB59Retirement is the owner-context producer seam.
- PB PlayerbotHolder::LogoutPlayerBot and DisablePlayerBot now acquire lease before RetireLogin, group cleanup, save, holder erasure, AI/session destruction. Logout verifies AI/session/owner and not already logging out before scheduler callback. Lease remains through deletion and never dereferences deleted Player. Recursive same-producer calls during saves refuse. Logout/disable return actual completed bool; manual remove/logout/rm does not report ok on refusal. Existing successful save, taxi, group, farewell and teardown sequence retained.
- RandomPlayerbotMgr population downsize, expired add event and direct Remove use admitted scheduler bookkeeping; busy refusal cannot clear add/currentBots/event-cache/DB records at those entrypoints. Count completed retirements, not selected candidates. Non-holder direct Remove retains existing event cleanup only under passed owner's lease. A missing player in expiry still follows normal stale bookkeeping cleanup. Automatic population/expiry may retry normal tick; manual caller is explicitly told to retry. This is not an automatic replay guarantee for every caller, nor a rollback of scheduler effects after an admitted operation subsequently fails.

## Test evidence

Local GCC and Clang C++20 -Wall -Wextra -Werror, Clang ASan+UBSan passed new retirement fixture, previous admission fixture and exact extracted PB manager bodies with synthetic dependencies. 100 two-thread competing-reservation rounds (no concurrent Player or mutation-state writes), exactly one winner; move/release and owner destruction before lease release; active/recovery/deferred saves/sealed state refuse; exception releases token; previous admission regression. PB fixture covers active/recovery no side effects, post-recovery successful logout ordered trace, recursive same-producer/save callback exclusion, already logging out refusal, external save/pending save refusal, successful disable and subsequent owner destruction. Synthetic dependency bodies, not native Worldserver gameplay. Source contract checks on actual published PB files pass for scheduler downsize/expiry/direct cleanup and manual result; not native DB/scheduler execution.

Exact tested blob identities:
- Core admission a65b0498b9f13f921a361454b17c3ecfe9cd4de8
- Core retirement fixture e76e0a854b3ed3316a6863849085a5c52ade738a
- PB exact extracted manager methods b48dc35712f242753011b4f43558f7c0fa126df9 (trim trailing whitespace + LF, through next RemoveFromPlayerbotsMap method)
- PB fixture ca824e44b911a45c52bc2885a938790cee3d285a
- PB extraction runner 56dc0a60f2246e584596aececca08b5a44ee2364

Reproduce:
```sh
# Core root
g++ -std=c++20 -Wall -Wextra -Werror -pthread -Isrc/server/game/Entities/Player tests/pb59/test_retirement.cpp -o /tmp/pb59-retirement && /tmp/pb59-retirement
clang++ -std=c++20 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -Isrc/server/game/Entities/Player tests/pb59/test_retirement.cpp -o /tmp/pb59-retirement-san && /tmp/pb59-retirement-san
# Playerbots root, paired Core checkout
python3 tests/pb59_test_bot_retirement.py --core /path/to/core
python3 tests/pb59_test_bot_retirement.py --core /path/to/core --cxx clang++ --sanitizers
```

## Remaining native integration gates

1. This is producer admission, NOT complete native lifetime protection. WorldSession::LogoutPlayer, WorldSession/Player destructors, pending-login cleanup, world/session deletion, direct map deletion and independent script callbacks still need producer ownership/authorization. Current PB LogoutAllBots ignores individual false results; master PlayerbotMgr destruction/RemovePlayerBotData and RandomPlayerbotMgr::OnPlayerLogout must preserve a retained bot/AI/session independent of the master. No assumption that a void destructor can defer or that skipping save prevents deletion. Need real owner scheduler and native serialized execution proof; atomic token reservation alone is not a cross-thread Player lock.
2. Existing logout still has callbacks, including SaveToDB and TellMaster, followed by raw pointer use; a separate native teardown could invalidate them. The lease blocks recursive admitted PB producer, not all independent Core callbacks. An admitted retirement may later fail or throw after bookkeeping/identity changes; false is not a rollback result. Need revalidation/explicit handoff and recovery after partial retirement, stable ownership for retry; manual busy response is not a durable intent queue. Actual logout/redirect/save flags and shutdown/replay policy remain open.
3. Native save/economic admission before first mutation, caller-owned transaction semantics, autosave replay, actual durability/DB commit and restart recovery remain open. No current save interception. Complete pre/post equipment effects and actual MoveExact/candidate/command binding only after reviewed native guard.
4. Rebuild exact Core+PB+IP+CS+Naxx tuple, native bot retirement/save/economic fault tests, master logout, disconnect, population expiry, pending login, real human/SelfBot/Altbot/RNDBOT regression, relog/restart, gameplay. No hosted CI, native compile, SQL/config/client/DBC/live operation, merge or deployment in this slice.

Rollback: revert isolated draft changes or discard branch; no production state affected.
