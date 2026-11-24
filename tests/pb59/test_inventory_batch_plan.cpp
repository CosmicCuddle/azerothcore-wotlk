#include "PB59InventoryBatchPlan.h"
#include <array>
#include <cstdio>

int main()
{
    unsigned checks = 0;
    auto check = [&](bool value)
    {
        ++checks;
        if (!value)
            std::printf("FAIL %u\n", checks);
        return value;
    };
    PB59::InventoryInstanceFacts item;
    item.guid = 10; item.owner = 1; item.entry = 20; item.count = 1;
    PB59::InventoryBatchMove first{{1, 0, 256, item}, {1, 0, 280, std::nullopt}};
    auto second = first;
    second.source.position = 257; second.source.item->guid = 11;
    second.destination.position = 281;
    std::array moves{first, second};
    using Error = PB59::InventoryBatchPlanError;
    auto validate = [&] { return PB59::ValidateInventoryBatchPlan(1, moves); };
    bool passed = check(validate() == Error::None);
    passed &= check(PB59::ValidateInventoryBatchPlan(1, {}) == Error::Empty);
    passed &= check(PB59::ValidateInventoryBatchPlan(0, moves) == Error::Owner);
    auto reject = [&](auto mutate, Error expected)
    {
        moves = {first, second};
        mutate(moves[1]);
        passed &= check(validate() == expected);
    };
    reject([](auto& move) { move.source.owner = 2; }, Error::Owner);
    reject([](auto& move) { move.destination.owner = 2; }, Error::Owner);
    reject([](auto& move) { move.source.item.reset(); }, Error::Source);
    reject([](auto& move) { move.source.item->owner = 2; }, Error::Source);
    reject([](auto& move) { move.source.item->guid = 0; }, Error::Source);
    reject([](auto& move) { move.source.item->count = 0; }, Error::Source);
    reject([](auto& move) { move.source.item->entry = 0; }, Error::Source);
    reject([&](auto& move) { move.destination.item = item; }, Error::Destination);
    reject([](auto& move) { move.source.item->guid = 10; }, Error::DuplicateItem);
    for (auto position : {uint16_t(256), uint16_t(280), uint16_t(281)})
        reject([&](auto& move) { move.source.position = position; }, Error::Overlap);
    for (auto position : {uint16_t(256), uint16_t(257), uint16_t(280)})
        reject([&](auto& move) { move.destination.position = position; }, Error::Overlap);
    std::array<PB59::InventoryBatchMove, 20> oversized{};
    passed &= check(PB59::ValidateInventoryBatchPlan(1, oversized) == Error::TooLarge);
    std::printf("%u actual batch-plan header checks %s\n", checks, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
