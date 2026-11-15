#include "EquipmentPublicationRecord.h"
#include <cassert>
#include <memory>
#include <unordered_map>

using namespace PB59Prototype;

// Test-only registry. Attach is a trusted, serialized login event, immediately
// after authoritative IP epoch allocation. Never call Attach from a poll or
// deferred task. Monotonic global high-water rejects stale attachment without
// retaining a per-character tombstone. Out-of-order login binds fail closed.
class EquipmentRegistryModel
{
    using Record = EquipmentPublicationRecord;
public:
    class Guard
    {
        friend class EquipmentRegistryModel;
        Guard(std::shared_ptr<Record> record, std::uint64_t epoch)
            : _record(std::move(record)), _guard(*_record, epoch) { }
        std::shared_ptr<Record> _record;
        Record::Mutation _guard;
    public:
        explicit operator bool() const { return bool(_guard); }
    };

    bool Attach(std::uint64_t guid, std::uint64_t epoch)
    {
        std::lock_guard lock(_mutex);
        if (!guid || !epoch || epoch <= _highWater || _records.contains(guid))
            return false;
        auto record = std::make_shared<Record>(guid);
        if (!record->BeginSession(epoch))
            return false;
        _records.emplace(guid, std::move(record));
        _highWater = epoch;
        return true;
    }

    bool Detach(std::uint64_t guid, std::uint64_t epoch)
    {
        std::lock_guard lock(_mutex);
        auto it = _records.find(guid);
        if (it == _records.end() || !it->second->EndSession(epoch))
            return false;
        _records.erase(it);
        return true;
    }

    EquipmentRecordValue Read(std::uint64_t guid) const
    {
        std::lock_guard lock(_mutex);
        auto it = _records.find(guid);
        return it == _records.end() ? EquipmentRecordValue{} : it->second->Read();
    }

    std::optional<Record::Ticket> Capture(std::uint64_t guid, std::uint64_t epoch) const
    {
        std::lock_guard lock(_mutex);
        auto it = _records.find(guid);
        if (it == _records.end() || it->second->Read().known)
            return std::nullopt;
        return it->second->BeginCapture(epoch);
    }

    bool Publish(Record::Ticket const& ticket, std::uint32_t level, std::uint32_t average)
    {
        std::lock_guard lock(_mutex);
        auto it = _records.find(ticket.guid);
        return it != _records.end() && it->second->Publish(ticket, level, average);
    }

    std::unique_ptr<Guard> Mutate(std::uint64_t guid, std::uint64_t epoch)
    {
        std::lock_guard lock(_mutex);
        auto it = _records.find(guid);
        if (it == _records.end())
            return {};
        auto guard = std::unique_ptr<Guard>(new Guard(it->second, epoch));
        if (!*guard)
            return {};
        return guard;
    }

    std::size_t Size() const
    {
        std::lock_guard lock(_mutex);
        return _records.size();
    }

private:
    mutable std::mutex _mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<Record>> _records;
    std::uint64_t _highWater = 0;
};

int main()
{
    EquipmentRegistryModel registry;
    assert(!registry.Read(42).known && registry.Size() == 0);
    assert(!registry.Attach(0, 1));
    assert(!registry.Attach(42, 0));
    assert(registry.Attach(42, 1));
    assert(!registry.Attach(42, 2));
    assert(!registry.Attach(43, 1));
    assert(registry.Attach(43, 2));
    assert(registry.Size() == 2);
    auto ticket = registry.Capture(42, 1);
    assert(ticket && registry.Publish(*ticket, 60, 55));
    assert(!registry.Capture(42, 1)); // clean tick must not churn revision
    assert(!registry.Mutate(42, 2)); // stale/foreign session
    assert(registry.Read(42).known);
    auto outer = registry.Mutate(42, 1);
    assert(outer && !registry.Read(42).known);
    auto inner = registry.Mutate(42, 1);
    assert(inner && !registry.Capture(42, 1));
    inner.reset();
    assert(!registry.Capture(42, 1));
    assert(!registry.Publish(*ticket, 60, 99));
    assert(registry.Detach(42, 1)); // old guard still holds its record alive
    assert(registry.Size() == 1 && !registry.Read(42).known);
    assert(!registry.Attach(42, 1));
    assert(registry.Attach(42, 3));
    assert(!registry.Detach(42, 1));
    assert(!registry.Mutate(42, 1));
    assert(!registry.Publish(*ticket, 60, 99));
    auto fresh = registry.Capture(42, 3);
    assert(fresh && registry.Publish(*fresh, 60, 40));
    auto known = registry.Read(42);
    outer.reset(); // cannot touch new session's record
    assert(registry.Read(42) == known);
    assert(!registry.Read(43).known);
    assert(registry.Detach(43, 2));
    assert(registry.Detach(42, 3));
    assert(registry.Size() == 0);
    assert(!registry.Attach(99, 2)); // no tombstone needed for old global epoch
    assert(!registry.Publish(*fresh, 60, 99));
    assert(!registry.Capture(42, 3));
    assert(registry.Attach(42, 4));
    auto interrupted = registry.Capture(42, 4);
    assert(interrupted);
    { auto guard = registry.Mutate(42, 4); assert(guard); }
    assert(!registry.Publish(*interrupted, 60, 99));
    auto stable = registry.Capture(42, 4);
    assert(stable && registry.Publish(*stable, 60, 35));
    assert(registry.Read(42).average == 35);
}
