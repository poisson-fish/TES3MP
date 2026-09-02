#ifndef TES3MP_TEST_SUPPORT_PHASE7_ADVERSE_PROFILES_HPP
#define TES3MP_TEST_SUPPORT_PHASE7_ADVERSE_PROFILES_HPP

#include "fault_injecting_link.hpp"

#include <tes3mp/transport.hpp>

#include <cstdint>
#include <optional>

namespace TES3MP::TestSupport
{
    inline constexpr std::uint64_t Phase7AdverseSeed = 0x745345334d505f37ULL;
    inline constexpr std::uint64_t Phase7LatencyNanoseconds = 50'000'000;
    inline constexpr std::uint64_t Phase7JitterNanoseconds = 20'000'000;
    inline constexpr std::uint64_t Phase7ReorderNanoseconds = 30'000'000;
    inline constexpr std::uint32_t Phase7LossPartsPerMillion = 50'000;
    inline constexpr std::uint32_t Phase7DuplicationPartsPerMillion = 50'000;
    inline constexpr std::uint64_t Phase7StallNanoseconds = 500'000'000;
    inline constexpr std::size_t Phase7ReconnectCycles = 32;
    inline constexpr std::size_t Phase7DeterministicSoakTicks = 10'000;
    inline constexpr std::uint64_t Phase7RealProcessSoakSeconds = 60;

    enum class Phase7AdverseProfile : std::uint8_t
    {
        LatencyJitterReorder,
        SampledStateLossDuplication,
        ReliableDuplicationReorder,
    };

    inline std::optional<FaultProfile> makePhase7AdverseProfile(Phase7AdverseProfile profile) noexcept
    {
        const auto loss = profile == Phase7AdverseProfile::SampledStateLossDuplication
            ? Phase7LossPartsPerMillion : 0;
        const auto duplication = profile == Phase7AdverseProfile::LatencyJitterReorder
            ? 0 : Phase7DuplicationPartsPerMillion;
        return FaultProfile::create(Phase7LatencyNanoseconds, Phase7JitterNanoseconds,
            Phase7ReorderNanoseconds, loss, duplication, OutboundQueuePolicy::MaxReliableMessages,
            OutboundQueuePolicy::MaxReliableBytes);
    }
}

#endif
