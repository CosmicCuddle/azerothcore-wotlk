// Exact native method body extracted from PlayerStorage.cpp by the Python runner.
// Everything around it is synthetic; this is not a native Worldserver build.
#include "PB59InventorySlotFacts.h"
#include "PB59InventoryCustody.h"
#include "EquipmentCaptureState.h"
#include "PB59TransactionAdmission.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <vector>

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
constexpr int INVENTORY_SLOT_BAG_0=255, EQUIPMENT_SLOT_END=19,
    EQUIPMENT_SLOT_BODY=3, EQUIPMENT_SLOT_TABARD=18;
constexpr int PLAYER_FIELD_INV_SLOT_HEAD=100, CONTAINER_FIELD_SLOT_1=200,
    ITEM_FIELD_CONTAINED=300, ITEM_CHANGED=1;
struct ObjectGuid
{
    std::uint64_t value=0;
    std::uint64_t GetRawValue() const { return value; }
    bool operator==(ObjectGuid const&) const = default;
    static const ObjectGuid Empty;
};
const ObjectGuid ObjectGuid::Empty{};
struct Faults
{
    int operation=0, fail=0;
    bool after=false, persistent=false;
    unsigned forced=0;
    std::function<void()> corrupt;
    void Write(auto fn)
    {
        ++operation;
        if ((operation==fail || (persistent && operation>=fail)) && !after)
        {
            if (corrupt) corrupt();
            throw std::runtime_error("before write");
        }
        fn();
        if ((operation==fail || (persistent && operation>=fail)) && after) throw std::runtime_error("after write");
    }
};
struct Player;
struct Bag;
struct Item
{
    PB59::InventoryInstanceFacts facts;
    Faults* faults=nullptr;
    uint8 slot=0;
    Bag* container=nullptr;
    ObjectGuid contained;
    bool queued=false;
    unsigned queuePos=0;
    ObjectGuid GetGUID() const { return {facts.guid}; }
    void SetState(int,Player*);
    void SetPB59State(int value, Player* player) { SetState(value, player); }
    void SetPB59Slot(uint8 value) { SetSlot(value); }
    void SetPB59Container(Bag* value) { SetContainer(value); }
    bool IsInUpdateQueue() const { return queued; }
    unsigned GetQueuePos() const { return queuePos; }
    void SetSlot(uint8 value) { slot=value; }
    void SetContainer(Bag* value) { container=value; }
    ObjectGuid GetGuidValue(int field) const { assert(field==ITEM_FIELD_CONTAINED); return contained; }
    void SetGuidValue(int field,ObjectGuid value)
    { assert(field==ITEM_FIELD_CONTAINED); if (contained!=value) faults->Write([&]{contained=value;}); }
    void ForceValuesUpdateAtIndex(unsigned) { faults->Write([&]{++faults->forced;}); }
};
struct Bag : Item
{
    Item* m_bagslot[36]{};
    ObjectGuid fields[36]{};
    ObjectGuid GetGuidValue(int field) const { return fields[(field-CONTAINER_FIELD_SLOT_1)/2]; }
    void SetGuidValue(int field,ObjectGuid value)
    { auto& current=fields[(field-CONTAINER_FIELD_SLOT_1)/2]; if(current!=value) faults->Write([&]{current=value;}); }
};
struct Player
{
    Faults faults;
    PB59::EquipmentCaptureState _pb59Equipment;
    PB59::TransactionAdmission _pb59Transaction;
    PB59::TransactionAdmission::Ticket ticket = _pb59Transaction.Begin();
    Item* m_items[40]{};
    std::vector<Item*> m_itemUpdateQueue;
    ObjectGuid fields[40]{};
    Bag bag;
    Item item;
    int captures=0, failCapture=0;
    bool throwCapture=false;
    ObjectGuid GetGUID() const { return {42}; }
    Bag* GetBagByPos(uint8 pos) { return pos==19 ? &bag : nullptr; }
    ObjectGuid GetGuidValue(int field) const { return fields[(field-PLAYER_FIELD_INV_SLOT_HEAD)/2]; }
    void SetGuidValue(int field,ObjectGuid value)
    { auto& current=fields[(field-PLAYER_FIELD_INV_SLOT_HEAD)/2]; if(current!=value) faults.Write([&]{current=value;}); }
    void ForceValuesUpdateAtIndex(unsigned) { faults.Write([&]{++faults.forced;}); }
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos)
    {
        if (++captures==failCapture)
        {
            if (throwCapture) throw std::runtime_error("capture");
            return std::nullopt;
        }
        uint8 b=pos>>8,s=pos&255;
        if (b!=255 && b!=19) return std::nullopt;
        if ((b==255 && s>=40) || (b==19 && s>=36)) return std::nullopt;
        Bag* container=b==19 ? &bag : nullptr;
        auto* current=container ? container->m_bagslot[s] : m_items[s];
        auto guid=container ? container->fields[s] : fields[s];
        PB59::InventorySlotFacts result{42,container?container->GetGUID().GetRawValue():0,pos,std::nullopt};
        if (current)
        {
            if (current->slot!=s || current->container!=container ||
                current->contained!=(container?container->GetGUID():GetGUID())) return std::nullopt;
            result.item=current->facts;
        }
        if (!PB59::ValidInventorySlot(result,guid.GetRawValue(),true,true)) return std::nullopt;
        return result;
    }
    PB59::LinkTransferResult RelocatePB59InventoryLinks(PB59::InventorySlotFacts const&,
        PB59::InventorySlotFacts const&, PB59::TransactionAdmission::Ticket,
        PB59::TransactionAdmission::Access);
    Player()
    {
        _pb59Equipment.Bind();
        item.faults=&faults; bag.faults=&faults;
        item.facts.guid=7; item.facts.owner=42; item.facts.entry=100; item.facts.count=1;
        item.facts.enchants[2]={88,0,0}; item.facts.durability=11;
        item.slot=15; item.contained=GetGUID();
        bag.facts.guid=99; bag.facts.owner=42; bag.facts.entry=200; bag.facts.count=1;
        m_items[15]=&item; fields[15]=item.GetGUID();
    }
};
void Item::SetState(int,Player* player)
{
    faults->Write([&]{
        if (!queued)
        {
            queuePos=player->m_itemUpdateQueue.size();
            player->m_itemUpdateQueue.push_back(this);
            queued=true;
        }
    });
}
#include "RelocatePB59InventoryLinks.inc"
using Result=PB59::LinkTransferResult;
using Admission=PB59::TransactionAdmission;
constexpr uint16 gear=0xff0f, bagSlot=0x1300, backpack=0xff17;
int main()
{
    auto check = [](Player& p,uint16 from,uint16 to)
    {
        auto a=p.CapturePB59InventorySlot(from), b=p.CapturePB59InventorySlot(to);
        assert(a&&b);
        auto result=p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Forward);
        if (result==Result::RecoveryRequired)
            assert(p._pb59Transaction.State()==Admission::Phase::RecoveryRequired);
        return result;
    };
    {
        Player p;
        auto before=p.CapturePB59InventorySlot(gear);
        assert(check(p,gear,bagSlot)==Result::Linked);
        assert(!p.m_items[15] && p.bag.m_bagslot[0]==&p.item);
        auto stored=p.CapturePB59InventorySlot(bagSlot);
        assert(stored && PB59::SameInventoryInstance(*before,*stored));
        assert(check(p,bagSlot,gear)==Result::Linked);
        assert(p.m_items[15]==&p.item && !p.bag.m_bagslot[0]);
        assert(p.CapturePB59InventorySlot(gear)==before);
        assert(check(p,gear,backpack)==Result::Linked);
        assert(p.m_items[23]==&p.item && !p.m_items[15]);
        assert(check(p,backpack,gear)==Result::Linked);
        assert(p.CapturePB59InventorySlot(gear)==before);
    }
    // Wrong/foreign/expired tickets and wrong phase refuse before observing or writing.
    {
        Player p, foreign;
        auto a=p.CapturePB59InventorySlot(gear), b=p.CapturePB59InventorySlot(bagSlot);
        auto captures=p.captures;
        for (auto t : {Admission::Ticket{0}, foreign.ticket})
            assert(p.RelocatePB59InventoryLinks(*a,*b,t,Admission::Access::Forward)==Result::Rejected);
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Recovery)==Result::Rejected);
        assert(p.captures==captures && p.faults.operation==0);
        assert(p._pb59Transaction.Finish(p.ticket,{true,true,true,true}));
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Forward)==Result::Rejected);
        assert(p.captures==captures && p.faults.operation==0);
    }
    // A quarantined operation can only use explicitly authorized recovery access.
    {
        Player p;
        assert(check(p,gear,bagSlot)==Result::Linked);
        assert(p._pb59Transaction.RequireRecovery(p.ticket));
        auto a=p.CapturePB59InventorySlot(bagSlot), b=p.CapturePB59InventorySlot(gear);
        auto writes=p.faults.operation;
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Forward)==Result::Rejected);
        assert(p.faults.operation==writes);
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Recovery)==Result::Linked);
        assert(p.m_items[15]==&p.item && p._pb59Transaction.State()==Admission::Phase::RecoveryRequired);
    }
    // Stale, duplicated, missing, or misdirected save-queue references refuse.
    for (int mode=0;mode<5;++mode)
    {
        Player p;
        if (mode==0) { p.item.queued=true; p.item.queuePos=8; }
        if (mode==1) { p.item.queued=true; p.m_itemUpdateQueue={&p.bag}; }
        if (mode==2) { p.item.queued=true; p.m_itemUpdateQueue={&p.item,&p.item}; }
        if (mode==3) { p.m_itemUpdateQueue={&p.item}; }
        if (mode==4) { p.bag.queued=true; p.bag.queuePos=8; }
        assert(check(p,gear,bagSlot)==Result::Rejected && p.faults.operation==0);
        assert(p.m_items[15]==&p.item && !p.bag.m_bagslot[0]);
    }
    // Exact function uses two queue writes then three field writes for bag transfer.
    // Pre- and post-write failure injection: all post-link failures preserve custody.
    for (int operation=1;operation<=5;++operation)
        for (bool after : {false,true})
        {
            Player p; p.faults.fail=operation; p.faults.after=after;
            auto result=check(p,gear,bagSlot);
            if (operation<=2)
            {
                assert(result==Result::RecoveryRequired && p.m_items[15]==&p.item && !p.bag.m_bagslot[0]);
                assert(p._pb59Transaction.State()==Admission::Phase::RecoveryRequired);
            }
            else
            {
                assert(result==Result::Linked && !p.m_items[15] && p.bag.m_bagslot[0]==&p.item);
                assert(p.faults.forced==6 && p.CapturePB59InventorySlot(bagSlot));
                assert(p.item.slot==0 && p.item.container==&p.bag);
            }
        }
    // Persistent write failures cannot be declared repaired. No pointer loss.
    for (int operation=3;operation<=5;++operation)
        for (bool after : {false,true})
        {
            Player p; p.faults.fail=operation; p.faults.after=after; p.faults.persistent=true;
            assert(check(p,gear,bagSlot)==Result::RecoveryRequired);
            assert(!p.m_items[15] && p.bag.m_bagslot[0]==&p.item);
        }
    // Unexpected third-party field values are never overwritten by repair.
    for (int field=0;field<3;++field)
    {
        Player p; p.faults.fail=3;
        p.faults.corrupt=[&]{
            if (field==0) p.fields[15]={123};
            if (field==1) p.bag.fields[0]={123};
            if (field==2) p.item.contained={123};
        };
        assert(check(p,gear,bagSlot)==Result::RecoveryRequired && p.faults.forced==0);
        assert(!p.m_items[15] && p.bag.m_bagslot[0]==&p.item);
        assert((field==0?p.fields[15]:field==1?p.bag.fields[0]:p.item.contained).value==123);
    }
    // Reverse and backpack paths also recover a one-shot field failure.
    for (auto target : {bagSlot,backpack})
    {
        Player p;
        assert(check(p,gear,target)==Result::Linked);
        p.faults.operation=0; p.faults.fail=target==bagSlot?3:2; p.faults.after=true;
        assert(check(p,target,gear)==Result::Linked && p.faults.forced==6);
        assert(p.m_items[15]==&p.item && p.CapturePB59InventorySlot(gear));
    }
    for (int capture : {5,6})
        for (bool throws : {false,true})
        {
            Player p; p.failCapture=capture; p.throwCapture=throws;
            assert(check(p,gear,bagSlot)==Result::RecoveryRequired);
            assert(!p.m_items[15] && p.bag.m_bagslot[0]==&p.item);
        }
    {
        Player p;
        auto a=p.CapturePB59InventorySlot(gear), b=p.CapturePB59InventorySlot(bagSlot);
        ++a->item->durability;
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Forward)==Result::Rejected);
        assert(p.m_items[15]==&p.item && !p.bag.m_bagslot[0]);
        a=p.CapturePB59InventorySlot(gear); ++b->bag;
        assert(p.RelocatePB59InventoryLinks(*a,*b,p.ticket,Admission::Access::Forward)==Result::Rejected);
        assert(p.faults.operation==0);
    }
}
