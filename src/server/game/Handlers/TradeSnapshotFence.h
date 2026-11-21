#pragma once
// Review-only, readback admission; no DB writes, item transfers or trade completion.
#include <cstdint>
namespace CosmicTrade {
enum class SnapshotCheck : std::uint8_t { Reject, HoldForReadback, RecoveryRequired };
struct OwnerSnapshot {
    std::uint32_t version = 0;
    std::uint64_t tradeId = 0;
    std::uint32_t realm = 0;
    std::uint64_t owner = 0;
    std::uint64_t peerOwner = 0;
    std::uint32_t peerRealm = 0;
    std::uint64_t payloadHash = 0;
    std::uint64_t revision = 0;
};
struct DurableSnapshotReadback {
    OwnerSnapshot snapshot{};
    bool persisted = false;
    bool prepared = false;
};
// Validate an immutable two-owner, two-realm snapshot against two independent
// persisted PREPARED readbacks. No positive result can authorize TRADE_COMPLETE.
inline SnapshotCheck VerifyPreparedPair(OwnerSnapshot const& a, OwnerSnapshot const& b,
    DurableSnapshotReadback const& ra, DurableSnapshotReadback const& rb) noexcept {
    if (a.version != 1 || b.version != 1 || !a.tradeId || a.tradeId != b.tradeId ||
        !a.realm || !b.realm || a.realm == b.realm || !a.owner || !b.owner ||
        a.owner == b.owner || !a.payloadHash || !b.payloadHash || !a.revision || !b.revision ||
        a.peerOwner != b.owner || b.peerOwner != a.owner ||
        a.peerRealm != b.realm || b.peerRealm != a.realm)
        return SnapshotCheck::Reject;
    if (!ra.persisted || !rb.persisted) return SnapshotCheck::HoldForReadback;
    auto equal = [](OwnerSnapshot const& x, OwnerSnapshot const& y) noexcept {
        return x.version == y.version && x.tradeId == y.tradeId && x.realm == y.realm &&
            x.owner == y.owner && x.peerOwner == y.peerOwner && x.peerRealm == y.peerRealm &&
            x.payloadHash == y.payloadHash && x.revision == y.revision;
    };
    if (!equal(a, ra.snapshot) || !equal(b, rb.snapshot) || !ra.prepared || !rb.prepared)
        return SnapshotCheck::RecoveryRequired;
    return SnapshotCheck::HoldForReadback; // PREPARED is never a grant or completion receipt.
}
}
