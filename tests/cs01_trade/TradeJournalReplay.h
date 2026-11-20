#pragma once
#include "TradeSettlementGate.h"
#include <cstdint>

// Replay-only contract. Caller must authenticate and durably store each event
// before invoking Apply. This cannot itself provide a DB transaction or custody.
namespace CosmicTrade {
enum class EventKind : std::uint8_t { Prepare, Commit, Refund, Conflict };
enum class ApplyResult : std::uint8_t { Applied, Duplicate, Rejected, ManualRecovery };
struct JournalEvent {
    Receipt identity{};
    EventKind kind = EventKind::Prepare;
    std::uint64_t sequence = 0;
};
class JournalReplay {
public:
    ApplyResult Apply(JournalEvent const& e) noexcept {
        if (!e.sequence || !e.identity.tradeId || !e.identity.realm || !e.identity.owner || !e.identity.payloadHash ||
            e.identity.durable || e.identity.state != SideState::None) return ApplyResult::Rejected;
        int slot = -1;
        for (int i = 0; i < 2; ++i)
            if (sides_[i].realm == e.identity.realm) slot = i;
        if (slot < 0) {
            if (count_ == 2) return ApplyResult::ManualRecovery;
            // An invalid first event must not reserve a realm slot.
            if (e.kind != EventKind::Prepare) return ApplyResult::Rejected;
            slot = count_++;
            sides_[slot] = e.identity;
        }
        Receipt& s = sides_[slot];
        if (s.tradeId != e.identity.tradeId || s.owner != e.identity.owner || s.payloadHash != e.identity.payloadHash)
            return ApplyResult::ManualRecovery;
        if (e.sequence < sequence_[slot]) return ApplyResult::ManualRecovery;
        SideState next = SideState::None;
        switch (e.kind) {
            case EventKind::Prepare: next = SideState::Prepared; break;
            case EventKind::Commit: next = SideState::Committed; break;
            case EventKind::Refund: next = SideState::Refunded; break;
            case EventKind::Conflict: next = SideState::Conflict; break;
        }
        if (e.sequence == sequence_[slot])
            return next == s.state ? ApplyResult::Duplicate : ApplyResult::ManualRecovery;
        if (sequence_[slot] == 0 && next != SideState::Prepared) return ApplyResult::Rejected;
        if (s.state == SideState::Prepared && next == SideState::Prepared) return ApplyResult::Rejected;
        if (s.state == SideState::Committed && next != SideState::Committed) return ApplyResult::ManualRecovery;
        if (s.state == SideState::Refunded || s.state == SideState::Conflict) return ApplyResult::ManualRecovery;
        if (s.state == SideState::Committed && next == SideState::Committed) return ApplyResult::Rejected;
        s.state = next;
        s.durable = true; // represents a verified journal event supplied by the adapter, not a DB proof
        sequence_[slot] = e.sequence;
        return ApplyResult::Applied;
    }
    Decision Status() const noexcept {
        if (count_ != 2) return Decision::Hold;
        return Decide(sides_[0], sides_[1]);
    }
    Receipt const& Side(unsigned i) const noexcept { return sides_[i]; }
private:
    Receipt sides_[2]{};
    std::uint64_t sequence_[2]{};
    int count_ = 0;
};
}
