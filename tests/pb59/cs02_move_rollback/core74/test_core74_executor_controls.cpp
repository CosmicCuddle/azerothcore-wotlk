// CS02 / PB59 -- independent fault controls for Codex's future four-outcome executor.
//
// PROVISIONAL SEAM. Codex's native executor signature does not exist yet, so the
// controls below run through `ProvisionalExecutor` (a callable taking an
// OwnerWorld and a Request, returning the four agreed outcomes). Outcome names
// equal Claude's PB52 ee084ebb copy (Applied / Restored / Refused /
// RecoveryRequired; #52 7133159535; mod-playerbots ee084ebbb82fdef3209e5c586211608b947225c1
// src/Bot/AutoGearCoreBatchRequest.h blob d0a12e5e, read-only: same names, same order).
// Native binding waits for the real signature.
//
// REAL (Core74 fa67482573e9426b701adb9b546a44a6d6113237, pinned by the runner):
//   PB59EffectBoundary.h        blob 008c93c4957335d0e20033303618b87d5917c2cf
//   PB59TransactionAdmission.h  blob 6c3bf34b767f58c72232145c143263cc66d60fdd
//   PB59InventoryBatchPlan.h    blob cb5574ab902334431ac42067013a8feb6d2fe57b
// SYNTHETIC: inventory core and its effect chain (shaped like the fa674825
// _ApplyItemMods Check sites), `CandidateExecutor` (a reference model of the
// executor CS02 expects, NOT Codex's code), its verifier, callbacks, owners,
// the other-thread producer, and every mutant.
//
// Not duplicated: structural plan tests (core74 crosscheck) and CS01's
// EffectBoundary header tests (test blob 410477aa: frame identity, nesting
// poison, recursive verifier, null owner, throw, second-thread no-frame).
// These controls sit one level up: what the EXECUTOR must report when those
// mechanisms fire in the middle of a real batch.
//
//   (a) targeted destination bag replacement during effects
//   (b) same-owner re-entry across another owner (A -> B -> A)
//   (c) pending-intent and lifetime faults; no thread-local frame as proof
#include "../MoveRollbackOracle.h"
#include "PB59EffectBoundary.h"
#include "PB59InventoryBatchPlan.h"
#include "PB59TransactionAdmission.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace CS02MoveRollback;
using Ledger = PB59::TransactionAdmission;
using Synthetic::BAG0;
using Synthetic::EventKind;

namespace
{
int g_checks = 0, g_failures = 0;
#define CHECK(cond, what)                                                                  \
    do                                                                                     \
    {                                                                                      \
        ++g_checks;                                                                        \
        if (!(cond))                                                                       \
        {                                                                                  \
            ++g_failures;                                                                  \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, std::string(what).c_str()); \
        }                                                                                  \
    } while (0)

// ---------------------------------------------------------------- owners
struct OwnerWorld
{
    std::unique_ptr<Synthetic::Core> core = std::make_unique<Synthetic::Core>();
    std::unique_ptr<Ledger> ledger = std::make_unique<Ledger>();
    std::shared_ptr<bool> alive = std::make_shared<bool>(true); // lifetime sentinel (synthetic)
    std::function<void()> betweenSteps; // SYNTHETIC scheduling point for an other-thread producer
    int derefAfterLoss = 0;             // verifier touched the owner after the sentinel died
    void LoseOwner()                    // callback-driven owner loss (logout delete analogue)
    {
        *alive = false;
        ledger.reset(); // REAL ledger destroyed mid-scope; MutationScope must survive it
        core->loggingOut = true;
    }
};

std::unique_ptr<OwnerWorld> MakeOwner(std::uint64_t guid, std::uint64_t itemBase)
{
    auto w = std::make_unique<OwnerWorld>();
    w->core->ownerGuid = guid;
    for (std::uint8_t s : {0, 2, 4})
        w->core->Create(100 + s, itemBase + s, Pos{BAG0, s});
    w->core->CreateBag(itemBase + 0xB19, 19, 4);
    w->core->Create(700, itemBase + 0x70, Pos{BAG0, 23}); // equippable pack item
    w->core->effectCheck = [](Synthetic::Core& c) { return PB59::EffectBoundary::Check(&c); };
    return w;
}

// ---------------------------------------------------------------- provisional seam
struct Knobs
{
    bool frameByExecutor = false;       // M1 frame keyed by the executor, not the owner
    bool verifierIgnoresPending = false; // M2 ledger CanAccess alone as permission
    bool verifierNoBagIdentity = false;  // M3 planned positions only, container identity ignored
    bool verifierPlannedOnly = false;    // M4 planned positions only (with bag identity)
    bool ignoreFrameFailure = false;     // M5 continue after the frame latched failure
    bool restoreAfterChainStop = false;  // M6 roll back and report Restored after partial effects
    bool noExplicitCheck = false;        // M7 rely on in-chain Check() only before writes
    bool derefBeforeSentinel = false;    // M8 verifier reads the owner before the sentinel
    bool admissionBeforeFrame = false;   // M9 ledger admission first, frame after (re-entry unmarked)
    bool skipAdmission = false;          // M10 proceed despite ledger refusal / pre-poisoned frame
    bool appliedFromSlotFacts = false;   // M11 report Applied when final slots look right after a stop
};

