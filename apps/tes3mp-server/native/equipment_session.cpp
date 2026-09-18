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
        constexpr uint64_t WorldMagic = 0x3453534553335354; // TS3SESS4: coherent world items.
        constexpr uint64_t DoorMagic = 0x3553534553335354; // TS3SESS5: world items plus one bound door.
        uint64_t magic(size_t count, bool world, bool door) { return door ? DoorMagic : world ? WorldMagic : count == 0 ? PairMagic : count == 1 ? SharedMagic : CellMagic; }
        size_t headerSize(size_t count, bool world, bool door) { return 32 + 8 * (count + world + door) + (count > 1 || world ? 8 : 0); }
        void validate(const EquipmentSessionValues& values, const std::array<EquipmentBindings, 2>& bindings,
            std::span<const EquipmentBindings> containers, const EquipmentBindings* world, const DoorBinding* door)
        {
            if (values.mRevision == 0 || values.mRevision >= std::numeric_limits<size_t>::max()
                || bool(values.mWorldItems) != bool(world)
                || bool(values.mDoor) != bool(door) || (door && !world)
                || containers.size() > MaxEquipmentContainers || values.mContainers.size() != containers.size())
                throw std::invalid_argument("Equipment session revision or container binding mismatch");
            std::set<ESM::RefNum> identities;
            if (door)
            {
                door->door().validate(*values.mDoor);
                identities.insert(values.mDoor->mRef.mRefNum);
            }
            for (size_t i = 0; i < 2 + containers.size() + (world != nullptr); ++i)
            {
                const bool ground = i == 2 + containers.size();
                const auto& image = ground ? *values.mWorldItems : i < 2 ? values.mActors[i] : values.mContainers[i - 2];
                const auto& binding = ground ? *world : i < 2 ? bindings[i] : containers[i - 2];
                if (image.mLastGenerated != values.mActors[0].mLastGenerated)
                    throw std::invalid_argument("Equipment session counter mismatch");
                if (binding.mEnvelope.mRuntime != bindings[0].mEnvelope.mRuntime
                    || binding.mEnvelope.mContent != bindings[0].mEnvelope.mContent
                    || &binding.mContent != &bindings[0].mContent)
                    throw std::invalid_argument("Equipment session shared content mismatch");
                if (i >= 2 && (image.mNpcStats || (!binding.mInventory
                    && (std::any_of(image.mSlots.begin(), image.mSlots.end(), [](auto id) { return id.isSet(); }) || image.mSelected.isSet()))))
                    throw std::invalid_argument("Equipment session container has equipment or stats");
                image.validate(binding.mContent, binding.mEnvelope.mActor, binding.mScriptLocals.get());
                if (!ground && !identities.insert(image.mActor).second)
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
        std::span<const EquipmentBindings> containers, const EquipmentBindings* world, const DoorBinding* door)
    {
        validate(values, bindings, containers, world, door);
        std::vector<EquipmentBytes> owners(2 + containers.size() + (world != nullptr) + (door != nullptr));
        size_t size = headerSize(containers.size(), world != nullptr, door != nullptr);
        for (size_t i = 0; i < owners.size(); ++i)
        {
            const bool ground = i == 2 + containers.size();
            if (door && i == owners.size() - 1) encodeDoor(*values.mDoor, *door, owners[i]);
            else encodeEquipment(ground ? *values.mWorldItems : i < 2 ? values.mActors[i] : values.mContainers[i - 2],
                    ground ? *world : i < 2 ? bindings[i] : containers[i - 2], owners[i]);
            if (owners[i].size() > MaxEquipmentSessionBytes - size)
                throw std::invalid_argument("Equipment session image budget exceeded");
            size += owners[i].size();
        }
        EquipmentBytes staged;
        staged.reserve(size);
        put(staged, magic(containers.size(), world != nullptr, door != nullptr));
        put(staged, values.mRevision);
        if (containers.size() > 1 || world) put(staged, containers.size());
        for (const auto& owner : owners) put(staged, owner.size());
        for (const auto& owner : owners) staged.insert(staged.end(), owner.begin(), owner.end());
        output.swap(staged);
    }
    void decodeEquipmentSession(std::span<const char> bytes,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentSessionValues& output,
        std::span<const EquipmentBindings> containers, const EquipmentBindings* world, const DoorBinding* door)
    {
        if (containers.size() > MaxEquipmentContainers || (door && !world))
            throw std::invalid_argument("Equipment session container budget exceeded");
        const auto header = headerSize(containers.size(), world != nullptr, door != nullptr);
        if (bytes.size() < header || bytes.size() > MaxEquipmentSessionBytes
            || get(bytes, 0) != magic(containers.size(), world != nullptr, door != nullptr)
            || ((containers.size() > 1 || world) && get(bytes, 16) != containers.size()))
            throw std::invalid_argument("Invalid equipment session header or container count");
        // Preflight every length before decoding or allocating owner images.
        std::array<size_t, 4 + MaxEquipmentContainers> lengths{};
        size_t remaining = bytes.size() - header;
        const size_t count = 2 + containers.size() + (world != nullptr) + (door != nullptr);
        for (size_t i = 0; i < count; ++i)
        {
            const auto size = get(bytes, 16 + ((containers.size() > 1 || world) ? 8 : 0) + 8 * i);
            if (!size || size > (door && i == count - 1 ? MaxDoorBytes : MaxEquipmentBytes) || size > remaining)
                throw std::invalid_argument("Invalid equipment session lengths");
            lengths[i] = size;
            remaining -= size;
        }
        if (remaining) throw std::invalid_argument("Invalid equipment session trailing bytes");
        // Check the complete door before any owner decode can allocate. Its
        // content-derived names/placement are fixed by trusted startup binding.
        if (door) door->preflight(bytes.last(lengths[count - 1]));
        EquipmentSessionValues staged;
        staged.mRevision = get(bytes, 8);
        if (staged.mRevision == 0 || staged.mRevision >= std::numeric_limits<size_t>::max())
            throw std::invalid_argument("Invalid equipment session revision");
        staged.mContainers.resize(containers.size());
        if (world) staged.mWorldItems.emplace();
        size_t offset = header;
        for (size_t i = 0; i < 2 + containers.size() + (world != nullptr); ++i)
        {
            const bool ground = i == 2 + containers.size();
            decodeEquipment(bytes.subspan(offset, lengths[i]), ground ? *world : i < 2 ? bindings[i] : containers[i - 2],
                ground ? *staged.mWorldItems : i < 2 ? staged.mActors[i] : staged.mContainers[i - 2]);
            offset += lengths[i];
        }
        if (door) staged.mDoor = decodeDoor(bytes.last(lengths[count - 1]), *door);
        validate(staged, bindings, containers, world, door);
        output.swap(staged);
    }
}
