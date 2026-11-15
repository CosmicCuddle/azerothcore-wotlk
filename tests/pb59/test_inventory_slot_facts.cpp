#include "PB59InventorySlotFacts.h"
#include <cassert>
using namespace PB59;

int main()
{
    InventoryInstanceFacts item;
    item.guid = 7; item.owner = 42; item.entry = 100; item.count = 2;
    item.creator = 88; item.giftCreator = 99; item.duration = 50;
    item.flags = 3; item.durability = 80; item.maxDurability = 100;
    item.playedTime = 5; item.suffix = 9; item.randomProperty = -10;
    item.spellCharges[2] = -3; item.enchants[4] = {1000,20,2};
    item.text = "original";
    InventorySlotFacts original{42,0,0xff0f,item};
    InventorySlotFacts empty{42,9,0x1300,std::nullopt};
    InventorySlotFacts stored{42,9,0x1300,item};
    assert(ValidInventorySlot(original,7,true,true));
    assert(ValidInventorySlot(empty,0,true,true));
    assert(ValidInventorySlot(stored,7,true,true));
    assert(!ValidInventorySlot(empty,7,true,true));
    assert(!ValidInventorySlot(original,0,true,true));
    assert(!ValidInventorySlot(original,8,true,true));
    assert(!ValidInventorySlot(original,7,false,true));
    assert(!ValidInventorySlot(original,7,true,false));
    assert(SameInventoryInstance(original,stored));
    assert(!SameInventoryInstance(original,empty));
    assert(!SameInventoryInstance(empty,empty));
    auto invalid = original; invalid.owner = 99;
    assert(!ValidInventorySlot(invalid,7,true,true));
    invalid = original; invalid.item->count = 0;
    assert(!ValidInventorySlot(invalid,7,true,true));
    invalid = original; invalid.item->entry = 0;
    assert(!ValidInventorySlot(invalid,7,true,true));
    auto changed = [&](auto change) {
        auto candidate = stored;
        change(*candidate.item);
        assert(!SameInventoryInstance(original,candidate));
    };
    changed([](auto& x){++x.guid;});
    changed([](auto& x){++x.owner;});
    changed([](auto& x){++x.creator;});
    changed([](auto& x){++x.giftCreator;});
    changed([](auto& x){++x.entry;});
    changed([](auto& x){++x.count;});
    changed([](auto& x){++x.duration;});
    changed([](auto& x){++x.flags;});
    changed([](auto& x){++x.durability;});
    changed([](auto& x){++x.maxDurability;});
    changed([](auto& x){++x.playedTime;});
    changed([](auto& x){++x.suffix;});
    changed([](auto& x){++x.randomProperty;});
    changed([](auto& x){++x.spellCharges[2];});
    changed([](auto& x){++x.enchants[4].id;});
    changed([](auto& x){++x.enchants[4].duration;});
    changed([](auto& x){++x.enchants[4].charges;});
    changed([](auto& x){x.text += "!";});
    invalid = stored; invalid.owner = 43;
    assert(!SameInventoryInstance(original,invalid));
}