enum class Stage { Admission, Source, Detached, AtDest, Done, Rollback };

class CandidateExecutor
{
public:
    explicit CandidateExecutor(Knobs k = {}) : _k(k) {}
    Report Execute(OwnerWorld& w, Request const& r);

private:
    struct Ctx
    {
        CandidateExecutor* self;
        OwnerWorld* world;
        Synthetic::Core* core;
        std::weak_ptr<bool> alive;
        Ledger::MutationScope* scope = nullptr;
        Ledger* ledger = nullptr; // raw: only touched after scope->CanAccess proves it alive
        std::uint64_t ledgerInc = 0, coreInc = 0;
        Request const* request;
        Snapshot s0;
        std::vector<std::uint64_t> plannedBag;
        std::size_t cur = 0;
        Stage stage = Stage::Admission;
    };
    static bool Verify(void* p) noexcept;
    static Snapshot StageSnapshot(Ctx const& c);
    Knobs _k;
};

Snapshot CandidateExecutor::StageSnapshot(Ctx const& c)
{
    Snapshot e = c.s0;
    auto const& m = c.request->moves;
    auto place = [&](std::size_t j, bool atDest) {
        auto it = e.slots.find(m[j].from);
        if (it == e.slots.end())
            return;
        auto f = it->second;
        e.slots.erase(it);
        e.where.erase(m[j].itemGuid);
        if (atDest)
        {
            f.bag = c.plannedBag[j];
            f.position = m[j].to.Packed();
            e.slots[m[j].to] = f;
            e.where[m[j].itemGuid] = m[j].to;
        }
    };
    std::size_t const done = c.stage == Stage::Done ? m.size() : c.cur;
    for (std::size_t j = 0; j < done; ++j)
        place(j, true);
    if (c.stage == Stage::Detached)
        place(c.cur, false);
    if (c.stage == Stage::AtDest)
        place(c.cur, true);
    return e;
}

bool CandidateExecutor::Verify(void* p) noexcept
{
    Ctx& c = *static_cast<Ctx*>(p);
    Knobs const& k = c.self->_k;
    if (c.stage == Stage::Admission)
        return false; // no effect may run before the ticket is open
    if (k.derefBeforeSentinel)
    {
        auto a = c.alive.lock();
        if (!a || !*a)
            ++c.world->derefAfterLoss; // stands in for reading Player state first
    }
    auto a = c.alive.lock();
    if (!a || !*a)
        return false; // sentinel FIRST; nothing below dereferences a lost owner
    if (!c.scope || !c.scope->CanAccess(c.ledgerInc, Ledger::Access::Forward))
        return false;
    if (!k.verifierIgnoresPending && (c.ledger->PendingSave() || c.ledger->PendingLogout()))
        return false;
    if (c.stage == Stage::Rollback)
        return true; // reverse writes: lifetime + access only; final state is proven afterwards
    Snapshot const now = Capture(*c.core);
    Snapshot const want = StageSnapshot(c);
    if (!k.verifierPlannedOnly && !k.verifierNoBagIdentity)
        return now == want;
    auto const& moves = c.request->moves;
    for (std::size_t j = 0; j < moves.size(); ++j)
    {
        if (!k.verifierNoBagIdentity && moves[j].to.bag != BAG0 && c.core->BagGuid(moves[j].to) != c.plannedBag[j])
            return false;
        for (Pos q : {moves[j].from, moves[j].to})
        {
            auto x = now.slots.find(q), y = want.slots.find(q);
            if ((x == now.slots.end()) != (y == want.slots.end()))
                return false;
            if (x != now.slots.end() && !PB59::SameInventoryInstance(x->second, y->second))
                return false;
            if (x != now.slots.end() && !k.verifierNoBagIdentity && x->second.bag != y->second.bag)
                return false;
        }
    }
    return now.incarnation == want.incarnation && now.loggingOut == want.loggingOut;
}

