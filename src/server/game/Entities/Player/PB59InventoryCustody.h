#pragma once

namespace PB59
{
enum class LinkTransferResult { Rejected, Linked, RecoveryRequired };

// Serialized owner context ONLY. No callbacks, allocation, or ownership
// destruction between the two writes. The target owns the pointer before
// the source is cleared; no observer may run in this tiny interval.
// This does not update native fields, effects, or database state.
template<class T>
bool RelocateInventoryPointer(T*& source, T*& destination, T* expected) noexcept
{
    if (!expected || &source == &destination || source != expected || destination)
        return false;
    destination = source;
    source = nullptr;
    return true;
}
}
