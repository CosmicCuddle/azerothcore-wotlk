// CS02 / PB59 move-rollback harness -- Claude's PB50 child 43320882 StripToBags through MoveExecutor.
//
// *** SYNTHETIC. *** Same method and fixtures as Pb50StripAdapter.h (ebc3a710), re-transcribed for
// CosmicCuddle/mod-playerbots draft PR51, branch claude/pb50-hook-safety-20261124, exact head
// 43320882ed18c97ec1f0898318fd152d38f2204f (one commit on ebc3a710).
//   - src/Bot/AutoGearStorage{Matching,Transaction}.h and AutoGearExactStorageObservation.h are
//     byte-identical at 43320882 and ebc3a710, so the verbatim copies in pb50_ebc3a710/ are reused;
//   - PlayerbotFactory.cpp is unchanged, so Prepare/Validate are reused unchanged;
//   - TrainerAction.cpp StripToBags (re-entry flag, inventory+context Fingerprint, unchanged() ->
//     RecoveryRequired, current() re-reads, contextValid after the hook, Detached before linking,
//     Rejected -> unchanged ? Refused : RecoveryRequired) and the per-bot command fence in
//     RunPB59AutoGearWorldPlan are transcribed line by line below.
// Findings are synthetic findings for Claude to assess; CS02 does not change PB50 or PR51.
#pragma once

#include "Pb50StripAdapter.h"

#include <set>

namespace CS02MoveRollback
{
class Pb50ChildStripAdapter
{
public:
    Pb50StripResult last = Pb50StripResult::Refused;

    Report Execute(Synthetic::Core& bot, Request const& r)
    {
        // RunPB59AutoGearWorldPlan (43320882): one AutoGear command per bot at a time.
        static thread_local std::set<std::uint64_t> running;
        if (!running.insert(bot.ownerGuid).second)
        {
            last = Pb50StripResult::Refused; // "I'm already changing my gear. Nothing else was changed."
            return Report{Outcome::Refused, "busy", {}};
        }
        struct Release
        {
            std::uint64_t guid;
            ~Release() { running.erase(guid); }
        } const release{bot.ownerGuid};
        last = StripToBags(bot, r);
        switch (last)
        {
            case Pb50StripResult::Done: return Report{Outcome::Applied, "Done", {}};
            case Pb50StripResult::Restored: return Report{Outcome::Restored, "Restored", {}};
            case Pb50StripResult::RecoveryRequired: return Report{Outcome::RecoveryRequired, "RecoveryRequired", {}};
            case Pb50StripResult::NoSpace: return Report{Outcome::Refused, "NoSpace", {}};
            case Pb50StripResult::Refused: break;
        }
        return Report{Outcome::Refused, "Refused", {}};
    }

private:
    struct EquipmentStorageMove // PlayerbotFactory.h EquipmentStorageMove
    {
        std::uint64_t itemGuid;
        std::uint16_t source, destination;
        std::uint32_t count;
        std::uint64_t destinationBagGuid;
        PB59::InventorySlotFacts original;
    };
    static constexpr std::uint8_t BODY = 3, TABARD = 18;

    // Player::CapturePB59InventorySlot analogue: unknown for an invalid position.
    static std::optional<PB59::InventorySlotFacts> CaptureSlot(Synthetic::Core const& bot, std::uint16_t pos)
    {
        Pos p = Synthetic::Core::Unpack(pos);
        if (!bot.ValidPosition(p))
            return std::nullopt;
        Item* it = bot.GetItemByPos(p);
        return Facts(bot, p, it && !it->tombstone ? it : nullptr);
    }
    static bool InWorldAlive(Synthetic::Core const& bot) { return !bot.loggingOut; }

