# PB59 equipment authority — Core writer audit

Owner: CosmicCuddle. Maintenance CS01. Base Core `21ff1973bc75216f8a0d0f3d7731eb3e8894ff97`. Design-only; no runtime, schema, config or deployment change.

## Exact source inventory
`src/server/game/Entities/Player/PlayerStorage.cpp` blob `c3117845d1b92b2e11f0362f6c8e2d6652857165`:

| Line | Writer | Classification |
|---|---|---|
| 2746 | `_StoreItem` | base-container write; audit destination eligibility, guard combat positions defensively |
| 2996 | `VisualizeItem` | equipment pointer write, reached by EquipItem and QuickEquipItem |
| 3039 | `RemoveItem` | direct removal, item mods and other effects precede pointer clear |
| 3202 | `DestroyItem` | destruction, callbacks and visible-slot changes precede pointer clear |
| 4082 | `AddItemToBuyBackSlot` | vendor buyback range, not combat equipment |
| 4118 | `RemoveItemFromBuyBackSlot` | vendor buyback range, not combat equipment |
| 5167 | `LoadFromDB` | clears equipment during load; no valid authority until login complete |

`Player.cpp` blob `324b3d2fe1213a631726bdd6d3fdea582523610f`: constructor initializes m_items (505); GiveLevel calls SetLevel (2521), after level packet and scaling removal. Audit other SetLevel call sites and module writers before claiming completeness.

## Proposed contract (not implementation)
- Owner-thread invalidation before first equipment/level side effect; no foreign Player pointer reads. Existing OnPlayerEquip/Unequip/AfterSetVisibleItemSlot/LevelChanged are too late for immediate invalidation.
- Prefer generic scoped mutation boundary with nested depth, invalidation on outer entry, publication only at a known stable owner-thread boundary after all nested operations. Do not publish at every primitive exit: SwapItem can remove and equip in separate calls (3748-3780), exposing partial state; include outer compound operations and audit other callers.
- Load/login starts Unknown; logout invalidates session identity; no valid publication while loading. Noncombat buyback/cosmetics need not dirty raw combat average unless another eligibility input changes.
- Final Known-B must match session and progression/equipment revision; later C diagnostic, no global lease. Snapshot value alone is not authority. Missing hook/no-IP paths fail closed when feature enabled.
- Distinguish a failed operation from a genuine no-set result. This seam does NOT make inventory replacement transactional or undo callbacks.

## Gates
1. Cross-file/module inventory of direct item writes, level setters, compound operations, callback reentry; audit item-template/scaling changes.
2. Isolated generic Core patch proposal and exact source compile; no shared Core changes until reviewed.
3. Tests: equip, quick equip, removal, destruction, swap, nested callback, failure, load/relog, level up/down, cosmetic/buyback, stale B and no-IP/default OFF. No valid publication during partial mutation.
4. Native composed build and gameplay regression; owner approval before merge/deploy.

Coordination: https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/59
