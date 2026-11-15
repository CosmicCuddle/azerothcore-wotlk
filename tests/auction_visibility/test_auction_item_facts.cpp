#include "AuctionSearchItemFacts.h"
#include <cassert>
#include <unordered_map>
#include <unordered_set>

using namespace AuctionSearchItemFacts;

int main()
{
    Facts facts;
    facts.AllowableClass = 1;
    facts.AllowableRace = 2;
    facts.RequiredLevel = 20;
    facts.RequiredSkill = 100;
    facts.RequiredSkillRank = 25;
    facts.itemSkill = 101;
    facts.RequiredSpell = 200;
    std::unordered_map<unsigned, unsigned> skills{{100, 25}, {101, 1}};
    std::unordered_set<unsigned> spells{200};
    auto skill = [&](unsigned id) { auto it = skills.find(id); return it == skills.end() ? 0u : it->second; };
    auto spell = [&](unsigned id) { return spells.contains(id); };
    auto usable = [&](unsigned level = 20) { return Usable(facts, 1, 2, level, skill, spell); };
    assert(usable());
    assert(!usable(19));
    assert(!Usable(facts, 2, 2, 20, skill, spell));
    assert(!Usable(facts, 1, 1, 20, skill, spell));
    skills[100] = 24;
    assert(!usable());
    skills[100] = 25;
    skills.erase(101);
    assert(!usable());
    skills[101] = 1;
    spells.erase(200);
    assert(!usable());
    spells.insert(200);
    facts.vanillaLearnedSpell = 300;
    assert(usable());
    spells.insert(300);
    assert(!usable());
    facts.vanillaLearnedSpell = 0;
    facts.teachingSpell = true;
    facts.taughtSpell = 301;
    assert(usable());
    spells.insert(301);
    assert(!usable());
    facts.teachingSpell = false;
    assert(usable());
    facts.RequiredSkillRank = 0;
    skills.erase(100);
    assert(!usable()); // original rejects absent skill even with rank zero
}
