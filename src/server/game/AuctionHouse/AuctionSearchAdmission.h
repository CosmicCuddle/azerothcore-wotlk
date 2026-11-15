#pragma once

#include <cstddef>

namespace AuctionSearchAdmission
{
// Externally serialized by the searcher's admission mutex. Count requests
// through completion (queued, running, or waiting for world-side response).
class Budget
{
public:
    Budget(std::size_t totalLimit, std::size_t browseLimit)
        : _totalLimit(totalLimit), _browseLimit(browseLimit) { }

    bool CanAcquire(bool browse) const
    {
        return !_closed && _total < _totalLimit && (!browse || _browse < _browseLimit);
    }

    bool Acquire(bool browse)
    {
        if (!CanAcquire(browse))
            return false;
        ++_total;
        if (browse)
            ++_browse;
        return true;
    }

    bool Release(bool browse)
    {
        if ((browse && !_browse) || (!browse && _total == _browse))
            return false;
        --_total;
        if (browse)
            --_browse;
        return true;
    }

    void Close() { _closed = true; }
    std::size_t Total() const { return _total; }
    std::size_t Browse() const { return _browse; }

private:
    std::size_t const _totalLimit;
    std::size_t const _browseLimit;
    std::size_t _total = 0;
    std::size_t _browse = 0;
    bool _closed = false;
};
}