    // ---- PlayerbotFactory::PrepareEquipmentStorage (hook-relevant transcription) ----
    static bool Prepare(Synthetic::Core& bot, Request const& r, std::uint32_t slotMask,
        std::vector<EquipmentStorageMove>& out, bool& noSpace)
    {
        noSpace = false;
        if (!InWorldAlive(bot))
            return false;
        for (std::uint8_t slot = 0; slot < Synthetic::EQUIP_END; ++slot)
        {
            if (slot == BODY || slot == TABARD || !(slotMask & (1u << slot)))
                continue;
            std::uint16_t const source = Pos{Synthetic::BAG0, slot}.Packed();
            auto original = CaptureSlot(bot, source);
            if (!original)
                return false;
            if (!original->item)
                continue;
            Item* item = bot.GetItemByPos(Synthetic::BAG0, slot);
            if (!item)
                return false;
            std::uint64_t const originalGuid = item->guid;
            std::uint32_t const originalCount = original->item->count;
            if (bot.CanUnequipItem(source, false) != Synthetic::InventoryResult::Ok)
                return false;
            auto current = CaptureSlot(bot, source);
            if (!current || *current != *original)
                return false;
            item = bot.GetItemByPos(Synthetic::BAG0, slot);
            if (!item || item->guid != originalGuid || !originalCount || item->count != originalCount || item->inTrade)
                return false;
            // Substitution for Match(): the harness plan's destination for this source.
            auto planned = std::find_if(r.moves.begin(), r.moves.end(),
                [&](Move const& m) { return m.from.Packed() == source; });
            if (planned == r.moves.end())
                return false;
            std::uint16_t const destination = planned->to.Packed();
            std::vector<Pos> dest;
            if (bot.GetItemByPos(planned->to) ||
                bot.CanStoreItem(planned->to.bag, planned->to.slot, dest, item) != Synthetic::InventoryResult::Ok ||
                dest.size() != 1 || dest.front().Packed() != destination)
            {
                noSpace = true;
                return false;
            }
            current = CaptureSlot(bot, source);
            if (!current || *current != *original)
                return false;
            out.push_back({originalGuid, source, destination, originalCount, bot.BagGuid(planned->to), *original});
        }
        // Offhand first: reverse restoration then puts mainhand back before offhand.
        auto offhand = std::find_if(out.begin(), out.end(),
            [](EquipmentStorageMove const& m) { return (m.source & 255) == Synthetic::OFFHAND; });
        if (offhand != out.end())
            std::rotate(out.begin(), offhand, offhand + 1);
        return Validate(bot, out, slotMask);
    }

    // ---- PlayerbotFactory::ValidateEquipmentStorage (transcription) ----
    static bool Validate(Synthetic::Core& bot, std::vector<EquipmentStorageMove> const& moves, std::uint32_t slotMask)
    {
        if (!InWorldAlive(bot) || moves.size() > Synthetic::EQUIP_END)
            return false;
        std::vector<PlayerbotsAutoGearStorage::Move> exact;
        for (auto const& move : moves)
        {
            if (!move.original.item || move.original.item->guid != move.itemGuid ||
                move.original.item->count != move.count || move.original.position != move.source)
                return false;
            exact.push_back({move.itemGuid, move.source, move.destination});
        }
        if (!PlayerbotsAutoGearStorage::ValidMoves(exact))
            return false;
        auto equippedCount = [&]() {
            std::size_t count = 0;
            for (std::uint8_t slot = 0; slot < Synthetic::EQUIP_END; ++slot)
                if (slot != BODY && slot != TABARD && (slotMask & (1u << slot)) && bot.GetItemByPos(Synthetic::BAG0, slot))
                    ++count;
            return count;
        };
        auto originalAndEmpty = [&](EquipmentStorageMove const& move) {
            std::uint8_t const slot = move.source & 255;
            std::uint8_t const bag = move.destination >> 8;
            std::uint8_t const destinationSlot = move.destination & 255;
            if ((move.source >> 8) != Synthetic::BAG0 || slot >= Synthetic::EQUIP_END || slot == BODY || slot == TABARD ||
                !(slotMask & (1u << slot)) || !move.count)
                return false;
            bool const backpack = bag == Synthetic::BAG0 && destinationSlot >= Synthetic::PACK_START &&
                destinationSlot < Synthetic::PACK_END;
            bool const equippedBag = bag >= Synthetic::BAG_START && bag < Synthetic::BAG_END;
            if (!backpack && !equippedBag)
                return false;
            if (backpack && move.destinationBagGuid != 0)
                return false;
            if (equippedBag)
            {
                Item* container = bot.BagAt(bag);
                if (!container || container->guid != move.destinationBagGuid || destinationSlot >= container->bagSize)
                    return false;
            }
            auto sourceNow = CaptureSlot(bot, move.source);
            auto destinationNow = CaptureSlot(bot, move.destination);
            return sourceNow && destinationNow &&
                PlayerbotsAutoGearStorage::OriginalAndEmpty(move.original, move.destination, move.destinationBagGuid,
                    *sourceNow, *destinationNow);
        };
        if (equippedCount() != moves.size())
            return false;
        for (auto const& move : moves)
        {
            if (!originalAndEmpty(move))
                return false;
            if (bot.CanUnequipItem(move.source, false) != Synthetic::InventoryResult::Ok)
                return false;
            if (!originalAndEmpty(move))
                return false;
            Item* item = bot.GetItemByPos(Synthetic::Core::Unpack(move.source));
            std::vector<Pos> destinations;
            Pos d = Synthetic::Core::Unpack(move.destination);
            if (bot.CanStoreItem(d.bag, d.slot, destinations, item) != Synthetic::InventoryResult::Ok)
                return false;
            if (destinations.size() != 1 || destinations.front().Packed() != move.destination)
                return false;
        }
        if (!InWorldAlive(bot) || equippedCount() != moves.size())
            return false;
        for (auto const& move : moves)
            if (!originalAndEmpty(move))
                return false;
        return true;
    }

