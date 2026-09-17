#include "equipment_session.hpp"

#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t Magic = 0x3152494150335354; // T S 3 P A I R 1
        void validate(const EquipmentSessionValues& values, const std::array<EquipmentBindings, 2>& bindings)
        {
            const auto& a = values.mActors[0];
            const auto& b = values.mActors[1];
            if (values.mRevision == 0 || values.mRevision >= std::numeric_limits<size_t>::max()
                || a.mLastGenerated != b.mLastGenerated || a.mActor == b.mActor
                || bindings[0].mEnvelope.mRuntime != bindings[1].mEnvelope.mRuntime
                || bindings[0].mEnvelope.mContent != bindings[1].mEnvelope.mContent
                || &bindings[0].mContent != &bindings[1].mContent)
                throw std::invalid_argument("Equipment session revision, owner or shared content/counter mismatch");
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& actor = values.mActors[i];
                actor.validate(bindings[i].mContent, bindings[i].mEnvelope.mActor, bindings[i].mScriptLocals.get());
                for (const auto& object : actor.mObjects)
                {
                    const auto id = object.mRef.mRefNum;
                    if (id == a.mActor || id == b.mActor)
                        throw std::invalid_argument("Equipment session item collides with actor");
                    for (const auto& other : values.mActors[1 - i].mObjects)
                        if (other.mRef.mRefNum == id)
                            throw std::invalid_argument("Equipment session duplicate item identity");
                }
            }
        }
        void put(EquipmentBytes& bytes, uint64_t value)
        {
            for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<char>(value >> (8 * i)));
        }
        uint64_t get(std::span<const char> bytes, size_t offset)
        {
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i) value |= uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
            return value;
        }
    }
    void encodeEquipmentSession(const EquipmentSessionValues& values,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output)
    {
        validate(values, bindings);
        std::array<EquipmentBytes, 2> actors;
        for (size_t i = 0; i < 2; ++i) encodeEquipment(values.mActors[i], bindings[i], actors[i]);
        EquipmentBytes staged;
        staged.reserve(32 + actors[0].size() + actors[1].size());
        put(staged, Magic);
        put(staged, values.mRevision);
        put(staged, actors[0].size());
        put(staged, actors[1].size());
        for (const auto& actor : actors) staged.insert(staged.end(), actor.begin(), actor.end());
        output.swap(staged);
    }
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output)
    {
        if (bytes.size() < 32 || bytes.size() > MaxEquipmentSessionBytes || get(bytes, 0) != Magic)
            throw std::invalid_argument("Invalid equipment session header");
        const auto a = get(bytes, 16), b = get(bytes, 24);
        if (a == 0 || b == 0 || a > MaxEquipmentBytes || b > MaxEquipmentBytes || a + b != bytes.size() - 32)
            throw std::invalid_argument("Invalid equipment session lengths");
        EquipmentSessionValues staged;
        staged.mRevision = get(bytes, 8);
        if (staged.mRevision == 0 || staged.mRevision >= std::numeric_limits<size_t>::max())
            throw std::invalid_argument("Invalid equipment session revision");
        // Each existing codec preflights bounded stock fields and trusted IDs
        // before allocating its image. Neither candidate is installed here.
        decodeEquipment(bytes.subspan(32, a), bindings[0], staged.mActors[0]);
        decodeEquipment(bytes.subspan(32 + a, b), bindings[1], staged.mActors[1]);
        validate(staged, bindings);
        output.swap(staged);
    }
}
