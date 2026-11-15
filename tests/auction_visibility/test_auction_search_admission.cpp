#include "AuctionSearchAdmission.h"
#include <cassert>

int main()
{
    AuctionSearchAdmission::Budget budget(128, 64);
    for (unsigned i = 0; i < 64; ++i)
        assert(budget.Acquire(true));
    assert(!budget.Acquire(true));
    assert(!budget.Release(false));
    for (unsigned i = 0; i < 64; ++i)
        assert(budget.Acquire(false));
    assert(!budget.Acquire(false) && !budget.Acquire(true));
    assert(budget.Total() == 128 && budget.Browse() == 64);
    assert(budget.Release(true));
    assert(budget.Acquire(true));
    budget.Close();
    assert(!budget.Acquire(false) && !budget.Acquire(true));
    for (unsigned i = 0; i < 64; ++i)
        assert(budget.Release(false) && budget.Release(true));
    assert(budget.Total() == 0 && budget.Browse() == 0);
    assert(!budget.Release(false) && !budget.Release(true));
    AuctionSearchAdmission::Budget none(0, 0);
    assert(!none.Acquire(false) && !none.Acquire(true));
}
