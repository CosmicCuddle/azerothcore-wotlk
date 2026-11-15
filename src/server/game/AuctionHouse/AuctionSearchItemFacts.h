#pragma once

#include <cstdint>

namespace AuctionSearchItemFacts
{
// Captured from ItemTemplate and Spell.dbc on the publishing thread.
// Workers never need a live ItemTemplate or spell-store lookup for usability.
struct Facts
{
    std::uint32_t Class = 0;
    std::uint32_t SubClass = 0;
    std::uint32_t InventoryType = 0;
    std::uint32_t Quality = 0;
    std::uint32_t RequiredLevel = 0;
    std::uint32_t AllowableClass = 0;
    std::uint32_t AllowableRace = 0;
    std::uint32_t RequiredSkill = 0;
    std::uint32_t RequiredSkillRank = 0;
    std::uint32_t RequiredSpell = 0;
    std::uint32_t itemSkill = 0;
    std::uint32_t vanillaLearnedSpell = 0;
    bool teachingSpell = false;
    std::uint32_t taughtSpell = 0;
};

template<class Skill, class Spell>
bool Usable(Facts const& facts, std::uint32_t classMask, std::uint32_t raceMask,
    std::uint32_t level, Skill skillValue, Spell hasSpell)
{
    if (facts.itemSkill && !skillValue(facts.itemSkill))
        return false;
    if (!(facts.AllowableClass & classMask) || !(facts.AllowableRace & raceMask))
        return false;
    if (facts.RequiredSkill)
    {
        auto skill = skillValue(facts.RequiredSkill);
        if (!skill || skill < facts.RequiredSkillRank)
            return false;
    }
    if (facts.RequiredSpell && !hasSpell(facts.RequiredSpell))
        return false;
    if (level < facts.RequiredLevel)
        return false;
    if (facts.vanillaLearnedSpell && hasSpell(facts.vanillaLearnedSpell))
        return false;
    if (facts.teachingSpell && hasSpell(facts.taughtSpell))
        return false;
    return true;
}
}
