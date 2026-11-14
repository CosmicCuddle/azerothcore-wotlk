/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Player.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "CharacterDatabase.h"

/*********************************************************/
/***              PLAYER SETTINGS SYSTEM               ***/
/*********************************************************/

namespace PlayerSettingsStore
{
    // Common helper: parse space-separated data string into PlayerSettingVector
    PlayerSettingVector ParseSettingsData(std::string const& data)
    {
        PlayerSettingVector result;
        std::vector<std::string_view> tokens = Acore::Tokenize(data, ' ', false);
        result.reserve(tokens.size());
        for (auto const& token : tokens)
        {
            if (token.empty())
                continue;
            if (auto parsed = Acore::StringTo<uint32>(token))
                result.emplace_back(*parsed);
        }
        return result;
    }

    // Common helper: serialize PlayerSettingVector to space-separated string
    std::string SerializeSettingsData(PlayerSettingVector const& settings)
    {
        if (settings.empty())
            return "";

        std::ostringstream data;
        data << settings[0].value << ' ';
        for (size_t i = 1; i < settings.size(); ++i)
            data << settings[i].value << ' ';
        return data.str();
    }

    // Avoid Core parser dropping malformed values and shifting slots into
    // apparently valid paid entitlements after the next normal character save.
    // Every unrelated settings source keeps the old permissive behaviour.
    static bool IsCanonicalPaidReceiptRow(std::string const& source, std::string const& data)
    {
        if (source != "ip_paid_expansion")
            return true;
        PlayerSettingVector const parsed = ParseSettingsData(data);
        return !parsed.empty() && SerializeSettingsData(parsed) == data;
    }

    // helper: load a single source row for a player and parse to vector
    static PlayerSettingVector LoadPlayerSettings(ObjectGuid::LowType playerLowGuid, std::string const& source, bool& unsafePaidRow)
    {
        PlayerSettingVector result;
        unsafePaidRow = false;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_SETTINGS);
        stmt->SetData(0, playerLowGuid);
        PreparedQueryResult dbRes = CharacterDatabase.Query(stmt);
        if (!dbRes)
            return result;

        do
        {
            Field* fields = dbRes->Fetch();
            std::string rowSource = fields[0].Get<std::string>();
            if (rowSource != source)
                continue;

            std::string data = fields[1].Get<std::string>();
            if (!IsCanonicalPaidReceiptRow(rowSource, data))
            {
                unsafePaidRow = true;
                LOG_ERROR("entities.player", "Refusing offline settings update to malformed paid receipt row for character {}",
                    playerLowGuid);
                return result;
            }
            return ParseSettingsData(data);
        } while (dbRes->NextRow());

        return result;
    }

    void UpdateSetting(ObjectGuid::LowType playerLowGuid, std::string const& source, uint32 index, uint32 value)
    {
        if (!sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED))
            return;

        bool unsafePaidRow = false;
        PlayerSettingVector settings = LoadPlayerSettings(playerLowGuid, source, unsafePaidRow);
        if (unsafePaidRow)
            return; // The original untrusted row must be preserved.
        size_t const requiredSize = static_cast<size_t>(index) + 1;
        if (settings.size() < requiredSize)
            settings.resize(requiredSize); // zero-initialized PlayerSetting::value

        settings[index].value = value;

        CharacterDatabasePreparedStatement* stmt = PlayerSettingsStore::PrepareReplaceStatement(playerLowGuid, source, settings);
        CharacterDatabase.Execute(stmt);
    }
}

// Implementation of PrepareReplaceStatement
CharacterDatabasePreparedStatement* PlayerSettingsStore::PrepareReplaceStatement(ObjectGuid::LowType playerLowGuid, std::string const& source, PlayerSettingVector const& settings)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHAR_SETTINGS);
    stmt->SetData(0, playerLowGuid);
    stmt->SetData(1, source);
    stmt->SetData(2, SerializeSettingsData(settings));
    return stmt;
}

void Player::_LoadCharacterSettings(PreparedQueryResult result)
{
    m_charSettingsMap.clear();
    m_preserveMalformedPaidSettingsRow = false;

    if (!sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED))
        return;

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        std::string source = fields[0].Get<std::string>();
        std::string data = fields[1].Get<std::string>();

        if (!PlayerSettingsStore::IsCanonicalPaidReceiptRow(source, data))
        {
            // Protect the missing cache entry from implicit GetPlayerSetting
            // creation, explicit UpdatePlayerSetting and ordinary autosaves.
            // No raw character/account settings are written to logs.
            m_preserveMalformedPaidSettingsRow = true;
            LOG_ERROR("entities.player", "Preserving malformed paid receipt row for character {}; repair required",
                GetGUID().GetCounter());
            continue;
        }

        PlayerSettingVector settings = PlayerSettingsStore::ParseSettingsData(data);
        m_charSettingsMap.emplace(std::move(source), std::move(settings));

    } while (result->NextRow());
}

PlayerSetting Player::GetPlayerSetting(std::string const& source, uint32 index)
{
    // Normally this read auto-creates a missing source via UpdatePlayerSetting.
    // Do not replace an intentionally skipped malformed paid DB row.
    if (m_preserveMalformedPaidSettingsRow && source == "ip_paid_expansion")
        return PlayerSetting(0);
    auto it = m_charSettingsMap.find(source);
    if (it == m_charSettingsMap.end() || static_cast<size_t>(index) >= it->second.size())
    {
        UpdatePlayerSetting(source, index, 0);
        return m_charSettingsMap[source][index];
    }

    return it->second[index];
}

void Player::_SavePlayerSettings(CharacterDatabaseTransaction trans)
{
    if (!sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED))
        return;

    for (auto const& [source, settings] : m_charSettingsMap)
    {
        if (m_preserveMalformedPaidSettingsRow && source == "ip_paid_expansion")
            continue;
        if (settings.empty())
            continue;

        CharacterDatabasePreparedStatement* stmt = PlayerSettingsStore::PrepareReplaceStatement(GetGUID().GetCounter(), source, settings);
        trans->Append(stmt);
    }
}

void Player::UpdatePlayerSetting(std::string const& source, uint32 index, uint32 value)
{
    if (m_preserveMalformedPaidSettingsRow && source == "ip_paid_expansion")
        return; // Never replace the damaged raw source, even on direct writes.
    auto it = m_charSettingsMap.find(source);
    size_t const requiredSize = static_cast<size_t>(index) + 1;

    if (it == m_charSettingsMap.end())
    {
        // Settings not found, create new vector of appropriate size
        PlayerSettingVector settings(requiredSize); // default-initialized PlayerSetting
        settings[index].value = value;

        m_charSettingsMap.emplace(source, std::move(settings));
    }
    else
    {
        PlayerSettingVector& settings = it->second;
        if (settings.size() < requiredSize)
            settings.resize(requiredSize); // new elements default to zero

        settings[index].value = value;
    }
}
