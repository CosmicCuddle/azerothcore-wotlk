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

#ifndef _AUCTION_HOUSE_SEARCHER_H
#define _AUCTION_HOUSE_SEARCHER_H

#include "AuctionHouseMgr.h"
#include "AuctionSearchItemFacts.h"
#include "AuctionBrowseSelection.h"
#include "AuctionCatalogPublication.h"
#include "AuctionResponseFence.h"
#include "Common.h"
#include "Item.h"
#include "MPSCQueue.h"
#include "PCQueue.h"
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct ItemTemplate;

enum AuctionSortOrder
{
    AUCTION_SORT_MINLEVEL       = 0,
    AUCTION_SORT_RARITY         = 1,
    AUCTION_SORT_BUYOUT         = 2,
    AUCTION_SORT_TIMELEFT       = 3,
    AUCTION_SORT_UNK4           = 4,
    AUCTION_SORT_ITEM           = 5,
    AUCTION_SORT_MINBIDBUY      = 6,
    AUCTION_SORT_OWNER          = 7,
    AUCTION_SORT_BID            = 8,
    AUCTION_SORT_STACK          = 9,
    AUCTION_SORT_BUYOUT_2       = 10,

    AUCTION_SORT_MAX
};

struct AuctionSortInfo
{
    AuctionSortInfo() = default;

    AuctionSortOrder sortOrder{ AUCTION_SORT_MAX };
    bool isDesc{ true };
};

struct AuctionEntryItemEnchants
{
    uint32 id;
    uint32 duration;
    uint32 charges;
};

struct SearchableAuctionEntryItem
{
    std::wstring itemName[TOTAL_LOCALES];
    uint32 entry;
    AuctionEntryItemEnchants enchants[MAX_INSPECTED_ENCHANTMENT_SLOT];
    int32 randomPropertyId;
    uint32 suffixFactor;
    uint32 count;
    int32 spellCharges;
    AuctionSearchItemFacts::Facts facts;
};

struct SearchableAuctionEntry
{
    uint32 Id;
    ObjectGuid ownerGuid;
    std::string ownerName;
    uint32 buyout;
    time_t expire_time;
    uint32 startbid;
    uint32 bid;
    ObjectGuid bidderGuid;
    AuctionHouseFaction listFaction;
    SearchableAuctionEntryItem item;
    AuctionVisibility::Tag visibilityTag; // Unknown until an authoritative publication is installed.

    void BuildAuctionInfo(WorldPacket& data, time_t searchTime) const;
    void SetItemNames(ItemTemplate const* proto);

    int CompareAuctionEntry(uint32 column, SearchableAuctionEntry const& auc, int loc_idx) const;
};

using SearchableAuctionCatalog = AuctionCatalogPublication::Catalog<SearchableAuctionEntry, MAX_AUCTION_HOUSE_FACTIONS>;
using SearchableAuctionEntriesMap = SearchableAuctionCatalog::Map;

typedef std::vector<AuctionSortInfo> AuctionSortOrderVector;

struct AuctionHouseSearchInfo
{
    std::wstring wsearchedname;
    uint32 listfrom;
    uint8 levelmin;
    uint8 levelmax;
    bool usable;
    uint32 inventoryType;
    uint32 itemClass;
    uint32 itemSubClass;
    uint32 quality;
    bool getAll;
    AuctionSortOrderVector sorting;
};

typedef std::unordered_map<uint32, uint16> AuctionPlayerSkills;
typedef std::unordered_set<uint32> AuctionPlayerSpells;

struct AuctionHouseUsablePlayerInfo
{
    uint32 classMask;
    uint32 raceMask;
    uint8 level;
    AuctionPlayerSkills skills; // active skills only
    AuctionPlayerSpells spells; // active spells only

    bool PlayerCanUseItem(AuctionSearchItemFacts::Facts const& facts) const;
    uint16 GetSkillValue(uint32 skill) const;
    bool HasSpell(uint32 spell) const;
};

struct AuctionHousePlayerInfo
{
    ObjectGuid playerGuid;
    uint64 sessionIncarnation = 0;
    uint32 faction;
    int loc_idx;
    int locdbc_idx;
    std::optional<AuctionHouseUsablePlayerInfo> usablePlayerInfo;
};

struct AuctionSearcherRequest
{
    enum class Type : uint8
    {
        LIST,
        OWNER_LIST,
        BIDDER_LIST
    };

    AuctionSearcherRequest(Type const _requestType, AuctionHouseFaction _listFaction) : requestType(_requestType), listFaction(_listFaction) { }
    virtual ~AuctionSearcherRequest() = default;

