# CS02 Layer 3e: independent rerun of the actual PB57 source (non-slot custody)

Test-only and local. No hosted CI. This is not a worldserver build and not gameplay. All scenarios are synthetic. Answers CS01's #52 7144147541.

**Pins:** PB57 4e1f253e (only changed file: TrainerAction.cpp, blob 3f56e600), PB56 66c66ddc, Core74 c779e7b4, plus 47 pinned files. The PR77, PR80, PR83 and PR86 files are reused unchanged and blob-pinned.

## What is real code and what is a test double

| Real code | Test doubles |
|---|---|
| PB57's `PlayerAutoGearInventory` member bodies, extracted verbatim. This includes `Fingerprint`, the `others` tuple, and the `SameContext`/comparator gates in `RunCoreBatch`. | Player, Item, Bag, ObjectAccessor and the session. These are Codex's fixture doubles plus the CS02 harness patches. |
| PB57's RefusalText and the tail of `RunPB59AutoGearWorldPlan`. | The `CS02_REALCAP` build only approximates the real `CapturePB59InventorySlot` refusals (PlayerStorage.cpp:531-534, :543-546, :551-552, :564). |
| The real Core74 Admission, InventoryStamp, CombatEffects, ItemMods, Executor and GlobalCooldownMgr bodies. | The "Item-field read" counter instruments the fixture `Item::GetOwnerGUID`, `GetEntry` and `GetCount`. |

The raw Item fields that PB57 reads carry no Core validation (CONTAINED, slot field, ITEM_REMOVED). This harness does not certify them natively.

## Configurations

- `pb57`: the actual source.
- `pb56-sketch`: the PR86 throwaway sketch, run only for reference.

Each configuration builds adapter ON, adapter OFF, factory, and adapter ON with REALCAP.

The **EQUIV** line compares two staged member files with comments stripped: the actual PB57 one, and PB56 with the PR86 SKETCH applied. It reports whether their token streams are identical.

The Y1-Y5 mutants are written against the actual PB57 text.

## Results, 24 Nov 2026

All 4 toolchain and mode combinations give identical rows, with every exit code 0. The toolchains are GCC 14.4 and Clang 19.1.7, each plain and ASan/UBSan with `-D_GLIBCXX_ASSERTIONS`, all `-Werror`.

- **EQUIV:** IDENTICAL (2898 tokens each).
- **Rows, PB57 vs sketch:** 200 signature lines each, 0 differ.
- **PB57 per layer:**

| Layer | Divergences | What diverged |
|---|---|---|
| Layer 3 (109 checks) | 1 | A7 |
| Layer 3b (43) | 1 | the original PR80 divergence |
| Layer 3c (34) | 1 | I2 and K2, kept as conservative count stops |
| Layer 3d (22) | 0 | |
| REALCAP (23) | 0 | |
| Adapter OFF (77 + 16) | 0 | |
| Factory (11) | 0 | |

- **Mutants:** 5 of 5 killed. Y4 (entry dropped) is killed only through the read counter (9 → 6). No row changes entry while keeping the same GUID. This is a known coverage limit that was not broadened, per CS01.

Run: `python3 run_layer3e.py --pb56 <pb@66c66ddc> --pb57 <pb@4e1f253e> --core74 <core@c779e7b4> --compiler g++ clang++ --sabotage`
