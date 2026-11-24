// ===== CS02 Layer 3d: test-first specification for non-slot custody (CS01 #52 7142198055, K9/K10 gap).
// Appended after the unchanged PR77/PR80/PR83 rows (their main() functions are renamed). The specification rows
// state what an extended PB custody check SHOULD do; they are run against real PB56 66c66ddc (gap expected to show
// as divergences) and against a TEST-ONLY throwaway sketch of PB56 compiled here (never committed to a PB branch).
// CS02_REALCAP=1 builds use a fixture capture made closer to real Core74 CapturePB59InventorySlot
// (PlayerStorage.cpp:531-534, :543-546, :551-552, :564); only the L3d rows run in that build.
namespace l3d
{
using PlayerbotsAutoGearExec::StripResult;
using l3b::Strip;
using l3b::CustodyOf;
using l3b::CustodyDiff;
using l3b::Join;
using l3c::Move;

[[maybe_unused]] static Bag* AddBag(World& w, uint16 pos, uint64 guid, bool equipped = true)
{
    w.p.ownership.emplace_back(new Bag(guid, &w.p.map));
    Bag* bag = static_cast<Bag*>(w.p.ownership.back().get());
    g_cs02Bags.insert(bag);
    w.p.slots.at(pos) = bag;
    if ((pos >> 8) == 255) w.p.fields[16 + (pos & 255) * 2] = uint32(guid);
    if (equipped) w.p.bags[uint8(pos & 255)] = bag;
    return bag;
}
[[maybe_unused]] static void Replace(World& w, uint16 pos, uint64 guid)
{
    w.p.slots.at(pos) = nullptr;
    w.p.Add(pos, guid);
}
[[maybe_unused]] static void MoveBag(World& w, uint8 from, uint8 to)
{
    Bag* bag = w.p.bags.at(from);
    w.p.bags.erase(from); w.p.bags[to] = bag;
    Move(w, E(from), E(to));
}

#if CS02_CORE_BATCH
struct Case { std::string id; bool rr; std::function<void(World&)> change; };
[[maybe_unused]] static void SpecRows()
{
    std::vector<Case> cases;
    // Specification (test-first): owner, identity, count and location of shirt (E3), tabard (E18) and bag (E19).
    for (auto const& [name, pos, guid] : std::vector<std::tuple<std::string, uint16, uint64>>{{"shirt", E(3), 950}, {"tabard", E(18), 960}})
    {
        cases.push_back({"S " + name + " owner 1->99 (same GUID)", true, [pos](World& w) { w.p.GetItemByPos(pos)->facts.owner = 99; }});
        cases.push_back({"S " + name + " identity " + std::to_string(guid) + "->" + std::to_string(guid + 1), true,
                         [pos, guid](World& w) { Replace(w, pos, guid + 1); }});
        // Added after sketch mutant X3 survived (fixture entry = GUID + 100 made identity rows also change entry).
        cases.push_back({"S " + name + " identity " + std::to_string(guid) + "->" + std::to_string(guid + 1) + " same entry (late row)", true,
                         [pos, guid](World& w) { Replace(w, pos, guid + 1); w.p.GetItemByPos(pos)->facts.entry = uint32(guid + 100); }});
        cases.push_back({"S " + name + " count 1->2", true, [pos](World& w) { w.p.GetItemByPos(pos)->facts.count = 2; }});
        cases.push_back({"S " + name + " location ->E34", true, [pos](World& w) { Move(w, pos, E(34)); }});
        cases.push_back({"C " + name + " durability only (control)", false, [pos](World& w) { w.p.GetItemByPos(pos)->facts.durability = 1; }});
    }
    cases.push_back({"S bag owner 1->99 (same GUID)", true, [](World& w) { w.p.GetItemByPos(E(19))->facts.owner = 99; }});
    cases.push_back({"S bag identity 5000->5001", true, [](World& w) { AddBag(w, E(19), 5001); }});
    cases.push_back({"S bag identity 5000->5001 same entry (late row)", true, [](World& w) { AddBag(w, E(19), 5001)->facts.entry = 5100; }});
    cases.push_back({"S bag count 1->2", true, [](World& w) { w.p.GetItemByPos(E(19))->facts.count = 2; }});
    cases.push_back({"S bag location E19->E20", true, [](World& w) { MoveBag(w, 19, 20); }});
    cases.push_back({"C bag durability only (control)", false, [](World& w) { w.p.GetItemByPos(E(19))->facts.durability = 1; }});
    cases.push_back({"C no change (control)", false, [](World&) {}});
    std::map<uint64, uint16> const planned = {{701, E(23)}, {700, E(24)}};
    for (auto const& c : cases)
    {
        g_cs02Bags.clear();  // freed Bag addresses can be reused by later Items
        auto w = NewWorld();
        AddBag(*w, E(19), 5000);
        w->Put(E(3), 950, 1); w->Put(E(18), 960, 1);
        auto cb = CustodyOf(w->p);
        auto rr = Strip(*w, c.change);
        auto cd = CustodyDiff(cb, CustodyOf(w->p), planned);
        std::string const fields = "custody=" + Join(cd);
        Row("D " + c.id, c.rr ? "RecoveryRequired" : "Done", std::string(SN(rr.r)) + "; core=" + rr.core + "; " + fields,
            rr.r == (c.rr ? StripResult::RecoveryRequired : StripResult::Done));
    }
}

// No Player reads and no Item field reads after native RecoveryRequired or lost context; when Applied, Item field
// reads happen only after SameContext re-resolved the bot.
[[maybe_unused]] static void ReadRows()
{
    struct Probe { std::string id; int mode; };
    for (auto const& pr : std::vector<Probe>{{"native RR", 0}, {"lost context (Player* swap)", 1}, {"Applied, same context", 2}})
    {
        g_cs02Bags.clear();  // freed Bag addresses can be reused by later Items
        auto w = NewWorld();
        AddBag(*w, E(19), 5000);
        w->Put(E(3), 950, 1); w->Put(E(18), 960, 1);
        Bot other; other.p = &w->p;
        int atCore = -1, atLookup = -1;
        if (pr.mode == 0) w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::RecoveryRequired);
        w->bot.afterCore = [&] { atCore = g_itemFieldReads; };
        ObjectAccessor::g_find = [&](ObjectGuid) -> Bot* { if (atLookup < 0) atLookup = g_itemFieldReads; return pr.mode == 1 ? &other : &w->bot; };
        auto rr = Strip(*w, [](World& x) { x.p.GetItemByPos(E(3))->facts.owner = 99; });
        ObjectAccessor::g_find = nullptr;
        int const total = g_itemFieldReads - atCore;
        int const beforeLookup = atLookup < 0 ? total : atLookup - atCore;
        if (pr.mode < 2)
            Row("Z " + pr.id + ": Player/Item reads after Core", "RR; 0 Player reads; 0 Item field reads",
                std::string(SN(rr.r)) + "; player=" + rr.all + "; itemFields=" + std::to_string(total),
                rr.r == StripResult::RecoveryRequired && rr.allN == 0 && total == 0);
        else
            Row("Z " + pr.id + ": Item field reads before SameContext", "0 (all after re-resolve)",
                "beforeLookup=" + std::to_string(beforeLookup) + " total=" + std::to_string(total), beforeLookup == 0);
    }
}

