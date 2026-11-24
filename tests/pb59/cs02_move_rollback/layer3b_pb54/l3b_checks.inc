// ===== CS02 Layer 3b: PB54 regression rows (new). Appended by run_layer3b.py AFTER the unchanged PR77 Layer 3
// rows (layer3_pb53/l3_adapter_checks.inc, whose main() is renamed cs02_l3_main). The same new rows run against
// PB53 96c579e8 (baseline configuration) and PB54 ec276855 (candidate configuration) side by side.
// Expectations were registered before the first run; divergences are printed and kept, never adapted.

// ---- DOUBLES for the outer caller tail (RunPB59AutoGearWorldPlan after the plan is resolved)
namespace cs02pb
{
struct PlayerbotAI
{
    std::vector<std::pair<std::string, std::string>> whispers;
    void Whisper(std::string const& text, std::string const& name)
    {
        whispers.push_back({text, name});
        if (g_trace) g_trace->push_back({nullptr, "WHISPER"});
    }
};
inline std::map<Bot const*, PlayerbotAI*> g_ai;
inline PlayerbotAI* LookupAI(Bot const* bot)
{
    if (g_trace) g_trace->push_back({nullptr, "AI_LOOKUP"});
    auto it = g_ai.find(bot);
    return it == g_ai.end() ? nullptr : it->second;
}
struct PlayerbotTextMgr
{
    static PlayerbotTextMgr& instance() { static PlayerbotTextMgr m; return m; }
    std::string GetBotTextOrDefault(std::string const& key, std::string const&, std::map<std::string, std::string> const&) { return key; }
};
struct Req { uint32 rawStage = 3, effectiveStage = 3; bool progressionContextVerified = true; };
struct Dec { uint32 cap = 300; PlayerbotsAutoGearSelection::CommandForm form = PlayerbotsAutoGearSelection::CommandForm::Plain; };
}
#define GET_PLAYERBOT_AI(x) ::cs02pb::LookupAI(x)
#include "OuterTail.inc"   // REAL PB tail of RunPB59AutoGearWorldPlan + REAL RefusalText, runner-assembled

namespace l3b
{
using PlayerbotsAutoGearExec::StripResult;
using Trace = std::vector<std::pair<Bot const*, std::string>>;

[[maybe_unused]] static bool IsObservation(std::string const& m)
{
    return m == "CaptureSlot" || m == "GetItemByPos" || m == "GetBagByPos" || m == "CaptureStamp" || m == "GetItemByGuid";
}
[[maybe_unused]] static std::size_t Find(Trace const& t, std::string const& marker, std::size_t from = 0)
{
    for (std::size_t i = from; i < t.size(); ++i) if (t[i].first == nullptr && t[i].second == marker) return i;
    return t.size();
}
[[maybe_unused]] static std::string Count(Trace const& t, Bot const* who, std::size_t from, std::size_t to, bool (*pred)(std::string const&), int& n)
{
    std::map<std::string, int> m;
    n = 0;
    for (std::size_t i = from; i < to && i < t.size(); ++i)
        if (t[i].first == who && (!pred || pred(t[i].second))) { ++n; ++m[t[i].second]; }
    std::string s;
    for (auto const& [k, v] : m) s += (s.empty() ? "" : ",") + k + "x" + std::to_string(v);
    return std::to_string(n) + " [" + (s.empty() ? "none" : s) + "]";
}

// ---------------- N: post-recovery stop in Execute (CS01 #52 7137340399, PB54 guard)
[[maybe_unused]] static void StopRows()
{
#if CS02_CORE_BATCH
    using PlayerbotsAutoGearExec::Refusal;
    for (int route = 0; route < 2; ++route)
    {
        auto w = NewWorld();
        Composite c{*w};
        auto stop = [&] {
            w->Put(E(1), 900, 999);
            auto s = w->inv.StripToBags(1u << 15);
            if (s != StripResult::Done) return true;
            w->p.rejectTransfer = w->p.transfers + 1;
            return w->inv.RestoreToSlot(ObjectGuid{700}, 15);
        };
        if (route == 0) c.factory = stop; else c.bis = stop;
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, route == 1, 300));
        std::string const id = route == 0 ? "N1 factory stop" : "N2 BiS stop";
        bool const flag = route == 0 ? o.factoryStopped : o.bisRestoreFailed;
        Row(id + ": created item left in place, flags kept", "900@E1 revoked=0 destroys=0 flag=1",
            std::string(w->At(E(1)) == 900 ? "900@E1" : "900 gone") + " revoked=" + std::to_string(o.revoked) +
            " destroys=" + std::to_string(w->bot.destroys) + " flag=" + std::to_string(flag),
            w->At(E(1)) == 900 && o.revoked == 0 && w->bot.destroys == 0 && flag);
        Row(id + ": Capture()/finishers after stop", "0,0",
            std::to_string(c.capturesAfterStop) + "," + std::to_string(c.finishersAfterStop), c.capturesAfterStop == 0 && c.finishersAfterStop == 0);
        Row(id + ": RR returned immediately", "refusal=RR ran=1 capHeld=0 newItems=0",
            OutcomeText(o), o.refusal == Refusal::RecoveryRequired && o.ran && !o.capHeld && o.newItems == 0);
    }
