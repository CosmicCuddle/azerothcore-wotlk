// CS02 / PB59 -- compile-only probe against REAL Core74 (27b61359) headers.
// Pins the signatures this cross-check reasons about. Nothing is linked or run;
// not a worldserver build. CheckPB59InventoryBatchPlan is check-only (bool) and
// is deliberately NOT used as a move executor anywhere in this lane.
#include "Player.h"
#include "PB59InventoryBatchPlan.h"
#include "PB59TransactionAdmission.h"

#include <span>
#include <type_traits>

static_assert(std::is_same_v<decltype(&Player::CheckPB59InventoryBatchPlan),
    bool (Player::*)(uint64, uint64, uint64, PB59::Stamp const&, std::span<PB59::InventoryBatchMove const>) const>);
static_assert(std::is_same_v<decltype(&Player::CapturePB59InventorySlot),
    std::optional<PB59::InventorySlotFacts> (Player::*)(uint16) const>);
static_assert(std::is_same_v<decltype(&PB59::ValidateInventoryBatchPlan),
    PB59::InventoryBatchPlanError (*)(std::uint64_t, std::span<PB59::InventoryBatchMove const>) noexcept>);
static_assert(std::is_same_v<decltype(&PB59::TransactionAdmission::MutationScope::CanAccess),
    bool (PB59::TransactionAdmission::MutationScope::*)(std::uint64_t, PB59::TransactionAdmission::Access) const noexcept>);
static_assert(std::is_same_v<decltype(&PB59::TransactionAdmission::CanAccess),
    bool (PB59::TransactionAdmission::*)(PB59::TransactionAdmission::Ticket, PB59::TransactionAdmission::Access) const noexcept>);
static_assert(std::is_same_v<decltype(&PB59::TransactionAdmission::Request),
    PB59::TransactionAdmission::Decision (PB59::TransactionAdmission::*)(PB59::TransactionAdmission::Work) noexcept>);
