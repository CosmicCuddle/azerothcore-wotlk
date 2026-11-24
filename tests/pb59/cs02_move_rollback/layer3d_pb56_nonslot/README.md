# CS02 Layer 3d: non-slot custody (K9/K10), test-first spec and feasibility check

Test-only and local. No hosted CI. This is not a worldserver build and not gameplay. Every scenario is synthetic: it runs real
PB56 66c66ddc member bodies and the real Core74 c779e7b4 executor bodies against the Codex fixture doubles, using the PR77/PR80/PR83
staging. It answers CS01's ask on #52 (7142198055). Count stays protected custody, and I2/K2 remain conservative stops
that this layer does not relax.

## What runs

| Configuration | What it is |
|---|---|
| `pb56` | The real PB56 66c66ddc, unchanged. The K9/K10 gap is expected to show here as divergences. |
| `pb56-sketch` | **A test-only throwaway sketch.** The staged copy of PB56's `Fingerprint` records `{GUID, owner, entry, count}` for shirt, tabard and bag-slot items instead of the GUID alone. It is never committed to any PB branch; CS01 owns the PB source. |

Each configuration builds these binaries per toolchain:
- adapter ON: every PR77, PR80 and PR83 row plus the L3d rows;
- adapter OFF;
- factory;
- adapter ON with `CS02_REALCAP=1`: a fixture capture double made closer to the real `Player::CapturePB59InventorySlot`. It refuses base positions 3, 18 and 19..22 (PlayerStorage.cpp:531-534, :551-552); it refuses bag contents when the container's owner or slot doesn't match (:543-546); and it refuses Bag items (:564). Only the L3d rows run in this build.

The toolchains are GCC 14.4 and Clang 19.1.7, each plain `-O2` and ASan/UBSan with `-D_GLIBCXX_ASSERTIONS`, all with `-Werror`.

## L3d rows (expectations pre-registered)

| Rows | What they check | Expected |
|---|---|---|
| D S | Shirt E3, tabard E18 and bag E19: owner change with the same GUID, identity change, same-entry identity change, count 1→2, and moving location | RecoveryRequired |
| D C | Durability-only change on each item, and no change at all (controls) | Done |
| Z | Native RR, and lost context (Player* swap) | 0 Player reads and 0 Item-field reads after Core |
| Z | Applied with the same context | 0 Item-field reads before the SameContext re-resolve |
| V (REALCAP only) | A spare Bag in backpack slot E33 | Strip Refused before Core: the availability observation |

The same-entry identity rows are late additions. They were added after sketch mutant X3 survived, because fixture entry = GUID + 100. No
expectation was changed.

Run:
`PATH=/workspace/cxx/bin:$PATH python3 run_layer3d.py --pb56 <pb@66c66ddc> --core74 <core@c779e7b4> --compiler g++ clang++ --sabotage`

## Results, 24 Nov 2026 (see RESULTS-20261124.txt)

All 4 toolchain and mode combinations give identical rows, with every exit code 0.

**L3d rows:**

| Configuration | Adapter ON (22 rows) | REALCAP build (23 rows) |
|---|---|---|
| `pb56` actual | 6 divergences: shirt owner, shirt count, tabard owner, tabard count, bag owner, bag count. All observed Done where RecoveryRequired was expected. | 5 divergences. Bag owner becomes RecoveryRequired, because the real capture refuses the contents of a foreign-owned bag. Bag count stays Done. |
| `pb56-sketch` | 0 divergences | 0 divergences |

- In every build, the identity, same-entry identity, location and control rows match expectations.
- Z rows: 0 Player reads and 0 Item-field reads after native RR or lost context. With the sketch, the 9 Item-field reads all happen after the SameContext re-resolve (0 before it).
- V row: a spare bag in the backpack refuses the strip before Core, as expected.

**Earlier rows, sketch vs actual:** every PR77, PR80 and PR83 row is unchanged except PR83 K9 and K10, which become RecoveryRequired. A7, I2 and K2 still diverge, so count remains a conservative stop.

**Sketch mutants:** 4 of 4 killed (X1 owner, X2 count, X3 identity, X4 `others` not compared).
