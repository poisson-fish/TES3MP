#ifndef TES3MP_NATIVE_DOOR_CODEC_HPP
#define TES3MP_NATIVE_DOOR_CODEC_HPP

#include "ordinary_door.hpp"
#include <memory>
#include <span>
#include <vector>

namespace TES3MP::Native
{
    inline constexpr size_t MaxDoorBytes = 4096;

    // Trusted content, serialized once at binding time. The immutable stock
    // reference fields are matched byte-for-byte before any save input allocates.
    class DoorBinding
    {
        OrdinaryDoor mDoor;
        ESM::Position mInitialPosition;
        std::vector<char> mTemplate;
    public:
        DoorBinding(const ESM::Door& base, const ESM::CellRef& placement);
        const OrdinaryDoor& door() const { return mDoor; }
        void preflight(std::span<const char> bytes) const;
    };

    void encodeDoor(const ESM::DoorState& state, const DoorBinding& binding, std::vector<char>& output);
    std::shared_ptr<const ESM::DoorState> decodeDoor(std::span<const char> bytes, const DoorBinding& binding);
}
#endif
