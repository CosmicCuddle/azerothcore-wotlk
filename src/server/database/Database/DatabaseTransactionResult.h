#pragma once

// Transaction execution outcome, not a durability or storage-engine guarantee.
// In particular a failed statement/rollback can leave nontransactional side
// effects. Callers must not infer safe replay from any non-success status.
enum class DatabaseTransactionStatus
{
    Committed,
    Empty,
    NotStarted,
    Ambiguous,
    FailedUnverified
};

struct DatabaseTransactionResult
{
    DatabaseTransactionStatus status;
    int executionCode;

    constexpr bool IsCommitted() const noexcept
    {
        return status == DatabaseTransactionStatus::Committed;
    }

    static constexpr int EmptyCode = -1;
    static constexpr int AmbiguousCode = -2;
    static constexpr int NotStartedCode = -3;
    // A later attempt did not start, but earlier statements may have run.
    static constexpr int PriorAttemptFailureCode = -4;

    static constexpr DatabaseTransactionResult FromExecutionCode(int code) noexcept
    {
        switch (code)
        {
            case 0: return {DatabaseTransactionStatus::Committed, code};
            case EmptyCode: return {DatabaseTransactionStatus::Empty, code};
            case NotStartedCode: return {DatabaseTransactionStatus::NotStarted, code};
            case AmbiguousCode: return {DatabaseTransactionStatus::Ambiguous, code};
            default: return {DatabaseTransactionStatus::FailedUnverified, code};
        }
    }
};
