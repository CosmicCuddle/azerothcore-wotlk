#!/usr/bin/env python3
"""CS02 Layer 3b runner: PB54 ec276855 regression NEXT TO the PB53 96c579e8 baseline, both against REAL Core74 c779e7b4.

Test-only and local (no hosted CI); not a worldserver build, not gameplay. See README.md.
  run_layer3b.py --pb53 <mod-playerbots@96c579e8> --pb54 <mod-playerbots@ec276855> --core74 <azerothcore@c779e7b4>
                 [--compiler g++ clang++] [--sabotage] [--log DIR] [--write-pins]
The unchanged PR77 Layer 3 rows (../layer3_pb53/l3_adapter_checks.inc) run first in every binary, then the new
rows (l3b_checks.inc). The PR77 file is copied into the temp dir with 5 asserted harness-only patches (trace,
name, incarnation/outcome knobs); the file in the repo is not changed.
"""
import argparse, importlib.util, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
L3 = HERE.parent / "layer3_pb53"
spec = importlib.util.spec_from_file_location("l3", L3 / "run_layer3.py")
l3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(l3)
blob, body, once, head_of, clean = l3.blob, l3.body, l3.once, l3.head_of, l3.clean

HEADS = {"pb53": "96c579e86443c86780d6f9cd8ef51b7c252b09f8", "pb54": "ec276855d869bdb30b9aa1907739e386e0fcac1e"}
CORE_HEAD = l3.CORE_HEAD
CP, TA, EXEC_H = l3.CP, l3.TA, l3.EXEC_H
PB_FILES, CORE_FILES = l3.PB_FILES, l3.CORE_FILES
PINS = HERE / "PINS.txt"
SIG = ("SCN", "MATRIX", "CS02 layer3", "FAIL", "SRC")

HARNESS_PATCHES = [  # applied to a temp copy of the PR77 checks file (doubles/instrumentation only)
    ("struct Bot\n{\n", "struct Bot;\nstatic std::vector<std::pair<Bot const*, std::string>>* g_trace = nullptr;\nstruct Bot\n{\n"),
    ("        ++reads;\n", "        ++reads;\n        if (g_trace) g_trace->push_back({this, m});\n"),
    ("    mutable PB59::InventoryBatchResult last{};\n",
     "    mutable PB59::InventoryBatchResult last{};\n    std::string name; mutable uint64 incOverride = 0; int forceOutcome = -1; unsigned forceCount = 0;\n"
     "    std::string GetName() const { R(\"GetName\"); return name; }\n"),
    ("{ R(\"OwnerIncarnation\"); return p->_pb59Transaction.OwnerIncarnation(); }",
     "{ R(\"OwnerIncarnation\"); return incOverride ? incOverride : p->_pb59Transaction.OwnerIncarnation(); }"),
    ("        last = p->ExecutePB59InventoryBatchPlan(owner, oi, si, stamp, moves);\n",
     "        last = p->ExecutePB59InventoryBatchPlan(owner, oi, si, stamp, moves);\n"
     "        if (forceOutcome >= 0) last.outcome = static_cast<PB59::InventoryBatchOutcome>(forceOutcome);\n"
     "        if (forceCount) last.recoveryCount = uint8(forceCount);\n"
     "        if (g_trace) g_trace->push_back({nullptr, \"CORE_RETURN\"});\n"),
]
ADAPTER = l3.ADAPTER + [("bool SameContext(ObjectGuid guid, WorldSession* session, uint64 ownerIncarnation,", True)]
TAIL_ANCHOR = ('"PB59 AutoGear bot={} sender={} refused: the raid source index is not loaded",\n'
               '            bot->GetGUID().ToString(), sender->GetGUID().ToString());\n')
