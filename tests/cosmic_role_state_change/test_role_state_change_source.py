import pathlib
import re
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
PARENT="c6e3bf04d2e49fd30afcb8b0e3416fca50287aab"
def read(path):return (ROOT/path).read_text(encoding="utf-8")

class RoleContract(unittest.TestCase):
 def test_world_hook_ids_append_without_renumbering(self):
  path="src/server/game/Scripting/ScriptDefines/WorldScript.h"
  old=subprocess.check_output(["git","show",PARENT+":"+path],cwd=ROOT).decode()
  names=lambda s: re.findall(r"\bWORLDHOOK_[A-Z0-9_]+\b",s.split("enum WorldHook",1)[1].split("};",1)[0])
  prior=names(old);current=names(read(path))
  self.assertEqual(current[:-3],prior[:-1]);self.assertEqual(current[-3:],["WORLDHOOK_ON_BEFORE_ROLE_STATE_CHANGE","WORLDHOOK_ON_AFTER_ROLE_STATE_CHANGE","WORLDHOOK_END"])
 def test_setters_bracket_before_their_writes(self):
  gm=read("src/server/game/Entities/Player/Player.cpp").split("void Player::SetGameMaster(bool on)",1)[1].split("void Player::SetGMVisible",1)[0]
  self.assertLess(gm.index("RoleStateChangeGuard"),gm.index("m_ExtraFlags |= PLAYER_EXTRA_GM_ON"))
  self.assertIn("roleChange(IsGameMaster() != on)",gm)
  session=read("src/server/game/Server/WorldSession.cpp").split("void WorldSession::SetSecurity(",1)[1].split("void WorldSession::UpdateAccountFlag",1)[0]
  self.assertLess(session.index("RoleStateChangeGuard"),session.index("_security = security"))
  self.assertIn("if (_security == security)",session)
  self.assertNotIn("SetSecurity(AccountTypes security) {",read("src/server/game/Server/WorldSession.h"))
 def test_observer_contract_is_pointer_free_non_vetoing(self):
  header=read("src/server/game/Scripting/ScriptDefines/WorldScript.h")
  self.assertIn("#define COSMIC_ROLE_STATE_CHANGE_HOOK_VERSION 1",header)
  for phase in ("Before","After"):
   self.assertIn("virtual void On"+phase+"RoleStateChange()",header)
   self.assertIn("script->On"+phase+"RoleStateChange()",read("src/server/game/Scripting/ScriptDefines/WorldScript.cpp"))
 def test_guard_is_paired_noncopyable_and_does_not_retain_actor(self):
  guard=read("src/server/game/Scripting/RoleStateChangeGuard.h")
  self.assertIn("~RoleStateChangeGuard()",guard);self.assertEqual(guard.count("= delete;"),2)
  self.assertNotRegex(guard,r"(?:Player|WorldSession)\s*\*")
