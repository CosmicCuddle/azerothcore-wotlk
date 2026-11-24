#pragma once

#include "EquipmentCaptureState.h"
#include "PB59RecoveryEvidence.h"
#include <memory>
#include <thread>

class Player;
class Item;
class Map;

namespace PB59::Recovery
{
constexpr std::size_t MaximumQueueObservations = 4096;
enum class CollectionStatus
{
    Prepared, Bound, Collected, Incomplete, OwnerUnavailable, ContextUnavailable,
    WrongScope, WrongPhase, WrongThread, InvalidInput
};
struct DestinationObservation
{
    SlotFacts before;
    bool observed = false;
    SlotFacts now;
};
struct CollectionSummary
{
    CollectionStatus status = CollectionStatus::Prepared;
    bool ticketBound = false, revisionObserved = false, intentsObserved = false;
    bool queueObserved = false, queueConsistent = false, journalsObserved = false;
    std::size_t preparedQueueEntries = 0, observedQueueEntries = 0;
    std::uint32_t nonFreshJournalMask = 0, uncertainJournalMask = 0;
};
struct CollectedValues
{
    Evidence evidence;
    CollectionSummary summary;
    std::array<DestinationObservation, MaximumOriginals> destinations{};
};

// Synchronous native scratch only. Borrowed pointers are private, never exported
// in CollectedValues and never retained across a tick/asynchronous dispatch.
// Owns pre-reserved value storage, NOT Player/Item lifetime or mutation authority.
class NativeCollection final
{
public:
    NativeCollection(NativeCollection const&) = delete;
    NativeCollection& operator=(NativeCollection const&) = delete;
    CollectedValues CopyValues() const noexcept { return _values; }
private:
    friend class ::Player;
    NativeCollection() = default;
    enum class Phase { Prepared, Bound, Terminal };
    Phase _phase = Phase::Prepared;
    std::thread::id _thread;
    Player const* _owner = nullptr;
    Map const* _map = nullptr;
    PB59::Stamp _preparedStamp{}, _boundStamp{};
    std::array<Item const*, MaximumOriginals> _originals{};
    CollectedValues _values;
};
static_assert(std::is_trivially_copyable_v<CollectedValues>);
static_assert(sizeof(NativeCollection) <= 96 * 1024);
}
