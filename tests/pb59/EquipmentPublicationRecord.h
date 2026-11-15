#pragma once
// Test-only PB59 publication record. The owner thread captures values; readers
// copy values only. No Player, database, command lease or runtime hook here.
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace PB59Prototype
{
struct EquipmentRecordValue
{
    std::uint64_t guid = 0;
    std::uint64_t session = 0;
    std::uint64_t revision = 0;
    std::uint32_t level = 0;
    std::uint32_t average = 0;
    bool known = false;
    bool operator==(EquipmentRecordValue const&) const = default;
};

class EquipmentPublicationRecord
{
public:
    struct Ticket
    {
        std::uint64_t guid = 0;
        std::uint64_t session = 0;
        std::uint64_t revision = 0;
    };

    // A record belongs to one GUID for its lifetime. The session comes from
    // the authoritative IP login epoch; the caller must verify that binding.
    explicit EquipmentPublicationRecord(std::uint64_t guid) : _guid(guid) { }

    bool BeginSession(std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        if (!_guid || !session || session <= _value.session || _active || _depth)
            return false;
        _value = {_guid, session, 1, 0, 0, false};
        _active = true;
        _exhausted = false;
        return true;
    }

    bool EndSession(std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        if (!_active || session != _value.session)
            return false;
        _active = false;
        Invalidate();
        return true;
    }

    // The scoped guard must enclose the actual mutation and all callbacks.
    // Outermost entry revokes the sample; scope exit never publishes.
    class Mutation
    {
    public:
        Mutation(EquipmentPublicationRecord& record, std::uint64_t session)
            : _record(record), _entered(record.Enter(session)) { }
        ~Mutation() { if (_entered) _record.Leave(); }
        Mutation(Mutation const&) = delete;
        Mutation& operator=(Mutation const&) = delete;
        explicit operator bool() const { return _entered; }
    private:
        EquipmentPublicationRecord& _record;
        bool _entered;
    };

    // Called at a stable owner-thread boundary before capturing the sample.
    std::optional<Ticket> BeginCapture(std::uint64_t session) const
    {
        std::lock_guard lock(_mutex);
        if (!_active || _depth || _exhausted || session != _value.session)
            return std::nullopt;
        return Ticket{_guid, session, _value.revision};
    }

    // Any intervening publication, mutation or logout makes a ticket stale.
    // A matching ticket is single-use, including a same-value publication.
    bool Publish(Ticket const& ticket, std::uint32_t level, std::uint32_t average)
    {
        std::lock_guard lock(_mutex);
        if (!_active || _depth || _exhausted || ticket.guid != _guid ||
            ticket.session != _value.session || ticket.revision != _value.revision)
            return false;
        Invalidate();
        if (_exhausted || !level || !average)
            return false;
        _value.level = level;
        _value.average = average;
        _value.known = true;
        return true;
    }

    EquipmentRecordValue Read() const
    {
        std::lock_guard lock(_mutex);
        return _value;
    }

private:
    void Invalidate()
    {
        _value.known = false;
        _value.level = 0;
        _value.average = 0;
        if (_value.revision == std::numeric_limits<std::uint64_t>::max())
            _exhausted = true;
        else
            ++_value.revision;
    }

    bool Enter(std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        if (!_active || session != _value.session)
            return false;
        if (_depth++ == 0)
            Invalidate();
        return true;
    }

    void Leave()
    {
        std::lock_guard lock(_mutex);
        --_depth;
    }

    const std::uint64_t _guid;
    mutable std::mutex _mutex;
    EquipmentRecordValue _value;
    std::uint64_t _depth = 0;
    bool _active = false;
    bool _exhausted = false;
};
}
