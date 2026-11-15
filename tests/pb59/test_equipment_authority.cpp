#include "EquipmentAuthorityPrototype.h"
#include <cassert>
#include <thread>
#include <atomic>
using namespace PB59Prototype;
int main()
{
    EquipmentAuthority a;
    assert(!a.Publish(70));
    assert(a.BeginSession(1));
    assert(!a.BeginSession(2));
    assert(!a.Read().known);
    assert(a.Publish(70));
    auto before = a.Read();
    assert(a.Matches(before));
    {
        EquipmentAuthority::Mutation outer(a);
        assert(!a.Matches(before));
        assert(!a.Publish(50));
        {
            EquipmentAuthority::Mutation inner(a);
            assert(!a.Publish(50));
        }
        assert(!a.Publish(50));
        assert(!a.Read().known);
    }
    assert(!a.Read().known); // no publication on scope exit
    assert(a.Publish(50));
    assert(!a.Matches(before));
    auto changed = a.Read();
    assert(a.Matches(changed));
    a.PublishUnknown();
    assert(!a.Matches(changed));
    assert(a.Publish(50));
    assert(!a.Matches(changed));
    auto current = a.Read();
    assert(a.Publish(60)); // defensive change invalidation
    assert(!a.Matches(current));
    auto valid = a.Read();
    assert(!a.Publish(0));
    assert(!a.Matches(valid));
    assert(a.Publish(60));
    auto latest = a.Read();
    {
        EquipmentAuthority::Mutation scope(a);
        a.EndSession();
        assert(!a.BeginSession(2));
        assert(!a.Publish(60));
    }
    assert(!a.Matches(latest));
    assert(!a.BeginSession(1));
    assert(a.BeginSession(2));
    assert(a.Publish(60));
    assert(!a.Matches(latest));
    auto relog = a.Read();
    std::atomic<bool> entered{false}, checked{false};
    std::thread reader([&] {
        while (!entered.load()) std::this_thread::yield();
        assert(!a.Matches(relog));
        assert(!a.Read().known);
        checked.store(true);
    });
    {
        EquipmentAuthority::Mutation scope(a);
        entered.store(true);
        while (!checked.load()) std::this_thread::yield();
    }
    reader.join();
    assert(!a.Read().known);
    assert(a.Publish(61));
    assert(a.Matches(a.Read()));
}
