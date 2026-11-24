// CS02 / PB59 -- independent cross-check of Codex's Core74 structural header and ledger.
//
// REAL headers (from a Core74 checkout pinned by run_core74_crosscheck.sh):
//   PB59InventoryBatchPlan.h   blob cb5574ab902334431ac42067013a8feb6d2fe57b (4742d86f = 8276ff5e = 27b61359)
//   PB59TransactionAdmission.h blob 6c3bf34b767f58c72232145c143263cc66d60fdd (27b61359)
//   PB59InventorySlotFacts.h   (unchanged since Core53)
// Everything else is SYNTHETIC: the inventory core, executors, callbacks and the
// stand-ins for session/world/capture terms of Player::CheckPB59InventoryBatchPlan
// (which needs a native Player and is NOT executed here).
//
// Sections
//   A  oracle-generated valid / single-defect / fuzzed batches vs ValidateInventoryBatchPlan
//   G  structurally accepted batches that are explicit LATER NATIVE GATES (informational)
//   L  random ledger operation sequences vs independent admission invariants
//   C  CS01 #51 7131372670 pending-intent-with-live-sentinel case and the in-call
//      _ApplyItemMods callback boundary (#51 7131425394), as synthetic oracle scenarios
//   S  sabotage: suppressed error classes, lying ledger readers, unsafe executors
#include "../Executors.h"
#include "PB59InventoryBatchPlan.h"
#include "PB59TransactionAdmission.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace CS02MoveRollback;
using PB59::InventoryBatchMove;
using PB59::InventoryBatchPlanError;
using Ledger = PB59::TransactionAdmission;
using Synthetic::BAG0;

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

constexpr std::uint64_t kOwner = 0x100;
using Err = InventoryBatchPlanError;
char const* Name(Err e)
{
    switch (e)
    {
        case Err::None: return "None";
        case Err::Empty: return "Empty";
        case Err::TooLarge: return "TooLarge";
        case Err::Owner: return "Owner";
        case Err::Source: return "Source";
        case Err::Destination: return "Destination";
        case Err::DuplicateItem: return "DuplicateItem";
        case Err::Overlap: return "Overlap";
    }
    return "?";
}

// ------------------------------------------------------------------ A: generator + oracle
std::uint16_t Equip(std::uint8_t slot) { return std::uint16_t((BAG0 << 8) | slot); }
std::uint16_t Pack(std::uint8_t slot) { return std::uint16_t((BAG0 << 8) | (23 + slot)); }
std::uint16_t InBag(std::uint8_t bag, std::uint8_t slot) { return std::uint16_t((bag << 8) | slot); }

PB59::InventorySlotFacts Slot(std::uint16_t pos, std::uint64_t bag, std::optional<std::uint64_t> guid)
{
    PB59::InventorySlotFacts f;
    f.owner = kOwner;
    f.bag = bag;
    f.position = pos;
    if (guid)
    {
        PB59::InventoryInstanceFacts i;
        i.guid = *guid;
        i.owner = kOwner;
        i.entry = std::uint32_t(1000 + (*guid & 0xFFF));
        i.count = 1;
        i.durability = 40;
        f.item = i;
    }
    return f;
}

// Valid batch of n moves: a mix of strips (equipment -> storage) and equips
// (storage -> equipment), all positions distinct, every destination empty.
std::vector<InventoryBatchMove> ValidBatch(std::mt19937& rng, std::size_t n)
{
    std::vector<std::uint16_t> equip, storage;
    for (std::uint8_t s = 0; s < 19; ++s)
        equip.push_back(Equip(s));
    for (std::uint8_t s = 0; s < 16; ++s)
        storage.push_back(Pack(s));
    for (std::uint8_t b = 19; b < 23; ++b)
        for (std::uint8_t s = 0; s < 16; ++s)
            storage.push_back(InBag(b, s));
    std::shuffle(equip.begin(), equip.end(), rng);
    std::shuffle(storage.begin(), storage.end(), rng);
    std::vector<InventoryBatchMove> out;
    std::size_t e = 0, st = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        bool strip = (rng() & 3) != 0 || e >= equip.size();
        std::uint16_t a = strip ? equip[e++] : storage[st++];
        std::uint16_t b = strip ? storage[st++] : equip[e++];
        auto bagOf = [](std::uint16_t p) -> std::uint64_t { return (p >> 8) == BAG0 ? 0 : 0xB000 + (p >> 8); };
        out.push_back({Slot(a, bagOf(a), 0x5000 + i), Slot(b, bagOf(b), std::nullopt)});
    }
    return out;
}

