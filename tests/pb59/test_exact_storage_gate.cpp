#include "ExactStorageGate.h"
#include <cassert>
using namespace PB59;
int main()
{
    ExactStorageReservation expected{11, 22, 0x1301, 3};
    ExactStorageObservation actual{11, 22, 0x1301, 3, true, true, true, true, false, false};
    assert(AcceptExactStorage(expected, actual));
    auto reject = [&](auto change) {
        auto modified = actual;
        change(modified);
        assert(!AcceptExactStorage(expected, modified));
    };
    reject([](auto& a) { a.itemGuid++; });
    reject([](auto& a) { a.bagGuid++; });
    reject([](auto& a) { a.bagGuid = 0; });
    reject([](auto& a) { a.position++; });
    reject([](auto& a) { a.count--; });
    reject([](auto& a) { a.count++; });
    reject([](auto& a) { a.detached = false; });
    reject([](auto& a) { a.owned = false; });
    reject([](auto& a) { a.transferable = false; });
    reject([](auto& a) { a.validPosition = false; });
    reject([](auto& a) { a.occupied = true; });
    reject([](auto& a) { a.clone = true; });
    auto malformed = expected;
    malformed.itemGuid = 0;
    actual.itemGuid = 0;
    assert(!AcceptExactStorage(malformed, actual));
    actual.itemGuid = 11;
    malformed = expected;
    malformed.count = 0;
    actual.count = 0;
    assert(!AcceptExactStorage(malformed, actual));
    actual.count = 3;
    malformed = expected;
    malformed.position = 0;
    actual.position = 0;
    assert(!AcceptExactStorage(malformed, actual));
    expected = {11, 0, 0xff17, 3};
    actual = {11, 0, 0xff17, 3, true, true, true, true, false, false};
    assert(AcceptExactStorage(expected, actual));
    actual.bagGuid = 22;
    assert(!AcceptExactStorage(expected, actual));
}
