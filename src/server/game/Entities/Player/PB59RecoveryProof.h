#pragma once

#include "PB59RecoveryEvidence.h"

namespace PB59::Recovery
{
// Prototype inputs only. A future native collector must establish each proof
// independently while the same operation and lifetime are retained. Booleans
// copied from a record, a command or another thread are not that collector.
struct NativeProof
{
    Operation operation;
    std::uint64_t evidenceRevision = 0;
    bool serializedContext = false, retainedLifetime = false, ownerWorldState = false;
    bool exactCustody = false, completeEffects = false, exactQueues = false;
};

enum class PersistenceState : std::uint8_t { Unobserved, Pending, Failed, Confirmed };
struct PersistenceEvidence
{
    Operation operation;
    std::uint64_t evidenceRevision = 0, commitSequence = 0;
    PersistenceState state = PersistenceState::Unobserved;
};
enum class MemoryAssessment { HeldForInspection, StaleProof, MissingNativeProof, VerifiedForReview };
enum class PersistenceAssessment { Unproven, Pending, Failed, StaleEvidence, ConfirmedForReview };
struct ReconciliationAssessment
{
    MemoryAssessment memory = MemoryAssessment::HeldForInspection;
    PersistenceAssessment persistence = PersistenceAssessment::Unproven;
};

// No StableProof/Finish conversion, result replay or native state mutation.
// Even VerifiedForReview + ConfirmedForReview is not an unlock capability.
inline ReconciliationAssessment Assess(Evidence const& evidence, Operation const& expected,
    NativeProof const& native, PersistenceEvidence const& durable) noexcept
{
    ReconciliationAssessment result;
    if (Inspect(evidence, expected).status != InspectionStatus::InMemoryCandidate)
        return result;
    if (native.operation != expected || native.evidenceRevision != evidence.revision)
    {
        result.memory = MemoryAssessment::StaleProof;
        return result;
    }
    if (!native.serializedContext || !native.retainedLifetime || !native.ownerWorldState ||
        !native.exactCustody || !native.completeEffects || !native.exactQueues)
    {
        result.memory = MemoryAssessment::MissingNativeProof;
        return result;
    }
    result.memory = MemoryAssessment::VerifiedForReview;
    if (durable.state == PersistenceState::Unobserved)
        return result;
    if (durable.operation != expected || durable.evidenceRevision != evidence.revision)
        result.persistence = PersistenceAssessment::StaleEvidence;
    else if (durable.state == PersistenceState::Pending)
        result.persistence = PersistenceAssessment::Pending;
    else if (durable.state == PersistenceState::Failed)
        result.persistence = PersistenceAssessment::Failed;
    else if (durable.state == PersistenceState::Confirmed && durable.commitSequence)
        result.persistence = PersistenceAssessment::ConfirmedForReview;
    return result;
}

// Per-owner admission observation, NOT a process-wide shutdown veto. Positive
// output means to consult the existing native producer, never to skip it.
struct RetirementObservation
{
    Operation expected, observed;
    bool ownerPresent = false, sessionPresent = false, serializedContext = false;
    bool quarantined = true, recoveryEvidenceOutstanding = true, ledgerIdle = false;
    bool pendingSave = false, pendingLogout = false;
};
enum class RetirementAssessment
{
    HoldMissingContext, HoldStaleIdentity, HoldRecovery, HoldPendingIntents,
    HoldLedger, CheckExistingNativeAdmission
};
inline RetirementAssessment InspectRetirement(RetirementObservation const& value) noexcept
{
    if (!value.ownerPresent || !value.sessionPresent || !value.serializedContext)
        return RetirementAssessment::HoldMissingContext;
    if (!value.expected.OwnerValid() || value.expected != value.observed)
        return RetirementAssessment::HoldStaleIdentity;
    if (value.quarantined || value.recoveryEvidenceOutstanding)
        return RetirementAssessment::HoldRecovery;
    if (value.pendingSave || value.pendingLogout)
        return RetirementAssessment::HoldPendingIntents;
    if (!value.ledgerIdle)
        return RetirementAssessment::HoldLedger;
    return RetirementAssessment::CheckExistingNativeAdmission;
}
}
