// CS02 checks for the REAL CapturePB59InventorySlot + RelocatePB59InventoryLinks bodies.
// Expectations below were registered before the first run. Divergences are printed, never adapted.
using Result = PB59::LinkTransferResult;
using Access = PB59::TransactionAdmission::Access;

static int g_checks = 0, g_fail = 0, g_div = 0;
static void Require(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}
static char const* N(Result r) { return r == Result::Linked ? "Linked" : r == Result::Rejected ? "Rejected" : r == Result::RecoveryRequired ? "RecoveryRequired" : "UNKNOWN"; }
static constexpr uint16 E(uint8 s) { return uint16(0xFF00 | s); }
static constexpr uint16 B(uint8 bag, uint8 s) { return uint16(bag << 8 | s); }

struct World
{
    Faults faults;
    WorldSession session;
    Player p;
    std::vector<std::unique_ptr<Item>> owned;
    ItemTemplate weapon{INVTYPE_WEAPON}, bagT{INVTYPE_BAG};
    PB59::TransactionAdmission::Ticket ticket = 0;

    Item* MakeItem(uint64 guid)
    {
        owned.push_back(std::make_unique<Item>());
        Item* i = owned.back().get();
        Init(*i, guid, &weapon);
        return i;
    }
    Bag* MakeBag(uint64 guid, uint32 size)
    {
        auto b = std::make_unique<Bag>();
        Bag* raw = b.get();
        owned.push_back(std::move(b));
        Init(*raw, guid, &bagT);
        raw->values[CONTAINER_FIELD_NUM_SLOTS] = size;
        return raw;
    }
    void Init(Item& i, uint64 guid, ItemTemplate const* t)
    {
        i.faults = &faults;
        i.guid = {guid};
        i.proto = t;
        i.values[ITEM_FIELD_OWNER] = p.GetGUID().value;
        i.values[ITEM_FIELD_STACK_COUNT] = 1;
        i.values[ITEM_FIELD_DURABILITY] = 50;
        i.values[ITEM_FIELD_MAXDURABILITY] = 60;
        i.values[ITEM_FIELD_CREATOR] = 77;
    }
    void Place(Item* i, uint16 pos)
    {
        uint8 bag = pos >> 8, slot = pos & 255;
        if (bag == INVENTORY_SLOT_BAG_0)
        {
            p.m_items[slot] = i;
            p.values[PLAYER_FIELD_INV_SLOT_HEAD + slot * 2] = i->guid.value;
            i->m_container = nullptr;
            i->values[ITEM_FIELD_CONTAINED] = p.GetGUID().value;
        }
        else
        {
            Bag* b = static_cast<Bag*>(p.m_items[bag]);
            b->m_bagslot[slot] = i;
            b->values[CONTAINER_FIELD_SLOT_1 + slot * 2] = i->guid.value;
            i->m_container = b;
            i->values[ITEM_FIELD_CONTAINED] = b->guid.value;
        }
        i->m_slot = slot;
    }
    Bag* BagAt(uint8 b) { return static_cast<Bag*>(p.m_items[b]); }

    World(uint64 owner = 42)
    {
        p.guid = {owner};
        p.faults = &faults;
        session.player = &p;
        p.session = &session;
        p._pb59Equipment.Bind();
        Place(MakeBag(900, 16), E(19));
        Place(MakeBag(901, 4), E(20));
        Item* gear = MakeItem(700);
        gear->enchants[1] = {77, 500, 0};
        gear->enchants[2] = {88, 0, 3};
        gear->charges[0] = -1;
        gear->m_text = "cs02";
        Place(gear, E(15));
        p.m_enchantDuration.push_back({gear, EnchantmentSlot(1), 300});
        Place(MakeItem(701), E(23));
        Place(MakeItem(702), B(19, 5));
        ticket = p._pb59Transaction.Begin();
    }
};
static std::unique_ptr<World> NewWorld(uint64 owner = 42) { return std::make_unique<World>(owner); }

