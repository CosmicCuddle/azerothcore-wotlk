#!/usr/bin/env python3
"""PB59 *offline* independent-DB scheduling oracle; not a native Core test.

The disposable SQLite recovery runner submits four writes in order 0,1,2,3.
Real independent DB workers may complete in other orders. This enumerates
the six schedules respecting each realm's own delete-before-save dependency.
No server/database connections, item writes, or production assumptions.
"""
from itertools import permutations

A, B = "A", "B"
A_ITEM, B_ITEM = "item-GUID-A1", "item-GUID-B1"

def signature(prefix):
    items_a, items_b = {A_ITEM}, {B_ITEM}
    gold_a, gold_b = 100, 200
    for phase in prefix:
        if phase == 0:
            items_a.remove(A_ITEM)
        elif phase == 1:
            items_b.remove(B_ITEM)
        elif phase == 2:
            items_a.add(B_ITEM)
            gold_a = 80
        elif phase == 3:
            items_b.add(A_ITEM)
            gold_b = 220
        else:
            raise AssertionError("unknown phase")
    return (tuple(sorted(items_a)), tuple(sorted(items_b)), gold_a, gold_b)

def run():
    schedules = [p for p in permutations(range(4))
                 if p.index(0) < p.index(2) and p.index(1) < p.index(3)]
    assert len(schedules) == 6
    observed = {signature(p[:k]) for p in schedules for k in range(5)}
    ordered = {signature(tuple(range(k))) for k in range(5)}
    assert len(observed) == 9 and len(ordered) == 5
    assert ordered <= observed
    extras = observed - ordered
    assert len(extras) == 4

    # Each is a legal independent-worker prefix, but the strict sequential
    # SQLite PREPARED validator would quarantine it. This is a documented
    # native scheduling acceptance gap, NOT a reason to loosen its safety.
    expected_extras = {
        ((A_ITEM,), (), 100, 200),
        ((B_ITEM,), (B_ITEM,), 80, 200),
        ((), (A_ITEM,), 100, 220),
        ((A_ITEM,), (A_ITEM,), 100, 220),
    }
    assert extras == expected_extras
    assert signature((0, 1, 2, 3)) == signature((1, 3, 0, 2))
    for p in schedules:
        assert signature(p) == ( (B_ITEM,), (A_ITEM,), 80, 220 )
    print("PB59 independent-realm schedule oracle: PASS "
          "schedules=6 prefixes=30 unique=9 ordered=5 unmodeled=4")

if __name__ == "__main__":
    run()
