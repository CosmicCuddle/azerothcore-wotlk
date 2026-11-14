// Native actual source primitive, not a Worldserver or client ACK.
// The genuine notifier is still compiled OFF by default.
#include "../../src/server/game/Grids/Notifiers/F14VisibilityPassTokenSource.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

int main()
{
    using CosmicF14::VisibilityPassTokenSource;
    int errors = 0;
    auto check = [&](bool value, char const* message) {
        if (!value) { ++errors; std::cerr << "FAIL " << message << '\n'; }
    };
    VisibilityPassTokenSource src;
    check(src.Issue() == 1, "first pass token must be 1");
    check(src.Issue() == 2, "tokens must be strictly monotone");
    check(src.PeekNextForTestOnly() == 3, "next id after two issues");
    constexpr int workers = 8;
    constexpr int perWorker = 600;
    std::mutex guard;
    std::vector<std::uint64_t> values;
    values.reserve(workers * perWorker);
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i)
        threads.emplace_back([&] {
            std::vector<std::uint64_t> local;
            local.reserve(perWorker);
            for (int j = 0; j < perWorker; ++j)
                local.push_back(src.Issue());
            std::lock_guard<std::mutex> lock(guard);
            values.insert(values.end(), local.begin(), local.end());
        });
    for (auto& t : threads) t.join();
    std::set<std::uint64_t> distinct(values.begin(), values.end());
    check(values.size() == workers * perWorker, "parallel issue count");
    check(distinct.size() == values.size(), "no repeated tokens across workers");
    check(*distinct.begin() == 3, "first concurrent token is 3");
    check(*distinct.rbegin() == 2 + workers * perWorker,
          "last concurrent token is expected positive value");

    auto const maxToken = std::numeric_limits<std::uint64_t>::max();
    VisibilityPassTokenSource ending(maxToken - 1);
    check(ending.Issue() == maxToken - 1, "penultimate token accepted once");
    check(ending.Issue() == maxToken, "maximum token accepted once");
    check(ending.Issue() == 0, "saturation must not wrap or recycle");
    check(ending.Issue() == 0, "once exhausted, stays exhausted");
    VisibilityPassTokenSource invalid(0);
    check(invalid.Issue() == 0, "invalid zero source never issues tokens");
    check(invalid.PeekNextForTestOnly() == 0, "zero source remains exhausted");

    std::cout << "CORE F14 real atomic token source: "
              << values.size() << " concurrent issues, "
              << errors << " errors; no packet/creature/client attribution\n";
    return errors == 0 ? 0 : 1;
}
