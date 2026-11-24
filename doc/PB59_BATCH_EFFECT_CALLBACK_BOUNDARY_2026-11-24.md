# Native batch effect callback dependency

Owner: CosmicCuddle. Core74 investigation, exact Core53 lineage. No runtime caller
or inventory mutation enabled. This records a dependency, not a complete executor.

## Exact native sequence

Player::ReconcilePB59OriginalCombatEffects keeps an equipped Item pointer while
its journal calls _ApplyItemMods. Player::_ApplyItemMods calls _ApplyItemBonuses,
which dispatches OnPlayerCustomScalingStatValueBefore. The caller subsequently
passes the original Item pointer to ApplyItemEquipSpell, ApplyItemDependentAuras
and ApplyEnchantment, without an intervening inventory identity/context check.
The journal's next verify occurs only after that whole mods call returns.

Core53 RemoveItem, StoreItem and DestroyItem begin with equipment publication
invalidation, not a transaction-admission check. Invalidation detects stale
snapshots but does not reject the mutation. The script dispatcher invokes the
scaling hook normally. Therefore the new outer MutationScope cannot, by itself,
guarantee that native effect work is not re-entered through these public paths.
Checking CanAccess after the complete call is necessary but insufficient.

## Executed evidence and its limits

test_batch_effect_callback_boundary.py extracts the unchanged actual _ApplyItemMods
body and compiles it with the actual TransactionAdmission header. Engine/effect
helpers and the bonus callback are doubles. A clean control has zero post-unlink
effect calls. A bonus callback deliberately marks the simulated item unlinked;
the actual method still invokes all three later effect helpers, and the actual
ledger still admits forward access. GCC16.2.0 C++20 warnings-as-errors PASS.

This does not execute real RemoveItem, real module hooks or deletion, and never
dereferences freed memory. It demonstrates the native control-flow boundary,
not a reproduced installed UAF. Public mutator admission absence is source
evidence from PlayerStorage.cpp. No installed module is accused of this behavior.

## Required implementation boundary

The executor must either establish audited producer admission covering supported
callback inventory/lifecycle entry points before any non-idempotent effects, or
use a reviewed effect-specific callback/lifetime protocol that preserves custom
hooks and checks currentness before further native use. A check after the callback
returns to the outer executor cannot repair a stale use inside _ApplyItemMods.
Do not solve this by suppressing module hooks, blindly replaying item mods or
publishing raw private insertion helpers. Guarding only three public methods is
not complete coverage: bag/item mutations and lifecycle producers require audit.

Preserve existing economic/retirement producers: a blanket Request from an already
admitted external producer can reject its legitimate nested native helper calls.
Forward and recovery ownership must be explicit; failed/uncertain effects must
retain quarantine. Native four-outcome executor remains unimplemented pending
this dependency. No SQL/config/DBC/client/live changes or hosted CI.

Rollback removes this investigation fixture/document only; no production behavior
is changed. Continue the same bounded Core lane and coordinate producer coverage
with Maintenance before expanding shared mutation semantics.
