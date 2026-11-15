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

#include "AuctionHouseMgr.h"
#include "AuctionHouseSearcher.h"
#include "CharacterCache.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Player.h"
#include "WorldSession.h"

AuctionHouseWorkerThread::AuctionHouseWorkerThread(ProducerConsumerQueue<AuctionSearcherRequest*>* requestQueue, MPSCQueue<AuctionSearcherResponse>* responseQueue)
    : _requestQueue(requestQueue), _responseQueue(responseQueue)
{
    _workerThread = std::thread(&AuctionHouseWorkerThread::Run, this);
}

AuctionHouseWorkerThread::~AuctionHouseWorkerThread()
{
    Stop();
}

void AuctionHouseWorkerThread::Stop()
{
    _stopped = true;
    if (_workerThread.joinable())
        _workerThread.join();
}

void AuctionHouseWorkerThread::Run()
{
    while (!_stopped)
    {
        std::this_thread::sleep_for(Milliseconds(25));
        if (!_stopped)
            ProcessSearchRequests();
    }
}

void AuctionHouseWorkerThread::ProcessSearchRequests()
{
    AuctionSearcherRequest* searchRequest;
    while (_requestQueue->Pop(searchRequest))
    {
        switch (searchRequest->requestType)
        {
        case AuctionSearcherRequest::Type::LIST:
        {
            AuctionSearchListRequest const* searchListRequest = static_cast<AuctionSearchListRequest*>(searchRequest);
            SearchListRequest(*searchListRequest);
            break;
        }
        case AuctionSearcherRequest::Type::OWNER_LIST:
        {
            AuctionSearchOwnerListRequest const* searchOwnerListRequest = static_cast<AuctionSearchOwnerListRequest*>(searchRequest);
            SearchOwnerListRequest(*searchOwnerListRequest);
            break;
        }
        case AuctionSearcherRequest::Type::BIDDER_LIST:
        {
            AuctionSearchBidderListRequest const* searchBidderListRequest = static_cast<AuctionSearchBidderListRequest*>(searchRequest);
            SearchBidderListRequest(*searchBidderListRequest);
            break;
        }
        default:
            break;
        }

        delete searchRequest;
    }
}

void AuctionHouseWorkerThread::SearchListRequest(AuctionSearchListRequest const& searchListRequest)
{
    SearchableAuctionEntriesMap const& searchableAuctionMap = searchListRequest.catalog->rows;
    uint32 count = 0, totalCount = 0;

    AuctionSearcherResponse* searchResponse = new AuctionSearcherResponse();
    searchResponse->playerGuid = searchListRequest.playerInfo.playerGuid;
    searchResponse->browse = true;
    searchResponse->stamp = searchListRequest.stamp;
    searchResponse->packet.Initialize(SMSG_AUCTION_LIST_RESULT, (4 + 4 + 4));
    searchResponse->packet << (uint32)0;

    if (!searchListRequest.searchInfo.getAll)
    {
        SortableAuctionEntriesList auctionEntries;
        BuildListAuctionItems(searchListRequest, auctionEntries, searchableAuctionMap);

        if (!searchListRequest.searchInfo.sorting.empty() && auctionEntries.size() > MAX_AUCTIONS_PER_PAGE)
        {
            AuctionSorter sorter(&searchListRequest.searchInfo.sorting, searchListRequest.playerInfo.loc_idx);
            std::sort(auctionEntries.begin(), auctionEntries.end(), sorter);
        }

        auto const window = AuctionBrowseSelection::PageWindow(auctionEntries.size(),
            searchListRequest.searchInfo.listfrom, MAX_AUCTIONS_PER_PAGE);
        for (std::size_t i = window.begin; i < window.end; ++i)
        {
            auctionEntries[i]->BuildAuctionInfo(searchResponse->packet, searchListRequest.searchTime);
            ++count;
        }

        totalCount = auctionEntries.size();
    }
    else
    {
        // getAll retains its legacy ordinary-filter bypass.
        auto const counts = AuctionBrowseSelection::GetAll(searchableAuctionMap,
            searchListRequest.visibility, MAX_GETALL_RETURN,
            [](auto const& pair) { return pair.second->visibilityTag; },
            [&](auto const& pair) { pair.second->BuildAuctionInfo(searchResponse->packet, searchListRequest.searchTime); });
        count = counts.returned;
        totalCount = counts.total;
    }

    searchResponse->packet.put<uint32>(0, count);
    searchResponse->packet << totalCount;
    searchResponse->packet << uint32(AUCTION_SEARCH_DELAY);

    _responseQueue->Enqueue(searchResponse);
}

