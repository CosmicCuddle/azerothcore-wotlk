#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
guild = (ROOT / "src/server/game/Guilds/Guild.cpp").read_text()
hdr = (ROOT / "src/server/game/Scripting/ScriptDefines/GuildScript.h").read_text()
impl = (ROOT / "src/server/game/Scripting/ScriptDefines/GuildScript.cpp").read_text()
mgr = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()

assert "#define COSMIC_F14_GUILD_POLICY_HOOK_VERSION 2" in hdr
assert "GUILDHOOK_CAN_ADD_MEMBER" in hdr
assert "GUILDHOOK_CAN_SEND_CHAT_TO_MEMBER" in hdr
assert "virtual bool CanAddMember(Guild const* /*guild*/, uint32 /*guildId*/, ObjectGuid /*guid*/)" in hdr
assert "virtual bool CanSendChatToMember" in hdr
assert "ScriptMgr::CanGuildAddMember(Guild const* guild, uint32 guildId, ObjectGuid guid)" in impl
assert "script->CanAddMember(guild, guildId, guid)" in impl
assert "ScriptMgr::CanGuildSendChatToMember" in impl
assert "bool CanGuildAddMember(Guild const* guild, uint32 guildId, ObjectGuid guid)" in mgr
assert "bool CanGuildSendChatToMember" in mgr

add = guild.index("bool Guild::AddMember")
veto = guild.index("sScriptMgr->CanGuildAddMember(this, m_id, guid)", add)
petition = guild.index("Player::RemovePetitionsAndSigns", add)
emplace = guild.index("m_members.try_emplace", add)
save = guild.index("member.SaveToDB", add)
broadcast = guild.index("_BroadcastEvent(GE_JOINED", add)
assert veto < petition < emplace < save < broadcast

broadcast_fn = guild.index("void Guild::BroadcastToGuild")
recipient = guild.index("sScriptMgr->CanGuildSendChatToMember", broadcast_fn)
send = guild.index("player->SendDirectMessage(&data)", broadcast_fn)
assert recipient < send
assert "session->GetPlayer(), player, officerOnly, language, msg" in guild[recipient:send]

for needle in ("RandomBot", "Blood Elf", "Draenei", "progression", "race 10", "race 11"):
    assert needle not in hdr
print("F14 guild policy hook source contract PASS")
