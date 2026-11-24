// CS02 Layer 3: REAL PB53 adapter methods + REAL PB53 headers + REAL Core74 executor in one translation unit.
// Included at the end of Codex's pinned Core74 executor fixture (in place of its Checks.inc), so the real
// Executor/Admission/Stamp/ItemMods/CombatEffects bodies above are compiled with it. The fixture's
// CapturePB59InventorySlot and RelocatePB59InventoryLinks are DOUBLES (see README; the real ones are
// covered by core74/native_c779). Expectations were registered before the first run; divergences are
// printed, never adapted.
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#ifndef CS02_CORE_BATCH
#define CS02_CORE_BATCH 1
#endif
#define PB59_AUTOGEAR_CORE_BATCH CS02_CORE_BATCH
#define LOG_ERROR(...) do { } while (false)
constexpr int EQUIPMENT_SLOT_START = 0, INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39,
    INVENTORY_SLOT_BAG_START = 19, EQUIPMENT_SLOT_HEAD = 0, EQUIPMENT_SLOT_NECK = 1;
using WorldSession = Session;
struct ObjectGuid
{
    uint64 value = 0;
    uint64 GetRawValue() const { return value; }
    bool IsEmpty() const { return value == 0; }
    std::string ToString() const { return std::to_string(value); }
    static const ObjectGuid Empty;
};
const ObjectGuid ObjectGuid::Empty{};

#include "AutoGearCappedExecution.h"   // REAL PB53
#include "AutoGearCoreBatchRequest.h"  // REAL PB53 (+ real Core PB59InventoryBatchPlan.h via __has_include)

// ---- fixture double completions (declared by the runner's asserted fixture patches)
static Player* g_owner = nullptr;
uint16 Item::GetPos() const
{
    for (auto const& [pos, item] : g_owner->slots) if (item == this) return pos;
    return 0;
}
uint32 Bag::GetBagSize() const { return 4; }
Item* Bag::GetItemByPos(uint8 slot) const
{
    for (auto const& [index, bag] : g_owner->bags)
        if (bag == this) return g_owner->GetItemByPos(uint16((index << 8) | slot));
    return nullptr;
}

// ---- DOUBLE: the Player* the adapter holds. Forwards to the fixture Player (whose executor is real) and
// counts every access, so post-stop / post-RecoveryRequired / post-owner-loss accesses are observable.
struct Bot
{
    Player* p = nullptr;
    mutable int reads = 0, coreCalls = 0, destroys = 0;
    mutable bool afterRR = false, ownerLost = false, stopped = false;
    mutable int postRR = 0, postLost = 0, postStopReads = 0, postStopWrites = 0;
    mutable std::map<std::string, int> postRRBy, postStopBy;
    mutable PB59::InventoryBatchResult last{};
    void R(char const* m) const
    {
        ++reads;
        if (afterRR) { ++postRR; ++postRRBy[m]; }
        if (ownerLost) ++postLost;
        if (stopped) { ++postStopReads; ++postStopBy[m]; }
    }
    WorldSession* GetSession() const { R("GetSession"); return p->GetSession(); }
    bool IsInWorld() const { R("IsInWorld"); return p->IsInWorld(); }
    bool IsAlive() const { R("IsAlive"); return p->IsAlive(); }
    Item* GetItemByPos(uint8 bag, uint8 slot) const { R("GetItemByPos"); return p->GetItemByPos(bag, slot); }
    Bag* GetBagByPos(uint8 bag) const { R("GetBagByPos"); return p->GetBagByPos(bag); }
    uint16 failCapturePos = 0; // double knob: this one observation is unavailable (A13)
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos) const { R("CaptureSlot"); if (failCapturePos && pos == failCapturePos) return {}; return p->CapturePB59InventorySlot(pos); }
    std::optional<PB59::Stamp> CapturePB59InventoryStamp() const { R("CaptureStamp"); return p->CapturePB59InventoryStamp(); }
    uint64 GetPB59OwnerIncarnation() const { R("OwnerIncarnation"); return p->_pb59Transaction.OwnerIncarnation(); }
    ObjectGuid GetGUID() const { R("GetGUID"); return {p->GetGUID().value}; }
    Item* GetItemByGuid(ObjectGuid guid) const
    {
        R("GetItemByGuid");
        for (auto const& [pos, item] : p->slots) if (item && item->GetGUID().value == guid.value) return item;
        return nullptr;
    }
    PB59::InventoryBatchResult ExecutePB59InventoryBatchPlan(uint64 owner, uint64 oi, uint64 si, PB59::Stamp const& stamp,
        std::span<PB59::InventoryBatchMove const> moves)
    {
        ++coreCalls;
        if (stopped) { ++postStopWrites; ++postStopBy["CoreExecute"]; }
        last = p->ExecutePB59InventoryBatchPlan(owner, oi, si, stamp, moves);
        afterRR = last.outcome == PB59::InventoryBatchOutcome::RecoveryRequired;
        return last;
    }
    void DestroyItem(uint8 bag, uint8 slot, bool)
    {
        ++destroys;
        if (stopped) { ++postStopWrites; ++postStopBy["DestroyItem"]; }
        p->slots.at(uint16((bag << 8) | slot)) = nullptr; // double of native DestroyItem
    }
};

