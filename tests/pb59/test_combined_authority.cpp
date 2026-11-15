#include "CombinedAuthorityPrototype.h"
#include <cassert>
using namespace PB59Prototype;
int main()
{
    ProgressionPublication p{42, 7, 10, 1, 3, 3, true, true};
    EquipmentPublication e{42, 7, 20, 45, 50, true};
    auto rp = [&](auto) { return p; };
    auto re = [&](auto) { return e; };
    auto expected = ReadCoherent(42, rp, re);
    assert(expected);
    assert(MatchesFinalB(*expected, rp, re));
    assert(!ReadCoherent(0, rp, re));
    assert(!ReadCoherent(43, rp, re));
    e.guid = 99;
    assert(!MatchesFinalB(*expected, rp, re));
    e = expected->equipment;
    e.session++;
    assert(!ReadCoherent(42, rp, re));
    e = expected->equipment;
    e.known = false;
    assert(!MatchesFinalB(*expected, rp, re));
    e = expected->equipment;
    e.average = 0;
    assert(!ReadCoherent(42, rp, re));
    e = expected->equipment;
    e.level = 0;
    assert(!ReadCoherent(42, rp, re));
    e = expected->equipment;
    e.revision = 0;
    assert(!ReadCoherent(42, rp, re));
    e = expected->equipment;
    e.revision++; // same average, different equipment generation
    assert(!MatchesFinalB(*expected, rp, re));
    e = expected->equipment;
    p.known = false;
    assert(!MatchesFinalB(*expected, rp, re));
    p = expected->progression;
    p.revision++;
    assert(!MatchesFinalB(*expected, rp, re));
    p = expected->progression;
    auto changeProgression = [&](auto) { ++p.revision; return e; };
    assert(!MatchesFinalB(*expected, rp, changeProgression));
    p = expected->progression;
    auto changeSession = [&](auto) { ++p.session; ++e.session; return e; };
    assert(!MatchesFinalB(*expected, rp, changeSession));
    p = expected->progression;
    e = expected->equipment;
    int reads = 0;
    auto laterEquipment = [&](auto) {
        if (++reads == 2) ++e.revision;
        return p;
    };
    // B linearizes at the middle read. Later C can observe a new revision.
    assert(MatchesFinalB(*expected, laterEquipment, re));
    assert(!MatchesFinalB(*expected, rp, re));
    e = expected->equipment;
    p.rawStage++;
    assert(!MatchesFinalB(*expected, rp, re));
    p = expected->progression;
    e.level++;
    assert(!MatchesFinalB(*expected, rp, re));
    e = expected->equipment;
    p.revision = 0;
    assert(!ReadCoherent(42, rp, re));
    p = expected->progression;
    p.generation = 0;
    assert(!ReadCoherent(42, rp, re));
    p = expected->progression;
    p.generation++;
    assert(!MatchesFinalB(*expected, rp, re));
    p = expected->progression;
    p.rawStage = 19;
    assert(!ReadCoherent(42, rp, re));
    p = expected->progression;
    p.effectiveStage = 4;
    assert(!ReadCoherent(42, rp, re));
    p = expected->progression;
    p.gatedHuman = false;
    assert(!MatchesFinalB(*expected, rp, re));
}
