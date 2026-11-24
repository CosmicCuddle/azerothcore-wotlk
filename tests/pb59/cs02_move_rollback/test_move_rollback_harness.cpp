// CS02 / PB59 AutoGear move-rollback failure-injection harness.
//
// Every result printed by this binary is SYNTHETIC: it runs against the value
// model in SyntheticInventoryCore.h, not the native Player. Real Core53
// headers are used only for the value facts and exact-storage predicates
// (PB59InventorySlotFacts.h, ExactStorageGate.h).
//
// Build/run: tests/pb59/cs02_move_rollback/run_local.sh (no CI workflow).
#include "Executors.h"
#include "Pb50StripAdapter.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace CS02MoveRollback;
using Synthetic::BAG0;
using Synthetic::InsertFault;
using Synthetic::RemoveFault;

namespace
{
int g_checks = 0, g_failures = 0;
#define CHECK(cond, what)                                                                     \
    do                                                                                        \
    {                                                                                         \
        ++g_checks;                                                                           \
        if (!(cond))                                                                          \
        {                                                                                     \
            ++g_failures;                                                                     \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, std::string(what).c_str());    \
        }                                                                                     \
    } while (0)

constexpr std::uint8_t kEquipped[] = {0, 2, 4, 6, 9, 15, 16, 17};
constexpr std::uint64_t kBagGuid = 0xB19;
constexpr std::uint8_t kBagSlot = 19, kBagSize = 4;
constexpr int kStorage = 16 + kBagSize;

// Standard fixture: eight equipped items, one 4-slot bag, `freeStorage` free
// general storage positions (backpack first, then the bag: native order).
std::unique_ptr<Synthetic::Core> Fixture(int freeStorage = 8, int emptySlot = -1)
{
    auto c = std::make_unique<Synthetic::Core>();
    for (std::uint8_t s : kEquipped)
        if (s != emptySlot)
            c->Create(100 + s, 0x1000 + s, Pos{BAG0, s});
    c->CreateBag(kBagGuid, kBagSlot, kBagSize);
    auto storage = c->StoragePositions();
    int fill = kStorage - freeStorage;
    for (int i = 0; i < fill; ++i)
        c->Create(500, 0x2000 + i, storage[storage.size() - 1 - i]); // fill from the end
    return c;
}

Request Base(Synthetic::Core const& c)
{
    return Request{c.ownerGuid, c.incarnation, c.custodyStamp, {}};
}

std::vector<Pos> FreeStorage(Synthetic::Core const& c)
{
    std::vector<Pos> out;
    for (Pos p : c.StoragePositions())
        if (!c.GetItemByPos(p))
            out.push_back(p);
    return out;
}

// Strip equipment slots to the first free storage positions in native search
// order, so the PB44 transcription chooses the same destinations.
Request Strip(Synthetic::Core const& c, std::vector<std::uint8_t> const& slots)
{
    Request r = Base(c);
    auto free = FreeStorage(c);
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        Pos to = i < free.size() ? free[i] : Pos{BAG0, std::uint8_t(Synthetic::PACK_START)};
        r.moves.push_back(Move{c.GetItemByPos(BAG0, slots[i])->guid, Pos{BAG0, slots[i]}, to});
    }
    return r;
}

std::vector<std::uint8_t> FirstSlots(int n)
{
    return std::vector<std::uint8_t>(kEquipped, kEquipped + n);
}

struct Scenario
{
    std::string name;
    std::function<Request(Synthetic::Core&)> setup;
    Outcome expected;
    std::function<std::unique_ptr<Synthetic::Core>()> fixture = [] { return Fixture(); };
};

struct Run
{
    Report rep;
    Verdict verdict;
    std::unique_ptr<Synthetic::Core> core;
};

template<class E>
Run Execute(Scenario const& s, E& exec)
{
    Run run;
    run.core = s.fixture();
    Request r = s.setup(*run.core);
    Snapshot s0 = Capture(*run.core);
    run.core->ledger.clear();
    run.rep = exec.Execute(*run.core, r);
    run.verdict = Judge(*run.core, s0, r, run.rep);
    return run;
}

