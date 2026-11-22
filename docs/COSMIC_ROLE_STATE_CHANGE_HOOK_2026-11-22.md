# Paired native role-state notifications

Owner: CosmicCuddle. Source-only child of Core70c6e3bf04d2e49fd30afcb8b0e3416fca50287aab.

COSMIC_ROLE_STATE_CHANGE_HOOK_VERSION1 appends WorldScript before/after role-state
hook IDs without renumbering previous values. Notifications are synchronous,
pointer-free and non-vetoing. They do not authorize Player/session access or make
other fields thread-safe. Subscribers must be non-throwing, support concurrent/
nested calls and subscribe to both halves when maintaining a balanced fence.
No change in world update order, role permissions, faction/phase or packet policy.

SetGameMaster brackets actual mode changes with a noncopyable stack guard through
all original work, nested callbacks, returns and unwinding. Same-mode calls retain
their native refresh side effects without notification. SetSecurity moves its
inline assignment to WorldSession.cpp and brackets changed values; same-value
calls do nothing. Constructor hydration/direct field writes/other eligibility
changes are not automatically covered. No actor pointers cross observer boundary.

An optional consumer can invalidate its global value cache before either native
setter writes. Rare global invalidation avoids resolving a remote session's Player.
An IP child implements that consumer separately; this API alone enables nothing.
All Core/modules must be rebuilt together because script enums/vtables/symbols
change. No script observer may assume it runs on the world thread.

Local Clang20/C++20 executable tests compile the actual guard, full SetGameMaster,
SetSecurity and WorldScript dispatch bodies with explicit engine/registry doubles:
26PASS including no-op/changed values, nested setter and unwinding/default-off
subscription checks. Source contracts4PASS. Paired GCC/Clang hosted outcome belongs
to companion IP PR/review queue. Not native engine ABI or gameplay proof. Full
Worldserver/module link, ownership/race QA and staging remain outstanding.

Required full Core C++ linter reports four inherited diagnostics in unchanged
BattlegroundAV.h/MoveSpline.cpp/Spell.cpp; no changed Core path is flagged. SQL
checker outcome recorded with exact run evidence; no SQL files changed. No live
config/DB/DBC/client/deployment/merge operation. Rollback reverts paired source
children and rebuilds compatible Core/modules; preserve custom state, no migration.
