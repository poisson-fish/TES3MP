#ifndef TES3MP_NATIVE_EQUIPMENT_SESSION_H
#define TES3MP_NATIVE_EQUIPMENT_SESSION_H

#include "equipment_codec.hpp"

namespace TES3MP::Native
{
    // One atomic file contains both actors and, when bound, the shared container.
    // All use the existing stock field codec, one counter and registry revision.
    // None of these images is independently durable in a connected session.
    inline constexpr size_t MaxEquipmentSessionBytes = 3 * MaxEquipmentBytes + 40;
    struct EquipmentSessionValues
    {
        std::array<PlainEquipmentValues, 2> mActors;
        uint64_t mRevision = 0;
        std::optional<PlainEquipmentValues> mContainer;
        void swap(EquipmentSessionValues& other) noexcept
        {
            for (size_t i = 0; i < 2; ++i) mActors[i].swap(other.mActors[i]);
            std::swap(mRevision, other.mRevision);
            mContainer.swap(other.mContainer);
        }
    };
    void encodeEquipmentSession(const EquipmentSessionValues& values,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output,
        const EquipmentBindings* container = nullptr);
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output,
        const EquipmentBindings* container = nullptr);
}
#endif