PRELUDE = r'''// Runner-assembled. LOG_* now EVALUATE their arguments (Layer 3 used no-ops), so a log that reads the bot is counted.
#undef LOG_ERROR
#undef LOG_DEBUG
#undef LOG_INFO
namespace cs02log { inline int g_logs = 0; template <class... A> void Log(A const&... a) { ((void)a, ...); ++g_logs; } }
#define LOG_ERROR(...) ::cs02log::Log(__VA_ARGS__)
#define LOG_DEBUG(...) ::cs02log::Log(__VA_ARGS__)
#define LOG_INFO(...) ::cs02log::Log(__VA_ARGS__)
// DOUBLE of ObjectAccessor::FindConnectedPlayer (registry; default = the adapter's own bot by GUID).
namespace ObjectAccessor
{
inline std::function<Bot*(ObjectGuid)> g_find;
inline std::map<uint64, Bot*> g_connected;
inline Bot* g_adapterBot = nullptr;
inline Bot* FindConnectedPlayer(ObjectGuid g)
{
    if (g_trace) g_trace->push_back({nullptr, "LOOKUP:" + std::to_string(g.value)});
    if (g_find) return g_find(g);
    if (!g_connected.empty()) { auto it = g_connected.find(g.value); return it == g_connected.end() ? nullptr : it->second; }
    return g_adapterBot && g_adapterBot->p->GetGUID().value == g.value ? g_adapterBot : nullptr;
}
}
namespace cs02script { inline std::function<bool(uint32, uint32, bool)> factory; }
namespace cs02pb
{
using Player = ::Bot; // inside this namespace the PB bodies' Player* is the counting facade
class PlayerAutoGearInventory
{
public:
    PlayerAutoGearInventory(Bot* bot, uint32 quality, void const* sources) : _bot(bot), _quality(quality), _sources(sources)
    { ObjectAccessor::g_adapterBot = bot; }
'''
EPILOGUE = r'''    // SCRIPTED (double): the real members drive PlayerbotFactory, which is not compiled here.
    bool RunFactory(uint32 q, uint32 c, bool i)
    {
        if (g_trace) g_trace->push_back({nullptr, "FACTORY"});
        return cs02script::factory ? cs02script::factory(q, c, i) : true;
    }
    bool ApplyBisOverlay(uint32) { return true; }
    void ApplyFinishers(uint32) { if (g_trace) g_trace->push_back({nullptr, "FINISHERS"}); }
    Bot* _bot;
    uint32 _quality;
    void const* _sources;
    bool _stripping = false;
};
}
using cs02pb::PlayerAutoGearInventory;
'''

def mutants(cfg):
    """PB54 guard mutants (temp copy only)."""
    if cfg != "pb54":
        return []
    SC = "        bool SameContext("
    return [
        ("M1 Execute early return removed", EXEC_H, "    if (outcome.factoryStopped || outcome.bisRestoreFailed)\n    {\n        outcome.ran = true;\n"
         "        outcome.capHeld = false;\n        outcome.refusal = Refusal::RecoveryRequired;\n        return outcome;\n    }\n", ""),
        ("M2 early return claims capHeld", EXEC_H, "        outcome.capHeld = false;\n        outcome.refusal = Refusal::RecoveryRequired;\n        return outcome;",
         "        outcome.capHeld = true;\n        outcome.refusal = Refusal::RecoveryRequired;\n        return outcome;"),
        ("M3 early return without refusal", EXEC_H, "        outcome.refusal = Refusal::RecoveryRequired;\n        return outcome;\n    }\n\n    // Only items",
         "        return outcome;\n    }\n\n    // Only items"),
        ("M4 RunCoreBatch RR early return removed", TA, "            if (outcome == CoreBatchOutcome::RecoveryRequired)\n            {", "            if (false)\n            {"),
        ("M5 SameContext skipped", TA, "            if (!SameContext(botGuid, session, ownerIncarnation, sessionIncarnation))",
         "            if (false && !SameContext(botGuid, session, ownerIncarnation, sessionIncarnation))"),
        ("M6 SameContext: no pointer compare", TA, "            return current == _bot && current->GetSession() == session &&",
         "            return current && current->GetSession() == session &&"),
        ("M7 SameContext: no owner incarnation", TA, "current->GetPB59OwnerIncarnation() == ownerIncarnation &&", "true &&"),
        ("M8 SameContext: no session incarnation", TA, "ownerIncarnation &&\n                   session->GetPB59SessionIncarnation() == sessionIncarnation &&",
         "ownerIncarnation &&\n                   true &&"),
        ("M9 SameContext: no logout check", TA, "sessionIncarnation && current->IsInWorld() &&\n                   !session->PlayerLogout();\n        }",
         "sessionIncarnation && current->IsInWorld() &&\n                   true;\n        }"),
        ("M10 outer: AI not rechecked", TA, "        GET_PLAYERBOT_AI(bot) != ai)", "        false)"),
        ("M11 outer: sender not rechecked", TA, "        !same(senderId, sender, senderSession, senderIncarnation, senderSessionIncarnation) ||", "        false ||"),
        ("M12 outer: no pointer compare", TA, "return current == expected &&", "return current &&"),
        ("M13 outer: no logout check", TA, "!session->PlayerLogout();\n    };", "true;\n    };"),
        ("M14 outer: whisper uses live name", TA, "        senderName);\n    return true;", "        sender->GetName());\n    return true;"),
        ("M15 recovery list unbounded", TA, "std::size_t const listed = std::min<std::size_t>(result.recoveryCount, result.recoveryGuids.size());",
         "std::size_t const listed = result.recoveryCount;"),
        ("M16 outer: log reads live bot GUID", TA, "             botText, senderText, request.rawStage", "             bot->GetGUID().ToString(), senderText, request.rawStage"),
        ("M17 outer: no owner incarnation", TA, "current->GetPB59OwnerIncarnation() == incarnation &&", "true &&"),
        ("M18 outer: no session incarnation", TA, "== incarnation &&\n               session->GetPB59SessionIncarnation() == sessionIncarnation &&",
         "== incarnation &&\n               true &&"),
    ]