// Full value dump of the double world: used to prove "no change".
static std::string Dump(World& w)
{
    std::string s;
    auto obj = [&](Object const& o) { for (auto const& [k, v] : o.values) s += std::to_string(k) + "=" + std::to_string(v) + ","; };
    auto item = [&](Item const& i)
    {
        s += "[" + std::to_string(i.guid.value) + " slot" + std::to_string(i.m_slot) + " c" +
            std::to_string(i.m_container ? i.m_container->guid.value : 0) + " st" + std::to_string(i.uState) +
            " q" + std::to_string(i.uQueuePos) + " ";
        obj(i);
        s += "]";
    };
    obj(w.p);
    for (int i = 0; i < 150; ++i)
        if (w.p.m_items[i]) s += "m" + std::to_string(i) + ":" + std::to_string(w.p.m_items[i]->guid.value) + ",";
    for (auto const& o : w.owned)
    {
        item(*o);
        if (auto* bag = dynamic_cast<Bag*>(o.get()))
            for (int i = 0; i < MAX_BAG_SIZE; ++i)
                if (auto* b = bag->m_bagslot[i]) s += "b" + std::to_string(i) + ":" + std::to_string(b->guid.value) + ",";
    }
    for (auto* q : w.p.m_itemUpdateQueue) s += "Q" + std::to_string(q->guid.value) + ",";
    s += "W" + std::to_string(w.faults.writes) + "F" + std::to_string(w.faults.forced);
    return s;
}
static bool Active(World& w) { return w.p._pb59Transaction.CanAccess(w.ticket, Access::Forward); }
static bool Recovery(World& w) { return w.p._pb59Transaction.CanAccess(w.ticket, Access::Recovery); }

// Item attributes except the link field (which legitimately changes).
static std::map<uint16, uint64> Attrs(Item const& i) { auto v = i.values; v.erase(ITEM_FIELD_CONTAINED); return v; }

static int Occurrences(World& w, Item* it)
{
    int n = 0;
    for (auto* x : w.p.m_items) n += x == it;
    for (auto const& o : w.owned)
        if (auto* bag = dynamic_cast<Bag*>(o.get()); bag && std::find(w.p.m_items, w.p.m_items + 150, o.get()) != w.p.m_items + 150)
            for (auto* x : bag->m_bagslot) n += x == it;
    return n;
}

// Independent consistency checker (does not call the real capture).
static bool Consistent(World& w, Item* it, uint16 src, uint16 dst, uint64 dstBagGuid, std::map<uint16, uint64> const& attrs)
{
    auto& p = w.p;
    if (!p.session || p.session->player != &p || p.session->loading || p.session->logout || !p.inWorld || !p.alive ||
        p.mSemaphoreTeleport_Far || p.mSemaphoreTeleport_Near || p.m_itemUpdateQueueBlocked)
        return false;
    uint64 const owner = p.guid.value;
    auto slotOk = [&](uint16 pos, Item* expect) -> bool
    {
        uint8 bag = pos >> 8, slot = pos & 255;
        uint64 want = expect ? expect->guid.value : 0;
        if (bag == INVENTORY_SLOT_BAG_0)
            return p.m_items[slot] == expect && p.Raw(PLAYER_FIELD_INV_SLOT_HEAD + slot * 2) == want &&
                (!expect || (!expect->m_container && expect->Raw(ITEM_FIELD_CONTAINED) == owner));
        Item* bi = p.m_items[bag];
        Bag* b = dynamic_cast<Bag*>(bi);
        if (!b || !b->proto || !b->IsBag()) return false;
        if (bag == (dst >> 8) && b->guid.value != dstBagGuid) return false;
        if (p.Raw(PLAYER_FIELD_INV_SLOT_HEAD + bag * 2) != b->guid.value || b->Raw(ITEM_FIELD_OWNER) != owner ||
            b->Raw(ITEM_FIELD_CONTAINED) != owner || b->m_slot != bag || b->m_container || b->uState == ITEM_REMOVED ||
            b->GetBagSize() > MAX_BAG_SIZE || slot >= b->GetBagSize())
            return false;
        return b->m_bagslot[slot] == expect && b->Raw(CONTAINER_FIELD_SLOT_1 + slot * 2) == want &&
            (!expect || (expect->m_container == b && expect->Raw(ITEM_FIELD_CONTAINED) == b->guid.value));
    };
    if (!slotOk(dst, it) || !slotOk(src, nullptr)) return false;
    if (it->m_slot != (dst & 255) || Attrs(*it) != attrs || it->uState == ITEM_REMOVED) return false;
    auto n = std::count(p.m_itemUpdateQueue.begin(), p.m_itemUpdateQueue.end(), it);
    return n == 1 && it->IsInUpdateQueue() && p.m_itemUpdateQueue[it->GetQueuePos()] == it;
}

struct Call
{
    Result r;
    bool consistent;
    int occurrences;
};

