"""Rewarded-quest observer contracts; source evidence, not a server build."""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = "21ff1973bc75216f8a0d0f3d7731eb3e8894ff97"
HEADER = "src/server/game/Scripting/ScriptDefines/PlayerScript.h"
DISPATCH = "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp"
QUEST = "src/server/game/Entities/Player/PlayerQuest.cpp"


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


def parent(name):
    return subprocess.run(["git", "show", BASE + ":" + name], cwd=ROOT,
                          check=True, capture_output=True).stdout.decode("utf-8")


def function(source, signature):
    start = source.index(signature)
    end, depth = source.index("{", start) + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RewardedQuestChange(unittest.TestCase):
    def test_existing_hook_ids_stay_in_order(self):
        def hooks(source):
            enum = source.split("enum PlayerHook", 1)[1].split("};", 1)[0]
            return re.findall(r"\bPLAYERHOOK_[A-Z0-9_]+\b", enum)
        original = hooks(parent(HEADER))
        current = hooks(read(HEADER))
        self.assertEqual(current, original[:-1] + ["PLAYERHOOK_ON_BEFORE_REWARDED_QUEST_CHANGE",
                                                  "PLAYERHOOK_ON_AFTER_REWARDED_QUEST_CHANGE", original[-1]])

    def test_version_and_void_dispatch_abi(self):
        self.assertEqual(read(HEADER).count("#define COSMIC_REWARDED_QUEST_CHANGE_HOOK_VERSION 1"), 1)
        for phase in ("Before", "After"):
            name = "OnPlayer" + phase + "RewardedQuestChange"
            self.assertIn("virtual void " + name + "(Player* /*player*/, uint32 /*questId*/) { }", read(HEADER))
            self.assertIn("void " + name + "(Player* player, uint32 questId);", read("src/server/game/Scripting/ScriptMgr.h"))
            dispatch = function(read(DISPATCH), "void ScriptMgr::" + name + "(")
            self.assertIn("PLAYERHOOK_ON_" + phase.upper() + "_REWARDED_QUEST_CHANGE", dispatch)
            self.assertIn("script->" + name + "(player, questId)", dispatch)

    def test_set_brackets_storage_and_keeps_save_flag(self):
        body = function(read(QUEST), "void Player::SetRewardedQuest(")
        self.assertIn("m_RewardedQuests.find(quest_id) == m_RewardedQuests.end()", body)
        self.assertEqual(body.count("if (changed)"), 2)
        self.assertLess(body.index("OnPlayerBeforeRewardedQuestChange"), body.index("m_RewardedQuests.insert"))
        self.assertLess(body.index("m_RewardedQuestsSave[quest_id] = true"), body.index("OnPlayerAfterRewardedQuestChange"))

    def test_remove_avoids_iterator_after_observer(self):
        body = function(read(QUEST), "void Player::RemoveRewardedQuest(")
        self.assertIn("if (rewItr != m_RewardedQuests.end())", body)
        self.assertLess(body.index("OnPlayerBeforeRewardedQuestChange"), body.index("m_RewardedQuests.erase(questId)"))
        self.assertLess(body.index("m_RewardedQuestsSave[questId] = false"), body.index("OnPlayerAfterRewardedQuestChange"))
        self.assertNotIn("erase(rewItr)", body)
        self.assertIn("if (update)\n        SendQuestUpdate(questId);", body)

    def test_only_projection_bodies_and_guard_change(self):
        current, old = read(QUEST), parent(QUEST)
        for signature in ("void Player::SetRewardedQuest(", "void Player::RemoveRewardedQuest(",
                          "void Player::AddQuest(", "void Player::RewardQuest(",
                          "void Player::SetQuestStatus(", "void Player::RemoveActiveQuest("):
            current = current.replace(function(current, signature), function(old, signature))
        current = current.replace(function(current, "namespace\n{") + "\n\n", "", 1)
        self.assertEqual(current, old)

    def test_active_projection_and_outer_reward_are_bracketed(self):
        source = read(QUEST)
        for signature in ("void Player::AddQuest(", "void Player::RewardQuest(",
                          "void Player::SetQuestStatus(", "void Player::RemoveActiveQuest("):
            body = function(source, signature)
            self.assertIn("RewardedQuestChangeGuard const mutation(", body)
        remove = function(source, "void Player::RemoveActiveQuest(")
        self.assertIn("m_QuestStatus.erase(questId);", remove)
        self.assertNotIn("m_QuestStatus.erase(itr);", remove)
        guard = function(source, "class RewardedQuestChangeGuard")
        self.assertIn("~RewardedQuestChangeGuard()", guard)
        self.assertIn("OnPlayerBeforeRewardedQuestChange(_player, _questId)", guard)
        self.assertIn("OnPlayerAfterRewardedQuestChange(_player, _questId)", guard)

    def test_shared_callers_and_prelogin_load_are_preserved(self):
        self.assertIn("SetRewardedQuest(quest_id);", function(read(QUEST), "void Player::RewardQuest("))
        self.assertIn("player->SetRewardedQuest(questId);", read("src/server/game/Spells/SpellEffects.cpp"))
        storage = "src/server/game/Entities/Player/PlayerStorage.cpp"
        self.assertEqual(read(storage), parent(storage))
        self.assertIn("m_RewardedQuests.insert(quest_id);", function(read(storage), "void Player::_LoadQuestStatusRewarded("))

    def test_observers_are_policy_neutral(self):
        added = subprocess.run(["git", "diff", "-U0", BASE, "--", QUEST, HEADER, DISPATCH,
                                "src/server/game/Scripting/ScriptMgr.h"], cwd=ROOT,
                               check=True, capture_output=True, text=True).stdout
        added = "\n".join(line[1:] for line in added.splitlines() if line.startswith("+") and not line.startswith("+++"))
        for forbidden in ("Database", "RACE_BLOODELF", "PROGRESSION_", "GetOption", "SetPhaseMask", "SetVisibility"):
            self.assertNotIn(forbidden, added)


if __name__ == "__main__":
    unittest.main()
