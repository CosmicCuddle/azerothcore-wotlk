// CS02 / PB59 -- independent fault controls against Codex's ACTUAL executor body.
// Included at the end of Codex's pinned fixture (doubles) by run_native_v1.py; the
// executor/admission/stamp/item-mod/combat-journal bodies are extracted verbatim
// from Core74 7fdc3a655f2f46cf8cba008a8f9226413f61e7f6. The Player/Item/Bag/
// Session/Map/aura/policy/transfer/script objects are Codex's DOUBLES, patched by
// the runner only for per-owner GUIDs. Expected outcomes below were written from
// the contract (CORE74_NATIVE_INVENTORY_BATCH.md) and CS02's earlier controls
// BEFORE the first run; observed != expected is printed as DIVERGENCE, never
// silently accepted. Property violations (CS01 #52 7133329461) are FAIL.
#include <cinttypes>
#include <cstring>
#include <string>
#include <thread>
using Outcome = PB59::InventoryBatchOutcome;
using Failure = PB59::InventoryBatchFailure;
using Moves = std::vector<PB59::InventoryBatchMove>;
static unsigned Checks = 0, Fails = 0, Divergences = 0;
static void Require(bool ok, std::string const& what)
{
    ++Checks;
    if (!ok) { ++Fails; std::printf("FAIL %s\n", what.c_str()); }
}
static char const* N(Outcome o)
{
    switch (o)
    {
        case Outcome::Refused: return "Refused";
        case Outcome::Applied: return "Applied";
        case Outcome::Restored: return "Restored";
        case Outcome::RecoveryRequired: return "RecoveryRequired";
    }
    return "UNKNOWN";
}
static char const* F(Failure f)
{
    switch (f)
    {
        case Failure::None: return "None"; case Failure::Context: return "Context"; case Failure::Plan: return "Plan";
        case Failure::UnsupportedState: return "UnsupportedState"; case Failure::Policy: return "Policy";
        case Failure::Effects: return "Effects"; case Failure::Links: return "Links"; case Failure::Restoration: return "Restoration";
    }
    return "UNKNOWN";
}
constexpr uint16 E(unsigned s) { return uint16(65280 + s); }
constexpr uint16 P(unsigned s) { return uint16(65280 + 23 + s); }
constexpr uint16 B19(unsigned s) { return uint16((19 << 8) | s); }
static Bag* PutBag(Player& p, uint8 slot, uint64 guid)
{
    auto bag = std::make_unique<Bag>(guid, &p.map);
    bag->facts.owner = p.GetGUID().value;
    Bag* raw = bag.get();
    p.bags[slot] = raw; p.slots.at(uint16(65280 + slot)) = raw; p.fields[16 + slot * 2] = uint32(guid);
    p.ownership.push_back(std::move(bag));
    return raw;
}
static PB59::InventoryBatchMove M(Player& p, uint16 from, uint16 to)
{
    return {*p.CapturePB59InventorySlot(from), *p.CapturePB59InventorySlot(to)};
}
static PB59::InventoryBatchResult Run(Player& p, Moves const& plan, std::optional<PB59::Stamp> stamp = {})
{
    if (!stamp) stamp = p.CapturePB59InventoryStamp();
    if (!stamp) { PB59::InventoryBatchResult r; r.failure = Failure::Context; return r; }
    return p.ExecutePB59InventoryBatchPlan(p.GetGUID().value, p._pb59Transaction.OwnerIncarnation(),
        p.session.incarnation, *stamp, plan);
}
struct Before
{
    std::array<uint32, PLAYER_END> fields;
    float flat[2][2], pct[2][2];
    std::vector<Item*> queue;
    std::set<void const*> updates;
    bool pendingLogout, pendingSave;
    explicit Before(Player const& p) : fields(p.fields), queue(p.m_itemUpdateQueue), updates(p.map.updates),
        pendingLogout(p._pb59Transaction.PendingLogout()), pendingSave(p._pb59Transaction.PendingSave())
    { std::memcpy(flat, p.m_auraFlatModifiersGroup, sizeof flat); std::memcpy(pct, p.m_auraPctModifiersGroup, sizeof pct); }
};
struct Expect
{
    std::vector<Outcome> allowed;
    bool pendingLogout = false, pendingSave = false; // intents that must be retained afterwards
    char const* basis = "";
};
// Independent post-state oracle. Returns false on a property violation.
static bool Judge(std::string const& id, Player& p, Moves const& plan, Before const& b,
    PB59::InventoryBatchResult const& r, Expect const* x)
{
    unsigned const before = Fails;
    auto req = [&](bool ok, char const* what) { Require(ok, id + ": " + what); };
    using Phase = PB59::TransactionAdmission::Phase;
    auto const phase = p._pb59Transaction.State();
    bool const quarantined = p._pb59Equipment.InventoryQuarantined();
    auto atSource = [&](PB59::InventoryBatchMove const& m) {
        auto now = p.CapturePB59InventorySlot(m.source.position); return now && *now == m.source; };
    auto destEmpty = [&](PB59::InventoryBatchMove const& m) {
        auto now = p.CapturePB59InventorySlot(m.destination.position); return now && *now == m.destination; };
    req(r.recoveryCount <= plan.size(), "recoveryCount within plan size");
    switch (r.outcome)
    {
        case Outcome::Applied:
            req(r.failure == Failure::None && r.recoveryCount == 0, "Applied is consistent (failure None, no GUIDs)");
            req(phase == Phase::Idle && !quarantined, "Applied finished the ticket and released quarantine");
            for (auto const& m : plan)
            {
                auto* item = p.GetItemByPos(m.destination.position);
                req(item && item->facts == *m.source.item && !p.GetItemByPos(m.source.position),
                    "Applied: every original at its exact destination");
            }
            break;
        case Outcome::Refused:
            req(p.transferOrder.empty(), "Refused: no link transfer accepted");
            for (auto const& m : plan) req(atSource(m) && destEmpty(m), "Refused: originals and destinations unchanged");
            req(phase == Phase::Idle && !quarantined, "Refused: ledger idle, not quarantined");
            break;
        case Outcome::Restored:
            for (auto const& m : plan) req(atSource(m) && destEmpty(m), "Restored: original custody (slot facts incl. bag GUID)");
            req(p.fields == b.fields, "Restored: published fields original");
            req(!std::memcmp(b.flat, p.m_auraFlatModifiersGroup, sizeof b.flat) &&
                !std::memcmp(b.pct, p.m_auraPctModifiersGroup, sizeof b.pct), "Restored: hidden combat modifiers original");
            req(p.m_itemUpdateQueue == b.queue && p.map.updates == b.updates, "Restored: queue/update registrations original");
            req(phase == Phase::Idle && !quarantined, "Restored: ledger idle, quarantine released");
            req(p.session.player == &p && p.session.incarnation == 7 && p.inWorld && !p.session.logout,
                "Restored: owner context original");
            break;
        case Outcome::RecoveryRequired:
        {
            req(quarantined && phase == Phase::RecoveryRequired, "RecoveryRequired: quarantined and ledger RecoveryRequired");
            // Exact GUID rule from the contract: originals whose location/instance is not re-proven.
            std::vector<uint64> want;
            for (auto const& m : plan) if (!atSource(m)) want.push_back(m.source.item->guid);
            std::vector<uint64> got(r.recoveryGuids.begin(), r.recoveryGuids.begin() + std::min<std::size_t>(r.recoveryCount, 19));
            req(got == want, "RecoveryRequired: GUID list == originals not re-proven at source");
            break;
        }
        default:
            req(false, "unknown outcome value");
    }
    if (x)
    {
        if (x->pendingLogout) req(p._pb59Transaction.PendingLogout(), "deferred logout intent retained");
        if (x->pendingSave) req(p._pb59Transaction.PendingSave(), "deferred save intent retained");
        if (b.pendingLogout) req(p._pb59Transaction.PendingLogout(), "earlier logout intent retained");
        bool const asExpected = std::find(x->allowed.begin(), x->allowed.end(), r.outcome) != x->allowed.end();
        std::string allowed;
        for (auto o : x->allowed) allowed += std::string(allowed.empty() ? "" : "|") + N(o);
        if (!asExpected) ++Divergences;
        std::printf("SCN %-5s expected=%-28s observed=%-16s failure=%-16s guids=%u %s%s\n", id.c_str(), allowed.c_str(),
            N(r.outcome), F(r.failure), unsigned(r.recoveryCount), asExpected ? "as-expected" : "DIVERGENCE",
            Fails == before ? "" : " PROPERTY-FAIL");
    }
    return Fails == before;
}

