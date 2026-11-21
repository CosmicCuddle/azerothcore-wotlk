"""Compile actual Core quest writers/dispatch with explicit container/script doubles.

Not a native Worldserver link or in-game test.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    end, depth = source.index("{", start) + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PRELUDE = r'''
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>
using uint32 = std::uint32_t;
using RewardedQuestSet = std::unordered_set<uint32>;
enum QuestStatus { QUEST_STATUS_NONE, QUEST_STATUS_COMPLETE, QUEST_STATUS_INCOMPLETE, QUEST_STATUS_REWARDED };
struct QuestStatusData { QuestStatus Status = QUEST_STATUS_NONE; };
using QuestStatusMap = std::map<uint32, QuestStatusData>;
struct Quest {
    bool GetQuestMethod() const { return true; }
    bool IsAutoComplete() const { return false; }
    bool IsSeasonal() const { return false; }
    bool IsRepeatable() const { return false; }
};
struct ObjectMgr {
    Quest quest;
    Quest const* GetQuestTemplate(uint32 id) const { return id ? &quest : nullptr; }
};
static ObjectMgr Objects;
static ObjectMgr* sObjectMgr = &Objects;
constexpr int ADDITIONAL_SAVING_QUEST_STATUS = 1;
struct Player {
    RewardedQuestSet m_RewardedQuests;
    std::map<uint32, bool> m_RewardedQuestsSave;
    unsigned packets = 0;
    QuestStatusMap m_QuestStatus;
    std::map<uint32, bool> m_QuestStatusSave;
    unsigned auraUpdates = 0, saveMasks = 0;
    void SetRewardedQuest(uint32 quest_id);
    void RemoveRewardedQuest(uint32 questId, bool update = true);
    void SetQuestStatus(uint32, QuestStatus, bool update = true);
    void RemoveActiveQuest(uint32, bool update = true);
    QuestStatus GetQuestStatus(uint32) const;
    bool IsQuestRewarded(uint32 id) const { return m_RewardedQuests.count(id) != 0; }
    bool SatisfyQuestSeasonal(Quest const*, bool) const { return true; }
    uint32 GetZoneId() const { return 0; }
    uint32 GetAreaId() const { return 0; }
    void UpdateZoneDependentAuras(uint32) { ++auraUpdates; }
    void UpdateAreaDependentAuras(uint32) { ++auraUpdates; }
    void AdditionalSavingAddMask(int) { ++saveMasks; }
    void SendQuestUpdate(uint32) { ++packets; }
};
enum Hook { PLAYERHOOK_ON_BEFORE_REWARDED_QUEST_CHANGE, PLAYERHOOK_ON_AFTER_REWARDED_QUEST_CHANGE };
struct PlayerScript {
    bool enabled = true;
    virtual ~PlayerScript() = default;
'''
SUPPORT = r'''
};
static std::vector<PlayerScript*> Scripts;
#define CALL_ENABLED_HOOKS(type, hook, expression) do { for (auto* script : Scripts) if (script->enabled) { expression; } } while (false)
struct ScriptMgr {
    void OnPlayerBeforeRewardedQuestChange(Player*, uint32);
    void OnPlayerAfterRewardedQuestChange(Player*, uint32);
};
static ScriptMgr Manager;
static ScriptMgr* sScriptMgr = &Manager;
struct Event { bool before, present, dirty; QuestStatus projected; };
struct Recorder : PlayerScript {
    std::vector<Event> events;
    std::function<void(Player*, uint32)> nested;
    int depth = 0, maxDepth = 0;
    void OnPlayerBeforeRewardedQuestChange(Player* player, uint32 id) override {
        events.push_back({true, player->m_RewardedQuests.count(id) != 0, player->m_RewardedQuestsSave[id], player->GetQuestStatus(id)});
        ++depth;
        if (depth > maxDepth) maxDepth = depth;
        if (nested) {
            auto call = std::move(nested);
            nested = {};
            call(player, id);
        }
    }
    void OnPlayerAfterRewardedQuestChange(Player* player, uint32 id) override {
        events.push_back({false, player->m_RewardedQuests.count(id) != 0, player->m_RewardedQuestsSave[id], player->GetQuestStatus(id)});
        --depth;
    }
};
'''
TEST = r'''
#define CHECK(value) do { ++checks; if (!(value)) { std::printf("FAIL line %d\n", __LINE__); return 1; } } while (0)
int main() {
    unsigned checks = 0;
    Player player;
    player.SetRewardedQuest(66008); // no consumers: existing behavior
    CHECK(player.m_RewardedQuests.count(66008) == 1);
    CHECK(player.m_RewardedQuestsSave[66008]);
    player.RemoveRewardedQuest(66008);
    CHECK(player.m_RewardedQuests.empty());
    CHECK(!player.m_RewardedQuestsSave[66008] && player.packets == 1);
    Recorder recorder;
    Scripts.push_back(&recorder);
    player.SetRewardedQuest(66008);
    CHECK(recorder.events.size() == 2 && recorder.depth == 0);
    CHECK(recorder.events[0].before && !recorder.events[0].present);
    CHECK(!recorder.events[1].before && recorder.events[1].present && recorder.events[1].dirty);
    player.m_RewardedQuestsSave[66008] = false;
    player.SetRewardedQuest(66008); // duplicate: no pair, keep save semantics
    CHECK(recorder.events.size() == 2 && player.m_RewardedQuestsSave[66008]);
    player.RemoveRewardedQuest(66008, false);
    CHECK(recorder.events.size() == 4 && recorder.depth == 0);
    CHECK(recorder.events[2].before && recorder.events[2].present && recorder.events[2].dirty);
    CHECK(!recorder.events[3].before && !recorder.events[3].present && !recorder.events[3].dirty);
    CHECK(player.packets == 1);
    player.RemoveRewardedQuest(66008, false);
    CHECK(recorder.events.size() == 4 && player.packets == 1);
    player.RemoveRewardedQuest(66008, true);
    CHECK(recorder.events.size() == 4 && player.packets == 2);
    recorder.nested = [](Player* p, uint32 id) { p->SetRewardedQuest(id); };
    player.SetRewardedQuest(66013);
    CHECK(recorder.depth == 0 && recorder.maxDepth == 2);
    CHECK(player.m_RewardedQuests.count(66013) == 1);
    CHECK(recorder.events.size() == 8);
    recorder.nested = [](Player* p, uint32 id) { p->RemoveRewardedQuest(id, false); };
    player.RemoveRewardedQuest(66013, false); // old iterator would have dangled after the nested erase
    CHECK(player.m_RewardedQuests.count(66013) == 0 && recorder.depth == 0);
    CHECK(recorder.events.size() == 12);
    player.SetRewardedQuest(123); // Core hook is generic, not an F14 policy
    CHECK(player.m_RewardedQuests.count(123) == 1 && recorder.events.size() == 14);
    recorder.enabled = false;
    player.RemoveRewardedQuest(123, false);
    CHECK(recorder.events.size() == 14 && player.m_RewardedQuests.count(123) == 0);
    recorder.enabled = true;
    player.SetRewardedQuest(66015);
    auto projectedStart = recorder.events.size();
    player.SetQuestStatus(66015, QUEST_STATUS_INCOMPLETE, false);
    CHECK(recorder.events.size() == projectedStart + 2);
    CHECK(recorder.events[projectedStart].projected == QUEST_STATUS_REWARDED);
    CHECK(recorder.events.back().projected == QUEST_STATUS_INCOMPLETE);
    CHECK(player.IsQuestRewarded(66015)); // active entry shadows, but does not erase, earned storage
    projectedStart = recorder.events.size();
    player.SetQuestStatus(66015, QUEST_STATUS_INCOMPLETE, false);
    CHECK(recorder.events.size() == projectedStart); // no-op status retains original save flag
    CHECK(player.m_QuestStatusSave[66015]);
    player.RemoveActiveQuest(66015, false);
    CHECK(recorder.events.size() == projectedStart + 2);
    CHECK(player.GetQuestStatus(66015) == QUEST_STATUS_REWARDED);
    CHECK(player.auraUpdates == 2 && player.saveMasks == 1);
    auto const packetsBefore = player.packets;
    projectedStart = recorder.events.size();
    player.RemoveActiveQuest(66015, false);
    CHECK(recorder.events.size() == projectedStart && player.packets == packetsBefore);
    CHECK(player.auraUpdates == 4 && player.saveMasks == 2); // preserve original no-op side effects
    {
        RewardedQuestChangeGuard const outer(&player, 66015);
        player.RemoveRewardedQuest(66015, false);
        CHECK(recorder.depth == 1); // inner pair must not release the outer scope
    }
    CHECK(recorder.depth == 0);
    try {
        RewardedQuestChangeGuard const interrupted(&player, 66015);
        throw 1;
    } catch (int) { }
    CHECK(recorder.depth == 0); // guard also balances exception unwinding
    std::printf("CORE ACTUAL REWARDED QUEST WRITERS %u/%u passed (container/script doubles)\n", checks, checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", nargs="+", default=["clang++"])
    parser.add_argument("--std", choices=("c++17", "c++20"), default="c++20")
    parser.add_argument("--ndebug", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    read = lambda path: (root / path).read_text(encoding="utf-8")
    header = read("src/server/game/Scripting/ScriptDefines/PlayerScript.h")
    methods = []
    dispatch = []
    for phase in ("Before", "After"):
        name = "OnPlayer" + phase + "RewardedQuestChange"
        methods.append(re.search(r"    virtual void " + name + r"[^\n]+", header).group())
        dispatch.append(function(read("src/server/game/Scripting/ScriptDefines/PlayerScript.cpp"), "void ScriptMgr::" + name + "("))
    quests = read("src/server/game/Entities/Player/PlayerQuest.cpp")
    guard = function(quests, "class RewardedQuestChangeGuard") + ";\n"
    writers = "\n".join(function(quests, sig) for sig in ("void Player::SetRewardedQuest(", "void Player::RemoveRewardedQuest(",
                                                        "void Player::SetQuestStatus(", "void Player::RemoveActiveQuest(",
                                                        "QuestStatus Player::GetQuestStatus("))
    with tempfile.TemporaryDirectory(prefix="core-quest-change-") as tmp:
        source, binary = Path(tmp) / "quest-change.cpp", Path(tmp) / "quest-change.exe"
        source.write_text(PRELUDE + "\n".join(methods) + SUPPORT + "\n".join(dispatch) + guard + writers + TEST, encoding="utf-8")
        command = args.compiler + ["-std=" + args.std, "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2"]
        if args.ndebug:
            command.append("-DNDEBUG")
        subprocess.run(command + [str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
