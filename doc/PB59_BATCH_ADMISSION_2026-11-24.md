# Batch inventory admission foundation

## Compatible-compiler and scope-access follow-up

The actual-ledger compiler gate is now passed locally with workspace-only
WinLibs GCC16.2.0 (x86_64 UCRT POSIX SEH). Publisher release archive SHA256:
3c69305081e4a647144339a936244fcf7d3d9e558319e1ad7be2cd2886d4b6a1.
Default admission runner:27 checks PASS using actual ledger/slot/stamp headers
and extracted native preflight body, with engine/session/capture doubles.
Earlier Zig/libc++ limitation below remains historical, not the current gate.

MutationScope now exposes CanAccess(expectedOwnerIncarnation, Access), using its
existing retained sentinel and exact ticket. It refuses after owner destruction,
incarnation mismatch, failed/nested admission, wrong forward/recovery phase or
Finish. It does not pin native Player or Item and must precede native access
after callbacks; context and item revalidation remain separately necessary.
The actual-ledger lifetime fixture passes31 checks including owner destruction,
independent producer rejection, nested scope refusal, quarantine access and
preservation of deferred logout through explicit Finish. No mutation executor
or effect/link rollback is wired by this increment; no hosted CI or full native
Player build/DB/gameplay proof. Existing ledger admission semantics unchanged.

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