Report CandidateExecutor::Execute(OwnerWorld& w, Request const& r)
{
    Synthetic::Core& core = *w.core;
    auto refuse = [](char const* why) { return Report{Outcome::Refused, why, {}}; };
    if (!w.alive || !*w.alive || !w.ledger)
        return refuse("owner gone");
    Ledger& ledger = *w.ledger;
    auto admitted = [&] { return ledger.CanBegin() && !ledger.PendingSave() && !ledger.PendingLogout(); };
    if (_k.admissionBeforeFrame && !_k.skipAdmission && !admitted())
        return refuse("ledger admission");

    Ctx ctx{this, &w, &core, w.alive, nullptr, &ledger, ledger.OwnerIncarnation(), core.incarnation, &r, Capture(core), {}};
    void const* key = _k.frameByExecutor ? static_cast<void const*>(this) : static_cast<void const*>(&core);
    // Frame FIRST (before any read): a same-owner re-entry poisons the outer frame.
    std::optional<PB59::EffectBoundary> frame;
    frame.emplace(key, &ctx, &Verify);
    if (!_k.skipAdmission && (frame->Failed() || (!_k.admissionBeforeFrame && !admitted())))
        return refuse(frame->Failed() ? "same-owner re-entry" : "ledger admission");

    std::vector<PB59::InventoryBatchMove> plan;
    for (auto const& m : r.moves)
    {
        if (!core.ValidPosition(m.to) || core.GetItemByPos(m.to))
            return refuse("native gate: destination");
        plan.push_back({Facts(core, m.from, core.GetItemByPos(m.from)), Facts(core, m.to, nullptr)});
        ctx.plannedBag.push_back(core.BagGuid(m.to));
    }
    if (PB59::ValidateInventoryBatchPlan(core.ownerGuid, std::span<PB59::InventoryBatchMove const>(plan.data(), plan.size())) !=
        PB59::InventoryBatchPlanError::None)
        return refuse("structural plan");

    auto scope = std::make_unique<Ledger::MutationScope>(ledger);
    if (!scope->GetTicket() && !_k.skipAdmission)
        return refuse("no ticket");
    ctx.scope = scope.get();
    ctx.stage = Stage::Source;
    Ledger::MutationScope* const scopeRaw = scope.get();
    auto finish = [&] {
        bool const ok = w.ledger && scopeRaw->Finish({true, true, true, true});
        return ok;
    };
    auto recovery = [&](char const* why) {
        // Dropping the unfinished scope moves a live ledger to RecoveryRequired;
        // after owner loss the scope destructor must not touch anything.
        scope.reset();
        Report rep{Outcome::RecoveryRequired, why, {}};
        for (auto const& m : r.moves)
        {
            Item* it = (w.alive && *w.alive) ? core.FindArena(m.itemGuid) : nullptr;
            if (!it || it->pos != m.to)
                rep.notWhereExpected.push_back(m.itemGuid);
        }
        return rep;
    };
    auto check = [&] { return _k.noExplicitCheck || PB59::EffectBoundary::Check(&core); };
    auto chainFailed = [&] { return frame->Failed() && !_k.ignoreFrameFailure; };
    // The frame latch does not say WHERE it failed: an in-chain stop means partial
    // effects (recovery), an explicit pre-write refusal does not. Track it here.
    std::size_t const evStart = core.ledger.size();
    auto chainStopped = [&] {
        for (std::size_t i = evStart; i < core.ledger.size(); ++i)
            if (core.ledger[i].kind == EventKind::EffectStopped)
                return !_k.ignoreFrameFailure;
        return false;
    };
    Item* limbo = nullptr; // current item detached by us and not yet linked

    std::size_t done = 0;
    bool wrote = false;
    char const* why = nullptr;
    bool partial = false;
    for (; done < r.moves.size(); ++done)
    {
        auto const& m = r.moves[done];
        ctx.cur = done;
        ctx.stage = Stage::Source;
        if (!check())
        {
            why = "verifier refused before write";
            partial = chainStopped();
            break;
        }
        Item* item = core.GetItemByPos(m.from);
        if (!item || item->guid != m.itemGuid)
        {
            why = "source identity";
            break;
        }
        ctx.stage = Stage::Detached;
        try
        {
            core.RemoveItem(m.from.bag, m.from.slot);
        }
        catch (Synthetic::InjectedFailure const&)
        {
            ctx.stage = Stage::Source;
            why = "remove threw";
            break;
        }
        if (!core.FindArena(m.itemGuid) || core.GetItemByPos(m.from) == item)
        {
            ctx.stage = Stage::Source;
            why = "remove did not detach";
            break;
        }
        wrote = true;
        limbo = item;
        if (chainFailed())
        {
            why = "effect chain stopped during unequip";
            partial = true;
            break;
        }
        if (!check())
        {
            why = "verifier refused before insert";
            partial = chainStopped();
            break;
        }
        ctx.stage = Stage::AtDest;
        Item* stored = nullptr;
        try
        {
            if (m.to.IsEquipment())
                stored = core.EquipItem(m.to.Packed(), item);
            else
            {
                std::vector<Pos> dest{m.to};
                stored = core.StoreItem(dest, item);
            }
        }
        catch (Synthetic::InjectedFailure const&)
        {
            why = "insert threw";
            partial = true;
            break;
        }
        if (chainFailed())
        {
            why = "effect chain stopped during equip";
            partial = true;
            break;
        }
        Item* there = core.GetItemByPos(m.to);
        if (!stored && core.Live(item) && item->pos.slot == Synthetic::NULL_SLOT && !chainStopped())
        {
            why = "insert refused"; // nothing linked: still in limbo, no partial effects
            break;
        }
        if (stored != item || there != item || item->pos != m.to)
        {
            why = "destination identity";
            partial = true;
            break;
        }
        limbo = nullptr;
        if (w.betweenSteps)
            w.betweenSteps();
        if (!w.alive || !*w.alive)
        {
            why = "owner lost";
            partial = true;
            break;
        }
    }
    if (!why)
    {
        ctx.stage = Stage::Done;
        if (!check() || chainFailed())
            return recovery("final verification");
        return finish() ? Report{Outcome::Applied, "applied", {}} : recovery("finish refused");
    }
    if (!w.alive || !*w.alive)
        return recovery("owner lost");
    if (partial && _k.appliedFromSlotFacts && Capture(core) == Expected(core, ctx.s0, r))
        return finish() ? Report{Outcome::Applied, "slot facts", {}} : recovery("finish refused");
    if (partial && !_k.restoreAfterChainStop)
        return recovery(why);
    if (!wrote && done == 0)
        return finish() ? refuse(why) : recovery("finish refused");
    // Verified reverse of the executor's own completed moves. The latched frame
    // would stop every reverse effect chain, so it is closed and a fresh frame
    // (no outer same-owner frame remains) covers the reverse writes.
    frame.reset();
    ctx.stage = Stage::Rollback;
    frame.emplace(key, &ctx, &Verify);
    if (frame->Failed())
        return recovery("rollback frame");
    if (limbo)
    {
        auto const& m = r.moves[done];
        Item* back = m.from.IsEquipment() ? core.EquipItem(m.from.Packed(), limbo) : [&] {
            std::vector<Pos> d{m.from};
            return core.StoreItem(d, limbo);
        }();
        if (back != limbo || chainStopped())
            return recovery("rollback of in-flight item");
    }
    for (std::size_t j = done; j-- > 0;)
    {
        auto const& m = r.moves[j];
        Item* it = core.GetItemByPos(m.to);
        if (!it || it->guid != m.itemGuid || !PB59::EffectBoundary::Check(&core))
            return recovery("rollback verification");
        core.RemoveItem(m.to.bag, m.to.slot);
        Item* back = m.from.IsEquipment() ? core.EquipItem(m.from.Packed(), it) : [&] {
            std::vector<Pos> d{m.from};
            return core.StoreItem(d, it);
        }();
        if (back != it || chainStopped())
            return recovery("rollback insert");
    }
    if (!(Capture(core) == ctx.s0))
        return recovery("rollback state");
    return finish() ? Report{Outcome::Restored, why, {}} : recovery("finish refused");
}

