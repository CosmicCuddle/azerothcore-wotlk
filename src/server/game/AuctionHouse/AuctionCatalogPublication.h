#pragma once

#include "AuctionVisibility.h"
#include "AuctionWorkerSnapshot.h"
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace AuctionCatalogPublication
{
// Source-side catalogue. Workers receive a pinned View, not this object or
// its classifier. One lock serializes add/remove/bid, capture and policy swap.
// No per-worker mutable catalogue or replay queue is needed.
template<class Entry, std::size_t Factions>
class Catalog
{
public:
    using Row = std::shared_ptr<Entry const>;
    using Map = std::unordered_map<std::uint32_t, Row>;
    using Classifier = std::function<std::optional<AuctionVisibility::Tag>(Entry const&, AuctionVisibility::Policy const&)>;
    struct View
    {
        AuctionVisibility::Mode mode;
        AuctionVisibility::Policy policy;
        Map rows;
    };
    struct Active
    {
        AuctionVisibility::Mode mode;
        AuctionVisibility::Policy policy;
    };

    std::shared_ptr<View const> Capture(std::size_t faction)
    {
        if (faction >= Factions)
            return {};
        std::lock_guard lock(_mutex);
        auto& cached = _views[faction];
        if (!cached)
            cached = std::make_shared<View const>(View{_mode, _policy, _rows[faction]});
        return cached;
    }

    Active Current() const
    {
        std::lock_guard lock(_mutex);
        return {_mode, _policy};
    }

    bool Add(std::size_t faction, Entry entry)
    {
        if (faction >= Factions)
            return false;
        std::lock_guard lock(_mutex);
        auto& rows = _rows[faction];
        if (rows.contains(entry.Id))
            return false; // preserve legacy insert semantics
        // A classification failure must not lose a real auction. Unknown is
        // hidden in Enforced, visible in Disabled/Observe and management.
        entry.visibilityTag = Classify(entry, _mode, _policy, _classifier).value_or(AuctionVisibility::Tag{});
        auto const id = entry.Id;
        rows.emplace(id, std::make_shared<Entry const>(std::move(entry)));
        _views[faction].reset();
        return true;
    }

    bool Remove(std::size_t faction, std::uint32_t id)
    {
        if (faction >= Factions)
            return false;
        std::lock_guard lock(_mutex);
        if (!_rows[faction].erase(id))
            return false;
        _views[faction].reset();
        return true;
    }

    template<class Bid, class Bidder>
    bool UpdateBid(std::size_t faction, std::uint32_t id, Bid bid, Bidder const& bidder)
    {
        if (faction >= Factions)
            return false;
        std::lock_guard lock(_mutex);
        auto it = _rows[faction].find(id);
        if (it == _rows[faction].end())
            return false;
        it->second = AuctionWorkerSnapshot::WithBid(it->second, bid, bidder);
        _views[faction].reset();
        return true;
    }

    // Development interface only: classifier must be pure, non-reentrant,
    // source-side and depend on copied facts. No provider/activation in Core B.
    // Build a complete candidate under the same mutation lock, then swap all
    // factions together. Failure retains the old generation and current rows.
    bool Publish(AuctionVisibility::Mode mode, AuctionVisibility::Policy policy, Classifier classifier)
    {
        using namespace AuctionVisibility;
        std::lock_guard lock(_mutex);
        if (policy.generation <= _policy.generation ||
            (mode != Mode::Disabled && mode != Mode::Observe && mode != Mode::Enforced) ||
            (mode != Mode::Disabled && (!Valid(policy) || !classifier)))
            return false;
        try
        {
            std::array<Map, Factions> candidate;
            std::array<std::shared_ptr<View const>, Factions> views;
            for (std::size_t faction = 0; faction < Factions; ++faction)
            {
                auto& rows = candidate[faction];
                rows.reserve(_rows[faction].size());
                for (auto const& [id, row] : _rows[faction])
                {
                    auto tag = Classify(*row, mode, policy, classifier);
                    if (!tag)
                        return false;
                    Entry copy = *row;
                    copy.visibilityTag = *tag;
                    rows.emplace(id, std::make_shared<Entry const>(std::move(copy)));
                }
                views[faction] = std::make_shared<View const>(View{mode, policy, rows});
            }
            _rows.swap(candidate);
            _views.swap(views);
            _mode = mode;
            _policy = policy;
            _classifier = std::move(classifier);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

private:
    static std::optional<AuctionVisibility::Tag> Classify(Entry const& entry,
        AuctionVisibility::Mode mode, AuctionVisibility::Policy const& policy,
        Classifier const& classifier)
    {
        using namespace AuctionVisibility;
        if (mode == Mode::Disabled)
            return Tag{};
        try
        {
            if (!classifier)
                return std::nullopt;
            auto tag = classifier(entry, policy);
            if (!tag)
                return std::nullopt;
            if (tag->state == TagState::Unknown)
                return Tag{};
            return Valid(*tag, policy) ? tag : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    mutable std::mutex _mutex;
    std::array<Map, Factions> _rows;
    std::array<std::shared_ptr<View const>, Factions> _views{};
    AuctionVisibility::Mode _mode = AuctionVisibility::Mode::Disabled;
    AuctionVisibility::Policy _policy{1, 0, 0, 0};
    Classifier _classifier;
};
}
