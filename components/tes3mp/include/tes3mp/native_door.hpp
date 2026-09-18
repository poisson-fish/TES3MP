#ifndef TES3MP_NATIVE_DOOR_HPP
#define TES3MP_NATIVE_DOOR_HPP

#include "session_types.hpp"
#include "value_types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace TES3MP
{
    // One bound ordinary door in the native world baseline. Motion identity is
    // session-scoped, changes on activation/reversal, and is never a client angle.
    struct NativeDoorSnapshot
    {
        uint64_t placement = 0;
        uint64_t motion = 0;
        float angle = 0;
        float stepSeconds = .05f;
        uint8_t direction = 0; // stock Idle / Opening / Closing
        bool blocked = false;
        friend bool operator==(const NativeDoorSnapshot&, const NativeDoorSnapshot&) = default;
    };

    inline constexpr uint64_t DoorObstructionLifetimeTicks = 10;
    struct ClientDoorObstruction
    {
        SessionId session;
        SessionGeneration generation;
        ServerTick observedTick;
        uint64_t placement;
        uint64_t motion;
        uint64_t sequence;
        bool blocked;
        friend bool operator==(const ClientDoorObstruction&, const ClientDoorObstruction&) = default;
    };
    // Fixed-size sensor telemetry, not a gameplay command or persistent state.
    std::vector<std::byte> encodeClientDoorObstruction(const ClientDoorObstruction& report);
    std::optional<ClientDoorObstruction> decodeClientDoorObstruction(std::span<const std::byte> bytes);
}
#endif