// ---------------------------------------------------------------- scenarios
std::vector<Scenario> PreflightScenarios()
{
    std::vector<Scenario> v;
    auto strip3 = [](auto mutate) {
        return [mutate](Synthetic::Core& c) {
            Request r = Strip(c, FirstSlots(3));
            mutate(c, r);
            return r;
        };
    };
    v.push_back({"P01 wrong owner", strip3([](auto&, Request& r) { r.owner = 0x999; }), Outcome::Refused});
    v.push_back({"P02 stale session incarnation", strip3([](auto&, Request& r) { r.incarnation--; }), Outcome::Refused});
    v.push_back({"P03 stale custody stamp", strip3([](auto&, Request& r) { r.custodyStamp++; }), Outcome::Refused});
    v.push_back({"P04 owner logging out", strip3([](Synthetic::Core& c, Request&) { c.loggingOut = true; }), Outcome::Refused});
    v.push_back({"P05 owner retiring", strip3([](Synthetic::Core& c, Request&) { c.retiring = true; }), Outcome::Refused});
    v.push_back({"P06 empty plan", [](Synthetic::Core& c) { return Base(c); }, Outcome::Refused});
    v.push_back({"P07 same slot, different GUID", strip3([](auto&, Request& r) { r.moves[1].itemGuid = 0x7777; }), Outcome::Refused});
    v.push_back({"P08 item moved since plan", strip3([](auto&, Request& r) { r.moves[2].from = Pos{BAG0, 5}; }), Outcome::Refused});
    v.push_back({"P09 item owned by someone else", strip3([](Synthetic::Core& c, Request&) { c.GetItemByPos(BAG0, 2)->owner = 0x555; }), Outcome::Refused});
    v.push_back({"P10 same GUID twice", strip3([](auto&, Request& r) { r.moves[2] = r.moves[0]; r.moves[2].to = Pos{BAG0, 30}; }), Outcome::Refused});
    v.push_back({"P11 two moves to one destination", strip3([](auto&, Request& r) { r.moves[2].to = r.moves[1].to; }), Outcome::Refused});
    v.push_back({"P12 destination occupied", strip3([](Synthetic::Core& c, Request& r) {
        r.moves[1].to = c.StoragePositions().back(); }), Outcome::Refused});
    v.push_back({"P13 destination NULL_SLOT", strip3([](auto&, Request& r) { r.moves[0].to = Pos{BAG0, Synthetic::NULL_SLOT}; }), Outcome::Refused});
    v.push_back({"P14 destination beyond bag size", strip3([](auto&, Request& r) { r.moves[0].to = Pos{kBagSlot, kBagSize}; }), Outcome::Refused});
    v.push_back({"P15 destination is another move's source", strip3([](auto&, Request& r) { r.moves[0].to = r.moves[1].from; }), Outcome::Refused});
    v.push_back({"P16 item in trade", strip3([](Synthetic::Core& c, Request&) { c.GetItemByPos(BAG0, 4)->inTrade = true; }), Outcome::Refused});
    v.push_back({"P17 loot generated", strip3([](Synthetic::Core& c, Request&) { c.GetItemByPos(BAG0, 4)->lootGenerated = true; }), Outcome::Refused});
    v.push_back({"P18 OnPlayerCanUnequipItem refuses", strip3([](Synthetic::Core& c, Request&) {
        c.onPlayerCanUnequipItem = [](auto&, std::uint16_t pos, bool) { return (pos & 0xFF) != 2; }; }), Outcome::Refused});
    v.push_back({"P19 in combat", strip3([](Synthetic::Core& c, Request&) { c.inCombat = true; }), Outcome::Refused});
    v.push_back({"P20 bags one short", [](Synthetic::Core& c) { return Strip(c, FirstSlots(3)); }, Outcome::Refused,
        [] { return Fixture(2); }});
    v.push_back({"P21 callback changes another slot", strip3([](Synthetic::Core& c, Request&) {
        c.onPlayerCanUnequipItem = [](Synthetic::Core& core, std::uint16_t, bool) {
            if (Item* it = core.GetItemByPos(BAG0, 9); it && it->pos.slot == 9)
                core.HookRelocate(it, Pos{BAG0, 24});
            return true;
        }; }), Outcome::RecoveryRequired}); // agreed point 2
    v.push_back({"P22 callback starts logout", strip3([](Synthetic::Core& c, Request&) {
        c.onPlayerCanUnequipItem = [](Synthetic::Core& core, std::uint16_t, bool) {
            core.loggingOut = true;
            ++core.incarnation;
            return true;
        }; }), Outcome::RecoveryRequired}); // agreed point 2: context changed
    v.push_back({"P23 bag item as a move", [](Synthetic::Core& c) {
        Request r = Base(c);
        r.moves.push_back(Move{kBagGuid, Pos{BAG0, kBagSlot}, FreeStorage(c)[0]});
        return r; }, Outcome::Refused});
    v.push_back({"P24 bag slot as destination", [](Synthetic::Core& c) {
        Request r = Strip(c, FirstSlots(1));
        r.moves[0].to = Pos{BAG0, 20};
        return r; }, Outcome::Refused});
    v.push_back({"P25 two-hander next to occupied off-hand", [](Synthetic::Core& c) {
        Synthetic::Item* th = c.Create(777, 0x3000, FreeStorage(c)[0], true);
        Request r = Base(c);
        r.moves.push_back(Move{th->guid, th->pos, Pos{BAG0, Synthetic::MAINHAND}});
        return r; }, Outcome::Refused, [] { return Fixture(8, Synthetic::MAINHAND); }});
    v.push_back({"P26 equip into occupied equipment slot", [](Synthetic::Core& c) {
        Synthetic::Item* it = c.Create(778, 0x3001, FreeStorage(c)[0]);
        Request r = Base(c);
        r.moves.push_back(Move{it->guid, it->pos, Pos{BAG0, 0}});
        return r; }, Outcome::Refused});
    v.push_back({"P29 two-hander before the off-hand move (point 4: order matters)", [](Synthetic::Core& c) {
        Synthetic::Item* oh = c.GetItemByPos(BAG0, Synthetic::OFFHAND);
        auto free = FreeStorage(c);
        Synthetic::Item* th = c.Create(783, 0x3008, free[1], true);
        Request r = Base(c);
        r.moves.push_back(Move{th->guid, th->pos, Pos{BAG0, Synthetic::MAINHAND}});
        r.moves.push_back(Move{oh->guid, oh->pos, free[0]});
        return r; }, Outcome::Refused, [] { return Fixture(8, Synthetic::MAINHAND); }});
    v.push_back({"P30 off-hand move invalid, two-hander after it (point 4: validated)", [](Synthetic::Core& c) {
        Synthetic::Item* oh = c.GetItemByPos(BAG0, Synthetic::OFFHAND);
        auto free = FreeStorage(c);
        Synthetic::Item* th = c.Create(784, 0x3009, free[1], true);
        Request r = Base(c);
        r.moves.push_back(Move{oh->guid, oh->pos, c.StoragePositions().back()}); // occupied destination
        r.moves.push_back(Move{th->guid, th->pos, Pos{BAG0, Synthetic::MAINHAND}});
        return r; }, Outcome::Refused, [] { return Fixture(8, Synthetic::MAINHAND); }});
    v.push_back({"P27 called while a transaction is already open", strip3([](Synthetic::Core& c, Request&) {
        c.transactionDepth = 1; }), Outcome::Refused});
    v.push_back({"P28 destination holds a mergeable stack of the same entry", [](Synthetic::Core& c) {
        Pos a = FreeStorage(c)[0], b = FreeStorage(c)[1];
        Synthetic::Item* mine = c.Create(600, 0x3006, a);
        Synthetic::Item* stack = c.Create(600, 0x3007, b);
        mine->maxStack = stack->maxStack = 20;
        stack->count = 5;
        Request r = Base(c);
        r.moves.push_back(Move{mine->guid, a, b});
        return r; }, Outcome::Refused});
    return v;
}