// Run one real relocation with pre-captured real facts, apply `before` corruption, then judge.
static Call RelocateJudge(std::string const& id, World& w, uint16 src, uint16 dst, Result expected,
    std::function<void(World&)> const& before = {}, std::function<void(PB59::InventorySlotFacts&, PB59::InventorySlotFacts&)> const& forge = {},
    bool printAlways = false)
{
    auto a = w.p.CapturePB59InventorySlot(src);
    auto b = w.p.CapturePB59InventorySlot(dst);
    Require(a && b && a->item && !b->item, id + ": real capture of clean src/dst");
    if (!a || !b) return {Result::Rejected, false, 0};
    Item* it = w.p.GetItemByPos(src);
    auto attrs = Attrs(*it);
    if (before) before(w);
    if (forge) forge(*a, *b);
    std::string const pre = Dump(w);
    Result r;
    try { r = w.p.RelocatePB59InventoryLinks(*a, *b, w.ticket, Access::Forward); }
    catch (...) { Require(false, id + ": relocation escaped an exception"); return {Result::Rejected, false, 0}; }
    bool const cons = Consistent(w, it, src, dst, b->bag, attrs);
    int const occ = Occurrences(w, it);
    bool const asExpected = r == expected;
    g_div += !asExpected;
    if (printAlways || !asExpected)
        std::printf("SCN %-34s expected=%-16s observed=%-16s consistent=%d %s\n", id.c_str(), N(expected), N(r), cons,
            asExpected ? "as-expected" : "DIVERGENCE");
    // Property oracle on every call.
    switch (r)
    {
        case Result::Rejected:
            Require(Dump(w) == pre, id + ": Rejected leaves every pointer/field/queue/write counter unchanged");
            Require(Active(w), id + ": Rejected keeps the ledger Active");
            break;
        case Result::Linked:
            Require(cons, id + ": Linked implies independent destination/source/queue consistency");
            Require(Active(w), id + ": Linked keeps the ledger Active");
            {
                auto now = w.p.CapturePB59InventorySlot(dst);
                Require(now && now->bag == b->bag && PB59::SameInventoryInstance(*a, *now), id + ": Linked re-captures the same instance");
            }
            break;
        case Result::RecoveryRequired:
            Require(Recovery(w), id + ": RecoveryRequired sets the ledger");
            Require(occ == 1, id + ": RecoveryRequired keeps the item reachable exactly once");
            break;
        default:
            Require(false, id + ": unknown LinkTransferResult");
    }
    return {r, cons, occ};
}

struct Dir { char const* name; uint16 src, dst; std::function<void(World&)> setup; };
static std::vector<Dir> Directions()
{
    return {
        {"E15>B19.0", E(15), B(19, 0), {}},
        {"E15>B20.3", E(15), B(20, 3), {}},
        {"E15>P24", E(15), E(24), {}},
        {"B19.5>E16", B(19, 5), E(16), {}},
        {"P23>E16", E(23), E(16), {}},
    };
}

