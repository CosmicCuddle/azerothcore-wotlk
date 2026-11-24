// CS02 / PB59 AutoGear move-rollback harness -- executors run by the oracle.
//
//  * ReferenceExecutor: CS02's own independent, SYNTHETIC implementation of
//    the proposed contract. It exists to prove the scenario matrix is
//    satisfiable and to give every mutant a baseline. It is NOT a proposed
//    Core implementation and must not be copied into native code (Codex owns
//    the Core interface, Claude owns the PB caller).
//  * Knobs: one-defect mutants of the reference (negative controls).
//  * Pb44StripExecutor: a line-for-line transcription of PB44 1a9e4801
//    TrainerAction.cpp:692-704 MoveEquippedToBags onto the synthetic core,
//    used to reproduce CS01's cited hazard as a scenario (expected red).
#pragma once

#include "ExactStorageGate.h" // REAL Core53 header (src/server/game/Entities/Player)
#include "MoveRollbackOracle.h"

#include <map>
#include <optional>
#include <set>

namespace CS02MoveRollback
{
using Synthetic::InventoryResult;

struct Knobs
{
    bool noRollback = false;            // reports Restored without undoing anything
    bool forwardOrderRollback = false;  // undoes 0..k-1 instead of k-1..0
    bool trustRollback = false;         // never verifies a restore
    bool allowMerge = false;            // native "anywhere" store (merge allowed), no empty-destination check
    bool skipDuplicateDestination = false;
    bool skipCustody = false;           // ignores incarnation/stamp/logout/retirement
    bool skipItemOwner = false;
    bool skipRevalidateAfterHook = false;
    bool refusedAfterEffects = false;   // labels every failure Refused
    bool partialRecoveryList = false;   // lists only the first misplaced guid
    bool skipTwoHandCheck = false;
    bool skipReentryFence = false;
    bool holdPointerAcrossHook = false; // the PB44 pattern inside an otherwise correct executor
    bool trustSourceEmpty = false;      // success = "source slot is empty"
    bool skipExactGate = false;         // does not consult PB59::AcceptExactStorage
};

class ReferenceExecutor
{
public:
    explicit ReferenceExecutor(Knobs k = {}) : _k(k) {}

