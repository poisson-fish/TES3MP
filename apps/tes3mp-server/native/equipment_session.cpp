#include "equipment_session.hpp"

#include <limits>
#include <set>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t PairMagic = 0x3152494150335354; // TS3PAIR1: existing pair images remain valid.
        constexpr uint64_t SharedMagic = 0x3253534553335354; // TS3SESS2: two actors and one required container.
        constexpr uint64_t CellMagic = 0x3353534553335354; // TS3SESS3: bounded shared inventories.
        uint64_t magic(size_t count) { return count == 0 ? PairMagic : count == 1 ? SharedMagic : CellMagic; }
        size_t headerSize(size_t count) { return 32 + 8 * count + (count > 1 ? 8 : 0); }
        void validate(const EquipmentSessionValues& values, const std::array<EquipmentBindings, 2>& bindings,
            std::span<const EquipmentBindings> containers)
        {
            if (values.mRevision == 0 || values.mRevision >= std::numeric_limits<size_t>::max()
                || containers.size() > MaxEquipmentContainers || values.mContainers.size() != containers.size())
                throw std::invalid_argument("Equipment session revision or container binding mismatch");
            std::set<ESM::RefNum> identities;
            for (size_t i = 0; i < 2 + containers.size(); ++i)
            {
                const auto& image = i < 2 ? values.mActors[i] : values.mContainers[i - 2];
                const auto& binding = i < 2 ? bindings[i] : containers[i - 2];
                if (image.mLastGenerated != values.mActors[0].mLastGenerated)
                    throw std::invalid_argument("Equipment session counter mismatch");
                if (binding.mEnvelope.mRuntime != bindings[0].mEnvelope.mRuntime
                    || binding.mEnvelope.mContent != bindings[0].mEnvelope.mContent
                    || &binding.mContent != &bindings[0].mContent)
                    throw std::invalid_argument("Equipment session shared content mismatch");
                if (i >= 2 && (std::any_of(image.mSlots.begin(), image.mSlots.end(), [](auto id) { return id.isSet(); }) || image.mSelected.isSet() || image.mNpcStats))
                    throw std::invalid_argument("Equipment session container has equipment or stats");
                image.validate(binding.mContent, binding.mEnvelope.mActor, binding.mScriptLocals.get());
                if (!identities.insert(image.mActor).second)
                    throw std::invalid_argument("Equipment session duplicate owner or item identity");
                for (const auto& object : image.mObjects)
                    if (!identities.insert(object.mRef.mRefNum).second)
                        throw std::invalid_argument("Equipment session duplicate owner or item identity");
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
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output,
        std::span<const EquipmentBindings> containers)
    {
        validate(values, bindings, containers);
        std::vector<EquipmentBytes> owners(2 + containers.size());
        size_t size = headerSize(containers.size());
        for (size_t i = 0; i < owners.size(); ++i)
        {
            encodeEquipment(i < 2 ? values.mActors[i] : values.mContainers[i - 2],
                i < 2 ? bindings[i] : containers[i - 2], owners[i]);
            if (owners[i].size() > MaxEquipmentSessionBytes - size)
                throw std::invalid_argument("Equipment session image budget exceeded");
            size += owners[i].size();
        }
        EquipmentBytes staged;
        staged.reserve(size);
        put(staged, magic(containers.size()));
        put(staged, values.mRevision);
        if (containers.size() > 1) put(staged, containers.size());
        for (const auto& owner : owners) put(staged, owner.size());
        for (const auto& owner : owners) staged.insert(staged.end(), owner.begin(), owner.end());
        output.swap(staged);
    }
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output,
        std::span<const EquipmentBindings> containers)
    {
        if (containers.size() > MaxEquipmentContainers)
            throw std::invalid_argument("Equipment session container budget exceeded");
        const auto header = headerSize(containers.size());
        if (bytes.size() < header || bytes.size() > MaxEquipmentSessionBytes
            || get(bytes, 0) != magic(containers.size())
            || (containers.size() > 1 && get(bytes, 16) != containers.size()))
            throw std::invalid_argument("Invalid equipment session header or container count");
        // Preflight every length before decoding or allocating owner images.
        std::array<size_t, 2 + MaxEquipmentContainers> lengths{};
        size_t remaining = bytes.size() - header;
        for (size_t i = 0; i < 2 + containers.size(); ++i)
        {
            const auto size = get(bytes, 16 + (containers.size() > 1 ? 8 : 0) + 8 * i);
            if (!size || size > MaxEquipmentBytes || size > remaining)
                throw std::invalid_argument("Invalid equipment session lengths");
            lengths[i] = size;
            remaining -= size;
        }
        if (remaining) throw std::invalid_argument("Invalid equipment session trailing bytes");
        EquipmentSessionValues staged;
        staged.mRevision = get(bytes, 8);
        if (staged.mRevision == 0 || staged.mRevision >= std::numeric_limits<size_t>::max())
            throw std::invalid_argument("Invalid equipment session revision");
        staged.mContainers.resize(containers.size());
        size_t offset = header;
        for (size_t i = 0; i < 2 + containers.size(); ++i)
        {
            decodeEquipment(bytes.subspan(offset, lengths[i]), i < 2 ? bindings[i] : containers[i - 2],
                i < 2 ? staged.mActors[i] : staged.mContainers[i - 2]);
            offset += lengths[i];
        }
        validate(staged, bindings, containers);
        output.swap(staged);
    }
}
