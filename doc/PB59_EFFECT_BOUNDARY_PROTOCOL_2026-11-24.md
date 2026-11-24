# Opt-in native effect boundary protocol — partial implementation

Owner: CosmicCuddle. Core74 child. No batch executor or production frame is wired.

EffectBoundary is stack-bound, thread-local and keyed by owner pointer identity
without dereferencing that pointer. Its caller supplies a context and verifier.
The verifier must first prove retained owner lifetime/scope access before reading
Player/session or looking up exact item identities; it must independently check
pending save/logout/context and expected stage-specific inventory state. A true
ledger CanAccess alone is insufficient. This protocol supplies none of those
proofs and is not a pin, lock, producer rewrite or authority grant.

Check returns true when no matching frame exists, preserving ordinary calls and
custom hook dispatch. Within an explicit frame, verifier refusal/throw latches
failure; state cannot be reset by restoring facts. Same-owner nested frames poison
both scopes; recursive verification refuses. Different owners remain independent.
The caller must interpret failure as potentially partial effects and retain
recovery, never replay non-idempotent work or report success from slot facts alone.

Native hooks now check at _ApplyItemMods entry and between helper calls, including
after the final enchantment step. _ApplyItemBonuses checks immediately after the
scaling hook before further native reads. No callback is skipped in ordinary mode;
failed opt-in verification stops the subsequent effect chain.

Executed with GCC16.2.0 C++20 warnings-as-errors:23 actual header checks (identity,
nested/different-owner frames, exception, recursive verifier, sticky failure and
ordinary behavior). The unchanged extracted current _ApplyItemMods body passes
the original no-frame callback-gap control and the opt-in early-stop regression.
Engine/bonus callback/effect helpers are doubles. These are not real module,
_ApplyItemBonuses full-body, native Player build/link or gameplay tests.

Still required BEFORE any executor wiring: audit/guard internal set, socket,
spell, dependent aura, ammo and enchantment callback continuations, not just outer
helper returns; prove native owner and journal lifetime through every callback;
implement a verifier backed by real scope/context/custody observations; propagate
failure to native effect journals without touching a destroyed journal; handle
policy validation, forward/rollback effects/queue/visible links and custom display
notifications. This partial protocol does not close those gates. A callback that
destroys the owner can also destroy its member journal; an outer sentinel cannot
authorize continuing that journal method. Do not claim universal callback safety.

Custom modification delta: new internal boundary header, opt-in checks in Player.cpp
and isolated tests. No public private-helper exposure, SQL/config/DBC/client or
live behavior activation. Defaults retain no frame. Rollback removes this source
increment and preserves the prior admission/custody work and existing custom hooks.
