#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>
using uint32=std::uint32_t; using uint8=std::uint8_t; using int32=std::int32_t;
constexpr unsigned ITEM_CLASS_ARMOR=4, ITEM_CLASS_WEAPON=2, INVTYPE_NON_EQUIP=0,
    INVTYPE_BODY=4, INVTYPE_TABARD=19, INVTYPE_BAG=18, INVTYPE_QUIVER=27,
    INVTYPE_AMMO=24, INVTYPE_RELIC=28, ITEM_QUALITY_HEIRLOOM=7,
    NULL_SLOT=255, PROP_ENCHANTMENT_SLOT_0=7, MAX_INSPECTED_ENCHANTMENT_SLOT=7,
    DEFAULT_NON_CROSSREALM_REALM_ID=0, MAX_ITEM_PROTO_SPELLS=5;
enum Field {ITEM_FIELD_OWNER, ITEM_FIELD_CONTAINED, ITEM_FIELD_STACK_COUNT,
    ITEM_FIELD_MAXDURABILITY, ITEM_FIELD_DURABILITY, ITEM_FIELD_DURATION, ITEM_FIELD_CREATE_PLAYED_TIME};
enum class HighGuid {Item};
// Core spells the scoped generator tag HighGuid::Item. The mock uses an enum class.
struct ObjectGuid {using LowType=uint32; uint32 id=0; static ObjectGuid const Empty; bool IsEmpty()const{return !id;} explicit operator bool()const{return id!=0;}};
ObjectGuid const ObjectGuid::Empty{};
struct Player {ObjectGuid GetGUID() const{return {123};}};
struct ItemTemplate {uint32 ItemLevel=60,Class=4,InventoryType=1,Duration=0,ScalingStatDistribution=0,ScalingStatValue=0,Quality=3,MaxDurability=50,stack=1; int32 RandomProperty=0,RandomSuffix=0; struct Spell {int32 SpellCharges=0;}; std::array<Spell,5> Spells{}; uint32 GetMaxStackSize()const{return stack;}};
struct Generator {uint32 next=0;uint32 Generate(unsigned){return ++next;}};
struct ObjectMgr {std::map<uint32,ItemTemplate> templates;Generator gen; ItemTemplate const* GetItemTemplate(uint32 id){auto i=templates.find(id);return i==templates.end()?nullptr:&i->second;} template<HighGuid> Generator& GetGenerator(){return gen;}} objectMgr;
ObjectMgr* sObjectMgr=&objectMgr;
struct Sidecar {bool cross=false;bool IsCrossrealm()const{return cross;}} sidecar;
Sidecar* sToCloud9Sidecar=&sidecar;
struct Store {std::set<uint32> ids; const uint32* LookupEntry(uint32 id)const{auto i=ids.find(id);return i==ids.end()?nullptr:&*i;}} sItemRandomPropertiesStore,sItemRandomSuffixStore;
struct Ench {uint32 ench;};std::map<uint32,std::vector<Ench>> RandomItemEnch;
#include "membership.inc"
uint32 suffixFactor=25;uint32 GenerateEnchSuffixFactor(uint32){return suffixFactor;}
struct Item;
struct ScriptMgr {int notifications=0;Player const* owner=nullptr;void OnItemCreate(Item*,ItemTemplate const*,Player const* p){++notifications;owner=p;}} scriptMgr;
ScriptMgr* sScriptMgr=&scriptMgr;
enum State {ITEM_NEW,ITEM_CHANGED};
struct Object {ObjectGuid guid;void _Create(uint32 id,int,HighGuid){guid={id};}};
struct Item: Object {
    static inline unsigned alive=0; static inline bool wrongProperty=false,queueUnexpected=false,worldUnexpected=false;
    uint32 entry=0,slot=0; int32 property=0;std::array<uint32,7> fields{};std::array<int32,5> charges{};bool queued=false,world=false;State state=ITEM_NEW;
    Item(){++alive;} ~Item(){--alive;}
    bool Create(ObjectGuid::LowType,uint32,Player const*);bool CreateInternal(ObjectGuid::LowType,uint32,Player const*,bool);
    static std::unique_ptr<Item> CreatePB59Detached(uint32,int32);
    void SetEntry(uint32 v){entry=v;} void SetObjectScale(float){} void SetGuidValue(Field f,ObjectGuid v){fields[f]=v.id;} void SetUInt32Value(Field f,uint32 v){fields[f]=v;} void SetSpellCharges(uint8 i,int32 v){charges[i]=v;}
    void SetSlot(uint32 v){slot=v;}void SetItemRandomProperties(int32 p){property=wrongProperty?p+1:p;queued=queueUnexpected;world=worldUnexpected;}
    ObjectGuid GetGUID()const{return guid;}uint32 GetEntry()const{return entry;}uint32 GetCount()const{return fields[ITEM_FIELD_STACK_COUNT];}int32 GetItemRandomPropertyId()const{return property;}ObjectGuid GetOwnerGUID()const{return {fields[ITEM_FIELD_OWNER]};}ObjectGuid GetGuidValue(Field f)const{return {fields[f]};}void* GetContainer()const{return nullptr;}uint32 GetSlot()const{return slot;}bool IsInWorld()const{return world;}bool IsInUpdateQueue()const{return queued;}State GetState()const{return state;}uint32 GetItemSuffixFactor()const{return property<0?suffixFactor:0;}
};
#include "init.inc"
#include "factory.inc"
int main(){unsigned n=0;
#define CHECK(x) do {assert(x);++n;}while(false)
auto& ts=objectMgr.templates; ts[1]=ItemTemplate{}; ts[1].Spells[0].SpellCharges=-3;ts[2]=ts[1];ts[2].RandomProperty=10;ts[3]=ts[1];ts[3].RandomSuffix=20;
RandomItemEnch[10]={{101},{102}};RandomItemEnch[20]={{201}};sItemRandomPropertiesStore.ids={101,102,103};sItemRandomSuffixStore.ids={201,202};
{auto a=Item::CreatePB59Detached(1,0);CHECK(a&&a->GetEntry()==1&&a->GetItemRandomPropertyId()==0);CHECK(a->GetCount()==1&&a->GetSlot()==255);CHECK(!a->GetOwnerGUID()&&!a->GetGuidValue(ITEM_FIELD_CONTAINED));CHECK(!a->IsInUpdateQueue()&&!a->IsInWorld()&&a->GetState()==ITEM_NEW);CHECK(a->fields[ITEM_FIELD_DURABILITY]==50&&a->charges[0]==-3);CHECK(scriptMgr.notifications==0);}
CHECK(Item::alive==0);{auto a=Item::CreatePB59Detached(2,101);CHECK(a&&a->GetItemRandomPropertyId()==101);auto b=Item::CreatePB59Detached(2,102);CHECK(b&&b->GetItemRandomPropertyId()==102&&b->GetGUID().id!=a->GetGUID().id);auto c=Item::CreatePB59Detached(3,-201);CHECK(c&&c->GetItemRandomPropertyId()==-201&&c->GetItemSuffixFactor()==25);}
CHECK(Item::alive==0);CHECK(!Item::CreatePB59Detached(2,0));CHECK(!Item::CreatePB59Detached(3,0));CHECK(!Item::CreatePB59Detached(2,103));CHECK(!Item::CreatePB59Detached(3,-202));CHECK(!Item::CreatePB59Detached(2,-201));CHECK(!Item::CreatePB59Detached(3,101));CHECK(!Item::CreatePB59Detached(1,101));CHECK(!Item::CreatePB59Detached(3,std::numeric_limits<int32>::min()));CHECK(!Item::CreatePB59Detached(99,0));CHECK(!HasItemEnchantMod(-1,101));CHECK(!HasItemEnchantMod(0,101));CHECK(!HasItemEnchantMod(10,0));CHECK(!HasItemEnchantMod(999,101));
{auto copy=ts[1];auto refuse=[&](ItemTemplate p){ts[4]=p;CHECK(!Item::CreatePB59Detached(4,0));};copy.InventoryType=INVTYPE_BAG;refuse(copy);copy=ts[1];copy.InventoryType=INVTYPE_TABARD;refuse(copy);copy=ts[1];copy.InventoryType=INVTYPE_BODY;refuse(copy);copy=ts[1];copy.Duration=10;refuse(copy);copy=ts[1];copy.ScalingStatValue=1;refuse(copy);copy=ts[1];copy.Quality=ITEM_QUALITY_HEIRLOOM;refuse(copy);copy=ts[1];copy.stack=2;refuse(copy);copy=ts[1];copy.RandomProperty=10;copy.RandomSuffix=20;refuse(copy);copy=ts[1];copy.RandomProperty=-1;refuse(copy);}
sidecar.cross=true;CHECK(!Item::CreatePB59Detached(1,0));sidecar.cross=false;suffixFactor=0;CHECK(!Item::CreatePB59Detached(3,-201));suffixFactor=25;
sItemRandomPropertiesStore.ids.erase(101);CHECK(!Item::CreatePB59Detached(2,101));sItemRandomPropertiesStore.ids.insert(101);
Item::wrongProperty=true;CHECK(!Item::CreatePB59Detached(2,101));Item::wrongProperty=false;Item::queueUnexpected=true;CHECK(!Item::CreatePB59Detached(1,0));Item::queueUnexpected=false;Item::worldUnexpected=true;CHECK(!Item::CreatePB59Detached(1,0));Item::worldUnexpected=false;CHECK(Item::alive==0);CHECK(scriptMgr.notifications==0);
{Player p;Item i;CHECK(i.Create(999,1,&p));CHECK(scriptMgr.notifications==1&&scriptMgr.owner==&p);CHECK(i.GetOwnerGUID().id==123&&i.GetGuidValue(ITEM_FIELD_CONTAINED).id==123);}
CHECK(Item::alive==0);std::cout<<n<<" checks PASS\n";
}