std::vector<Scenario> AppliedScenarios()
{
    std::vector<Scenario> v;
    for (int n = 1; n <= 5; ++n)
        v.push_back({"A0" + std::to_string(n) + " strip " + std::to_string(n) + " item(s)",
            [n](Synthetic::Core& c) { return Strip(c, FirstSlots(n)); }, Outcome::Applied});
    v.push_back({"A06 exact fit: 3 moves, 3 free", [](Synthetic::Core& c) { return Strip(c, FirstSlots(3)); },
        Outcome::Applied, [] { return Fixture(3); }});
    v.push_back({"A07 strip into the bag, not the backpack", [](Synthetic::Core& c) {
        Request r = Strip(c, FirstSlots(2));
        r.moves[0].to = Pos{kBagSlot, 0};
        r.moves[1].to = Pos{kBagSlot, 1};
        return r; }, Outcome::Applied, [] { return Fixture(kStorage); }});
    v.push_back({"A08 equip from bag into empty slot", [](Synthetic::Core& c) {
        Synthetic::Item* it = c.Create(779, 0x3002, FreeStorage(c)[0]);
        Request r = Base(c);
        r.moves.push_back(Move{it->guid, it->pos, Pos{BAG0, 1}});
        return r; }, Outcome::Applied});
    v.push_back({"A09 off-hand out, then two-hander into empty main hand (no M4 mail)", [](Synthetic::Core& c) {
        Synthetic::Item* oh = c.GetItemByPos(BAG0, Synthetic::OFFHAND);
        auto free = FreeStorage(c);
        Synthetic::Item* th = c.Create(780, 0x3003, free[1], true);
        Request r = Base(c);
        r.moves.push_back(Move{oh->guid, oh->pos, free[0]});
        r.moves.push_back(Move{th->guid, th->pos, Pos{BAG0, Synthetic::MAINHAND}});
        return r; }, Outcome::Applied, [] { return Fixture(2, Synthetic::MAINHAND); }});
    v.push_back({"A10 nested call from callback is fenced; outer applies", [](Synthetic::Core& c) {
        c.onPlayerCanUnequipItem = [](Synthetic::Core& core, std::uint16_t, bool) {
            ReferenceExecutor nested;
            Request inner = Strip(core, {9});
            Report rep = nested.Execute(core, inner);
            return rep.outcome == Outcome::Refused; // fence must refuse with no effects
        };
        return Strip(c, FirstSlots(2)); }, Outcome::Applied});
    return v;
}