def stage(pb, core, work, mutant=None):
    pbtext = {f: (pb / f).read_text() for f in PB_FILES}
    if mutant:
        pbtext[mutant[1]] = once(pbtext[mutant[1]], mutant[2], mutant[3], mutant[0])
    pbinc = work / "pbinc"; pbinc.mkdir(exist_ok=True)
    for f in PB_FILES:
        if f.endswith(".h") and f.startswith("src/Bot/") and "Factory" not in f:
            (pbinc / Path(f).name).write_text(pbtext[f])
    storage = (core / CP / "PlayerStorage.cpp").read_text(); player = (core / CP / "Player.cpp").read_text()
    executor = (core / CP / "PlayerPB59Inventory.cpp").read_text(); charm = (core / "src/server/game/Entities/Unit/CharmInfo.h").read_text()
    checks = (L3 / "l3_adapter_checks.inc").read_text()
    for old, new in HARNESS_PATCHES:
        checks = once(checks, old, new, old[:40])
    incs = {
        "Admission.inc": body(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": body(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": body(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": body(player, "void Player::_ApplyItemMods("),
        "Executor.inc": body(executor, "PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan("),
        "GlobalCooldownMgr.inc": body(charm, "class GlobalCooldownMgr ").rstrip("\n") + ";\n",
        "Checks.inc": "#define main cs02_l3_main\n" + checks + "#undef main\n" + (HERE / "l3b_checks.inc").read_text(),
    }
    ta = pbtext[TA]
    members = []
    for sig, gated in ADAPTER:
        if sig not in ta:
            continue  # SameContext exists only in PB54
        text = body(ta, sig, struct=sig.startswith("struct"))
        members.append(("#if PB59_AUTOGEAR_CORE_BATCH\n" + text + "#endif\n") if gated else text)
    incs["AdapterMethods.inc"] = PRELUDE + "".join(members) + EPILOGUE
    fn = body(ta, "bool RunPB59AutoGearWorldPlan(")
    tail = fn[fn.index(TAIL_ANCHOR) + len(TAIL_ANCHOR):]
    incs["OuterTail.inc"] = ("namespace cs02pb\n{\n" + body(ta, "std::string RefusalText(PlayerbotsAutoGearExec::Refusal refusal)") +
        "// REAL PB tail of RunPB59AutoGearWorldPlan (verbatim from the line after the sources-unavailable log to the end)\n"
        "bool OuterTail(Player* bot, PlayerbotAI* ai, Player* sender, PlayerbotsAutoGearExec::Plan const& plan, int const& sources,\n"
        "    Req const& request, Dec const& decision)\n{\n" + tail + "\n}\n")
    pf = pbtext[l3.PF]
    incs["DestroyObsoleteAmmo.inc"] = body(pf, "static void DestroyObsoleteAmmo(Player* bot, ItemTemplate const* ammo)")
    incs["InitAmmo.inc"] = body(pf, "void PlayerbotFactory::InitAmmo()")
    incs["DisplacesOffhand.inc"] = body(pf, "bool PlayerbotFactory::DisplacesOffhand(Player* bot, ItemTemplate const* mainhand)")
    for name, text in incs.items():
        (work / name).write_text(text)
    fixture = (core / "tests/pb59/inventory_batch_native_fixture.cpp").read_text()
    for old, new in l3.FIXTURE_PATCHES:
        fixture = once(fixture, old, new, old[:40])
    (work / "adapter_tu.cpp").write_text(fixture)
    (work / "factory_tu.cpp").write_text((L3 / "l3_factory.cpp").read_text())


ASAN = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pb53", required=True); ap.add_argument("--pb54", required=True); ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"]); ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log"); ap.add_argument("--write-pins", action="store_true")
    a = ap.parse_args()
    repos = {"pb53": Path(a.pb53).resolve(), "pb54": Path(a.pb54).resolve()}
    core = Path(a.core74).resolve()
    for name, repo in list(repos.items()) + [("core74", core)]:
        want = HEADS.get(name, CORE_HEAD)
        if head_of(repo) != want: sys.exit(f"{repo} head {head_of(repo)} != pinned {want}")
        if not clean(repo): sys.exit(f"{repo} is not clean")
    blobs = {}
    for name, repo in repos.items():
        blobs.update({f"{name}:{f}": blob((repo / f).read_bytes()) for f in PB_FILES})
    blobs.update({f"core74:{f}": blob((core / f).read_bytes()) for f in CORE_FILES})
    blobs["cs02:layer3_pb53/l3_adapter_checks.inc"] = blob((L3 / "l3_adapter_checks.inc").read_bytes())
    if a.write_pins:
        PINS.write_text("".join(f"{k} {v}\n" for k, v in HEADS.items()) + f"core74 {CORE_HEAD}\n" + "".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = {l.split(" ", 1)[1]: l.split(" ", 1)[0] for l in PINS.read_text().splitlines()[3:] if l}
    for k, v in blobs.items():
        if pinned.get(k) != v: sys.exit(f"{k} blob {v} != pinned {pinned.get(k)}")
    print(f"PB53 {HEADS['pb53']} | PB54 {HEADS['pb54']} | Core74 {CORE_HEAD}; {len(blobs)} blobs verified")
    status = 0
    baselines = {}
    with tempfile.TemporaryDirectory(prefix="cs02-l3b-") as tmp:
        for cfg, repo in repos.items():
            print(f"=== configuration {cfg} ({HEADS[cfg][:8]}) ===")
            print("\n".join(l3.source_checks(repo)))
            work = Path(tmp) / cfg; work.mkdir()
            stage(repo, core, work)
            base = None
            for cxx in a.compiler:
                for mode, flags in (("plain", ["-O2"]), ("asan-ubsan", ASAN)):
                    tag = f"{cfg}-{Path(cxx).name}-{mode}"
                    codes, out = l3.suite(cxx, flags, work, core, tag)
                    if codes is None: sys.exit(out[-6000:])
                    if a.log:
                        Path(a.log).mkdir(parents=True, exist_ok=True); (Path(a.log) / f"{tag}.log").write_text(out)
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    if base is None:
                        base = sig; print("\n".join(f"[{cfg}] {l}" for l in sig))
                    totals = [l for l in out.splitlines() if l.startswith("CS02 layer3")]
                    print(f"{tag}: exits {codes}; " + " | ".join(totals) + f"; {'identical to first toolchain' if sig == base else 'DIFFERS'}")
                    status |= any(codes) or sig != base
            baselines[cfg] = base
            if a.sabotage:
                ms = mutants(cfg); killed = 0
                for m in ms:
                    stage(repo, core, work, m)
                    codes, out = l3.suite(a.compiler[0], ASAN, work, core, "mut", strict=False)
                    if codes is None:
                        print(f"[SABOTAGE] {m[0]:<45} NOT RUN (does not compile): {out[-400:]}"); continue
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    changed = [l for l in sig if l not in base]
                    dead = any(codes) or sig != base
                    killed += dead
                    why = changed[0][:160] if changed else ("exit " + str(codes) + " " + (out.strip().splitlines() or [""])[-1][:120])
                    print(f"[SABOTAGE] {m[0]:<45} {'killed' if dead else 'SURVIVED'}" + (f"  first change: {why}" if dead else ""))
                stage(repo, core, work)
                if ms: print(f"[SABOTAGE] {killed}/{len(ms)} killed")
        a53 = set(baselines["pb53"]); a54 = set(baselines["pb54"])
        print("=== rows that differ PB53 -> PB54 ===")
        for l in baselines["pb54"]:
            if l not in a53 and l.startswith(("SCN", "MATRIX", "SRC")):
                print("PB54 " + l)
        for l in baselines["pb53"]:
            if l not in a54 and l.startswith(("SCN", "MATRIX", "SRC")):
                print("PB53 " + l)
    return status


if __name__ == "__main__":
    sys.exit(main())
