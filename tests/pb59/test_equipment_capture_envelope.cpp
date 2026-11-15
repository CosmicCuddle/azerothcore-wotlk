#include "EquipmentCaptureEnvelope.h"
#include <cassert>

using namespace PB59Prototype;
constexpr EquipmentCaptureEnvelope valid{1, 42, 7, 3, 2, 60, 0x3, 100, 2, 50, true, false, false, false, true};
static_assert(ValidEnvelope(valid));
static_assert(SameEnvelope(valid, valid));
static_assert(!ValidEnvelope({}));
static_assert(std::popcount(CombatSlotMask) == 17);

int main()
{
    auto changed = valid;
    changed.policyRevision++;
    assert(ValidEnvelope(changed) && !SameEnvelope(valid, changed));
    changed = valid;
    changed.equipmentRevision++;
    assert(!SameEnvelope(valid, changed));
    changed = valid;
    changed.session++;
    assert(!SameEnvelope(valid, changed));
    changed = valid;
    changed.level++;
    assert(!SameEnvelope(valid, changed));
    changed = valid;
    changed.dualWield = true;
    assert(!SameEnvelope(valid, changed));
    changed = valid;
    changed.titanGrip = true;
    assert(!SameEnvelope(valid, changed));
    changed = valid;
    changed.capabilitiesKnown = false;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.unresolvedScaling = true;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.eligibleMask |= 1u << 3;
    ++changed.denominator;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.eligibleMask |= 1u << 18;
    ++changed.denominator;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.denominator = 1;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.rawSum = 101;
    assert(ValidEnvelope(changed) && !SameEnvelope(valid, changed));
    changed.rawSum = 102;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.rawSum = std::numeric_limits<std::uint64_t>::max();
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.rawSum = changed.average = 0;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.schema = 2;
    assert(!ValidEnvelope(changed));
    changed = valid;
    changed.policyRevision = 0;
    assert(!ValidEnvelope(changed));
    // Empty eligible slot contributes zero; removing weak gear cannot raise cap.
    changed = valid;
    changed.rawSum = 60;
    changed.average = 30;
    assert(ValidEnvelope(changed) && !SameEnvelope(valid, changed));
}
