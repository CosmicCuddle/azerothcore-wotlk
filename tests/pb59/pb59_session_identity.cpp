#include "PB59SessionIdentity.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

int main()
{
    static_assert(!std::is_copy_constructible_v<PB59::SessionIdentity>);
    static_assert(!std::is_move_constructible_v<PB59::SessionIdentity>);
    PB59::SessionIdentity first, second;
    assert(first.Get() && second.Get() && first.Get() != second.Get());
    // Reconstruct a session identity at the exact same storage address.
    alignas(PB59::SessionIdentity) std::byte storage[sizeof(PB59::SessionIdentity)];
    auto* p = std::construct_at(reinterpret_cast<PB59::SessionIdentity*>(storage));
    auto old = p->Get();
    std::destroy_at(p);
    p = std::construct_at(reinterpret_cast<PB59::SessionIdentity*>(storage));
    assert(p->Get() && p->Get() != old);
    std::destroy_at(p);
    // Competing allocations must not alias, even across different owner contexts.
    std::array<std::vector<std::uint64_t>, 8> values;
    std::array<std::thread, 8> workers;
    for (std::size_t i = 0; i < workers.size(); ++i)
        workers[i] = std::thread([&, i] {
            for (int n = 0; n < 1000; ++n)
            {
                PB59::SessionIdentity identity;
                values[i].push_back(identity.Get());
            }
        });
    for (auto& worker : workers) worker.join();
    std::vector<std::uint64_t> all{first.Get(), second.Get(), old};
    for (auto const& list : values) all.insert(all.end(), list.begin(), list.end());
    std::sort(all.begin(), all.end());
    assert(all.front() && std::adjacent_find(all.begin(), all.end()) == all.end());
    PB59::SessionIncarnationSequence exhausted(std::numeric_limits<std::uint64_t>::max() - 1);
    std::array<std::uint64_t, 8> last{};
    for (std::size_t i = 0; i < workers.size(); ++i)
        workers[i] = std::thread([&, i] { last[i] = exhausted.Issue(); });
    for (auto& worker : workers) worker.join();
    assert(std::count(last.begin(), last.end(), std::numeric_limits<std::uint64_t>::max()) == 1);
    assert(std::count(last.begin(), last.end(), 0) == 7 && exhausted.Issue() == 0);
}
