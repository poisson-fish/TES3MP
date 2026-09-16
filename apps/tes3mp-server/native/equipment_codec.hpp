#ifndef TES3MP_NATIVE_EQUIPMENT_CODEC_H
#define TES3MP_NATIVE_EQUIPMENT_CODEC_H

#include <apps/openmw/mwworld/plainequipment.hpp>

#include <array>
#include <span>

namespace MWWorld::Testing
{
    // Test-target-only in-memory equipment format, independent of transfer v4.
    // Trusted caller supplies bindings; matching an actor is not authentication.
    struct EquipmentEnvelope
    {
        std::string mRuntime;
        std::array<unsigned char, 32> mContent{};
        ESM::RefNum mActor;
    };

    struct EquipmentBindings
    {
        const EquipmentEnvelope& mEnvelope;
        const ESMStore& mContent;
        // Already interned TES3 IDs, including semantic owner/soul/key fields.
        // Decode must never intern an unknown name from external bytes.
        std::span<const ESM::RefId> mReferenceIds;
    };

    inline constexpr uint32_t EquipmentFormatVersion = 1;
    // Accommodate every supported combination of 65 items, 256 animations and
    // 4096-byte strings. These limits include stock fields and lossless fields.
    inline constexpr size_t MaxEquipmentBytes = 80 * 1024 * 1024;
    inline constexpr size_t MaxEquipmentObjectBytes = 2 * 1024 * 1024;
    using EquipmentBytes = std::vector<char>;

    // Complete owned output is published by swap only after validation. No
    // file persistence, live installation, effects or borrowed output data.
    void encodeEquipment(const PlainEquipmentValues& input, const EquipmentBindings& bindings, EquipmentBytes& output);
    void decodeEquipment(std::span<const char> bytes, const EquipmentBindings& bindings, PlainEquipmentValues& output);
}

#endif