// Independent oracle: the SET of contract rules a batch violates (agreed point 1
// plus value sanity). The real validator must return None iff the set is empty,
// and otherwise one of its members.
std::set<Err> Violations(std::uint64_t owner, std::vector<InventoryBatchMove> const& m)
{
    std::set<Err> v;
    if (m.empty())
        return {Err::Empty};
    if (m.size() > 19)
        v.insert(Err::TooLarge);
    if (!owner)
        v.insert(Err::Owner);
    for (std::size_t i = 0; i < m.size(); ++i)
    {
        auto const& a = m[i];
        if (a.source.owner != owner || a.destination.owner != owner)
            v.insert(Err::Owner);
        bool srcOk = a.source.item && a.source.item->guid && a.source.item->owner == owner &&
            a.source.item->entry && a.source.item->count;
        if (!srcOk)
            v.insert(Err::Source);
        if (a.destination.item)
            v.insert(Err::Destination);
        if (a.source.position == a.destination.position)
            v.insert(Err::Overlap);
        for (std::size_t j = 0; j < m.size(); ++j)
        {
            if (i == j)
                continue;
            auto const& b = m[j];
            if (a.source.item && b.source.item && a.source.item->guid && a.source.item->guid == b.source.item->guid)
                v.insert(Err::DuplicateItem);
            std::uint16_t pa[2] = {a.source.position, a.destination.position};
            std::uint16_t pb[2] = {b.source.position, b.destination.position};
            for (auto x : pa)
                for (auto y : pb)
                    if (x == y)
                        v.insert(Err::Overlap);
        }
    }
    return v;
}

using Validator = std::function<Err(std::uint64_t, std::vector<InventoryBatchMove> const&)>;
Err Real(std::uint64_t owner, std::vector<InventoryBatchMove> const& m)
{
    return PB59::ValidateInventoryBatchPlan(owner, std::span<InventoryBatchMove const>(m.data(), m.size()));
}

enum class Defect { SrcOwner, DstOwner, NoItem, Guid0, ItemOwner, Entry0, Count0, DstOccupied, SelfOverlap,
    DupGuid, SameSource, SameDest, SrcIsOtherDst, DstIsOtherSrc, Count };
char const* Name(Defect d)
{
    static char const* n[] = {"source owner", "destination owner", "no source item", "guid 0", "item owner",
        "entry 0", "count 0", "destination occupied", "source == destination", "duplicate guid", "same source",
        "same destination", "source is another's destination", "destination is another's source"};
    return n[int(d)];
}
Err Intended(Defect d)
{
    switch (d)
    {
        case Defect::SrcOwner: case Defect::DstOwner: return Err::Owner;
        case Defect::NoItem: case Defect::Guid0: case Defect::ItemOwner: case Defect::Entry0: case Defect::Count0:
            return Err::Source;
        case Defect::DstOccupied: return Err::Destination;
        case Defect::DupGuid: return Err::DuplicateItem;
        default: return Err::Overlap;
    }
}
bool Apply(Defect d, std::vector<InventoryBatchMove>& m, std::size_t k, std::size_t o)
{
    auto& a = m[k];
    auto& b = m[o];
    bool pair = k != o;
    switch (d)
    {
        case Defect::SrcOwner: a.source.owner = kOwner + 1; return true;
        case Defect::DstOwner: a.destination.owner = kOwner + 1; return true;
        case Defect::NoItem: a.source.item.reset(); return true;
        case Defect::Guid0: a.source.item->guid = 0; return true;
        case Defect::ItemOwner: a.source.item->owner = kOwner + 1; return true;
        case Defect::Entry0: a.source.item->entry = 0; return true;
        case Defect::Count0: a.source.item->count = 0; return true;
        case Defect::DstOccupied: a.destination.item = a.source.item; a.destination.item->guid += 0x777; return true;
        case Defect::SelfOverlap: a.destination.position = a.source.position; return true;
        case Defect::DupGuid: if (!pair) return false; a.source.item->guid = b.source.item->guid; return true;
        case Defect::SameSource: if (!pair) return false; a.source.position = b.source.position; return true;
        case Defect::SameDest: if (!pair) return false; a.destination.position = b.destination.position; return true;
        case Defect::SrcIsOtherDst: if (!pair) return false; a.source.position = b.destination.position; return true;
        case Defect::DstIsOtherSrc: if (!pair) return false; a.destination.position = b.source.position; return true;
        default: return false;
    }
}

