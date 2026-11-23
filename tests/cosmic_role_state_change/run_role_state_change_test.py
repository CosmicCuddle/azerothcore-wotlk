"""Actual guard, native role setters and dispatcher; explicit engine/script doubles."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
p=argparse.ArgumentParser();p.add_argument("--compiler",nargs="+",required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
def read(path):return (root/path).read_text(encoding="utf-8")
def function(source,signature):
 start=source.index(signature);opening=source.index("{",start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=="{")-(source[end]=="}");end+=1
 return source[start:end]
player=function(read("src/server/game/Entities/Player/Player.cpp"),"void Player::SetGameMaster(")
session=function(read("src/server/game/Server/WorldSession.cpp"),"void WorldSession::SetSecurity(")
world=read("src/server/game/Scripting/ScriptDefines/WorldScript.cpp")
dispatch="\n".join(function(world,"void ScriptMgr::On"+phase+"RoleStateChange(") for phase in ("Before","After"))
enum=re.search(r"enum WorldHook\s*\{[^}]+\};",read("src/server/game/Scripting/ScriptDefines/WorldScript.h")).group()
commands=read("src/server/scripts/Commands/cs_gm.cpp")
command_routes="struct NativeGMCommands {"+"\n".join(function(commands,"static bool HandleGM"+phase+"Command(") for phase in ("On","Off"))+"};"
spells=read("src/server/scripts/Spells/spell_generic.cpp")
spell_routes=""
for name in ("on","off"):
 scoped=spells[spells.index("class spell_gen_gm_"+name+" "):]
 spell_routes+="struct NativeGMSpell_"+name+" { SpellCaster* caster=nullptr; SpellCaster* GetCaster(){return caster;} "+function(scoped,"void HandleScriptEffect(")+"};\n"
fixture=r"""
#include "RoleStateChangeGuard.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>
using uint32=unsigned;
enum AccountTypes {SEC_PLAYER,SEC_GAMEMASTER};
constexpr uint32 PLAYER_EXTRA_GM_ON=1,FACTION_FRIENDLY=35,PLAYER_FLAGS_GM=1,UNIT_FLAG2_ALLOW_CHEAT_SPELLS=1;
constexpr uint32 UNIT_FIELD_BYTES_2=0,UNIT_BYTE2_FLAG_FFA_PVP=1,PHASEMASK_ANYWHERE=0xFFFF,PHASEMASK_NORMAL=1,SERVERSIDE_VISIBILITY_GM=0;
struct WorldSession {AccountTypes _security=SEC_PLAYER;void SetSecurity(AccountTypes);bool IsGMAccount(){return _security!=SEC_PLAYER;}AccountTypes GetSecurity(){return _security;}};
struct Pet {uint32 faction=0;void SetFaction(uint32 n){faction=n;}};
struct World {bool ffa=false;bool IsFFAPvPRealm(){return ffa;}} world;auto sWorld=&world;
std::function<void()> nestedPvp;
struct Player {WorldSession session;Pet pet;uint32 m_ExtraFlags=0,m_areaUpdateId=0,faction=1,phase=0;bool ffa=false,throwVisibility=false;unsigned visibility=0,triggerVisibility=0;
 bool IsGameMaster(){return (m_ExtraFlags&PLAYER_EXTRA_GM_ON)!=0;}void SetGameMaster(bool);WorldSession* GetSession(){return &session;}
 void SetFaction(uint32 n){faction=n;}void SetPlayerFlag(uint32){}void SetUnitFlag2(uint32){}Pet* GetPet(){return &pet;}
 bool HasByteFlag(uint32,uint32,uint32){return ffa;}void RemoveByteFlag(uint32,uint32,uint32){ffa=false;}void SetByteFlag(uint32,uint32,uint32){ffa=true;}
 void ResetContestedPvP(){}void CombatStopWithPets(){}void SetPhaseMask(uint32 n,bool){phase=n;}void SetServerSideVisibilityDetect(uint32,uint32){}
 uint32 GetPhaseByAuras(){return 0;}uint32 getRace(bool){return 1;}void SetFactionForRace(uint32 n){faction=n;}
 void RemovePlayerFlag(uint32){}void RemoveUnitFlag2(uint32){}uint32 GetFaction(){return faction;}void UpdateArea(uint32){}
 void UpdateTriggerVisibility(){++triggerVisibility;}
 void UpdateObjectVisibility(){++visibility;if(throwVisibility)throw std::runtime_error("fixture unwind");}};
constexpr int LANG_GM_ON=1,LANG_GM_OFF=2;
using SpellEffIndex=int;
struct ChatHandler{Player* player;int notification=0;Player* GetPlayer(){return player;}void SendNotification(int n){notification=n;}};
struct SpellCaster{Player* player;Player* ToPlayer(){return player;}};
void ScriptMgr::OnPlayerFfaPvpStateUpdate(Player*,bool){if(nestedPvp)nestedPvp();}
struct WorldScript {std::set<int> hooks;std::function<void()> before,after;
 void OnBeforeRoleStateChange(){if(before)before();}void OnAfterRoleStateChange(){if(after)after();}};
