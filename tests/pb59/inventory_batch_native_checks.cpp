using Outcome = PB59::InventoryBatchOutcome;
using Failure = PB59::InventoryBatchFailure;
static unsigned Checks = 0;
#define CHECK(value) do { ++Checks; if (!(value)) { std::printf("FAIL %u line %d\n", Checks, __LINE__); return 1; } } while (false)
static std::vector<PB59::InventoryBatchMove> Plan(Player& actor, unsigned count)
{
    std::vector<PB59::InventoryBatchMove> plan;
    for (unsigned index = 0, slot = 0; index < count; ++index, ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY) ++slot;
        auto const from = uint16(65280 + slot);
        uint16 const to = index < 16 ? uint16(65303 + index) : uint16((19 << 8) | (index - 16));
        if (index == 16)
        {
            auto bag = std::make_unique<Bag>(500, &actor.map);
            actor.bags.emplace(19, bag.get()); actor.slots.at(65299) = bag.get();
            actor.fields[16 + 19 * 2] = 500;
            actor.ownership.push_back(std::move(bag));
        }
        actor.Add(from, 10 + index);
        plan.push_back({*actor.CapturePB59InventorySlot(from), *actor.CapturePB59InventorySlot(to)});
    }
    return plan;
}
static PB59::InventoryBatchResult Execute(Player& actor, std::span<PB59::InventoryBatchMove const> plan)
{
    return actor.ExecutePB59InventoryBatchPlan(1, actor._pb59Transaction.OwnerIncarnation(), 7,
        *actor.CapturePB59InventoryStamp(), plan);
}
int main()
{
    for (unsigned count : {1u, 2u, 4u, 17u})
    {
        Player actor; auto plan = Plan(actor, count);
        CHECK(bool(actor.CapturePB59InventoryStamp())); // headless owner capture
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::Applied);
        CHECK(result.failure == Failure::None && result.recoveryCount == 0);
        CHECK(actor.fields[1] == 0 && actor.transfers == count);
        CHECK(actor._pb59Transaction.State() == PB59::TransactionAdmission::Phase::Idle);
        CHECK(!actor._pb59Equipment.InventoryQuarantined());
        for (auto const& move : plan)
        {
            CHECK(!actor.GetItemByPos(move.source.position));
            CHECK(actor.GetItemByPos(move.destination.position)->facts == *move.source.item);
        }
        actor.PublicInventoryMutation(); CHECK(actor.fields[4] == 99);
    }
    for (unsigned failure : {1u, 2u, 3u, 4u})
    {
        Player actor; auto plan = Plan(actor, 2); actor.rejectPolicy = failure;
        auto const before = actor.fields;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::Refused);
        CHECK(actor.fields == before && actor.transfers == 0 && actor.modsCalls == 0);
        CHECK(!actor._pb59Equipment.InventoryQuarantined());
    }
    for (unsigned count : {1u, 2u, 4u, 17u})
        for (unsigned failure = 1; failure <= count; ++failure)
        {
            Player actor; auto plan = Plan(actor, count); actor.rejectTransfer = failure;
            auto const before = actor.fields;
            auto result = Execute(actor, plan);
            CHECK(result.outcome == Outcome::Restored);
            CHECK(actor.fields == before && actor.m_itemUpdateQueue.empty());
            CHECK(actor.map.updates.empty() && !actor._pb59Equipment.InventoryQuarantined());
            for (auto const& move : plan)
            {
                CHECK(actor.GetItemByPos(move.source.position)->facts == *move.source.item);
                CHECK(!actor.GetItemByPos(move.destination.position));
            }
            for (unsigned reverse = 0; reverse + 1 < failure; ++reverse)
                CHECK(actor.transferOrder[failure - 1 + reverse] == plan[failure - 2 - reverse].source.item->guid);
        }
    {
        Player actor; auto plan = Plan(actor, 2); actor.rejectPolicy = 7;
        auto const before = actor.fields;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::Restored && actor.fields == before);
    }
    for (unsigned failure : {1u, 2u, 3u})
    {
        Player actor; auto plan = Plan(actor, 3); actor.recoveryTransfer = failure;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired);
        CHECK(result.recoveryCount == failure);
        CHECK(actor._pb59Equipment.InventoryQuarantined());
        CHECK(actor._pb59Transaction.State() == PB59::TransactionAdmission::Phase::RecoveryRequired);
        for (auto const& move : plan) CHECK(PB59::InventoryQuarantine::Contains(nullptr, move.source.owner));
        actor.PublicInventoryMutation(); CHECK(actor.fields[4] == 0);
        for (unsigned index = 0; index < result.recoveryCount; ++index)
            CHECK(result.recoveryGuids[index] == plan[index].source.item->guid);
    }
    {
        Player actor; auto plan = Plan(actor, 2); actor.rejectTransfer = 2; actor.denyUndo = 1;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 1);
        CHECK(result.recoveryGuids[0] == plan[0].source.item->guid);
    }
    {
        Player actor; auto plan = Plan(actor, 1); actor.lieTransfer = 1;
        CHECK(Execute(actor, plan).outcome == Outcome::RecoveryRequired);
    }
    {
        Player actor; auto plan = Plan(actor, 1); actor.throwMods = 1;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1);
        actor.policyHook = [&] { actor.PublicInventoryMutation(); };
        auto const before = actor.fields;
        CHECK(Execute(actor, plan).outcome == Outcome::Refused);
        CHECK(actor.fields == before && actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1);
        actor.modsHook = [&] { actor.PublicInventoryMutation(); };
        auto const before = actor.fields;
        CHECK(Execute(actor, plan).outcome == Outcome::RecoveryRequired);
        CHECK(actor.fields == before && actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1);
        actor.policyHook = [&] { (void)actor._pb59Transaction.Request(PB59::TransactionAdmission::Work::Logout); };
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 0);
        CHECK(actor._pb59Transaction.PendingLogout() && actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1); unsigned nested = 0;
        auto const stamp = *actor._pb59Equipment.Capture();
        actor.policyHook = [&] {
            auto result = actor.ExecutePB59InventoryBatchPlan(1, actor._pb59Transaction.OwnerIncarnation(), 7, stamp, plan);
            if (result.outcome == Outcome::Refused) ++nested;
        };
        CHECK(Execute(actor, plan).outcome == Outcome::Applied);
        CHECK(nested >= 2 && actor.transfers == 1);
    }
    {
        Player actor; auto plan = Plan(actor, 1);
        actor.policyHook = [&] { actor.session.player = nullptr; };
        CHECK(Execute(actor, plan).outcome == Outcome::RecoveryRequired);
        CHECK(actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1); actor.visibleHook = [] { };
        actor.rejectTransfer = 2; // first move completes, then force a later failure via a second source
        actor.Add(65281, 11);
        plan.push_back({*actor.CapturePB59InventorySlot(65281), *actor.CapturePB59InventorySlot(65304)});
        CHECK(Execute(actor, plan).outcome == Outcome::RecoveryRequired);
    }
    {
        Player actor; auto plan = Plan(actor, 1); actor.m_itemDuration.push_back(actor.GetItemByPos(plan[0].source.position));
        CHECK(Execute(actor, plan).outcome == Outcome::Refused); CHECK(actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1); actor.GetItemByPos(plan[0].source.position)->prototype.Spells[0] = {1, ITEM_SPELLTRIGGER_ON_NO_DELAY_USE};
        CHECK(Execute(actor, plan).outcome == Outcome::Refused); CHECK(actor.transfers == 0);
    }
    {
        Player actor; auto plan = Plan(actor, 1); auto* item = actor.GetItemByPos(plan[0].source.position);
        actor.policyHook = [&] { item->facts.count = 2; };
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 1);
        CHECK(actor.transfers == 0);
    }
    // Bag->equipment is a real supported direction; no public-helper fallback.
    {
        Player actor; actor.Add(65303, 10);
        std::array plan{PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65303), *actor.CapturePB59InventorySlot(65280)}};
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::Applied && actor.GetItemByPos(65280));
        CHECK(actor.fields[1] == 5);
    }
    {
        Player actor; actor.Add(65296, 11); auto* weapon = actor.Add(65303, 10);
        weapon->prototype.InventoryType = INVTYPE_2HWEAPON;
        std::array plan{PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65303), *actor.CapturePB59InventorySlot(65295)}};
        CHECK(Execute(actor, plan).outcome == Outcome::Refused); CHECK(actor.transfers == 0);
    }
    {
        Player actor; actor.Add(65296, 11); auto* weapon = actor.Add(65303, 10);
        weapon->prototype.InventoryType = INVTYPE_2HWEAPON;
        std::array plan{
            PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65296), *actor.CapturePB59InventorySlot(65304)},
            PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65303), *actor.CapturePB59InventorySlot(65295)}};
        CHECK(Execute(actor, plan).outcome == Outcome::Applied);
        CHECK(!actor.GetItemByPos(65296) && actor.GetItemByPos(65295) == weapon);
    }
    // Reverse a completed storage->equipment move when the next link refuses.
    {
        Player actor; actor.Add(65303, 10); actor.Add(65304, 11);
        std::array plan{
            PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65303), *actor.CapturePB59InventorySlot(65280)},
            PB59::InventoryBatchMove{*actor.CapturePB59InventorySlot(65304), *actor.CapturePB59InventorySlot(65281)}};
        actor.rejectTransfer = 2;
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::Restored && actor.fields[1] == 0);
        CHECK(actor.GetItemByPos(65303) && actor.GetItemByPos(65304));
        CHECK(!actor.GetItemByPos(65280) && !actor.GetItemByPos(65281));
    }
    // Hidden native modifier divergence must not masquerade as Restored merely
    // because all published fields and physical links returned to their origin.
    {
        Player actor; auto plan = Plan(actor, 2); actor.rejectTransfer = 2;
        actor.modsHook = [&] { actor.m_auraFlatModifiersGroup[0][0] += 0.25f; };
        auto result = Execute(actor, plan);
        CHECK(result.outcome == Outcome::RecoveryRequired);
        CHECK(actor._pb59Equipment.InventoryQuarantined());
    }
    std::printf("%u actual native executor/journal/item-mod assertions PASS (engine/transfer doubles)\n", Checks);
}
