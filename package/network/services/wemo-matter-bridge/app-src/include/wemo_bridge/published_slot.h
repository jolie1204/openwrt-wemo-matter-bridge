#pragma once
namespace wemo_bridge {
// Used by publication failure and event dispatch: an identity is usable only
// while its device exists. Clearing the whole slot also clears stale flags.
template<class Slot> void ClearUnpublishedSlot(Slot &slot) { slot = Slot{}; }
template<class Slot> bool PublishedSlotMatches(const Slot &slot, int id)
{
    return slot.device && slot.wemo_id == id;
}
}