// ---- Pre-call predicates: every one expected Rejected with no writes (stale facts or failed capture).
struct Pred { char const* name; char const* side; std::function<bool(World&, uint16, uint16)> apply; };
static std::vector<Pred> Predicates()
{
    auto srcItem = [](World& w, uint16 s, uint16) { return w.p.GetItemByPos(s); };
    auto dstBag = [](World& w, uint16, uint16 d) -> Bag* { return (d >> 8) == 255 ? nullptr : w.BagAt(d >> 8); };
    auto srcBag = [](World& w, uint16 s, uint16) -> Bag* { return (s >> 8) == 255 ? nullptr : w.BagAt(s >> 8); };
    std::vector<Pred> v = {
        {"session missing", "session", [](World& w, uint16, uint16) { w.p.session = nullptr; return true; }},
        {"session other player", "session", [](World& w, uint16, uint16) { w.session.player = nullptr; return true; }},
        {"session loading", "session", [](World& w, uint16, uint16) { w.session.loading = true; return true; }},
        {"session logout", "session", [](World& w, uint16, uint16) { w.session.logout = true; return true; }},
        {"not in world", "session", [](World& w, uint16, uint16) { w.p.inWorld = false; return true; }},
        {"dead", "session", [](World& w, uint16, uint16) { w.p.alive = false; return true; }},
        {"teleport far", "session", [](World& w, uint16, uint16) { w.p.mSemaphoreTeleport_Far = 1; return true; }},
        {"teleport near", "session", [](World& w, uint16, uint16) { w.p.mSemaphoreTeleport_Near = 1; return true; }},
        {"update queue blocked", "session", [](World& w, uint16, uint16) { w.p.m_itemUpdateQueueBlocked = true; return true; }},
        {"src field GUID mismatch", "src", [=](World& w, uint16 s, uint16 d) { auto* i = srcItem(w, s, d); auto* b = srcBag(w, s, d);
            (b ? b->values[CONTAINER_FIELD_SLOT_1 + (s & 255) * 2] : w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (s & 255) * 2]) = i->guid.value + 1; return true; }},
        {"src owner foreign", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_OWNER] = 43; return true; }},
        {"src slot field wrong", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->m_slot ^= 1; return true; }},
        {"src container pointer wrong", "src", [=](World& w, uint16 s, uint16 d) { auto* i = srcItem(w, s, d); i->m_container = i->m_container ? nullptr : w.BagAt(20); return true; }},
        {"src CONTAINED foreign", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_CONTAINED] = 4242; return true; }},
        {"src no template", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->proto = nullptr; return true; }},
        {"src state removed", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->uState = ITEM_REMOVED; return true; }},
        {"src is a bag", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->proto = &w.bagT; return true; }},
        {"src in trade", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->mb_in_trade = true; return true; }},
        {"src loot generated", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->m_lootGenerated = true; return true; }},
        {"src is loot target", "src", [=](World& w, uint16 s, uint16 d) { w.p.m_lootGuid = srcItem(w, s, d)->guid; return true; }},
        {"src refundable", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_FLAGS] |= ITEM_FIELD_FLAG_REFUNDABLE; return true; }},
        {"src BOP tradable", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_FLAGS] |= ITEM_FIELD_FLAG_BOP_TRADEABLE; return true; }},
        {"src wrapped", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_FLAGS] |= ITEM_FIELD_FLAG_WRAPPED; return true; }},
        {"src stale facts: durability", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_DURABILITY] -= 1; return true; }},
        {"src stale facts: count", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->values[ITEM_FIELD_STACK_COUNT] = 2; return true; }},
        {"src stale facts: live enchant timer", "src", [=](World& w, uint16 s, uint16 d) { auto* i = srcItem(w, s, d); i->enchants[3] = {66, 900, 0};
            w.p.m_enchantDuration.push_back({i, EnchantmentSlot(3), 899}); return true; }},
        {"src duplicate enchant timer", "src", [=](World& w, uint16 s, uint16 d) { auto* i = srcItem(w, s, d); i->enchants[4] = {55, 1, 0};
            w.p.m_enchantDuration.push_back({i, EnchantmentSlot(4), 10}); w.p.m_enchantDuration.push_back({i, EnchantmentSlot(4), 10}); return true; }},
        {"src timer on empty enchant", "src", [=](World& w, uint16 s, uint16 d) { w.p.m_enchantDuration.push_back({srcItem(w, s, d), EnchantmentSlot(6), 10}); return true; }},
        {"src timer zero left", "src", [=](World& w, uint16 s, uint16 d) { auto* i = srcItem(w, s, d); i->enchants[7] = {44, 5, 0};
            w.p.m_enchantDuration.push_back({i, EnchantmentSlot(7), 0}); return true; }},
        {"src stale queue position", "src", [=](World& w, uint16 s, uint16 d) { srcItem(w, s, d)->uQueuePos = 0; return true; }},
        {"src bag replaced (GUID field)", "srcbag", [=](World& w, uint16 s, uint16 d) { if (!srcBag(w, s, d)) return false;
            w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (s >> 8) * 2] = 950; return true; }},
        {"dst slot field stale GUID", "dst", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d);
            (b ? b->values[CONTAINER_FIELD_SLOT_1 + (d & 255) * 2] : w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (d & 255) * 2]) = 3333; return true; }},
        {"dst slot occupied", "dst", [](World& w, uint16, uint16 d) { w.Place(w.MakeItem(3334), d); return true; }},
        {"dst pointer without field", "dst", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); Item* x = w.MakeItem(3335);
            if (b) b->m_bagslot[d & 255] = x; else w.p.m_items[d & 255] = x; return true; }},
        {"dst bag missing (pointer)", "dstbag", [=](World& w, uint16, uint16 d) { if (!dstBag(w, 0, d)) return false; w.p.m_items[d >> 8] = nullptr; return true; }},
        {"dst bag GUID field replaced", "dstbag", [=](World& w, uint16, uint16 d) { if (!dstBag(w, 0, d)) return false;
            w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (d >> 8) * 2] = 950; return true; }},
        {"dst bag owner foreign", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->values[ITEM_FIELD_OWNER] = 43; return true; }},
        {"dst bag CONTAINED foreign", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->values[ITEM_FIELD_CONTAINED] = 43; return true; }},
        {"dst bag slot wrong", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->m_slot = 21; return true; }},
        {"dst bag nested in container", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->m_container = w.BagAt(d >> 8 == 19 ? 20 : 19); return true; }},
        {"dst bag removed", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->uState = ITEM_REMOVED; return true; }},
        {"dst bag oversize", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->values[CONTAINER_FIELD_NUM_SLOTS] = 37; return true; }},
        {"dst bag shrunk below slot", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->values[CONTAINER_FIELD_NUM_SLOTS] = d & 255; return true; }},
        {"dst bag stale queue position", "dstbag", [=](World& w, uint16, uint16 d) { auto* b = dstBag(w, 0, d); if (!b) return false; b->uQueuePos = 3; return true; }},
        {"dst bag replaced by coherent bag", "dstbag", [=](World& w, uint16, uint16 d) { if (!dstBag(w, 0, d)) return false;
            Bag* old = w.BagAt(d >> 8); Bag* n = w.MakeBag(950, old->GetBagSize()); w.Place(n, E(d >> 8));
            for (int i = 0; i < MAX_BAG_SIZE; ++i) if (old->m_bagslot[i]) w.Place(old->m_bagslot[i], B(d >> 8, i));
            return true; }},
    };
    return v;
}

