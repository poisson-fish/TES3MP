#ifndef TES3MP_NATIVE_EQUIPMENT_SESSION_H
#define TES3MP_NATIVE_EQUIPMENT_SESSION_H

#include "equipment_codec.hpp"
#include "door_codec.hpp"
#include <map>

namespace TES3MP::Native
{
    // One atomic file contains both actors and all bound shared containers.
    // All use the existing stock field codec, one counter and registry revision.
    // None of these images is independently durable in a connected session.
    inline constexpr size_t MaxEquipmentSessionBytes = 3 * MaxEquipmentBytes + 40;
    inline constexpr size_t MaxEquipmentContainers = 32;
    struct EquipmentSessionValues
    {
        std::array<PlainEquipmentValues, 2> mActors;
        uint64_t mRevision = 0;
        std::vector<PlainEquipmentValues> mContainers;
        // Complete active world membership; absent placed references stay removed.
        // Uses the first actor solely as the field-codec envelope anchor.
        std::optional<PlainEquipmentValues> mWorldItems;
        // Immutable detached stock state; the runtime installs it with the
        // other owners. No independently durable door file or live engine Ptr.
        std::shared_ptr<const ESM::DoorState> mDoor;
        // V10's two interiors share the registry/image. Membership is keyed by
        // reference identity, never object order or a live CellStore pointer.
        using WorldCells = std::map<ESM::RefNum, uint8_t>;
        std::optional<WorldCells> mWorldCells;
        void swap(EquipmentSessionValues& other) noexcept
        {
            for (size_t i = 0; i < 2; ++i) mActors[i].swap(other.mActors[i]);
            std::swap(mRevision, other.mRevision);
            mContainers.swap(other.mContainers);
            mWorldItems.swap(other.mWorldItems);
            mDoor.swap(other.mDoor);
            mWorldCells.swap(other.mWorldCells);
        }
    };
    void encodeEquipmentSession(const EquipmentSessionValues& values,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output,
        std::span<const EquipmentBindings> containers = {}, const EquipmentBindings* world = nullptr,
        const DoorBinding* door = nullptr, bool cells = false);
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output,
        std::span<const EquipmentBindings> containers = {}, const EquipmentBindings* world = nullptr,
        const DoorBinding* door = nullptr, bool cells = false);
}
#endif
