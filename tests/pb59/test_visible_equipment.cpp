#include "PB59VisibleEquipment.h"
#include <cassert>
using namespace PB59;
int main()
{
    InventoryInstanceFacts item;
    item.guid=7; item.owner=42; item.entry=100; item.count=1;
    item.enchants[0].id=44; item.enchants[1].id=55;
    InventorySlotFacts equipped{42,0,0xff0f,item}, empty{42,0,0xff0f,std::nullopt};
    auto old=VisibleEquipment(equipped), next=VisibleEquipment(empty);
    assert((old==VisibleEquipmentFacts{100,44,55}));
    assert(next==VisibleEquipmentFacts{});
    assert(VisibleTransitionValid(equipped,empty));
    assert(VisibleTransitionValid(empty,equipped));
    assert(!VisibleTransitionValid(equipped,equipped));
    assert(!VisibleTransitionValid(empty,empty));
    auto bad=empty; ++bad.position;
    assert(!VisibleTransitionValid(equipped,bad));
    bad=empty; ++bad.owner;
    assert(!VisibleTransitionValid(equipped,bad));
    bad=empty; bad.bag=9;
    assert(!VisibleTransitionValid(equipped,bad));
    for (unsigned mask=0;mask<8;++mask)
    {
        VisibleEquipmentFacts partial{mask&1?old.entry:next.entry,
            mask&2?old.permanent:next.permanent,mask&4?old.temporary:next.temporary};
        assert(CanReconcileVisible(old,next,partial));
        assert(CanReconcileVisible(next,old,partial));
    }
    auto custom=old; custom.entry=999;
    assert(!CanReconcileVisible(old,next,custom));
    custom=old; custom.permanent=99;
    assert(!CanReconcileVisible(old,next,custom));
    custom=old; custom.temporary=99;
    assert(!CanReconcileVisible(old,next,custom));
    equipped.item->enchants[0].id=0x1002c;
    assert(VisibleEquipment(equipped).permanent==44); // same 16-bit native field
}
