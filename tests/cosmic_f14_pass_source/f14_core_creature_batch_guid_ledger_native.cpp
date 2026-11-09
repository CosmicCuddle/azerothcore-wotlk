#include "../../src/server/game/Entities/Object/Updates/F14CreatureBatchGuidCandidateLedger.h"
#include <cstdint>
#include <iostream>

int main()
{
    using namespace CosmicF14;
    using Ledger = CreatureBatchGuidCandidateLedger<std::uint64_t, 4>;
    using Dir = CreatureBatchDirection;
    int failed = 0;
    auto check = [&](bool yes, char const* name) {
        if (!yes) { ++failed; std::cerr << "FAIL " << name << '\n'; }
    };
    Ledger a;
    check(a.Entries().empty() && !a.Poisoned(), "new ledger empty");
    check(a.Record(10, Dir::Create), "regular creature create");
    check(a.Record(20, Dir::OutOfRange), "different creature out");
    check(a.Entries().size() == 2, "two specific candidate GUIDs");
    check(a.Entries()[0].guid == 10 && a.Entries()[0].direction == Dir::Create,
          "create exactly attached");
    check(a.Entries()[1].guid == 20 && a.Entries()[1].direction == Dir::OutOfRange,
          "out exactly attached");
    check(a.CompatibleWithRealOutGuids({20}),
          "exact one OUT for nominated OUT and none for nominated CREATE");
    check(!a.CompatibleWithRealOutGuids({}),
          "missing real OUT list cannot establish nominated OUT");
    check(!a.CompatibleWithRealOutGuids({20, 20}),
          "duplicate real OUT must invalidate all F14 evidence");
    check(!a.CompatibleWithRealOutGuids({20, 10}),
          "generic OUT for an intended CREATE is an ambiguity");
    check(a.CompatibleWithRealOutGuids({20, 30}),
          "unrelated generic OUT does not imply nominated creature GUID");
    check(!a.Record(10, Dir::OutOfRange), "same GUID contradictory direction refused");
    check(a.Poisoned() && a.Entries().empty(), "ambiguity poisons entire packet");
    check(!a.Record(30, Dir::Create), "poison permanent until explicit reset");

    a.Clear();
    check(!a.Poisoned() && a.Entries().empty(), "explicit clear resets ledger");
    check(a.Record(10, Dir::OutOfRange), "new packet can use same GUID");
    check(a.CompatibleWithRealOutGuids({10}), "one exact real OUT matches");
    check(!a.CompatibleWithRealOutGuids({10, 10}), "duplicate generic OUT fails");
    check(!a.Record(10, Dir::OutOfRange), "same-direction duplicate also poisons");

    a.Clear();
    check(!a.Record(0, Dir::Create), "missing GUID refused");
    check(a.Poisoned(), "bad GUID poisons");

    a.Clear();
    check(!a.Record(9, static_cast<Dir>(3)), "invalid direction refused");
    check(a.Poisoned(), "invalid direction poisons");

    a.Clear();
    check(a.Record(1, Dir::Create), "capacity 1");
    check(a.Record(2, Dir::Create), "capacity 2");
    check(a.Record(3, Dir::OutOfRange), "capacity 3");
    check(a.Record(4, Dir::OutOfRange), "capacity 4");
    check(!a.Record(5, Dir::Create), "overflow refused");
    check(a.Poisoned() && a.Entries().empty(), "overflow poisons whole batch");
    a.Clear();
    check(a.Record(7, Dir::Create), "reset after overflow restores candidate");
    a.Poison();
    check(a.Entries().empty() && a.Poisoned(), "merge poisons candidates");
    std::cout << "Core F14 actual pure GUID-sidecar ledger native: " << failed
              << " failures, never client ACK or creature restore\n";
    return failed ? 1 : 0;
}