void AuctionHouseWorkerThread::SearchOwnerListRequest(AuctionSearchOwnerListRequest const& searchOwnerListRequest)
{
    SearchableAuctionEntriesMap const& searchableAuctionMap = searchOwnerListRequest.catalog->rows;

    AuctionSearcherResponse* searchResponse = new AuctionSearcherResponse();
    searchResponse->playerGuid = searchOwnerListRequest.ownerGuid;
    searchResponse->packet.Initialize(SMSG_AUCTION_OWNER_LIST_RESULT, (4 + 4 + 4));
    searchResponse->packet << (uint32)0;                                     // amount place holder

    uint32 count = 0;
    uint32 totalcount = 0;

    for (auto const& pair : searchableAuctionMap)
    {
        if (pair.second->ownerGuid != searchOwnerListRequest.ownerGuid)
            continue;

        std::shared_ptr<SearchableAuctionEntry const> const& auctionEntry = pair.second;
        auctionEntry->BuildAuctionInfo(searchResponse->packet, searchOwnerListRequest.searchTime);
        ++count;
        ++totalcount;
    }

    searchResponse->packet.put<uint32>(0, count);
    searchResponse->packet << (uint32)totalcount;
    searchResponse->packet << uint32(AUCTION_SEARCH_DELAY);

    _responseQueue->Enqueue(searchResponse);
}

void AuctionHouseWorkerThread::SearchBidderListRequest(AuctionSearchBidderListRequest const& searchBidderListRequest)
{
    SearchableAuctionEntriesMap const& searchableAuctionMap = searchBidderListRequest.catalog->rows;

    AuctionSearcherResponse* searchResponse = new AuctionSearcherResponse();
    searchResponse->playerGuid = searchBidderListRequest.ownerGuid;
    searchResponse->packet.Initialize(SMSG_AUCTION_BIDDER_LIST_RESULT, (4 + 4 + 4));
    searchResponse->packet << (uint32)0;                                     //add 0 as count

    uint32 count = 0;
    uint32 totalcount = 0;

    for (uint32 const auctionId : searchBidderListRequest.outbiddedAuctionIds)
    {
        SearchableAuctionEntriesMap::const_iterator itr = searchableAuctionMap.find(auctionId);
        if (itr == searchableAuctionMap.end())
            continue;

        std::shared_ptr<SearchableAuctionEntry const> const& auctionEntry = itr->second;
        auctionEntry->BuildAuctionInfo(searchResponse->packet, searchBidderListRequest.searchTime);
        ++count;
        ++totalcount;
    }

    for (auto const& pair : searchableAuctionMap)
    {
        if (pair.second->bidderGuid != searchBidderListRequest.ownerGuid)
            continue;

        std::shared_ptr<SearchableAuctionEntry const> const& auctionEntry = pair.second;
        auctionEntry->BuildAuctionInfo(searchResponse->packet, searchBidderListRequest.searchTime);
        ++count;
        ++totalcount;
    }

    searchResponse->packet.put<uint32>(0, count);                           // add count to placeholder
    searchResponse->packet << totalcount;
    searchResponse->packet << uint32(AUCTION_SEARCH_DELAY);

    _responseQueue->Enqueue(searchResponse);
}

