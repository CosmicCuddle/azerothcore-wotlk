#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace PB59
{
// A queue token is NOT item ownership. The native caller must retain the
// item and exclude saves/retirement until it restores or explicitly disposes it.
template<class T>
struct OriginalQueueCustody
{
    T* item = nullptr;
    std::int32_t index = -1;
    std::uint32_t state = 0;
    bool held = false;
};

template<class T>
bool ExactOriginalQueue(std::vector<T*> const& queue, T const* item,
    std::int32_t index, bool mustBeQueued) noexcept
{
    if (!item || index < -1 || (index >= 0) != mustBeQueued)
        return false;
    auto const occurrences = std::count(queue.begin(), queue.end(), item);
    if (index == -1)
        return occurrences == 0;
    auto const pos = static_cast<std::size_t>(index);
    return pos < queue.size() && queue[pos] == item && occurrences == 1;
}

// No allocation, callbacks, state changes or queue compaction. Preserve the
// exact hole for rollback. The item must be held by the enclosing transaction.
template<class T>
bool SuspendOriginalQueue(std::vector<T*>& queue, T* item, std::int32_t& index,
    std::uint32_t state, bool mustBeQueued, OriginalQueueCustody<T>& token) noexcept
{
    if (token.held || !ExactOriginalQueue(queue, item, index, mustBeQueued))
        return false;
    token = {item, index, state, true};
    if (index >= 0)
        queue[static_cast<std::size_t>(index)] = nullptr;
    index = -1;
    return true;
}

template<class T>
bool RestoreOriginalQueue(std::vector<T*>& queue, T* item, std::int32_t& index,
    std::uint32_t state, OriginalQueueCustody<T>& token) noexcept
{
    if (!token.held || token.item != item || token.state != state || index != -1 ||
        token.index < -1 || !ExactOriginalQueue(queue, item, index, false))
        return false;
    if (token.index >= 0)
    {
        auto const pos = static_cast<std::size_t>(token.index);
        if (pos >= queue.size() || queue[pos])
            return false;
        queue[pos] = item;
        index = token.index;
    }
    token = {};
    return true;
}
}
