#include "PB59RecoveryProof.h"
#include "PB59TransactionAdmission.h"
#include <cstdlib>
#include <iostream>
#include <type_traits>

using namespace PB59::Recovery;
static unsigned Checks = 0;
#define CHECK(value) do { \
    ++Checks; \
    if (!(value)) \
    { \
        std::cerr << "line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

static PB59::InventorySlotFacts NativeSlot(unsigned index = 0)
{
    PB59::InventoryInstanceFacts item;
    item.guid = 100 + index;
    item.owner = 1;
    item.creator = 11;
    item.giftCreator = 12;
    item.entry = 200;
    item.count = 1;
    item.duration = 2;
    item.flags = 3;
    item.durability = 4;
    item.maxDurability = 5;
    item.playedTime = 6;
    item.suffix = 7;
    item.randomProperty = -8;
    item.spellCharges = {1, 2, 3, 4, 5};
    item.enchants[0] = {10, 20, 30};
    item.text = std::string("a\0b", 3);
    return {1, 0, static_cast<std::uint16_t>(65280 + index), item};
}

static Evidence Record()
{
    Evidence record;
    record.operation = {1, 2, 3, 4};
    record.revision = 5;
    record.originalCount = 2;
    record.ownerPresent = record.sessionPresent = true;
    record.observedOwnerIncarnation = 2;
    record.observedSessionIncarnation = 3;
    record.effects = record.queues = ProofState::Verified;
    for (unsigned index = 0; index < record.originalCount; ++index)
    {
        auto& item = record.originals[index];
        if (!CopySlot(NativeSlot(index), item.before))
            std::abort();
        item.now = item.before;
        item.observed = true;
        item.instance = InstanceState::ExactNativeInstance;
    }
    return record;
}

int main()
{
    static_assert(std::is_trivially_copyable_v<Evidence>);
    static_assert(!std::is_convertible_v<ReconciliationAssessment, PB59::TransactionAdmission::StableProof>);
    auto record = Record();
    auto const expected = record.operation;
    auto const untouched = record;
    CHECK(Inspect(record, expected).status == InspectionStatus::InMemoryCandidate);
    CHECK(record == untouched);
    auto inspect = [&](auto change)
    {
        auto next = record;
        change(next);
        return Inspect(next, expected);
    };
    CHECK(inspect([](auto& v) { v.effects = ProofState::Uncertain; }).status == InspectionStatus::NeedsEffectsProof);
    CHECK(inspect([](auto& v) { v.queues = ProofState::Unknown; }).status == InspectionStatus::NeedsQueueProof);
    for (auto flag : {&Evidence::pendingSave, &Evidence::pendingLogout})
        CHECK(inspect([&](auto& v) { v.*flag = true; }).status == InspectionStatus::HeldForPendingIntents);
    auto lost = inspect([](auto& v)
    {
        v.ownerPresent = false;
        v.sessionPresent = false;
        v.pendingLogout = v.pendingSave = true;
        v.effects = ProofState::Unknown;
    });
    CHECK(lost.status == InspectionStatus::OwnerUnavailable);
    CHECK(lost.Has(Issue::SessionUnavailable) && lost.Has(Issue::PendingLogout) && lost.Has(Issue::PendingSave));
    CHECK(lost.Has(Issue::EffectsUnproven));
    CHECK(inspect([](auto& v) { v.sessionPresent = false; }).status == InspectionStatus::SessionUnavailable);
    for (auto member : {&Operation::owner, &Operation::ownerIncarnation,
            &Operation::sessionIncarnation, &Operation::ticket})
    {
        auto stale = expected;
        ++(stale.*member);
        CHECK(Inspect(record, stale).status == InspectionStatus::StaleOperation);
    }
    CHECK(inspect([](auto& v) { ++v.observedOwnerIncarnation; }).status == InspectionStatus::StaleOperation);
    CHECK(inspect([](auto& v) { ++v.observedSessionIncarnation; }).status == InspectionStatus::StaleOperation);
    CHECK(inspect([](auto& v) { v.originals[0].observed = false; }).status == InspectionStatus::NeedsFacts);
    for (auto state : {InstanceState::Unknown, InstanceState::ValueOnly})
        CHECK(inspect([&](auto& v) { v.originals[0].instance = state; }).status == InspectionStatus::NeedsCustodyProof);
    CHECK(inspect([](auto& v) { ++v.originals[0].now.bag; }).status == InspectionStatus::NeedsCustodyProof);
    CHECK(inspect([](auto& v) { ++v.originals[0].now.position; }).status != InspectionStatus::InMemoryCandidate);
    CHECK(inspect([](auto& v) { v.originals[1].before = v.originals[0].before; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originals[1].now = v.originals[0].now; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originalCount = 0; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originalCount = 20; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.reportedCount = 20; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.version = 2; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.revision = 0; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.failure = PB59::InventoryBatchFailure::None; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.failure = static_cast<PB59::InventoryBatchFailure>(255); }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.outcome = PB59::InventoryBatchOutcome::Applied; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.reportedCount = 1; v.reportedGuids[0] = 999; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.reportedCount = 2; v.reportedGuids[0] = v.reportedGuids[1] = 100; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originals[0].before.item.text[511] = 'x'; }).status ==
        InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.effects = static_cast<ProofState>(255); }).status ==
        InspectionStatus::InvalidEvidence);
    // Every scalar field copied from the actual native instance participates in
    // equality. This cannot be replaced by GUID-only or item-entry-only proof.
    for (auto member : {&PB59::InventoryInstanceFacts::entry, &PB59::InventoryInstanceFacts::count,
            &PB59::InventoryInstanceFacts::duration, &PB59::InventoryInstanceFacts::flags,
            &PB59::InventoryInstanceFacts::durability, &PB59::InventoryInstanceFacts::maxDurability,
            &PB59::InventoryInstanceFacts::playedTime, &PB59::InventoryInstanceFacts::suffix})
    {
        auto next = record;
        auto native = NativeSlot();
        ++(native.item.value().*member);
        CHECK(CopySlot(native, next.originals[0].now));
        CHECK(Inspect(next, expected).status == InspectionStatus::NeedsCustodyProof);
    }
    for (auto member : {&PB59::InventoryInstanceFacts::guid, &PB59::InventoryInstanceFacts::creator,
            &PB59::InventoryInstanceFacts::giftCreator})
    {
        auto next = record;
        auto native = NativeSlot();
        native.item.value().*member += 1000;
        CHECK(CopySlot(native, next.originals[0].now));
        CHECK(Inspect(next, expected).status == InspectionStatus::NeedsCustodyProof);
    }
    CHECK(inspect([](auto& v) { ++v.originals[0].now.item.randomProperty; }).status ==
        InspectionStatus::NeedsCustodyProof);
    CHECK(inspect([](auto& v) { ++v.originals[0].now.item.spellCharges[4]; }).status ==
        InspectionStatus::NeedsCustodyProof);
    CHECK(inspect([](auto& v) { ++v.originals[0].now.item.enchants[11].charges; }).status ==
        InspectionStatus::NeedsCustodyProof);
    CHECK(inspect([](auto& v) { v.originals[0].now.item.text[0] = 'b'; }).status ==
        InspectionStatus::NeedsCustodyProof);
    auto native = NativeSlot();
    SlotFacts bounded;
    CHECK(CopySlot(native, bounded) && bounded.item.textSize == 3 && bounded.item.text[2] == 'b');
    native.item->text.assign(MaximumTextBytes, 'x');
    CHECK(CopySlot(native, bounded) && bounded.item.textSize == MaximumTextBytes);
    auto const beforeCopy = bounded;
    native.item->text.push_back('x');
    CHECK(!CopySlot(native, bounded) && bounded == beforeCopy);
    auto uncertain = inspect([](auto& v)
    {
        v.originals[0].observed = false;
        v.effects = ProofState::Uncertain;
        v.pendingLogout = true;
    });
    CHECK(uncertain.unprovenCount == 1 && uncertain.unprovenGuids[0] == 100);
    CHECK(uncertain.Has(Issue::EffectsUnproven) && uncertain.Has(Issue::PendingLogout));
    auto full = record;
    full.originalCount = full.reportedCount = MaximumOriginals;
    for (unsigned index = 0; index < MaximumOriginals; ++index)
    {
        auto& item = full.originals[index];
        CHECK(CopySlot(NativeSlot(index), item.before));
        item.now = item.before;
        item.observed = true;
        item.instance = InstanceState::ExactNativeInstance;
        full.reportedGuids[index] = item.before.item.guid;
    }
    CHECK(Inspect(full, expected).status == InspectionStatus::InMemoryCandidate);
    for (auto& item : full.originals)
        item.observed = false;
    CHECK(Inspect(full, expected).unprovenCount == MaximumOriginals);
    CHECK(Inspect(full, expected).status == InspectionStatus::NeedsFacts);
    CHECK(inspect([](auto& v) { v.originalCount = 255; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originals[0].before.hasItem = false; }).status == InspectionStatus::InvalidEvidence);
    CHECK(inspect([](auto& v) { v.originals[0].instance = static_cast<InstanceState>(255); }).status ==
        InspectionStatus::InvalidEvidence);
    NativeProof proof{expected, record.revision, true, true, true, true, true, true};
    PersistenceEvidence durable{expected, record.revision, 9, PersistenceState::Confirmed};
    CHECK(Assess(record, expected, proof, durable).memory == MemoryAssessment::VerifiedForReview);
    CHECK(Assess(record, expected, proof, durable).persistence == PersistenceAssessment::ConfirmedForReview);
    for (auto member : {&NativeProof::serializedContext, &NativeProof::retainedLifetime, &NativeProof::ownerWorldState,
            &NativeProof::exactCustody, &NativeProof::completeEffects, &NativeProof::exactQueues})
    {
        auto missing = proof;
        missing.*member = false;
        auto result = Assess(record, expected, missing, durable);
        CHECK(result.memory == MemoryAssessment::MissingNativeProof);
        CHECK(result.persistence == PersistenceAssessment::Unproven);
    }
    auto staleProof = proof;
    ++staleProof.evidenceRevision;
    CHECK(Assess(record, expected, staleProof, durable).memory == MemoryAssessment::StaleProof);
    staleProof = proof;
    ++staleProof.operation.ticket;
    CHECK(Assess(record, expected, staleProof, durable).memory == MemoryAssessment::StaleProof);
    for (auto state : {PersistenceState::Unobserved, PersistenceState::Pending, PersistenceState::Failed})
    {
        auto pending = durable;
        pending.state = state;
        auto result = Assess(record, expected, proof, pending);
        CHECK(result.memory == MemoryAssessment::VerifiedForReview);
        CHECK(result.persistence != PersistenceAssessment::ConfirmedForReview);
    }
    auto staleDurable = durable;
    ++staleDurable.operation.sessionIncarnation;
    CHECK(Assess(record, expected, proof, staleDurable).persistence == PersistenceAssessment::StaleEvidence);
    staleDurable = durable;
    staleDurable.commitSequence = 0;
    CHECK(Assess(record, expected, proof, staleDurable).persistence == PersistenceAssessment::Unproven);
    staleDurable = durable;
    staleDurable.state = static_cast<PersistenceState>(255);
    CHECK(Assess(record, expected, proof, staleDurable).persistence == PersistenceAssessment::Unproven);
    auto pending = record;
    pending.pendingSave = true;
    CHECK(Assess(pending, expected, proof, durable).memory == MemoryAssessment::HeldForInspection);
    pending = record;
    pending.effects = ProofState::Uncertain;
    CHECK(Assess(pending, expected, proof, durable).memory == MemoryAssessment::HeldForInspection);
    RetirementObservation retirement{expected, expected, true, true, true, false, false, true, false, false};
    CHECK(InspectRetirement(retirement) == RetirementAssessment::CheckExistingNativeAdmission);
    retirement.expected.ticket = retirement.observed.ticket = 0; // ordinary owner with no batch history
    CHECK(InspectRetirement(retirement) == RetirementAssessment::CheckExistingNativeAdmission);
    retirement.sessionPresent = false;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldMissingContext);
    retirement.sessionPresent = true;
    retirement.quarantined = true;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldRecovery);
    retirement.quarantined = false;
    retirement.recoveryEvidenceOutstanding = true;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldRecovery);
    retirement.recoveryEvidenceOutstanding = false;
    retirement.pendingLogout = true;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldPendingIntents);
    retirement.pendingLogout = false;
    retirement.ledgerIdle = false;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldLedger);
    retirement.ownerPresent = false;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldMissingContext);
    retirement.ownerPresent = true;
    ++retirement.observed.ownerIncarnation;
    CHECK(InspectRetirement(retirement) == RetirementAssessment::HoldStaleIdentity);
    CHECK(record == untouched);
    std::cout << Checks << " recovery evidence/proof prototype checks PASS\n";
}
