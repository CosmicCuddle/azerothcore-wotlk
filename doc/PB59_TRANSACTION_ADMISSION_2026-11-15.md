# PB59 transaction admission and native custody gate — 15 November 2026

Owner: CosmicCuddle. Part 12. Core draft #37 admission implementation d1589ac0/073da8ac/82b64505/7584351a, private native linkage 2f6dc106/1d138ae5/c6196a186/a8bf7609. Paired PB #29 383e8c908fea40c560fc28173f41ad5c2efca9b2. No native Begin caller, no save interception, no activation.

## Implemented

- Owner-context-only TransactionAdmission: Idle/Active/RecoveryRequired; process-unique monotonically issued nonzero ticket; forward vs recovery access; exact ticket and independent stable owner/inventory/effect/queue proof required for Finish. Abandoned MutationScope retains recovery, never releases the owner. No timeout reset or fake durable success.
- ExternalScope admits ordinary nested work while idle and prevents new PB59 admission until the outer scope exits. Active/recovering state separates standalone save intent, logout intent, caller-owned/full/inventory+gold/gold-only/economic mutation/redirect rejection, and destruction retention. Coalesced deferred save is cleared only by outer admitted replay scope after actual acceptance; logout waits on pending save. Final lifecycle seal prevents starting new gear work in the gap before owner destruction. Scope state must outlive its references; this is not a Player/session lifetime pin or a persistent ledger.
- Private Player _pb59Transaction; RelocatePB59InventoryLinks and ReconcilePB59VisibleEquipment now require exact ticket and explicitly correct access phase before any observation/mutation. Recovery access is a private capability only for the exact validated recovery plan, not general authority. No stock public equip/storage/save behavior changed.
- Raw custody failures after the first queue marking begins now return RecoveryRequired and mark ledger even if the item remains attached to original slot. Earlier Rejected was insufficient because SetState may partially mutate queue/item state before throwing. Existing preflight rejection before mutation remains Rejected. Partial field repair and foreign GUID protections retained. Any admitted visible reconciliation failure marks recovery; explicit recovery retry permitted, no forward continuation or implicit release.

## Executed evidence

Local GCC and Clang C++20 -Wall -Wextra -Werror and Clang ASan+UBSan all passed for admission and both exact native bodies. All six tested blobs verified against GitHub (native body hash = git hash-object of extracted method through next method marker, rstrip + LF):

| Artifact | Blob |
| --- | --- |
| admission header | e45f643db6b62e6d936526a4a4606944ceb7951e |
| admission fixture | 47f4444a6745cf431df452df279539208c4d65be |
| raw custody method | b2760bfbfe9d438505eae31b66eda30bd8c45534 |
| custody fixture | 2009907becccc88048ff60567c2b68e5460753a4 |
| visible method | fd30d99f2033bc0f8a86e9c09bf7b46bf56f0e50 |
| visible fixture | aaf81540f061217177e4905b80407803fff2e310 |

Admission: all eight work classes idle/active/recovery, 240 incomplete stable-proof combinations (15 x 8 x 2), foreign/stale/zero/wrong-phase tickets, nested ordinary scopes, abandoned operation exception, pending save/logout, replay ordering, lifecycle seal, invalid enum. Custody: previous complete test suite rerun; four before/after queue faults now correctly quarantine with original attached; six initial raw field write faults still repair; persistent/foreign/post-observation/queue/stale/reverse tests pass; foreign/zero/expired/recovery-only tickets refuse without observation or writes; explicit recovery reverse preserves original object and retains recovery state. Visible: 20 injected before/after writes, ordinary forward retry blocked, explicit recovery retry repairs, custom and stale values refuse; ticket failures no writes. Synthetic inventory dependencies, not complete native compile or gameplay.

Reproduce from Core root:
```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/server/game/Entities/Player tests/pb59/test_transaction_admission.cpp -o /tmp/pb59-admission && /tmp/pb59-admission
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/server/game/Entities/Player tests/pb59/test_transaction_admission.cpp -o /tmp/pb59-admission-san && /tmp/pb59-admission-san
python3 tests/pb59/test_inventory_custody_body.py
python3 tests/pb59/test_inventory_custody_body.py --cxx clang++ --sanitizers
python3 tests/pb59/test_inventory_custody_body.py --visible
python3 tests/pb59/test_inventory_custody_body.py --visible --cxx clang++ --sanitizers
```

## Native boundary audit (no new runtime adapter yet)

Codex #59 comments 6896777094 and 6896791090, CS01 ACK 6896828656. At Core c687ce80:

