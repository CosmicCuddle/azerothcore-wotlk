#include <cstdlib>
#include <iostream>
#include <new>

static bool FailCollectionAllocation = false;
// Keep the replaceable test allocator out of optimized call sites: otherwise
// GCC can diagnose its intentional malloc/free pair as mismatched new/delete.
[[gnu::noinline]] void* operator new(std::size_t size)
{
    if (FailCollectionAllocation)
        throw std::bad_alloc{};
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc{};
}
[[gnu::noinline]] void operator delete(void* memory) noexcept
{
    std::free(memory);
}
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

using namespace PB59::Recovery;
using Ledger = PB59::TransactionAdmission;
static unsigned CollectionChecks = 0;
#define COLLECT_CHECK(value) do { \
    ++CollectionChecks; \
    if (!(value)) \
    { \
        std::cerr << "collector line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

static std::array<PB59::InventoryBatchMove, 1> CollectorPlan(Player& actor)
{
    actor.Add(65280, 10);
    return {{{*actor.CapturePB59InventorySlot(65280), *actor.CapturePB59InventorySlot(65303)}}};
}
static PB59::InventoryBatchResult RecoveryFailure()
{
    PB59::InventoryBatchResult result;
    result.outcome = PB59::InventoryBatchOutcome::RecoveryRequired;
    result.failure = PB59::InventoryBatchFailure::Effects;
    return result;
}

int main()
{
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto const before = actor.fields;
        FailCollectionAllocation = true;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        FailCollectionAllocation = false;
        COLLECT_CHECK(!buffer && actor.fields == before && actor._pb59Transaction.CanBegin());
        actor.m_itemUpdateQueue.resize(MaximumQueueObservations + 1, nullptr);
        COLLECT_CHECK(!Player::PreparePB59RecoveryCollection(actor, plan));
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && !buffer->CopyValues().evidence.operation.ticket);
        Player unrelated; Ledger::MutationScope wrong(unrelated._pb59Transaction);
        auto const reads = actor.collectionReads;
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, wrong) == CollectionStatus::WrongScope);
        COLLECT_CHECK(actor.collectionReads == reads && !buffer->CopyValues().summary.ticketBound);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction);
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::ContextUnavailable);
        COLLECT_CHECK(actor._pb59Transaction.CanAccess(scope.GetTicket(), Ledger::Access::Forward));
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.GetItemByPos(65280)->facts.text.assign(MaximumTextBytes + 1, 'x');
        plan[0].source = *actor.CapturePB59InventorySlot(65280);
        COLLECT_CHECK(!Player::PreparePB59RecoveryCollection(actor, plan));
        COLLECT_CHECK(actor._pb59Transaction.CanBegin());
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        actor.GetItemByPos(65280)->facts.count = 2; // deliberately stale original before binding
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::ContextUnavailable);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        auto const ticket = scope.GetTicket();
        actor.AdvanceCollectionRevision();
        auto const stamp = actor._pb59Equipment.Capture();
        (void)actor._pb59Transaction.Request(Ledger::Work::StandaloneSave);
        (void)actor._pb59Transaction.Request(Ledger::Work::Logout);
        COLLECT_CHECK(actor._pb59Transaction.RequireRecovery(ticket));
        auto const before = actor.fields;
        auto failure = RecoveryFailure();
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, failure) == CollectionStatus::Collected);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.summary.ticketBound && data.summary.revisionObserved && data.summary.intentsObserved);
        COLLECT_CHECK(data.evidence.operation.ticket == ticket && data.evidence.revision == stamp->revision);
        COLLECT_CHECK(data.evidence.pendingSave && data.evidence.pendingLogout && data.evidence.reportedCount == 0);
        COLLECT_CHECK(data.evidence.originals[0].observed && data.destinations[0].observed);
        COLLECT_CHECK(data.evidence.originals[0].instance == InstanceState::ExactNativeInstance);
        COLLECT_CHECK(data.evidence.effects == ProofState::Unknown && data.evidence.queues == ProofState::Unknown);
        COLLECT_CHECK(Inspect(data.evidence, data.evidence.operation).status == InspectionStatus::NeedsEffectsProof);
        COLLECT_CHECK(actor.fields == before && actor._pb59Equipment.InventoryQuarantined());
        COLLECT_CHECK(actor._pb59Transaction.State() == Ledger::Phase::RecoveryRequired);
        auto const reads = actor.collectionReads;
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, failure) == CollectionStatus::WrongPhase);
        COLLECT_CHECK(actor.collectionReads == reads && buffer->CopyValues().evidence == data.evidence);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        actor.slots.at(65280) = nullptr;
        auto failure = RecoveryFailure(); failure.recoveryCount = 1; failure.recoveryGuids[0] = 10;
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, failure) == CollectionStatus::Collected);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.evidence.reportedCount == 1 && data.evidence.reportedGuids[0] == 10);
        COLLECT_CHECK(Inspect(data.evidence, data.evidence.operation).status == InspectionStatus::NeedsCustodyProof);
    }
    {
        auto actor = std::make_unique<Player>(); auto plan = CollectorPlan(*actor);
        auto buffer = Player::PreparePB59RecoveryCollection(*actor, plan);
        Ledger::MutationScope scope(actor->_pb59Transaction); actor->ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        auto const ticket = scope.GetTicket();
        actor.reset(); // before static collection; no call through a destroyed Player
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::OwnerUnavailable);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.evidence.operation.ticket == ticket && !data.evidence.ownerPresent);
        COLLECT_CHECK(!data.summary.intentsObserved && !data.summary.revisionObserved);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        actor.Add(65280, 10); // deliberate same-value/different-object fault after binding
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.evidence.originals[0].instance == InstanceState::ValueOnly);
        COLLECT_CHECK(Inspect(data.evidence, data.evidence.operation).status == InspectionStatus::NeedsCustodyProof);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        (void)actor._pb59OriginalCombatEffects[0].BeginUnequipped(scope.GetTicket(), 10);
        auto* item = actor.GetItemByPos(65280);
        actor.m_itemUpdateQueue.push_back(item); item->uQueuePos = 99;
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Incomplete);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.summary.journalsObserved && data.summary.nonFreshJournalMask == 1);
        COLLECT_CHECK(data.evidence.effects == ProofState::Uncertain && data.evidence.queues == ProofState::Uncertain);
        COLLECT_CHECK(item->uQueuePos == 99 && actor.m_itemUpdateQueue[0] == item);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        actor.collectionHook = [&]
        {
            actor.collectionStopped = true;
            (void)scope.Finish({true, true, true, true}); // synthetic loss of access at a getter boundary
        };
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::OwnerUnavailable);
        COLLECT_CHECK(actor.readsAfterCollectionStop == 0);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope first(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, first) == CollectionStatus::Bound);
        auto const ticket = first.GetTicket();
        (void)first.Finish({true, true, true, true}); // synthetic ticket reuse attempt, not native reconciliation
        Ledger::MutationScope later(actor._pb59Transaction);
        auto const reads = actor.collectionReads;
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, later, RecoveryFailure()) ==
            CollectionStatus::OwnerUnavailable);
        COLLECT_CHECK(actor.collectionReads == reads && buffer->CopyValues().evidence.operation.ticket == ticket);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        (void)actor._pb59Transaction.Request(Ledger::Work::StandaloneSave);
        actor.collectionHook = [] { throw std::bad_alloc{}; };
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Incomplete);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.evidence.pendingSave && !data.evidence.originals[0].observed);
        COLLECT_CHECK(actor._pb59Equipment.InventoryQuarantined());
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        auto const beforeBind = actor.collectionReads;
        auto status = CollectionStatus::Prepared;
        std::thread foreignBind([&] { status = Player::BindPB59RecoveryCollection(*buffer, scope); });
        foreignBind.join();
        COLLECT_CHECK(status == CollectionStatus::WrongThread && actor.collectionReads == beforeBind);
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        auto const beforeCollect = actor.collectionReads;
        std::thread foreignCollect([&]
        {
            status = Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        });
        foreignCollect.join();
        COLLECT_CHECK(status == CollectionStatus::WrongThread && actor.collectionReads == beforeCollect);
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
    }
    for (unsigned defect = 0; defect < 3; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        if (defect == 0) actor.session.incarnation++;
        if (defect == 1) actor.m_itemUpdateQueue.resize(MaximumQueueObservations + 1, nullptr);
        auto failure = RecoveryFailure();
        if (defect == 2) failure.recoveryCount = 255;
        auto const result = Player::CollectPB59RecoveryEvidence(*buffer, scope, failure);
        COLLECT_CHECK(result == (defect == 0 ? CollectionStatus::ContextUnavailable :
            defect == 1 ? CollectionStatus::Incomplete : CollectionStatus::InvalidInput));
        COLLECT_CHECK(actor._pb59Equipment.InventoryQuarantined());
    }
    for (unsigned defect = 0; defect < 5; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto* item = actor.GetItemByPos(65280);
        actor.m_itemUpdateQueue = {nullptr, item}; item->uQueuePos = 1;
        if (defect == 0) item->uQueuePos = 0;
        if (defect == 1) item->facts.owner = 99;
        if (defect == 2) item->facts.guid = 0;
        if (defect == 3) actor.m_itemUpdateQueue.push_back(item);
        if (defect == 4)
        {
            actor.Add(65310, 10); // different pointer with a duplicate GUID
            auto* duplicate = actor.GetItemByPos(65310);
            duplicate->uQueuePos = 2; actor.m_itemUpdateQueue.push_back(duplicate);
        }
        COLLECT_CHECK(!Player::PreparePB59RecoveryCollection(actor, plan));
        COLLECT_CHECK(actor._pb59Transaction.CanBegin());
    }
    for (unsigned defect = 0; defect < 5; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto* item = actor.GetItemByPos(65280);
        actor.m_itemUpdateQueue = {nullptr, item}; item->uQueuePos = 1;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && buffer->CopyValues().summary.queueBeforeObserved);
        if (defect == 0) actor.m_itemUpdateQueue.push_back(nullptr);
        if (defect == 1) actor.m_itemUpdateQueue[1] = nullptr;
        if (defect == 2) item->uQueuePos = 0;
        if (defect == 3) item->facts.owner = 99;
        if (defect == 4)
        {
            actor.Add(65310, 10); auto* replacement = actor.GetItemByPos(65310);
            replacement->uQueuePos = 1; actor.m_itemUpdateQueue[1] = replacement;
        }
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::ContextUnavailable);
        COLLECT_CHECK(actor._pb59Transaction.CanAccess(scope.GetTicket(), Ledger::Access::Forward));
    }
    for (unsigned defect = 0; defect < 9; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.Add(65310, 800); auto* item = actor.GetItemByPos(65310);
        actor.m_itemUpdateQueue = {nullptr, item}; item->uQueuePos = 1;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        if (defect == 0) item->facts.durability = 1; // queue identity does not claim metadata proof
        if (defect == 1) item->facts.owner = 99;
        if (defect == 2) item->uQueuePos = 0;
        if (defect == 3) item->facts.guid = 801;
        if (defect == 4) actor.m_itemUpdateQueue[1] = nullptr;
        if (defect == 5) actor.m_itemUpdateQueue.resize(1);
        if (defect == 6) actor.m_itemUpdateQueue.push_back(nullptr);
        if (defect == 7)
        {
            actor.Add(65310, 800); auto* replacement = actor.GetItemByPos(65310);
            replacement->uQueuePos = 1; actor.m_itemUpdateQueue[1] = replacement;
        }
        if (defect == 8)
        {
            actor.m_itemUpdateQueue = {item, nullptr}; item->uQueuePos = 0;
        }
        auto const result = Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        auto const data = buffer->CopyValues();
        COLLECT_CHECK(result == (defect == 1 || defect == 2 ?
            CollectionStatus::Incomplete : CollectionStatus::Collected));
        COLLECT_CHECK(data.summary.queueCompared && data.summary.queueUnchanged == (defect == 0));
        COLLECT_CHECK(data.summary.changedQueueEntries == (defect == 0 ? 0u : defect == 8 ? 2u : 1u));
        COLLECT_CHECK(data.evidence.queues == (defect == 0 ? ProofState::Unknown : ProofState::Uncertain));
        if (defect != 0)
        {
            auto const& difference = data.queueDifferences[0];
            COLLECT_CHECK(difference.index == (defect == 6 ? 2u : defect == 8 ? 0u : 1u));
            COLLECT_CHECK(difference.sameNativeInstance == (defect <= 3 || defect == 6));
            if (defect == 7)
                COLLECT_CHECK(difference.before == difference.now); // identical values are insufficient
        }
        COLLECT_CHECK(actor._pb59Equipment.InventoryQuarantined());
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.m_itemUpdateQueue.resize(MaximumQueueObservations, nullptr);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer != nullptr);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        actor.m_itemUpdateQueue.clear(); // 4096 differences, first 19 exported
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(data.summary.queueCompared && !data.summary.queueUnchanged);
        COLLECT_CHECK(data.summary.changedQueueEntries == MaximumQueueObservations);
        COLLECT_CHECK(data.summary.queueDifferenceCount == MaximumOriginals);
        COLLECT_CHECK(data.queueDifferences[MaximumOriginals - 1].index == MaximumOriginals - 1);
        COLLECT_CHECK(data.evidence.queues == ProofState::Uncertain);
    }
    for (bool binding : {false, true})
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto* item = actor.GetItemByPos(65280);
        actor.m_itemUpdateQueue = {item}; item->uQueuePos = 0;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        if (!binding)
            COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        auto const reads = item->collectionQueueReads;
        item->collectionQueueHook = [&] { (void)scope.Finish({true, true, true, true}); };
        auto const result = binding ? Player::BindPB59RecoveryCollection(*buffer, scope) :
            Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        COLLECT_CHECK(result == CollectionStatus::OwnerUnavailable);
        COLLECT_CHECK(item->collectionQueueReads == reads + 1); // no owner getter after sentinel loss at GUID getter
        COLLECT_CHECK(!buffer->CopyValues().summary.queueCompared);
    }
    std::cout << CollectionChecks << " extracted native collector checks PASS (explicit engine doubles)\n";
}
