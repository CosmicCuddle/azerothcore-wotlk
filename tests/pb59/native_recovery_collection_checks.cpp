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
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, failure) == CollectionStatus::Incomplete);
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
            CollectionStatus::Incomplete);
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
        bool advanced = false;
        actor.collectionHook = [&]
        {
            if (!advanced)
            {
                advanced = true;
                actor.AdvanceCollectionRevision();
                (void)actor._pb59Transaction.Request(Ledger::Work::StandaloneSave);
            }
        };
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::ContextUnavailable);
        auto data = buffer->CopyValues();
        COLLECT_CHECK(advanced && !data.summary.revisionObserved && data.evidence.revision == 0);
        COLLECT_CHECK(data.evidence.pendingSave && actor._pb59Equipment.InventoryQuarantined());
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::WrongPhase);
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
            COLLECT_CHECK(difference.sameNativeInstance == (defect <= 3));
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
    {
        UpdateMask native;
        native.SetCount(MaximumItemUpdateFields);
        for (unsigned index : {0u, 31u, 32u, 63u, 137u, 255u}) native.SetBit(index);
        ItemUpdateMask value;
        COLLECT_CHECK(CopyUpdateMask(native, MaximumItemUpdateFields, value));
        COLLECT_CHECK(value.fieldCount == MaximumItemUpdateFields);
        COLLECT_CHECK(value.words[0] == 0x80000001u && value.words[1] == 0x80000001u);
        COLLECT_CHECK(value.words[4] == 512u && value.words[7] == 0x80000000u && value.words[6] == 0);
        auto const before = value;
        COLLECT_CHECK(!CopyUpdateMask(native, 0, value) && value == before);
        COLLECT_CHECK(!CopyUpdateMask(native, MaximumItemUpdateFields + 1, value) && value == before);
        COLLECT_CHECK(!CopyUpdateMask(native, 64, value) && value == before);
        native.SetCount(33); native.SetBit(32);
        COLLECT_CHECK(CopyUpdateMask(native, 33, value));
        COLLECT_CHECK(value.words[0] == 0 && value.words[1] == 1 && value.words[7] == 0);
        native.SetCount(MaximumOwnerUpdateFields); native.SetBit(MaximumOwnerUpdateFields - 1);
        UpdateMaskFacts<MaximumOwnerUpdateFields> ownerMask;
        COLLECT_CHECK(CopyUpdateMask(native, MaximumOwnerUpdateFields, ownerMask));
        COLLECT_CHECK(ownerMask.words.back() == 0x80000000u && ownerMask.words[0] == 0);
    }
    for (unsigned defect = 0; defect < 7; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.Add(65310, 800); auto* item = actor.GetItemByPos(65310);
        actor.m_itemUpdateQueue = {item}; item->uQueuePos = 0;
        if (defect == 0) item->_changesMask.SetCount(0);
        if (defect == 1)
        {
            item->m_valuesCount = MaximumItemUpdateFields + 1;
            item->_changesMask.SetCount(item->m_valuesCount);
        }
        if (defect == 2) item->updated = true;
        if (defect == 3) item->uState = static_cast<ItemUpdateState>(99);
        if (defect == 4) actor._changesMask.SetCount(0);
        if (defect == 5) actor.m_objectUpdated = true;
        if (defect == 6)
        {
            actor.m_valuesCount = MaximumOwnerUpdateFields + 1;
            actor._changesMask.SetCount(actor.m_valuesCount);
        }
        COLLECT_CHECK(!Player::PreparePB59RecoveryCollection(actor, plan));
        COLLECT_CHECK(actor._pb59Transaction.CanBegin());
    }
    for (unsigned defect = 0; defect < 7; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.Add(65310, 800); auto* item = actor.GetItemByPos(65310);
        actor.m_itemUpdateQueue = {item}; item->uQueuePos = 0;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        if (defect == 0) item->_changesMask.SetBit(31);
        if (defect == 1) item->uState = ITEM_CHANGED;
        if (defect == 2) item->world = false;
        if (defect == 3) { item->updated = true; actor.map.updates.insert(item); }
        if (defect == 4) actor._changesMask.SetBit(63);
        if (defect == 5) { actor.m_objectUpdated = true; actor.map.updates.insert(&actor); }
        if (defect == 6) item->_changesMask.SetCount(0);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::ContextUnavailable);
        COLLECT_CHECK(actor._pb59Transaction.CanAccess(scope.GetTicket(), Ledger::Access::Forward));
    }
    for (unsigned defect = 0; defect < 14; ++defect)
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.Add(65310, 800); auto* item = actor.GetItemByPos(65310);
        actor.m_itemUpdateQueue = {item}; item->uQueuePos = 0;
        item->_changesMask.SetBit(31);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        if (defect == 1) item->_changesMask.SetBit(63);
        if (defect == 2) item->uState = ITEM_CHANGED;
        if (defect == 3) item->world = false;
        if (defect == 4) { item->updated = true; actor.map.updates.insert(item); }
        if (defect == 5) item->updated = true;
        if (defect == 6) actor.map.updates.insert(item);
        if (defect == 7) item->_changesMask.SetCount(0);
        if (defect == 8) item->uState = static_cast<ItemUpdateState>(99);
        if (defect == 9) actor._changesMask.SetBit(63);
        if (defect == 10) { actor.m_objectUpdated = true; actor.map.updates.insert(&actor); }
        if (defect == 11) actor.m_objectUpdated = true;
        if (defect == 12) actor._changesMask.SetCount(0);
        if (defect == 13) { item->m_valuesCount = 128; item->_changesMask.SetCount(128); }
        auto const fields = actor.fields;
        auto const state = item->uState;
        auto const result = Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        auto const data = buffer->CopyValues();
        bool const incomplete = defect == 5 || defect == 6 || defect == 7 || defect == 8 ||
            defect == 11 || defect == 12;
        COLLECT_CHECK(result == (incomplete ? CollectionStatus::Incomplete : CollectionStatus::Collected));
        COLLECT_CHECK(data.summary.ownerUpdateObserved == (defect != 12));
        if (defect != 12)
        {
            COLLECT_CHECK(data.summary.ownerUpdateConsistent == (defect != 11));
            COLLECT_CHECK(data.summary.ownerUpdateUnchanged == (defect < 9 || defect == 13));
        }
        COLLECT_CHECK(data.summary.queueCompared == (defect != 7 && defect != 12));
        if (defect == 7)
            COLLECT_CHECK(!data.summary.queueConsistent && data.evidence.queues == ProofState::Uncertain);
        if (data.summary.queueCompared)
        {
            bool const itemChanged = (defect > 0 && defect < 9) || defect == 13;
            COLLECT_CHECK(data.summary.queueUnchanged == !itemChanged);
            COLLECT_CHECK(data.summary.changedQueueEntries == (itemChanged ? 1u : 0u));
            COLLECT_CHECK(data.evidence.queues == (defect == 0 ? ProofState::Unknown : ProofState::Uncertain));
        }
        if (defect == 1)
        {
            COLLECT_CHECK(data.queueDifferences[0].before.updates.mask.words[1] == 0);
            COLLECT_CHECK(data.queueDifferences[0].now.updates.mask.words[1] == 0x80000000u);
        }
        COLLECT_CHECK(actor.fields == fields && item->uState == state && item->_changesMask.GetCount() ==
            (defect == 7 ? 0u : defect == 13 ? 128u : 64u));
        COLLECT_CHECK(actor._pb59Equipment.InventoryQuarantined());
    }
    for (bool binding : {false, true})
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        if (!binding)
            COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        auto const reads = actor.map.collectionMapReads;
        actor.map.collectionMapHook = [&]
        {
            actor.collectionStopped = true;
            (void)scope.Finish({true, true, true, true});
        };
        auto const result = binding ? Player::BindPB59RecoveryCollection(*buffer, scope) :
            Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        COLLECT_CHECK(result == CollectionStatus::OwnerUnavailable);
        COLLECT_CHECK(actor.map.collectionMapReads == reads + 1 && actor.readsAfterCollectionStop == 0);
        COLLECT_CHECK(!buffer->CopyValues().summary.ownerUpdateObserved);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        actor.Add(65310, 800); auto* item = actor.GetItemByPos(65310);
        actor.m_itemUpdateQueue = {item}; item->uQueuePos = 0;
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        FailCollectionAllocation = true;
        auto const result = Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        FailCollectionAllocation = false;
        COLLECT_CHECK(result == CollectionStatus::Collected);
        COLLECT_CHECK(buffer->CopyValues().summary.ownerUpdateUnchanged && buffer->CopyValues().summary.queueUnchanged);
    }
    {
        ItemFieldValues values;
        unsigned reads = 0;
        auto getter = [&](unsigned index) { ++reads; return index + 100; };
        COLLECT_CHECK(!CopyItemFieldValues(0, getter, values) && reads == 0);
        COLLECT_CHECK(!CopyItemFieldValues(MaximumItemUpdateFields + 1, getter, values) && reads == 0);
        COLLECT_CHECK(CopyItemFieldValues(MaximumItemUpdateFields, getter, values));
        COLLECT_CHECK(reads == MaximumItemUpdateFields && values.fields.back() == MaximumItemUpdateFields + 99);
        COLLECT_CHECK(CopyItemFieldValues(33, getter, values));
        COLLECT_CHECK(values.fieldCount == 33 && values.fields[32] == 132 && values.fields.back() == 0);
        auto const before = values;
        bool threw = false;
        try { (void)CopyItemFieldValues(64, [](unsigned) -> unsigned { throw std::bad_alloc{}; }, values); }
        catch (std::bad_alloc const&) { threw = true; }
        COLLECT_CHECK(threw && values == before);
    }
    for (bool beforeBind : {false, true})
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto* original = actor.GetItemByPos(65280);
        original->_changesMask.SetBit(31); // changing a value under this bit must still be detected
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer->CopyValues().summary.watchedItems == 1);
        COLLECT_CHECK(!buffer->CopyWatchedItem(MaximumWatchedItems));
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        if (!beforeBind)
            COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        original->rawFields[31] = 0x1234;
        if (beforeBind)
            COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::ContextUnavailable);
        else
        {
            COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
                CollectionStatus::Collected);
            auto data = buffer->CopyValues(); auto watched = buffer->CopyWatchedItem(0);
            COLLECT_CHECK(data.summary.observedWatchedItems == 1 && data.summary.changedWatchedItems == 1);
            COLLECT_CHECK(watched && watched->observed && watched->sameNativeInstance && !watched->unchanged);
            COLLECT_CHECK(watched->before.values.fields[31] == 0 && watched->now.values.fields[31] == 0x1234);
            COLLECT_CHECK(watched->before.state.updates.mask == watched->now.state.updates.mask);
            COLLECT_CHECK(data.summary.queueUnchanged && data.evidence.queues == ProofState::Uncertain);
        }
        COLLECT_CHECK(original->rawFields[31] == 0x1234);
    }
    {
        Player actor; auto plan = CollectorPlan(actor);
        auto* original = actor.GetItemByPos(65280);
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        actor.slots.at(65280) = nullptr; actor.slots.at(65303) = original;
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
        auto watched = buffer->CopyWatchedItem(0);
        COLLECT_CHECK(watched && watched->observed && watched->sameNativeInstance);
        COLLECT_CHECK(buffer->CopyValues().summary.observedWatchedItems == 1);
    }
    for (unsigned defect = 0; defect < 7; ++defect)
    {
        Player actor;
        Bag bag(5000, &actor.map), replacement(5000, &actor.map);
        bag.m_valuesCount = CONTAINER_END; bag._changesMask.SetCount(CONTAINER_END);
        actor.bags[19] = &bag;
        auto* original = actor.Add(65280, 10);
        std::array<PB59::InventoryBatchMove, 1> plan{{{*actor.CapturePB59InventorySlot(65280),
            *actor.CapturePB59InventorySlot(19 << 8)}}};
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && buffer->CopyValues().summary.watchedItems == 2);
        COLLECT_CHECK(buffer->CopyWatchedItem(1)->before.values.fieldCount == CONTAINER_END);
        COLLECT_CHECK(buffer->CopyWatchedItem(1)->container);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        if (defect == 1) bag.rawFields[CONTAINER_END - 1] = 0x1234;
        if (defect == 2) bag._changesMask.SetBit(137);
        if (defect == 3) bag.uState = ITEM_CHANGED;
        if (defect == 4) actor.bags[19] = &replacement;
        if (defect == 5) bag._changesMask.SetCount(0);
        if (defect == 6) { bag.updated = true; actor.map.updates.insert(&bag); }
        auto const reads = replacement.collectionFieldReads;
        auto const result = Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure());
        auto watched = buffer->CopyWatchedItem(1); auto data = buffer->CopyValues();
        COLLECT_CHECK(result == (defect == 4 || defect == 5 ?
            CollectionStatus::Incomplete : CollectionStatus::Collected));
        COLLECT_CHECK(data.summary.observedWatchedItems == (defect == 4 || defect == 5 ? 1u : 2u));
        COLLECT_CHECK(data.evidence.queues == (defect == 0 ? ProofState::Unknown : ProofState::Uncertain));
        COLLECT_CHECK(watched->observed == (defect != 4 && defect != 5));
        if (defect == 1)
            COLLECT_CHECK(watched->now.values.fields[CONTAINER_END - 1] == 0x1234);
        COLLECT_CHECK(replacement.collectionFieldReads == reads); // never inspect the replacement as the original
        COLLECT_CHECK(original == actor.GetItemByPos(65280) && actor._pb59Equipment.InventoryQuarantined());
    }
    {
        Player actor;
        Bag bag(5000, &actor.map); actor.bags[19] = &bag;
        actor.Add(65280, 10); actor.Add(65281, 11);
        std::array<PB59::InventoryBatchMove, 2> plan{{{*actor.CapturePB59InventorySlot(65280),
            *actor.CapturePB59InventorySlot(19 << 8)}, {*actor.CapturePB59InventorySlot(65281),
            *actor.CapturePB59InventorySlot((19 << 8) | 1)}}};
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && buffer->CopyValues().summary.watchedItems == 3); // one shared bag, two originals
        COLLECT_CHECK(!buffer->CopyWatchedItem(3));
    }
    {
        Player actor;
        Bag bag(5000, &actor.map); actor.bags[19] = &bag;
        actor.Add(19 << 8, 10);
        std::array<PB59::InventoryBatchMove, 1> plan{{{*actor.CapturePB59InventorySlot(19 << 8),
            *actor.CapturePB59InventorySlot(65280)}}};
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && buffer->CopyValues().summary.watchedItems == 2);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
        COLLECT_CHECK(buffer->CopyWatchedItem(0)->observed && buffer->CopyWatchedItem(1)->observed);
    }
    {
        Player actor;
        Bag bag19(5000, &actor.map), bag20(5001, &actor.map), bag21(5002, &actor.map), bag22(5003, &actor.map);
        std::array<Bag*, 4> bags{&bag19, &bag20, &bag21, &bag22};
        for (unsigned index = 0; index < bags.size(); ++index)
        {
            actor.bags[19 + index] = bags[index];
            bags[index]->m_valuesCount = CONTAINER_END; bags[index]->_changesMask.SetCount(CONTAINER_END);
        }
        std::vector<PB59::InventoryBatchMove> plan;
        for (unsigned slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD) continue;
            auto const source = std::uint16_t(65280 + slot);
            auto const count = plan.size();
            auto const destination = count < 16 ? std::uint16_t(((19 + count / 4) << 8) | (count % 4)) : 65303;
            actor.Add(source, 10 + slot);
            plan.push_back({*actor.CapturePB59InventorySlot(source), *actor.CapturePB59InventorySlot(destination)});
        }
        auto buffer = Player::PreparePB59RecoveryCollection(actor, plan);
        COLLECT_CHECK(buffer && buffer->CopyValues().summary.watchedItems == plan.size() + 4);
        Ledger::MutationScope scope(actor._pb59Transaction); actor.ArmCollection();
        COLLECT_CHECK(Player::BindPB59RecoveryCollection(*buffer, scope) == CollectionStatus::Bound);
        COLLECT_CHECK(Player::CollectPB59RecoveryEvidence(*buffer, scope, RecoveryFailure()) ==
            CollectionStatus::Collected);
        COLLECT_CHECK(buffer->CopyValues().summary.observedWatchedItems == plan.size() + 4);
    }
    std::cout << CollectionChecks << " extracted native collector checks PASS (explicit engine doubles)\n";
}
