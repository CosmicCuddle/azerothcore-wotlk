#include "AuctionWorkerSnapshot.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <unordered_map>

struct Entry
{
    std::uint32_t id;
    std::uint32_t bid;
    std::uint64_t bidderGuid;
};
using Snapshot = std::shared_ptr<Entry const>;

int main()
{
    Snapshot original = std::make_shared<Entry>(Entry{42, 0, 0});
    auto const changed = AuctionWorkerSnapshot::WithBid(original, 7u, 7ull);
    assert(original->bid == 0 && original->bidderGuid == 0);
    assert(changed->id == 42 && changed->bid == 7 && changed->bidderGuid == 7);
    std::array<std::unordered_map<unsigned, Snapshot>, 4> maps;
    for (auto& map : maps)
        map.emplace(42, original);
    std::atomic<bool> start{false};
    std::array<std::thread, 4> threads;
    for (unsigned i = 0; i < threads.size(); ++i)
        threads[i] = std::thread([&, i]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            auto& map = maps[i];
            for (unsigned bid = 1; bid <= 10000; ++bid)
            {
                auto old = map.at(42);
                map.at(42) = AuctionWorkerSnapshot::WithBid(old, bid, std::uint64_t(bid));
                assert(old->bid == bid - 1 && old->bidderGuid == bid - 1);
                assert(map.at(42)->bid == bid && map.at(42)->bidderGuid == bid);
                assert(original->bid == 0 && original->bidderGuid == 0);
            }
            auto held = map.at(42);
            map.erase(42);
            assert(held->bid == 10000 && held->bidderGuid == 10000);
        });
    start.store(true, std::memory_order_release);
    for (auto& thread : threads)
        thread.join();
    for (auto const& map : maps)
        assert(map.empty());
}
