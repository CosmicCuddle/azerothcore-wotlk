// Exact native body included by the Python runner; dependencies are synthetic.
#include "PB59VisibleEquipment.h"
#include "EquipmentCaptureState.h"
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <stdexcept>
using uint8=std::uint8_t;
using uint16=std::uint16_t;
using uint32=std::uint32_t;
constexpr int INVENTORY_SLOT_BAG_0=255, EQUIPMENT_SLOT_END=19,
    EQUIPMENT_SLOT_BODY=3, EQUIPMENT_SLOT_TABARD=18,
    PERM_ENCHANTMENT_SLOT=0, TEMP_ENCHANTMENT_SLOT=1,
    PLAYER_VISIBLE_ITEM_1_ENTRYID=10, PLAYER_VISIBLE_ITEM_1_ENCHANTMENT=11;
struct Player
{
    PB59::EquipmentCaptureState _pb59Equipment;
    PB59::TransactionAdmission _pb59Transaction;
    PB59::TransactionAdmission::Ticket ticket = _pb59Transaction.Begin();
    PB59::InventorySlotFacts inventory;
    PB59::VisibleEquipmentFacts visible;
    unsigned writes=0, forced=0, fail=0;
    bool after=false;
    void Write(auto fn)
    {
        ++writes;
        if (writes==fail && !after) throw std::runtime_error("before");
        fn();
        if (writes==fail && after) throw std::runtime_error("after");
    }
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos)
    { if(pos!=inventory.position) return std::nullopt; return inventory; }
    uint32 GetUInt32Value(uint16 f) const { assert(f==40); return visible.entry; }
    uint16 GetUInt16Value(uint16 f,uint8 half) const
    { assert(f==41); return half?visible.temporary:visible.permanent; }
    void SetUInt32Value(uint16 f,uint32 v) { assert(f==40); Write([&]{visible.entry=v;}); }
    void SetUInt16Value(uint16 f,uint8 half,uint16 v)
    { assert(f==41); Write([&]{(half?visible.temporary:visible.permanent)=v;}); }
    void ForceValuesUpdateAtIndex(uint32 f) { assert(f==40||f==41); Write([&]{++forced;}); }
    bool ReconcilePB59VisibleEquipment(PB59::InventorySlotFacts const&,
        PB59::InventorySlotFacts const&, PB59::TransactionAdmission::Ticket,
        PB59::TransactionAdmission::Access);
};
#include "ReconcilePB59VisibleEquipment.inc"
using namespace PB59;
using Admission=PB59::TransactionAdmission;
int main()
{
    InventoryInstanceFacts item;
    item.guid=7; item.owner=42; item.entry=100; item.count=1;
    item.enchants[0].id=44; item.enchants[1].id=55;
    InventorySlotFacts equipped{42,0,0xff0f,item}, empty{42,0,0xff0f,std::nullopt};
    auto old=VisibleEquipment(equipped), target=VisibleEquipment(empty);
    for(bool restore : {false,true})
    {
        auto before=restore?empty:equipped, next=restore?equipped:empty;
        auto oldVisible=VisibleEquipment(before), newVisible=VisibleEquipment(next);
        for(unsigned fault=1;fault<=5;++fault)
            for(bool after : {false,true})
            {
                Player p; p.inventory=next; p.visible=oldVisible; p.fail=fault; p.after=after;
                assert(!p.ReconcilePB59VisibleEquipment(before,next,p.ticket,Admission::Access::Forward));
                assert(p.inventory==next && p._pb59Transaction.State()==Admission::Phase::RecoveryRequired);
                p.fail=0; p.writes=0; p.forced=0;
                assert(!p.ReconcilePB59VisibleEquipment(before,next,p.ticket,Admission::Access::Forward));
                assert(p.writes==0);
                assert(p.ReconcilePB59VisibleEquipment(before,next,p.ticket,Admission::Access::Recovery));
                assert(p._pb59Transaction.State()==Admission::Phase::RecoveryRequired);
                assert(p.visible==newVisible && p.forced==2 && p.inventory==next);
            }
    }
    Player p; p.inventory=empty; p.visible=old; p.visible.entry=999;
    assert(!p.ReconcilePB59VisibleEquipment(equipped,empty,p.ticket,Admission::Access::Forward) && p.writes==0);
    p.visible=old; p.inventory=equipped;
    assert(!p.ReconcilePB59VisibleEquipment(equipped,empty,p.ticket,Admission::Access::Recovery) && p.writes==0);
    p.inventory=empty;
    assert(p.ReconcilePB59VisibleEquipment(equipped,empty,p.ticket,Admission::Access::Recovery) && p.visible==target);
    auto bad=empty; bad.position=0xff03;
    assert(!p.ReconcilePB59VisibleEquipment(equipped,bad,p.ticket,Admission::Access::Recovery));
    Player q, foreign;
    q.inventory=empty; q.visible=old;
    for (auto t : {Admission::Ticket{0}, foreign.ticket})
        assert(!q.ReconcilePB59VisibleEquipment(equipped,empty,t,Admission::Access::Forward));
    assert(!q.ReconcilePB59VisibleEquipment(equipped,empty,q.ticket,Admission::Access::Recovery));
    assert(q.writes==0 && q._pb59Transaction.IsActive(q.ticket));
    assert(q._pb59Transaction.Finish(q.ticket,{true,true,true,true}));
    assert(!q.ReconcilePB59VisibleEquipment(equipped,empty,q.ticket,Admission::Access::Forward));
    assert(q.writes==0 && q.visible==old);
}