#endif
}

// ---------------- R: RunCoreBatch context after the native call (SameContext)
struct RunResult { StripResult r; std::string core; std::string obs; int obsN = 0; std::string all; int allN = 0; };
[[maybe_unused]] static RunResult Strip(World& w, std::function<void(World&)> const& hook, char hookAt = 'p')
{
    Trace t;
    g_trace = &t;
    bool fired = false;
    auto once = [&] { if (fired) return; fired = true; hook(w); };
    if (hook) { if (hookAt == 'p') w.p.policyHook = once; else w.p.modsHook = once; }
    RunResult rr;
    rr.r = w.inv.StripToBags((1u << 15) | (1u << 16));
    g_trace = nullptr;
    rr.core = CN(w.bot.last.outcome);
    std::size_t const at = Find(t, "CORE_RETURN");
    rr.obs = Count(t, &w.bot, at, t.size(), IsObservation, rr.obsN);
    rr.all = Count(t, &w.bot, at, t.size(), nullptr, rr.allN);
    return rr;
}
[[maybe_unused]] static void ContextRows()
{
#if CS02_CORE_BATCH
    {
        auto w = NewWorld();
        auto rr = Strip(*w, nullptr);
        Row("R0 control: same context, Core Applied", "Done; observation happens (>0)", std::string(SN(rr.r)) + "; core=" + rr.core + "; obs=" + rr.obs,
            rr.r == StripResult::Done && rr.obsN > 0);
    }
    {
        auto w = NewWorld();
        Player otherP; otherP.guid = 1; Bot other; other.p = &otherP;
        auto rr = Strip(*w, [&](World&) { ObjectAccessor::g_find = [&](ObjectGuid) { return &other; }; });
        ObjectAccessor::g_find = nullptr;
        Row("R1 bot pointer swapped (same GUID)", "RR; 0 reads of old bot after Core", std::string(SN(rr.r)) + "; core=" + rr.core + "; all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
    {
        // Pointer identity alone: a second Player* for the same fixture player (same GUID, session, incarnation).
        auto w = NewWorld();
        Bot other; other.p = &w->p;
        auto rr = Strip(*w, [&](World&) { ObjectAccessor::g_find = [&](ObjectGuid) { return &other; }; });
        ObjectAccessor::g_find = nullptr;
        Row("R1b different Player* only (same GUID/session/inc)", "RR; 0 reads of old bot after Core", std::string(SN(rr.r)) + "; core=" + rr.core + "; all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
    {
        auto w = NewWorld();
        auto rr = Strip(*w, [](World& x) { x.bot.incOverride = 999; });
        Row("R2 owner incarnation mismatch", "RR; 0 observation reads", std::string(SN(rr.r)) + "; core=" + rr.core + "; obs=" + rr.obs,
            rr.r == StripResult::RecoveryRequired && rr.obsN == 0);
    }
    {
        auto w = NewWorld();
        auto rr = Strip(*w, [](World& x) { x.p.session.incarnation = 8; });
        Row("R3 session incarnation mismatch", "RR; 0 observation reads", std::string(SN(rr.r)) + "; core=" + rr.core + "; obs=" + rr.obs,
            rr.r == StripResult::RecoveryRequired && rr.obsN == 0);
    }
    {
        auto w = NewWorld();
        auto rr = Strip(*w, [](World& x) { x.p.session.logout = true; }, 'm');
        Row("R4 session logging out (late hook)", "RR; 0 observation reads", std::string(SN(rr.r)) + "; core=" + rr.core + "; obs=" + rr.obs,
            rr.r == StripResult::RecoveryRequired && rr.obsN == 0);
    }
    // R3b/R4b were ADDED AFTER the first mutant run (M8 survived because the real Core already answers RR to
    // these changes). The native result is forced to Applied so the PB-side check is reached on its own.
    {
        auto w = NewWorld();
        w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::Applied);
        auto rr = Strip(*w, [](World& x) { x.p.session.incarnation = 8; });
        Row("R3b session incarnation mismatch, native Applied (forced)", "RR; 0 observation reads", std::string(SN(rr.r)) + "; obs=" + rr.obs,
            rr.r == StripResult::RecoveryRequired && rr.obsN == 0);
    }
    {
        auto w = NewWorld();
        w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::Applied);
        auto rr = Strip(*w, [](World& x) { x.p.session.logout = true; }, 'm');
        Row("R4b session logging out, native Applied (forced)", "RR; 0 observation reads", std::string(SN(rr.r)) + "; obs=" + rr.obs,
            rr.r == StripResult::RecoveryRequired && rr.obsN == 0);
    }
    {
        auto w = NewWorld();
        auto rr = Strip(*w, [](World&) { ObjectAccessor::g_find = [](ObjectGuid) -> Bot* { return nullptr; }; });
        ObjectAccessor::g_find = nullptr;
        Row("R5 bot no longer connected", "RR; 0 reads of bot after Core", std::string(SN(rr.r)) + "; core=" + rr.core + "; all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
    {
        auto w = NewWorld();
        w->bot.forceOutcome = 7;
        auto rr = Strip(*w, nullptr);
        Row("R6 unknown native outcome 7", "RR", std::string(SN(rr.r)) + "; obs=" + rr.obs, rr.r == StripResult::RecoveryRequired);
    }
    {
        auto w = NewWorld();
        w->bot.forceCount = 255;
        auto rr = Strip(*w, nullptr);
        Row("R7 Applied with recoveryCount 255 (array of 19)", "RR; no out-of-bounds", std::string(SN(rr.r)), rr.r == StripResult::RecoveryRequired);
    }
    {
        auto w = NewWorld();
        w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::RecoveryRequired);
        auto rr = Strip(*w, nullptr);
        Row("R8 forced RR, empty GUID list", "RR; 0 reads after Core", std::string(SN(rr.r)) + "; all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
    {
        auto w = NewWorld();
        w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::RecoveryRequired);
        w->bot.forceCount = 255;
        auto rr = Strip(*w, nullptr);
        Row("R9 forced RR, recoveryCount 255", "RR; 0 reads after Core; no out-of-bounds", std::string(SN(rr.r)) + "; all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
#endif
}

// ---------------- O: outer caller after Execute (revalidation, copied text)
struct Outer
{
    std::unique_ptr<World> w = NewWorld();
    Player sp, swapP;
    Bot sender, swapBot;
    cs02pb::PlayerbotAI ai, ai2;
    Trace t;
    Outer()
    {
        sp.guid = 2; sender.p = &sp; sender.name = "Sender"; w->bot.name = "Bot";
        swapP.guid = 1; swapBot.p = &swapP;
        ObjectAccessor::g_connected = {{1, &w->bot}, {2, &sender}};
        cs02pb::g_ai = {{&w->bot, &ai}};
    }
    ~Outer() { ObjectAccessor::g_connected.clear(); cs02pb::g_ai.clear(); cs02script::factory = nullptr; g_trace = nullptr; }
    bool Run(std::function<bool()> factory)
    {
        cs02script::factory = [factory](uint32, uint32, bool) { return factory(); };
        cs02pb::Req req; cs02pb::Dec dec; int sources = 0;
        g_trace = &t;
        bool const r = cs02pb::OuterTail(&w->bot, &ai, &sender, MakePlan(false, true, false, 300), sources, req, dec);
        g_trace = nullptr;
        return r;
    }
};
[[maybe_unused]] static bool IsIdentity(std::string const& m) { return m == "GetGUID" || m == "GetName"; }
[[maybe_unused]] static void OuterRows()
{
    {
        Outer o;
        bool const r = o.Run([] { return true; });
        std::size_t const fin = Find(o.t, "FINISHERS"), lb = Find(o.t, "LOOKUP:1"), ls = Find(o.t, "LOOKUP:2"), wh = Find(o.t, "WHISPER");
        int nb = 0, ns = 0;
        std::string const b = Count(o.t, &o.w->bot, fin, std::min(lb, wh), IsIdentity, nb);
        std::string const s = Count(o.t, &o.sender, Find(o.t, "FACTORY"), std::min(ls, wh), nullptr, ns);
        bool const ordered = lb < wh && ls < wh && Find(o.t, "AI_LOOKUP") < wh;
        Row("O1 success: reply after revalidation", "true; 1 whisper to Sender; after both lookups+AI",
            std::string(r ? "true" : "false") + "; whispers=" + std::to_string(o.ai.whispers.size()) + (ordered ? "; ordered" : "; NOT revalidated first"),
            r && o.ai.whispers.size() == 1 && o.ai.whispers[0].second == "Sender" && ordered);
        Row("O1b success: bot identity reads after Execute, before re-resolve", "0 (logs use copied text)", b, nb == 0);
        Row("O1c success: sender reads during/after Execute, before re-resolve", "0", s, ns == 0);
    }
    {
        Outer o;
        bool const r = o.Run([] { return false; });
        std::size_t const f = Find(o.t, "FACTORY"), lb = Find(o.t, "LOOKUP:1"), wh = Find(o.t, "WHISPER");
        int nb = 0;
        std::string const b = Count(o.t, &o.w->bot, f, std::min(lb, wh), nullptr, nb);
        bool const rrText = o.ai.whispers.size() == 1 && o.ai.whispers[0].first == "autogear_ip_recovery_error";
        Row("O2 factory stop: recovery reply", "false; 1 RR whisper after revalidation",
            std::string(r ? "true" : "false") + "; whispers=" + std::to_string(o.ai.whispers.size()) + (rrText ? " RR-text" : ""),
            !r && rrText && lb < wh);
        Row("O2b factory stop: bot reads after stop, before re-resolve", "0", b, nb == 0);
    }
    struct Case { char const* id; std::function<void(Outer&)> change; };
    std::vector<Case> const cases = {
        {"O3 bot pointer swapped during command", [](Outer& o) { ObjectAccessor::g_connected[1] = &o.swapBot; }},
        {"O3b bot Player* differs only (same GUID/session/inc)", [](Outer& o) { o.swapBot.p = &o.w->p; ObjectAccessor::g_connected[1] = &o.swapBot; }},
        {"O4 bot session logging out", [](Outer& o) { o.w->p.session.logout = true; }},
        {"O5 sender logging out", [](Outer& o) { o.sp.session.logout = true; }},
        {"O6 sender owner incarnation changed", [](Outer& o) { o.sender.incOverride = 999; }},
        {"O7 sender session incarnation changed", [](Outer& o) { o.sp.session.incarnation = 8; }},
        {"O8 bot owner incarnation changed", [](Outer& o) { o.w->bot.incOverride = 999; }},
        {"O9 bot AI replaced", [](Outer& o) { cs02pb::g_ai[&o.w->bot] = &o.ai2; }},
        {"O10 sender disconnected", [](Outer&) { ObjectAccessor::g_connected.erase(2); }},
        {"O11 bot left world", [](Outer& o) { o.w->p.inWorld = false; }},
    };
    for (auto const& c : cases)
    {
        Outer o;
        bool const r = o.Run([&] { c.change(o); return true; });
        Row(c.id, "false; 0 whispers", std::string(r ? "true" : "false") + "; whispers=" + std::to_string(o.ai.whispers.size() + o.ai2.whispers.size()),
            !r && o.ai.whispers.empty() && o.ai2.whispers.empty());
    }
    {
        Outer o;
        o.Run([&] { o.sender.name = "Renamed"; return true; });
        std::string const to = o.ai.whispers.empty() ? "none" : o.ai.whispers[0].second;
        Row("O12 sender renamed during command", "whisper to name copied before run (Sender)", "to=" + to, to == "Sender");
    }
}

// ---------------- I: A7 precise custody invariant (CS01 #79 7138547373)
struct Custody { uint64 owner = 0, container = 0; uint16 pos = 0; bool operator==(Custody const&) const = default; };
[[maybe_unused]] static uint64 ContainerOf(Player const& p, uint16 pos)
{
    if ((pos >> 8) == 255) return 0;
    auto* bag = p.GetBagByPos(uint8(pos >> 8));
    return bag ? bag->GetGUID().value : ~uint64(0);
}
[[maybe_unused]] static std::map<uint64, Custody> CustodyOf(Player const& p)
{
    std::map<uint64, Custody> m;
    for (auto const& [pos, item] : p.slots) if (item) m[item->GetGUID().value] = {item->facts.owner, ContainerOf(p, pos), pos};
    return m;
}
[[maybe_unused]] static std::map<uint64, PB59::InventoryInstanceFacts> MetaOf(Player const& p)
{
    std::map<uint64, PB59::InventoryInstanceFacts> m;
    for (auto const& [pos, item] : p.slots) if (item) { auto f = item->facts; f.owner = 0; m[f.guid] = f; }
    return m;
}
[[maybe_unused]] static std::string Hex(uint64 v) { char b[32]; std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(v)); return b; }
// Custody fields that changed beyond the plan (owner, container, slot, GUID presence).
[[maybe_unused]] static std::vector<std::string> CustodyDiff(std::map<uint64, Custody> before, std::map<uint64, Custody> const& after,
                                            std::map<uint64, uint16> const& planned)
{
    for (auto const& [guid, dst] : planned) if (before.count(guid)) { before[guid].pos = dst; before[guid].container = 0; }
    std::vector<std::string> out;
    for (auto const& [guid, c] : before)
    {
        auto it = after.find(guid);
        if (it == after.end()) { out.push_back(std::to_string(guid) + ".present 1->0"); continue; }
        if (it->second.owner != c.owner) out.push_back(std::to_string(guid) + ".owner " + std::to_string(c.owner) + "->" + std::to_string(it->second.owner));
        if (it->second.container != c.container) out.push_back(std::to_string(guid) + ".container " + std::to_string(c.container) + "->" + std::to_string(it->second.container));
        if (it->second.pos != c.pos) out.push_back(std::to_string(guid) + ".slot " + Hex(c.pos) + "->" + Hex(it->second.pos));
    }
    for (auto const& [guid, c] : after) if (!before.count(guid)) out.push_back(std::to_string(guid) + ".present 0->1@" + Hex(c.pos));
    return out;
}
[[maybe_unused]] static std::vector<std::string> MetaDiff(std::map<uint64, PB59::InventoryInstanceFacts> const& b, std::map<uint64, PB59::InventoryInstanceFacts> const& a)
{
    std::vector<std::string> out;
    for (auto const& [guid, f] : b)
    {
        auto it = a.find(guid);
        if (it == a.end() || it->second == f) continue;
        auto const& g = it->second;
#define CS02_FIELD(name) if (f.name != g.name) out.push_back(std::to_string(guid) + "." #name " " + std::to_string(f.name) + "->" + std::to_string(g.name));
        CS02_FIELD(entry) CS02_FIELD(count) CS02_FIELD(duration) CS02_FIELD(flags) CS02_FIELD(durability) CS02_FIELD(maxDurability)
        CS02_FIELD(playedTime) CS02_FIELD(suffix) CS02_FIELD(randomProperty) CS02_FIELD(creator) CS02_FIELD(giftCreator)
#undef CS02_FIELD
        if (f.spellCharges != g.spellCharges) out.push_back(std::to_string(guid) + ".spellCharges");
        if (f.enchants != g.enchants) out.push_back(std::to_string(guid) + ".enchants");
        if (f.text != g.text) out.push_back(std::to_string(guid) + ".text");
    }
    return out;
}
[[maybe_unused]] static std::string Join(std::vector<std::string> const& v) { std::string s; for (auto const& x : v) s += (s.empty() ? "" : ";") + x; return s.empty() ? "none" : s; }
[[maybe_unused]] static void CustodyRows()
{
#if CS02_CORE_BATCH
    struct Case { char const* id; bool custody; std::function<void(World&)> change; };
    std::vector<Case> const cases = {
        {"I0 no unrelated change (control)", false, [](World&) {}},
        {"I1 metadata only: 800.durability (= original A7)", false, [](World& w) { w.p.GetItemByPos(E(30))->facts.durability = 1; }},
        {"I2 metadata only: 800.count/flags/enchants", false, [](World& w) {
             auto& f = w.p.GetItemByPos(E(30))->facts; f.count = 5; f.flags = 1; f.enchants[0] = PB59::EnchantFacts{}; f.enchants[0].id = 77; }},
        {"I3 custody: 800 slot E30->E31", true, [](World& w) {
             Item* i = w.p.slots.at(E(30)); w.p.slots.at(E(30)) = nullptr; w.p.slots.at(E(31)) = i;
             w.p.fields[16 + 30 * 2] = 0; w.p.fields[16 + 31 * 2] = uint32(i->GetGUID().value); }},
        {"I4 custody: 800 owner 1->99", true, [](World& w) { w.p.GetItemByPos(E(30))->facts.owner = 99; }},
        {"I5 custody: 800 disappears", true, [](World& w) { w.p.slots.at(E(30)) = nullptr; w.p.fields[16 + 30 * 2] = 0; }},
        {"I6 custody: 801 appears at E32", true, [](World& w) { w.p.Add(E(32), 801); }},
        {"I7 custody: 800 moves into bag 19 slot 0", true, [](World& w) {
             Item* i = w.p.slots.at(E(30)); w.p.slots.at(E(30)) = nullptr; w.p.slots.at(uint16((19 << 8) | 0)) = i; w.p.fields[16 + 30 * 2] = 0; }},
    };
    std::map<uint64, uint16> const planned = {{701, E(23)}, {700, E(24)}};
    for (auto const& c : cases)
    {
        auto w = NewWorld();
        {
            w->p.ownership.emplace_back(new Bag(5000, &w->p.map));
            Bag* bag = static_cast<Bag*>(w->p.ownership.back().get());
            w->p.slots.at(E(19)) = bag; w->p.bags[19] = bag; w->p.fields[16 + 19 * 2] = 5000;
        }
        auto cb = CustodyOf(w->p);
        auto mb = MetaOf(w->p);
        auto rr = Strip(*w, c.change);
        auto ca = CustodyOf(w->p);
        auto ma = MetaOf(w->p);
        auto cd = CustodyDiff(cb, ca, planned);
        auto md = MetaDiff(mb, ma);
        std::string const fields = "custody=" + Join(cd) + " metadata=" + Join(md);
        if (!c.custody)
            Row(std::string(c.id), "custody: planned only; PB Done (metadata permitted)",
                std::string(SN(rr.r)) + "; core=" + rr.core + "; " + fields, cd.empty() && rr.r == StripResult::Done);
        else
            Row(std::string(c.id), "custody change detected; PB RecoveryRequired",
                std::string(SN(rr.r)) + "; core=" + rr.core + "; " + fields, !cd.empty() && rr.r == StripResult::RecoveryRequired);
    }
#endif
}
}

int main()
{
    int const rc = cs02_l3_main();
    int const c0 = g_checks, f0 = g_fail, d0 = g_div;
    l3b::StopRows();
    l3b::ContextRows();
    l3b::OuterRows();
    l3b::CustodyRows();
    std::printf("CS02 layer3b new rows: %d checks, %d property failures, %d divergences from pre-registered expectations\n",
        g_checks - c0, g_fail - f0, g_div - d0);
    return (rc || g_fail) ? 1 : 0;
}
