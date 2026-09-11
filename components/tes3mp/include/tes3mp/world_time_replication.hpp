#ifndef TES3MP_WORLD_TIME_REPLICATION_HPP
#define TES3MP_WORLD_TIME_REPLICATION_HPP

#include "protocol_frame.hpp"
#include "world_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    enum class WorldTimeReplicationDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        InvalidStrongValue,
        InvalidWorldTime,
    };

    struct WorldTimeReplicationDecodeError
    {
        WorldTimeReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        friend constexpr bool operator==(WorldTimeReplicationDecodeError, WorldTimeReplicationDecodeError) noexcept
            = default;
    };

    struct ReliableWorldTimeState
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        CanonicalWorldTimeState time;
        bool completeBaseline = false;

        friend constexpr bool operator==(const ReliableWorldTimeState&, const ReliableWorldTimeState&) noexcept
            = default;
    };

    using ReliableWorldTimeStateDecodeResult
        = std::variant<ReliableWorldTimeState, WorldTimeReplicationDecodeError>;

    std::vector<std::byte> encodeReliableWorldTimeState(const ReliableWorldTimeState& value);
    ReliableWorldTimeStateDecodeResult decodeReliableWorldTimeState(std::span<const std::byte> payload);
}

#endif
