# Batch inventory admission foundation

Owner: CosmicCuddle. Child of exact Core53
79b0b1fe2d0fac93227c28329508553cfec15c4d. Uninstalled, no activation.

`Player::CheckPB59InventoryBatchPlan(owner, ownerIncarnation,
sessionIncarnation, stamp, moves)` is a synchronous read-only bot owner-context
check. It returns no ticket, lifetime pin, authorization or reservation lease.
It calls no policy hooks and performs no native writes. No caller is wired.
It checks exact owner/session incarnation, lifecycle/teleport/combat state,
ledger availability, pending save/logout and unsettled original custody,
equipment stamp and each full native source/destination observation. Combat
equipment must cross to/from storage; native capture validates legal positions,
bag identity, metadata and inventory links. All destinations must be initially
empty and structurally distinct from every source/destination. Headless bot
sessions remain permitted; the separate human sender authority predicate is
not reused or weakened.

This is not CanStore/CanEquip/CanUnequip policy approval, an offhand displacement
operation, or the finished four-outcome move API. The future executor must prove
owner context, acquire custody admission, preflight policy and re-observe after
callbacks, then reconcile effects/visible/queue/link state and reverse restoration.
Do not call private insertion helpers or raw link relocation on the strength of
this boolean alone. No DB atomicity or safe arbitrary callback lifetime is claimed.

Tests: structural actual header19 checks passed locally. Extracted unchanged
native admission method23 checks passed with actual equipment-stamp/slot headers
but explicit engine/session/capture AND ledger doubles, Clang20.1.2/Zig0.15.2,
C++20 warnings-as-errors. Default test mode instead uses the real transaction
ledger header, but local libc++ fails to compile its existing atomic shared_ptr
field. That gate remains unpassed; no hosted CI was dispatched. No full native
Player/Worldserver build or runtime evidence is claimed. The default real-header
test retains transaction/retirement checks for a compatible compiler.

Custom register delta: new batch structural header, read-only Player signature
and PlayerStorage method plus isolated tests. No SQL/config/DBC/client/migration
or live state changes. Rollback removes the unused child additions, preserving
Core53 and existing custom inventory/custody behavior. Future mutation needs
separate exact-source failure injection and native linked acceptance.
