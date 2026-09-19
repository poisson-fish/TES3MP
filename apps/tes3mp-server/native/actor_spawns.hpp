#ifndef TES3MP_NATIVE_ACTOR_SPAWNS_HPP
#define TES3MP_NATIVE_ACTOR_SPAWNS_HPP

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace TES3MP::Native
{
    // Initial campaign choices, including chance-none. No respawn is implied.
    // These values are sealed with inventories/doors, never in a sidecar file.
    struct ActorSpawnSelection
    {
        uint64_t mPlacement = 0, mRecord = 0;
        friend bool operator==(const ActorSpawnSelection&, const ActorSpawnSelection&) = default;
    };
    inline constexpr uint64_t SpawnAreaMagic = 0x3253414552413354; // T3AREAS2
    inline constexpr size_t MaximumActorSpawns = 1024;

    inline void putAreaWord(std::vector<char>& bytes, uint64_t value)
    {
        for (unsigned i = 0; i < 8; ++i) bytes.push_back(char(value >> (8 * i)));
    }
    inline uint64_t getAreaWord(std::span<const char> bytes, size_t& offset)
    {
        if (offset > bytes.size() || bytes.size() - offset < 8)
            throw std::invalid_argument("Truncated native area image");
        uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= uint64_t(uint8_t(bytes[offset++])) << (8 * i);
        return value;
    }
    inline void validateActorSpawns(std::span<const ActorSpawnSelection> values)
    {
        if (values.size() > MaximumActorSpawns) throw std::invalid_argument("Native actor spawn budget exceeded");
        for (size_t i = 0; i < values.size(); ++i)
            if (!(values[i].mPlacement >> 63) || (i && values[i - 1].mPlacement >= values[i].mPlacement)
                || (values[i].mRecord && (values[i].mRecord >> 62) != 2))
                throw std::invalid_argument("Native actor spawn identity invalid");
    }
    inline std::vector<ActorSpawnSelection> readActorSpawns(std::span<const char> bytes, size_t& offset)
    {
        const auto count = getAreaWord(bytes, offset);
        if (count > MaximumActorSpawns || count > (bytes.size() - offset) / 16)
            throw std::invalid_argument("Native actor spawn image length invalid");
        std::vector<ActorSpawnSelection> result;
        for (size_t i = 0; i < count; ++i)
        {
            const auto placement = getAreaWord(bytes, offset), record = getAreaWord(bytes, offset);
            result.push_back({placement, record});
        }
        validateActorSpawns(result);
        return result;
    }
}
#endif