#include "AdapterMethods.inc"  // REAL PB53 PlayerAutoGearInventory method bodies (verbatim), runner-assembled class

static int g_checks = 0, g_fail = 0, g_div = 0;
[[maybe_unused]] static void Require(bool ok, std::string const& what) { ++g_checks; if (!ok) { ++g_fail; std::printf("FAIL %s\n", what.c_str()); } }
static char const* SN(PlayerbotsAutoGearExec::StripResult r)
{
    using S = PlayerbotsAutoGearExec::StripResult;
    switch (r) { case S::Done: return "Done"; case S::NoSpace: return "NoSpace"; case S::Refused: return "Refused";
        case S::Restored: return "Restored"; case S::RecoveryRequired: return "RecoveryRequired"; }
    return "Unknown";
}
[[maybe_unused]] static char const* CN(PB59::InventoryBatchOutcome o)
{
    using O = PB59::InventoryBatchOutcome;
    switch (o) { case O::Refused: return "Refused"; case O::Applied: return "Applied"; case O::Restored: return "Restored";
        case O::RecoveryRequired: return "RecoveryRequired"; }
    return "Unknown";
}
// One printed row per scenario: pre-registered expectation vs observation.
static void Row(std::string const& id, std::string const& expected, std::string const& observed, bool ok, std::string const& note = "")
{
    g_div += !ok;
    ++g_checks;
    std::printf("SCN %-44s expected=%-28s observed=%-28s %s%s%s\n", id.c_str(), expected.c_str(), observed.c_str(),
        ok ? "as-expected" : "DIVERGENCE", note.empty() ? "" : " ", note.c_str());
}
static constexpr uint16 E(uint8 s) { return uint16(0xFF00 | s); }

struct World
{
    Player p;
    Bot bot;
    PlayerAutoGearInventory inv{&bot, 0, nullptr};
    World()
    {
        g_owner = &p;
        p.guid = 1;
        bot.p = &p;
        Put(E(15), 700, 200);
        Put(E(16), 701, 200);
        Put(E(0), 702, 150);
        Put(E(30), 800, 10); // unrelated backpack item
    }
    Item* Put(uint16 pos, uint64 guid, uint32 ilvl)
    {
        Item* item = p.Add(pos, guid);
        item->prototype.ItemLevel = ilvl;
        return item;
    }
    uint64 At(uint16 pos) const { auto* i = p.GetItemByPos(pos); return i ? i->GetGUID().value : 0; }
    std::map<uint16, uint64> Layout() const
    {
        std::map<uint16, uint64> m;
        for (auto const& [pos, item] : p.slots) if (item) m[pos] = item->GetGUID().value;
        return m;
    }
};
static std::unique_ptr<World> NewWorld() { return std::make_unique<World>(); }
[[maybe_unused]] static std::string Methods(std::map<std::string, int> const& m)
{
    std::string s;
    for (auto const& [k, v] : m) s += (s.empty() ? "" : ",") + k + "x" + std::to_string(v);
    return s.empty() ? "none" : s;
}

