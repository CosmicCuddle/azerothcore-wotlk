#include "PB59ReadbackQueryPlan.h"
#include <iostream>
using namespace PB59::Recovery::ReadbackWire;
unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while(false)
int main()
{
    constexpr std::uint64_t p = std::uint64_t(0x4000) << 48;
    std::array<std::uint64_t,2> ids{p|7,p|0xffffffffULL};
    auto plan=BuildReadbackQueryPlan(ids);
    CHECK(plan && plan->rowLimit==2);
    CHECK(plan->links=="SELECT SUBSTRING(CAST(`guid` AS BINARY),1,11) AS `guid`,SUBSTRING(CAST(`bag` AS BINARY),1,11) AS `bag`,SUBSTRING(CAST(`slot` AS BINARY),1,4) AS `slot`,SUBSTRING(CAST(`item` AS BINARY),1,11) AS `item` FROM `character_inventory` WHERE `item` IN (7,4294967295) LIMIT 3");
    CHECK(plan->gifts=="SELECT SUBSTRING(CAST(`item_guid` AS BINARY),1,11) AS `item_guid`,SUBSTRING(CAST(`guid` AS BINARY),1,11) AS `guid`,SUBSTRING(CAST(`entry` AS BINARY),1,11) AS `entry`,SUBSTRING(CAST(`flags` AS BINARY),1,11) AS `flags` FROM `character_gifts` WHERE `item_guid` IN (7,4294967295) LIMIT 3");
    CHECK(plan->instances=="SELECT SUBSTRING(CAST(`guid` AS BINARY),1,11) AS `guid`,SUBSTRING(CAST(`itemEntry` AS BINARY),1,11) AS `itemEntry`,SUBSTRING(CAST(`owner_guid` AS BINARY),1,11) AS `owner_guid`,SUBSTRING(CAST(`creatorGuid` AS BINARY),1,11) AS `creatorGuid`,SUBSTRING(CAST(`giftCreatorGuid` AS BINARY),1,11) AS `giftCreatorGuid`,SUBSTRING(CAST(`count` AS BINARY),1,11) AS `count`,SUBSTRING(CAST(`duration` AS BINARY),1,12) AS `duration`,SUBSTRING(CAST(`charges` AS BINARY),1,256) AS `charges`,SUBSTRING(CAST(`flags` AS BINARY),1,11) AS `flags`,SUBSTRING(CAST(`enchantments` AS BINARY),1,65536) AS `enchantments`,SUBSTRING(CAST(`randomPropertyId` AS BINARY),1,7) AS `randomPropertyId`,SUBSTRING(CAST(`durability` AS BINARY),1,6) AS `durability`,SUBSTRING(CAST(`playedTime` AS BINARY),1,11) AS `playedTime`,SUBSTRING(CAST(`text` AS BINARY),1,513) AS `text` FROM `item_instance` WHERE `guid` IN (7,4294967295) LIMIT 3");
    CHECK(!BuildReadbackQueryPlan({}));
    for (auto bad : std::array<std::uint64_t,7>{0,7,p,p|(1ULL<<32)|7,(0x4001ULL<<48)|7,~0ULL,p|(1ULL<<47)|7})
    {
        std::array<std::uint64_t,1> invalid{bad}; CHECK(!BuildReadbackQueryPlan(invalid));
    }
    ids[1]=ids[0]; CHECK(!BuildReadbackQueryPlan(ids));
    std::array<std::uint64_t,20> many{};
    for(std::size_t i=0;i<many.size();++i) many[i]=p|(i+1);
    CHECK(!BuildReadbackQueryPlan(many));
    auto max=BuildReadbackQueryPlan(std::span(many).first(19));
    CHECK(max && max->rowLimit==19);
    for(auto const* sql : {&max->links,&max->instances,&max->gifts})
    {
        CHECK(sql->starts_with("SELECT "));
        CHECK(sql->ends_with(" IN (1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19) LIMIT 20"));
        CHECK(sql->find(';')==std::string::npos);
        CHECK(sql->find("JOIN")==std::string::npos);
        CHECK(sql->find("DISTINCT")==std::string::npos);
    }
    auto single=BuildReadbackQueryPlan(std::span(many).first(1));
    CHECK(single && single->rowLimit==1 && single->links.ends_with(" IN (1) LIMIT 2"));
    std::cout << checks << '/' << checks << " query-plan checks passed\n";
}