void AuctionHouseWorkerThread::BuildListAuctionItems(AuctionSearchListRequest const& searchRequest, SortableAuctionEntriesList& auctionEntries, SearchableAuctionEntriesMap const& auctionMap) const
{
    // pussywizard: optimization, this is a simplified case for the default search state (no filters)
    if (searchRequest.searchInfo.itemClass == 0xffffffff && searchRequest.searchInfo.itemSubClass == 0xffffffff
        && searchRequest.searchInfo.inventoryType == 0xffffffff && searchRequest.searchInfo.quality == 0xffffffff
        && searchRequest.searchInfo.levelmin == 0x00 && searchRequest.searchInfo.levelmax == 0x00
        && searchRequest.searchInfo.usable == 0x00 && searchRequest.searchInfo.wsearchedname.empty())
    {
        for (auto const& pair : auctionMap)
            if (AuctionBrowseSelection::Visible(searchRequest.visibility, pair.second->visibilityTag))
                auctionEntries.push_back(pair.second.get());

        return;
    }

    for (auto const& pair : auctionMap)
    {
        std::shared_ptr<SearchableAuctionEntry const> const& Aentry = pair.second;
        if (!AuctionBrowseSelection::Visible(searchRequest.visibility, Aentry->visibilityTag))
            continue;
        SearchableAuctionEntryItem const& Aitem = Aentry->item;
        AuctionSearchItemFacts::Facts const* proto = &Aitem.facts;

        if (searchRequest.searchInfo.itemClass != 0xffffffff && proto->Class != searchRequest.searchInfo.itemClass)
            continue;

        if (searchRequest.searchInfo.itemSubClass != 0xffffffff && proto->SubClass != searchRequest.searchInfo.itemSubClass)
            continue;

        if (searchRequest.searchInfo.inventoryType != 0xffffffff && proto->InventoryType != searchRequest.searchInfo.inventoryType)
        {
            // xinef: exception, robes are counted as chests
            if (searchRequest.searchInfo.inventoryType != INVTYPE_CHEST || proto->InventoryType != INVTYPE_ROBE)
                continue;
        }

        if (searchRequest.searchInfo.quality != 0xffffffff && proto->Quality < searchRequest.searchInfo.quality)
            continue;

        if (searchRequest.searchInfo.levelmin != 0x00 && (proto->RequiredLevel < searchRequest.searchInfo.levelmin
            || (searchRequest.searchInfo.levelmax != 0x00 && proto->RequiredLevel > searchRequest.searchInfo.levelmax)))
        {
            continue;
        }

        if (searchRequest.searchInfo.usable != 0x00)
        {
            if (!searchRequest.playerInfo.usablePlayerInfo.value().PlayerCanUseItem(*proto))
                continue;
        }

        // Allow search by suffix (ie: of the Monkey) or partial name (ie: Monkey)
        // No need to do any of this if no search term was entered
        if (!searchRequest.searchInfo.wsearchedname.empty())
        {
            if (Aitem.itemName[searchRequest.playerInfo.loc_idx].find(searchRequest.searchInfo.wsearchedname) == std::wstring::npos)
                continue;
        }

        auctionEntries.push_back(Aentry.get());
    }
}

AuctionHouseSearcher::AuctionHouseSearcher()
{
    for (uint32 i = 0; i < sWorld->getIntConfig(CONFIG_AUCTIONHOUSE_WORKERTHREADS); ++i)
        _workerThreads.push_back(std::make_unique<AuctionHouseWorkerThread>(&_requestQueue, &_responseQueue));
}

AuctionHouseSearcher::~AuctionHouseSearcher()
{
    {
        std::lock_guard lock(_admissionMutex);
        _closing = true;
        _requestQueue.Cancel();
    }
    for (std::unique_ptr<AuctionHouseWorkerThread> const& workerThread : _workerThreads)
        workerThread->Stop();
}

void AuctionHouseSearcher::Update()
{
    AuctionSearcherResponse* response = nullptr;
    while (_responseQueue.Dequeue(response))
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(response->playerGuid);
        bool send = player != nullptr;
        if (send && response->browse)
        {
            auto const active = _catalog.Current();
            WorldSession* session = player->GetSession();
            // Native B remains Disabled: no real provider or eligibility revision
            // source is registered. Non-Disabled cannot emit until that seam exists.
            AuctionResponseFence::Stamp current;
            if (active.mode == AuctionVisibility::Mode::Disabled && session &&
                session->GetPlayer() == player && !session->PlayerLogout())
                current = {active.policy.generation, player->GetGUID().GetRawValue(),
                    session->GetAuctionBrowseIncarnation(), 1};
            send = AuctionResponseFence::MayEmit(response->stamp, current);
        }
        if (send)
            player->SendDirectMessage(&response->packet);

        {
            std::lock_guard lock(_admissionMutex);
            --_outstanding;
            if (response->browse)
                --_browseOutstanding;
        }
        delete response;
    }
}