struct Corpus
{
    std::vector<std::pair<std::uint64_t, std::vector<InventoryBatchMove>>> batches;
};
Corpus Build()
{
    Corpus c;
    std::mt19937 rng(0xC502);
    for (std::size_t n = 1; n <= 19; ++n)
        for (int rep = 0; rep < 12; ++rep)
        {
            auto valid = ValidBatch(rng, n);
            c.batches.push_back({kOwner, valid});
            for (int d = 0; d < int(Defect::Count); ++d)
            {
                auto m = valid;
                std::size_t k = rng() % n, o = n > 1 ? (k + 1 + rng() % (n - 1)) % n : k;
                if (Apply(Defect(d), m, k, o))
                    c.batches.push_back({kOwner, m});
            }
        }
    c.batches.push_back({kOwner, {}});
    for (std::size_t n = 20; n <= 24; ++n)
    {
        auto m = ValidBatch(rng, 19);
        while (m.size() < n)
            m.push_back(m.back());
        c.batches.push_back({kOwner, m});
    }
    c.batches.push_back({0, ValidBatch(rng, 3)});
    // Fuzz: random multi-defect batches.
    for (int f = 0; f < 4000; ++f)
    {
        std::size_t n = 1 + rng() % 19;
        auto m = ValidBatch(rng, n);
        int defects = 1 + int(rng() % 3);
        for (int i = 0; i < defects; ++i)
        {
            std::size_t k = rng() % n, o = rng() % n;
            Defect d = Defect(rng() % int(Defect::Count));
            if (d == Defect::Guid0 || d == Defect::ItemOwner || d == Defect::Entry0 || d == Defect::Count0 ||
                d == Defect::DstOccupied || d == Defect::DupGuid)
                if (!m[k].source.item || (d == Defect::DupGuid && !m[o].source.item))
                    continue;
            Apply(d, m, k, o);
        }
        c.batches.push_back({kOwner, m});
    }
    return c;
}

// Returns number of disagreements between `v` and the oracle over the corpus.
int CrossCheck(Corpus const& c, Validator const& v, int* valid = nullptr, std::string* first = nullptr)
{
    int mismatches = 0, accepted = 0;
    for (auto const& [owner, m] : c.batches)
    {
        auto want = Violations(owner, m);
        Err got = v(owner, m);
        bool ok = want.empty() ? got == Err::None : (got != Err::None && want.count(got));
        accepted += got == Err::None;
        if (!ok)
        {
            if (!mismatches && first)
                *first = "n=" + std::to_string(m.size()) + " got " + Name(got) + " oracle " +
                    (want.empty() ? "valid" : Name(*want.begin()));
            ++mismatches;
        }
    }
    if (valid)
        *valid = accepted;
    return mismatches;
}

// ------------------------------------------------------------------ L: ledger properties
enum class Op { OpenScope, FinishFull, FinishPartial, DropScope, Request, OpenExternal, CloseExternal, CompleteDeferred,
    SealOwner, Count };
constexpr Ledger::Work kWorks[] = {Ledger::Work::StandaloneSave, Ledger::Work::CallerOwnedSave,
    Ledger::Work::InventoryAndGold, Ledger::Work::GoldOnly, Ledger::Work::InventoryMutation, Ledger::Work::Logout,
    Ledger::Work::Redirect, Ledger::Work::Destroy, Ledger::Work::ScheduledSave, Ledger::Work::Callback};
bool Economic(Ledger::Work w)
{
    return w == Ledger::Work::CallerOwnedSave || w == Ledger::Work::InventoryAndGold || w == Ledger::Work::GoldOnly ||
        w == Ledger::Work::InventoryMutation;
}

// Reader seam so sabotage can lie about what the ledger says.
struct Reader
{
    std::function<bool(Ledger const&)> canBegin = [](Ledger const& l) { return l.CanBegin(); };
    std::function<bool(Ledger const&)> pendingSave = [](Ledger const& l) { return l.PendingSave(); };
    std::function<bool(Ledger const&)> pendingLogout = [](Ledger const& l) { return l.PendingLogout(); };
    std::function<bool(Ledger::MutationScope const&, Ledger const&, Ledger::Access, int depth)> canAccess =
        [](Ledger::MutationScope const& s, Ledger const& l, Ledger::Access a, int) {
            return s.CanAccess(l.OwnerIncarnation(), a);
        };
    std::function<Ledger::Decision(Ledger&, Ledger::Work)> request = [](Ledger& l, Ledger::Work w) {
        return l.Request(w);
    };
};

struct LedgerRun
{
    int states = 0, violations = 0, liveSentinelWithPending = 0, recoveryStates = 0;
    std::string first;
};

