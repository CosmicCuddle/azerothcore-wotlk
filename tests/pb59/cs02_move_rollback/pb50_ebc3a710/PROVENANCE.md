# PB50 fixture copy (test fixture only)

Verbatim, unmodified copies from CosmicCuddle/mod-playerbots draft PR50 (Claude's branch
`claude/pb44-inventory-safety-20261124`), exact head `ebc3a710d55c23009216720686644056eab11621`, path `src/Bot/`.
Copied so the CS02 harness can run PB50's real transaction runner (`PlayerbotsAutoGearStorage::Execute`) against the
SYNTHETIC inventory core. They are not built into any server target from here and are not a fork of PB50.

| File | git blob | sha256 |
|---|---|---|
| AutoGearStorageMatching.h | ff8ae579b10049e4fd8da15c61e462e358e43ec8 | 02aef2416a68ea20be5332c2424b5e1dae677cbac415924bc147b3e17761c08f |
| AutoGearStorageTransaction.h | 781e100d96d2e19ecd11d1d280de9b2189ea1e12 | 3a73b2156604b1c5d4dd457253a690877f912a100f58b09e0a00b5c4b2d85537 |
| AutoGearExactStorageObservation.h | b8818d0ba95e3c2f815c460dc1962076416c6204 | 1db3c3e42ec30a4d085631b23e46d3fb0946ef334a58f6f6eac70d7740bb384c |

The StripToBags lambdas (TrainerAction.cpp:700-773) and the hook-relevant parts of PrepareEquipmentStorage /
ValidateEquipmentStorage (PlayerbotFactory.cpp:4155-4366) are transcribed, not copied, in `../Pb50StripAdapter.h`.

## Reuse for Claude's PB50 child (PR51) at 43320882
At `43320882ed18c97ec1f0898318fd152d38f2204f` (branch `claude/pb50-hook-safety-20261124`, draft PR51) the three
files above have the same blobs (ff8ae579 / 781e100d / b8818d0b) and PlayerbotFactory.cpp/.h are unchanged, so these
copies are reused as-is. The changed StripToBags (TrainerAction.cpp) and the per-bot command fence are transcribed in
`../Pb50ChildStripAdapter.h`. H1-H8, the fault matrix and the controls run unchanged against both adapters.