    Report Execute(Synthetic::Core& core, Request const& r)
    {
        if (core.transactionDepth > 0 && !_k.skipReentryFence)
            return Refuse(core, "re-entered while a transaction is open");
        struct Depth
        {
            Synthetic::Core& c;
            explicit Depth(Synthetic::Core& core) : c(core) { ++c.transactionDepth; }
            ~Depth() { --c.transactionDepth; }
        } depth(core);
        core.phase = 0;
        if (std::string why = Custody(core, r); !why.empty())
            return Refuse(core, why);
        if (r.moves.empty())
            return Refuse(core, "empty plan");

        Snapshot const s0 = Capture(core);
        std::set<std::uint64_t> guids;
        std::set<Pos> froms, tos;
        std::vector<PB59::ExactStorageReservation> reservations;
        std::vector<Item*> held;
        for (auto const& m : r.moves)
        {
            Item* it = core.GetItemByPos(m.from);
            if (!it || it->guid != m.itemGuid)
                return Refuse(core, "item is not at its planned source");
            if (!_k.skipItemOwner && it->owner != core.ownerGuid)
                return Refuse(core, "item is owned by someone else");
            if (it->inTrade || it->lootGenerated || it->isBag)
                return Refuse(core, "item is not transferable");
            if (m.to.slot == Synthetic::NULL_SLOT || !core.ValidPosition(m.to))
                return Refuse(core, "destination is not an exact valid position");
            if (!guids.insert(m.itemGuid).second || !froms.insert(m.from).second)
                return Refuse(core, "same item or source twice in one plan");
            if (!tos.insert(m.to).second && !_k.skipDuplicateDestination)
                return Refuse(core, "two moves to one destination");
            if (!_k.allowMerge && core.GetItemByPos(m.to))
                return Refuse(core, "destination is occupied");
            if (m.to.bag == Synthetic::BAG0 && m.to.slot >= Synthetic::BAG_START && m.to.slot < Synthetic::BAG_END)
                return Refuse(core, "bag slots are not move destinations");
            reservations.push_back({m.itemGuid, core.BagGuid(m.to), m.to.Packed(), it->count});
            held.push_back(it);
        }
        for (auto const& m : r.moves)
            if (froms.count(m.to))
                return Refuse(core, "destination is another move's source");
        if (!_k.skipTwoHandCheck)
            if (std::string why = TwoHand(core, r); !why.empty())
                return Refuse(core, why);
        for (std::size_t i = 0; i < r.moves.size(); ++i)
        {
            auto const& m = r.moves[i];
            if (m.to.IsEquipment())
            {
                if (core.CanEquipItem(m.to.slot, held[i]) != InventoryResult::Ok)
                    return Refuse(core, "CanEquipItem refuses the exact destination");
            }
            else
            {
                std::vector<Pos> dest;
                if (core.CanStoreItem(m.to.bag, m.to.slot, dest, held[i]) != InventoryResult::Ok)
                    return Refuse(core, "CanStoreItem refuses the exact destination");
            }
        }
        // Callbacks last; they may change anything, so revalidate afterwards.
        for (auto const& m : r.moves)
            if (m.from.IsEquipment())
                if (core.CanUnequipItem(m.from.Packed(), false) != InventoryResult::Ok)
                    return Refuse(core, "CanUnequipItem (incl. OnPlayerCanUnequipItem) refused");
        if (!_k.skipRevalidateAfterHook)
        {
            if (std::string why = Custody(core, r); !why.empty())
                return Refuse(core, "after callback: " + why);
            if (!(Capture(core) == s0))
                return Refuse(core, "inventory changed during a callback");
        }

        // ---- apply ----
        core.phase = 1;
        std::size_t done = 0;
        bool touched = false; // did any step change state (own bookkeeping, not the ledger)
        std::string failure;
        for (; done < r.moves.size(); ++done)
        {
            auto const& m = r.moves[done];
            Item* it = _k.holdPointerAcrossHook ? held[done] : core.GetItemByPos(m.from);
            if (!core.Live(it) || it->guid != m.itemGuid)
            {
                failure = "source changed before move";
                if (!_k.holdPointerAcrossHook)
                    break;
            }
            try
            {
                core.RemoveItem(m.from.bag, m.from.slot);
            }
            catch (std::exception const& e)
            {
                failure = e.what();
                break;
            }
            if (core.GetItemByPos(m.from) == it && core.Live(it))
            {
                failure = "RemoveItem did not detach the source";
                break;
            }
            touched = true;
            Item* stored = nullptr;
            try
            {
                stored = Insert(core, m.to, it, reservations[done]);
            }
            catch (std::exception const& e)
            {
                failure = e.what();
                break;
            }
            bool exact = stored == it && core.Live(stored) && stored->guid == m.itemGuid &&
                core.GetItemByPos(m.to) == stored;
            if (_k.trustSourceEmpty)
                exact = !core.GetItemByPos(m.from);
            if (!exact)
            {
                failure = "store did not return the same item at the exact destination";
                break;
            }
        }
        if (done == r.moves.size())
            return Report{Outcome::Applied, {}, {}};

        // ---- rollback, reverse order ----
        core.phase = 2;
        if (_k.refusedAfterEffects)
            return Report{Outcome::Refused, failure, {}};
        if (_k.noRollback)
            return Report{Outcome::Restored, failure, {}};
        std::vector<std::size_t> order;
        order.push_back(done); // the failing move's own item first
        for (std::size_t j = done; j-- > 0;)
            order.push_back(j);
        if (_k.forwardOrderRollback)
        {
            order.assign(1, done);
            for (std::size_t j = 0; j < done; ++j)
                order.push_back(j);
        }
        for (std::size_t j : order)
            Undo(core, r.moves[j]);
        if (_k.trustRollback)
            return Report{Outcome::Restored, failure, {}};

        Snapshot s1 = Capture(core);
        std::vector<std::string> incoherent;
        Coherence(core, incoherent);
        if (s1 == s0 && incoherent.empty())
            return Report{touched ? Outcome::Restored : Outcome::Refused,
                touched ? failure : "no effects: " + failure, {}};
        Report rep{Outcome::RecoveryRequired, failure, {}};
        for (auto const& [g, pos] : s0.where)
        {
            auto it = s1.where.find(g);
            if (it == s1.where.end() || it->second != pos)
                rep.notWhereExpected.push_back(g);
        }
        if (_k.partialRecoveryList && rep.notWhereExpected.size() > 1)
            rep.notWhereExpected.resize(1);
        return rep;
    }

private:
    Knobs _k;

    static Report Refuse(Synthetic::Core&, std::string why) { return Report{Outcome::Refused, std::move(why), {}}; }

    std::string Custody(Synthetic::Core const& core, Request const& r) const
    {
        if (r.owner != core.ownerGuid)
            return "wrong owner";
        if (_k.skipCustody)
            return {};
        if (r.incarnation != core.incarnation)
            return "stale session incarnation";
        if (r.custodyStamp != core.custodyStamp)
            return "stale custody stamp";
        if (core.loggingOut || core.retiring)
            return "owner is logging out or retiring";
        return {};
    }