template<class E>
concept ProvisionalExecutor = requires(E& e, OwnerWorld& w, Request const& r) {
    { e.Execute(w, r) } -> std::same_as<Report>;
};
static_assert(ProvisionalExecutor<CandidateExecutor>);

// PB52 ee084ebb mapping (read-only copy of Claude's table; synthetic).
enum class StripResult { Done, Restored, Refused, RecoveryRequired };
StripResult MapPb52(Outcome o, bool fingerprintUnchanged)
{
    switch (o)
    {
        case Outcome::Applied: return StripResult::Done;
        case Outcome::Restored: return StripResult::Restored;
        case Outcome::Refused: return fingerprintUnchanged ? StripResult::Refused : StripResult::RecoveryRequired;
        case Outcome::RecoveryRequired: return StripResult::RecoveryRequired;
    }
    return StripResult::RecoveryRequired;
}

// ---------------------------------------------------------------- controls
Request Plan(Synthetic::Core const& c, std::vector<std::pair<Pos, Pos>> moves)
{
    Request r{c.ownerGuid, c.incarnation, c.custodyStamp, {}};
    for (auto [from, to] : moves)
        r.moves.push_back(Move{c.GetItemByPos(from)->guid, from, to});
    return r;
}
constexpr Pos E(std::uint8_t s) { return Pos{BAG0, s}; }
constexpr Pos B19(std::uint8_t s) { return Pos{19, s}; }

struct Expect
{
    std::vector<Outcome> allowed;
    bool pendingLogoutAfter = false, pendingSaveAfter = false;
};

struct Result
{
    Report rep;
    bool ok = true;
    std::string why;
};