#if CS02_CORE_BATCH
using PlayerbotsAutoGearExec::StripResult;
static void StripScenarios()
{
    {
        auto w = NewWorld();
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        bool const order = w->p.transferOrder == std::vector<uint64>{701, 700};
        Row("A1 strip main+off through real executor", "Done; off-hand first", std::string(SN(r)) + "; order=" + (order ? "off,main" : "other"),
            r == StripResult::Done && order);
        Require(w->At(E(15)) == 0 && w->At(E(16)) == 0 && w->At(E(23)) == 701 && w->At(E(24)) == 700, "A1 items in the planned backpack slots");
        Require(w->bot.coreCalls == 1, "A1 exactly one Core call");
    }
    {
        auto w = NewWorld();
        auto before = w->Layout();
        w->p.rejectTransfer = 2;
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A2 second link refused (Core Restored)", "Restored; layout original", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome),
            r == StripResult::Restored && w->Layout() == before);
    }
    {
        auto w = NewWorld();
        w->p.recoveryTransfer = 1;
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A3 Core RecoveryRequired", "RecoveryRequired", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome) +
            " guids=" + std::to_string(w->bot.last.recoveryCount), r == StripResult::RecoveryRequired);
        Row("A3p Player reads after Core RR (CS01 oracle)", "0", std::to_string(w->bot.postRR) + " [" + Methods(w->bot.postRRBy) + "]",
            w->bot.postRR == 0, "TrainerAction.cpp RunCoreBatch post-return Fingerprint/AllAtDestination/log");
    }
    {
        auto w = NewWorld();
        bool fired = false;
        w->p.policyHook = [&] { if (fired) return; fired = true; w->p.session.logout = true; w->p.inWorld = false; w->p.session.player = nullptr; w->bot.ownerLost = true; };
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        bool const safe = r == StripResult::RecoveryRequired || r == StripResult::Restored || r == StripResult::Refused;
        Row("A4 owner context lost inside Core", "not Done", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome), safe && fired);
        Row("A4p Player reads after owner loss (CS01 oracle)", "0", std::to_string(w->bot.postLost) + " [" + Methods(w->bot.postRRBy) + "]",
            w->bot.postLost == 0, "no live Player pointer may be assumed from a recovery result");
    }
    {
        auto w = NewWorld();
        w->p.rejectPolicy = 1;
        w->p.policyHook = [&] { w->p.GetItemByPos(E(30))->facts.durability = 1; };
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A5 Core Refused + unrelated change", "RecoveryRequired", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome),
            r == StripResult::RecoveryRequired);
    }
    {
        auto w = NewWorld();
        w->p.rejectTransfer = 2;
        w->p.policyHook = [&] { w->p.GetItemByPos(E(30))->facts.durability = 1; };
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A6 Core Restored + unrelated change", "RecoveryRequired", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome),
            r == StripResult::RecoveryRequired);
    }
    {
        auto w = NewWorld();
        w->p.policyHook = [&] { w->p.GetItemByPos(E(30))->facts.durability = 1; };
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A7 Core Applied + unrelated change", "RecoveryRequired (CS02 independent)", std::string(SN(r)) + "; core=" + CN(w->bot.last.outcome),
            r == StripResult::RecoveryRequired, "PB contract believes Applied on destinations only");
    }
    {
        // Precondition search is declared: the first fault knob that makes the real executor return RR with an
        // EMPTY GUID list is used; the adapter row is judged only if the precondition holds.
        bool found = false;
        for (unsigned k = 1; k <= 6 && !found; ++k)
        {
            auto w = NewWorld();
            w->p.throwMods = k;
            auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
            if (w->bot.last.outcome == PB59::InventoryBatchOutcome::RecoveryRequired && w->bot.last.recoveryCount == 0)
            {
                found = true;
                Row("A8 Core RR with empty GUID list (throwMods=" + std::to_string(k) + ")", "RecoveryRequired", SN(r), r == StripResult::RecoveryRequired);
            }
        }
        if (!found) Row("A8 Core RR with empty GUID list", "precondition reachable", "not reached with throwMods 1..6", false, "N/A");
    }
    {
        auto w = NewWorld();
        w->inv.StripToBags((1u << 15) | (1u << 16));
        bool const a = w->inv.RestoreToSlot(ObjectGuid{700}, 15), b = w->inv.RestoreToSlot(ObjectGuid{701}, 16);
        Row("A9 RestoreToSlot main then off", "true,true; layout original", std::string(a ? "true" : "false") + "," + (b ? "true" : "false"),
            a && b && w->At(E(15)) == 700 && w->At(E(16)) == 701);
    }
    {
        auto w = NewWorld();
        w->inv.StripToBags(1u << 15);
        w->p.rejectTransfer = w->p.transfers + 1;
        bool const a = w->inv.RestoreToSlot(ObjectGuid{700}, 15);
        Row("A10 RestoreToSlot with Core refusing the link", "false; item still owned once", std::string(a ? "true" : "false"),
            !a && (w->At(E(15)) == 700 || w->At(E(23)) == 700));
    }
    {
        auto w = NewWorld();
        auto r0 = w->inv.StripToBags(0);
        for (uint8 s = 23; s < 39; ++s) if (!w->p.GetItemByPos(E(s))) w->Put(E(s), 1000 + s, 1);
        auto r1 = w->inv.StripToBags(1u << 15);
        Row("A11 empty mask / no free slot", "Refused,NoSpace; 0 Core calls", std::string(SN(r0)) + "," + SN(r1) + "; core=" + std::to_string(w->bot.coreCalls),
            r0 == StripResult::Refused && r1 == StripResult::NoSpace && w->bot.coreCalls == 0);
    }
    {
        auto w = NewWorld();
        StripResult inner = StripResult::Done;
        bool fired = false;
        w->p.policyHook = [&] { if (fired) return; fired = true; inner = w->inv.StripToBags(1u << 0); };
        auto r = w->inv.StripToBags(1u << 15);
        Row("A12 nested strip from a hook inside Core", "inner Refused; outer Done", std::string("inner=") + SN(inner) + " outer=" + SN(r),
            inner == StripResult::Refused && r == StripResult::Done && w->At(E(0)) == 702);
    }
    // A12b and A13 were ADDED AFTER the first mutant run (P7/P13 survived). They are new rows with their own
    // pre-registered expectations; no existing row or expectation was changed.
    {
        auto w = NewWorld();
        int innerReads = -1, innerCore = -1;
        bool fired = false;
        w->p.policyHook = [&] {
            if (fired)
                return;
            fired = true;
            int const r0 = w->bot.reads, c0 = w->bot.coreCalls;
            (void)w->inv.StripToBags(1u << 0);
            innerReads = w->bot.reads - r0; innerCore = w->bot.coreCalls - c0;
        };
        (void)w->inv.StripToBags(1u << 15);
        Row("A12b nested strip touches nothing", "inner: 0 Player reads, 0 Core calls",
            "reads=" + std::to_string(innerReads) + " core=" + std::to_string(innerCore), innerReads == 0 && innerCore == 0);
    }
    {
        auto w = NewWorld();
        auto before = w->Layout();
        w->bot.failCapturePos = E(16);
        auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
        Row("A13 one masked slot unobservable", "Refused; 0 Core calls; layout original",
            std::string(SN(r)) + "; core=" + std::to_string(w->bot.coreCalls) + (w->Layout() == before ? "; layout original" : "; layout CHANGED"),
            r == StripResult::Refused && w->bot.coreCalls == 0 && w->Layout() == before);
    }
}
#endif