    bool _stripping = false;

    // ---- 43320882 Detached(item, from) ----
    static bool Detached(Synthetic::Core const& bot, Item const* item, std::uint16_t from)
    {
        return !bot.GetItemByPos(Synthetic::Core::Unpack(from)) && item->pos.slot == Synthetic::NULL_SLOT &&
            item->containedIn == 0;
    }

    // ---- 43320882 InventoryFingerprint / Fingerprint() ----
    struct InventoryFingerprint
    {
        std::uint64_t session = 0;
        bool inWorld = false, alive = false, logout = false;
        std::vector<PB59::InventorySlotFacts> slots;
        std::vector<std::uint64_t> others;
        bool operator==(InventoryFingerprint const&) const = default;
    };
    static std::optional<InventoryFingerprint> Fingerprint(Synthetic::Core const& bot)
    {
        InventoryFingerprint print;
        print.session = bot.incarnation;
        print.inWorld = !bot.loggingOut; // synthetic: IsInWorld/IsAlive/PlayerLogout share one flag
        print.alive = !bot.loggingOut;
        print.logout = bot.loggingOut;
        auto add = [&](std::uint8_t bag, std::uint8_t slot) {
            auto facts = CaptureSlot(bot, std::uint16_t((bag << 8) | slot));
            if (facts)
                print.slots.push_back(*facts);
            return facts.has_value();
        };
        auto other = [&](std::uint8_t slot) {
            Item* item = bot.GetItemByPos(Synthetic::BAG0, slot);
            print.others.push_back(item ? item->guid : 0);
        };
        for (std::uint8_t slot = 0; slot < Synthetic::EQUIP_END; ++slot)
        {
            if (slot == BODY || slot == TABARD)
                other(slot);
            else if (!add(Synthetic::BAG0, slot))
                return std::nullopt;
        }
        for (std::uint8_t slot = Synthetic::PACK_START; slot < Synthetic::PACK_END; ++slot)
            if (!add(Synthetic::BAG0, slot))
                return std::nullopt;
        for (std::uint8_t bag = Synthetic::BAG_START; bag < Synthetic::BAG_END; ++bag)
        {
            other(bag);
            if (Item* container = bot.BagAt(bag))
                for (std::uint8_t slot = 0; slot < container->bagSize; ++slot)
                    if (!add(bag, slot))
                        return std::nullopt;
        }
        return print;
    }

