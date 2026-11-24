#pragma once

#include "PB59CharacterSaveObserver.h"
#include "PB59RecoverySaveProvenance.h"
#include "PB59RecoveryProof.h"

// Deliberate engine boundary doubles: markers replace native transaction
// construction/append. No database/SQL or production save body is executed.
using CharacterDatabaseTransaction = int;
constexpr int DELAYED_SAVE_PLAYER = 1;
struct SaveBoundaryCounters
{
    unsigned transactionBoundary = 0, appendBoundary = 0, scheduled = 0, logs = 0;
};
static SaveBoundaryCounters SaveBoundary;
#define LOG_ERROR(...) do { ++SaveBoundary.logs; } while (false)
