#include <iostream>

using Outcome = PB59::InventoryBatchOutcome;
using Status = PB59::Recovery::CollectionStatus;
using Ledger = PB59::TransactionAdmission;
static unsigned BridgeChecks = 0;
#define BRIDGE_CHECK(value) do { \
    ++BridgeChecks; \
    if (!(value)) \
    { \
        std::cerr << "bridge line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

static std::vector<PB59::InventoryBatchMove> BridgePlan(Player& actor, unsigned count = 1)
{
    Bridge = {};
    std::vector<PB59::InventoryBatchMove> plan;
    for (unsigned index = 0; index < count; ++index)
    {
        actor.Add(65280 + index, 10 + index);
        plan.push_back({*actor.CapturePB59InventorySlot(65280 + index),
            *actor.CapturePB59InventorySlot(65303 + index)});
    }
    return plan;
}
static PB59::InventoryBatchResult RunBridge(Player& actor, std::span<PB59::InventoryBatchMove const> plan)
{
    auto const result = actor.ExecutePB59InventoryBatchPlan(1, actor._pb59Transaction.OwnerIncarnation(), 7,
        *actor.CapturePB59InventoryStamp(), plan);
    Bridge.beforeBind = {};
    Bridge.beforeCapture = {};
    return result;
}

struct BridgeTrace
{
    Outcome outcome;
    PB59::InventoryBatchFailure failure;
    std::uint8_t recoveryCount;
    std::array<std::uint64_t, 19> recoveryGuids;
    std::array<uint32, PLAYER_END> fields;
    std::array<std::uint64_t, 6> links{};
    Ledger::Phase phase;
    bool quarantined, pendingSave, pendingLogout;
    unsigned transfers, mods, policies;
    bool operator==(BridgeTrace const&) const = default;
};

static BridgeTrace RunTrace(unsigned scenario, bool bridged)
{
    Player actor;
    auto plan = BridgePlan(actor, scenario == 2 ? 2 : scenario == 3 || scenario == 4 ? 3 : 1);
    if (scenario == 1) actor.rejectPolicy = 1;
    if (scenario == 2) actor.rejectPolicy = 7;
    if (scenario == 3 || scenario == 4) actor.recoveryTransfer = scenario - 2;
    if (scenario == 5) actor.throwMods = 1;
    if (scenario == 6 || scenario == 7)
        actor.policyHook = [&] { (void)actor._pb59Transaction.Request(scenario == 6 ?
            Ledger::Work::StandaloneSave : Ledger::Work::Logout); };
    if (scenario == 8) actor.policyHook = [&] { actor.session.player = nullptr; };
    if (scenario == 9)
    {
        actor.rejectPolicy = 1;
        actor.policyHook = [&] { actor.m_auraFlatModifiersGroup[0][0] += 0.25f; };
    }
    if (scenario == 10)
    {
        actor.rejectPolicy = 1;
        actor.policyHook = [&] { actor.auras.emplace(123, nullptr); };
    }
    auto const stamp = *actor.CapturePB59InventoryStamp();
    auto const result = bridged ? RunBridge(actor, plan) : actor.ExecutePB59InventoryBatchPlanControl(1,
        actor._pb59Transaction.OwnerIncarnation(), 7, stamp, plan);
    BridgeTrace trace{result.outcome, result.failure, result.recoveryCount, result.recoveryGuids, actor.fields, {},
        actor._pb59Transaction.State(), actor._pb59Equipment.InventoryQuarantined(),
        actor._pb59Transaction.PendingSave(),
        actor._pb59Transaction.PendingLogout(), actor.transfers, actor.modsCalls, actor.policyCalls};
    // Test inspection of a still-owned fixture actor, not a post-RR production reader.
    for (std::size_t index = 0; index < plan.size(); ++index)
    {
        auto const* source = actor.GetItemByPos(plan[index].source.position);
        auto const* destination = actor.GetItemByPos(plan[index].destination.position);
        trace.links[index * 2] = source ? source->facts.guid : 0;
        trace.links[index * 2 + 1] = destination ? destination->facts.guid : 0;
    }
    return trace; // actor retires before another same-GUID scenario starts
}

int main()
{
    for (bool rejected : {false, true})
    {
        Player actor; auto plan = BridgePlan(actor);
        if (rejected) plan[0].source.item->count = 99;
        auto const result = RunBridge(actor, plan);
        BRIDGE_CHECK(result.outcome == (rejected ? Outcome::Refused : Outcome::Applied));
        BRIDGE_CHECK(Bridge.prepared == !rejected && Bridge.recoveryCalls == 0 && !Bridge.values);
        BRIDGE_CHECK(actor._pb59Transaction.CanBegin() && !actor._pb59Equipment.InventoryQuarantined());
        if (!rejected)
            BRIDGE_CHECK(Bridge.bindStatus == Status::Bound && Bridge.nativeTicket != 0);
    }
    {
        Player actor; auto plan = BridgePlan(actor);
        actor.GetItemByPos(65280)->_changesMask.SetCount(0); // unavailable Item mask, fixture-only fault
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::Refused);
        BRIDGE_CHECK(!Bridge.prepared && Bridge.nativeTicket == 0 && actor.transfers == 0);
        BRIDGE_CHECK(actor._pb59Transaction.CanBegin() && !actor._pb59Equipment.InventoryQuarantined());
    }
    {
        Player actor; auto plan = BridgePlan(actor, 2); actor.rejectPolicy = 7;
        auto const result = RunBridge(actor, plan);
        BRIDGE_CHECK(result.outcome == Outcome::Restored && Bridge.bindStatus == Status::Bound);
        BRIDGE_CHECK(!Bridge.values && Bridge.recoveryCalls == 0);
    }
    for (unsigned failure : {1u, 2u, 3u})
    {
        Player actor; auto plan = BridgePlan(actor, 3); actor.recoveryTransfer = failure;
        auto const result = RunBridge(actor, plan);
        BRIDGE_CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == failure);
        BRIDGE_CHECK(Bridge.prepared && Bridge.bindStatus == Status::Bound && Bridge.recoveryCalls == 1);
        BRIDGE_CHECK(Bridge.scopeLiveBefore && Bridge.scopeLiveAfter && Bridge.values);
        auto const& data = *Bridge.values;
        BRIDGE_CHECK(data.evidence.operation.ticket == Bridge.nativeTicket && Bridge.nativeTicket != 0);
        BRIDGE_CHECK(data.evidence.operation.ownerIncarnation == actor._pb59Transaction.OwnerIncarnation() &&
            data.evidence.operation.sessionIncarnation == actor.session.incarnation);
        BRIDGE_CHECK(data.evidence.revision == actor._pb59Equipment.Capture()->revision);
        BRIDGE_CHECK(data.evidence.reportedCount == result.recoveryCount);
        for (unsigned index = 0; index < result.recoveryCount; ++index)
            BRIDGE_CHECK(data.evidence.reportedGuids[index] == result.recoveryGuids[index]);
        BRIDGE_CHECK(data.summary.revisionObserved && data.summary.observedWatchedItems == 3);
        BRIDGE_CHECK(actor._pb59Transaction.State() == Ledger::Phase::RecoveryRequired);
        BRIDGE_CHECK(actor._pb59Equipment.InventoryQuarantined() && PB59::InventoryQuarantine::Contains(nullptr, 1));
        actor.PublicInventoryMutation(); BRIDGE_CHECK(actor.fields[4] == 0);
    }
    {
        Player actor; auto plan = BridgePlan(actor); actor.throwMods = 1;
        auto const result = RunBridge(actor, plan);
        BRIDGE_CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 0);
        BRIDGE_CHECK(Bridge.values && Bridge.collectStatus == Status::Collected);
        BRIDGE_CHECK(Bridge.effectsObserved && Bridge.effectsSame);
        BRIDGE_CHECK(Bridge.values->evidence.effects != PB59::Recovery::ProofState::Verified);
        BRIDGE_CHECK(Bridge.values->evidence.reportedCount == 0 && Bridge.values->summary.intentsObserved);
    }
    {
        Player actor; auto plan = BridgePlan(actor);
        actor.rejectPolicy = 1;
        actor.policyHook = [&] { actor.auras.emplace(123, nullptr); }; // unreadable effect-domain fault on refusal
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::RecoveryRequired);
        BRIDGE_CHECK(Bridge.values && !Bridge.effectsObserved && !Bridge.effectsSame);
        BRIDGE_CHECK(Bridge.scopeLiveBefore && Bridge.scopeLiveAfter);
        BRIDGE_CHECK(Bridge.values->evidence.effects != PB59::Recovery::ProofState::Verified);
    }
    for (auto work : {Ledger::Work::StandaloneSave, Ledger::Work::Logout})
    {
        Player actor; auto plan = BridgePlan(actor);
        actor.policyHook = [&] { (void)actor._pb59Transaction.Request(work); };
        auto const result = RunBridge(actor, plan);
        BRIDGE_CHECK(result.outcome == Outcome::RecoveryRequired && result.recoveryCount == 0);
        BRIDGE_CHECK(Bridge.values && Bridge.values->summary.intentsObserved);
        BRIDGE_CHECK(Bridge.values->evidence.pendingSave == (work == Ledger::Work::StandaloneSave));
        BRIDGE_CHECK(Bridge.values->evidence.pendingLogout == (work == Ledger::Work::Logout));
        BRIDGE_CHECK(actor._pb59Transaction.PendingSave() == Bridge.values->evidence.pendingSave);
        BRIDGE_CHECK(actor._pb59Transaction.PendingLogout() == Bridge.values->evidence.pendingLogout);
    }
    {
        Player actor; auto plan = BridgePlan(actor); actor.rejectPolicy = 1;
        actor.policyHook = [&] { actor.m_auraFlatModifiersGroup[0][0] += 0.25f; };
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::RecoveryRequired);
        BRIDGE_CHECK(Bridge.effectsObserved && !Bridge.effectsSame && Bridge.values);
        BRIDGE_CHECK(Bridge.values->evidence.effects != PB59::Recovery::ProofState::Verified);
    }
    {
        Player actor; auto plan = BridgePlan(actor);
        actor.policyHook = [&] { actor.session.player = nullptr; };
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::RecoveryRequired);
        BRIDGE_CHECK(Bridge.collectStatus == Status::ContextUnavailable && !Bridge.effectsObserved);
        BRIDGE_CHECK(Bridge.values && !Bridge.values->evidence.sessionPresent);
    }
    {
        Player actor; auto plan = BridgePlan(actor);
        Bridge.beforeBind = [&] { actor.GetItemByPos(65280)->rawFields[31] = 99; };
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::RecoveryRequired);
        BRIDGE_CHECK(Bridge.bindStatus == Status::ContextUnavailable && actor.transfers == 0);
        BRIDGE_CHECK(Bridge.collectStatus == Status::WrongPhase && Bridge.values);
        BRIDGE_CHECK(actor._pb59Transaction.State() == Ledger::Phase::RecoveryRequired);
        Bridge.beforeBind = {};
    }
    {
        Player actor; auto plan = BridgePlan(actor); actor.throwMods = 1;
        Bridge.beforeCapture = [&](Ledger::MutationScope& scope)
        {
            (void)scope.Finish({true, true, true, true}); // synthetic stale-scope fault, not native reconciliation
            actor.collectionStopped = true;
        };
        BRIDGE_CHECK(RunBridge(actor, plan).outcome == Outcome::RecoveryRequired);
        BRIDGE_CHECK(!Bridge.scopeLiveBefore && !Bridge.scopeLiveAfter);
        BRIDGE_CHECK(Bridge.collectStatus == Status::OwnerUnavailable && !Bridge.effectsObserved);
        BRIDGE_CHECK(actor.readsAfterCollectionStop == 0);
        Bridge.beforeCapture = {};
    }
    for (unsigned scenario = 0; scenario < 11; ++scenario)
    {
        auto const control = RunTrace(scenario, false);
        auto const bridged = RunTrace(scenario, true);
        BRIDGE_CHECK(control == bridged);
    }
    // The last actor, collector frame and native scope have all retired. Only
    // owned diagnostic values survive; no second native collection is attempted.
    BRIDGE_CHECK(Bridge.values && Bridge.recoveryCalls == 1);
    BRIDGE_CHECK(Bridge.values->evidence.operation.ticket == Bridge.nativeTicket);
    BRIDGE_CHECK(!Bridge.beforeBind && !Bridge.beforeCapture);
    std::cout << BridgeChecks << " extracted executor/collector handoff checks PASS (test-only instrumentation)\n";
}
