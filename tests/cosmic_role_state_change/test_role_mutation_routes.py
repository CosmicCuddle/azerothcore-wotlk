"""Inventory current Core role writes; out-of-tree modules/DB/runtime not inferred."""
import pathlib
import re
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
def read(path):return (ROOT/path).read_text(encoding="utf-8")
def code(text):return re.sub(r"/\*.*?\*/|//[^\n]*","",text,flags=re.S)

class MutationRoutes(unittest.TestCase):
 def test_security_value_assignment_is_confined_to_setter(self):
  found=[]
  for path in (ROOT/"src/server/game").rglob("*"):
   if path.suffix not in (".cpp",".h"):continue
   for match in re.finditer(r"\b_security\s*=(?!=)\s*[^;]+;",code(path.read_text(encoding="utf-8"))):
    found.append((path.relative_to(ROOT).as_posix(),re.sub(r"\s+","",match.group())))
  self.assertEqual(found,[("src/server/game/Server/WorldSession.cpp","_security=security;")])
 def test_gm_bit_writes_are_native_setter_or_known_hydration(self):
  found=[]
  for path in (ROOT/"src/server/game/Entities/Player").glob("*"):
   if path.suffix not in (".cpp",".h"):continue
   for match in re.finditer(r"\bm_ExtraFlags\s*(?:[|&^]=|=(?!=))[^;]+;",code(path.read_text(encoding="utf-8"))):
    statement=re.sub(r"\s+","",match.group())
    if "PLAYER_EXTRA_GM_ON" in statement or statement.startswith("m_ExtraFlags="):
     found.append((path.name,statement))
  self.assertCountEqual(found,[("Player.cpp","m_ExtraFlags=0;"),("Player.cpp","m_ExtraFlags|=PLAYER_EXTRA_GM_ON;"),("Player.cpp","m_ExtraFlags&=~PLAYER_EXTRA_GM_ON;"),("PlayerStorage.cpp","m_ExtraFlags=extraflags;")])
 def test_native_login_allocates_and_loads_before_world_publication(self):
  source=read("src/server/game/Handlers/CharacterHandler.cpp").split("void WorldSession::HandlePlayerLoginFromDB(",1)[1]
  self.assertLess(source.index("new Player(this)"),source.index("pCurrChar->LoadFromDB("))
  self.assertLess(source.index("pCurrChar->LoadFromDB("),source.index("AddPlayerToMap(pCurrChar)"))
  self.assertLess(source.index("AddPlayerToMap(pCurrChar)"),source.index("OnPlayerLogin(pCurrChar)"))
 def test_commands_and_spell_effects_route_through_gm_setter(self):
  command=read("src/server/scripts/Commands/cs_gm.cpp")
  for phase,value in (("On","true"),("Off","false")):
   body=command.split("static bool HandleGM"+phase+"Command(",1)[1].split("\n    }",1)[0]
   self.assertIn("->SetGameMaster("+value+")",body)
   self.assertIn("UpdateTriggerVisibility()",body)
  spells=read("src/server/scripts/Spells/spell_generic.cpp")
  for phase,value in (("on","true"),("off","false")):
   body=spells.split("class spell_gen_gm_"+phase+" ",1)[1].split("\n};",1)[0]
   self.assertIn("player->SetGameMaster("+value+")",body)
   self.assertIn("RegisterSpellScript(spell_gen_gm_"+phase+")",spells)
 def test_account_access_and_headless_construction_are_distinct_contracts(self):
  account=read("src/server/game/Accounts/AccountMgr.cpp").split("void AccountMgr::UpdateAccountAccess(",1)[1].split("\n}",1)[0]
  self.assertIn("rbac->SetSecurityLevel(securityLevel)",account)
  self.assertIn("LoginDatabase.CommitTransaction(trans)",account)
  self.assertNotIn("WorldSession",account);self.assertNotIn("_security",account)
  self.assertIn("_headless(!sock)",read("src/server/game/Server/WorldSession.cpp"))