    // ---- TrainerAction.cpp StripToBags @ 43320882 (transcription) ----
    Pb50StripResult StripToBags(Synthetic::Core& bot, Request const& r)
    {
        std::uint32_t slotMask = 0;
        for (auto const& m : r.moves)
            if (m.from.IsEquipment())
                slotMask |= 1u << m.from.slot;
        bot.phase = 0;
        if (_stripping)
            return Pb50StripResult::Refused;
        _stripping = true;
        struct Reset
        {
            bool& flag;
            ~Reset() { flag = false; }
        } const reset{_stripping};
        auto const before = Fingerprint(bot);
        if (!before)
            return Pb50StripResult::Refused;
        auto unchanged = [&]() {
            auto const after = Fingerprint(bot);
            return after && *after == *before;
        };
        std::vector<EquipmentStorageMove> plan;
        bool noSpace = false;
        bool const prepared = Prepare(bot, r, slotMask, plan, noSpace);
        if (!unchanged())
            return Pb50StripResult::RecoveryRequired;
        if (noSpace)
            return Pb50StripResult::NoSpace;
        if (!prepared || plan.empty())
            return Pb50StripResult::Refused;

        std::vector<PlayerbotsAutoGearStorage::ExactMove> moves;
        for (auto const& move : plan)
            moves.push_back({{move.itemGuid, move.source, move.destination}, move.count, move.destinationBagGuid});

        std::uint64_t const session = bot.incarnation; // WorldSession* identity analogue
        auto inspect = [&](std::uint16_t position) -> std::optional<PlayerbotsAutoGearStorage::ExactSlot> {
            auto facts = CaptureSlot(bot, position);
            if (!facts)
                return std::nullopt;
            return PlayerbotsAutoGearStorage::ExactSlot{facts->item ? facts->item->guid : 0, facts->bag,
                facts->item ? facts->item->count : 0};
        };
        auto contextValid = [&]() { return InWorldAlive(bot) && bot.incarnation == session; };
        auto moveExact = [&](PlayerbotsAutoGearStorage::ExactMove const& move, bool restore) {
            bot.phase = restore ? 2 : 1; // ledger attribution only (harness instrumentation)
            std::uint16_t const from = restore ? move.position.destination : move.position.source;
            std::uint16_t const to = restore ? move.position.source : move.position.destination;
            auto current = [&]() -> Item* {
                Item* item = bot.GetItemByPos(Synthetic::Core::Unpack(from));
                return item && item->guid == move.position.itemGuid && item->count == move.count ? item : nullptr;
            };
            Item* item = current();
            if (!item)
                return false;
            std::uint16_t equipDest = 0;
            std::vector<Pos> storeDest;
            if (restore)
            {
                equipDest = to; // CanEquipItem(slot, dest, ...) yields the slot itself here
                if (bot.CanEquipItem(to & 255, item) != Synthetic::InventoryResult::Ok || equipDest != to)
                    return false;
            }
            else if (bot.CanUnequipItem(from, false) != Synthetic::InventoryResult::Ok)
                return false;
            item = current();
            if (!item || !contextValid())
                return false;
            if (!restore &&
                (bot.CanStoreItem(to >> 8, to & 255, storeDest, item) != Synthetic::InventoryResult::Ok ||
                    storeDest.size() != 1 || storeDest.front().Packed() != to || item->count != move.count))
                return false;
            bot.RemoveItem(from >> 8, from & 255);
            if (!Detached(bot, item, from))
                return false;
            return restore ? bot.EquipItem(equipDest, item) == item : bot.StoreItem(storeDest, item) == item;
        };

        switch (PlayerbotsAutoGearStorage::Execute(moves, /*admittedFinalB*/ true, inspect, contextValid, moveExact))
        {
            case PlayerbotsAutoGearStorage::TransactionStatus::Committed: return Pb50StripResult::Done;
            case PlayerbotsAutoGearStorage::TransactionStatus::RolledBack: return Pb50StripResult::Restored;
            case PlayerbotsAutoGearStorage::TransactionStatus::RecoveryRequired: return Pb50StripResult::RecoveryRequired;
            case PlayerbotsAutoGearStorage::TransactionStatus::Rejected: break;
        }
        return unchanged() ? Pb50StripResult::Refused : Pb50StripResult::RecoveryRequired;
    }
};

static_assert(MoveExecutor<Pb50ChildStripAdapter>);
}