int ExecutorWritesAfter(Synthetic::Core const& c, std::size_t from)
{
    int n = 0;
    for (std::size_t i = from; i < c.ledger.size(); ++i)
        n += c.ledger[i].actor == Synthetic::Actor::Executor &&
            (c.ledger[i].kind == EventKind::Insert || c.ledger[i].kind == EventKind::Remove);
    return n;
}
std::size_t FirstFault(Synthetic::Core const& c)
{
    for (std::size_t i = 0; i < c.ledger.size(); ++i)
        if (c.ledger[i].actor == Synthetic::Actor::Hook || c.ledger[i].kind == EventKind::PendingIntent ||
            c.ledger[i].kind == EventKind::EffectStopped)
            return i + 1;
    return c.ledger.size();
}

// Common judgement for one owner after a run. Independent of the executor.
void Judge(OwnerWorld& w, Request const& r, Snapshot const& s0, Report const& rep, Expect const& x, Result& out,
    std::string const& who)
{
    auto fail = [&](std::string const& m) {
        if (out.ok)
            out.why = who + ": " + m;
        out.ok = false;
    };
    if (std::find(x.allowed.begin(), x.allowed.end(), rep.outcome) == x.allowed.end())
        fail(std::string("outcome ") + Name(rep.outcome) + " not allowed (" + rep.reason + ")");
    if (w.derefAfterLoss)
        fail("verifier touched the owner after its lifetime sentinel died");
    bool const lost = !*w.alive;
    if (lost)
    {
        // After loss no executor write may follow the first hook/fault event.
        if (ExecutorWritesAfter(*w.core, FirstFault(*w.core)))
            fail("executor wrote after owner loss");
        return;
    }
    Synthetic::Core const& c = *w.core;
    std::vector<std::string> inc;
    Coherence(c, inc);
    if (!inc.empty())
        fail("incoherent: " + inc.front());
    for (auto const& e : c.ledger)
        if (e.kind == EventKind::StaleUse || e.kind == EventKind::MutationWhileClosed)
            fail("stale use / mutation while closed");
    if (x.pendingLogoutAfter && !w.ledger->PendingLogout())
        fail("pending logout lost");
    if (x.pendingSaveAfter && !w.ledger->PendingSave())
        fail("pending save lost");
    // Any executor write after a pending intent / stopped chain is forward work on a refused context,
    // except the executor's own verified reverse (Restored is not allowed in those controls anyway).
    if (rep.outcome != Outcome::Restored)
    {
        bool stopped = false;
        for (auto const& e : c.ledger)
        {
            if (e.kind == EventKind::PendingIntent || e.kind == EventKind::EffectStopped)
                stopped = true;
            else if (stopped && e.actor == Synthetic::Actor::Executor && e.kind == EventKind::Insert)
            {
                fail("executor inserted after a pending intent / stopped effect chain");
                break;
            }
        }
    }
    // No planned item may sit in a container that is not the planned one.
    for (auto const& m : r.moves)
        if (Item* it = c.FindArena(m.itemGuid); it && it->pos == m.to && m.to.bag != BAG0 &&
            c.BagGuid(m.to) != s0.slots.at(Pos{BAG0, m.to.bag}).item->guid)
            fail("item stored into a replaced destination container");
    switch (rep.outcome)
    {
        case Outcome::Applied:
            if (!(Capture(c) == Expected(c, s0, r)))
                fail("Applied but state differs from the exact plan");
            if (w.ledger->State() != Ledger::Phase::Idle)
                fail("Applied without finishing the ticket");
            break;
        case Outcome::Refused:
            if (ExecutorWritesAfter(c, 0))
                fail("Refused after executor writes");
            break;
        case Outcome::Restored:
            if (!(Capture(c) == s0))
                fail("Restored but state differs from s0");
            break;
        case Outcome::RecoveryRequired:
            if (w.ledger->State() != Ledger::Phase::RecoveryRequired)
                fail("RecoveryRequired without quarantining the ledger");
            break;
    }
    MapPb52(rep.outcome, true); // mapping is total (switch without default, -Wswitch)
    if ((MapPb52(rep.outcome, true) == StripResult::Done) != (rep.outcome == Outcome::Applied))
        fail("PB52 mapping: Done iff Applied");
}

struct Control
{
    std::string id, what;
    // Runs the scenario with executor knobs; returns the judged result.
    std::function<Result(Knobs const&)> run;
};

// Single-owner control helper.
template<class Setup>
Result RunOne(Knobs const& k, Setup setup, Expect x)
{
    auto w = MakeOwner(0x100, 0x1000);
    Request r = setup(*w);
    Snapshot s0 = Capture(*w->core);
    w->core->ledger.clear();
    CandidateExecutor ex(k);
    Result out;
    out.rep = ex.Execute(*w, r);
    Judge(*w, r, s0, out.rep, x, out, "A");
    return out;
}

