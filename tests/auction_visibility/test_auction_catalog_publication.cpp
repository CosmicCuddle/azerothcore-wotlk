#include "AuctionCatalogPublication.h"
#include <atomic>
#include <cassert>
#include <thread>

using namespace AuctionVisibility;
struct Entry
{
    std::uint32_t Id;
    unsigned bid;
    std::uint64_t bidderGuid;
    Tag visibilityTag;
};
using Catalog = AuctionCatalogPublication::Catalog<Entry, 2>;

int main()
{
    Catalog catalog;
    assert(catalog.Current().policy.generation == 1);
    assert(catalog.Add(0, {1, 10, 10, {}}));
    assert(catalog.Add(1, {2, 20, 20, {}}));
    assert(!catalog.Add(0, {1, 99, 99, {}}));
    auto old = catalog.Capture(0);
    assert(old == catalog.Capture(0));
    assert(old->rows.at(1)->bid == 10);
    assert(catalog.UpdateBid(0, 1, 30u, 30ull));
    assert(old->rows.at(1)->bid == 10);
    auto classify = [](Entry const& row, Policy const& policy) -> std::optional<Tag>
    {
        if (row.Id == 999)
            return std::nullopt;
        return Tag{TagState::Controlled, policy.generation, policy.domain, row.Id % 2, 0};
    };
    assert(catalog.Publish(Mode::Enforced, {2, 1, 3, 0}, classify));
    auto current = catalog.Capture(0);
    assert(current->policy.generation == 2 && current->rows.at(1)->visibilityTag.generation == 2);
    assert(current->rows.at(1)->bid == 30 && current->rows.at(1)->bidderGuid == 30);
    assert(catalog.Capture(1)->rows.at(2)->visibilityTag.generation == 2);
    assert(!catalog.Publish(Mode::Observe, {2, 1, 3, 0}, classify));
    assert(catalog.Add(0, {999, 0, 0, {}}));
    assert(catalog.Capture(0)->rows.at(999)->visibilityTag.state == TagState::Unknown);
    assert(!catalog.Publish(Mode::Enforced, {3, 1, 3, 0}, classify));
    assert(catalog.Current().policy.generation == 2);
    assert(catalog.Remove(0, 999));
    assert(!catalog.UpdateBid(0, 999, 1u, 1ull));
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::thread reader([&]
    {
        started.store(true, std::memory_order_release);
        while (!done.load(std::memory_order_acquire))
        {
            auto view = catalog.Capture(0);
            for (auto const& [id, row] : view->rows)
            {
                assert(id == row->Id);
                assert(row->bid == row->bidderGuid);
                assert(row->visibilityTag.generation == view->policy.generation);
            }
        }
    });
    while (!started.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::thread mutator([&]
    {
        for (unsigned id = 3; id < 103; ++id)
        {
            assert(catalog.Add(0, {id, id, id, {}}));
            assert(catalog.UpdateBid(0, id, id + 100, std::uint64_t(id + 100)));
            if (id % 2 == 0)
                assert(catalog.Remove(0, id));
        }
    });
    for (unsigned generation = 3; generation < 23; ++generation)
        assert(catalog.Publish(Mode::Enforced, {generation, 1, 3, 0}, classify));
    mutator.join();
    done.store(true, std::memory_order_release);
    reader.join();
    auto final = catalog.Capture(0);
    assert(final->policy.generation == 22 && final->rows.size() == 51);
    for (unsigned id = 3; id < 103; ++id)
    {
        assert(final->rows.contains(id) == bool(id % 2));
        if (id % 2)
            assert(final->rows.at(id)->bid == id + 100 && final->rows.at(id)->visibilityTag.generation == 22);
    }
    assert(old->rows.size() == 1 && old->policy.generation == 1);
    assert(current->rows.size() == 1 && current->policy.generation == 2);
    assert(catalog.Publish(Mode::Disabled, {23, 0, 0, 0}, {}));
    assert(catalog.Capture(0)->rows.size() == 51);
    assert(catalog.Capture(0)->rows.at(1)->visibilityTag.state == TagState::Unknown);
    assert(!catalog.Capture(2));
}
