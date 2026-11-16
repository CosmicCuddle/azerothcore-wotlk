#include "PB59OriginalQueueCustody.h"
#include <cassert>
#include <iostream>

int main()
{
    int a = 1, b = 2, c = 3;
    using PB59::OriginalQueueCustody;
    using PB59::SuspendOriginalQueue;
    using PB59::RestoreOriginalQueue;
    unsigned checks = 0;
    auto check = [&](bool ok) { assert(ok); ++checks; };
    std::vector<int*> q{&b, &a, nullptr};
    std::int32_t i = 1;
    OriginalQueueCustody<int> t;
    check(SuspendOriginalQueue(q, &a, i, 2, true, t));
    check(i == -1 && t.held && t.index == 1 && q[1] == nullptr && q[0] == &b);
    check(!SuspendOriginalQueue(q, &a, i, 2, false, t));
    check(!RestoreOriginalQueue(q, &b, i, 2, t));
    check(!RestoreOriginalQueue(q, &a, i, 3, t));
    q[1] = &c;
    check(!RestoreOriginalQueue(q, &a, i, 2, t));
    check(t.held && i == -1 && q[1] == &c);
    q[1] = nullptr;
    q[2] = &a;
    check(!RestoreOriginalQueue(q, &a, i, 2, t));
    q[2] = nullptr;
    q.resize(1);
    check(!RestoreOriginalQueue(q, &a, i, 2, t));
    q.resize(3);
    check(RestoreOriginalQueue(q, &a, i, 2, t));
    check(i == 1 && !t.held && q[1] == &a && q[0] == &b);
    check(!RestoreOriginalQueue(q, &a, i, 2, t));
    q[2] = &a;
    check(!SuspendOriginalQueue(q, &a, i, 2, true, t));
    q[2] = nullptr;
    i = 0;
    check(!SuspendOriginalQueue(q, &a, i, 2, true, t));
    i = 9;
    check(!SuspendOriginalQueue(q, &a, i, 2, true, t));
    i = -1;
    check(!SuspendOriginalQueue(q, &a, i, 2, true, t));
    check(!SuspendOriginalQueue(q, &a, i, 0, false, t));
    q[1] = nullptr;
    check(SuspendOriginalQueue(q, &a, i, 0, false, t));
    check(t.held && t.index == -1 && i == -1);
    check(RestoreOriginalQueue(q, &a, i, 0, t));
    check(!t.held && i == -1 && q[1] == nullptr);
    i = -2;
    check(!SuspendOriginalQueue(q, &a, i, 0, false, t));
    std::cout << checks << " checks passed\n";
}