constexpr std::uint64_t kNewBag = 0xBEEF;
std::vector<Control> Controls()
{
    std::vector<Control> cs;
    auto strip2 = [](OwnerWorld& w) { return Plan(*w.core, {{E(0), B19(0)}, {E(2), B19(1)}}); };
    auto equipThenStrip = [](OwnerWorld& w) { return Plan(*w.core, {{E(23), E(1)}, {E(0), B19(0)}}); };
    auto stripThenEquip = [](OwnerWorld& w) { return Plan(*w.core, {{E(0), B19(0)}, {E(23), E(1)}}); };

    // ---- (a) targeted destination bag replacement during effects
    cs.push_back({"A0", "control: effect callback changes nothing", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->effectsOnUnequip = true;
            w.core->onApplyItemMods = [](Synthetic::Core&, Item*) {};
            return strip2(w); }, {{Outcome::Applied}});
    }});
    cs.push_back({"A1", "unequip effects of move 0 replace its destination bag (same size, empty)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->effectsOnUnequip = true;
            w.core->onApplyItemMods = [](Synthetic::Core& c, Item*) {
                if (c.BagGuid(B19(0)) != kNewBag) c.HookReplaceBag(19, kNewBag); };
            return strip2(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"A2", "equip effects of move 0 replace move 1's destination bag", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->onApplyItemMods = [](Synthetic::Core& c, Item*) {
                if (c.BagGuid(B19(0)) != kNewBag) c.HookReplaceBag(19, kNewBag); };
            return equipThenStrip(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"A3", "replacement bag is larger: destination slot still valid and empty", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->onApplyItemMods = [](Synthetic::Core& c, Item*) {
                if (c.BagGuid(B19(0)) != kNewBag) c.HookReplaceBag(19, kNewBag, 16); };
            return equipThenStrip(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"A4", "other-thread producer replaces the destination bag between steps (no effect chain)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            Synthetic::Core* c = w.core.get();
            w.betweenSteps = [c] {
                if (c->BagGuid(B19(1)) == kNewBag) return;
                bool sawFrame = true;
                std::thread t([&] { sawFrame = !PB59::EffectBoundary::Check(c); c->HookReplaceBag(19, kNewBag); });
                t.join();
                CHECK(!sawFrame, "A4 other thread sees no frame: Check() is true there");
            };
            // bag 19 must be empty for a native swap: move 0 goes to the backpack
            return Plan(*w.core, {{E(0), E(24)}, {E(2), B19(1)}}); }, {{Outcome::RecoveryRequired}});
    }});

    // ---- (b) same-owner re-entry across another owner
    struct Two
    {
        std::unique_ptr<OwnerWorld> a = MakeOwner(0x100, 0x1000), b = MakeOwner(0x200, 0x2000);
    };
    auto runAB = [](Knobs const& k, std::function<void(Two&, Knobs const&, Report&, Report&)> wire,
                     std::vector<Outcome> aAllowed, std::vector<Outcome> bAllowed, std::vector<Outcome> innerAllowed) {
        Two t;
        Request ra = Plan(*t.a->core, {{E(23), E(1)}, {E(0), B19(0)}});
        Request rb = Plan(*t.b->core, {{E(23), E(1)}, {E(2), B19(0)}});
        Snapshot sa = Capture(*t.a->core), sb = Capture(*t.b->core);
        Report bRep{}, innerRep{Outcome::Refused, "not attempted", {}};
        wire(t, k, bRep, innerRep);
        t.a->core->ledger.clear();
        t.b->core->ledger.clear();
        CandidateExecutor ex(k);
        Result out;
        out.rep = ex.Execute(*t.a, ra);
        Judge(*t.a, ra, sa, out.rep, {aAllowed}, out, "A");
        Judge(*t.b, rb, sb, bRep, {bAllowed}, out, "B");
        if (std::find(innerAllowed.begin(), innerAllowed.end(), innerRep.outcome) == innerAllowed.end() && out.ok)
        {
            out.ok = false;
            out.why = std::string("inner A outcome ") + Name(innerRep.outcome) + " (" + innerRep.reason + ")";
        }
        return out;
    };
    cs.push_back({"B0", "control: A's effects run B's batch; B leaves A alone", [=](Knobs const& k) {
        return runAB(k, [](Two& t, Knobs const& kk, Report& bRep, Report&) {
            OwnerWorld* b = t.b.get();
            t.a->core->onApplyItemMods = [b, kk, &bRep](Synthetic::Core&, Item*) {
                static thread_local int depth = 0;
                if (depth++ == 0) { CandidateExecutor eb(kk); bRep = eb.Execute(*b, Plan(*b->core, {{E(23), E(1)}, {E(2), B19(0)}})); }
                --depth; };
        }, {Outcome::Applied}, {Outcome::Applied}, {Outcome::Refused});
    }});
    cs.push_back({"B1", "A -> B -> A: B's effects start another batch on A", [=](Knobs const& k) {
        return runAB(k, [](Two& t, Knobs const& kk, Report& bRep, Report& inner) {
            OwnerWorld* a = t.a.get();
            OwnerWorld* b = t.b.get();
            b->core->onApplyItemMods = [a, kk, &inner](Synthetic::Core&, Item*) {
                if (inner.reason != std::string("not attempted")) return;
                CandidateExecutor ia(kk);
                inner = ia.Execute(*a, Plan(*a->core, {{E(4), B19(2)}}));
            };
            a->core->onApplyItemMods = [b, kk, &bRep](Synthetic::Core&, Item*) {
                if (bRep.reason.empty()) { CandidateExecutor eb(kk); bRep = eb.Execute(*b, Plan(*b->core, {{E(23), E(1)}, {E(2), B19(0)}})); }
            };
        }, {Outcome::RecoveryRequired}, {Outcome::Applied}, {Outcome::Refused});
    }});
    cs.push_back({"B2", "A -> B: B's effects move an unplanned item of A (raw native write)", [=](Knobs const& k) {
        return runAB(k, [](Two& t, Knobs const& kk, Report& bRep, Report&) {
            OwnerWorld* a = t.a.get();
            OwnerWorld* b = t.b.get();
            b->core->onApplyItemMods = [a](Synthetic::Core&, Item*) {
                if (Item* it = a->core->GetItemByPos(E(4))) a->core->HookRelocate(it, E(30)); };
            a->core->onApplyItemMods = [b, kk, &bRep](Synthetic::Core&, Item*) {
                if (bRep.reason.empty()) { CandidateExecutor eb(kk); bRep = eb.Execute(*b, Plan(*b->core, {{E(23), E(1)}, {E(2), B19(0)}})); }
            };
        }, {Outcome::RecoveryRequired}, {Outcome::Applied}, {Outcome::Refused});
    }});
    cs.push_back({"B3", "A -> B: B's effects occupy A's next destination (raw native write)", [=](Knobs const& k) {
        return runAB(k, [](Two& t, Knobs const& kk, Report& bRep, Report&) {
            OwnerWorld* a = t.a.get();
            OwnerWorld* b = t.b.get();
            b->core->onApplyItemMods = [a](Synthetic::Core&, Item*) {
                if (!a->core->GetItemByPos(B19(0))) a->core->HookCreate(555, 0x1555, B19(0)); };
            a->core->onApplyItemMods = [b, kk, &bRep](Synthetic::Core&, Item*) {
                if (bRep.reason.empty()) { CandidateExecutor eb(kk); bRep = eb.Execute(*b, Plan(*b->core, {{E(23), E(1)}, {E(2), B19(0)}})); }
            };
        }, {Outcome::RecoveryRequired}, {Outcome::Applied}, {Outcome::Refused});
    }});

    // ---- (c) pending intent and lifetime
    auto intent = [](Ledger::Work work) {
        return [work](OwnerWorld& w) {
            Ledger* L = w.ledger.get();
            w.core->onApplyItemMods = [L, work](Synthetic::Core& c, Item*) {
                if (L->PendingLogout() || L->PendingSave()) return;
                (void)L->Request(work);
                c.RecordPendingIntent();
            };
        };
    };
    cs.push_back({"C1", "pending logout requested inside move 0's equip effects", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) { intent(Ledger::Work::Logout)(w); return equipThenStrip(w); },
            {{Outcome::RecoveryRequired}, true});
    }});
    cs.push_back({"C1b", "pending logout inside the LAST move's equip effects (slots already final)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) { intent(Ledger::Work::Logout)(w); return stripThenEquip(w); },
            {{Outcome::RecoveryRequired}, true});
    }});
    cs.push_back({"C2", "pending standalone save inside equip effects", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) { intent(Ledger::Work::StandaloneSave)(w); return equipThenStrip(w); },
            {{Outcome::RecoveryRequired}, false, true});
    }});
    cs.push_back({"C3", "owner lost inside equip effects (ledger destroyed, sentinel dead)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            OwnerWorld* self = &w;
            w.core->onApplyItemMods = [self](Synthetic::Core&, Item*) { if (*self->alive) self->LoseOwner(); };
            return equipThenStrip(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"C3b", "owner lost inside the LAST move's effects", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            OwnerWorld* self = &w;
            w.core->onApplyItemMods = [self](Synthetic::Core&, Item*) { if (*self->alive) self->LoseOwner(); };
            return stripThenEquip(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"C4", "owner incarnation changes inside effects (relog analogue)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->onApplyItemMods = [](Synthetic::Core& c, Item*) { if (c.incarnation == 7) ++c.incarnation; };
            return equipThenStrip(w); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"C5", "other-thread producer asks the ledger first: rejected, A applies", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            Synthetic::Core* c = w.core.get();
            Ledger* L = w.ledger.get();
            w.betweenSteps = [c, L] {
                static thread_local bool once = false;
                if (once) return;
                once = true;
                bool noFrame = false;
                Ledger::Decision d{};
                std::thread t([&] {
                    noFrame = PB59::EffectBoundary::Check(c); // true: no frame on this thread
                    d = L->Request(Ledger::Work::InventoryMutation);
                    if (d == Ledger::Decision::Admit) c->HookCreate(556, 0x1556, E(30));
                });
                t.join();
                once = false;
                CHECK(noFrame && d == Ledger::Decision::RejectBeforeMutation,
                    "C5 thread-local Check() is true on the other thread; the ledger rejects its mutation");
            };
            return Plan(*w.core, {{E(0), E(24)}, {E(2), E(25)}}); }, {{Outcome::Applied}});
    }});
    cs.push_back({"C6", "other-thread producer treats Check()==true as permission and writes A", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            Synthetic::Core* c = w.core.get();
            w.betweenSteps = [c] {
                if (c->GetItemByPos(E(30))) return;
                std::thread t([&] { if (PB59::EffectBoundary::Check(c)) c->HookCreate(557, 0x1557, E(30)); });
                t.join();
            };
            return Plan(*w.core, {{E(0), E(24)}, {E(2), E(25)}}); }, {{Outcome::RecoveryRequired}});
    }});
    cs.push_back({"C7", "first removal throws, nothing changed: Refused (F5 distinction)", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->removeFaults[0] = Synthetic::RemoveFault::Throw;
            return strip2(w); }, {{Outcome::Refused}});
    }});
    cs.push_back({"C8", "second insertion refused, no effects: verified Restored", [=](Knobs const& k) {
        return RunOne(k, [&](OwnerWorld& w) {
            w.core->effectCheck = nullptr; // plain moves, no chain
            w.core->insertFaults[1] = Synthetic::InsertFault::Refuse;
            return Plan(*w.core, {{E(0), E(24)}, {E(2), E(25)}}); }, {{Outcome::Restored}});
    }});
    return cs;
}
}