    // Would any step put a two-hander next to an occupied off-hand, or an
    // off-hand next to a two-hander? Then Core would reach
    // AutoUnequipOffhandIfNeed (M4) and possibly mail the item: refuse.
    static std::string TwoHand(Synthetic::Core const& core, Request const& r)
    {
        std::map<Pos, Item*> occ;
        for (Pos p : {Pos{Synthetic::BAG0, Synthetic::MAINHAND}, Pos{Synthetic::BAG0, Synthetic::OFFHAND}})
            occ[p] = core.GetItemByPos(p);
        for (auto const& m : r.moves)
        {
            Item* it = core.GetItemByPos(m.from);
            if (occ.count(m.from))
                occ[m.from] = nullptr;
            if (occ.count(m.to))
                occ[m.to] = it;
            Item* mh = occ[Pos{Synthetic::BAG0, Synthetic::MAINHAND}];
            Item* oh = occ[Pos{Synthetic::BAG0, Synthetic::OFFHAND}];
            if (mh && oh && mh->twoHanded)
                return "plan pairs a two-hander with an occupied off-hand (would reach AutoUnequipOffhandIfNeed)";
        }
        return {};
    }
    Item* Insert(Synthetic::Core& core, Pos to, Item* it, PB59::ExactStorageReservation const& res)
    {
        if (!_k.skipExactGate && core.Live(it))
        {
            PB59::ExactStorageObservation obs{it->guid, core.BagGuid(to), to.Packed(), it->count,
                it->pos.slot == Synthetic::NULL_SLOT && it->containedIn == 0, it->owner == core.ownerGuid,
                !it->inTrade && !it->lootGenerated && !it->isBag, core.ValidPosition(to),
                core.GetItemByPos(to) != nullptr, false};
            bool ok = to.IsEquipment() ? PB59::AcceptExactEquip(res, obs) : PB59::AcceptExactStorage(res, obs);
            if (!ok && !_k.allowMerge)
                return nullptr;
        }
        if (to.IsEquipment())
            return core.EquipItem(to.Packed(), it);
        if (_k.allowMerge)
        {
            std::vector<Pos> dest{to};
            return core.StoreItem(dest, it);
        }
        return core.StoreExact(to, it);
    }

    // Put one moved item back at its source, verifying identity both ends.
    void Undo(Synthetic::Core& core, Move const& m)
    {
        Item* it = core.FindArena(m.itemGuid);
        if (!it)
            return; // identity lost: RecoveryRequired
        if (it->pos == m.from)
            return; // never left / already back
        if (core.GetItemByPos(m.from))
            return; // source reoccupied: cannot restore exactly
        try
        {
            if (it->pos.slot != Synthetic::NULL_SLOT)
            {
                if (core.GetItemByPos(it->pos) != it)
                    return;
                core.RemoveItem(it->pos.bag, it->pos.slot);
            }
            PB59::ExactStorageReservation res{m.itemGuid, core.BagGuid(m.from), m.from.Packed(), it->count};
            Insert(core, m.from, it, res);
        }
        catch (std::exception const&)
        {
        }
    }
};

// PB44 1a9e4801 src/Ai/Base/Actions/TrainerAction.cpp:692-704, transcribed.
// Strip-only; ignores the planned `to` exactly as PB44 does (native search).
// PB44 maps a false return to Refusal::StripFailed with no rollback; that is
// reported here as Refused, which the oracle checks for truthfulness.
class Pb44StripExecutor
{
public:
    Report Execute(Synthetic::Core& core, Request const& r)
    {
        core.phase = 1;
        for (auto const& m : r.moves)
            if (!MoveEquippedToBags(core, m.from.slot))
                return Report{Outcome::Refused, "StripFailed (PB44: no rollback)", {}};
        return Report{Outcome::Applied, {}, {}};
    }

private:
    static bool MoveEquippedToBags(Synthetic::Core& bot, std::uint8_t slot)
    {
        Item* item = bot.GetItemByPos(Synthetic::BAG0, slot); // held across the callback below
        if (!item)
            return true;
        std::vector<Pos> dest;
        if (bot.CanStoreItem(Synthetic::NULL_BAG, Synthetic::NULL_SLOT, dest, item) != InventoryResult::Ok ||
            bot.CanUnequipItem(std::uint16_t((Synthetic::BAG0 << 8) | slot), false) != InventoryResult::Ok)
            return false;
        bot.RemoveItem(Synthetic::BAG0, slot);
        bot.StoreItem(dest, item);                     // returned identity ignored
        return !bot.GetItemByPos(Synthetic::BAG0, slot); // success from empty source only
    }
};

static_assert(MoveExecutor<ReferenceExecutor>);
static_assert(MoveExecutor<Pb44StripExecutor>);
}
