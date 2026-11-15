// Exact native scheduled-save bodies; DB and Player dependencies are synthetic.
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>
using uint32=std::uint32_t;
using uint8=std::uint8_t;
using A=PB59::TransactionAdmission;
constexpr uint8 ADDITIONAL_SAVING_INVENTORY_AND_GOLD=1, ADDITIONAL_SAVING_QUEST_STATUS=2,
    ADDITIONAL_SAVING_ACHIEVEMENTS=4;
using CharacterDatabaseTransaction=unsigned;
struct Database
{
    unsigned begins=0, commits=0;
    unsigned BeginTransaction(){return ++begins;}
    void CommitTransaction(unsigned id){assert(id==begins);++commits;}
} CharacterDatabase;
struct Session { bool logout=false; bool IsLoggingOut() const{return logout;} };
struct Achievement {std::vector<unsigned>* trace; void SaveToDB(unsigned){trace->push_back(7);} };
#define LOG_DEBUG(...) ((void)0)
struct Player
{
    A _pb59Transaction;
    uint32 m_nextSave=0,m_additionalSaveTimer=0;
    uint8 m_additionalSaveMask=0;
    Session session;
    bool farTeleport=false;
    bool IsBeingTeleportedFar() const{return farTeleport;}
    std::vector<unsigned> trace;
    Achievement achievement{&trace};
    Achievement* m_achievementMgr=&achievement;
    std::function<void()> onSave;
    Session* GetSession(){return &session;}
    void SaveInventoryAndGoldToDB(unsigned){trace.push_back(1);if(onSave)onSave();}
    void _SaveQuestStatus(unsigned){trace.push_back(2);}
    void _SaveDailyQuestStatus(unsigned){trace.push_back(3);}
    void _SaveWeeklyQuestStatus(unsigned){trace.push_back(4);}
    void _SaveSeasonalQuestStatus(unsigned){trace.push_back(5);}
    void _SaveMonthlyQuestStatus(unsigned){trace.push_back(6);}
    void _SaveSkills(unsigned){trace.push_back(8);}
    void SaveGoldToDB(unsigned){trace.push_back(9);}
    void SaveToDB(bool create,bool logout)
    {
        assert(!create&&!logout);trace.push_back(10);
        m_nextSave=1000;m_additionalSaveMask=0;m_additionalSaveTimer=0;
        if(onSave)onSave();
    }
    void UpdateAdditionalSaves(uint32);
    void AutoSaveTick(uint32 p_time)
    {
#include "AutoSaveTick.inc"
    }
};
#include "UpdateAdditionalSaves.inc"
constexpr A::StableProof stable{true,true,true,true};
int main()
{
    // Every nonempty category combination, active and recovery states.
    for(bool recovery:{false,true}) for(uint8 mask=1;mask<8;++mask)
    {
        Player p; CharacterDatabase={};
        p.m_additionalSaveTimer=1;p.m_additionalSaveMask=mask;
        auto t=p._pb59Transaction.Begin();assert(t);
        if(recovery)assert(p._pb59Transaction.RequireRecovery(t));
        for(unsigned tick=0;tick<3;++tick)p.UpdateAdditionalSaves(20);
        assert(p.m_additionalSaveTimer==1&&p.m_additionalSaveMask==mask);
        assert(!CharacterDatabase.begins&&!CharacterDatabase.commits&&p.trace.empty());
        assert(!p._pb59Transaction.PendingSave());
        // Core AdditionalSavingAddMask coalesces masks and resets timer.
        p.m_additionalSaveMask|=4;p.m_additionalSaveTimer=2000;
        p.UpdateAdditionalSaves(1000);assert(p.m_additionalSaveTimer==1000);
        p.UpdateAdditionalSaves(1000);assert(p.m_additionalSaveTimer==1000);
        assert(p._pb59Transaction.Finish(t,stable));
        p.onSave=[&]{assert(!p._pb59Transaction.Begin());};
        p.UpdateAdditionalSaves(1000);
        std::vector<unsigned> expected;
        if(mask&1)expected.push_back(1);
        if(mask&2)expected.insert(expected.end(),{2,3,4,5,6});
        expected.insert(expected.end(),{7,8,9});
        assert(p.trace==expected&&CharacterDatabase.begins==1&&CharacterDatabase.commits==1);
        assert(!p.m_additionalSaveMask&&!p.m_additionalSaveTimer);
        p.UpdateAdditionalSaves(1000);assert(CharacterDatabase.commits==1);
    }
    // A callback can enqueue more work; the next tick retains its own flags.
    {Player p;CharacterDatabase={};p.m_additionalSaveMask=1;p.m_additionalSaveTimer=1;
     p.onSave=[&]{assert(!p._pb59Transaction.Begin());p.m_additionalSaveMask|=2;p.m_additionalSaveTimer=2000;};
     p.UpdateAdditionalSaves(1);assert(p.m_additionalSaveMask==2&&p.m_additionalSaveTimer==2000);
     p.UpdateAdditionalSaves(2000);assert(CharacterDatabase.commits==2&&p.trace.size()==6);}
    // Full autosave stays due; successful full save keeps stock mask reset.
    for(bool recovery:{false,true})
    {Player p;p.m_nextSave=5;p.m_additionalSaveTimer=3;p.m_additionalSaveMask=7;
     auto t=p._pb59Transaction.Begin();if(recovery)assert(p._pb59Transaction.RequireRecovery(t));
     p.AutoSaveTick(2);assert(p.m_nextSave==3);
     p.AutoSaveTick(3);p.AutoSaveTick(20);assert(p.m_nextSave==3&&p.trace.empty());
     assert(p.m_additionalSaveMask==7&&p.m_additionalSaveTimer==3);
     assert(p._pb59Transaction.Finish(t,stable));p.onSave=[&]{assert(!p._pb59Transaction.Begin());};
     p.AutoSaveTick(3);assert(p.m_nextSave==1000&&p.trace==std::vector<unsigned>{10});
     assert(!p.m_additionalSaveMask&&!p.m_additionalSaveTimer);}
    {Player p;p.m_nextSave=1;p.farTeleport=true;p.AutoSaveTick(10);
     assert(p.m_nextSave==1&&p.trace.empty());p.farTeleport=false;p.AutoSaveTick(1);
     assert(p.m_nextSave==1000&&p.trace==std::vector<unsigned>{10});}
    {Player p;CharacterDatabase={};p.session.logout=true;p.m_additionalSaveTimer=1;p.m_additionalSaveMask=7;
     p.UpdateAdditionalSaves(100);assert(!CharacterDatabase.begins&&p.m_additionalSaveMask==7&&p.m_additionalSaveTimer==1);}
    {Player p;CharacterDatabase={};auto l=p._pb59Transaction.TryAcquireRetirement();assert(l);
     p.m_additionalSaveTimer=1;p.m_additionalSaveMask=7;p.UpdateAdditionalSaves(1);
     assert(CharacterDatabase.commits==1&&!p._pb59Transaction.CanBegin());}
}
