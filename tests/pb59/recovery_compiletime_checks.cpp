#include "PB59RecoveryProof.h"

using namespace PB59::Recovery;

constexpr Evidence Example()
{
    Evidence evidence;
    evidence.operation = {1, 2, 3, 4};
    evidence.revision = 5;
    evidence.ownerPresent = evidence.sessionPresent = true;
    evidence.observedOwnerIncarnation = 2;
    evidence.observedSessionIncarnation = 3;
    evidence.effects = evidence.queues = ProofState::Verified;
    evidence.originalCount = 1;
    auto& original = evidence.originals[0];
    original.before.owner = original.before.item.owner = 1;
    original.before.position = 65280;
    original.before.hasItem = true;
    original.before.item.guid = 100;
    original.before.item.entry = 200;
    original.before.item.count = 1;
    original.now = original.before;
    original.observed = true;
    original.instance = InstanceState::ExactNativeInstance;
    return evidence;
}

constexpr auto Base = Example();
constexpr NativeProof Complete{Base.operation, Base.revision, true, true, true, true, true, true};
constexpr PersistenceEvidence Durable{Base.operation, Base.revision, 9, PersistenceState::Confirmed};
static_assert(Inspect(Base, Base.operation).status == InspectionStatus::InMemoryCandidate);
static_assert(Assess(Base, Base.operation, Complete, Durable).memory == MemoryAssessment::VerifiedForReview);
static_assert(Assess(Base, Base.operation, Complete, Durable).persistence == PersistenceAssessment::ConfirmedForReview);

constexpr bool RejectMissingNativeProof()
{
    for (auto member : {&NativeProof::serializedContext, &NativeProof::retainedLifetime, &NativeProof::ownerWorldState,
            &NativeProof::exactCustody, &NativeProof::completeEffects, &NativeProof::exactQueues})
    {
        auto proof = Complete;
        proof.*member = false;
        auto const result = Assess(Base, Base.operation, proof, Durable);
        if (result.memory != MemoryAssessment::MissingNativeProof ||
            result.persistence != PersistenceAssessment::Unproven)
            return false;
    }
    return true;
}
static_assert(RejectMissingNativeProof(), "every native proof domain is required");

constexpr bool HoldUncertainEffectsAndInstance()
{
    auto effects = Base;
    effects.effects = ProofState::Uncertain;
    if (Inspect(effects, Base.operation).status != InspectionStatus::NeedsEffectsProof ||
        Inspect(effects, Base.operation).unprovenCount != 0 ||
        Assess(effects, Base.operation, Complete, Durable).memory != MemoryAssessment::HeldForInspection)
        return false;
    auto custody = Base;
    custody.originals[0].instance = InstanceState::ValueOnly;
    return Inspect(custody, Base.operation).status == InspectionStatus::NeedsCustodyProof &&
        Assess(custody, Base.operation, Complete, Durable).memory == MemoryAssessment::HeldForInspection;
}
static_assert(HoldUncertainEffectsAndInstance(), "empty GUIDs and matching values are not full proof");

constexpr bool RejectIncompletePersistence()
{
    auto noSequence = Durable;
    noSequence.commitSequence = 0;
    if (Assess(Base, Base.operation, Complete, noSequence).persistence != PersistenceAssessment::Unproven)
        return false;
    auto stale = Durable;
    ++stale.evidenceRevision;
    if (Assess(Base, Base.operation, Complete, stale).persistence != PersistenceAssessment::StaleEvidence)
        return false;
    for (auto state : {PersistenceState::Unobserved, PersistenceState::Pending, PersistenceState::Failed})
    {
        auto incomplete = Durable;
        incomplete.state = state;
        if (Assess(Base, Base.operation, Complete, incomplete).persistence == PersistenceAssessment::ConfirmedForReview)
            return false;
    }
    return true;
}
static_assert(RejectIncompletePersistence(), "memory verification is not a durable outcome");

constexpr bool PreserveMalformedContext()
{
    for (unsigned defect = 0; defect < 6; ++defect)
    {
        auto evidence = Base;
        evidence.ownerPresent = evidence.sessionPresent = false;
        evidence.pendingSave = evidence.pendingLogout = true;
        evidence.queues = ProofState::Unknown;
        switch (defect)
        {
            case 0: evidence.version = 0; break;
            case 1: evidence.originalCount = 0; break;
            case 2: evidence.originalCount = 20; break;
            case 3: evidence.reportedCount = 255; break;
            case 4: evidence.failure = PB59::InventoryBatchFailure::None; break;
            case 5: evidence.effects = static_cast<ProofState>(255); break;
        }
        auto stale = Base.operation;
        ++stale.ticket;
        auto const result = Inspect(evidence, stale);
        if (result.status != InspectionStatus::InvalidEvidence || !result.Has(Issue::InvalidRecord) ||
            !result.Has(Issue::PendingSave) || !result.Has(Issue::PendingLogout) ||
            !result.Has(Issue::OwnerUnavailable) || !result.Has(Issue::SessionUnavailable) ||
            !result.Has(Issue::StaleOperation) || !result.Has(Issue::QueuesUnproven) || result.unprovenCount)
            return false;
    }
    return true;
}
static_assert(PreserveMalformedContext(), "malformed structure cannot erase known lifecycle/intent diagnostics");

constexpr bool KeepRetirementAdmission()
{
    RetirementObservation value{Base.operation, Base.operation, true, true, true, false, false, true, false, false};
    value.expected.ticket = value.observed.ticket = 0;
    if (InspectRetirement(value) != RetirementAssessment::CheckExistingNativeAdmission)
        return false;
    value.quarantined = true;
    if (InspectRetirement(value) != RetirementAssessment::HoldRecovery)
        return false;
    value.quarantined = false;
    value.pendingSave = true;
    if (InspectRetirement(value) != RetirementAssessment::HoldPendingIntents)
        return false;
    value.pendingSave = false;
    value.sessionPresent = false;
    return InspectRetirement(value) == RetirementAssessment::HoldMissingContext;
}
static_assert(KeepRetirementAdmission(), "inspection never bypasses native retirement admission");
