# CS02 Layer 3b: PB54 regression next to the PB53 baseline (test-only, local)

Asked for by CS01 in coordination #79 7138309430 and 7138547373. Michael approved this at 17:36 UK on 24 Nov 2026. Independent tester: CS02/Grok.
These are local test files only. They are **not** a worldserver build, **not** gameplay, and there are **no** hosted CI runs.

## Configurations (both run in every invocation; neither replaces the other)
| Config | PB source | Core |
|---|---|---|
| `pb53` (baseline) | mod-playerbots#53 `96c579e86443c86780d6f9cd8ef51b7c252b09f8` | Core74 `c779e7b4f735cab048f5b351ca6b9053f188787a` |
| `pb54` (candidate) | mod-playerbots#54 `claude/pb53-recovery-stop-20261124` `ec276855d869bdb30b9aa1907739e386e0fcac1e` | same Core74 (not repinned) |

- `PINS.txt` holds 38 blob hashes: 11 PB files per configuration, 15 Core74 files, and the PR77 checks file. A run with any mismatch stops.
- PB source is read only. It is never vendored or edited. Mutants change only a temporary copy.
- The PR77 baseline is left exactly as it was. `../layer3_pb53/run_layer3.py` (unchanged) still reproduces `../layer3_pb53/RESULTS-20261124.txt` line for line.

## What runs
1. **The unchanged PR77 Layer 3 rows** (A/B/C/D/F, plus SRC). `l3_adapter_checks.inc` is copied into the temp dir with 5 asserted, harness-only patches: a call trace, a `GetName`, owner-incarnation and native-outcome/count override knobs, and a CORE_RETURN marker. The repo file is not changed, and its blob is pinned.
2. **New rows** (`l3b_checks.inc`):
   - **N:** post-recovery stop. After a factory or BiS stop: immediate RR with ran=1 and capHeld=0, flags kept, created item left in place, 0 `Capture()` calls and 0 finishers.
   - **R:** RunCoreBatch after the native call:
     - R0: control (observation happens).
     - R1: a different Player* for the GUID. R1b: Player* differs only.
     - R2: owner incarnation changed. R3/R3b: session incarnation changed. R4/R4b: session logging out. R5: bot no longer connected.
     - R6: unknown native outcome 7. R7: Applied with recoveryCount 255 but an array of 19. R8: RR with an empty list. R9: RR with count 255.
   - **O:** the REAL tail of `RunPB59AutoGearWorldPlan`, copied verbatim from the line after the sources-unavailable log to the end of the function, plus the real `RefusalText`. It checks:
     - O1: success, with the reply only after both re-resolves and the AI check.
     - O1b/O1c/O2b: no bot or sender reads between Execute and the re-resolve. With log arguments evaluated, this shows the logs use copied text.
     - O2: recovery reply after a factory stop.
     - O3–O11: pointer swap, Player*-only swap, bot or sender logout, sender or bot owner incarnation, sender session incarnation, AI replaced, sender disconnected, bot left world. Each must give 0 whispers.
     - O12: sender renamed mid-command. The whisper must use the name copied before the run.
   - **I:** the A7 custody invariant (CS01 #79 7138547373). After Core reports Applied, **custody** (owner, container, slot, GUID presence) may change only for planned moves. **Metadata/native effects** (count, flags, durability, enchants, charges, text, …) are permitted and are only reported. Every row prints the exact changed fields. The original A7 row (any change → RR) is kept as a divergence; the oracle is not adapted.
3. **The real factory pieces** (TU3: ammo, DisplacesOffhand), the same as in PR77.

## What is real and what is a double
- REAL:
  - PB `PlayerAutoGearInventory` members, plus `SameContext` (PB54 only).
  - `Execute`/`PlanExecution`.
  - The outcome mapping headers.
  - The outer caller tail and `RefusalText`.
  - Factory ammo and off-hand code.
  - The Core74 executor, admission, stamp, item mods and combat effects.
- DOUBLES:
  - Codex's fixture Player/Item/Bag/Session, including capture and relocation (the real ones are covered by `core74/native_c779`).
  - The counting Bot facade, used as `Player*` inside a test namespace.
  - `ObjectAccessor::FindConnectedPlayer` (a registry).
  - `PlayerbotAI::Whisper`, `GET_PLAYERBOT_AI`, `PlayerbotTextMgr` (returns the key).
  - Scripted `RunFactory`/`ApplyBisOverlay`/`ApplyFinishers`.
- **Change from Layer 3:** `LOG_*` now evaluate their arguments. A log that reads the bot is therefore counted. That is why A3p/A4p for PB53 read 51/7 here, against 50/6 in PR77.
- Owner deallocation mid-call is not simulated (that would be undefined behaviour). Lifetime is measured by counting accesses and comparing pointers.
- R3b/R4b and R1b/O3b were added after mutant runs (M8/M9; M6/M12 redundancy). They are labelled in the source. No existing expectation was changed.

## Run
```
PATH=/workspace/cxx/bin:$PATH python3 run_layer3b.py --pb53 <mod-playerbots@96c579e8> --pb54 <mod-playerbots@ec276855> \
    --core74 <azerothcore@c779e7b4> --compiler g++ clang++ --sabotage --log <dir>
```
Each compiler runs plain `-O2` and `-O1` ASan+UBSan, all with `-std=c++20 -Wall -Wextra -Werror`. There are 3 binaries per toolchain per configuration (adapter ON, adapter OFF, factory). `--sabotage` runs 18 PB54 guard mutants under ASan+UBSan.
