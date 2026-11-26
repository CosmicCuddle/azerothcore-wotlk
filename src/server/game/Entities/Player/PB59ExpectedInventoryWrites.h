#pragma once

#include "PreparedStatement.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace PB59::Recovery
{
// Owned copies of the exact bindings on the staged statements.
struct InventorySaveStatement
{
    std::uint32_t index = 0;
    std::vector<PreparedStatementData> parameters;
};

struct InventoryWriteValues
{
    std::uint64_t itemGuid = 0;
    InventorySaveStatement link;
    InventorySaveStatement instance;
    std::optional<InventorySaveStatement> giftOwner;
};

struct ExpectedInventoryWrites
{
    std::vector<InventoryWriteValues> items;
};
}
