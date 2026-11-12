"""Core24 upstream restack: current semantic preservation gate.

This test is deliberately NOT an old exact-scope child guard. It validates the
current merged source after the real Grim-upstream + Core23 three-way restack.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = "afd7036bab385e96a2c540272680cc0ec2f7dd4d"
CORE23 = "dbb1dd5150f330d88522a10b85e93546fff72bad"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

def read(path):
    return (ROOT / path).read_text()

def ancestor(commit):
    return subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"],
        cwd=ROOT, check=False
    ).returncode == 0

# The candidate must genuinely contain BOTH histories.
require(ancestor(UPSTREAM), "current Grim upstream is not an ancestor")
require(ancestor(CORE23), "protected Core23 is not an ancestor")
subprocess.run(["git", "diff", "--check", UPSTREAM, "HEAD"], cwd=ROOT, check=True)

overlap_paths = (
    "src/server/game/Handlers/CharacterHandler.cpp",
    "src/server/game/Server/WorldSession.cpp",
    "src/server/game/Scripting/ScriptMgr.h",
    "src/server/game/Grids/Notifiers/GridNotifiers.h",
)
for path in overlap_paths:
    src = read(path)
    require("<<<<<<<" not in src and ">>>>>>>" not in src,
            "merge conflict marker remains in " + path)

# Core23 reconnect hook + upstream taxi/LFG locale corrections.
ch = read("src/server/game/Handlers/CharacterHandler.cpp")
reconnect = ch.split("if (!sWorld->getBoolConfig(CONFIG_ENABLE_LOGIN_AFTER_DC))", 1)[1].split(
    "std::shared_ptr<LoginQueryHolder>", 1)[0]
for needle in (
    "p->SetSession(this);",
    "sScriptMgr->OnF14PlayerSessionRebound(p);",
    "delete p->PlayerTalkClass;",
    "HandlePlayerLoginToCharInWorld(p);",
):
    require(needle in reconnect, "reconnect anchor missing: " + needle)
require(reconnect.index("p->SetSession(this);") <
        reconnect.index("sScriptMgr->OnF14PlayerSessionRebound(p);") <
        reconnect.index("delete p->PlayerTalkClass;") <
        reconnect.index("HandlePlayerLoginToCharInWorld(p);"),
        "reconnect lifecycle ordering changed")
for needle in (
    "TaxiPath.dbc has a path from node 0",
    "if (!nodeId || nodeId > TaxiMaskSize * 32)",
    "LocaleConstant dbcLocale = GetSessionDbcLocale();",
    "GetZoneLFGDungeonEntry(zone->area_name[dbcLocale], dbcLocale)",
):
    require(needle in ch, "upstream CharacterHandler correction missing: " + needle)

# Core20 kept-in-world disconnect hook + upstream session/gossip locale sync.
ws = read("src/server/game/Server/WorldSession.cpp")
body = ws.split("bool WorldSession::HandleSocketClosed()", 1)[1].split(
    "bool WorldSession::IsSocketClosed()", 1)[0]
for needle in (
    "m_Socket = nullptr;",
    "sScriptMgr->OnF14PlayerSessionDisconnected(GetPlayer());",
    "GetPlayer()->TradeCancel(false);",
):
    require(needle in body, "disconnect anchor missing: " + needle)
require(body.index("m_Socket = nullptr;") <
        body.index("OnF14PlayerSessionDisconnected") <
        body.index("GetPlayer()->TradeCancel(false);"),
        "disconnect lifecycle ordering changed")
for needle in (
    '#include "GossipDef.h"',
    "void WorldSession::SetSessionDbLocaleIndex(LocaleConstant locale)",
    "GetGossipMenu().SetLocale(m_sessionDbLocaleIndex);",
):
    require(needle in ws, "upstream WorldSession locale correction missing: " + needle)

# ScriptMgr must retain both upstream APIs and guarded/default-safe F14 APIs.
sm = read("src/server/game/Scripting/ScriptMgr.h")
for needle in (
    "bool CanLootOtherFactionItem(Player const* player, ItemTemplate const* proto, ObjectGuid source);",
    "static void Unload()",
    "ShouldF14SuppressCreatureInitialOutForViewer",
    "ShouldF14SuppressCreatureInitialCreateForViewer",
    "OnF14PlayerSessionRebound",
    "OnF14PlayerSessionDisconnected",
):
    require(needle in sm, "ScriptMgr merged contract missing: " + needle)

# VisibleNotifier remains the sole guarded pass-token owner while the upstream
# Classic centre-to-centre assist-distance correction also survives.
grid = read("src/server/game/Grids/Notifiers/GridNotifiers.h")
for needle in (
    "uint64 _f14PassToken = 0;",
    "VisibleNotifier(VisibleNotifier const&) = delete;",
    "VisibleNotifier& operator=(VisibleNotifier const&) = delete;",
    "BeginF14NotifierPass(); // before any near/far and zone-wide scan.",
    "centre to centre: Classic testing shows no combat reach added",
    "IsWithinDistInMap(u, i_range, true, false, false)",
):
    require(needle in grid, "GridNotifiers merged contract missing: " + needle)

# Current token-bound OUT + CREATE remain fail-visible/default-FALSE.
woc = read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp")
for name in (
    "ShouldF14SuppressCreatureInitialOutForViewer",
    "ShouldF14SuppressCreatureInitialCreateForViewer",
):
    fn = woc.split("bool ScriptMgr::" + name + "(", 1)[1].split("#endif", 1)[0]
    require("if (!viewer || !creature || !token)" in fn and "return false;" in fn,
            name + " no longer fails visible on malformed provenance")
    require("ASSERT(viewer && creature && token)" not in fn,
            name + " reintroduced malformed-input ASSERT")
    require("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE" in fn,
            name + " lost default-FALSE dispatch")

woh = read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.h")
for needle in (
    "OnF14ShouldSuppressCreatureInitialOutForViewer",
    "OnF14ShouldSuppressCreatureInitialCreateForViewer",
    "WorldObject const* /*creature*/) { return false; }",
):
    require(needle in woh, "WorldObjectScript default-safe ABI missing: " + needle)

# Batched CREATE/OUT may use the notifier token; direct visibility may not.
player = read("src/server/game/Entities/Player/PlayerUpdates.cpp")
batched = player.split(
    "template <class T>\nvoid Player::UpdateVisibilityOf(T* target, UpdateData& data", 1
)[1].split("void Player::GetInitialVisiblePackets", 1)[0]
for needle in (
    "IsF14CreatureSuppressedFromClientViewForInitialOut(",
    "IsF14CreatureSuppressedFromClientViewForInitialCreate(",
    "data.F14InitialOutQueryToken()",
    "data.F14InitialCreateQueryToken()",
):
    require(needle in batched, "batched token contract missing: " + needle)
direct = player.split("void Player::UpdateVisibilityOf(WorldObject* target)", 1)[1].split(
    "void Player::UpdateTriggerVisibility()", 1)[0]
for forbidden in (
    "IsF14CreatureSuppressedFromClientViewForInitialOut(",
    "IsF14CreatureSuppressedFromClientViewForInitialCreate(",
    "F14InitialOutQueryToken",
    "F14InitialCreateQueryToken",
):
    require(forbidden not in direct, "direct visibility borrowed notifier authority: " + forbidden)

ud = read("src/server/game/Entities/Object/Updates/UpdateData.h")
require("F14InitialOutQueryToken() const" in ud, "OUT token accessor missing")
require("F14InitialCreateQueryToken() const" in ud, "CREATE token accessor missing")
create_piece = ud.split("F14InitialCreateQueryToken() const", 1)[1].split("#endif", 1)[0]
require("_f14InitialOutAdmission.Token()" in create_piece,
        "CREATE no longer derives from the same notifier admission")

# Pre-map-removal observer remains before removal side effects.
mapcpp = read("src/server/game/Maps/Map.cpp")
remove = mapcpp.split("void Map::RemovePlayerFromMap(Player* player, bool remove)", 1)[1].split(
    "void Map::AfterPlayerUnlinkFromMap", 1)[0]
hook = "sScriptMgr->OnF14BeforePlayerMapRemoval(this, player, remove);"
require(hook in remove, "pre-map-removal observer missing")
require(remove.index(hook) < remove.index("UpdatePlayerZoneStats") <
        remove.index("player->RemoveFromWorld()"),
        "pre-map-removal observer moved after teardown began")

print("PASS Core24 restack contains exact upstream + Core23 ancestry and preserves current F14/upstream semantic seams")
