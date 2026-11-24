# Native AutoGear inventory batch — draft integration contract

Owner: CosmicCuddle. Core PR74, 24 November 2026. Parent Core53
`79b0b1fe2d0fac93227c28329508553cfec15c4d`.
The bounded reservation-protection extension was approved for isolated development/testing;
see coordination #81 comment 7131868276 and technical thread #51.
No merge, deployment, activation, hosted CI or established gameplay-rule change is authorized.

## Public API version 1

`Player.h` defines `COSMIC_PB59_INVENTORY_BATCH_API_VERSION 1` and exposes:

```cpp
std::optional<PB59::Stamp> CapturePB59InventoryStamp() const;
PB59::InventoryBatchResult ExecutePB59InventoryBatchPlan(
    uint64 owner, uint64 ownerIncarnation, uint64 sessionIncarnation,
    PB59::Stamp const& stamp, std::span<PB59::InventoryBatchMove const> moves);
```

Use `CapturePB59InventoryStamp()` for headless bots. The older human authority
`CapturePB59Equipment()` is not the bot stamp source. Obtain owner/session incarnations
from `GetPB59OwnerIncarnation()` and the bound session's `GetPB59SessionIncarnation()`.
Capture exact source/destination facts with `CapturePB59InventorySlot()` and make the call
synchronously in the bot's serialized owner/world context. Retain no native pointer across
a tick, asynchronous dispatch or context handoff. A snapshot is neither a thread lock nor
permission to mutate. Core does not classify bot identities or authorize a gameplay command:
the Playerbots adapter must positively establish its approved actor/command context first.
No opcode or automatic caller is added here.

The existing `CheckPB59InventoryBatchPlan()` remains a read-only observation. A successful
check does not authorize a later public-helper movement sequence. The executor owns all
native policy checks, exact pointer transfers, effects and reverse restoration. The PB
adapter must never fall back to RemoveItem/StoreItem/EquipItem/SwapItem/DestroyItem.

## Four results

- `Applied`: every exact requested move completed, native effect journals settled,
  context/custody/queues verified, quarantine released. `failure == None`, recoveryCount zero.
- `Refused`: no inventory move was accepted and the relevant observed original state is
  unchanged. A preflight policy callback that changes verifiable state cannot obtain this
  result merely by returning a refusal. Stamp invalidation itself may occur on refusal.
- `Restored`: a failed forward attempt was reversed in strict reverse order using only
  completed journal stages. Original exact items/slots, native combat modifier arrays,
  published fields, item sets, owned/applied aura observations, cooldowns and save/update
  registrations were re-proven. Deferred intents remain retained. This is in-memory proof,
  not DB commit evidence, permission to continue gearing, or a reason to repeat callbacks.
- `RecoveryRequired`: any partial/uncertain effect, opaque custom effect callback on a
  failed operation, failed reverse step or unprovable context/links/effects. The owner stays
  quarantined. Never retry, recreate, mail, sell, delete or discard an original automatically.

`InventoryBatchResult` contains `outcome`, `failure`, `recoveryGuids` (fixed capacity 19)
and `recoveryCount`. Only the first recoveryCount entries are meaningful. GUIDs identify
originals whose original full instance/location could not be re-proven; they are not a
list of destroyed items. An effects-only recovery may have zero GUIDs. Any inconsistent
Applied result or unknown result value must fail closed in the adapter. A PB fingerprint
mismatch must not be interpreted as success, including for Restored.

## Supported scope and conservative refusals

Up to 19 distinct original single items, combat equipment to initially empty storage or
storage to initially empty combat equipment, with no source/destination overlap. BODY,
TABARD, bags as moved items, stacks, bank and mail are excluded by native admission.
A destination vacated by an earlier move is not accepted. Multi-step replacement therefore
uses explicitly captured empty staging positions, not implicit swapping or merging.

Before mutation, refuse timed originals/enchantments, on-obtain spells, unsupported item
instance states, or equipment that would require new binding. Snapshot bounds are 512
unique owned/applied auras, 128 applications per aura, 128 active item sets and 4096 dirty
queue entries. Exceeding a bound refuses safely. Automatic two-handed offhand/mail fallback
is excluded; an occupied offhand must be moved by an explicit earlier plan step if native
policy requires it. Native CanUnequip/CanStore/CanEquip and existing script hooks still run.
No new content/progression/equipment eligibility rule is selected by this draft.

## Mutation, callback and lifetime boundary

A synchronous reservation rejects supported re-entrant mutations before native writes;
an internal cancellation exception is caught by the executor. It is not an exception API
for network/tick callers. Exact original items, destination/source containers and preexisting
dirty queue items are protected by pointer and owner/GUID keys. The executor's private
link/queue methods are not a broad callback-borrowable write permit.

Shared Item/Bag setters, creation/loading, save/deletion, bag-slot writes and world entry/exit;
Player storage, save, level/spell/talent/session/world mutation; and session/map retirement
producers check the batch protection. Standalone-save/logout intent is recorded before
rejection without starting DB work or changing native logout flags. Other producer work
is rejected rather than silently replayed. Active/offline session cleanup and reconnect
replacement retain quarantined owners. No custom hook is suppressed. Actual script dispatch
is marked as opaque during mutating effects; a later failure cannot claim arbitrary external
script state was reversed.

