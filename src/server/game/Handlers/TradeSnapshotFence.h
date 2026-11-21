#pragma once
// Review-only snapshot verification. NOT a trusted DB readback adapter or settlement grant.
#include <cstdint>
namespace CosmicTrade {
enum class SnapshotCheck : std::uint8_t { Reject, HoldForReadback, RecoveryRequired, PreparedVerified };
struct OwnerSnapshot {
    std::uint32_t version = 0;
    std::uint64_t tradeId = 0;
    std::uint32_t realm = 0;
    std::uint64_t owner = 0; // Realm-local low GUID: may equal the peer's low GUID.
    std::uint64_t peerOwner = 0;
    std::uint32_t peerRealm = 0;
    std::uint64_t payloadHash = 0;
    std::uint64_t peerPayloadHash = 0; // Bind BOTH offered item/gold payloads.
    std::uint64_t revision = 0;
};
struct DurableSnapshotReadback {
    OwnerSnapshot snapshot{};
    bool persisted = false; // Caller assertion, NOT proof of a committed MariaDB SELECT.
    bool prepared = false;
    std::uint32_t queriedRealm = 0;
    std::uint64_t queriedOwner = 0;
    std::uint64_t journalSequence = 0;
    // Values the adapter claims it selected from the same realm-local journal row.
    // Still UNTRUSTED until the native adapter proves the SELECT's backend/transaction.
    std::uint64_t journalTradeId = 0;
    std::uint32_t journalRealm = 0;
    std::uint64_t journalPayloadHash = 0;
    std::uint64_t journalRevision = 0;
    std::uint8_t journalKind = 0; // Explicitly 1 = PREPARED in this test-only wire contract.
};
// The caller MUST prove that queriedRealm/Owner/sequence were returned by an
// independently authenticated realm-specific, GUID-parameterised SQL readback.
// All journal fields must originate in one coherent PREPARED row; copying a struct
// or supplying plausible values is not an authenticated readback.
// This pure function cannot authenticate a fabricated DurableSnapshotReadback.
// PreparedVerified is NOT permission to debit, grant, refund or TRADE_COMPLETE.
inline SnapshotCheck VerifyPreparedPair(OwnerSnapshot const& a, OwnerSnapshot const& b,
    DurableSnapshotReadback const& ra, DurableSnapshotReadback const& rb) noexcept {
    if (a.version != 1 || b.version != 1 || !a.tradeId || a.tradeId != b.tradeId ||
        !a.realm || !b.realm || a.realm == b.realm || !a.owner || !b.owner ||
        !a.payloadHash || !b.payloadHash || !a.peerPayloadHash || !b.peerPayloadHash ||
        !a.revision || !b.revision ||
        a.peerOwner != b.owner || b.peerOwner != a.owner ||
        a.peerRealm != b.realm || b.peerRealm != a.realm ||
        a.peerPayloadHash != b.payloadHash || b.peerPayloadHash != a.payloadHash)
        return SnapshotCheck::Reject;
    if (!ra.persisted || !rb.persisted) return SnapshotCheck::HoldForReadback;
    auto equal = [](OwnerSnapshot const& x, OwnerSnapshot const& y) noexcept {
        return x.version == y.version && x.tradeId == y.tradeId && x.realm == y.realm &&
            x.owner == y.owner && x.peerOwner == y.peerOwner && x.peerRealm == y.peerRealm &&
            x.payloadHash == y.payloadHash && x.peerPayloadHash == y.peerPayloadHash &&
            x.revision == y.revision;
    };
    if (!equal(a, ra.snapshot) || !equal(b, rb.snapshot) || !ra.prepared || !rb.prepared ||
        ra.queriedRealm != a.realm || rb.queriedRealm != b.realm ||
        ra.queriedOwner != a.owner || rb.queriedOwner != b.owner ||
        !ra.journalSequence || !rb.journalSequence ||
        ra.journalTradeId != a.tradeId || rb.journalTradeId != b.tradeId ||
        ra.journalRealm != a.realm || rb.journalRealm != b.realm ||
        ra.journalPayloadHash != a.payloadHash || rb.journalPayloadHash != b.payloadHash ||
        ra.journalRevision != a.revision || rb.journalRevision != b.revision ||
        ra.journalKind != 1 || rb.journalKind != 1)
        return SnapshotCheck::RecoveryRequired;
    return SnapshotCheck::PreparedVerified; // Never TRADE_COMPLETE.
}
}
