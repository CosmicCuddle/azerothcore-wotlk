#pragma once

// Fixture-only hooks, inserted into a temporary copy of the actual executor.
// Only owned diagnostics are exported. Test fault hooks are cleared before the
// call returns to its owner; they never retain the actual native scope.
struct ExecutorRecoveryBridge
{
    bool prepared = false, scopeLiveBefore = false, scopeLiveAfter = false;
    bool effectsObserved = false, effectsSame = false;
    unsigned recoveryCalls = 0;
    std::uint64_t nativeTicket = 0;
    PB59::Recovery::CollectionStatus bindStatus = PB59::Recovery::CollectionStatus::Prepared;
    PB59::Recovery::CollectionStatus collectStatus = PB59::Recovery::CollectionStatus::Prepared;
    std::optional<PB59::Recovery::CollectedValues> values;
    std::function<void()> beforeBind;
    std::function<void(PB59::TransactionAdmission::MutationScope&)> beforeCapture;
};
static ExecutorRecoveryBridge Bridge;
