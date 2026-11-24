# CS02 Layer 3c: PB56 focused rerun next to the PB54 baseline (test-only, local)

Asked for by CS01 in coordination #52 7140837639 and #79 7140838376. Michael approved this at 18:41 UK on 24 Nov 2026. Independent tester: CS02/Grok.
These tests were written independently of CS01's `tests/acceptance/autogear/test_batch_custody.cpp`; nothing was copied from it.
These are local test files only. They are **not** a worldserver build, **not** gameplay, and there are **no** hosted CI runs.

## Configurations
| Config | PB source | Core |
|---|---|---|
| `pb54` (baseline) | mod-playerbots#54 `ec276855d869bdb30b9aa1907739e386e0fcac1e` | Core74 `c779e7b4…` (not repinned) |
| `pb56` (candidate) | mod-playerbots#56 `feature/cs01-pb54-applied-custody-20261124` `66c66ddc3dc07ea81b5f27d96fec4f77f907afe3` (TrainerAction.cpp b2b16988, AutoGearBatchCustody.h 0691aa07) | same |

- `PINS.txt` holds the PB blobs for both configurations, the Core74 blobs, and the blobs of the PR77/PR80 harness files this runner reuses.
- PR77 (`../layer3_pb53`, PB53) and PR80 (`../layer3b_pb54`, PB53+PB54) are unchanged. Their own runners still reproduce their committed RESULTS files.

## What runs in every binary
1. The unchanged PR77 Layer 3 rows. This covers B1/B2 post-stop, A3p/A4p, adapter OFF (D1–D3), F1 old ammo, B3 original gear restored, and C1–C6 mapping.
2. The unchanged PR80 Layer 3b rows: N post-stop, R SameContext/unknown/counts, O outer caller and whisper, and I0–I7 custody.
3. The new rows (`l3c_checks.inc`):
   - **K0–K12:** custody after Core reports Applied. The setup adds bag 5000 at E19, shirt 950 at E3, tabard 960 at E18, and item 802 at E31 with the same entry and count as 800.
     - K1: flags and enchant only. K2: count only.
     - K3: after-snapshot missing.
     - K4–K7: non-slot GUID changes (shirt replaced, tabard removed, bag replaced, shirt moved).
     - K8: non-slot metadata.
     - K9/K10: non-slot owner change with the same GUID.
     - K11: swap of two items with the same entry and count.
     - K12: a bag added mid-batch.
     - Every K row also requires that planned items 700/701 are still owned exactly once.
   - **L1–L4:** the custody check adds no Player reads.
     - L1: the Applied control makes exactly the 47 observation reads it made at PB54.
     - L2: a discrepancy stops early with ≤47 reads.
     - L3: native RR plus a custody change gives 0 reads.
     - L4: lost context plus a custody change gives 0 reads of the old bot.

## What is real and what is a double
This is the same boundary as PR80 (see `../layer3b_pb54/README.md`), plus the REAL PB56 `AutoGearBatchCustody.h` and the REAL wiring in `RunCoreBatch`.
- Custody changes are made by the fixture's `policyHook` while Core is running. Whether natives can actually reach these changes has not been established (Codex #51 7140735652).
- A RecoveryRequired here is a command stop. It is **not** proof of native quarantine.

## Run
```
PATH=/workspace/cxx/bin:$PATH python3 run_layer3c.py --pb54 <mod-playerbots@ec276855> --pb56 <mod-playerbots@66c66ddc> \
    --core74 <azerothcore@c779e7b4> --compiler g++ clang++ --sabotage --log <dir>
```
`--sabotage` runs 11 comparator mutants (C1–C11) and 5 wiring mutants (W1–W5) under ASan+UBSan with `-D_GLIBCXX_ASSERTIONS`. The ASan builds of this layer also add `-D_GLIBCXX_ASSERTIONS`, which PR77/PR80 do not. Rows K13 and K13b, the assertions flag, and a harness-only `afterCore` hook in the facade (which runs a callback when the native call returns) were added after earlier mutant runs (W5 and W2 survived). All are labelled.