- PlayerStorage.cpp:7600–7667 full SaveToDB wrapper begins/commits transaction; trans overload updates nextSave/additional masks/honor before OnPlayerSave, then _SaveCharacter/_SaveInventory and others, pet save separate. A void early return is not a safe caller-owned failure. Far teleport already schedules a delayed save; do not confuse that with PB59 replay. 7683–7695 SaveInventoryAndGoldToDB appends inventory and money; direct SaveGoldToDB also used. 7803+ _SaveInventory changes buyback and item state before queue processing. Existing async CommitTransaction submission is not durability confirmation.
- Player.cpp:16200–16251 RefundItem response, destruction, items, gold/honor/arena and transaction append before SaveInventoryAndGoldToDB. 12804–12812 AutoUnequipOffhandIfNeed may mail original item and issue direct item DB operations outside general save; do not invoke blind ordinary offhand fallback during exact movement.
- MailHandler.cpp:340–375 send moves/deletes/changes item owner and drafts mail before inventory+gold commit; 624–626 item take commits mail and inventory; 671–677 money-only mail mutates and commits gold + mail. AuctionHouseHandler.cpp:321–328/398–402/589–590/662–664 sell/bid/remove have auction and economic side effects before save. TradeHandler.cpp:643–669 spell effects, trade cleanup and both participants' saves; admission must cover both players before either side effects, and preserve existing crossrealm transaction distinctions. This is a concrete nonexhaustive list, no live incident or broad economic redesign.
- WorldSession.cpp:692–818 logout starts teleport/identity detach/hooks/cleanup before save; 854–866 CleanupsBeforeDelete, RemovePlayerFromMap(...,true), SetPlayer(nullptr). Destructor 169–194 invokes logout then destroys session resources; returning early from LogoutPlayer inside destructor cannot retain the session. Player.cpp:429+ destructor invokes OnDestructPlayer and deletes item pointers. WorldSession update 606–623 may remove closed session; 1646–1665 redirect uses caller-owned SaveToDB + async result and must not report skipped save success.
- **Playerbots-specific lifetime blocker:** PB 383e8c90 PlayerbotMgr.cpp:406–468 LogoutPlayerBot retires login identity before queueing group cleanup, taxi mutation, first save, map erasure, LogoutPlayer and deletion of headless WorldSession. 471–499 DisablePlayerBot retires identity, stops movement, saves, erases holder and deletes AI. 236–252 pending-login cleanup calls LogoutPlayer/delete. 1721–1735 master saves bots; RandomPlayerbotMgr.cpp:969–974/1582–1586 clears event/currentBots state before logout. Guarding only Core LogoutPlayer is too late. Producer admission must precede identity retirement, manager removal and caller side effects. Retain the actual owner and recovery path independent of command sender context; preserve logout intent and normal policy after reconciliation. Do not hold a reference-based scope on a Player through its deletion.

## Next implementation gate (before any native Begin caller)

1. Complete source inventory of save/lifecycle producers across installed Core/IP/CS/PB/Naxx/custom modules, distinguish true standalone autosave from caller-owned or owner-ending work even if same overload. Establish actual serialized owner scheduler and lifetime retention (including headless bot manager and world/session destruction), not a global mutex or raw pointer across unproven callback.
2. Design explicit native admission results at producer boundaries before first side effect, including paired trade, full/trans/inventory/gold paths and independent item writes. A standalone deferred save must retain actual intent and service once verified stable, with bounded retry/failure diagnostics; caller-owned transaction refusal must occur before mutation or require explicit caller recovery, never silently append partial inventory/gold or commit/discard unrelated SQL. No current save skip patch.
3. Add pre-logout/disable/retire and shutdown admission, preserve original save/redirect flags, keep state alive through scope exit, seal before release. Do not cancel or defer a destructor already executing; move admission to ownership producers, and define unresolved recovery containment plus bounded orderly shutdown/crash policy. Durable recovery/restart and DB commit ordering remain separate gates.
4. Complete pre-link effects and post-link reconciliation under the verified native scope; callbacks may not cause unreviewed saves/mutation/lifetime changes. Preserve custom hooks/appearance, no blind replay. Integrate actual MoveExact, full candidate pool, final sender authorization and bot context, then with/no-IP native compile/gameplay, fault/relog/restart tests.

No live/schema/config/client/DBC changes, merge, deploy, or hosted CI. Rollback: revert this isolated draft slice (or discard development branch); no production state to revert.