// ---- Mid-call link corruption (raw, through no producer), applied at the k-th field write.
struct Mid { char const* name; int needs; std::function<void(World&, Item*, uint16, uint16)> apply; }; // needs: 0 any, 1 dst in bag, 2 src in bag
static std::vector<Mid> MidCorruptions()
{
    return {
        {"dst bag GUID field replaced", 1, [](World& w, Item*, uint16, uint16 d) { w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (d >> 8) * 2] = 950; }},
        {"dst slot field third party", 0, [](World& w, Item*, uint16, uint16 d) { if ((d >> 8) != 255) w.BagAt(d >> 8)->values[CONTAINER_FIELD_SLOT_1 + (d & 255) * 2] = 951; else w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (d & 255) * 2] = 951; }},
        {"dst bag replaced by coherent bag", 1, [](World& w, Item*, uint16, uint16 d) { uint8 bag = d >> 8;
            Bag* old = w.BagAt(bag); Bag* n = w.MakeBag(950, old->GetBagSize()); w.p.m_items[bag] = n; w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + bag * 2] = 950;
            n->m_slot = bag; n->values[ITEM_FIELD_CONTAINED] = w.p.guid.value;
            for (int i = 0; i < MAX_BAG_SIZE; ++i) if (Item* x = old->m_bagslot[i]) { n->m_bagslot[i] = x; n->values[CONTAINER_FIELD_SLOT_1 + i * 2] = old->Raw(CONTAINER_FIELD_SLOT_1 + i * 2);
                x->m_container = n; x->values[ITEM_FIELD_CONTAINED] = 950; } }},
        {"dst bag owner foreign", 1, [](World& w, Item*, uint16, uint16 d) { w.BagAt(d >> 8)->values[ITEM_FIELD_OWNER] = 43; }},
        {"item owner foreign", 0, [](World&, Item* it, uint16, uint16) { it->values[ITEM_FIELD_OWNER] = 43; }},
        {"item slot changed", 0, [](World&, Item* it, uint16, uint16) { it->m_slot = 37; }},
        {"item durability changed", 0, [](World&, Item* it, uint16, uint16) { it->values[ITEM_FIELD_DURABILITY] = 1; }},
        {"session logout", 0, [](World& w, Item*, uint16, uint16) { w.session.logout = true; }},
        {"left world", 0, [](World& w, Item*, uint16, uint16) { w.p.inWorld = false; }},
        {"dst bag shrunk", 1, [](World& w, Item*, uint16, uint16 d) { w.BagAt(d >> 8)->values[CONTAINER_FIELD_NUM_SLOTS] = d & 255; }},
        {"src bag GUID field replaced", 2, [](World& w, Item*, uint16 s, uint16) { w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (s >> 8) * 2] = 950; }},
        {"died", 0, [](World& w, Item*, uint16, uint16) { w.p.alive = false; }},
        {"src field third party", 0, [](World& w, Item*, uint16 s, uint16) { if ((s >> 8) != 255) w.BagAt(s >> 8)->values[CONTAINER_FIELD_SLOT_1 + (s & 255) * 2] = 5555; else w.p.values[PLAYER_FIELD_INV_SLOT_HEAD + (s & 255) * 2] = 5555; }},
    };
}