int main()
{
    std::printf("CS02 Core74 executor fault controls -- PROVISIONAL seam; REAL EffectBoundary/ledger/plan headers; "
                "executor, core, callbacks SYNTHETIC\n");
    auto controls = Controls();
    int pass = 0;
    for (auto const& c : controls)
    {
        Result r = c.run(Knobs{});
        pass += r.ok;
        CHECK(r.ok, c.id + " " + c.what + ": " + r.why);
        std::printf("[SYNTHETIC] %-4s %-84s %-16s %s\n", c.id.c_str(), c.what.c_str(), Name(r.rep.outcome),
            r.ok ? "ok" : ("FAIL " + r.why).c_str());
    }
    std::printf("[SYNTHETIC] candidate executor: %d/%zu controls hold\n", pass, controls.size());

    struct Mutant
    {
        char const* name;
        Knobs k;
    };
    std::vector<Mutant> mutants = {
        {"M1 frame keyed by executor, not owner", [] { Knobs k; k.frameByExecutor = true; return k; }()},
        {"M2 verifier: ledger CanAccess alone (pending ignored)", [] { Knobs k; k.verifierIgnoresPending = true; return k; }()},
        {"M3 verifier: planned positions, no container identity", [] { Knobs k; k.verifierNoBagIdentity = true; return k; }()},
        {"M4 verifier: planned positions only", [] { Knobs k; k.verifierPlannedOnly = true; return k; }()},
        {"M5 continue after frame failure", [] { Knobs k; k.ignoreFrameFailure = true; return k; }()},
        {"M6 roll back and report Restored after partial effects", [] { Knobs k; k.restoreAfterChainStop = true; return k; }()},
        {"M7 no explicit pre-write check (in-chain Check only)", [] { Knobs k; k.noExplicitCheck = true; return k; }()},
        {"M8 verifier reads owner before the sentinel", [] { Knobs k; k.derefBeforeSentinel = true; return k; }()},
        {"M9 ledger admission before the frame", [] { Knobs k; k.admissionBeforeFrame = true; return k; }()},
        {"M10 proceed despite ledger refusal / pre-poisoned frame", [] { Knobs k; k.skipAdmission = true; return k; }()},
        {"M11 Applied from final slot facts after a stopped chain", [] { Knobs k; k.appliedFromSlotFacts = true; return k; }()},
    };
    int killed = 0;
    for (auto const& m : mutants)
    {
        std::string by;
        for (auto const& c : controls)
        {
            Result r = c.run(m.k);
            if (!r.ok)
                by += (by.empty() ? "" : ",") + c.id;
        }
        killed += !by.empty();
        CHECK(!by.empty(), std::string("mutant survived: ") + m.name);
        std::printf("[SABOTAGE] %-58s %s\n", m.name, by.empty() ? "SURVIVED" : ("killed by " + by).c_str());
    }
    std::printf("[SABOTAGE] %d/%zu executor mutants killed\n", killed, mutants.size());
    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