// ---- REAL Execute over (REAL adapter Capture/StripToBags/DestroyCreatedEquipped/RestoreToSlot) +
// SCRIPTED factory/BiS/finishers (the factory boundary is a double: PlayerbotFactory is not compiled here).
struct Composite
{
    World& w;
    std::function<bool()> factory = [] { return true; };
    std::function<bool()> bis = [] { return true; };
    int strips = 0, factories = 0, bisCalls = 0, finishers = 0, destroyCalls = 0, captures = 0;
    int finishersAfterStop = 0, capturesAfterStop = 0, destroysAfterStop = 0;
    PlayerbotsAutoGearExec::Snapshot Capture() { ++captures; if (w.bot.stopped) ++capturesAfterStop; return w.inv.Capture(); }
    PlayerbotsAutoGearExec::StripResult StripToBags(uint32 mask) { ++strips; return w.inv.StripToBags(mask); }
    bool RunFactory(uint32, uint32, bool) { ++factories; bool ok = factory(); if (!ok) w.bot.stopped = true; return ok; }
    bool ApplyBisOverlay(uint32) { ++bisCalls; bool ok = bis(); if (!ok) w.bot.stopped = true; return ok; }
    void ApplyFinishers(uint32) { ++finishers; if (w.bot.stopped) ++finishersAfterStop; }
    bool DestroyCreatedEquipped(uint8 slot, uint64 guid) { ++destroyCalls; if (w.bot.stopped) ++destroysAfterStop; return w.inv.DestroyCreatedEquipped(slot, guid); }
};
static PlayerbotsAutoGearExec::Plan MakePlan(bool reset, bool random, bool bisForm, uint32 cap)
{
    PlayerbotsAutoGearExec::PlanInput in;
    in.capVerified = true; in.cap = cap; in.reset = reset; in.quality = 4; in.populationKnown = true;
    in.population = random ? PlayerbotsAutoGearSelection::Population::RandomBot : PlayerbotsAutoGearSelection::Population::AltBot;
    in.form = bisForm ? PlayerbotsAutoGearSelection::CommandForm::Bis : PlayerbotsAutoGearSelection::CommandForm::Plain;
    in.sourcesAvailable = true;
    return PlayerbotsAutoGearExec::PlanExecution(in);
}
static std::string OutcomeText(PlayerbotsAutoGearExec::Outcome const& o)
{
    return "refusal=" + std::to_string(static_cast<int>(o.refusal)) + " ran=" + std::to_string(o.ran) + " capHeld=" +
        std::to_string(o.capHeld) + " new=" + std::to_string(o.newItems) + " revoked=" + std::to_string(o.revoked) +
        " stripped=" + std::to_string(o.stripped) + " missing=" + std::to_string(o.ownedMissing);
}

