// Exact native delayed-operation and autosave bodies; gameplay/DB dependencies mocked.
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
using uint32=std::uint32_t;
using A=PB59::TransactionAdmission;
constexpr uint32 DELAYED_RESURRECT_PLAYER=1,DELAYED_SAVE_PLAYER=2,DELAYED_SPELL_CAST_DESERTER=4,
    DELAYED_BG_MOUNT_RESTORE=8,DELAYED_BG_TAXI_RESTORE=16,DELAYED_BG_GROUP_RESTORE=32,DELAYED_VEHICLE_TELEPORT=64;
constexpr int POWER_MANA=0,POWER_RAGE=1,POWER_ENERGY=2,SPELL_AURA_MOD_SHAPESHIFT=3;
struct Player;
struct Unit{unsigned clicks=0;void HandleSpellClick(Player*,int){++clicks;}};
using SeatMap=std::map<int,int>;
struct Vehicle{SeatMap Seats{{0,0}};Unit base;auto GetSeatIteratorForPassenger(Player*){return Seats.begin();}Unit* GetBase(){return &base;}};
struct Group{unsigned updates=0;void SendUpdateToPlayer(unsigned){++updates;}};
struct Entry{unsigned mountSpell=42,taxiPath[2]{1,2};bool HasTaxiPath(){return taxiPath[0]!=0;}void ClearTaxiPath(){taxiPath[0]=taxiPath[1]=0;}};
struct Taxi{unsigned destinations=0;void AddTaxiDestination(unsigned){++destinations;}};
#define LOG_DEBUG(...) ((void)0)
struct Player
{
    A _pb59Transaction;
    uint32 m_DelayedOperations=0,m_nextSave=0,m_resurrectHealth=10,m_resurrectMana=10, interval=0;
    unsigned saves=0,resurrections=0,bones=0,deserters=0,mounts=0,taxis=0,exits=0;
    bool far=false;
    Entry m_entryPointData;Taxi m_taxi;Group group;Vehicle vehicle;
    std::function<void()> onSave,onTaxi;
    bool IsBeingTeleportedFar()const{return far;}
    void SaveToDB(bool create,bool logout){assert(!create&&!logout&&!far);assert(!_pb59Transaction.Begin());++saves;m_nextSave=interval;if(onSave)onSave();}
    void ResurrectPlayer(float,bool){++resurrections;}uint32 GetMaxHealth(){return 20;}void SetHealth(uint32){}void SetFullHealth(){}
    uint32 GetMaxPower(int){return 20;}void SetPower(int,uint32){}void SpawnCorpseBones(){++bones;}
    bool GetAura(int){return false;}void CastSpell(Player*,int,bool){++deserters;}
    bool IsInDisallowedMountForm(){return true;}void RemoveAurasByType(int){}void AddAura(unsigned,Player*){++mounts;}
    void ContinueTaxiFlight(){++taxis;if(onTaxi)onTaxi();}Group* GetGroup(){return &group;}unsigned GetGUID(){return 1;}
    Vehicle* GetVehicle(){return &vehicle;}void ExitVehicle(){++exits;}
    void ProcessDelayedOperations();
    void AutoSaveTick(uint32 p_time){
#include "AutoSaveTick.inc"
    }
};
#include "ProcessDelayedOperations.inc"
constexpr A::StableProof stable{true,true,true,true};
static void allEffectsOnce(Player& p)
{
    assert(p.resurrections==1&&p.bones==1&&p.deserters==1&&p.mounts==1&&p.taxis==1&&p.m_taxi.destinations==2&&p.group.updates==1&&p.exits==1&&p.vehicle.base.clicks==1);
}
int main()
{
    for(bool recovery:{false,true}){
        Player p;p.m_DelayedOperations=127;auto ticket=p._pb59Transaction.Begin();assert(ticket);
        if(recovery)assert(p._pb59Transaction.RequireRecovery(ticket));
        p.onTaxi=[&]{p.m_nextSave=200;}; // later effect overwrites ordinary timer
        p.ProcessDelayedOperations();assert(p.saves==0&&p.m_nextSave==1&&p.m_DelayedOperations==0);allEffectsOnce(p);
        p.ProcessDelayedOperations();p.AutoSaveTick(50);assert(p.saves==0&&p.m_nextSave==1);allEffectsOnce(p);
        assert(p._pb59Transaction.Finish(ticket,stable));p.AutoSaveTick(50);assert(p.saves==1&&p.m_nextSave==0);allEffectsOnce(p);
        p.AutoSaveTick(50);assert(p.saves==1);
    }
    {Player p;p.m_DelayedOperations=127;p.ProcessDelayedOperations();assert(p.saves==1&&p.m_nextSave==0&&p.m_DelayedOperations==0);allEffectsOnce(p);}
    {Player p;p.far=true;p.m_DelayedOperations=DELAYED_SAVE_PLAYER;p.ProcessDelayedOperations();assert(p.saves==0&&p.m_nextSave==1);
     p.AutoSaveTick(50);assert(p.saves==0&&p.m_nextSave==1);p.far=false;p.AutoSaveTick(50);assert(p.saves==1&&p.m_nextSave==0);}
    {Player p;p.m_DelayedOperations=DELAYED_SAVE_PLAYER;p.onSave=[&]{p.m_DelayedOperations|=DELAYED_SAVE_PLAYER;};
     p.ProcessDelayedOperations();assert(p.saves==1&&p.m_nextSave==1&&p.m_DelayedOperations==0);p.onSave={};p.AutoSaveTick(50);assert(p.saves==2&&p.m_nextSave==0);}
    {Player p;p.m_DelayedOperations=DELAYED_BG_TAXI_RESTORE;p.onTaxi=[&]{p.m_DelayedOperations|=DELAYED_SAVE_PLAYER;};
     p.ProcessDelayedOperations();assert(p.saves==0&&p.m_nextSave==1&&p.taxis==1);p.AutoSaveTick(50);assert(p.saves==1&&p.taxis==1);}
    {Player p;p.m_DelayedOperations=DELAYED_BG_TAXI_RESTORE;p.ProcessDelayedOperations();assert(p.saves==0&&p.m_nextSave==0&&p.taxis==1);}
}