// Availability observation under the closer-to-real capture: a spare (unequipped) bag in the backpack.
[[maybe_unused]] static void AvailabilityRows()
{
#if CS02_REALCAP
    g_cs02Bags.clear();  // freed Bag addresses can be reused by later Items
        auto w = NewWorld();
    AddBag(*w, E(33), 5100, false);
    auto r = w->inv.StripToBags((1u << 15) | (1u << 16));
    Row("V spare bag in backpack E33 (real capture refuses IsBag, PlayerStorage.cpp:564)", "Refused; 0 Core calls (availability)",
        std::string(SN(r)) + "; core=" + std::to_string(w->bot.coreCalls), r == StripResult::Refused && w->bot.coreCalls == 0);
#endif
}
#endif
}

int main()
{
    int rc = 0;
#if !CS02_REALCAP
    rc = cs02_l3c_main();
#endif
    int const c0 = g_checks, f0 = g_fail, d0 = g_div;
#if CS02_CORE_BATCH
    l3d::SpecRows();
    l3d::ReadRows();
    l3d::AvailabilityRows();
#endif
    std::printf("CS02 layer3d%s new rows: %d checks, %d property failures, %d divergences from pre-registered expectations\n",
        CS02_REALCAP ? " (realcap)" : "", g_checks - c0, g_fail - f0, g_div - d0);
    return (rc || g_fail) ? 1 : 0;
}
