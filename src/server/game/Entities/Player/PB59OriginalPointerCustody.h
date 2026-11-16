#pragma once

#include <memory>
#include <utility>

namespace PB59
{
// The serialized native owner retains exactly one owning pointer. These
// operations do not change item state, fields, world links or gameplay effects.
template<class T>
bool TakeOriginalPointer(T*& equipped, std::unique_ptr<T>& held, T* expected) noexcept
{
    if (!expected || equipped != expected || held)
        return false;
    held.reset(std::exchange(equipped, nullptr));
    return true;
}

template<class T>
bool ReturnOriginalPointer(T*& equipped, std::unique_ptr<T>& held, T* expected) noexcept
{
    if (!expected || equipped || held.get() != expected)
        return false;
    equipped = held.release();
    return true;
}

// Link repair may overwrite only the exact original or empty field. A
// third-party GUID or an unexpected slot is not ours to reconcile.
template<class Guid, class Slot>
bool CanRestoreOriginalLinks(Guid slotField, Guid contained, Guid itemGuid,
    Guid ownerGuid, Guid empty, Slot itemSlot, Slot originalSlot, Slot nullSlot) noexcept
{
    return itemGuid != empty && ownerGuid != empty &&
        (slotField == empty || slotField == itemGuid) &&
        (contained == empty || contained == ownerGuid) &&
        (itemSlot == nullSlot || itemSlot == originalSlot);
}
}
