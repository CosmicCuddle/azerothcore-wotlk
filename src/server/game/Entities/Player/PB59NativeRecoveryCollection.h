#pragma once

#include "EquipmentCaptureState.h"
#include "PB59RecoveryEvidence.h"
#include "PB59RecoveryUpdateObservation.h"
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
struct QueueEntry
{
    std::uint64_t guid = 0, owner = 0;
    std::int32_t position = -1;
    bool present = false;
    std::uint32_t itemState = 0;
    ObjectUpdateFacts<MaximumItemUpdateFields> updates;
    bool operator==(QueueEntry const&) const = default;
};
struct QueueDifference
{
    std::size_t index = 0;
    QueueEntry before, now;
    bool sameNativeInstance = false;
};
struct CollectionSummary
{
    CollectionStatus status = CollectionStatus::Prepared;
    bool ticketBound = false, revisionObserved = false, intentsObserved = false;
    bool queueObserved = false, queueConsistent = false, journalsObserved = false;
    bool queueBeforeObserved = false, queueCompared = false, queueUnchanged = false;
    bool ownerUpdateObserved = false, ownerUpdateConsistent = false, ownerUpdateUnchanged = false;
    std::size_t preparedQueueEntries = 0, observedQueueEntries = 0;
    std::size_t changedQueueEntries = 0;
    std::uint8_t queueDifferenceCount = 0;
    std::uint32_t nonFreshJournalMask = 0, uncertainJournalMask = 0;
};
struct CollectedValues
{
    Evidence evidence;
    CollectionSummary summary;
    std::array<DestinationObservation, MaximumOriginals> destinations{};
    // All indices are compared; only the first MaximumOriginals differences are
    // exported. changedQueueEntries retains the total, including truncation.
    std::array<QueueDifference, MaximumOriginals> queueDifferences{};
    OwnerUpdateFacts ownerUpdateBefore, ownerUpdateNow;
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
    struct NativeQueueEntry
    {
        Item const* item = nullptr; // compare only; never dereference this saved pointer during recovery
        QueueEntry value;
    };
    std::array<NativeQueueEntry, MaximumQueueObservations> _queueBefore{};
    CollectedValues _values;
};
static_assert(std::is_trivially_copyable_v<CollectedValues>);
static_assert(sizeof(NativeCollection) <= 512 * 1024);
}