The separate mutex/atomic quarantine registry contains copied keys only. Cross-thread Item
load/row-deletion guards consult it without resolving a Player. A thread-local frame alone
is not cross-thread protection. The Player ledger, raw Player/Item use and engine writes
still require their existing serialized native context. This does not make foreign-thread
Player access safe or race-proof arbitrary external code. The inherited World::Update performs
world session maintenance before MapMgr::Update, whose worker phase waits before returning;
this ordering supports those engine producer guards, not arbitrary foreign-thread calls. Direct delete, raw field/SQL writes,
manual Object-base bypass and unguarded third-party module destruction are outside this
supported producer contract and must not be used by the integration. Process shutdown is
not recovery. Do not infer that a successful fixture proves every module retirement path.

`RecoveryRequired` has no timeout/reset escape or public clear-quarantine API. Native safe
reconciliation/operator recovery and producer replay remain release gates; the dormant
adapter must not enable this on a live server. Dropping a failed operation's result is unsafe.

## Executable evidence and its limits

Native integration follow-up: Claude's full integrated build of `7fdc3a65` failed
because `Item::AddPB59ToUpdateQueueOf` was not a friend of Player. The correction
adds only that specific friendship; queue fields remain protected and the helper
remains private. `test_item_queue_access.py` extracts the actual friend/member
declarations and complete queue helper bodies: seven ordinary queue checks pass,
and deleting the helper friendship reproduces the protected-access compiler error.
This access-control fixture still doubles the remaining engine members. The exact
corrected full integrated build remains a separate gate; see coordination #51
comment 7134437862 for the original native failure evidence.

Local standalone GCC 16.2.0, C++20, warnings-as-errors:

- `test_inventory_batch_native.py`: actual executor/admission/stamp/journal/item-mod bodies,
  real PB59 ledger/reservation/facts headers; 986 assertions in ordinary and optimized
  NDEBUG modes. Covers 1/2/4/17 moves, bag spill, all forward rejection positions, reverse
  order, storage-to-equipment restoration, policy/effect/link failures, pending intent,
  re-entry, context loss, opaque effects, exact recovery GUIDs and hidden-modifier divergence.
- `test_inventory_reservation_guards.py`: 40 checks using actual Item deletion/SQL guard
  bodies, actual producer prologues/SetPlayer/KickSession and actual cleanup gates. Includes
  persistent quarantine, registry query from a worker and ordinary unreserved behavior.
- Existing actual raw relocation and visible-field body suites pass. Existing original
  queue/pointer/combat suites: 22 + 21 + 43 + 293 + 100 + 35 checks pass.
- Actual-ledger admission 27 and lifetime/re-entry 31 checks pass; actual item-mod callback
  boundary control and early-stop checks pass.

Engine, DB, aura/map/policy infrastructure and executor physical transfer are explicit
doubles. The raw transfer body is exercised independently, not linked into the executor
fixture. No full Player translation-unit/Worldserver/module ABI, real DB durability,
production tuple, real hook behavior or gameplay pass is claimed. Exact native compilation,
source-coupled Playerbots integration, independent fault injection and disposable gameplay
validation remain required before any release decision. Hosted workflows were not invoked.

Reproduce with Python 3 and a compatible C++20 compiler:

```text
python -B tests/pb59/test_inventory_batch_native.py --compiler g++
python -B tests/pb59/test_inventory_batch_native.py --ndebug --compiler g++
python -B tests/pb59/test_inventory_reservation_guards.py --compiler g++
python -B tests/pb59/test_item_queue_access.py --compiler g++
python -B tests/pb59/test_original_queue_custody.py --cxx g++
python -B tests/pb59/test_inventory_custody_body.py --cxx g++
python -B tests/pb59/test_inventory_custody_body.py --visible --cxx g++
```

SQL style passes. The C++ repository-wide style check still reports four inherited
untouched diagnostics: BattlegroundAV.h lines 56/797, MoveSpline.cpp line 224 and the preexisting trailing-whitespace line in Spell.cpp
(baseline line 3896; shifted by this draft). There are no SQL/config/DBC/client edits in this milestone.

## Preservation and rollback

The API has no runtime caller on this branch. All F14/Paid/visual-concealment configuration
and established progression/custom behavior remain as inherited. The new guards take their
ordinary paths when no batch/quarantine exists. Shared hook macros only record actual
callbacks for an active effects phase and preserve normal dispatch. Native spell preparation
also marks opaque effects because a cast can change remote targets/charges without scripts.

Development rollback is to discard this draft branch or revert its coherent executor commit
and dependent PB integration together. No migration is required. Any future installation
requires exact installed-source/module inventory, recoverable character/config/binary backups,
verified restoration and separate CosmicCuddle approval. Do not hot-revert code around an
in-memory quarantined owner or pretend a process restart has repaired its originals.
