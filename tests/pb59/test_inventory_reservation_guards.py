"""Actual Item deletion/SQL guards, native producer prefixes and SetPlayer.

DB/refund/update queues and Player/session engines are doubles. The prefix
tests execute only the unchanged gate before a synthetic side-effect marker.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


item = (root / "src/server/game/Entities/Item/Item.cpp").read_text(encoding="utf-8")
storage = (native / "PlayerStorage.cpp").read_text(encoding="utf-8")
session = (root / "src/server/game/Server/WorldSession.cpp").read_text(encoding="utf-8")
manager = (root / "src/server/game/Server/WorldSessionMgr.cpp").read_text(encoding="utf-8")
methods = "\n".join(function(item, signature) for signature in (
    "bool Item::PB59InventoryMutationAllowed(", "void Item::SetState(", "void Item::SetPB59State(",
    "void Item::DeleteFromDB(CharacterDatabaseTransaction trans,", "void Item::DeleteFromInventoryDB(CharacterDatabaseTransaction trans,"))
remove = function(storage, "void Player::RemoveItem(")
methods += remove[:remove.index("    PB59::EquipmentCaptureState::Mutation")] + "    (void)bag; (void)slot; (void)update; ++writes;\n}\n"
logout = function(session, "void WorldSession::LogoutPlayer(")
assert logout.index("InventoryMutationAllowed") < logout.index("m_playerLogout = true")
methods += logout[:logout.index("    // finish pending transfers")] + "    (void)save; (void)redirecting; ++writes;\n}\n"
methods += function(session, "void WorldSession::SetPlayer(")
update = function(session, "bool WorldSession::Update(")
methods += update[:update.index("    ///- Before")] + "    (void)diff; (void)updater; ++writes; return false;\n}\n"

spell_source = (root / "src/server/game/Spells/Spell.cpp").read_text(encoding="utf-8")
cast = function(spell_source, "SpellCastResult Spell::prepare(")
methods += cast[:cast.index("    Unit* unitCaster")] + "    (void)targets; (void)triggeredByAura; ++writes; return 0;\n}\n"

map_source = (root / "src/server/game/Maps/Map.cpp").read_text(encoding="utf-8")
for signature in ("void Map::DeleteFromWorld(Player* player)",
                  "void Map::RemovePlayerFromMap(Player* player, bool remove)",
                  "void InstanceMap::RemovePlayerFromMap(Player* player, bool remove)",
                  "void BattlegroundMap::RemovePlayerFromMap(Player* player, bool remove)"):
    body = function(map_source, signature)
    gate_end = body.index("        return;") + len("        return;")
    unused = "" if "DeleteFromWorld" in signature else "(void)remove; "
    methods += body[:gate_end] + "\n    " + unused + "++writes;\n}\n"

closed = function(session, "bool WorldSession::HandleSocketClosed(")
methods += closed[:closed.index("    if (m_Socket")] + "    ++writes; return true;\n}\n"
methods += function(manager, "bool WorldSessionMgr::KickSession(")
add = function(manager, "void WorldSessionMgr::AddSession_(")
methods += add[:add.index("    // kick existing session")] + "    ++writes;\n}\n"
# Execute the exact cleanup-loop gate before a synthetic erase/delete marker.
gate = "        WorldSession* pSession = itr->second;"
start = manager.index(gate, manager.index("///- Then send an update"))
end = manager.index("        WorldSessionFilter", start)
cleanup_gate = manager[start:end]
assert manager.count(cleanup_gate) == 2  # active and offline cleanup loops
methods += "void WorldSessionMgr::Cleanup() {\nfor (auto itr = _sessions.begin(); itr != _sessions.end(); ++itr) {\n" + cleanup_gate + "++writes;\n}\n}\n"

fixture = r'''
#include "EquipmentCaptureState.h"
#include "PB59TransactionAdmission.h"
#include <atomic>
#include <memory>
#include <map>
#define ASSERT(x) do { if (!(x)) std::abort(); } while (false)
#include <thread>
#include <cstdio>
using uint8 = std::uint8_t; using uint32 = std::uint32_t; using uint64 = std::uint64_t;
struct ObjectGuid { using LowType = uint64; uint64 value; uint64 GetRawValue() const { return value; } uint64 GetCounter() const { return value; } };
struct Statement { template<class T> void SetData(int, T) { } };
using CharacterDatabasePreparedStatement = Statement;
struct Transaction { unsigned rows = 0; void Append(Statement*) { ++rows; } };
using CharacterDatabaseTransaction = std::shared_ptr<Transaction>;
constexpr int CHAR_DEL_ITEM_INSTANCE = 1, CHAR_DEL_CHAR_INVENTORY_BY_ITEM = 2;
struct DB { Statement statement; Statement* GetPreparedStatement(int) { return &statement; } } CharacterDatabase;
struct Scripts { unsigned calls = 0; void OnGlobalItemDelFromDB(CharacterDatabaseTransaction, uint64) { ++calls; } } scripts;
auto sScriptMgr = &scripts;
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
class Player {
public:
    PB59::EquipmentCaptureState _pb59Equipment;
    unsigned writes = 0;
    PB59::TransactionAdmission ledger;
    void RecordPB59ReservedLogout() { if (_pb59Equipment.InventoryQuarantined()) (void)ledger.Request(PB59::TransactionAdmission::Work::Logout); }
    auto& GetPB59EquipmentState() { return _pb59Equipment; }
    ObjectGuid GetGUID() const { return {1}; }
    void DeleteRefundReference(ObjectGuid) { }
    void RemoveItem(uint8, uint8, bool);
    bool Arm(std::span<void const* const> items, std::span<uint64 const> ids) {
        if (!_pb59Equipment.PrepareInventoryReservation(1, items, ids)) return false;
        _pb59Equipment.SetInventoryQuarantined(true); return true;
    }
    void Release() { _pb59Equipment.SetInventoryQuarantined(false); }
};
static unsigned Deleted = 0;
class Item {
public:
    uint64 id = 10, owner = 1;
    ItemUpdateState uState = ITEM_NEW; int uQueuePos = -1;
    ~Item() { ++Deleted; }
    ObjectGuid GetGUID() const { return {id}; } ObjectGuid GetOwnerGUID() const { return {owner}; }
    bool PB59InventoryMutationAllowed() const;
    void SetState(ItemUpdateState, Player*);
    void SetPB59State(ItemUpdateState, Player*);
    void RemoveFromUpdateQueueOf(Player*) { uQueuePos = -1; }
    void AddPB59ToUpdateQueueOf(Player*) { uQueuePos = 0; }
    static void DeleteFromDB(CharacterDatabaseTransaction, ObjectGuid::LowType);
    static void DeleteFromInventoryDB(CharacterDatabaseTransaction, ObjectGuid::LowType);
};
struct PacketFilter { };
class WorldSession {
public:
    Player* _player = nullptr;
    uint64 m_GUIDLow = 0;
    unsigned writes = 0;
    void LogoutPlayer(bool, bool);
    void SetPlayer(Player*);
    bool Update(uint32, PacketFilter&);
    Player* GetPlayer() { return _player; }
    bool PlayerLoading() { return false; }
    bool HandleSocketClosed();
    void KickPlayer(char const*, bool = true) { ++writes; }
    uint32 GetAccountId() { return 1; }
};
class Map {
public:
    unsigned writes = 0;
    void DeleteFromWorld(Player*);
    void RemovePlayerFromMap(Player*, bool);
};
class InstanceMap : public Map { public: void RemovePlayerFromMap(Player*, bool); };
class BattlegroundMap : public Map { public: void RemovePlayerFromMap(Player*, bool); };
using SpellCastResult = int;
struct SpellCastTargets { };
struct AuraEffect { };
class Spell { public: unsigned writes = 0; SpellCastResult prepare(SpellCastTargets const*, AuraEffect const*); };
class WorldSessionMgr {
public:
    using SessionMap = std::map<uint32, WorldSession*>;
    SessionMap _sessions;
    WorldSession* offline = nullptr;
    unsigned writes = 0;
    WorldSession* FindOfflineSession(uint32) { return offline; }
    bool KickSession(uint32);
    void AddSession_(WorldSession*);
    void Cleanup();
};
'''
checks = r'''
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %u\n", checks); return 1; } } while (false)
int main() {
    unsigned checks = 0;
    auto* item = new Item;
    Spell ordinary; ordinary.prepare(nullptr, nullptr); CHECK(ordinary.writes == 1);
    auto transaction = std::make_shared<Transaction>();
    {
        Player actor;
        WorldSession session; session._player = &actor;
        std::array<void const*, 1> items{item}; std::array<uint64, 1> ids{10};
        CHECK(actor.Arm(items, ids));
        PB59::TransactionAdmission::MutationScope scope(actor.ledger);
        CHECK(scope.GetTicket());
        {
            PB59::InventoryReservation reservation(&actor._pb59Equipment, 1, items, ids);
            bool refused = false;
            try { item->SetState(ITEM_REMOVED, nullptr); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && Deleted == 0 && item->uState == ITEM_NEW);
            refused = false;
            try { actor.RemoveItem(0, 0, true); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && actor.writes == 0);
            refused = false;
            try { session.LogoutPlayer(true, false); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && session.writes == 0 && actor.ledger.PendingLogout());
            refused = false;
            try { session.SetPlayer(nullptr); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && session._player == &actor);
            refused = false;
            try { Item::DeleteFromDB(transaction, 10); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && transaction->rows == 0 && scripts.calls == 0);
            refused = false;
            try { Item::DeleteFromInventoryDB(transaction, 10); } catch (PB59::ReservedInventoryMutation const&) { refused = true; }
            CHECK(refused && transaction->rows == 0);
            CHECK(reservation.Violations() == 6);
            Spell cast; reservation.SetEffectsPhase(true); cast.prepare(nullptr, nullptr);
            CHECK(reservation.ExternalEffects() && cast.writes == 1);
            reservation.SetEffectsPhase(false);

            Item::DeleteFromDB(transaction, 77);
            CHECK(transaction->rows == 1 && scripts.calls == 1); // unrelated identity preserved
        }
        // After the stack frame leaves, quarantine remains queryable without
        // resolving/dereferencing native Player from a loader/DB thread.
        CHECK(PB59::InventoryQuarantine::Contains(item, 1));
        CHECK(!session.HandleSocketClosed() && session.writes == 0);
        PacketFilter updater; CHECK(session.Update(1, updater) && session.writes == 0);
        Map map; InstanceMap instance; BattlegroundMap battleground;
        map.DeleteFromWorld(&actor); map.RemovePlayerFromMap(&actor, true);
        instance.RemovePlayerFromMap(&actor, true); battleground.RemovePlayerFromMap(&actor, true);
        CHECK(map.writes == 0 && instance.writes == 0 && battleground.writes == 0);

        WorldSessionMgr manager; manager._sessions.emplace(1, &session);
        CHECK(!manager.KickSession(1) && session.writes == 0);
        manager.Cleanup(); CHECK(manager.writes == 0);
        manager.offline = &session;
        manager.AddSession_(new WorldSession); CHECK(manager.writes == 0);

        CHECK(PB59::InventoryQuarantine::ContainsCounter(10));
        item->owner = 2; CHECK(!item->PB59InventoryMutationAllowed()); // pointer identity still protects it
        std::atomic<unsigned> denied = 0;
        std::thread reader([&] { for (unsigned i = 0; i < 1000; ++i) if (!item->PB59InventoryMutationAllowed()) ++denied; });
        reader.join(); CHECK(denied == 1000);
        item->SetState(ITEM_REMOVED, nullptr); CHECK(Deleted == 0);
        actor.RemoveItem(0, 0, true); CHECK(actor.writes == 0);
        session.LogoutPlayer(true, false); CHECK(session.writes == 0);
        Item::DeleteFromDB(transaction, 10); CHECK(transaction->rows == 1);
        actor.Release();
        CHECK(!session.Update(1, updater) && session.writes == 1);
        session.writes = 0;
        CHECK(session.HandleSocketClosed() && session.writes == 1);
        map.DeleteFromWorld(&actor); map.RemovePlayerFromMap(&actor, true);
        instance.RemovePlayerFromMap(&actor, true); battleground.RemovePlayerFromMap(&actor, true);
        CHECK(map.writes == 2 && instance.writes == 1 && battleground.writes == 1);

        CHECK(manager.KickSession(1) && session.writes == 2);
        manager.Cleanup(); CHECK(manager.writes == 1);
        WorldSession ordinary; manager.AddSession_(&ordinary); CHECK(manager.writes == 2);
        session.writes = 0;

        CHECK(!PB59::InventoryQuarantine::Contains(item, 1));
        actor.RemoveItem(0, 0, true); CHECK(actor.writes == 1);
        session.LogoutPlayer(true, false); CHECK(session.writes == 1);
        session.SetPlayer(nullptr); CHECK(!session._player);
        item->SetState(ITEM_REMOVED, nullptr); item = nullptr; CHECK(Deleted == 1);
        Item::DeleteFromInventoryDB(transaction, 10); CHECK(transaction->rows == 2);
    }
    CHECK(!PB59::InventoryQuarantine::Contains(nullptr, 1));
    CHECK(!PB59::InventoryQuarantine::ContainsCounter(10));
    std::printf("%u actual item/producer guard and quarantine checks PASS (engine/DB doubles)\n", checks);
}
'''
with tempfile.TemporaryDirectory(prefix="pb59-reservation-guards-") as temporary:
    path = Path(temporary)
    source = path / "guards.cpp"
    binary = path / "guards.exe"
    source.write_text(fixture + methods + checks, encoding="utf-8")
    subprocess.run(args.compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(native),
                                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