LedgerRun RunLedger(Reader const& rd, unsigned seeds, unsigned steps)
{
    LedgerRun out;
    auto fail = [&](std::string const& why) {
        if (!out.violations++)
            out.first = why;
    };
    for (unsigned seed = 1; seed <= seeds; ++seed)
    {
        std::mt19937 rng(seed * 7919u);
        auto ledger = std::make_unique<Ledger>();
        std::unique_ptr<Ledger::MutationScope> scope;
        std::vector<std::pair<std::unique_ptr<Ledger::ExternalScope>, Ledger::Work>> externals;
        int depth = 0; // admitted externals currently open (independent count)
        bool sawRecovery = false;
        for (unsigned step = 0; step < steps; ++step)
        {
            bool const pS0 = ledger->PendingSave(), pL0 = ledger->PendingLogout();
            Op op = Op(rng() % int(Op::Count));
            bool sealOk = false, completeOk = false;
            switch (op)
            {
                case Op::OpenScope:
                    if (!scope)
                        scope = std::make_unique<Ledger::MutationScope>(*ledger);
                    break;
                case Op::FinishFull:
                    if (scope && scope->Finish({true, true, true, true}))
                        scope.reset();
                    break;
                case Op::FinishPartial:
                    if (scope)
                        CHECK(!scope->Finish({true, true, rng() % 2 == 0, false}), "partial StableProof must not finish");
                    break;
                case Op::DropScope: scope.reset(); break;
                case Op::Request:
                {
                    Ledger::Work w = kWorks[rng() % std::size(kWorks)];
                    bool const idle = ledger->State() == Ledger::Phase::Idle;
                    bool const sealed = ledger->OwnerSealed();
                    Ledger::Decision d = rd.request(*ledger, w);
                    if (Economic(w) && d == Ledger::Decision::Admit && (pS0 || pL0 || depth || !idle || sealed))
                        fail("I5 economic work admitted with pending intent/external depth/active/sealed");
                    if (Economic(w) && d == Ledger::Decision::RejectBeforeMutation && idle && !depth && !sealed &&
                        !rd.pendingSave(*ledger) && !rd.pendingLogout(*ledger))
                        fail("I5' idle economic rejection without a visible pending intent");
                    if (w == Ledger::Work::Callback && d == Ledger::Decision::Admit && (!idle || pS0 || pL0 || sealed))
                        fail("I9 callback admitted while active/pending/sealed");
                    if (sealed && w == Ledger::Work::InventoryMutation && d == Ledger::Decision::Admit)
                        fail("I7 inventory mutation admitted after seal");
                    break;
                }
                case Op::OpenExternal:
                    if (externals.size() < 3)
                    {
                        Ledger::Work w = kWorks[rng() % std::size(kWorks)];
                        auto e = std::make_unique<Ledger::ExternalScope>(*ledger, w);
                        depth += e->Admitted();
                        externals.push_back({std::move(e), w});
                    }
                    break;
                case Op::CloseExternal:
                    if (!externals.empty())
                    {
                        depth -= externals.back().first->Admitted();
                        externals.pop_back();
                    }
                    break;
                case Op::CompleteDeferred:
                    if (!externals.empty())
                        completeOk = externals.back().first->CompleteDeferred();
                    break;
                case Op::SealOwner:
                    if (!externals.empty())
                        sealOk = externals.back().first->SealOwner();
                    break;
                default: break;
            }
            ++out.states;
            bool const pS = rd.pendingSave(*ledger), pL = rd.pendingLogout(*ledger);
            bool const realPS = ledger->PendingSave(), realPL = ledger->PendingLogout();
            if (pS0 && !realPS && !completeOk)
                fail("I4 pending save cleared without CompleteDeferred");
            if (pL0 && !realPL && !sealOk)
                fail("I3 pending logout cleared without SealOwner");
            if (rd.canBegin(*ledger) && (ledger->State() != Ledger::Phase::Idle || pS || pL || ledger->OwnerSealed() || depth))
                fail("I1 CanBegin while not idle / pending / sealed / external depth");
            if (sawRecovery && ledger->State() != Ledger::Phase::RecoveryRequired)
                fail("I6 RecoveryRequired left without explicit reconciliation");
            if (ledger->State() == Ledger::Phase::RecoveryRequired)
            {
                sawRecovery = true;
                ++out.recoveryStates;
                if (rd.canBegin(*ledger))
                    fail("I6 CanBegin in RecoveryRequired");
            }
            if (scope)
            {
                bool fwd = rd.canAccess(*scope, *ledger, Ledger::Access::Forward, depth);
                if (fwd && (ledger->State() != Ledger::Phase::Active || !scope->GetTicket() || depth || ledger->OwnerSealed()))
                    fail("I2 CanAccess(Forward) outside an active, undisturbed ticket");
                if (fwd && (realPS || realPL))
                    ++out.liveSentinelWithPending; // CS01 boundary: reachable, and NOT write permission
            }
        }
        // Teardown order: externals first, then the mutation scope, then the ledger.
        externals.clear();
        scope.reset();
    }
    return out;
}

// ------------------------------------------------------------------ C: synthetic harness scenarios
constexpr std::uint8_t kEquipped[] = {0, 2, 4, 6, 9, 15, 16, 17};
std::unique_ptr<Synthetic::Core> Fixture()
{
    auto c = std::make_unique<Synthetic::Core>();
    for (std::uint8_t s : kEquipped)
        c->Create(100 + s, 0x1000 + s, Pos{BAG0, s});
    c->CreateBag(0xB19, 19, 4);
    return c;
}
std::vector<Pos> FreeStorage(Synthetic::Core const& c)
{
    std::vector<Pos> out;
    for (Pos p : c.StoragePositions())
        if (!c.GetItemByPos(p))
            out.push_back(p);
    return out;
}
Request Strip(Synthetic::Core const& c, std::vector<std::uint8_t> slots)
{
    Request r{c.ownerGuid, c.incarnation, c.custodyStamp, {}};
    auto free = FreeStorage(c);
    for (std::size_t i = 0; i < slots.size(); ++i)
        r.moves.push_back(Move{c.GetItemByPos(BAG0, slots[i])->guid, Pos{BAG0, slots[i]}, free[i]});
    return r;
}

