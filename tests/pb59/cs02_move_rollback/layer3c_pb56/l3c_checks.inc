// ===== CS02 Layer 3c: PB56 focused rows (new). Appended by run_layer3c.py after the unchanged PR77 Layer 3 rows
// and the unchanged PR80 Layer 3b rows (their main() functions are renamed cs02_l3_main / cs02_l3b_main).
// Runs against PB54 ec276855 (baseline) and PB56 66c66ddc (candidate). Written independently of CS01's
// comparator tests. Expectations were registered before the first PB56 run; divergences are printed and kept.
namespace l3c
{
using PlayerbotsAutoGearExec::StripResult;
using l3b::Strip;
using l3b::CustodyOf;
using l3b::MetaOf;
using l3b::CustodyDiff;
using l3b::MetaDiff;
using l3b::Join;

[[maybe_unused]] static Bag* AddBag(World& w, uint8 slot, uint64 guid)
{
    w.p.ownership.emplace_back(new Bag(guid, &w.p.map));
    Bag* bag = static_cast<Bag*>(w.p.ownership.back().get());
    w.p.slots.at(E(slot)) = bag; w.p.bags[slot] = bag; w.p.fields[16 + slot * 2] = uint32(guid);
    return bag;
}
[[maybe_unused]] static void Move(World& w, uint16 from, uint16 to)
{
    Item* i = w.p.slots.at(from); w.p.slots.at(from) = nullptr; w.p.slots.at(to) = i;
    if ((from >> 8) == 255) w.p.fields[16 + (from & 255) * 2] = 0;
    if ((to >> 8) == 255) w.p.fields[16 + (to & 255) * 2] = uint32(i->GetGUID().value);
}
[[maybe_unused]] static std::map<uint64, int> Owned(World const& w)
{
    std::map<uint64, int> m;
    for (auto const& [pos, item] : w.p.slots) if (item) ++m[item->GetGUID().value];
    return m;
}

#if CS02_CORE_BATCH
[[maybe_unused]] static void CustodyRows()
{
    // Setup shared by every K row: bag 5000 at E19, shirt 950 at E3, tabard 960 at E18, 802 at E31 with the
    // same entry and count as 800 (so only GUID identity tells them apart).
    struct Case { char const* id; int expect; std::function<void(World&)> change; char const* note; };
    enum { DONE = 0, RR = 1 };
    std::vector<Case> const cases = {
        {"K0 control (setup only)", DONE, [](World&) {}, ""},
        {"K1 metadata: 800.flags + enchant, no count", DONE, [](World& w) {
             auto& f = w.p.GetItemByPos(E(30))->facts; f.flags = 1; f.enchants[0].id = 77; }, ""},
        {"K2 metadata: 800.count 1->5 only", DONE, [](World& w) { w.p.GetItemByPos(E(30))->facts.count = 5; },
         "CS01 brief lists count as permitted metadata"},
        {"K3 after-snapshot missing (E30 unobservable)", RR, [](World& w) { w.bot.failCapturePos = E(30); }, ""},
        {"K4 non-slot: shirt GUID 950->951", RR, [](World& w) { w.p.slots.at(E(3)) = nullptr; w.p.Add(E(3), 951); }, ""},
        {"K5 non-slot: tabard 960 removed", RR, [](World& w) { w.p.slots.at(E(18)) = nullptr; w.p.fields[16 + 18 * 2] = 0; }, ""},
        {"K6 non-slot: bag 5000 replaced by 5001", RR, [](World& w) { AddBag(w, 19, 5001); }, ""},
        {"K7 non-slot: shirt moved E3->E33", RR, [](World& w) { Move(w, E(3), E(33)); }, ""},
        {"K8 non-slot metadata: shirt durability", DONE, [](World& w) { w.p.GetItemByPos(E(3))->facts.durability = 1; }, ""},
        {"K9 non-slot custody: shirt owner 1->99 (same GUID)", RR, [](World& w) { w.p.GetItemByPos(E(3))->facts.owner = 99; },
         "CS01-declared limit: non-slot items only by GUID"},
        {"K10 non-slot custody: bag owner 1->99 (same GUID)", RR, [](World& w) { w.p.GetItemByPos(E(19))->facts.owner = 99; },
         "CS01-declared limit: non-slot items only by GUID"},
        {"K11 swap 800<->802 (same entry/count)", RR, [](World& w) {
             Item* a = w.p.slots.at(E(30)); Item* b = w.p.slots.at(E(31)); w.p.slots.at(E(30)) = b; w.p.slots.at(E(31)) = a; }, ""},
        {"K12 new bag 5002 at E20 (4 empty slots)", RR, [](World& w) { AddBag(w, 20, 5002); }, ""},
        // K13 was ADDED AFTER the first mutant run (W5 survived): a context change that SameContext does not cover.
        {"K13 context: bot died during the batch (alive 1->0)", RR, [](World& w) { w.p.alive = false; },
         "context, not custody; the conservative stop is expected"},
        // K13b ADDED AFTER the second mutant run: the real Core already answers RR to K13, so the bot dies right
        // after Core returns Applied (harness hook at CORE_RETURN) to reach the PB56 alive comparison on its own.
        {"K13b context: bot died right after native Applied", RR, [](World& w) { w.bot.afterCore = [&w] { w.p.alive = false; }; },
         "context, not custody; the conservative stop is expected"},
    };
    std::map<uint64, uint16> const planned = {{701, E(23)}, {700, E(24)}};
    for (auto const& c : cases)
    {
        auto w = NewWorld();
        AddBag(*w, 19, 5000);
        w->Put(E(3), 950, 1); w->Put(E(18), 960, 1);
        Item* twin = w->Put(E(31), 802, 10);
        twin->facts.entry = w->p.GetItemByPos(E(30))->facts.entry;
        auto cb = CustodyOf(w->p); auto mb = MetaOf(w->p); auto before = Owned(*w);
        auto rr = Strip(*w, c.change);
        auto cd = CustodyDiff(cb, CustodyOf(w->p), planned);
        auto md = MetaDiff(mb, MetaOf(w->p));
        std::string const fields = "custody=" + Join(cd) + " metadata=" + Join(md);
        StripResult const want = c.expect == DONE ? StripResult::Done : StripResult::RecoveryRequired;
        Row(c.id, c.expect == DONE ? "Done (metadata permitted)" : "RecoveryRequired (custody/snapshot)",
            std::string(SN(rr.r)) + "; core=" + rr.core + "; obsReads=" + std::to_string(rr.obsN) + "; " + fields, rr.r == want, c.note);
        // Preservation: the planned items are never lost or duplicated, whatever the verdict.
        auto after = Owned(*w);
        Require(after[700] == 1 && after[701] == 1, std::string(c.id) + ": 700/701 owned exactly once");
    }
}

// The custody check must not read the Player at all: it uses the fingerprint already taken.
[[maybe_unused]] static void ReadRows()
{
    {
        auto w = NewWorld();
        auto rr = Strip(*w, nullptr);
        Row("L1 Applied control: observation reads after Core", "47 (PB54 R0 value)", rr.obs + " all=" + rr.all, rr.obsN == 47);
    }
    {
        auto w = NewWorld();
        auto rr = Strip(*w, [](World& x) { Move(x, E(30), E(31)); });
        Row("L2 Applied + custody discrepancy: observation reads after Core", "RR; <= 47 (no extra reads)", std::string(SN(rr.r)) + "; " + rr.obs + " all=" + rr.all,
            rr.r == StripResult::RecoveryRequired && rr.obsN <= 47);
    }
    {
        auto w = NewWorld();
        w->bot.forceOutcome = static_cast<int>(PB59::InventoryBatchOutcome::RecoveryRequired);
        auto rr = Strip(*w, [](World& x) { Move(x, E(30), E(31)); });
        Row("L3 native RR + custody change: Player reads after Core", "0", rr.all, rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
    {
        auto w = NewWorld();
        Bot other; other.p = &w->p;
        auto rr = Strip(*w, [&](World& x) { Move(x, E(30), E(31)); ObjectAccessor::g_find = [&](ObjectGuid) { return &other; }; });
        ObjectAccessor::g_find = nullptr;
        Row("L4 lost context + custody change: reads of old bot after Core", "0", rr.all, rr.r == StripResult::RecoveryRequired && rr.allN == 0);
    }
}
#endif
}

int main()
{
    int const rc = cs02_l3b_main();
    int const c0 = g_checks, f0 = g_fail, d0 = g_div;
#if CS02_CORE_BATCH
    l3c::CustodyRows();
    l3c::ReadRows();
#endif
    std::printf("CS02 layer3c new rows: %d checks, %d property failures, %d divergences from pre-registered expectations\n",
        g_checks - c0, g_fail - f0, g_div - d0);
    return (rc || g_fail) ? 1 : 0;
}
