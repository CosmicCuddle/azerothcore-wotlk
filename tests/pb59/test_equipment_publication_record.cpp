#include "EquipmentPublicationRecord.h"
#include <cassert>
#include <atomic>
#include <thread>
using namespace PB59Prototype;
int main()
{
    EquipmentPublicationRecord a(42), b(43), bad(0);
    assert(!bad.BeginSession(1));
    assert(!a.BeginCapture(1));
    assert(a.BeginSession(7));
    assert(b.BeginSession(7));
    assert(!a.BeginSession(8));
    auto t = a.BeginCapture(7);
    assert(t);
    assert(!b.Publish(*t, 45, 70));
    assert(a.Publish(*t, 45, 70));
    assert(!a.Publish(*t, 45, 70));
    auto first = a.Read();
    assert(first.known && first.guid == 42 && first.session == 7);
    assert(first.level == 45 && first.average == 70);
    t = a.BeginCapture(7);
    assert(a.Publish(*t, 45, 70));
    assert(a.Read().revision != first.revision);
    t = a.BeginCapture(7);
    {
        EquipmentPublicationRecord::Mutation outer(a, 7);
        assert(outer);
        assert(!a.Read().known);
        assert(!a.BeginCapture(7));
        assert(!a.Publish(*t, 45, 70));
        {
            EquipmentPublicationRecord::Mutation inner(a, 7);
            assert(inner);
            assert(!a.BeginCapture(7));
        }
        assert(!a.BeginCapture(7));
    }
    assert(!a.Read().known);
    assert(!a.Publish(*t, 45, 70));
    auto current = a.BeginCapture(7);
    assert(current && a.Publish(*current, 46, 65));
    assert(a.Read().level == 46);
    t = a.BeginCapture(7);
    assert(!a.Publish(*t, 0, 65));
    assert(!a.Read().known);
    assert(!a.Publish(*t, 46, 65));
    current = a.BeginCapture(7);
    assert(!a.Publish(*current, 46, 0));
    assert(!a.Read().known);
    current = a.BeginCapture(7);
    assert(a.Publish(*current, 46, 65));
    t = a.BeginCapture(7);
    {
        EquipmentPublicationRecord::Mutation outer(a, 7);
        assert(a.EndSession(7));
        assert(!a.BeginSession(8));
        assert(!a.Publish(*t, 46, 65));
    }
    assert(!a.BeginSession(7));
    assert(a.BeginSession(8));
    assert(!a.EndSession(7));
    assert(!a.Publish(*t, 46, 65));
    {
        EquipmentPublicationRecord::Mutation stale(a, 7);
        assert(!stale);
    }
    current = a.BeginCapture(8);
    assert(current && a.Publish(*current, 46, 65));
    auto before = a.Read();
    std::atomic<bool> entered{false}, checked{false};
    std::thread reader([&] {
        while (!entered.load()) std::this_thread::yield();
        auto value = a.Read();
        assert(!value.known && value.revision != before.revision);
        checked.store(true);
    });
    {
        EquipmentPublicationRecord::Mutation scope(a, 8);
        entered.store(true);
        while (!checked.load()) std::this_thread::yield();
    }
    reader.join();
    assert(!a.Read().known);
    current = a.BeginCapture(8);
    assert(current && a.Publish(*current, 46, 65));
    assert(a.Read().session == 8 && a.Read().known);
}