const InsertFault kInsertFaults[] = {InsertFault::Refuse, InsertFault::Throw, InsertFault::InsertThenThrow,
    InsertFault::WrongPlace, InsertFault::ReplacePointer};
char const* FaultName(InsertFault f)
{
    switch (f)
    {
        case InsertFault::Refuse: return "insert-refuse";
        case InsertFault::Throw: return "insert-throw";
        case InsertFault::InsertThenThrow: return "insert-then-throw";
        case InsertFault::WrongPlace: return "claims-success-wrong-place";
        case InsertFault::ReplacePointer: return "returns-replacement-pointer";
        default: return "none";
    }
}

std::vector<Scenario> ForwardFaultScenarios()
{
    std::vector<Scenario> v;
    for (int n = 1; n <= 4; ++n)
        for (int k = 0; k < n; ++k)
        {
            std::string at = "N=" + std::to_string(n) + " fail@" + std::to_string(k) + " ";
            for (InsertFault f : kInsertFaults)
                v.push_back({"F " + at + FaultName(f), [n, k, f](Synthetic::Core& c) {
                    c.insertFaults[k] = f;
                    return Strip(c, FirstSlots(n)); },
                    f == InsertFault::ReplacePointer ? Outcome::RecoveryRequired : Outcome::Restored});
            for (RemoveFault f : {RemoveFault::Noop, RemoveFault::Throw})
                v.push_back({"F " + at + (f == RemoveFault::Noop ? "remove-noop" : "remove-throw"),
                    [n, k, f](Synthetic::Core& c) {
                        c.removeFaults[k] = f;
                        return Strip(c, FirstSlots(n)); },
                    k == 0 ? Outcome::Refused : Outcome::Restored});
        }
    return v;
}

std::vector<Scenario> RollbackFaultScenarios()
{
    std::vector<Scenario> v;
    for (int n = 2; n <= 4; ++n)
        for (int k = 1; k < n; ++k)
        {
            // forward insertions 0..k (k refused); rollback insertions k+1 .. 2k+1
            for (int j = 0; j <= k; ++j)
                for (InsertFault f : {InsertFault::Refuse, InsertFault::Throw, InsertFault::ReplacePointer})
                    v.push_back({"R N=" + std::to_string(n) + " fail@" + std::to_string(k) + " undo#" +
                            std::to_string(j) + " " + FaultName(f),
                        [n, k, j, f](Synthetic::Core& c) {
                            c.insertFaults[k] = InsertFault::Refuse;
                            c.insertFaults[k + 1 + j] = f;
                            return Strip(c, FirstSlots(n)); },
                        Outcome::RecoveryRequired});
            // forward removals 0..k; rollback removals k+1 .. 2k
            for (int j = 0; j < k; ++j)
                v.push_back({"R N=" + std::to_string(n) + " fail@" + std::to_string(k) + " undo-remove#" +
                        std::to_string(j) + " throw",
                    [n, k, j](Synthetic::Core& c) {
                        c.insertFaults[k] = InsertFault::Refuse;
                        c.removeFaults[k + 1 + j] = RemoveFault::Throw;
                        return Strip(c, FirstSlots(n)); },
                    Outcome::RecoveryRequired});
        }
    for (auto [a, b] : {std::pair{0, 2}, std::pair{1, 2}, std::pair{0, 1}})
        v.push_back({"R N=3 fail@2 two undo failures #" + std::to_string(a) + "+#" + std::to_string(b),
            [a, b](Synthetic::Core& c) {
                c.insertFaults[2] = InsertFault::Refuse;
                c.insertFaults[3 + a] = InsertFault::Refuse;
                c.insertFaults[3 + b] = InsertFault::Refuse;
                return Strip(c, FirstSlots(3)); },
            Outcome::RecoveryRequired});
    return v;
}

// Which OnPlayerCanUnequipItem call for the hazard slot fires the side
// effect (1 = first call). PB44 calls the hook once per item; PB50 calls it
// at planning, validation and inside the move (calls 1, 2, 3). The nested
// executor for H5 is the executor under test (re-entry of itself).
struct HazardControl
{
    int fireOnCall = 1;
    std::function<void(Synthetic::Core&)> nested = [](Synthetic::Core& core) {
        ReferenceExecutor ref;
        ref.Execute(core, Strip(core, {2}));
    };
};
HazardControl g_hazard;

