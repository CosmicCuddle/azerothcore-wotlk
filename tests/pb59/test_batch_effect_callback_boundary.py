"""Actual _ApplyItemMods body; callback/unlink and effect engines are doubles.

Demonstrates that an outer ledger scope cannot interpose inside this native
method after its bonus callback. No freed pointer is dereferenced in the test.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", nargs="+", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"
source = (native / "Player.cpp").read_text(encoding="utf-8")
start = source.index("void Player::_ApplyItemMods(")
end = source.index("void Player::_ApplyItemBonuses(", start)
body = source[start:end]
fixture = r'''
#include "PB59TransactionAdmission.h"
#include "PB59EffectBoundary.h"
#include <cstdio>
#include <functional>
using uint8 = unsigned char;
constexpr uint8 INVENTORY_SLOT_BAG_END = 23, EQUIPMENT_SLOT_RANGED = 17;
enum WeaponAttackType { MAX_ATTACK };
struct ItemTemplate { };
struct Item {
    bool linked = true;
    ItemTemplate prototype;
    ItemTemplate const* GetTemplate() const { return &prototype; }
    bool IsBroken() const { return false; }
    bool HasSocket() const { return false; }
};
#define LOG_DEBUG(...) do { } while (false)
struct Player {
    PB59::TransactionAdmission ledger;
    std::function<void()> callback;
    unsigned callsAfterUnlink = 0;
    void _ApplyItemMods(Item*, uint8, bool);
    void CorrectMetaGemEnchants(uint8, bool) { }
    void _ApplyItemBonuses(ItemTemplate const*, uint8, bool) { if (callback) callback(); }
    void _ApplyAmmoBonuses() { }
    void ApplyItemEquipSpell(Item* item, bool) { if (!item->linked) ++callsAfterUnlink; }
    void ApplyItemDependentAuras(Item* item, bool) { if (!item->linked) ++callsAfterUnlink; }
    static WeaponAttackType GetAttackBySlot(uint8) { return MAX_ATTACK; }
    void UpdateWeaponDependentAuras(WeaponAttackType) { }
    void ApplyEnchantment(Item* item, bool) { if (!item->linked) ++callsAfterUnlink; }
};
'''
checks = r'''
int main() {
    Player player;
    Item item;
    PB59::TransactionAdmission::MutationScope scope(player.ledger);
    if (!scope.GetTicket()) return 1;
    player._ApplyItemMods(&item, 0, false);
    if (player.callsAfterUnlink) return 2;
    player.callback = [&] {
        // Models an unguarded native inventory mutation from a module hook.
        // It is NOT an execution of actual RemoveItem or a real module.
        item.linked = false;
    };
    player._ApplyItemMods(&item, 0, false);
    if (player.callsAfterUnlink != 3) return 3;
    if (!scope.CanAccess(player.ledger.OwnerIncarnation(),
                        PB59::TransactionAdmission::Access::Forward)) return 4;
    item.linked = true;
    player.callsAfterUnlink = 0;
    {
        PB59::EffectBoundary boundary(&player, &item,
            [](void* context) { return static_cast<Item*>(context)->linked; });
        player._ApplyItemMods(&item, 0, false);
        if (!boundary.Failed() || player.callsAfterUnlink) return 5;
        item.linked = true;
        if (PB59::EffectBoundary::Check(&player)) return 6; // cannot reset uncertain effects
    }
    // The ledger alone cannot see the callback's direct inventory write.
    std::puts("Actual item-mod body: ordinary gap control + opt-in early-stop/sticky failure PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="pb59-effect-boundary-") as temporary:
    path = Path(temporary)
    cpp = path / "boundary.cpp"
    binary = path / "boundary.exe"
    cpp.write_text(fixture + body + checks, encoding="utf-8")
    subprocess.run(args.compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(native), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
