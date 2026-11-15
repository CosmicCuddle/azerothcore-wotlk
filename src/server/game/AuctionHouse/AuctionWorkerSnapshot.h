#pragma once

#include <memory>

namespace AuctionWorkerSnapshot
{
// A worker replaces its own map pointer, never mutates an entry shared with
// other workers. The old entry stays alive for any reader holding a copy.
template<class Entry, class Bid, class Bidder>
std::shared_ptr<Entry const> WithBid(std::shared_ptr<Entry const> const& previous,
    Bid bid, Bidder const& bidder)
{
    auto next = std::make_shared<Entry>(*previous);
    next->bid = bid;
    next->bidderGuid = bidder;
    return next;
}
}
