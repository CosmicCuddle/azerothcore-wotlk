#pragma once
// Test-only owner-thread mutation/publication model; not a Player or script hook.
#include <cstdint>
#include <limits>
#include <mutex>

namespace PB59Prototype
{
struct Snapshot
{
    std::uint64_t session = 0;
    std::uint64_t revision = 0;
    std::uint32_t average = 0;
    bool known = false;
};

class EquipmentAuthority
{
public:
    // Owner thread only: epoch must be nonzero and monotonically increasing
    // for the lifetime of this authority record. Registry lifetime is external.
    bool BeginSession(std::uint64_t epoch)
    {
        std::lock_guard lock(_mutex);
        if (_depth || _active || !epoch || epoch <= _state.session)
            return false;
        _state = {epoch, 0, 0, false};
        _active = true;
        _exhausted = false;
        return true;
    }

    void EndSession()
    {
        std::lock_guard lock(_mutex);
        _active = false;
        Invalidate();
    }

    class Mutation
    {
    public:
        explicit Mutation(EquipmentAuthority& owner) : _owner(owner) { _owner.Enter(); }
        ~Mutation() { _owner.Leave(); }
        Mutation(Mutation const&) = delete;
        Mutation& operator=(Mutation const&) = delete;
    private:
        EquipmentAuthority& _owner;
    };

    // Owner thread, stable update boundary ONLY, never a primitive exit or
    // arbitrary callback. Capture and publish in the same owner-thread turn.
    bool Publish(std::uint32_t average)
    {
        std::lock_guard lock(_mutex);
        if (!_active || _depth || _exhausted)
            return false;
        if (!average)
        {
            Invalidate();
            return false;
        }
        if (_state.known && _state.average != average)
            Invalidate(); // defensive: changed value without prior invalidation
        if (_exhausted)
            return false;
        _state.average = average;
        _state.known = true;
        return true;
    }

    // Unknown input at a stable boundary must revoke previous publication.
    void PublishUnknown()
    {
        std::lock_guard lock(_mutex);
        Invalidate();
    }

    // Any thread: copy only, no Player pointer. No lock retained by caller.
    Snapshot Read() const
    {
        std::lock_guard lock(_mutex);
        return _state;
    }

    bool Matches(Snapshot const& expected) const
    {
        std::lock_guard lock(_mutex);
        return expected.known && _state.known && _active && !_depth &&
            expected.session == _state.session && expected.revision == _state.revision &&
            expected.average == _state.average;
    }

private:
    void Invalidate()
    {
        _state.known = false;
        _state.average = 0;
        if (_state.revision == std::numeric_limits<std::uint64_t>::max())
            _exhausted = true;
        else
            ++_state.revision;
    }
    void Enter()
    {
        std::lock_guard lock(_mutex);
        if (_depth++ == 0)
            Invalidate();
    }
    void Leave()
    {
        std::lock_guard lock(_mutex);
        --_depth;
        // Intentionally no publication: an outer caller may still have work.
    }
    mutable std::mutex _mutex;
    Snapshot _state;
    std::uint64_t _depth = 0;
    bool _active = false;
    bool _exhausted = false;
};
}
