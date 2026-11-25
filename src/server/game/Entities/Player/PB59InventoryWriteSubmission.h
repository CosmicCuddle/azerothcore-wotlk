#pragma once

#include <cstdint>

namespace PB59
{
struct InventoryWriteTicket
{
    std::uint64_t guid = 0;
    std::uint64_t sequence = 0;
    bool operator==(InventoryWriteTicket const&) const = default;
};

enum class InventoryWriteAdmission { Tracking, Closed, Full, SequenceExhausted, WriterUnavailable, Declined, Uncertain };

struct InventoryWriteSubmission
{
    InventoryWriteAdmission status;
    InventoryWriteTicket ticket{};
};
}