void AuctionHouseSearcher::QueueSearchRequest(AuctionSearcherRequest* searchRequestInfo)
{
    std::unique_ptr<AuctionSearcherRequest> request(searchRequestInfo);
    if (!request)
        return;
    std::lock_guard lock(_admissionMutex);
    bool const browse = request->requestType == AuctionSearcherRequest::Type::LIST;
    if (_closing || _workerThreads.empty() || _outstanding >= MaxOutstanding ||
        (browse && _browseOutstanding >= MaxBrowseOutstanding))
        return;
    request->catalog = _catalog.Capture(static_cast<uint8>(request->listFaction));
    if (!request->catalog)
        return;
    request->searchTime = GameTime::GetGameTime().count();
    if (browse)
    {
        auto& list = static_cast<AuctionSearchListRequest&>(*request);
        // No live provider in Phase 4B. Disabled uses a constant eligibility
        // revision because progression cannot affect the result.
        if (request->catalog->mode != AuctionVisibility::Mode::Disabled)
            return;
        list.visibility = {request->catalog->mode, request->catalog->policy, {}};
        list.stamp = {request->catalog->policy.generation, list.playerInfo.playerGuid.GetRawValue(),
            list.playerInfo.sessionIncarnation, 1};
        if (!AuctionResponseFence::Valid(list.stamp))
            return;
    }
    _requestQueue.Push(request.get());
    request.release();
    ++_outstanding;
    if (browse)
        ++_browseOutstanding;
}

