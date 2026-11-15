#include "PB59InventoryCustody.h"
#include <cassert>
#include <memory>
using namespace PB59;
struct Item
{
    int id;
    int* destroyed;
    ~Item() { ++*destroyed; }
};
int main()
{
    int destroyed = 0;
    auto one = std::unique_ptr<Item>(new Item{1,&destroyed});
    auto two = std::unique_ptr<Item>(new Item{2,&destroyed});
    Item* source = one.get();
    Item* destination = nullptr;
    assert(RelocateInventoryPointer(source,destination,one.get()));
    assert(!source && destination == one.get() && destroyed == 0);
    assert(RelocateInventoryPointer(destination,source,one.get()));
    assert(!destination && source == one.get() && destroyed == 0);
    assert(!RelocateInventoryPointer(source,source,one.get()));
    assert(source == one.get());
    assert(!RelocateInventoryPointer(source,destination,two.get()));
    assert(source == one.get() && !destination);
    assert(!RelocateInventoryPointer(source,destination,static_cast<Item*>(nullptr)));
    destination = two.get();
    assert(!RelocateInventoryPointer(source,destination,one.get()));
    assert(source == one.get() && destination == two.get());
    source = nullptr;
    assert(!RelocateInventoryPointer(source,destination,one.get()));
    assert(!source && destination == two.get());
    source = one.get(); destination = nullptr;
    // Repeated moves preserve the exact same live item without cloning or delete.
    for (int i = 0; i < 1000; ++i)
    {
        assert(RelocateInventoryPointer(source,destination,one.get()));
        assert(RelocateInventoryPointer(destination,source,one.get()));
    }
    assert(source == one.get() && !destination && destroyed == 0);
    one.reset(); two.reset();
    assert(destroyed == 2);
}
