#include "equipment_session.hpp"

#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t PairMagic = 0x3152494150335354; // TS3PAIR1: existing pair images remain valid.
        constexpr uint64_t SharedMagic = 0x3253534553335354; // TS3SESS2: two actors and one required container.
        void validate(const EquipmentSessionValues& values, const std::array<EquipmentBindings, 2>& bindings,
            const EquipmentBindings* container)
        {
            if (values.mRevision == 0 || values.mRevision >= std::numeric_limits<size_t>::max()
                || values.mContainer.has_value() != (container != nullptr))
                throw std::invalid_argument("Equipment session revision or container binding mismatch");
            const std::array images{ &values.mActors[0], &values.mActors[1],
                values.mContainer ? &*values.mContainer : nullptr };
            const std::array contexts{ &bindings[0], &bindings[1], container };
            for (size_t i = 0; i < (container ? 3 : 2); ++i)
            {
                const auto& image = *images[i];
                const auto& binding = *contexts[i];
                if (image.mLastGenerated != images[0]->mLastGenerated)
                    throw std::invalid_argument("Equipment session counter mismatch");
                if (binding.mEnvelope.mRuntime != bindings[0].mEnvelope.mRuntime
                    || binding.mEnvelope.mContent != bindings[0].mEnvelope.mContent
                    || &binding.mContent != &bindings[0].mContent)
                    throw std::invalid_argument("Equipment session shared content mismatch");
                if (i == 2 && (image.mShirt.isSet() || image.mSelected.isSet() || image.mNpcStats))
                    throw std::invalid_argument("Equipment session container has equipment or stats");
                image.validate(binding.mContent, binding.mEnvelope.mActor, binding.mScriptLocals.get());
                for (size_t j = 0; j < (container ? 3 : 2); ++j)
                {
                    if (i != j && image.mActor == images[j]->mActor)
                        throw std::invalid_argument("Equipment session duplicate owner identity");
                    for (const auto& object : image.mObjects)
                    {
                        const auto id = object.mRef.mRefNum;
                        if (id == images[j]->mActor)
                            throw std::invalid_argument("Equipment session item collides with owner");
                        if (i != j)
                            for (const auto& other : images[j]->mObjects)
                                if (other.mRef.mRefNum == id)
                                    throw std::invalid_argument("Equipment session duplicate item identity");
                    }
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
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output, const EquipmentBindings* container)
    {
        validate(values, bindings, container);
        std::array<EquipmentBytes, 3> actors;
        for (size_t i = 0; i < 2; ++i) encodeEquipment(values.mActors[i], bindings[i], actors[i]);
        if (container) encodeEquipment(*values.mContainer, *container, actors[2]);
        EquipmentBytes staged;
        staged.reserve((container ? 40 : 32) + actors[0].size() + actors[1].size() + actors[2].size());
        put(staged, container ? SharedMagic : PairMagic);
        put(staged, values.mRevision);
        put(staged, actors[0].size());
        put(staged, actors[1].size());
        if (container) put(staged, actors[2].size());
        for (const auto& actor : actors) staged.insert(staged.end(), actor.begin(), actor.end());
        output.swap(staged);
    }
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output, const EquipmentBindings* container)
    {
        const size_t header = container ? 40 : 32;
        if (bytes.size() < header || bytes.size() > MaxEquipmentSessionBytes
            || get(bytes, 0) != (container ? SharedMagic : PairMagic))
            throw std::invalid_argument("Invalid equipment session header");
        const auto a = get(bytes, 16), b = get(bytes, 24), c = container ? get(bytes, 32) : 0;
        if (a == 0 || b == 0 || a > MaxEquipmentBytes || b > MaxEquipmentBytes || c > MaxEquipmentBytes || (c != 0) != (container != nullptr)
            || a + b + c != bytes.size() - header)
            throw std::invalid_argument("Invalid equipment session lengths");
        EquipmentSessionValues staged;
        staged.mRevision = get(bytes, 8);
        if (staged.mRevision == 0 || staged.mRevision >= std::numeric_limits<size_t>::max())
            throw std::invalid_argument("Invalid equipment session revision");
        // Each existing codec preflights bounded stock fields and trusted IDs
        // before allocating its image. Neither candidate is installed here.
        decodeEquipment(bytes.subspan(header, a), bindings[0], staged.mActors[0]);
        decodeEquipment(bytes.subspan(header + a, b), bindings[1], staged.mActors[1]);
        if (container)
        {
            staged.mContainer.emplace();
            decodeEquipment(bytes.subspan(header + a + b, c), *container, *staged.mContainer);
        }
        validate(staged, bindings, container);
        output.swap(staged);
    }
}