void AuctionHouseSearcher::AddAuction(AuctionEntry const* auctionEntry)
{
    Item* item = sAuctionMgr->GetAItem(auctionEntry->item_guid);
    if (!item)
        return;

    SearchableAuctionEntry searchableAuctionEntry{};
    searchableAuctionEntry.Id = auctionEntry->Id;

    // Auction info
    searchableAuctionEntry.ownerGuid = auctionEntry->owner;
    sCharacterCache->GetCharacterNameByGuid(auctionEntry->owner, searchableAuctionEntry.ownerName);
    searchableAuctionEntry.startbid = auctionEntry->startbid;
    searchableAuctionEntry.buyout = auctionEntry->buyout;
    searchableAuctionEntry.expire_time = auctionEntry->expire_time;
    searchableAuctionEntry.listFaction = auctionEntry->GetFactionId();
    searchableAuctionEntry.bid = auctionEntry->bid;
    searchableAuctionEntry.bidderGuid = auctionEntry->bidder;

    // Item info
    searchableAuctionEntry.item.entry = item->GetEntry();

    for (uint8 i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
    {
        searchableAuctionEntry.item.enchants[i].id = item->GetEnchantmentId(EnchantmentSlot(i));
        searchableAuctionEntry.item.enchants[i].duration = item->GetEnchantmentDuration(EnchantmentSlot(i));
        searchableAuctionEntry.item.enchants[i].charges = item->GetEnchantmentCharges(EnchantmentSlot(i));
    }

    searchableAuctionEntry.item.randomPropertyId = item->GetItemRandomPropertyId();
    searchableAuctionEntry.item.suffixFactor = item->GetItemSuffixFactor();
    searchableAuctionEntry.item.count = item->GetCount();
    searchableAuctionEntry.item.spellCharges = item->GetSpellCharges();
    ItemTemplate const* proto = item->GetTemplate();
    auto& facts = searchableAuctionEntry.item.facts;
    facts.Class = proto->Class;
    facts.SubClass = proto->SubClass;
    facts.InventoryType = proto->InventoryType;
    facts.Quality = proto->Quality;
    facts.RequiredLevel = proto->RequiredLevel;
    facts.AllowableClass = proto->AllowableClass;
    facts.AllowableRace = proto->AllowableRace;
    facts.RequiredSkill = proto->RequiredSkill;
    facts.RequiredSkillRank = proto->RequiredSkillRank;
    facts.RequiredSpell = proto->RequiredSpell;
    facts.itemSkill = proto->GetSkill();
    if (proto->Spells[0].SpellId)
    {
        SpellEntry const* spellEntry = sSpellStore.LookupEntry(proto->Spells[0].SpellId);
        if (spellEntry && spellEntry->Effect[0] == SPELL_EFFECT_LEARN_SPELL && spellEntry->EffectTriggerSpell[0])
            facts.vanillaLearnedSpell = spellEntry->EffectTriggerSpell[0];
        facts.teachingSpell = proto->Spells[0].SpellId == 483 || proto->Spells[0].SpellId == 55884;
        facts.taughtSpell = proto->Spells[1].SpellId;
    }

    searchableAuctionEntry.SetItemNames(proto);

    _catalog.Add(static_cast<uint8>(searchableAuctionEntry.listFaction), std::move(searchableAuctionEntry));
}

void AuctionHouseSearcher::RemoveAuction(AuctionEntry const* auctionEntry)
{
    _catalog.Remove(static_cast<uint8>(auctionEntry->GetFactionId()), auctionEntry->Id);
}

void AuctionHouseSearcher::UpdateBid(AuctionEntry const* auctionEntry)
{
    _catalog.UpdateBid(static_cast<uint8>(auctionEntry->GetFactionId()), auctionEntry->Id, auctionEntry->bid, auctionEntry->bidder);
}

void SearchableAuctionEntry::BuildAuctionInfo(WorldPacket& data, time_t searchTime) const
{
    data << uint32(Id);
    data << uint32(item.entry);

    for (uint8 i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
    {
        data << uint32(item.enchants[i].id);
        data << uint32(item.enchants[i].duration);
        data << uint32(item.enchants[i].charges);
    }

    data << int32(item.randomPropertyId);                           // Random item property id
    data << uint32(item.suffixFactor);                              // SuffixFactor
    data << uint32(item.count);                                     // item->count
    data << uint32(item.spellCharges);                              // item->charge FFFFFFF
    data << uint32(0);                                              // item->flags (client doesnt do anything with it)
    data << ownerGuid;                                              // Auction->owner
    data << uint32(startbid);                                       // Auction->startbid (not sure if useful)
    data << uint32(bid ? AuctionEntry::CalculateAuctionOutBid(bid) : 0);
    // Minimal outbid
    data << uint32(buyout);                                         // Auction->buyout
    data << uint32((expire_time - searchTime) * IN_MILLISECONDS); // time left
    data << bidderGuid;                                             // auction->bidder current
    data << uint32(bid);                                            // current bid
}

void SearchableAuctionEntry::SetItemNames(ItemTemplate const* proto)
{
    ItemLocale const* il = sObjectMgr->GetItemLocale(proto->ItemId);

    for (uint32 locale = 0; locale < TOTAL_LOCALES; ++locale)
    {
        if (proto->Name1.empty() || !IsLocaleRegistered(LocaleConstant(locale)))
            continue;

        std::string itemName = proto->Name1;

        // local name
        LocaleConstant locdbc_idx = sWorld->GetAvailableDbcLocale(static_cast<LocaleConstant>(locale));
        if (locdbc_idx >= LOCALE_enUS && il)
            ObjectMgr::GetLocaleString(il->Name, locale, itemName);

        // DO NOT use GetItemEnchantMod(proto->RandomProperty) as it may return a result
        //  that matches the search but it may not equal item->GetItemRandomPropertyId()
        //  used in BuildAuctionInfo() which then causes wrong items to be listed
        int32 propRefID = item.randomPropertyId;

        if (propRefID)
        {
            // Append the suffix to the name (ie: of the Monkey) if one exists
            // These are found in ItemRandomSuffix.dbc and ItemRandomProperties.dbc
            // even though the DBC name seems misleading
            std::array<char const*, 16> const* suffix = nullptr;

            if (propRefID < 0)
            {
                ItemRandomSuffixEntry const* itemRandEntry = sItemRandomSuffixStore.LookupEntry(-item.randomPropertyId);
                if (itemRandEntry)
                    suffix = &itemRandEntry->Name;
            }
            else
            {
                ItemRandomPropertiesEntry const* itemRandEntry = sItemRandomPropertiesStore.LookupEntry(item.randomPropertyId);
                if (itemRandEntry)
                    suffix = &itemRandEntry->Name;
            }

            // dbc local name
            if (suffix)
            {
                // Append the suffix (ie: of the Monkey) to the name using localization
                // or default enUS if localization is invalid
                itemName += ' ';
                itemName += (*suffix)[locdbc_idx >= 0 ? locdbc_idx : LOCALE_enUS];
            }
        }

        if (!Utf8toWStr(itemName, item.itemName[locale]))
            continue;

        wstrToLower(item.itemName[locale]);
    }
}

int SearchableAuctionEntry::CompareAuctionEntry(uint32 column, SearchableAuctionEntry const& auc, int loc_idx) const
{
    switch (column)
    {
    case AUCTION_SORT_MINLEVEL:                                             // level = 0
    {
        AuctionSearchItemFacts::Facts const* itemProto1 = &item.facts;
        AuctionSearchItemFacts::Facts const* itemProto2 = &auc.item.facts;

        if (itemProto1->RequiredLevel > itemProto2->RequiredLevel)
            return -1;
        else if (itemProto1->RequiredLevel < itemProto2->RequiredLevel)
            return +1;
        break;
    }
    case AUCTION_SORT_RARITY:                                             // quality = 1
    {
        AuctionSearchItemFacts::Facts const* itemProto1 = &item.facts;
        AuctionSearchItemFacts::Facts const* itemProto2 = &auc.item.facts;

        if (itemProto1->Quality < itemProto2->Quality)
            return -1;
        else if (itemProto1->Quality > itemProto2->Quality)
            return +1;
        break;
    }
    case AUCTION_SORT_BUYOUT:                                             // buyoutthenbid = 2 (UNUSED?)
        if (buyout != auc.buyout)
        {
            if (buyout < auc.buyout)
                return -1;
            else if (buyout > auc.buyout)
                return +1;
        }
        else
        {
            if (bid < auc.bid)
                return -1;
            else if (bid > auc.bid)
                return +1;
        }
        break;
    case AUCTION_SORT_TIMELEFT:                                             // duration = 3
        if (expire_time < auc.expire_time)
            return -1;
        else if (expire_time > auc.expire_time)
            return +1;
        break;
    case AUCTION_SORT_UNK4:                                             // status = 4 (WRONG)
        if (bidderGuid.GetCounter() < auc.bidderGuid.GetCounter())
            return -1;
        else if (bidderGuid.GetCounter() > auc.bidderGuid.GetCounter())
            return +1;
        break;
    case AUCTION_SORT_ITEM:                                             // name = 5
    {
        int comparison = item.itemName[loc_idx].compare(auc.item.itemName[loc_idx]);
        if (comparison > 0)
            return -1;
        else if (comparison < 0)
            return +1;

        break;
    }
    case AUCTION_SORT_MINBIDBUY:                                             // minbidbuyout = 6
    {
        if (buyout != auc.buyout)
        {
            if (buyout > auc.buyout)
                return -1;
            else if (buyout < auc.buyout)
                return +1;
        }
        else
        {
            if (bid < auc.bid)
                return -1;
            else if (bid > auc.bid)
                return +1;
        }
        break;
    }
    case AUCTION_SORT_OWNER:                                             // seller = 7
    {
        int comparison = ownerName.compare(auc.ownerName);
        if (comparison > 0)
            return -1;
        else if (comparison < 0)
            return +1;

        break;
    }
    case AUCTION_SORT_BID:                                             // bid = 8
    {
        uint32 bid1 = bid ? bid : startbid;
        uint32 bid2 = auc.bid ? auc.bid : auc.startbid;

        if (bid1 > bid2)
            return -1;
        else if (bid1 < bid2)
            return +1;
        break;
    }
    case AUCTION_SORT_STACK:                                             // quantity = 9
    {
        if (item.count < auc.item.count)
            return -1;
        else if (item.count > auc.item.count)
            return +1;
        break;
    }
    case AUCTION_SORT_BUYOUT_2:                                            // buyout = 10 (UNUSED?)
        if (buyout < auc.buyout)
            return -1;
        else if (buyout > auc.buyout)
            return +1;
        break;
    default:
        break;
    }

    return 0;
}

bool AuctionSorter::operator()(SearchableAuctionEntry const* auc1, SearchableAuctionEntry const* auc2) const
{
    if (_sort->empty())                      // not sorted
        return false;

    for (AuctionSortOrderVector::const_iterator itr = _sort->begin(); itr != _sort->end(); ++itr)
    {
        int res = auc1->CompareAuctionEntry(itr->sortOrder, *auc2, _loc_idx);
        // "equal" by used column
        if (res == 0)
            continue;
        // less/greater and normal/reversed ordered
        return (res < 0) == itr->isDesc;
    }

    return false;                                           // "equal" by all sorts
}

// Slightly simplified version of Player::CanUseItem. Only checks relevant to auctionhouse items
bool AuctionHouseUsablePlayerInfo::PlayerCanUseItem(AuctionSearchItemFacts::Facts const& facts) const
{
    return AuctionSearchItemFacts::Usable(facts, classMask, raceMask, level,
        [this](uint32 skill) { return GetSkillValue(skill); },
        [this](uint32 spell) { return HasSpell(spell); });
}

uint16 AuctionHouseUsablePlayerInfo::GetSkillValue(uint32 skill) const
{
    if (!skill)
        return 0;

    AuctionPlayerSkills::const_iterator itr = skills.find(skill);
    if (itr == skills.end())
        return 0;

    return itr->second;
}

bool AuctionHouseUsablePlayerInfo::HasSpell(uint32 spell) const
{
    AuctionPlayerSpells::const_iterator itr = spells.find(spell);
    return (itr != spells.end());
}