std::vector<WorldScript*> Scripts;
#define CALL_ENABLED_HOOKS(type,hook,expression) do{for(auto* script:Scripts)if(script->hooks.count(hook)){expression;}}while(false)
ScriptMgr Manager;ScriptMgr* sScriptMgr=&Manager;
"""
checks=r"""
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %u at line %d\n",checks,__LINE__);return 1;}}while(false)
int main(){unsigned checks=0,before=0,after=0,depth=0,maxDepth=0;Player player;
 WorldScript observer;observer.hooks={WORLDHOOK_ON_BEFORE_ROLE_STATE_CHANGE,WORLDHOOK_ON_AFTER_ROLE_STATE_CHANGE};
 observer.before=[&](){++before;++depth;maxDepth=std::max(maxDepth,depth);};observer.after=[&](){++after;--depth;};Scripts.push_back(&observer);
 static_assert(!std::is_copy_constructible_v<RoleStateChangeGuard>);
 static_assert(!std::is_copy_assignable_v<RoleStateChangeGuard>);
 player.session.SetSecurity(SEC_PLAYER);CHECK(before==0 && after==0);
 player.session.SetSecurity(SEC_GAMEMASTER);CHECK(before==1 && after==1 && depth==0);CHECK(player.session.GetSecurity()==SEC_GAMEMASTER);
 player.session.SetSecurity(SEC_GAMEMASTER);CHECK(before==1 && after==1);
 unsigned oldVisibility=player.visibility;player.SetGameMaster(false);CHECK(player.visibility==oldVisibility+1);CHECK(before==1);
 bool oldRole=false,newRole=false;
 observer.before=[&](){++before;++depth;maxDepth=std::max(maxDepth,depth);oldRole=player.IsGameMaster();};
 observer.after=[&](){++after;--depth;newRole=player.IsGameMaster();};
 player.SetGameMaster(true);CHECK(!oldRole && newRole);CHECK(player.IsGameMaster());CHECK(before==2 && after==2 && depth==0);CHECK(player.faction==FACTION_FRIENDLY);CHECK(player.phase==PHASEMASK_ANYWHERE);
 player.SetGameMaster(true);CHECK(before==2 && after==2);CHECK(player.IsGameMaster());
 player.SetGameMaster(false);CHECK(before==3 && after==3);CHECK(!player.IsGameMaster());CHECK(player.phase==PHASEMASK_NORMAL);
 observer.before=[&](){++before;++depth;maxDepth=std::max(maxDepth,depth);};observer.after=[&](){++after;--depth;};
 player.ffa=true;nestedPvp=[&](){player.session.SetSecurity(SEC_PLAYER);};player.SetGameMaster(true);nestedPvp={};
 CHECK(maxDepth==2);CHECK(depth==0 && before==after);CHECK(player.session.GetSecurity()==SEC_PLAYER);CHECK(player.IsGameMaster());
 player.throwVisibility=true;unsigned oldBefore=before;bool caught=false;
 try{player.SetGameMaster(false);}catch(std::runtime_error const&){caught=true;}
 CHECK(caught);CHECK(before==oldBefore+1 && after==before && depth==0);CHECK(!player.IsGameMaster());
 oldBefore=before;{RoleStateChangeGuard inactive(false);}CHECK(before==oldBefore && after==before);
 WorldScript disabled;unsigned disabledCalls=0;disabled.before=[&](){++disabledCalls;};Scripts.push_back(&disabled);
 player.session.SetSecurity(SEC_GAMEMASTER);CHECK(disabledCalls==0);CHECK(before==after && depth==0);
 player.throwVisibility=false;
 if(player.IsGameMaster())player.SetGameMaster(false);
 ChatHandler handler{&player};oldBefore=before;unsigned oldTrigger=player.triggerVisibility;
 CHECK(NativeGMCommands::HandleGMOnCommand(&handler));CHECK(player.IsGameMaster());CHECK(handler.notification==LANG_GM_ON);
 CHECK(before==oldBefore+1 && after==before);CHECK(player.triggerVisibility==oldTrigger+1);
 CHECK(NativeGMCommands::HandleGMOffCommand(&handler));CHECK(!player.IsGameMaster());CHECK(handler.notification==LANG_GM_OFF);CHECK(before==oldBefore+2 && after==before);
 SpellCaster caster{&player};NativeGMSpell_on on;on.caster=&caster;NativeGMSpell_off off;off.caster=&caster;
 oldBefore=before;on.HandleScriptEffect(0);CHECK(player.IsGameMaster());CHECK(before==oldBefore+1 && after==before);
 off.HandleScriptEffect(0);CHECK(!player.IsGameMaster());CHECK(before==oldBefore+2 && after==before);
 caster.player=nullptr;oldBefore=before;on.HandleScriptEffect(0);off.HandleScriptEffect(0);CHECK(before==oldBefore && after==before);CHECK(depth==0);
 std::printf("%u actual role guard/setter/dispatch checks PASS\n",checks);
}
"""
with tempfile.TemporaryDirectory() as tmp:
 r=Path(tmp);(r/"ScriptMgr.h").write_text("#pragma once\nstruct Player;class ScriptMgr{public:void OnBeforeRoleStateChange();void OnAfterRoleStateChange();void OnPlayerFfaPvpStateUpdate(Player*,bool);};extern ScriptMgr* sScriptMgr;\n")
 (r/"RoleStateChangeGuard.h").write_text(read("src/server/game/Scripting/RoleStateChangeGuard.h"),encoding="utf-8")
 cpp=r/"role.cpp";cpp.write_text(fixture+enum+dispatch+player+session+command_routes+spell_routes+checks,encoding="utf-8")
 exe=r/("role.exe" if __import__("os").name=="nt" else "role")
 subprocess.run(a.compiler+["-std=c++20","-Wall","-Wextra","-Werror","-I",str(r),str(cpp),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