static void ExecuteScenarios()
{
    using PlayerbotsAutoGearExec::Refusal;
#if CS02_CORE_BATCH
    for (int route = 0; route < 2; ++route) // 0: failed factory restore, 1: BiS restore failed
    {
        auto w = NewWorld();
        Composite c{*w};
        auto stopAfterOvercapCreate = [&] {
            // The run creates a new over-cap item into an empty slot, then a replaced item cannot be put back.
            w->Put(E(1), 900, 999);
            auto s = w->inv.StripToBags(1u << 15);
            if (s != PlayerbotsAutoGearExec::StripResult::Done) return true;
            w->p.rejectTransfer = w->p.transfers + 1;
            return w->inv.RestoreToSlot(ObjectGuid{700}, 15);
        };
        if (route == 0) c.factory = stopAfterOvercapCreate; else c.bis = stopAfterOvercapCreate;
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, route == 1, 300));
        std::string const id = route == 0 ? "B1 failed factory restore" : "B2 BiS restore failed";
        Row(id + ": outcome", "RecoveryRequired ran=1 capHeld=0", OutcomeText(o),
            o.refusal == Refusal::RecoveryRequired && o.ran && !o.capHeld, "CS01 #52 7137340399");
        Row(id + ": post-stop inventory reads", "0", std::to_string(w->bot.postStopReads) + " [" + Methods(w->bot.postStopBy) + "]",
            w->bot.postStopReads == 0, "AutoGearCappedExecution.h:224-256");
        Row(id + ": post-stop writes (Core/Destroy)", "0", std::to_string(w->bot.postStopWrites) + " destroyCalls=" + std::to_string(c.destroysAfterStop),
            w->bot.postStopWrites == 0 && c.destroysAfterStop == 0);
        Row(id + ": post-stop finishers", "0", std::to_string(c.finishersAfterStop), c.finishersAfterStop == 0);
        Require(w->At(E(23)) == 700 || w->At(E(15)) == 700, id + ": the replaced item is still owned (no loss)");
    }
    {
        // Displaced off-hand during BiS: both leave in one strip (off-hand first); the new item fails to equip;
        // both come back main hand first.
        auto w = NewWorld();
        Composite c{*w};
        c.bis = [&] {
            auto s = w->inv.StripToBags((1u << 15) | (1u << 16));
            if (s != PlayerbotsAutoGearExec::StripResult::Done) return s != PlayerbotsAutoGearExec::StripResult::RecoveryRequired;
            return w->inv.RestoreToSlot(ObjectGuid{700}, 15) && w->inv.RestoreToSlot(ObjectGuid{701}, 16);
        };
        auto before = w->Layout();
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, true, 300));
        bool const order = w->p.transferOrder == std::vector<uint64>{701, 700, 700, 701};
        Row("B3 displaced off-hand, BiS equip fails", "Succeeded; layout original; order off,main,main,off",
            OutcomeText(o) + (order ? " order ok" : " order other"), o.Succeeded() && w->Layout() == before && order);
    }
    {
        auto w = NewWorld();
        Composite c{*w};
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, true, 300));
        Row("B4 zero-strip (nothing over cap), later BiS", "Succeeded; strips=0 factory=1 bis=1 finishers=1",
            OutcomeText(o) + " strips=" + std::to_string(c.strips) + " f=" + std::to_string(c.factories) + " b=" + std::to_string(c.bisCalls) +
            " fin=" + std::to_string(c.finishers),
            o.Succeeded() && c.strips == 0 && c.factories == 1 && c.bisCalls == 1 && c.finishers == 1 && w->bot.coreCalls == 0);
    }
    {
        auto w = NewWorld();
        for (uint8 s : {0, 15, 16}) w->p.slots.at(E(s)) = nullptr;
        Composite c{*w};
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(true, true, true, 300));
        Row("B5 empty reset, later BiS", "Succeeded; strips=0 core=0 finishers=1",
            OutcomeText(o) + " strips=" + std::to_string(c.strips) + " core=" + std::to_string(w->bot.coreCalls),
            o.Succeeded() && c.strips == 0 && w->bot.coreCalls == 0 && c.finishers == 1);
    }
    {
        auto w = NewWorld();
        Composite c{*w};
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(true, true, true, 300));
        Row("B6 reset strip then BiS", "Succeeded; stripped=3 one Core call", OutcomeText(o) + " core=" + std::to_string(w->bot.coreCalls),
            o.Succeeded() && o.stripped == 3 && w->bot.coreCalls == 1 && w->At(E(15)) == 0);
    }
    {
        auto w = NewWorld();
        Composite c{*w};
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, false, false, 100));
        Row("B7 Altbot over cap", "Succeeded; retained=3 strips=0", OutcomeText(o) + " retained=" + std::to_string(o.retainedOverCap) +
            " strips=" + std::to_string(c.strips), o.Succeeded() && o.retainedOverCap == 3 && c.strips == 0 && w->bot.coreCalls == 0);
    }
    {
        auto w = NewWorld();
        Composite c{*w};
        c.factory = [&] { w->p.slots.at(E(16)) = nullptr; return true; }; // e.g. Core mailed an off-hand
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, false, 300));
        Row("B8 owned off-hand leaves inventory", "RecoveryRequired missing=1", OutcomeText(o),
            o.refusal == Refusal::RecoveryRequired && o.ownedMissing == 1);
    }
    {
        auto w = NewWorld();
        w->p.recoveryTransfer = 1;
        Composite c{*w};
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(true, true, false, 300));
        Row("B9 reset strip RecoveryRequired", "RecoveryRequired ran=0; factory=0", OutcomeText(o) + " f=" + std::to_string(c.factories),
            o.refusal == Refusal::RecoveryRequired && !o.ran && c.factories == 0 && c.finishers == 0);
    }
