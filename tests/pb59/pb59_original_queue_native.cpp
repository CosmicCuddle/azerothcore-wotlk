// Compile the exact private Player adapter against a narrow mock; not a
// Worldserver build. native_methods.inc is extracted from PlayerStorage.cpp.
#include "PB59OriginalQueueCustody.h"
#include <cassert>
#include <iostream>
#include <optional>
#include <stdexcept>

using uint16 = std::uint16_t;
constexpr unsigned INVENTORY_SLOT_BAG_0 = 255, EQUIPMENT_SLOT_END = 19,
    EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18;
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
struct Guid { std::uint64_t value; auto GetRawValue() const { return value; } };
struct Item
{
    Guid guid{42}; ItemUpdateState state = ITEM_CHANGED; std::int32_t uQueuePos = 0;
    auto GetGUID() const { return guid; }
    auto GetState() const { return state; }
};
namespace PB59
{
struct Instance { std::uint64_t guid; bool operator==(Instance const&) const = default; };
struct InventorySlotFacts
{
    std::uint64_t owner = 7, bag = 0; uint16 position = 0xff00;
    std::optional<Instance> item = Instance{42};
    bool operator==(InventorySlotFacts const&) const = default;
};
struct TransactionAdmission
{
    using Ticket = std::uint64_t;
    enum class Access { Forward, Recovery };
    bool recovered = false;
    bool CanAccess(Ticket t, Access a) const { return t == 1 && recovered == (a == Access::Recovery); }
    void RequireRecovery(Ticket t) { assert(t == 1); recovered = true; }
};
}
struct Player
{
    struct PB59OriginalQueueHold
    {
        PB59::OriginalQueueCustody<Item> queue;
        PB59::TransactionAdmission::Ticket ticket = 0;
        uint16 position = 0;
    };
    PB59::TransactionAdmission _pb59Transaction;
    bool m_itemUpdateQueueBlocked = false, failCapture = false, missing = false;
    Item a, b{Guid{43}, ITEM_CHANGED, -1};
    Item* equipped = &a;
    std::vector<Item*> m_itemUpdateQueue{&a, nullptr};
    PB59::InventorySlotFacts facts;
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 p) const
    {
        if (failCapture) throw std::runtime_error("capture failure");
        if (missing || p != facts.position) return std::nullopt;
        return facts;
    }
    Item* GetItemByPos(uint16 p) const { return p == facts.position ? equipped : nullptr; }
    bool SuspendPB59OriginalQueue(PB59::InventorySlotFacts const&, std::uint64_t, PB59OriginalQueueHold&);
    bool RestorePB59OriginalQueue(PB59::InventorySlotFacts const&, std::uint64_t, PB59OriginalQueueHold&);
};
#include "native_methods.inc"

int main()
{
    Player p; Player::PB59OriginalQueueHold hold;
    auto original = p.facts;
    unsigned checks = 0;
    auto check = [&](bool ok) { assert(ok); ++checks; };
    check(!p.SuspendPB59OriginalQueue(original, 2, hold));
    auto wrong = original; wrong.position = 0xff03;
    check(!p.SuspendPB59OriginalQueue(wrong, 1, hold));
    wrong = original; wrong.bag = 1;
    check(!p.SuspendPB59OriginalQueue(wrong, 1, hold));
    wrong = original; wrong.owner = 9;
    check(!p.SuspendPB59OriginalQueue(wrong, 1, hold));
    p.m_itemUpdateQueueBlocked = true;
    check(!p.SuspendPB59OriginalQueue(original, 1, hold));
    p.m_itemUpdateQueueBlocked = false;
    p.m_itemUpdateQueue[1] = &p.a;
    check(!p.SuspendPB59OriginalQueue(original, 1, hold));
    p.m_itemUpdateQueue[1] = nullptr;
    check(p.SuspendPB59OriginalQueue(original, 1, hold));
    check(hold.ticket == 1 && hold.queue.held && p.a.uQueuePos == -1 && !p.m_itemUpdateQueue[0]);
    check(!p.RestorePB59OriginalQueue(original, 2, hold));
    p.equipped = &p.b;
    check(!p.RestorePB59OriginalQueue(original, 1, hold));
    check(p._pb59Transaction.recovered && hold.queue.held && !p.m_itemUpdateQueue[0]);
    p.equipped = &p.a;
    p.m_itemUpdateQueue[0] = &p.b;
    check(!p.RestorePB59OriginalQueue(original, 1, hold));
    check(p.m_itemUpdateQueue[0] == &p.b && hold.queue.held);
    p.m_itemUpdateQueue[0] = nullptr;
    p.failCapture = true;
    check(!p.RestorePB59OriginalQueue(original, 1, hold));
    p.failCapture = false;
    check(p.RestorePB59OriginalQueue(original, 1, hold));
    check(!hold.ticket && !hold.queue.held && p.a.uQueuePos == 0 && p.m_itemUpdateQueue[0] == &p.a);
    check(!p.RestorePB59OriginalQueue(original, 1, hold));
    p._pb59Transaction.recovered = false;
    p.a.state = ITEM_UNCHANGED;
    check(!p.SuspendPB59OriginalQueue(original, 1, hold));
    p.m_itemUpdateQueue[0] = nullptr; p.a.uQueuePos = -1;
    check(p.SuspendPB59OriginalQueue(original, 1, hold));
    check(p.RestorePB59OriginalQueue(original, 1, hold));
    check(!hold.queue.held && p.a.uQueuePos == -1 && !p.m_itemUpdateQueue[0]);
    std::cout << checks << " native adapter mock checks passed\n";
}
