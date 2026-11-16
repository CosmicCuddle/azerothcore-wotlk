#include "PB59OriginalPointerCustody.h"
#include <cassert>
#include <iostream>

struct Item
{
    explicit Item(unsigned& deleted) : deletes(deleted) {}
    ~Item() { ++deletes; }
    unsigned& deletes;
};

int main()
{
    unsigned deleted = 0, checks = 0;
    auto check = [&](bool ok) { assert(ok); ++checks; };
    Item* equipped = new Item(deleted);
    Item* original = equipped;
    Item* other = new Item(deleted);
    std::unique_ptr<Item> held;
    using PB59::TakeOriginalPointer;
    using PB59::ReturnOriginalPointer;
    check(!TakeOriginalPointer(equipped, held, other));
    check(equipped == original && !held && deleted == 0);
    check(TakeOriginalPointer(equipped, held, original));
    check(!equipped && held.get() == original && deleted == 0);
    check(!TakeOriginalPointer(equipped, held, original));
    equipped = other;
    check(!ReturnOriginalPointer(equipped, held, original));
    check(equipped == other && held.get() == original && deleted == 0);
    equipped = nullptr;
    check(!ReturnOriginalPointer(equipped, held, other));
    check(ReturnOriginalPointer(equipped, held, original));
    check(equipped == original && !held && deleted == 0);
    check(!ReturnOriginalPointer(equipped, held, original));
    check(TakeOriginalPointer(equipped, held, original));
    check(ReturnOriginalPointer(equipped, held, original));
    delete equipped;
    delete other;
    check(deleted == 2);
    using PB59::CanRestoreOriginalLinks;
    for (unsigned field : {0u, 10u, 99u})
        for (unsigned contained : {0u, 20u, 99u})
            for (unsigned slot : {0u, 255u, 7u})
                check(CanRestoreOriginalLinks(field, contained, 10u, 20u, 0u, slot, 0u, 255u) ==
                    (field != 99 && contained != 99 && slot != 7));
    check(!CanRestoreOriginalLinks(0u, 0u, 0u, 20u, 0u, 0u, 0u, 255u));
    check(!CanRestoreOriginalLinks(0u, 0u, 10u, 0u, 0u, 0u, 0u, 255u));
    std::cout << checks << " original pointer checks passed\n";
}
