#ifndef TES3MP_NATIVE_EQUIPMENT_SESSION_H
#define TES3MP_NATIVE_EQUIPMENT_SESSION_H

#include "equipment_codec.hpp"

namespace TES3MP::Native
{
    // One atomic file contains both stock field images and the shared registry
    // revision. Actor blobs are never independently durable in a session.
    inline constexpr size_t MaxEquipmentSessionBytes = 2 * MaxEquipmentBytes + 32;
    struct EquipmentSessionValues
    {
        std::array<PlainEquipmentValues, 2> mActors;
        uint64_t mRevision = 0;
        void swap(EquipmentSessionValues& other) noexcept
        {
            for (size_t i = 0; i < 2; ++i) mActors[i].swap(other.mActors[i]);
            std::swap(mRevision, other.mRevision);
        }
    };
    void encodeEquipmentSession(const EquipmentSessionValues& values,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output);
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output);
}
#endif
