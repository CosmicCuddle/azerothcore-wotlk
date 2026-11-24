"""Execute unchanged native read-only preflight body with explicit engine doubles."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ledger-double", action="store_true",
                    help="explicit control-flow-only seam for toolchains lacking atomic<shared_ptr>")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"
text = (native / "PlayerStorage.cpp").read_text(encoding="utf-8")
start = text.index("bool Player::CheckPB59InventoryBatchPlan(")
end = text.index("std::optional<PB59::InventorySlotFacts> Player::CapturePB59InventorySlot", start)
body = text[start:end]
fixture = r'''
#include "PB59InventoryBatchPlan.h"
#if !PB59_LEDGER_DOUBLE
#include "PB59TransactionAdmission.h"
#else
// Not TransactionAdmission evidence. Native-header mode remains the default.
namespace PB59 { struct TransactionAdmission {
    bool available = true;
    std::uint64_t OwnerIncarnation() const { return 1; }
    bool PendingSave() const { return false; }
    bool PendingLogout() const { return false; }
    bool CanBegin() const { return available; }
}; }
#endif
#include "EquipmentCaptureState.h"
#include <cstdio>
#include <map>
using uint64 = std::uint64_t;
using uint16 = std::uint16_t;
constexpr int INVENTORY_SLOT_BAG_0 = 255, EQUIPMENT_SLOT_END = 19;
constexpr int EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18;
struct Guid { uint64 GetRawValue() const { return 1; } };
struct Player;
struct Session {
    Player* player = nullptr;
    bool loading = false, logout = false, headless = true;
    uint64 incarnation = 7;
    Player* GetPlayer() const { return player; }
    uint64 GetPB59SessionIncarnation() const { return incarnation; }
    bool PlayerLoading() const { return loading; }
    bool PlayerLogout() const { return logout; }
};
struct Player {
    Session session;
    PB59::TransactionAdmission _pb59Transaction;
    PB59::EquipmentCaptureState _pb59Equipment;
    bool world = true, alive = true, combat = false, far = false, near = false, unsettled = false;
    std::map<uint16, PB59::InventorySlotFacts> slots;
    mutable unsigned captures = 0;
    Player() { session.player = this; _pb59Equipment.Bind(); }
    Session const* GetSession() const { return &session; }
    Guid GetGUID() const { return {}; }
    bool IsInWorld() const { return world; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsBeingTeleportedFar() const { return far; }
    bool IsBeingTeleportedNear() const { return near; }
    bool HasPB59UnsettledOriginals() const { return unsettled; }
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos) const {
        ++captures; auto it = slots.find(pos);
        return it == slots.end() ? std::nullopt : std::optional{it->second};
    }
    bool CheckPB59InventoryBatchPlan(uint64, uint64, uint64, PB59::Stamp const&,
        std::span<PB59::InventoryBatchMove const>) const;
};
'''
checks = r'''
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %u\n", checks); return 1; } } while (false)
int main() {
    unsigned checks = 0;
    Player player;
    PB59::InventoryInstanceFacts item;
    item.guid = 10; item.owner = 1; item.entry = 20; item.count = 1;
    PB59::InventoryBatchMove move{{1, 0, 65280, item}, {1, 0, 65303, std::nullopt}};
    player.slots.emplace(move.source.position, move.source);
    player.slots.emplace(move.destination.position, move.destination);
    auto stamp = *player._pb59Equipment.Capture();
    auto incarnation = player._pb59Transaction.OwnerIncarnation();
    auto check = [&] { return player.CheckPB59InventoryBatchPlan(1, incarnation, 7, stamp, {&move, 1}); };
    CHECK(check()); CHECK(player.session.headless); // bot context, not human authorization
    CHECK(!player.CheckPB59InventoryBatchPlan(2, incarnation, 7, stamp, {&move, 1}));
    CHECK(!player.CheckPB59InventoryBatchPlan(1, incarnation + 1, 7, stamp, {&move, 1}));
    CHECK(!player.CheckPB59InventoryBatchPlan(1, incarnation, 8, stamp, {&move, 1}));
    for (bool* field : {&player.session.loading, &player.session.logout, &player.combat,
                       &player.far, &player.near, &player.unsettled}) {
        *field = true; CHECK(!check()); *field = false;
    }
    for (bool* field : {&player.world, &player.alive}) {
        *field = false; CHECK(!check()); *field = true;
    }
    player.session.player = nullptr; CHECK(!check()); player.session.player = &player;
    player.slots[move.destination.position].bag = 99; CHECK(!check());
    player.slots[move.destination.position] = move.destination;
    player.slots[move.source.position].item->durability = 1; CHECK(!check());
    player.slots[move.source.position] = move.source;
    player.slots.erase(move.destination.position); CHECK(!check());
    player.slots.emplace(move.destination.position, move.destination);
    auto source = move.source; move.source.position = 65304; CHECK(!check()); move.source = source;
    { PB59::EquipmentCaptureState::Mutation mutation(player._pb59Equipment); CHECK(!check()); }
    CHECK(!check()); stamp = *player._pb59Equipment.Capture(); CHECK(check());
#if PB59_LEDGER_DOUBLE
    player._pb59Transaction.available = false; CHECK(!check());
    player._pb59Transaction.available = true; CHECK(check());
#else
    auto ticket = player._pb59Transaction.Begin(); CHECK(ticket != 0); CHECK(!check());
    CHECK(player._pb59Transaction.Finish(ticket, {true, true, true, true})); CHECK(check());
    auto retirement = player._pb59Transaction.TryAcquireRetirement(); CHECK(bool(retirement)); CHECK(!check());
#endif
    std::printf("%u actual native admission-body checks PASS (engine capture/session doubles)\n", checks);
}
'''
with tempfile.TemporaryDirectory(prefix="pb59-batch-admission-") as temporary:
    source = Path(temporary) / "admission.cpp"
    binary = Path(temporary) / "admission.exe"
    source.write_text(fixture + body + checks, encoding="utf-8")
    print("Ledger mode: DOUBLE (control flow only)" if args.ledger_double else "Ledger mode: ACTUAL HEADER", flush=True)
    subprocess.run(args.compiler + ["-std=c++20", "-DPB59_LEDGER_DOUBLE=" + str(int(args.ledger_double)),
                                    "-Wall", "-Wextra", "-Werror", "-I", str(native),
                                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