    Type requestType;
    AuctionHouseFaction listFaction;
    std::shared_ptr<SearchableAuctionCatalog::View const> catalog;
    time_t searchTime = 0;
};

struct AuctionSearchListRequest : AuctionSearcherRequest
{
    AuctionSearchListRequest(AuctionHouseFaction _listFaction, AuctionHouseSearchInfo const&& _searchInfo, AuctionHousePlayerInfo const&& _playerInfo)
        : AuctionSearcherRequest(AuctionSearcherRequest::Type::LIST, _listFaction), searchInfo(_searchInfo), playerInfo(_playerInfo) { }

    AuctionHouseSearchInfo searchInfo;
    AuctionHousePlayerInfo playerInfo;
    AuctionBrowseSelection::Context visibility;
    AuctionResponseFence::Stamp stamp;
};

struct AuctionSearchOwnerListRequest : AuctionSearcherRequest
{
    AuctionSearchOwnerListRequest(AuctionHouseFaction _listFaction, ObjectGuid _ownerGuid)
        : AuctionSearcherRequest(AuctionSearcherRequest::Type::OWNER_LIST, _listFaction), ownerGuid(_ownerGuid) { }

    ObjectGuid ownerGuid;
};

struct AuctionSearchBidderListRequest : AuctionSearcherRequest
{
    AuctionSearchBidderListRequest(AuctionHouseFaction _listFaction, std::vector<uint32> const&& _outbiddedAuctionIds, ObjectGuid _ownerGuid)
        : AuctionSearcherRequest(AuctionSearcherRequest::Type::BIDDER_LIST, _listFaction), outbiddedAuctionIds(_outbiddedAuctionIds), ownerGuid(_ownerGuid) { }

    std::vector<uint32> outbiddedAuctionIds;
    ObjectGuid ownerGuid;
};

struct AuctionSearcherResponse
{
    ObjectGuid playerGuid;
    WorldPacket packet;
    bool browse = false;
    AuctionResponseFence::Stamp stamp;
};

typedef std::vector<SearchableAuctionEntry const*> SortableAuctionEntriesList;

class AuctionSorter
{
public:
    AuctionSorter(AuctionSortOrderVector const* sort, int loc_idx) : _sort(sort), _loc_idx(loc_idx) {}
    bool operator()(SearchableAuctionEntry const* auc1, SearchableAuctionEntry const* auc2) const;

private:
    AuctionSortOrderVector const* _sort;
    int _loc_idx;
};

class AuctionHouseWorkerThread
{
public:
    AuctionHouseWorkerThread(ProducerConsumerQueue<AuctionSearcherRequest*>* requestQueue, MPSCQueue<AuctionSearcherResponse>* responseQueue);

    ~AuctionHouseWorkerThread();
    void Stop();

private:
    void Run();

    void ProcessSearchRequests();
    void SearchListRequest(AuctionSearchListRequest const& searchListRequest);
    void SearchOwnerListRequest(AuctionSearchOwnerListRequest const& searchOwnerListRequest);
    void SearchBidderListRequest(AuctionSearchBidderListRequest const& searchBidderListRequest);

    void BuildListAuctionItems(AuctionSearchListRequest const& searchRequest, SortableAuctionEntriesList& auctionEntries, SearchableAuctionEntriesMap const& auctionMap) const;

    ProducerConsumerQueue<AuctionSearcherRequest*>* _requestQueue;
    MPSCQueue<AuctionSearcherResponse>* _responseQueue;

    std::atomic<bool> _stopped{false};
    std::thread _workerThread;
};

class AuctionHouseSearcher
{
public:
    AuctionHouseSearcher();
    ~AuctionHouseSearcher();

    void Update();

    void QueueSearchRequest(AuctionSearcherRequest* searchRequestInfo);

    void AddAuction(AuctionEntry const* auctionEntry);
    void RemoveAuction(AuctionEntry const* auctionEntry);
    void UpdateBid(AuctionEntry const* auctionEntry);

private:
    SearchableAuctionCatalog _catalog;
    // Draft bounded admission: reserve half the total for management requests.
    // Revisit limits with native load evidence before acceptance.
    static constexpr std::size_t MaxOutstanding = 128;
    static constexpr std::size_t MaxBrowseOutstanding = 64;
    std::mutex _admissionMutex;
    std::size_t _outstanding = 0;
    std::size_t _browseOutstanding = 0;
    bool _closing = false;
    ProducerConsumerQueue<AuctionSearcherRequest*> _requestQueue;
    MPSCQueue<AuctionSearcherResponse> _responseQueue;
    std::vector<std::unique_ptr<AuctionHouseWorkerThread>> _workerThreads;
};

#endif
