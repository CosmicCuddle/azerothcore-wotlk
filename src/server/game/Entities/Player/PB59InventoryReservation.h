#pragma once

#include <cstdint>
#include <span>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace PB59
{
// Only an active synchronous batch throws this internal cancellation. It must
// be caught by that batch before control returns to a packet/tick producer.
struct ReservedInventoryMutation final { };

// Value/key registry only: Item entry points never resolve/dereference a Player
// on a foreign loader/DB thread. The ordinary path is an atomic zero check.
class InventoryQuarantine final
{
    struct Record
    {
        std::uint64_t owner;
        std::vector<void const*> items;
        std::vector<std::uint64_t> counters;
        bool active = false;
    };
    struct Registry
    {
        std::mutex mutex;
        std::unordered_map<void const*, Record> records;
    };
    static Registry& Data()
    {
        // Process lifetime: native owners may unwind during global shutdown.
        static auto* registry = new Registry;
        return *registry;
    }
    inline static std::atomic<unsigned> _activeCount{0};
public:
    static bool Prepare(void const* state, std::uint64_t owner, std::span<void const* const> items,
        std::span<std::uint64_t const> counters = {}) noexcept
    {
        try
        {
            Record next{owner, {items.begin(), items.end()}, {counters.begin(), counters.end()}, false};
            auto& registry = Data();
            std::lock_guard lock(registry.mutex);
            auto found = registry.records.find(state);
            if (found != registry.records.end() && found->second.active)
                return false;
            registry.records.insert_or_assign(state, std::move(next));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    static void Set(void const* state, bool active) noexcept
    {
        auto& registry = Data();
        std::lock_guard lock(registry.mutex);
        auto found = registry.records.find(state);
        if (found != registry.records.end() && found->second.active != active)
        {
            found->second.active = active;
            if (active)
                _activeCount.fetch_add(1, std::memory_order_release);
            else
                _activeCount.fetch_sub(1, std::memory_order_release);
        }
    }
    static void Forget(void const* state) noexcept
    {
        auto& registry = Data();
        std::lock_guard lock(registry.mutex);
        auto found = registry.records.find(state);
        if (found != registry.records.end())
        {
            if (found->second.active)
                _activeCount.fetch_sub(1, std::memory_order_release);
            registry.records.erase(found);
        }
    }
    static bool Contains(void const* item, std::uint64_t owner)
    {
        if (!_activeCount.load(std::memory_order_acquire))
            return false;
        auto& registry = Data();
        std::lock_guard lock(registry.mutex);
        for (auto const& [state, record] : registry.records)
        {
            (void)state;
            if (!record.active)
                continue;
            if (owner && record.owner == owner)
                return true;
            for (auto const* pointer : record.items)
                if (pointer == item)
                    return true;
        }
        return false;
    }
    static bool ContainsCounter(std::uint64_t counter)
    {
        if (!counter || !_activeCount.load(std::memory_order_acquire))
            return false;
        auto& registry = Data();
        std::lock_guard lock(registry.mutex);
        for (auto const& [state, record] : registry.records)
        {
            (void)state;
            if (record.active)
                for (auto value : record.counters)
                    if (value == counter)
                        return true;
        }
        return false;
    }
};

class InventoryReservation final
{
public:
    InventoryReservation(void const* state, std::uint64_t owner,
        std::span<void const* const> items, std::span<std::uint64_t const> counters = {})
        : _state(state), _owner(owner), _items(items), _counters(counters), _previous(_active)
    {
        CheckState(state);
        _active = this;
    }
    ~InventoryReservation() { _active = _previous; }
    InventoryReservation(InventoryReservation const&) = delete;
    InventoryReservation& operator=(InventoryReservation const&) = delete;

    static void CheckState(void const* state)
    {
        for (auto* frame = _active; frame; frame = frame->_previous)
            if (frame->_state == state)
                frame->Reject();
    }
    static bool ActiveForState(void const* state) noexcept
    {
        for (auto* frame = _active; frame; frame = frame->_previous)
            if (frame->_state == state)
                return true;
        return false;
    }
    static void CheckItem(void const* item, std::uint64_t owner)
    {
        for (auto* frame = _active; frame; frame = frame->_previous)
        {
            if (owner && frame->_owner == owner)
                frame->Reject();
            for (auto const* reserved : frame->_items)
                if (reserved == item)
                    frame->Reject();
        }
    }
    [[nodiscard]] unsigned Violations() const noexcept { return _violations; }
    static void CheckCounter(std::uint64_t counter)
    {
        if (counter)
            for (auto* frame = _active; frame; frame = frame->_previous)
                for (auto value : frame->_counters)
                    if (value == counter)
                        frame->Reject();
    }
    void SetEffectsPhase(bool value) noexcept { _effectsPhase = value; }
    static void NoteCallback() noexcept
    {
        for (auto* frame = _active; frame; frame = frame->_previous)
            if (frame->_effectsPhase)
                frame->_externalEffects = true;
    }
    bool ExternalEffects() const noexcept { return _externalEffects; }
private:
    [[noreturn]] void Reject()
    {
        ++_violations;
        throw ReservedInventoryMutation{};
    }
    inline static thread_local InventoryReservation* _active = nullptr;
    void const* _state;
    std::uint64_t _owner;
    std::span<void const* const> _items;
    std::span<std::uint64_t const> _counters;
    InventoryReservation* _previous;
    unsigned _violations = 0;
    bool _effectsPhase = false;
    bool _externalEffects = false;
};
}
