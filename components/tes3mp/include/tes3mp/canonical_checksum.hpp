#ifndef TES3MP_CANONICAL_CHECKSUM_HPP
#define TES3MP_CANONICAL_CHECKSUM_HPP

#include "canonical_state.hpp"

#include <compare>
#include <cstdint>
#include <span>
#include <vector>

namespace TES3MP
{
    inline constexpr std::uint16_t CanonicalStateEncodingVersion = 1;
    inline constexpr std::uint16_t CanonicalChecksumAlgorithmVersion = 1;
    inline constexpr std::uint32_t CanonicalRulesVersion = 1;

    class CanonicalChecksum
    {
    public:
        constexpr explicit CanonicalChecksum(std::uint64_t value = 0) noexcept
            : mValue(value)
        {
        }

        constexpr std::uint64_t value() const noexcept { return mValue; }

        friend constexpr bool operator==(CanonicalChecksum, CanonicalChecksum) noexcept = default;
        friend constexpr auto operator<=>(CanonicalChecksum, CanonicalChecksum) noexcept = default;

    private:
        std::uint64_t mValue;
    };

    std::vector<std::uint8_t> canonicalStateBytesV1(
        CanonicalStateVersion stateVersion, ServerTick checkpointTick, const CanonicalServerState& state);
    CanonicalChecksum crc64Ecma182(std::span<const std::uint8_t> bytes) noexcept;
    CanonicalChecksum canonicalStateChecksumV1(
        CanonicalStateVersion stateVersion, ServerTick checkpointTick, const CanonicalServerState& state) noexcept;
}

#endif