// SYNTHETIC executor gated by the REAL ledger. Stand-in for the future native
// four-outcome executor (NOT Codex's check-only bool; that is never used as
// write permission here). Admission = ledger terms of CheckPB59InventoryBatchPlan
// + REAL ValidateInventoryBatchPlan over model facts; session/world/combat/
// teleport/stamp/capture terms are later native gates.
struct LedgerGatedExecutor
{
    Ledger& ledger;
    bool canAccessOnly = false;   // sabotage: treats CanAccess(Forward) as permission
    bool skipAdmission = false;   // sabotage: ignores CanBegin/pending at admission and a zero ticket
    bool trustInsertReturn = false; // sabotage: success = insertion returned the same pointer
    bool finished = false, recovery = false;

    Report Execute(Synthetic::Core& core, Request const& r)
    {
        if (!skipAdmission && (!ledger.CanBegin() || ledger.PendingSave() || ledger.PendingLogout()))
            return Report{Outcome::Refused, "ledger admission refused", {}};
        std::vector<InventoryBatchMove> plan;
        for (auto const& m : r.moves)
            plan.push_back({Facts(core, m.from, core.GetItemByPos(m.from)), Facts(core, m.to, core.GetItemByPos(m.to))});
        if (Real(core.ownerGuid, plan) != Err::None)
            return Report{Outcome::Refused, "structural plan refused", {}};
        Ledger::MutationScope scope(ledger);
        if (!scope.GetTicket() && !skipAdmission)
            return Report{Outcome::Refused, "no ticket", {}};
        std::uint64_t const inc = ledger.OwnerIncarnation();
        Knobs k;
        k.trustInsertReturn = trustInsertReturn;
        k.forwardPermit = [&] {
            if (skipAdmission && !scope.GetTicket())
                return true;
            bool const access = scope.CanAccess(inc, Ledger::Access::Forward);
            return canAccessOnly ? access : access && !ledger.PendingSave() && !ledger.PendingLogout();
        };
        ReferenceExecutor ref(k);
        Report rep = ref.Execute(core, r);
        if (rep.outcome != Outcome::RecoveryRequired)
            finished = scope.GetTicket() && scope.Finish({true, true, true, true});
        else
            recovery = true; // scope unwinds without Finish -> ledger RecoveryRequired (quarantine)
        return rep;
    }
};

struct CRun
{
    Report rep;
    Verdict verdict;
    std::unique_ptr<Synthetic::Core> core;
    std::unique_ptr<Ledger> ledger;
    bool accessDuringPending = false;
    int useAfterUnlink = 0;
};

template<class Setup>
CRun RunC(Setup setup, bool canAccessOnly = false, bool skipAdmission = false, bool trustInsert = false)
{
    CRun run;
    run.core = Fixture();
    run.ledger = std::make_unique<Ledger>();
    Request r = setup(run);
    Snapshot s0 = Capture(*run.core);
    run.core->ledger.clear();
    {
        LedgerGatedExecutor ex{*run.ledger, canAccessOnly, skipAdmission, trustInsert};
        run.rep = ex.Execute(*run.core, r);
    }
    run.verdict = Judge(*run.core, s0, r, run.rep);
    for (auto const& e : run.core->ledger)
        run.useAfterUnlink += e.kind == Synthetic::EventKind::UseAfterUnlink;
    return run;
}
}

