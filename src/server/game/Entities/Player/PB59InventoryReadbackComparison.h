#pragma once

#include "CharacterDatabase.h"
#include "PB59ExpectedInventoryWrites.h"
#include "PB59RecoveryEvidence.h"
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace PB59::Recovery
{
// Value comparison only. Caller must independently establish schema, row scope,
// lossless decoding, snapshot identity and receipt ownership. No database IO,
// mutation, hold release, or proof of custody is provided by this header.
struct InventoryReadbackLink
{
    uint32 owner, bag;
    uint8 slot;
    uint32 item;
};

struct InventoryReadbackInstance
{
    uint32 guid;
    std::optional<uint32> entry;
    uint32 owner, creator, giftCreator, count;
    int32 duration;
    std::optional<std::string> charges;
    std::optional<uint32> flags;
    std::string enchantments;
    int16 randomProperty;
    uint16 durability;
    uint32 playedTime;
    std::optional<std::string> text;
};

struct InventoryReadbackGift
{
    uint32 item, owner, entry, flags;
};

struct InventoryReadbackRows
{
    std::vector<InventoryReadbackLink> links;
    std::vector<InventoryReadbackInstance> instances;
    std::vector<InventoryReadbackGift> gifts;
};

enum class InventoryValueOutcome
{
    Match, Refused, Unrepresentable, Missing, Duplicate, Unexpected, Mismatch
};

struct InventoryValueComparison
{
    InventoryValueOutcome outcome;
    uint64 itemGuid;
    char const* table;
    char const* column;
};

namespace InventoryComparisonDetail
{
template<typename T>
T const* Parameter(InventorySaveStatement const& s, std::size_t i)
{
    return std::get_if<T>(&s.parameters[i].data);
}

// A single-realm global item GUID has no entry or realm bits. Do not
// silently discard a different full identity that aliases the SQL low key.
constexpr uint64 ItemPrefix = uint64(0x4000) << 48;
constexpr uint64 LowMask = 0xffffffffULL;
constexpr bool ItemKey(uint64 full, uint32 low)
{
    return low && full == (ItemPrefix | low);
}

// utf8mb4 accepts Unicode scalar values, including embedded NUL. Reject
// truncated sequences, overlong encodings, surrogates and out-of-range values.
inline bool Utf8(std::string const& text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80)
            continue;
        unsigned n, value, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { n = 1; value = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { n = 2; value = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { n = 3; value = lead & 7; minimum = 0x10000; }
        else return false;
        if (n > text.size() - i)
            return false;
        while (n--)
        {
            auto b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (b & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

template<typename T, typename Rows, typename Key>
bool Index(Rows const& rows, Key key, std::map<uint32, T const*>& out, uint32& duplicate)
{
    for (auto const& row : rows)
        if (!out.emplace(key(row), &row).second)
        {
            duplicate = key(row);
            return false;
        }
    return true;
}
}

[[nodiscard]] inline InventoryValueComparison CompareInventoryWriteValues(
    ExpectedInventoryWrites const& expected, InventoryReadbackRows const& observed)
{
    using O = InventoryValueOutcome;
    using namespace InventoryComparisonDetail;
    constexpr char const* L = "character_inventory";
    constexpr char const* I = "item_instance";
    constexpr char const* G = "character_gifts";
    auto fail = [](O o, uint64 guid, char const* table, char const* col)
    {
        return InventoryValueComparison{o, guid, table, col};
    };
    if (expected.items.empty())
        return fail(O::Refused, 0, "", "empty expected set");

    // Validate bindings before treating supplied rows as evidence.
    std::map<uint32, InventoryReadbackLink> wantLinks;
    std::map<uint32, InventoryReadbackInstance> wantInstances;
    std::map<uint32, uint32> wantGifts;
    std::set<std::tuple<uint32, uint32, uint8>> slots;
    uint32 batchOwner = 0;
    for (auto const& e : expected.items)
    {
        auto const guid = e.itemGuid;
        auto const& l = e.link;
        auto const& v = e.instance;
        if (l.index != CHAR_REP_INVENTORY_ITEM || l.parameters.size() != 4)
            return fail(O::Refused, guid, L, "statement");
        if ((v.index != CHAR_REP_ITEM_INSTANCE && v.index != CHAR_UPD_ITEM_INSTANCE) || v.parameters.size() != 14)
            return fail(O::Refused, guid, I, "statement");
        auto owner = Parameter<uint64>(l, 0);
        auto bag = Parameter<uint32>(l, 1);
        auto slot = Parameter<uint8>(l, 2);
        auto key = Parameter<uint32>(l, 3);
        if (!owner || !bag || !slot || !key)
            return fail(O::Refused, guid, L, "binding type");
        if (!*owner || *owner > LowMask || !ItemKey(guid, *key))
            return fail(O::Unrepresentable, guid, L, "identity");
        auto ownerLow = static_cast<uint32>(*owner);
        if (batchOwner && batchOwner != ownerLow)
            return fail(O::Refused, guid, L, "mixed owners");
        batchOwner = ownerLow;
        if (!slots.emplace(ownerLow, *bag, *slot).second || wantLinks.contains(*key))
            return fail(O::Refused, guid, L, "duplicate expected link");
        auto entry = Parameter<uint32>(v, 0);
        auto instanceOwner = Parameter<uint64>(v, 1);
        auto creator = Parameter<uint32>(v, 2);
        auto giftCreator = Parameter<uint32>(v, 3);
        auto count = Parameter<uint32>(v, 4);
        auto duration = Parameter<uint32>(v, 5);
        auto charges = Parameter<std::string>(v, 6);
        auto flags = Parameter<uint32>(v, 7);
        auto enchants = Parameter<std::string>(v, 8);
        auto random = Parameter<int32>(v, 9);
        auto durability = Parameter<uint32>(v, 10);
        auto played = Parameter<uint32>(v, 11);
        auto text = Parameter<std::string>(v, 12);
        auto instanceKey = Parameter<uint32>(v, 13);
        if (!entry || !instanceOwner || !creator || !giftCreator || !count || !duration ||
            !charges || !flags || !enchants || !random || !durability || !played || !text || !instanceKey)
            return fail(O::Refused, guid, I, "binding type");
        if (*instanceOwner != *owner || *instanceKey != *key || !*entry || !*count)
            return fail(O::Refused, guid, I, "identity/count");
        if (*duration > static_cast<uint32>(std::numeric_limits<int32>::max()))
            return fail(O::Unrepresentable, guid, I, "duration");
        if (*random < std::numeric_limits<int16>::min() || *random > std::numeric_limits<int16>::max())
            return fail(O::Unrepresentable, guid, I, "randomPropertyId");
        if (*durability > std::numeric_limits<uint16>::max())
            return fail(O::Unrepresentable, guid, I, "durability");
        if (charges->size() > 255 || !Utf8(*charges))
            return fail(O::Unrepresentable, guid, I, "charges");
        if (enchants->size() > 65535 || !Utf8(*enchants))
            return fail(O::Unrepresentable, guid, I, "enchantments");
        if (text->size() > MaximumTextBytes || !Utf8(*text))
            return fail(O::Unrepresentable, guid, I, "text");
        if (e.giftOwner)
        {
            auto const& g = *e.giftOwner;
            if (v.index != CHAR_UPD_ITEM_INSTANCE || g.index != CHAR_UPD_GIFT_OWNER || g.parameters.size() != 2)
                return fail(O::Refused, guid, G, "statement");
            auto giftOwner = Parameter<uint64>(g, 0);
            auto giftKey = Parameter<uint32>(g, 1);
            if (!giftOwner || !giftKey || *giftOwner != *owner || *giftKey != *key)
                return fail(O::Refused, guid, G, "identity");
            wantGifts.emplace(*key, ownerLow);
        }
        wantLinks.emplace(*key, InventoryReadbackLink{ownerLow, *bag, *slot, *key});
        wantInstances.emplace(*key, InventoryReadbackInstance{*key, *entry, ownerLow,
            *creator, *giftCreator, *count, static_cast<int32>(*duration), *charges, *flags,
            *enchants, static_cast<int16>(*random), static_cast<uint16>(*durability), *played, *text});
    }

    std::map<uint32, InventoryReadbackLink const*> links;
    std::map<uint32, InventoryReadbackInstance const*> instances;
    std::map<uint32, InventoryReadbackGift const*> gifts;
    uint32 duplicate = 0;
    if (!Index(observed.links, [](auto const& x) { return x.item; }, links, duplicate))
        return fail(O::Duplicate, ItemPrefix | duplicate, L, "item");
    if (!Index(observed.instances, [](auto const& x) { return x.guid; }, instances, duplicate))
        return fail(O::Duplicate, ItemPrefix | duplicate, I, "guid");
    if (!Index(observed.gifts, [](auto const& x) { return x.item; }, gifts, duplicate))
        return fail(O::Duplicate, ItemPrefix | duplicate, G, "item_guid");
    slots.clear();
    for (auto const& [key, row] : links)
        if (!slots.emplace(row->owner, row->bag, row->slot).second)
            return fail(O::Duplicate, ItemPrefix | key, L, "owner/bag/slot");

    for (auto const& [key, row] : wantLinks)
    {
        if (!links.contains(key))
            return fail(O::Missing, ItemPrefix | key, L, "item");
        if (!instances.contains(key))
            return fail(O::Missing, ItemPrefix | key, I, "guid");
        if (wantGifts.contains(key) && !gifts.contains(key))
            return fail(O::Missing, ItemPrefix | key, G, "item_guid");
    }
    for (auto const& [key, row] : links)
        if (!wantLinks.contains(key))
            return fail(O::Unexpected, ItemPrefix | key, L, "item");
    for (auto const& [key, row] : instances)
        if (!wantInstances.contains(key))
            return fail(O::Unexpected, ItemPrefix | key, I, "guid");
    for (auto const& [key, row] : gifts)
        if (!wantGifts.contains(key))
            return fail(O::Unexpected, ItemPrefix | key, G, "item_guid");

    for (auto const& [key, row] : wantLinks)
    {
        auto const guid = ItemPrefix | key;
        auto const& actual = *links.at(key);
        if (actual.owner != row.owner) return fail(O::Mismatch, guid, L, "guid");
        if (actual.bag != row.bag) return fail(O::Mismatch, guid, L, "bag");
        if (actual.slot != row.slot) return fail(O::Mismatch, guid, L, "slot");
        auto const& a = *instances.at(key);
        auto const& b = wantInstances.at(key);
        if (a.entry != b.entry) return fail(O::Mismatch, guid, I, "itemEntry");
        if (a.owner != b.owner) return fail(O::Mismatch, guid, I, "owner_guid");
        if (a.creator != b.creator) return fail(O::Mismatch, guid, I, "creatorGuid");
        if (a.giftCreator != b.giftCreator) return fail(O::Mismatch, guid, I, "giftCreatorGuid");
        if (a.count != b.count) return fail(O::Mismatch, guid, I, "count");
        if (a.duration != b.duration) return fail(O::Mismatch, guid, I, "duration");
        if (a.charges != b.charges) return fail(O::Mismatch, guid, I, "charges");
        if (a.flags != b.flags) return fail(O::Mismatch, guid, I, "flags");
        if (a.enchantments != b.enchantments) return fail(O::Mismatch, guid, I, "enchantments");
        if (a.randomProperty != b.randomProperty) return fail(O::Mismatch, guid, I, "randomPropertyId");
        if (a.durability != b.durability) return fail(O::Mismatch, guid, I, "durability");
        if (a.playedTime != b.playedTime) return fail(O::Mismatch, guid, I, "playedTime");
        if (a.text != b.text) return fail(O::Mismatch, guid, I, "text");
        // Gift entry/flags are NOT part of the captured write set. Their
        // preservation needs a separate pre-image; this cannot prove it.
        if (wantGifts.contains(key) && gifts.at(key)->owner != wantGifts.at(key))
            return fail(O::Mismatch, guid, G, "guid");
    }
    return fail(O::Match, 0, "", "");
}
}