// CS01 7118646677: PB44 1a9e4801 TrainerAction.cpp:692-704 holds Item*
// across CanUnequipItem's OnPlayerCanUnequipItem callback and trusts an empty
// source slot. Each hook below is a callback side effect Core53 permits.
// Expected outcomes are for the reference executor under the agreed contract
// (#52 7130720231 / #51 7127659287): callback changes -> RecoveryRequired.
std::vector<Scenario> HookHazardScenarios()
{
    std::vector<Scenario> v;
    auto onSlot = [](std::uint8_t slot, auto effect) {
        auto calls = std::make_shared<int>(0);
        int const fire = g_hazard.fireOnCall;
        return [slot, effect, calls, fire](Synthetic::Core& core, std::uint16_t pos, bool) {
            if ((pos & 0xFF) == slot && ++*calls == fire)
                effect(core);
            return true;
        };
    };
    v.push_back({"H1 callback deletes the held item (merge/destroy)", [onSlot](Synthetic::Core& c) {
        c.onPlayerCanUnequipItem = onSlot(2, [](Synthetic::Core& core) {
            core.HookMergeAway(core.GetItemByPos(BAG0, 2)); });
        return Strip(c, FirstSlots(3)); }, Outcome::RecoveryRequired});
    v.push_back({"H2 callback moves the held item into its planned destination", [onSlot](Synthetic::Core& c) {
        c.onPlayerCanUnequipItem = onSlot(2, [](Synthetic::Core& core) {
            if (Synthetic::Item* it = core.GetItemByPos(BAG0, 2))
                core.HookRelocate(it, FreeStorage(core)[0]); });
        return Strip(c, FirstSlots(3)); }, Outcome::RecoveryRequired});
    v.push_back({"H3 callback swaps another GUID into the slot", [onSlot](Synthetic::Core& c) {
        Synthetic::Item* other = c.Create(781, 0x3004, FreeStorage(c).back());
        std::uint64_t og = other->guid;
        c.onPlayerCanUnequipItem = onSlot(2, [og](Synthetic::Core& core) {
            if (Synthetic::Item* it = core.GetItemByPos(BAG0, 2))
                core.HookRelocate(it, FreeStorage(core)[1]);
            core.HookRelocate(core.FindArena(og), Pos{BAG0, 2}); });
        return Strip(c, FirstSlots(3)); }, Outcome::RecoveryRequired});
    v.push_back({"H4 callback begins logout", [onSlot](Synthetic::Core& c) {
        c.onPlayerCanUnequipItem = onSlot(2, [](Synthetic::Core& core) {
            core.loggingOut = true;
            ++core.incarnation; });
        return Strip(c, FirstSlots(3)); }, Outcome::RecoveryRequired});
    v.push_back({"H5 callback re-enters the strip of the same slot", [onSlot](Synthetic::Core& c) {
        auto nested = g_hazard.nested;
        c.onPlayerCanUnequipItem = onSlot(2, [nested](Synthetic::Core& core) { nested(core); });
        return Strip(c, FirstSlots(3)); }, Outcome::Applied}); // reference: nested call fenced
    v.push_back({"H6 callback refuses the second strip (partial strip)", [](Synthetic::Core& c) {
        auto calls = std::make_shared<int>(0);
        int const fire = g_hazard.fireOnCall;
        c.onPlayerCanUnequipItem = [calls, fire](Synthetic::Core&, std::uint16_t pos, bool) {
            return (pos & 0xFF) != 2 || ++*calls != fire;
        };
        return Strip(c, FirstSlots(3)); }, Outcome::Refused});
    v.push_back({"H7 StoreItem returns a replacement pointer (hypothetical; Core53 comment permits)",
        [](Synthetic::Core& c) {
            c.insertFaults[1] = InsertFault::ReplacePointer;
            return Strip(c, FirstSlots(3)); }, Outcome::RecoveryRequired});
    v.push_back({"H8 partial strip: bags run out at the third item", [](Synthetic::Core& c) {
        Request r = Strip(c, FirstSlots(3));
        c.insertFaults[2] = InsertFault::Refuse;
        return r; }, Outcome::Restored});
    return v;
}

std::vector<Scenario> All()
{
    std::vector<Scenario> all;
    for (auto group : {PreflightScenarios(), AppliedScenarios(), ForwardFaultScenarios(), RollbackFaultScenarios(),
             HookHazardScenarios()})
        all.insert(all.end(), group.begin(), group.end());
    return all;
}