int main()
{
    std::printf("CS02 Core74 cross-check -- REAL Core74 headers for the batch plan and ledger; everything else SYNTHETIC\n");

    // ---------------- A
    Corpus corpus = Build();
    int accepted = 0;
    std::string first;
    int mism = CrossCheck(corpus, Real, &accepted, &first);
    CHECK(mism == 0, "real ValidateInventoryBatchPlan disagrees with the oracle: " + first);
    int defectsRejected = 0, defectCases = 0;
    {
        std::mt19937 rng(0xD3F3C7);
        for (std::size_t n = 1; n <= 19; ++n)
            for (int d = 0; d < int(Defect::Count); ++d)
                for (int rep = 0; rep < 6; ++rep)
                {
                    auto m = ValidBatch(rng, n);
                    std::size_t k = rng() % n, o = n > 1 ? (k + 1 + rng() % (n - 1)) % n : k;
                    if (!Apply(Defect(d), m, k, o))
                        continue;
                    ++defectCases;
                    Err got = Real(kOwner, m);
                    bool ok = got == Intended(Defect(d)) || (got != Err::None && Violations(kOwner, m).count(got));
                    defectsRejected += ok;
                    CHECK(ok, std::string("single defect '") + Name(Defect(d)) + "' n=" + std::to_string(n) +
                        " -> " + Name(got));
                }
    }
    std::printf("[REAL HEADER] A: %zu oracle-generated batches, %d accepted, %d disagreements; "
                "single-defect batches rejected %d/%d\n",
        corpus.batches.size(), accepted, mism, defectsRejected, defectCases);

    // ---------------- G (informational: structurally accepted, must be refused by later native gates)
    {
                struct Gap
        {
            char const* gate;
            std::function<void(std::vector<InventoryBatchMove>&)> mutate;
        };
        std::vector<Gap> gaps = {
            {"N1 destination container identity (bag GUID replaced)", [](auto& m) { m[0].destination.bag = 0xDEAD; }},
            {"N2 destination position invalid (0)", [](auto& m) { m[0].destination.position = 0; }},
            {"N2 destination position invalid (beyond bag size)", [](auto& m) { m[0].destination.position = InBag(19, 40); }},
            {"N2 destination position invalid (bag 0xFF slot 0xFF)", [](auto& m) { m[0].destination.position = 0xFFFF; }},
            {"N3 source claims a container", [](auto& m) { m[0].source.bag = 0xB013; }},
            {"N4 direction storage->storage", [](auto& m) { m[0].source.position = Pack(15); m[0].destination.position = InBag(22, 15); }},
            {"N4 direction equipment->equipment", [](auto& m) { m[0].source.position = Equip(1); m[0].destination.position = Equip(3); }},
            {"N5 stale facts (item moved since capture)", [](auto& m) { m[0].source.item->durability = 1; }},
            {"N6 two-hander before off-hand move (agreed point 4; executor gate)", [](auto& m) {
                m[0].source.position = Pack(14); m[0].destination.position = Equip(15);
                m[1].source.position = Equip(16); m[1].destination.position = Pack(13); }},
            {"N7 owner/session incarnation, stamp, world/combat/teleport (context)", [](auto&) {}},
        };
        int pinned = 0;
        for (auto const& g : gaps)
        {
            // Fixed valid base batch; each gap mutates move 0 or 1 only, using positions
            // no other move touches, so the only difference is the gap itself.
            std::vector<InventoryBatchMove> m = {
                {Slot(Equip(0), 0, 0x6000), Slot(Pack(0), 0, std::nullopt)},
                {Slot(Equip(2), 0, 0x6001), Slot(Pack(1), 0, std::nullopt)},
                {Slot(Equip(4), 0, 0x6002), Slot(InBag(19, 0), 0xB013, std::nullopt)}};
            g.mutate(m);
            Err got = Real(kOwner, m);
            pinned += got == Err::None;
            std::printf("[LATER NATIVE GATE] %-70s structural header: %s\n", g.gate,
                got == Err::None ? "accepts (gap: must be refused natively)" : Name(got));
        }
        std::printf("[LATER NATIVE GATE] %d/%zu gap batches are structurally accepted (informational, not failures)\n",
            pinned, gaps.size());
    }

    // ---------------- L
    Reader real;
    LedgerRun lr = RunLedger(real, 400, 60);
    CHECK(lr.violations == 0, "ledger invariant violated: " + lr.first);
    CHECK(lr.liveSentinelWithPending > 0, "CS01 boundary state (live sentinel, active ticket, pending intent) reachable");
    std::printf("[REAL LEDGER] L: %d states over 400 random sequences, %d invariant violations, %d states with "
                "CanAccess(Forward)=true AND a pending intent, %d RecoveryRequired states\n",
        lr.states, lr.violations, lr.liveSentinelWithPending, lr.recoveryStates);

    // ---------------- C
    // C1/C2: CS01 #51 7131372670 -- callback registers a pending intent while the sentinel and ticket are live.
    for (auto work : {Ledger::Work::Logout, Ledger::Work::StandaloneSave})
    {
        char const* nm = work == Ledger::Work::Logout ? "C1 pending logout" : "C2 pending standalone save";
        bool sawAccess = false;
        Ledger::Decision decision{};
        auto setup = [&](CRun& run) {
            Ledger* L = run.ledger.get();
            run.core->onPlayerCanUnequipItem = [L, work, &sawAccess, &decision](Synthetic::Core& core, std::uint16_t, bool) {
                if (L->State() == Ledger::Phase::Active && !L->PendingLogout() && !L->PendingSave())
                {
                    decision = L->Request(work);
                    core.RecordPendingIntent();
                    sawAccess = L->State() == Ledger::Phase::Active;
                }
                return true;
            };
            return Strip(*run.core, {0, 2, 4});
        };
        CRun ok = RunC(setup);
        bool const deferred = decision == (work == Ledger::Work::Logout ? Ledger::Decision::DeferLogout
                                                                        : Ledger::Decision::DeferStandaloneSave);
        CHECK(deferred && sawAccess, std::string(nm) + ": real ledger defers the intent and stays Active");
        CHECK(ok.rep.outcome == Outcome::Refused || ok.rep.outcome == Outcome::RecoveryRequired,
            std::string(nm) + ": gated executor must not apply (got " + Name(ok.rep.outcome) + ")");
        CHECK(ok.verdict.ok(), std::string(nm) + ": oracle\n" + ok.verdict.Text());
        bool preserved = work == Ledger::Work::Logout ? ok.ledger->PendingLogout() : ok.ledger->PendingSave();
        CHECK(preserved, std::string(nm) + ": deferred intent preserved for native replay");
        CHECK(ok.ledger->State() == Ledger::Phase::Idle, std::string(nm) + ": no-effect refusal finishes the ticket");
        // Same scenario, CanAccess(Forward) treated as permission: must be flagged.
        decision = {};
        CRun bad = RunC(setup, /*canAccessOnly*/ true);
        CHECK(bad.rep.outcome == Outcome::Applied && !bad.verdict.ok(),
            std::string(nm) + ": CanAccess-only executor applies and the oracle flags it");
        std::printf("[SYNTHETIC] %-28s real decision=%s CanAccess(Forward) during pending=%s | gated: %-16s oracle %s | "
                    "CanAccess-only: %-8s oracle %s\n", nm, deferred ? "Defer" : "?", sawAccess ? "true" : "false",
            Name(ok.rep.outcome), ok.verdict.ok() ? "clean" : "VIOLATION", Name(bad.rep.outcome),
            bad.verdict.ok() ? "clean (MISSED)" : ("flagged: " + bad.verdict.violations.front()).c_str());
    }
    // C3: pending intent left from an earlier operation: refused at admission.
    {
        auto setup = [](CRun& run) {
            {
                Ledger::MutationScope s(*run.ledger);
                (void)run.ledger->Request(Ledger::Work::Logout); // deferred while active
                CHECK(s.Finish({true, true, true, true}), "C3 setup finishes earlier ticket");
            }
            return Strip(*run.core, {0, 2});
        };
        CRun ok = RunC(setup);
        CHECK(ok.ledger->PendingLogout() && ok.rep.outcome == Outcome::Refused && ok.verdict.ok(),
            "C3 earlier pending logout: refused at admission, no effects");
        CRun bad = RunC(setup, false, /*skipAdmission*/ true);
        CHECK(bad.rep.outcome == Outcome::Applied, "C3 sabotage: admission-skipping executor applies (expected Refused)");
        std::printf("[SYNTHETIC] C3 earlier pending logout        gated: %s | admission-skipping: %s (expected Refused -> caught)\n",
            Name(ok.rep.outcome), Name(bad.rep.outcome));
    }
    // C4/C5: in-call _ApplyItemMods boundary (synthetic analogue of #51 7131425394).
    auto equipSetup = [](auto callback) {
        return [callback](CRun& run) {
            Synthetic::Item* it = run.core->Create(790, 0x3100, FreeStorage(*run.core)[0]);
            Request r{run.core->ownerGuid, run.core->incarnation, run.core->custodyStamp, {}};
            r.moves.push_back(Move{it->guid, it->pos, Pos{BAG0, 1}}); // equip into empty slot 1
            Pos park = FreeStorage(*run.core)[0];
            r.moves.push_back(Move{0x1000, Pos{BAG0, 0}, park});
            Ledger* L = run.ledger.get();
            run.core->onApplyItemMods = [callback, L](Synthetic::Core& core, Synthetic::Item* item) { callback(core, *L, item); };
            return r;
        };
    };
    {
        auto logout = equipSetup([](Synthetic::Core& core, Ledger& L, Synthetic::Item*) {
            if (!L.PendingLogout())
            {
                (void)L.Request(Ledger::Work::Logout);
                core.RecordPendingIntent();
            }
        });
        CRun ok = RunC(logout);
        CHECK(ok.rep.outcome != Outcome::Applied && ok.verdict.ok(), "C4 in-call pending logout: gated executor stops\n" + ok.verdict.Text());
        CRun bad = RunC(logout, true);
        CHECK(bad.rep.outcome == Outcome::Applied && !bad.verdict.ok(), "C4 CanAccess-only executor continues and is flagged");
        std::printf("[SYNTHETIC] C4 in-call pending logout        gated: %-16s oracle %s | CanAccess-only: %s oracle %s\n",
            Name(ok.rep.outcome), ok.verdict.ok() ? "clean" : "VIOLATION", Name(bad.rep.outcome),
            bad.verdict.ok() ? "clean (MISSED)" : "flagged");
    }
    {
        bool accessAfter = false;
        auto unlink = equipSetup([&accessAfter](Synthetic::Core& core, Ledger& L, Synthetic::Item* item) {
            if (item->pos == Pos{BAG0, 1})
            {
                core.HookRelocate(item, FreeStorage(core).back()); // safe simulated unlink, no deletion
                accessAfter = L.State() == Ledger::Phase::Active; // ledger cannot see an in-call unlink
            }
        });
        CRun ok = RunC(unlink);
        CHECK(ok.useAfterUnlink == 3, "C5 three synthetic effect helpers ran after the in-call unlink");
        CHECK(accessAfter, "C5 ledger forward access unchanged by the in-call unlink");
        CHECK(ok.rep.outcome != Outcome::Applied && ok.verdict.ok(), "C5 gated executor re-reads identity\n" + ok.verdict.Text());
        CRun bad = RunC(unlink, false, false, /*trustInsertReturn*/ true);
        CHECK(bad.rep.outcome == Outcome::Applied && !bad.verdict.ok(), "C5 trust-returned-pointer executor applies and is flagged");
        std::printf("[SYNTHETIC] C5 in-call unlink (_ApplyItemMods analogue) helpers-after-unlink=%d ledger-forward=%s | "
                    "gated: %s oracle %s | trust-return: %s oracle %s\n", ok.useAfterUnlink, accessAfter ? "true" : "false",
            Name(ok.rep.outcome), ok.verdict.ok() ? "clean" : "VIOLATION", Name(bad.rep.outcome),
            bad.verdict.ok() ? "clean (MISSED)" : "flagged");
    }
    {
        CRun ok = RunC([](CRun& run) { return Strip(*run.core, {0, 2, 4}); });
        CHECK(ok.rep.outcome == Outcome::Applied && ok.verdict.ok() && ok.ledger->State() == Ledger::Phase::Idle &&
                !ok.ledger->PendingLogout() && !ok.ledger->PendingSave(),
            "C6 control: clean strip applies and finishes the ticket");
    }

    // ---------------- S
    int killed = 0, total = 0;
    auto expectCaught = [&](char const* nm, bool caught) {
        ++total;
        killed += caught;
        CHECK(caught, std::string("sabotage not caught: ") + nm);
        std::printf("[SABOTAGE] %-62s %s\n", nm, caught ? "caught" : "MISSED");
    };
    for (Err e : {Err::Empty, Err::TooLarge, Err::Owner, Err::Source, Err::Destination, Err::DuplicateItem, Err::Overlap})
    {
        Validator suppressed = [e](std::uint64_t o, std::vector<InventoryBatchMove> const& m) {
            Err r = Real(o, m);
            return r == e ? Err::None : r;
        };
        expectCaught((std::string("validator suppresses ") + Name(e)).c_str(), CrossCheck(corpus, suppressed) > 0);
    }
    expectCaught("validator accepts everything",
        CrossCheck(corpus, [](std::uint64_t, std::vector<InventoryBatchMove> const&) { return Err::None; }) > 0);
    {
        Reader r1 = real;
        r1.canBegin = [](Ledger const& l) {
            return l.State() == Ledger::Phase::Idle && !l.OwnerSealed();
        };
        expectCaught("ledger reader: CanBegin ignores pending intents/external depth", RunLedger(r1, 400, 60).violations > 0);
        // Equivalence probe (NOT counted): CanAccess's ExternalDepth()/_ownerSealed terms.
        Reader r2 = real;
        r2.canAccess = [](Ledger::MutationScope const& s, Ledger const& l, Ledger::Access a, int) {
            return s.GetTicket() && (a == Ledger::Access::Forward ? l.State() == Ledger::Phase::Active
                                                                  : l.State() == Ledger::Phase::RecoveryRequired);
        };
        {
            int v = RunLedger(r2, 400, 60).violations;
            std::printf("[EQUIVALENT] ledger reader: CanAccess ignores external depth/seal -> %s\n",
                v ? "caught" : "not distinguishable: an active ticket with external depth or seal is unreachable through the "
                               "sequential public API (externals admit only while Idle; Begin needs depth 0). Defence-in-depth term; "
                               "reachability is a native/re-entrant later gate");
        }
        Reader r3 = real;
        r3.pendingLogout = [](Ledger const&) { return false; };
        expectCaught("ledger reader: pending logout hidden", RunLedger(r3, 400, 60).violations > 0);
        Reader r4 = real;
        r4.request = [](Ledger& l, Ledger::Work w) {
            auto d = l.Request(w);
            return w == Ledger::Work::InventoryMutation && d == Ledger::Decision::RejectBeforeMutation ? Ledger::Decision::Admit : d;
        };
        expectCaught("ledger reader: inventory mutation rejection turned into Admit", RunLedger(r4, 400, 60).violations > 0);
    }
    std::printf("[SABOTAGE] %d/%d caught (plus executor sabotage in C1-C5)\n", killed, total);

    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
