// CS02 / PB59 AutoGear move-rollback harness -- independent oracle.
//
// The oracle never trusts the executor's report. It captures value facts
// before and after (using the REAL Core53 header type PB59::InventorySlotFacts
// and its SameInventoryInstance / ValidInventorySlot predicates), checks link
// and effect coherence on the synthetic core, and decides which outcome is
// truthful. A synthetic pass is "the classification is consistent with the
// model", never "native success".
#pragma once

#include "MoveRollbackContract.h"
#include "PB59InventorySlotFacts.h" // REAL Core53 header (src/server/game/Entities/Player)

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace CS02MoveRollback
{
using Synthetic::Actor;
using Synthetic::Core;
using Synthetic::EventKind;
using Synthetic::Item;

struct Snapshot
{
    std::map<Pos, PB59::InventorySlotFacts> slots; // occupied positions only
    std::map<std::uint64_t, Pos> where;            // guid -> position
    // Owner context (agreed point 2: a callback that changes context is not "no effects").
    std::uint64_t incarnation = 0, custodyStamp = 0;
    bool loggingOut = false, retiring = false;
    bool operator==(Snapshot const& o) const
    {
        if (where != o.where || slots.size() != o.slots.size() || incarnation != o.incarnation ||
            custodyStamp != o.custodyStamp || loggingOut != o.loggingOut || retiring != o.retiring)
            return false;
        for (auto const& [p, f] : slots)
        {
            auto it = o.slots.find(p);
            if (it == o.slots.end() || it->second.bag != f.bag || !PB59::SameInventoryInstance(f, it->second))
                return false;
        }
        return true;
    }
};

inline PB59::InventorySlotFacts Facts(Core const& core, Pos p, Item const* it)
{
    PB59::InventorySlotFacts f;
    f.owner = core.ownerGuid;
    f.bag = core.BagGuid(p);
    f.position = p.Packed();
    if (it)
    {
        PB59::InventoryInstanceFacts i;
        i.guid = it->guid;
        i.owner = it->owner;
        i.entry = it->entry;
        i.count = it->count;
        i.durability = it->durability;
        i.enchants[0].id = it->enchantId;
        f.item = i;
    }
    return f;
}

inline Snapshot Capture(Core const& core)
{
    Snapshot s;
    s.incarnation = core.incarnation;
    s.custodyStamp = core.custodyStamp;
    s.loggingOut = core.loggingOut;
    s.retiring = core.retiring;
    for (Pos p : core.AllPositions())
        if (Item* it = core.GetItemByPos(p))
        {
            s.slots[p] = Facts(core, p, it);
            if (!it->tombstone)
                s.where[it->guid] = p;
        }
    return s;
}

// Expected post-state if every move applies exactly.
inline Snapshot Expected(Core const& core, Snapshot const& s0, Request const& r)
{
    Snapshot e = s0;
    std::vector<PB59::InventorySlotFacts> lifted;
    for (auto const& m : r.moves)
    {
        auto it = e.slots.find(m.from);
        if (it == e.slots.end())
            return s0;
        lifted.push_back(it->second);
        e.slots.erase(it);
        e.where.erase(m.itemGuid);
    }
    for (std::size_t i = 0; i < r.moves.size(); ++i)
    {
        auto f = lifted[i];
        f.bag = core.BagGuid(r.moves[i].to);
        f.position = r.moves[i].to.Packed();
        e.slots[r.moves[i].to] = f;
        e.where[r.moves[i].itemGuid] = r.moves[i].to;
    }
    return e;
}

struct Verdict
{
    std::vector<std::string> violations;
    Outcome truth = Outcome::Refused; // the class the oracle derives from state
    bool ok() const { return violations.empty(); }
    std::string Text() const
    {
        std::ostringstream o;
        for (auto const& v : violations)
            o << "    - " << v << "\n";
        return o.str();
    }
};

// Link/effect coherence of the synthetic core (the native analogue is the
// queue/map/visible/effect reconciliation the checklist's A05 asks for).
inline void Coherence(Core const& core, std::vector<std::string>& v)
{
    std::map<std::uint64_t, int> links;
    for (Pos p : core.AllPositions())
    {
        Item* it = core.GetItemByPos(p);
        if (p.bag == Synthetic::BAG0 && core.slotField[p.slot] != (it ? it->guid : 0))
            v.push_back("slot field mismatch at slot " + std::to_string(p.slot));
        if (p.IsEquipment())
        {
            int want = it ? 1 : 0;
            if (core.appliedMods[p.slot] != want)
                v.push_back("item mods applied " + std::to_string(core.appliedMods[p.slot]) +
                    " times at equipment slot " + std::to_string(p.slot));
            if (core.visible[p.slot] != (it ? it->guid : 0))
                v.push_back("visible equipment mismatch at slot " + std::to_string(p.slot));
        }
        if (!it)
            continue;
        if (it->tombstone)
        {
            v.push_back("dangling link to destroyed item guid " + std::to_string(it->guid));
            continue;
        }
        ++links[it->guid];
        if (it->pos != p)
            v.push_back("item guid " + std::to_string(it->guid) + " linked at a position it does not record");
        // REAL Core53 predicate over the copied facts.
        if (!PB59::ValidInventorySlot(Facts(core, p, it), it->guid, core.ValidPosition(p), it->pos == p))
            v.push_back("PB59::ValidInventorySlot rejects guid " + std::to_string(it->guid));
        if (core.durations.count(it->guid) != 1)
            v.push_back("duration registrations for guid " + std::to_string(it->guid) + " = " +
                std::to_string(core.durations.count(it->guid)));
    }
    for (auto const& [g, n] : links)
        if (n > 1)
            v.push_back("guid " + std::to_string(g) + " linked " + std::to_string(n) + " times (clone/duplicate)");
}

inline int ExecutorMutations(Core const& core)
{
    int n = 0;
    for (auto const& e : core.ledger)
        if (e.actor == Actor::Executor &&
            (e.kind == EventKind::Remove || e.kind == EventKind::Insert || e.kind == EventKind::Merge ||
             e.kind == EventKind::Destroy || e.kind == EventKind::Clone))
            ++n;
    return n;
}

// checkGuidList=false for adapters whose result type carries no GUID list
// (e.g. PB50's StripResult); the class is still judged from state.
inline Verdict Judge(Core const& core, Snapshot const& s0, Request const& r, Report const& rep,
    bool checkGuidList = true)
{
    Verdict v;
    Snapshot s1 = Capture(core);
    Snapshot p = Expected(core, s0, r);
    std::vector<std::string> incoherent;
    Coherence(core, incoherent);

    // Identity loss: an original that existed in s0 is gone, merged, cloned or orphaned.
    std::set<std::uint64_t> lost;
    for (auto const& [g, pos] : s0.where)
        if (!s1.where.count(g))
            lost.insert(g);
    bool mailed = !core.mailed.empty();
    for (auto const& e : core.ledger)
    {
        if (e.kind == EventKind::StaleUse && e.actor == Actor::Executor)
            v.violations.push_back("executor used a stale Item* (guid " + std::to_string(e.guid) +
                ") after a callback or deletion");
        if (e.kind == EventKind::MutationWhileClosed)
            v.violations.push_back("executor mutated inventory after logout/retirement began (guid " +
                std::to_string(e.guid) + ")");
    }
    // CS01 #51 7131372670: a pending logout/save intent is not permission to
    // continue, even while the scope's CanAccess(Forward) is true. Any forward
    // (phase 1) executor write after a recorded pending intent is a violation.
    {
        bool pending = false;
        for (auto const& e : core.ledger)
        {
            if (e.kind == EventKind::PendingIntent)
                pending = true;
            else if (pending && e.actor == Actor::Executor && e.phase == 1 &&
                (e.kind == EventKind::Remove || e.kind == EventKind::Insert))
            {
                v.violations.push_back("executor kept writing forward after a pending logout/save intent (guid " +
                    std::to_string(e.guid) + ")");
                break;
            }
        }
    }
    int mutations = ExecutorMutations(core);
    bool restoredState = s1 == s0 && incoherent.empty();
    bool appliedState = s1 == p && incoherent.empty() && !mailed;

    // Agreed contract (#52 7130720231 / #51 7127659287), point 2: Refused only
    // when the original state (inventory AND owner context) is proven unchanged.
    // A callback-made change with no executor effect is RecoveryRequired.
    if (appliedState && mutations > 0 && !(s0 == p))
        v.truth = Outcome::Applied;
    else if (mutations == 0 && s1 == s0)
        v.truth = Outcome::Refused; // pre-existing incoherence is not the executor's effect
    else if (restoredState && mutations > 0)
        v.truth = Outcome::Restored;
    else
        v.truth = Outcome::RecoveryRequired;

    if (rep.outcome != v.truth)
        v.violations.push_back(std::string("reported ") + Name(rep.outcome) + " but state proves " +
            Name(v.truth) + (rep.reason.empty() ? "" : " (reason: " + rep.reason + ")"));
    if (rep.outcome != Outcome::Refused || mutations > 0)
        for (auto const& i : incoherent)
            v.violations.push_back("incoherent: " + i);
    if (mailed)
        v.violations.push_back("an item left the inventory by mail (AutoUnequipOffhandIfNeed full-bag path)");
    if (!lost.empty() && rep.outcome != Outcome::RecoveryRequired && mutations > 0)
        v.violations.push_back("original identity lost (" + std::to_string(lost.size()) +
            " guid(s)) without RecoveryRequired");

    if (checkGuidList && rep.outcome == Outcome::RecoveryRequired && v.truth == Outcome::RecoveryRequired)
    {
        std::set<std::uint64_t> want;
        for (auto const& [g, pos] : s0.where)
        {
            auto it = s1.where.find(g);
            if (it == s1.where.end() || it->second != pos)
                want.insert(g);
        }
        std::set<std::uint64_t> got(rep.notWhereExpected.begin(), rep.notWhereExpected.end());
        if (got != want)
            v.violations.push_back("RecoveryRequired list has " + std::to_string(got.size()) +
                " guid(s); state shows " + std::to_string(want.size()) + " not at their original position");
    }

    // Reverse order: rollback removals follow the reverse of completed forward insertions.
    if (rep.outcome == Outcome::Restored || rep.outcome == Outcome::RecoveryRequired)
    {
        std::vector<std::uint64_t> fwd, rev;
        for (auto const& e : core.ledger)
        {
            if (e.actor != Actor::Executor)
                continue;
            if (e.phase == 1 && e.kind == EventKind::Insert && s0.where.count(e.guid))
                fwd.push_back(e.guid);
            if (e.phase == 2 && e.kind == EventKind::Remove)
                rev.push_back(e.guid);
        }
        // Only the executor's own completed moves have an order to honour; an
        // executor that also (verifiably) undoes a callback's relocation is
        // judged by the final state, not by this order check.
        {
            std::vector<std::uint64_t> own;
            for (auto g : rev)
                if (std::find(fwd.begin(), fwd.end(), g) != fwd.end())
                    own.push_back(g);
            rev.swap(own);
        }
        std::vector<std::uint64_t> want;
        for (auto it = fwd.rbegin(); it != fwd.rend(); ++it)
            if (std::find(rev.begin(), rev.end(), *it) != rev.end())
                want.push_back(*it);
        if (rev != want)
            v.violations.push_back("rollback order is not the exact reverse of the completed forward moves");
    }
    return v;
}
}