// A naive native-shaped factory step: equip the two-hander, then let Core
// tidy the off-hand. With full bags Core53 mails it (M4).
struct NativeTwoHanderThenAutoUnequip
{
    Report Execute(Synthetic::Core& core, Request const& r)
    {
        core.phase = 1;
        for (auto const& m : r.moves)
        {
            Synthetic::Item* it = core.GetItemByPos(m.from);
            core.RemoveItem(m.from.bag, m.from.slot);
            if (m.to.IsEquipment())
                core.EquipItem(m.to.Packed(), it);
            else
                core.StoreExact(m.to, it);
        }
        core.AutoUnequipOffhandIfNeed(); // factory tidies the off-hand after equipping
        return Report{Outcome::Applied, {}, {}};
    }
};
}

int main()
{
    std::printf("CS02 PB59 move-rollback harness -- ALL RESULTS SYNTHETIC (value model, not native Player)\n");
    auto scenarios = All();

    // 1. Reference executor: every scenario classified exactly and truthfully.
    int refPass = 0;
    for (auto const& s : scenarios)
    {
        ReferenceExecutor ref;
        Run run = Execute(s, ref);
        bool ok = run.verdict.ok() && run.rep.outcome == s.expected;
        CHECK(run.verdict.ok(), s.name + ": oracle violations\n" + run.verdict.Text());
        CHECK(run.rep.outcome == s.expected, s.name + ": reference reported " + Name(run.rep.outcome) +
            ", expected " + Name(s.expected) + " (" + run.rep.reason + ")");
        CHECK(run.verdict.truth == s.expected, s.name + ": oracle derived " + Name(run.verdict.truth));
        CHECK(run.core->mailed.empty(), s.name + ": item mailed");
        refPass += ok;
    }
    std::printf("[SYNTHETIC] reference executor: %d/%zu scenarios exact\n", refPass, scenarios.size());

    // 2. Mainhand-first restore after an off-hand-first strip (A04 order).
    {
        auto c = Fixture();
        Request r = Strip(*c, {Synthetic::OFFHAND, Synthetic::MAINHAND});
        c->insertFaults[1] = InsertFault::Refuse;
        Snapshot s0 = Capture(*c);
        c->ledger.clear();
        ReferenceExecutor ref;
        Report rep = ref.Execute(*c, r);
        Verdict v = Judge(*c, s0, r, rep);
        CHECK(rep.outcome == Outcome::Restored && v.ok(), "offhand-first strip restores\n" + v.Text());
        std::vector<std::uint64_t> reinserted;
        for (auto const& e : c->ledger)
            if (e.phase == 2 && e.kind == Synthetic::EventKind::Insert)
                reinserted.push_back(e.guid);
        CHECK(reinserted.size() == 2 && reinserted[0] == 0x1000 + Synthetic::MAINHAND &&
                reinserted[1] == 0x1000 + Synthetic::OFFHAND,
            "rollback restores the main hand before the off-hand");
    }

    // 3. PB44 1a9e4801 MoveEquippedToBags transcription: hazard reproduction.
    int hazards = 0, hazardScenarios = 0;
    g_hazard.nested = [](Synthetic::Core& core) {
        Pb44StripExecutor nested; // PB44 re-entering itself (it has no fence)
        nested.Execute(core, Strip(core, {2}));
    };
    for (auto const& s : HookHazardScenarios())
    {
        ++hazardScenarios;
        Pb44StripExecutor pb44;
        Run run = Execute(s, pb44);
        bool flagged = !run.verdict.ok();
        hazards += flagged;
        CHECK(flagged, "PB44 pattern not flagged in " + s.name);
        std::printf("[SYNTHETIC] PB44-pattern %-75s reported %-8s oracle: %s\n", s.name.c_str(),
            Name(run.rep.outcome), flagged ? run.verdict.violations.front().c_str() : "no violation");
    }
    g_hazard = HazardControl{};
    std::printf("[SYNTHETIC] PB44 1a9e4801 TrainerAction.cpp:692-704 pattern flagged in %d/%d hazard scenarios"
                " (expected red; Claude owns the fix)\n", hazards, hazardScenarios);
    // Controls: PB44 pattern is not falsely flagged when nothing goes wrong.
    for (int n = 1; n <= 4; ++n)
    {
        Scenario s{"PB44 control strip " + std::to_string(n),
            [n](Synthetic::Core& c) { return Strip(c, FirstSlots(n)); }, Outcome::Applied};
        Pb44StripExecutor pb44;
        Run run = Execute(s, pb44);
        CHECK(run.verdict.ok() && run.rep.outcome == Outcome::Applied, s.name + "\n" + run.verdict.Text());
    }
    {
        Scenario s{"PB44 control: callback refuses the first strip", [](Synthetic::Core& c) {
            c.onPlayerCanUnequipItem = [](Synthetic::Core&, std::uint16_t pos, bool) { return (pos & 0xFF) != 0; };
            return Strip(c, FirstSlots(3)); }, Outcome::Refused};
        Pb44StripExecutor pb44;
        Run run = Execute(s, pb44);
        CHECK(run.verdict.ok() && run.rep.outcome == Outcome::Refused, s.name + "\n" + run.verdict.Text());
    }

    // 3b. PB50 ebc3a710 StripToBags through the MoveExecutor seam (findings, not harness failures).
    int pb50Rows = 0, pb50Clean = 0, pb50Findings = 0;
    {
        std::printf("[SYNTHETIC] PB50 ebc3a710 StripToBags (real PlayerbotsAutoGearStorage::Execute, transcribed lambdas)\n");
        std::printf("[SYNTHETIC] %-4s %-6s %-17s %-17s %s\n", "case", "hook#", "PB50 StripResult", "oracle truth", "verdict");
        g_hazard.nested = [](Synthetic::Core& core) {
            Pb50StripAdapter nested; // PB50 re-entering its own strip
            nested.Execute(core, Strip(core, {2}));
        };
        auto hazards50 = [] { return HookHazardScenarios(); };
        std::size_t const count = hazards50().size();
        for (std::size_t i = 0; i < count; ++i)
        {
            bool const hookCase = i < 6; // H1-H6 depend on which callback fires
            for (int fire = 1; fire <= (hookCase ? 3 : 1); ++fire)
            {
                g_hazard.fireOnCall = fire;
                Scenario s = hazards50()[i];
                Pb50StripAdapter pb50;
                Run run = Execute(s, pb50);
                // PB50's StripResult carries no GUID list: list-mismatch lines are ignored below.
                ++pb50Rows;
                bool clean = run.verdict.violations.empty() ||
                    std::all_of(run.verdict.violations.begin(), run.verdict.violations.end(),
                        [](std::string const& x) { return x.rfind("RecoveryRequired list", 0) == 0; });
                pb50Clean += clean;
                pb50Findings += !clean;
                std::string verdict = "clean";
                if (!clean)
                    for (auto const& x : run.verdict.violations)
                        if (x.rfind("RecoveryRequired list", 0) != 0)
                        {
                            verdict = "FINDING: " + x;
                            break;
                        }
                std::printf("[SYNTHETIC] %-4s %-6s %-17s %-17s %s\n", s.name.substr(0, 2).c_str(),
                    hookCase ? std::to_string(fire).c_str() : "-", Name(pb50.last), Name(run.verdict.truth),
                    verdict.c_str());
            }
        }
        g_hazard = HazardControl{};
        // Controls: fault-free strips must commit cleanly.
        for (int n = 1; n <= 5; ++n)
        {
            Scenario s{"PB50 control strip " + std::to_string(n),
                [n](Synthetic::Core& c) { return Strip(c, FirstSlots(n)); }, Outcome::Applied};
            Pb50StripAdapter pb50;
            Run run = Execute(s, pb50);
            CHECK(run.verdict.ok() && pb50.last == Pb50StripResult::Done, s.name + "\n" + run.verdict.Text());
        }
        // Wider: the forward and rollback fault matrices (strip plans) against PB50.
        int matrix = 0, matrixClean = 0;
        std::vector<std::string> matrixFindings;
        for (auto group : {ForwardFaultScenarios(), RollbackFaultScenarios()})
            for (auto const& s : group)
            {
                Pb50StripAdapter pb50;
                Run run = Execute(s, pb50);
                ++matrix;
                bool clean = std::all_of(run.verdict.violations.begin(), run.verdict.violations.end(),
                    [](std::string const& x) { return x.rfind("RecoveryRequired list", 0) == 0; });
                matrixClean += clean;
                if (!clean)
                    matrixFindings.push_back(s.name + " -> " + Name(pb50.last) + ": " + run.verdict.violations.front());
            }
        std::printf("[SYNTHETIC] PB50 hazard rows: %d, oracle-clean %d, findings %d\n", pb50Rows, pb50Clean, pb50Findings);
        std::printf("[SYNTHETIC] PB50 F+R fault matrix: %d/%d oracle-clean (GUID list not checked; PB50 StripResult has none)\n",
            matrixClean, matrix);
        for (auto const& f : matrixFindings)
            std::printf("[SYNTHETIC]   PB50 matrix finding: %s\n", f.c_str());
    }

    // 4. M4: two-hander equip then AutoUnequipOffhandIfNeed with full bags mails the off-hand.
    {
        Scenario s{"M4 mail path", [](Synthetic::Core& c) {
            Pos only = FreeStorage(c)[0];
            Synthetic::Item* th = c.Create(782, 0x3005, only, true);
            Request r = Base(c);
            r.moves.push_back(Move{th->guid, only, Pos{BAG0, Synthetic::MAINHAND}});
            r.moves.push_back(Move{0x1000, Pos{BAG0, 0}, only}); // refills the only free slot
            return r; }, Outcome::Refused, [] { return Fixture(1, Synthetic::MAINHAND); }};
        NativeTwoHanderThenAutoUnequip naive;
        Run run = Execute(s, naive);
        CHECK(!run.core->mailed.empty() && !run.verdict.ok(), "naive two-hander path reaches the mail branch and is flagged");
        ReferenceExecutor ref;
        Run safe = Execute(s, ref);
        CHECK(safe.rep.outcome == Outcome::Refused && safe.verdict.ok() && safe.core->mailed.empty(),
            "reference refuses the two-hander plan before mutation");
    }

    // 5. Negative controls: each one-defect mutant must be caught by the suite.
    struct Mutant
    {
        char const* name;
        Knobs k;
    };
    std::vector<Mutant> mutants;
    auto add = [&](char const* n, auto set) {
        Knobs k;
        set(k);
        mutants.push_back({n, k});
    };
    add("no rollback, reports Restored", [](Knobs& k) { k.noRollback = true; });
    add("forward-order rollback", [](Knobs& k) { k.forwardOrderRollback = true; });
    add("unverified rollback", [](Knobs& k) { k.trustRollback = true; });
    add("native merge store / no empty-destination check", [](Knobs& k) { k.allowMerge = true; });
    add("no duplicate-destination check", [](Knobs& k) { k.skipDuplicateDestination = true; });
    add("no incarnation/stamp/logout check", [](Knobs& k) { k.skipCustody = true; });
    add("no item-owner check", [](Knobs& k) { k.skipItemOwner = true; });
    add("no revalidation after callback", [](Knobs& k) { k.skipRevalidateAfterHook = true; });
    add("labels failures Refused after effects", [](Knobs& k) { k.refusedAfterEffects = true; });
    add("partial RecoveryRequired list", [](Knobs& k) { k.partialRecoveryList = true; });
    add("no two-hander/off-hand check", [](Knobs& k) { k.skipTwoHandCheck = true; });
    add("no re-entry fence", [](Knobs& k) { k.skipReentryFence = true; });
    add("success from empty source only", [](Knobs& k) { k.trustSourceEmpty = true; });
    add("callback change labelled Refused (pre-agreement)", [](Knobs& k) { k.callbackChangeRefused = true; });
    add("two-hander rule on final state only (ignores order)", [](Knobs& k) { k.twoHandFinalStateOnly = true; });
    add("PB44 pattern: holds Item* across callback, no revalidation",
        [](Knobs& k) { k.holdPointerAcrossHook = true; k.skipRevalidateAfterHook = true; });
    int killed = 0;
    for (auto const& m : mutants)
    {
        std::string by;
        for (auto const& s : scenarios)
        {
            ReferenceExecutor exec(m.k);
            Run run = Execute(s, exec);
            if (!run.verdict.ok() || run.rep.outcome != s.expected)
            {
                by = s.name;
                break;
            }
        }
        CHECK(!by.empty(), std::string("mutant survived: ") + m.name);
        killed += !by.empty();
        std::printf("[SYNTHETIC] mutant %-60s %s\n", m.name, by.empty() ? "SURVIVED" : ("killed by " + by).c_str());
    }
    std::printf("[SYNTHETIC] negative controls: %d/%zu mutants killed\n", killed, mutants.size());

    // 6. Real-header predicate sanity: the exact gate rejects what the model says it must.
    {
        PB59::ExactStorageReservation res{0x1000, kBagGuid, Pos{kBagSlot, 1}.Packed(), 1};
        PB59::ExactStorageObservation ok{0x1000, kBagGuid, Pos{kBagSlot, 1}.Packed(), 1, true, true, true, true, false, false};
        CHECK(PB59::AcceptExactStorage(res, ok), "exact gate accepts the reserved empty slot");
        auto occupied = ok;
        occupied.occupied = true;
        CHECK(!PB59::AcceptExactStorage(res, occupied), "exact gate refuses an occupied (merge) destination");
        auto replacedBag = ok;
        replacedBag.bagGuid = kBagGuid + 1;
        CHECK(!PB59::AcceptExactStorage(res, replacedBag), "exact gate refuses a replaced bag");
        CHECK(!PB59::AcceptExactEquip(res, ok), "exact equip gate refuses a bag position");
    }

    std::printf("%s: %d checks, %d failures, %zu scenarios (ALL SYNTHETIC)\n", g_failures ? "FAIL" : "PASS",
        g_checks, g_failures, scenarios.size());
    return g_failures ? 1 : 0;
}
