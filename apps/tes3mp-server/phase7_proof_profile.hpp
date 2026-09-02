#ifndef TES3MP_SERVER_PHASE7_PROOF_PROFILE_HPP
#define TES3MP_SERVER_PHASE7_PROOF_PROFILE_HPP

#include <tes3mp/authentication.hpp>

#include <cstddef>
#include <cstdint>

namespace TES3MP::ServerApp
{
    inline constexpr std::uint16_t Phase7ProtocolMajor = 1;
    inline constexpr std::uint16_t Phase7ProtocolMinor = 0;
    inline constexpr std::uint16_t Phase7ProtocolPatch = 0;
    inline constexpr std::size_t Phase7SourceAuthenticationBurst = 4;
    inline constexpr std::size_t Phase7GlobalAuthenticationBurst = 32;
    inline constexpr std::uint64_t Phase7AuthenticationRefillMilliseconds = 1'000;
    inline constexpr std::size_t Phase7ConnectionCapacity = 8;

    constexpr bool phase7ProofDisconnectGraceAccepted(std::uint64_t milliseconds) noexcept
    {
        return milliseconds >= MinimumResumeTokenLifetimeMilliseconds
            && milliseconds <= MaximumResumeTokenLifetimeMilliseconds;
    }
}

#endif
