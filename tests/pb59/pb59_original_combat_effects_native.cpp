#include "PB59OriginalCombatEffects.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
using uint8=std::uint8_t; using uint32=std::uint32_t;
constexpr unsigned INVENTORY_SLOT_BAG_0=255,EQUIPMENT_SLOT_END=19,EQUIPMENT_SLOT_BODY=3,EQUIPMENT_SLOT_TABARD=18;
struct ItemTemplate { uint32 ItemSet=9; };
struct ItemSetEntry { uint32 required_skill_id=1,required_skill_value=10; };
struct Store { bool missing=false; ItemSetEntry row; ItemSetEntry const* LookupEntry(uint32 id) const { return id==9&&!missing?&row:nullptr; } } sItemSetStore;
struct Guid { std::uint64_t GetRawValue() const {return 42;} };
struct Item { ItemTemplate proto; Guid GetGUID() const{return {};} ItemTemplate const* GetTemplate() const{return &proto;} };
struct Effect { uint32 setid=9,item_count=2; };
namespace PB59 {
struct Instance {std::uint64_t guid=42; bool operator==(Instance const&) const=default;};
struct InventorySlotFacts {std::uint64_t owner=7,bag=0; std::uint16_t position=0xff00; std::optional<Instance> item=Instance{}; bool operator==(InventorySlotFacts const&) const=default;};
struct TransactionAdmission {using Ticket=std::uint64_t; enum class Access {Forward,Recovery}; bool recovery=false; bool CanAccess(Ticket t,Access a) const {return t==1&&recovery==(a==Access::Recovery);} void RequireRecovery(Ticket t){assert(t==1);recovery=true;}};
struct EquipmentCaptureState {struct Mutation{explicit Mutation(EquipmentCaptureState&) {}};};
}
struct Player;
void AddItemsSetItem(Player*,Item*);
void RemoveItemsSetItem(Player*,ItemTemplate const*);
struct Player {
 PB59::TransactionAdmission _pb59Transaction; PB59::EquipmentCaptureState _pb59Equipment;
 std::array<PB59::OriginalCombatEffects,19> _pb59OriginalCombatEffects{};
 Item item; Effect effect; std::vector<Effect*> ItemSetEff{&effect};
 PB59::InventorySlotFacts facts; bool combat=false,missing=false,skipSet=false,failMods=false,changed=false; unsigned skill=10; int stat=30,modsCalls=0,setCalls=0;
 bool IsInCombat() const{return combat;} unsigned GetSkillValue(unsigned) const{return skill;}
 Item* GetItemByPos(unsigned p){return p==facts.position&&!missing?&item:nullptr;}
 std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(unsigned p) const {if(p!=facts.position||missing)return {};auto f=facts;if(changed)f.owner=99;return f;}
 void _ApplyItemMods(Item*,uint8,bool add){++modsCalls;stat+=add?10:-10;if(failMods)throw std::runtime_error("partial mods");}
 PB59::OriginalCombatEffects::Result ReconcilePB59OriginalCombatEffects(PB59::InventorySlotFacts const&,PB59::TransactionAdmission::Ticket,bool);
};
void AddItemsSetItem(Player* p,Item*){++p->setCalls;if(!p->skipSet){++p->effect.item_count;if(p->ItemSetEff.empty())p->ItemSetEff.push_back(&p->effect);}}
void RemoveItemsSetItem(Player* p,ItemTemplate const*){++p->setCalls;if(!p->skipSet&&!--p->effect.item_count)p->ItemSetEff.clear();}
#include "native_effects.inc"
int main(){using R=PB59::OriginalCombatEffects::Result;unsigned n=0;auto check=[&](bool ok){assert(ok);++n;};
 {Player p;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Complete);check(p.effect.item_count==1&&p.stat==20);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);check(p.ReconcilePB59OriginalCombatEffects(p.facts,2,true)==R::Rejected);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Complete);check(p.effect.item_count==2&&p.stat==30&&p.modsCalls==2&&p.setCalls==2);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Rejected);}
 {Player p;p.effect.item_count=1;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Complete);check(p.ItemSetEff.empty());check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Complete);check(p.effect.item_count==1&&p.ItemSetEff.size()==1);}
 {Player p;p.combat=true;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);p.combat=false;p.skill=0;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);p.skill=10;sItemSetStore.missing=true;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);sItemSetStore.missing=false;p.ItemSetEff.push_back(&p.effect);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);check(p.setCalls==0&&p.modsCalls==0);}
 {Player p;p.skipSet=true;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::RecoveryRequired);check(p._pb59Transaction.recovery&&p.modsCalls==0);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Rejected);}
 {Player p;p.failMods=true;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::RecoveryRequired);check(p.effect.item_count==1&&p.stat==20&&p._pb59Transaction.recovery);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Rejected);check(p.modsCalls==1);}
 {Player p;p.item.proto.ItemSet=0;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Complete);check(p.setCalls==0&&p.stat==20);check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,true)==R::Complete);check(p.stat==30&&p.setCalls==0);}
 {Player p;p.changed=true;check(p.ReconcilePB59OriginalCombatEffects(p.facts,1,false)==R::Rejected);check(p.setCalls==0&&p.modsCalls==0);}
 std::cout<<n<<" native combat effect adapter mock checks passed\n";}
