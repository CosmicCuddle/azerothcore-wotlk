"""Compile exact durability methods with isolated synthetic item/payment/effect adapters."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--compiler', default='g++')
parser.add_argument('--ndebug', action='store_true')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()

def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2]

loss = method('void Player::DurabilityPointsLoss(Item* item, int32 points)')
repair = method('uint32 Player::DurabilityRepair(uint16 pos, bool cost, float discountMod, bool guildBank)')
fixture = r'''
#include <cstdint>
#include <iostream>
using uint32 = std::uint32_t;
using int32 = std::int32_t;
using uint16 = std::uint16_t;
enum { ITEM_FIELD_MAXDURABILITY, ITEM_FIELD_DURABILITY, ITEM_CHANGED, RATE_REPAIRCOST };
#define LOG_ERROR(...) ((void)0)
struct Player;
struct ItemTemplate { uint32 ItemLevel=1, Quality=1, Class=1, SubClass=1; };
struct Item {
    bool allowed=true, equipped=true;
    uint32 durability=50, maximum=100;
    int writes=0, queued=0, reads=0, guards=0;
    ItemTemplate proto;
    bool PB59InventoryMutationAllowed() { ++guards; return allowed; }
    uint32 GetUInt32Value(int field) { ++reads; return field==ITEM_FIELD_DURABILITY?durability:maximum; }
    void SetUInt32Value(int, uint32 value) { ++writes; durability=value; }
    bool IsEquipped() const { return equipped; }
    int GetSlot() const { return 1; }
    void SetState(int, Player*);
    ItemTemplate const* GetTemplate() const { return &proto; }
};
struct Equipment { bool allowed=true; bool InventoryMutationAllowed() const { return allowed; } };
struct DurabilityCostsEntry { uint32 multiplier[1]{1}; };
struct DurabilityQualityEntry { double quality_mod=1; };
template<class T> struct Store { T entry; bool available=true; T const* LookupEntry(uint32) { return available?&entry:nullptr; } };
Store<DurabilityCostsEntry> sDurabilityCostsStore;
Store<DurabilityQualityEntry> sDurabilityQualityStore;
uint32 ItemSubClassToDurabilityMultiplierId(uint32, uint32) { return 0; }
struct World { double getRate(int) { return 1; } } world;
World* sWorld=&world;
struct Guild { int charges=0; bool allow=true; bool HandleMemberWithdrawMoney(void*, uint32, bool) { ++charges; return allow; } } guild;
struct GuildMgr { Guild* GetGuildById(uint32) { return &guild; } } guildMgr;
GuildMgr* sGuildMgr=&guildMgr;
struct Player {
    Equipment _pb59Equipment;
    Item* item=nullptr;
    bool aura=false, enough=true;
    int mods=0, money=0, lookups=0;
    bool HasPreventDurabilityLossAura() const { return aura; }
    Item* GetItemByPos(uint16) { ++lookups; return item; }
    void _ApplyItemMods(Item*, int, bool) { ++mods; }
    bool IsEquipmentPos(uint16) const { return true; }
    uint32 GetGuildId() const { return 1; }
    void* GetSession() const { return nullptr; }
    bool HasEnoughMoney(uint32) const { return enough; }
    void ModifyMoney(int32 cost) { money += cost; }
    void DurabilityPointsLoss(Item*, int32);
    uint32 DurabilityRepair(uint16, bool, float, bool);
};
void Item::SetState(int, Player* owner) { if (allowed && owner->_pb59Equipment.allowed) ++queued; }
'''
checks = r'''
unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while(false)
int main() {
    for (bool blockOwner : {false,true}) for (bool blockItem : {false,true}) {
        for (bool paid : {false,true}) for (bool fromGuild : {false,true}) {
            Player p; Item i; p.item=&i; p._pb59Equipment.allowed=!blockOwner; i.allowed=!blockItem;
            i.durability=0; guild.charges=0;
            auto cost=p.DurabilityRepair(1,paid,1,fromGuild);
            if (blockOwner || blockItem) {
                CHECK(i.durability==0 && i.writes==0 && i.queued==0);
                CHECK(p.mods==0 && p.money==0 && guild.charges==0 && cost==0);
                CHECK(i.reads==0);
                if (blockOwner) CHECK(p.lookups==0);
            } else {
                CHECK(i.durability==100 && i.writes==1 && i.queued==1 && p.mods==1);
                CHECK(p.money==(paid&&!fromGuild?-100:0));
                CHECK(guild.charges==(paid&&fromGuild?1:0));
                CHECK(cost==(paid&&fromGuild?100u:0u));
            }
        }
        for (int points : {10,50,-10}) {
            Player p; Item i; p.item=&i; p._pb59Equipment.allowed=!blockOwner; i.allowed=!blockItem;
            p.DurabilityPointsLoss(&i, points);
            if (blockOwner || blockItem) {
                CHECK(i.durability==50 && i.writes==0 && i.queued==0 && p.mods==0);
                CHECK(i.reads==0);
            } else {
                CHECK(i.durability==uint32(50-points) && i.writes==1 && i.queued==1);
                CHECK(p.mods==(points==50?1:0));
            }
        }
    }
    { Player p; Item i; p.aura=true; p.DurabilityPointsLoss(&i, 50); CHECK(i.durability==50 && i.writes==0); }
    { Player p; p.DurabilityPointsLoss(nullptr, 50); CHECK(p.mods==0); }
    { Player p; CHECK(p.DurabilityRepair(1,true,1,false)==0 && p.money==0); }
    { Player p; Item i; p.item=&i; p.enough=false; CHECK(p.DurabilityRepair(1,true,1,false)==0 && i.durability==50 && p.money==0); }
    { Player p; Item i; p.item=&i; guild.allow=false; CHECK(p.DurabilityRepair(1,true,1,true)==0 && i.durability==50); guild.allow=true; }
    std::cout << checks << '/' << checks << " durability checks passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='pb59-durability-') as tmp:
    cpp = Path(tmp)/'checks.cpp'
    cpp.write_text(fixture+'\n'+loss+'\n'+repair+'\n'+checks)
    binary = Path(tmp)/'checks'
    flags = ['-std=c++20','-Wall','-Wextra','-Werror','-pedantic']
    if args.ndebug:
        flags += ['-O2','-DNDEBUG']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-D_GLIBCXX_ASSERTIONS']
    subprocess.run([args.compiler,*flags,str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
