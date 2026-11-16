#include "PB59OriginalQueueCustody.h"
#include "PB59OriginalPointerCustody.h"
#include <array>
#include <cassert>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, EQUIPMENT_SLOT_END = 19, EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18, NULL_SLOT = 255;
constexpr uint16 PLAYER_FIELD_INV_SLOT_HEAD = 100, ITEM_FIELD_CONTAINED = 2;
struct ObjectGuid { std::uint64_t v{}; auto GetRawValue() const { return v; } bool IsEmpty() const { return !v; } bool operator==(ObjectGuid const&) const = default; static ObjectGuid const Empty; };
ObjectGuid const ObjectGuid::Empty{};
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
struct Fault { int remaining = -1; void hit() { if (remaining == 0) { remaining = -1; throw std::runtime_error("field write"); } if (remaining > 0) --remaining; } };
struct Item {
    Fault* fault{}; ObjectGuid guid{42}, owner{7}, contained{7}; uint8 slot{}; ItemUpdateState state = ITEM_CHANGED; int uQueuePos = 0;
    ObjectGuid GetGUID() const { return guid; } ObjectGuid GetOwnerGUID() const { return owner; }
    ObjectGuid GetGuidValue(uint16) const { return contained; } void SetGuidValue(uint16, ObjectGuid g) { contained = g; fault->hit(); }
    void ForceValuesUpdateAtIndex(uint16) { fault->hit(); } uint8 GetSlot() const { return slot; } void SetSlot(uint8 s) { slot = s; }
    void* GetContainer() const { return nullptr; } ItemUpdateState GetState() const { return state; }
};
namespace PB59 {
struct Instance { std::uint64_t guid; bool operator==(Instance const&) const = default; };
struct InventorySlotFacts { std::uint64_t owner{}, bag{}; uint16 position{}; std::optional<Instance> item; bool operator==(InventorySlotFacts const&) const = default; };
struct TransactionAdmission { using Ticket = unsigned; enum class Access { Forward, Recovery }; bool recovery{}; bool CanAccess(Ticket t, Access a) const { return t == 1 && (recovery == (a == Access::Recovery)); } void RequireRecovery(Ticket t) { assert(t == 1); recovery = true; } };
struct EquipmentCaptureState { struct Mutation { explicit Mutation(EquipmentCaptureState&) {} }; };
enum class LinkTransferResult { Rejected, Linked, RecoveryRequired };
}
struct Player {
    struct PB59OriginalQueueHold { PB59::OriginalQueueCustody<Item> queue; unsigned ticket{}; uint16 position{}; };
    struct PB59OriginalItemHold { std::unique_ptr<Item> item; std::optional<PB59::InventorySlotFacts> original; PB59OriginalQueueHold queue; };
    Fault fault; PB59::TransactionAdmission _pb59Transaction; PB59::EquipmentCaptureState _pb59Equipment;
    std::array<Item*,23> m_items{}; std::array<ObjectGuid,19> fields{}; std::array<PB59OriginalItemHold,19> _pb59OriginalItems{};
    std::vector<Item*> m_itemUpdateQueue; bool m_itemUpdateQueueBlocked{};
    Player() { m_items[0] = new Item{&fault}; fields[0] = {42}; m_itemUpdateQueue.push_back(m_items[0]); }
    ~Player() { for (auto* p : m_items) delete p; }
    ObjectGuid GetGUID() const { return {7}; }
    ObjectGuid GetGuidValue(uint16 f) const { return fields.at((f-PLAYER_FIELD_INV_SLOT_HEAD)/2); }
    void SetGuidValue(uint16 f, ObjectGuid g) { fields.at((f-PLAYER_FIELD_INV_SLOT_HEAD)/2) = g; fault.hit(); }
    void ForceValuesUpdateAtIndex(uint16) { fault.hit(); }
    Item* GetItemByPos(uint16 p) const { return m_items.at(p & 255); }
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 p) const {
        auto s = p & 255; if ((p >> 8) != 255 || s >= 19) return {};
        PB59::InventorySlotFacts result{7,0,p,{}}; auto* i = m_items[s];
        if (!i) return fields[s].IsEmpty() ? std::optional{result} : std::nullopt;
        if (fields[s] != i->guid || i->owner != GetGUID() || i->contained != GetGUID() || i->slot != s) return {};
        result.item = PB59::Instance{i->guid.v}; return result;
    }
    bool SuspendPB59OriginalQueue(PB59::InventorySlotFacts const&, unsigned, PB59OriginalQueueHold&);
    bool RestorePB59OriginalQueue(PB59::InventorySlotFacts const&, unsigned, PB59OriginalQueueHold&);
    PB59::LinkTransferResult HoldPB59OriginalItem(PB59::InventorySlotFacts const&, unsigned);
    PB59::LinkTransferResult RestorePB59OriginalItem(uint8, unsigned);
};
#include "native_methods.inc"
int main() {
    using R = PB59::LinkTransferResult; unsigned checks = 0;
    auto check = [&](bool ok) { assert(ok); ++checks; };
    auto snapshot = [](Player& p) { return *p.CapturePB59InventorySlot(0xff00); };
    auto restored = [&](Player& p, Item* i) { check(p.m_items[0] == i); check(p.fields[0] == i->guid); check(i->contained == p.GetGUID() && i->slot == 0); check(!p._pb59OriginalItems[0].item && !p._pb59OriginalItems[0].original); check(p.m_itemUpdateQueue[0] == i && i->uQueuePos == 0); };
    { Player p; auto* i = p.m_items[0]; auto s = snapshot(p); check(p.HoldPB59OriginalItem(s,2)==R::Rejected); p.m_items[1]=i; check(p.HoldPB59OriginalItem(s,1)==R::Rejected); p.m_items[1]=nullptr; check(p.HoldPB59OriginalItem(s,1)==R::Linked); check(!p.m_items[0] && p._pb59OriginalItems[0].item.get()==i); check(!p.m_itemUpdateQueue[0] && i->uQueuePos==-1); check(p.RestorePB59OriginalItem(0,2)==R::Rejected); check(p.RestorePB59OriginalItem(0,1)==R::Linked); restored(p,i); }
    for(int f=0; f<2; ++f) { Player p; auto* i=p.m_items[0]; auto s=snapshot(p); p.fault.remaining=f; check(p.HoldPB59OriginalItem(s,1)==R::RecoveryRequired); check(p._pb59Transaction.recovery && p._pb59OriginalItems[0].item.get()==i); check(p.RestorePB59OriginalItem(0,1)==R::Linked); restored(p,i); }
    for(int f=0; f<6; ++f) { Player p; auto* i=p.m_items[0]; check(p.HoldPB59OriginalItem(snapshot(p),1)==R::Linked); p.fault.remaining=f; check(p.RestorePB59OriginalItem(0,1)==R::RecoveryRequired); check(p._pb59OriginalItems[0].item.get()==i); check(p.RestorePB59OriginalItem(0,1)==R::Linked); restored(p,i); }
    { Player p; auto* i=p.m_items[0]; check(p.HoldPB59OriginalItem(snapshot(p),1)==R::Linked); Item foreign{&p.fault}; foreign.guid={99}; p.m_items[0]=&foreign; check(p.RestorePB59OriginalItem(0,1)==R::RecoveryRequired); check(p.m_items[0]==&foreign); p.m_items[0]=nullptr; p.fields[0]={99}; check(p.RestorePB59OriginalItem(0,1)==R::RecoveryRequired); p.fields[0]={}; i->state=ITEM_NEW; check(p.RestorePB59OriginalItem(0,1)==R::RecoveryRequired); i->state=ITEM_CHANGED; p.m_itemUpdateQueue[0]=&foreign; check(p.RestorePB59OriginalItem(0,1)==R::RecoveryRequired); check(p.m_items[0]==i && !p._pb59OriginalItems[0].item && p._pb59OriginalItems[0].original); p.m_itemUpdateQueue[0]=nullptr; check(p.RestorePB59OriginalItem(0,1)==R::Linked); restored(p,i); }
    { Player p; auto* i=p.m_items[0]; i->state=ITEM_UNCHANGED; i->uQueuePos=-1; p.m_itemUpdateQueue.clear(); check(p.HoldPB59OriginalItem(snapshot(p),1)==R::Linked); check(p.RestorePB59OriginalItem(0,1)==R::Linked); check(p.m_items[0]==i && p.m_itemUpdateQueue.empty() && i->uQueuePos==-1); }
    std::cout << checks << " native pointer custody checks passed\n";
}