#else
    {
        // Default-OFF build: no Core executor. Nothing owned moves; reset refuses before anything changed.
        auto w = NewWorld();
        Composite c{*w};
        auto before = w->Layout();
        auto o = PlayerbotsAutoGearExec::Execute(c, MakePlan(true, true, false, 300));
        bool const restore = w->inv.RestoreToSlot(ObjectGuid{700}, 15);
        Row("D1 OFF: reset", "StripRefused; layout original; factory=0", OutcomeText(o) + " f=" + std::to_string(c.factories),
            o.refusal == Refusal::StripRefused && w->Layout() == before && c.factories == 0);
        Row("D2 OFF: RestoreToSlot", "false; 0 Core calls", std::string(restore ? "true" : "false") + " core=" + std::to_string(w->bot.coreCalls),
            !restore && w->bot.coreCalls == 0);
        auto o2 = PlayerbotsAutoGearExec::Execute(c, MakePlan(false, true, false, 100));
        Row("D3 OFF: random bot over cap", "StripRefused; layout original", OutcomeText(o2), o2.refusal == Refusal::StripRefused && w->Layout() == before);
    }
#endif
}

// ---- REAL pure PB53 mapping vs CS02's independent oracle.
static void MappingScenarios()
{
    using namespace PlayerbotsAutoGearCoreBatch;
    using S = PlayerbotsAutoGearExec::StripResult;
    int rows = 0, agree = 0;
    for (int o = 0; o < 4; ++o)
        for (int bits = 0; bits < 16; ++bits)
        {
            CoreBatchObservation ob;
            ob.unchangedSinceStart = bits & 1; ob.everyMoveAtDestination = bits & 2; ob.failureReported = bits & 4; ob.missing = (bits & 8) ? 3 : 0;
            auto const outcome = static_cast<CoreBatchOutcome>(o);
            // Independent oracle (CS01 #52 7133329461 + CS02): believe a result only with agreeing observation.
            S expect = S::RecoveryRequired;
            if (!ob.missing)
            {
                if (outcome == CoreBatchOutcome::Refused && ob.unchangedSinceStart) expect = S::Refused;
                if (outcome == CoreBatchOutcome::Restored && ob.unchangedSinceStart) expect = S::Restored;
                if (outcome == CoreBatchOutcome::Applied && ob.everyMoveAtDestination && !ob.failureReported) expect = S::Done;
            }
            S const got = ToStripResult(outcome, ob);
            ++rows; agree += got == expect;
            if (got != expect) Row("C1 " + std::string(OutcomeName(outcome)) + " bits=" + std::to_string(bits), SN(expect), SN(got), false);
        }
    std::printf("MATRIX C1 ToStripResult: %d rows, %d agree with the independent oracle\n", rows, agree);
    g_checks += rows;
    for (int raw : {4, 7, 0xFF})
    {
        auto const unknown = OutcomeFrom(static_cast<PB59::InventoryBatchOutcome>(raw));
        CoreBatchObservation ob{true, true, 0, false};
        Row("C2 unknown native outcome " + std::to_string(raw), "RecoveryRequired/Unknown", std::string(SN(ToStripResult(unknown, ob))) + "/" + OutcomeName(unknown),
            ToStripResult(unknown, ob) == S::RecoveryRequired && std::string(OutcomeName(unknown)) == "Unknown");
    }
    for (std::size_t count : {std::size_t(19), std::size_t(20), std::size_t(1000), std::numeric_limits<std::size_t>::max()})
    {
        CoreBatchObservation ob{true, true, count, false};
        Row("C3 Restored/Applied with recovery count " + std::to_string(count), "RecoveryRequired,RecoveryRequired",
            std::string(SN(ToStripResult(CoreBatchOutcome::Restored, ob))) + "," + SN(ToStripResult(CoreBatchOutcome::Applied, ob)),
            ToStripResult(CoreBatchOutcome::Restored, ob) == S::RecoveryRequired && ToStripResult(CoreBatchOutcome::Applied, ob) == S::RecoveryRequired);
    }
    {
        CoreBatchObservation ob{true, true, 0, false};
        Row("C4 RecoveryRequired with empty GUID list", "RecoveryRequired", SN(ToStripResult(CoreBatchOutcome::RecoveryRequired, ob)),
            ToStripResult(CoreBatchOutcome::RecoveryRequired, ob) == S::RecoveryRequired);
    }
    Row("C5 no-fallback gate", "MayMove(false)=0 MayMove(true)=1", std::string("MayMove(false)=") + std::to_string(MayMoveOwnedItems(false)) +
        " MayMove(true)=" + std::to_string(MayMoveOwnedItems(true)), !MayMoveOwnedItems(false) && MayMoveOwnedItems(true));
    {
        auto alt = MakePlan(true, false, false, 300), altPlain = MakePlan(false, false, false, 300);
        Row("C6 Altbot plan", "reset refused; no over-cap strip", "reset=" + std::to_string(static_cast<int>(alt.refusal)) + " stripOverCap=" + std::to_string(altPlain.stripOverCap),
            alt.refusal == PlayerbotsAutoGearExec::Refusal::ResetNotAllowed && altPlain.Runs() && !altPlain.stripOverCap && !altPlain.strip);
    }
}

int main()
{
    std::printf("REAL: PB53 AutoGearCappedExecution.h (Execute), AutoGearCoreBatchRequest.h, PlayerAutoGearInventory::{Capture,StripToBags,"
                "DestroyCreatedEquipped,RestoreToSlot,InWorld,RunCoreBatch,AllAtDestination,Fingerprint} bodies; Core74 executor, admission, "
                "stamp, item mods, combat effects, PB59 headers. DOUBLES: Codex fixture Player/Item/Bag/Session incl. CapturePB59InventorySlot "
                "and RelocatePB59InventoryLinks; CS02 Bot facade (Player* forwarding + access counters, DestroyItem); scripted factory/BiS/finishers. "
                "CORE_BATCH=%d\n", CS02_CORE_BATCH);
#if CS02_CORE_BATCH
    StripScenarios();
#endif
    ExecuteScenarios();
    MappingScenarios();
    std::printf("CS02 layer3 adapter: %d checks, %d property failures, %d divergences from pre-registered expectations\n", g_checks, g_fail, g_div);
    return g_fail ? 1 : 0;
}