static bool Judge(std::string const& id, Player& p, Moves const& plan, Before const& b,
    PB59::InventoryBatchResult const& r, Expect const& x)
{
    return Judge(id, p, plan, b, r, &x);
}

static int Scenarios()
{
    std::printf("REAL bodies: ExecutePB59InventoryBatchPlan, CheckPB59InventoryBatchPlan, CapturePB59InventoryStamp, "
                "ReconcilePB59OriginalCombatEffects, _ApplyItemMods + real PB59 headers. DOUBLES: Codex fixture "
                "(Player/Item/Bag/Session/Map/aura/policy/RelocatePB59InventoryLinks/scripts/CapturePB59InventorySlot)\n");
    // ---------------- (a) destination bag replacement
    {   // A1 raw (unsupported-bypass) replacement of move 0's destination bag during its unequip effects
        Player p; PutBag(p, 19, 500); p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), B19(0)), M(p, E(2), B19(1))}; Before b(p);
        p.modsHook = [&] { if (p.modsCalls == 1) PutBag(p, 19, 501); };
        auto r = Run(p, plan);
        Judge("A1", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
        Require(!(p.GetBagByPos(19)->GetGUID().value == 501 && (p.GetItemByPos(B19(0)) || p.GetItemByPos(B19(1)))),
            "A1: no original stored into the replacement container");
    }
    {   // A2 replacement attempted through a guarded producer (reservation CheckItem, as Bag/Player storage producers do)
        Player p; PutBag(p, 19, 500); p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), B19(0)), M(p, E(2), B19(1))}; Before b(p);
        p.modsHook = [&] { if (p.modsCalls == 1) { PB59::InventoryReservation::CheckItem(p.GetBagByPos(19), 0); PutBag(p, 19, 501); } };
        auto r = Run(p, plan);
        Judge("A2", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
        Require(p.GetBagByPos(19)->GetGUID().value == 500, "A2: guarded producer could not replace the reserved bag");
    }
    {   // A3 equip effects of move 0 replace move 1's destination bag
        Player p; PutBag(p, 19, 500); p.Add(P(0), 20); p.Add(E(2), 12);
        Moves plan{M(p, P(0), E(1)), M(p, E(2), B19(0))}; Before b(p);
        p.modsHook = [&] { if (p.modsCalls == 1) PutBag(p, 19, 501); };
        auto r = Run(p, plan);
        Judge("A3", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
        Require(!p.GetItemByPos(B19(0)), "A3: move 1 not written into the replacement container");
    }
    {   // A4 bag replaced during REVERSE effects (forward link 3 refused)
        Player p; PutBag(p, 19, 500); p.Add(E(0), 10); p.Add(E(2), 12); p.Add(E(4), 14);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), B19(0)), M(p, E(4), B19(1))}; Before b(p);
        p.rejectTransfer = 3; bool done = false;
        p.modsHook = [&] { if (p.transfers >= 3 && !done) { done = true; PutBag(p, 19, 501); } };
        auto r = Run(p, plan);
        Judge("A4", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    // ---------------- (b) A -> B -> A
    auto pair = [](Player& a, Player& b) {
        b.guid = 2;
        a.Add(E(0), 10); a.Add(E(2), 12); a.Add(E(4), 14);
        b.Add(E(0), 30); b.Add(E(2), 32);
    };
    {   // B0 control
        Player a, bb; pair(a, bb);
        Moves pa{M(a, E(0), P(0)), M(a, E(2), P(1))}, pb{M(bb, E(0), P(0)), M(bb, E(2), P(1))};
        Before ba(a), bbf(bb); PB59::InventoryBatchResult rb; bool ran = false;
        a.modsHook = [&] { if (!ran) { ran = true; rb = Run(bb, pb); } };
        auto ra = Run(a, pa);
        Judge("B0a", a, pa, ba, ra, Expect{{Outcome::Applied}});
        Judge("B0b", bb, pb, bbf, rb, Expect{{Outcome::Applied}});
    }
    {   // B1 A's effects run B's batch; B's effects start another batch on A
        Player a, bb; pair(a, bb);
        Moves pa{M(a, E(0), P(0)), M(a, E(2), P(1))}, pb{M(bb, E(0), P(0)), M(bb, E(2), P(1))};
        Moves inner{M(a, E(4), P(2))};
        auto innerStamp = a.CapturePB59InventoryStamp();
        Before ba(a), bbf(bb); PB59::InventoryBatchResult rb, ri; bool ranB = false, ranI = false;
        auto transfersBefore = std::size_t(0);
        a.modsHook = [&] { if (!ranB) { ranB = true; rb = Run(bb, pb); } };
        bb.modsHook = [&] { if (!ranI) { ranI = true; transfersBefore = a.transferOrder.size(); ri = Run(a, inner, innerStamp); } };
        auto ra = Run(a, pa);
        std::printf("SCN B1i  inner A (re-entry from B's effects): observed=%s failure=%s; A links before inner=%zu\n",
            N(ri.outcome), F(ri.failure), transfersBefore);
        Require(ranI && ri.outcome == Outcome::Refused, "B1: inner A re-entry refused");
        Require(std::count(a.transferOrder.begin(), a.transferOrder.end(), uint64(14)) == 0, "B1: inner A moved nothing");
        Judge("B1b", bb, pb, bbf, rb, Expect{{Outcome::Applied}, false, false, "B independent of A"});
        Judge("B1a", a, pa, ba, ra, Expect{{Outcome::RecoveryRequired}, false, false,
            "CS02 #51 7133592671 finding 2: same-owner re-entry during A's effects fails A closed (Codex unanswered)"});
    }
    {   // B2 B's effects attempt a GUARDED public mutation of A
        Player a, bb; pair(a, bb);
        Moves pa{M(a, E(0), P(0)), M(a, E(2), P(1))}, pb{M(bb, E(0), P(0)), M(bb, E(2), P(1))};
        Before ba(a), bbf(bb); PB59::InventoryBatchResult rb; bool ranB = false, ranM = false;
        a.modsHook = [&] { if (!ranB) { ranB = true; rb = Run(bb, pb); } };
        bb.modsHook = [&] { if (!ranM) { ranM = true; a.PublicInventoryMutation(); } };
        auto ra = Run(a, pa);
        Require(a.fields[4] != 99, "B2: guarded mutation of A was not performed");
        Judge("B2b", bb, pb, bbf, rb, Expect{{Outcome::RecoveryRequired}, false, false, "B's effects threw A's cancellation"});
        Judge("B2a", a, pa, ba, ra, Expect{{Outcome::RecoveryRequired}, false, false, "foreign producer violated A's reservation"});
    }
    {   // B3 B's effects raw-write A's next original (unsupported bypass)
        Player a, bb; pair(a, bb);
        Moves pa{M(a, E(0), P(0)), M(a, E(2), P(1))}, pb{M(bb, E(0), P(0)), M(bb, E(2), P(1))};
        Before ba(a), bbf(bb); PB59::InventoryBatchResult rb; bool ranB = false, ranM = false;
        a.modsHook = [&] { if (!ranB) { ranB = true; rb = Run(bb, pb); } };
        bb.modsHook = [&] { if (!ranM) { ranM = true; a.GetItemByPos(E(2))->facts.durability -= 1; } };
        auto ra = Run(a, pa);
        Judge("B3b", bb, pb, bbf, rb, Expect{{Outcome::Applied}});
        Judge("B3a", a, pa, ba, ra, Expect{{Outcome::RecoveryRequired}});
    }
    // ---------------- (c) pending intent and lifetime
    {   // C1 logout requested inside move 1's unequip effects (live sentinel, active ticket)
        Player p; p.Add(E(0), 10); p.Add(E(2), 12); p.Add(E(4), 14);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1)), M(p, E(4), P(2))}; Before b(p);
        PB59::TransactionAdmission::Decision d{}; bool active = false;
        p.modsHook = [&] { if (p.modsCalls == 2) { active = p._pb59Transaction.State() == PB59::TransactionAdmission::Phase::Active;
            d = p._pb59Transaction.Request(PB59::TransactionAdmission::Work::Logout); } };
        auto r = Run(p, plan);
        Require(active && d == PB59::TransactionAdmission::Decision::DeferLogout, "C1: real ledger deferred logout while active");
        Judge("C1", p, plan, b, r, Expect{{Outcome::Restored, Outcome::RecoveryRequired}, true, false,
            "contract: Restored only with full re-proof and retained intent, else RecoveryRequired"});
        Require(p.transferOrder.size() <= 2, "C1: no forward link after the intent");
    }
    {   // C2 standalone save requested inside move 0's equip effects
        Player p; p.Add(P(0), 20); p.Add(P(1), 21);
        Moves plan{M(p, P(0), E(1)), M(p, P(1), E(5))}; Before b(p);
        PB59::TransactionAdmission::Decision d{};
        p.modsHook = [&] { if (p.modsCalls == 1) d = p._pb59Transaction.Request(PB59::TransactionAdmission::Work::StandaloneSave); };
        auto r = Run(p, plan);
        Require(d == PB59::TransactionAdmission::Decision::DeferStandaloneSave, "C2: real ledger deferred the save");
        Judge("C2", p, plan, b, r, Expect{{Outcome::Restored, Outcome::RecoveryRequired}, false, true,
            "contract: Restored only with full re-proof and retained intent, else RecoveryRequired"});
    }
    {   // C3 owner leaves the world inside effects (real deallocation is not simulable in-process: see README)
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.modsHook = [&] { if (p.modsCalls == 2) { p.inWorld = false; p.session.logout = true; } };
        auto r = Run(p, plan);
        Judge("C3", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    {   // C4 session incarnation changes inside effects
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.modsHook = [&] { if (p.modsCalls == 2) ++p.session.incarnation; };
        auto r = Run(p, plan);
        Judge("C4", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    {   // C5 other-thread producer that consults only thread-local state; registry is the cross-thread guard
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        Item* original = p.GetItemByPos(E(2));
        bool frameSeen = true, reservationSeen = true, registrySeen = false, ownerRegistry = false, ran = false;
        p.modsHook = [&] { if (ran) return; ran = true; std::thread t([&] {
            frameSeen = !PB59::EffectBoundary::Check(&p);
            reservationSeen = PB59::InventoryReservation::ActiveForState(&p._pb59Equipment);
            registrySeen = PB59::InventoryQuarantine::Contains(original, 0);
            ownerRegistry = PB59::InventoryQuarantine::Contains(nullptr, p.GetGUID().value); }); t.join(); };
        auto r = Run(p, plan);
        std::printf("SCN C5t  other thread: EffectBoundary frame visible=%d, InventoryReservation visible=%d, "
                    "quarantine registry item=%d owner=%d\n", frameSeen, reservationSeen, registrySeen, ownerRegistry);
        Require(!frameSeen && !reservationSeen, "C5: thread-local frame/reservation are invisible on another thread");
        Require(registrySeen && ownerRegistry, "C5: mutex registry reports the reserved original/owner on another thread");
        Judge("C5", p, plan, b, r, Expect{{Outcome::Applied}});
    }
    {   // C6 other-thread producer ignores the registry and raw-writes A's next original (unsupported)
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        bool ran = false;
        p.modsHook = [&] { if (ran) return; ran = true; std::thread t([&] { p.GetItemByPos(E(2))->facts.durability -= 1; }); t.join(); };
        auto r = Run(p, plan);
        Judge("C6", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    // ---------------- (d) reverse failure during Restored
    {   // D1 reverse effect of move 1 throws (move 0 still forward)
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.rejectTransfer = 2; p.throwMods = 3;
        auto r = Run(p, plan);
        Judge("D1", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    {   // D2 reverse effect of move 0 throws after its link was reversed: effects-only recovery
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.rejectTransfer = 2; p.throwMods = 4;
        auto r = Run(p, plan);
        Judge("D2", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    {   // D3 logout requested during reverse
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.rejectTransfer = 2; bool done = false;
        p.modsHook = [&] { if (p.transfers >= 2 && !done) { done = true; (void)p._pb59Transaction.Request(PB59::TransactionAdmission::Work::Logout); } };
        auto r = Run(p, plan);
        Judge("D3", p, plan, b, r, Expect{{Outcome::Restored, Outcome::RecoveryRequired}, true, false,
            "contract: Restored keeps deferred intents"});
    }
    {   // D4 a registered (no-op) OnPlayerEquip script and a forward link failure
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.rejectTransfer = 2; scripts.equip = [] {};
        auto r = Run(p, plan);
        scripts.equip = nullptr;
        Judge("D4", p, plan, b, r, Expect{{Outcome::RecoveryRequired}, false, false,
            "contract: opaque custom effect callback on a failed operation"});
    }
    {   // D5 hidden combat modifier diverges during REVERSE effects
        Player p; p.Add(E(0), 10); p.Add(E(2), 12);
        Moves plan{M(p, E(0), P(0)), M(p, E(2), P(1))}; Before b(p);
        p.rejectTransfer = 2; bool done = false;
        p.modsHook = [&] { if (p.transfers >= 2 && !done) { done = true; p.m_auraFlatModifiersGroup[1][1] += 0.5f; } };
        auto r = Run(p, plan);
        Judge("D5", p, plan, b, r, Expect{{Outcome::RecoveryRequired}});
    }
    return 0;
}

// CS01 #52 7133329461 properties over a mixed-direction fault matrix (no pre-registered outcome:
// every result must satisfy the Judge properties).
static void Matrix()
{
    unsigned counts[4]{}, runs = 0;
    for (unsigned n : {2u, 3u, 5u})
        for (int kind = 0; kind < 6; ++kind)
            for (unsigned k = 1; k <= n * 3; ++k)
            {
                Player p;
                Moves plan;
                unsigned const strips[] = {0, 2, 4, 6, 7}, equips[] = {9, 10, 11, 12, 13};
                for (unsigned i = 0; i < n; ++i)
                    if (i % 2 == 0) { p.Add(E(strips[i]), 10 + i); }
                    else p.Add(P(8 + i), 40 + i);
                for (unsigned i = 0; i < n; ++i)
                    plan.push_back(i % 2 == 0 ? M(p, E(strips[i]), P(i)) : M(p, P(8 + i), E(equips[i])));
                Before b(p);
                switch (kind)
                {
                    case 0: p.rejectTransfer = k; break;
                    case 1: p.rejectPolicy = k; break;
                    case 2: p.throwMods = k; break;
                    case 3: p.recoveryTransfer = k; break;
                    case 4: p.lieTransfer = k; break;
                    case 5: p.rejectTransfer = k; p.denyUndo = 1; break;
                }
                auto r = Run(p, plan);
                ++runs;
                ++counts[int(r.outcome) & 3];
                Judge("M" + std::to_string(n) + "." + std::to_string(kind) + "." + std::to_string(k), p, plan, b, r, nullptr);
            }
    std::printf("MATRIX %u runs: Refused=%u Applied=%u Restored=%u RecoveryRequired=%u (property oracle on every run)\n",
        runs, counts[0], counts[1], counts[2], counts[3]);
}

// CS02 reference adapter rule (SYNTHETIC; Claude's PB52 code is not compiled here):
// inconsistent Applied or unknown values must fail closed.
enum class Strip { Done, Restored, Refused, RecoveryRequired };
static Strip Adapter(PB59::InventoryBatchResult const& r, bool fingerprintUnchanged)
{
    switch (r.outcome)
    {
        case Outcome::Applied:
            return r.failure == Failure::None && r.recoveryCount == 0 && fingerprintUnchanged ? Strip::Done : Strip::RecoveryRequired;
        case Outcome::Restored: return fingerprintUnchanged ? Strip::Restored : Strip::RecoveryRequired;
        case Outcome::Refused: return fingerprintUnchanged ? Strip::Refused : Strip::RecoveryRequired;
        case Outcome::RecoveryRequired: return Strip::RecoveryRequired;
    }
    return Strip::RecoveryRequired;
}
static void AdapterControls()
{
    PB59::InventoryBatchResult r;
    r.outcome = Outcome::Applied; r.recoveryCount = 1; r.recoveryGuids[0] = 10;
    Require(Adapter(r, true) == Strip::RecoveryRequired, "adapter: Applied with recovery GUIDs fails closed");
    r.recoveryCount = 0; r.failure = Failure::Links;
    Require(Adapter(r, true) == Strip::RecoveryRequired, "adapter: Applied with a failure code fails closed");
    r.failure = Failure::None;
    Require(Adapter(r, false) == Strip::RecoveryRequired, "adapter: Applied with a fingerprint mismatch fails closed");
    r.outcome = Outcome::Restored;
    Require(Adapter(r, false) == Strip::RecoveryRequired, "adapter: Restored with a fingerprint mismatch fails closed");
    std::underlying_type_t<Outcome> raw = 7; std::memcpy(&r.outcome, &raw, sizeof raw);
    Require(Adapter(r, true) == Strip::RecoveryRequired, "adapter: unknown outcome value fails closed");
}

int main()
{
    Scenarios();
    Matrix();
    AdapterControls();
    std::printf("CS02 native v1: %u checks, %u property failures, %u divergences from pre-registered expectations\n",
        Checks, Fails, Divergences);
    return Fails ? 1 : 0;
}