static int Scenarios()
{
    // R: real capture across positions (no mutation).
    {
        auto w = NewWorld();
        int valid = 0, invalid = 0;
        for (int s = 0; s < 40; ++s)
        {
            auto f = w->p.CapturePB59InventorySlot(E(s));
            bool const expect = (s < 19 && s != 3 && s != 18) || (s >= 23 && s < 39);
            Require(bool(f) == expect, "R base slot " + std::to_string(s) + (expect ? " captures" : " refuses"));
            if (f) { ++valid; Require(f->owner == 42 && f->bag == 0 && f->position == E(s), "R base facts owner/bag/pos " + std::to_string(s)); }
            else ++invalid;
        }
        for (uint8 bag : {19, 20, 21, 22, 23, 67})
            for (int s = 0; s < MAX_BAG_SIZE; ++s)
            {
                auto f = w->p.CapturePB59InventorySlot(B(bag, s));
                bool const expect = (bag == 19 && s < 16) || (bag == 20 && s < 4);
                Require(bool(f) == expect, "R bag " + std::to_string(bag) + "." + std::to_string(s));
                if (f) { ++valid; Require(f->owner == 42 && f->bag == (bag == 19 ? 900u : 901u) && f->position == B(bag, s), "R bag facts"); }
                else ++invalid;
            }
        auto g = w->p.CapturePB59InventorySlot(E(15));
        Require(g && g->item && g->item->guid == 700 && g->item->enchants[1].duration == 300 && g->item->enchants[2].charges == 3 &&
            g->item->text == "cs02" && g->item->spellCharges[0] == -1 && g->item->creator == 77,
            "R gear facts copy all attributes; enchant duration normalised to live timer (300, not field 500)");
        std::printf("SCN R   real capture: %d positions captured, %d refused (as pre-registered)\n", valid, invalid);
    }
    // P: pre-call predicates x directions.
    int pRuns = 0, pRejected = 0, pNotApplicable = 0;
    for (auto const& d : Directions())
        for (auto const& pr : Predicates())
        {
            auto probe = NewWorld();
            if (!pr.apply(*probe, d.src, d.dst)) { ++pNotApplicable; continue; } // e.g. a source-bag fault on a base-slot source
            auto w = NewWorld();
            RelocateJudge(std::string("P ") + pr.name + " " + d.name, *w, d.src, d.dst, Result::Rejected,
                [&](World& x) { pr.apply(x, d.src, d.dst); });
            ++pRuns;
            pRejected += Active(*w) && !Recovery(*w);
            (void)pRejected;
        }
    std::printf("MATRIX P pre-call predicates: %d runs (+%d not applicable), expected Rejected for all\n", pRuns, pNotApplicable);
    // V: capture-level validity (added after sabotage K6 survived the stale-facts P matrix). For every predicate that
    // is a validity fault (not a stale-facts/occupied/coherent-replacement/queue fault), the REAL capture of the
    // affected position must refuse. Expected values come from the body's own contract comments and the predicate.
    int vRuns = 0, vRefused = 0;
    for (auto const& d : Directions())
        for (auto const& pr : Predicates())
        {
            std::string const name = pr.name;
            bool const capturesStill = name.rfind("src stale", 0) == 0 || name == "dst slot occupied" ||
                name == "dst bag replaced by coherent bag" || name == "dst bag stale queue position";
            auto w = NewWorld();
            if (!pr.apply(*w, d.src, d.dst)) continue;
            std::string side = pr.side;
            uint16 const pos = (side == "dst" || side == "dstbag") ? d.dst : d.src;
            bool const refused = !w->p.CapturePB59InventorySlot(pos);
            ++vRuns; vRefused += refused;
            bool const ok = refused != capturesStill;
            g_div += !ok;
            Require(true, "V counted");
            if (!ok)
                std::printf("SCN V %-40s %s expected=%s observed=%s DIVERGENCE\n", pr.name, d.name,
                    capturesStill ? "captures" : "refuses", refused ? "refuses" : "captures");
        }
    std::printf("MATRIX V real-capture validity: %d runs, %d refused\n", vRuns, vRefused);
    // F: forged facts. All expected Rejected.
    auto forge = [](std::string id, uint16 s, uint16 d, std::function<void(PB59::InventorySlotFacts&, PB59::InventorySlotFacts&)> f)
    {
        auto w = NewWorld();
        RelocateJudge(id, *w, s, d, Result::Rejected, {}, f, true);
    };
    forge("F1 both owners forged 43", E(15), B(19, 0), [](auto& a, auto& b) { a.owner = 43; b.owner = 43; if (a.item) a.item->owner = 43; });
    forge("F2 dst facts claim base bag", E(15), B(19, 0), [](auto&, auto& b) { b.bag = 0; });
    forge("F3 dst facts claim other bag GUID", E(15), B(19, 0), [](auto&, auto& b) { b.bag = 901; });
    forge("F4 same position", E(15), B(19, 0), [](auto& a, auto& b) { b = a; b.item.reset(); });
    forge("F5 src item GUID forged", E(15), B(19, 0), [](auto& a, auto&) { a.item->guid = 701; });
    forge("F6 dst facts claim occupied", E(15), B(19, 0), [](auto& a, auto& b) { b.item = a.item; });
    {
        // F7 as pre-registered: facts for B19.0 with the position rewritten to B19.1. Observed result is printed
        // against the pre-registered expectation; the property oracle judges the slot those facts actually name.
        auto w = NewWorld();
        auto a = w->p.CapturePB59InventorySlot(E(15)), b = w->p.CapturePB59InventorySlot(B(19, 0));
        Item* it = w->p.GetItemByPos(E(15));
        auto attrs = Attrs(*it);
        b->position = B(19, 1);
        bool const honest = w->p.CapturePB59InventorySlot(B(19, 1)) == b;
        auto r = w->p.RelocatePB59InventoryLinks(*a, *b, w->ticket, Access::Forward);
        std::printf("SCN %-34s expected=%-16s observed=%-16s rewritten facts equal an honest capture of B19.1=%d %s\n",
            "F7 dst position rewritten to B19.1", "Rejected", N(r), honest, r == Result::Rejected ? "as-expected" : "DIVERGENCE");
        g_div += r != Result::Rejected;
        if (r == Result::Linked) Require(Consistent(*w, it, E(15), B(19, 1), 900, attrs), "F7 Linked is consistent at the slot the facts name");
    }
    forge("F7b dst position B19.1 with bag GUID 901", E(15), B(19, 0), [](auto&, auto& b) { b.position = B(19, 1); b.bag = 901; });
    {
        auto w = NewWorld();
        auto a = w->p.CapturePB59InventorySlot(E(15)), b = w->p.CapturePB59InventorySlot(E(16));
        std::string pre = Dump(*w);
        auto r = w->p.RelocatePB59InventoryLinks(*a, *b, w->ticket, Access::Forward);
        std::printf("SCN %-34s expected=%-16s observed=%-16s %s\n", "F8 equipment->equipment", "Rejected", N(r), r == Result::Rejected ? "as-expected" : "DIVERGENCE");
        g_div += r != Result::Rejected; Require(Dump(*w) == pre, "F8 no change");
        a = w->p.CapturePB59InventorySlot(E(23)); b = w->p.CapturePB59InventorySlot(B(19, 0));
        r = w->p.RelocatePB59InventoryLinks(*a, *b, w->ticket, Access::Forward);
        std::printf("SCN %-34s expected=%-16s observed=%-16s %s\n", "F9 backpack->bag (no combat side)", "Rejected", N(r), r == Result::Rejected ? "as-expected" : "DIVERGENCE");
        g_div += r != Result::Rejected; Require(Dump(*w) == pre, "F9 no change");
    }
    {
        // F10: facts captured from another Player with the same layout and a different GUID.
        auto w = NewWorld(), o = NewWorld(43);
        auto a = o->p.CapturePB59InventorySlot(E(15)), b = o->p.CapturePB59InventorySlot(B(19, 0));
        Require(a && b && a->owner == 43, "F10 other player's real capture");
        std::string pre = Dump(*w);
        auto r = w->p.RelocatePB59InventoryLinks(*a, *b, w->ticket, Access::Forward);
        std::printf("SCN %-34s expected=%-16s observed=%-16s %s\n", "F10 facts from another owner", "Rejected", N(r), r == Result::Rejected ? "as-expected" : "DIVERGENCE");
        g_div += r != Result::Rejected; Require(Dump(*w) == pre, "F10 no change");
    }
    // Q: queue states through the REAL SetPB59State/AddPB59ToUpdateQueueOf.
    {
        auto w = NewWorld();
        w->p.GetItemByPos(E(15))->uState = ITEM_NEW;
        RelocateJudge("Q1 ITEM_NEW source", *w, E(15), B(19, 0), Result::Linked, {}, {}, true);
        Require(w->p.GetItemByPos(B(19, 0))->uState == ITEM_NEW, "Q1 real SetPB59State keeps ITEM_NEW");
    }
    {
        auto w = NewWorld();
        Item* g = w->p.GetItemByPos(E(15));
        g->uState = ITEM_CHANGED; w->p.m_itemUpdateQueue.push_back(g); g->uQueuePos = 0;
        RelocateJudge("Q2 already validly queued", *w, E(15), B(19, 0), Result::Linked, {}, {}, true);
        Require(w->p.m_itemUpdateQueue.size() == 2, "Q2 item not queued twice (item + destination bag)");
    }
    {
        // Q3 queue blocked AFTER capture succeeds: real AddPB59ToUpdateQueueOf silently skips -> must not be Linked.
        auto w = NewWorld();
        auto a = w->p.CapturePB59InventorySlot(E(15)), b = w->p.CapturePB59InventorySlot(B(19, 0));
        // Queue marks precede every field write, so no write hook can block the queue mid-call; the
        // reachable case is a block between the caller's capture and the call.
        w->p.m_itemUpdateQueueBlocked = true;
        std::string pre = Dump(*w);
        auto r = w->p.RelocatePB59InventoryLinks(*a, *b, w->ticket, Access::Forward);
        std::printf("SCN %-34s expected=%-16s observed=%-16s %s\n", "Q3 queue blocked before call", "Rejected", N(r), r == Result::Rejected ? "as-expected" : "DIVERGENCE");
        g_div += r != Result::Rejected; Require(Dump(*w) == pre, "Q3 no change");
    }
    // C: clean controls (one per direction), expected Linked + round trip back.
    for (auto const& d : Directions())
    {
        auto w = NewWorld();
        auto c = RelocateJudge(std::string("C ") + d.name, *w, d.src, d.dst, Result::Linked, {}, {}, true);
        if (c.r == Result::Linked)
            RelocateJudge(std::string("C back ") + d.name, *w, d.dst, d.src, Result::Linked);
    }
    // M: mid-call raw link corruption at field write k (before or after that write).
    int mRuns = 0, mRR = 0, mLinked = 0, mConservative = 0, mStrict = 0, mNA = 0, mOverwritten = 0;
    for (auto const& d : Directions())
        for (auto const& m : MidCorruptions())
            for (int k = 1; k <= 3; ++k)
                for (bool after : {false, true})
                {
                    bool const dstBagged = (d.dst >> 8) != 255, srcBagged = (d.src >> 8) != 255;
                    // Field writes in the real body: from, to, and CONTAINED only when the container changes.
                    int const writes = (dstBagged || srcBagged) ? 3 : 2;
                    if ((m.needs == 1 && !dstBagged) || (m.needs == 2 && !srcBagged) || k > writes) { ++mNA; continue; }
                    auto w = NewWorld();
                    Item* it = w->p.GetItemByPos(d.src);
                    bool fired = false;
                    w->faults.hookAt = k;
                    w->faults.hookAfter = after;
                    w->faults.hook = [&] { if (fired) return; fired = true; m.apply(*w, it, d.src, d.dst); };
                    std::string id = std::string("M ") + m.name + " " + d.name + " k" + std::to_string(k) + (after ? "after" : "before");
                    Result const expected = Result::RecoveryRequired; // pre-registered for every mid corruption
                    Call c;
                    c = RelocateJudge(id, *w, d.src, d.dst, expected);
                    ++mRuns;
                    if (!fired) { Require(false, id + ": corruption hook never fired"); continue; }
                    mRR += c.r == Result::RecoveryRequired;
                    mLinked += c.r == Result::Linked;
                    if (c.r == Result::RecoveryRequired && c.consistent) ++mConservative;
                    if (!c.consistent) { ++mStrict; Require(c.r != Result::Linked, id + ": inconsistent world must not be Linked"); }
                    if (c.r == Result::Linked && c.consistent) ++mOverwritten;
                }
    std::printf("MATRIX M mid-call link corruption: %d runs (+%d not applicable): RecoveryRequired=%d Linked=%d; "
        "final world inconsistent=%d (must all be RR), Linked-with-consistent-world=%d, RR-while-consistent=%d\n",
        mRuns, mNA, mRR, mLinked, mStrict, mOverwritten, mConservative);
    // T: a repairable one-shot write throw must not hide a replaced bag on the bagged side (pre-registered RR).
    for (auto const& d : Directions())
    {
        bool const dstBagged = (d.dst >> 8) != 255, srcBagged = (d.src >> 8) != 255;
        if (!dstBagged && !srcBagged) continue;
        auto w = NewWorld();
        Item* it = w->p.GetItemByPos(d.src);
        w->faults.throwAt = 2;
        w->faults.hookAt = 2;
        uint8 const bag = dstBagged ? (d.dst >> 8) : (d.src >> 8);
        w->faults.hook = [&w, bag] { w->p.values[PLAYER_FIELD_INV_SLOT_HEAD + bag * 2] = 950; };
        (void)it;
        RelocateJudge(std::string("T throw+") + (dstBagged ? "dst" : "src") + " bag GUID replaced " + d.name, *w, d.src, d.dst,
            Result::RecoveryRequired, {}, {}, true);
    }
    return 0;
}

// Adapter-side: relocation results never claim success without the destination re-proof.
int main()
{
    std::printf("REAL bodies: CapturePB59InventorySlot, RelocatePB59InventoryLinks, Player::GetItemByPos x2, GetBagByPos, "
                "Item::GetBagSlot, Item::SetPB59State, Item::AddPB59ToUpdateQueueOf, Bag::GetItemByPos + real PB59 headers. "
                "DOUBLES: ObjectGuid, Object fields/writes, WorldSession, Player/Item/Bag data, ItemTemplate, macros.\n");
    Scenarios();
    std::printf("CS02 native c779: %d checks, %d property failures, %d divergences from pre-registered expectations\n", g_checks, g_fail, g_div);
    return g_fail ? 1 : 0;
}
