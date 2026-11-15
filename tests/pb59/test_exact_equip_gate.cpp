#include "ExactStorageGate.h"
#include <cassert>
using namespace PB59;
int main()
{
    ExactStorageReservation expected{11, 0, 0xff0f, 1};
    ExactStorageObservation actual{11, 0, 0xff0f, 1, true, true, true, true, false, false};
    assert(AcceptExactEquip(expected, actual));
    auto reject = [&](auto change) {
        auto copy = actual;
        change(copy);
        assert(!AcceptExactEquip(expected, copy));
    };
    reject([](auto& a) { ++a.itemGuid; });
    reject([](auto& a) { ++a.position; });
    reject([](auto& a) { ++a.count; });
    reject([](auto& a) { a.bagGuid = 9; });
    reject([](auto& a) { a.detached = false; });
    reject([](auto& a) { a.owned = false; });
    reject([](auto& a) { a.transferable = false; });
    reject([](auto& a) { a.validPosition = false; });
    reject([](auto& a) { a.occupied = true; });
    reject([](auto& a) { a.clone = true; });
    expected.bagGuid = 9;
    actual.bagGuid = 9;
    assert(AcceptExactStorage(expected, actual));
    assert(!AcceptExactEquip(expected, actual));
}
