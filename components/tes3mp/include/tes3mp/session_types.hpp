#ifndef TES3MP_SESSION_TYPES_HPP
#define TES3MP_SESSION_TYPES_HPP

#include "monotonic_clock.hpp"
#include "value_types.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace TES3MP
{
    inline constexpr std::uint64_t MinimumSessionStageTimeoutNanoseconds = 1'000'000;
    inline constexpr std::uint64_t MaximumSessionStageTimeoutNanoseconds = 120'000'000'000;

    enum class SessionStage : std::uint8_t
    {
        TransportAndNegotiation,
        AuthenticationInput,
        AuthenticationProvider,
    };

    enum class AuthenticationRejectionReason : std::uint8_t
    {
        MalformedInput,
        Denied,
        ProviderUnavailable,
        Cancelled,
    };

    class SessionTimeoutPolicy
    {
    public:
        static constexpr std::optional<SessionTimeoutPolicy> create(std::uint64_t transportAndNegotiationNanoseconds,
            std::uint64_t authenticationInputNanoseconds, std::uint64_t authenticationProviderNanoseconds) noexcept
        {
            if (!validDuration(transportAndNegotiationNanoseconds) || !validDuration(authenticationInputNanoseconds)
                || !validDuration(authenticationProviderNanoseconds))
                return std::nullopt;
            return SessionTimeoutPolicy(
                transportAndNegotiationNanoseconds, authenticationInputNanoseconds, authenticationProviderNanoseconds);
        }

        constexpr std::uint64_t duration(SessionStage stage) const noexcept
        {
            switch (stage)
            {
                case SessionStage::TransportAndNegotiation:
                    return mTransportAndNegotiationNanoseconds;
                case SessionStage::AuthenticationInput:
                    return mAuthenticationInputNanoseconds;
                case SessionStage::AuthenticationProvider:
                    return mAuthenticationProviderNanoseconds;
            }
            return 0;
        }

        friend constexpr bool operator==(SessionTimeoutPolicy, SessionTimeoutPolicy) noexcept = default;

    private:
        static constexpr bool validDuration(std::uint64_t duration) noexcept
        {
            return duration >= MinimumSessionStageTimeoutNanoseconds
                && duration <= MaximumSessionStageTimeoutNanoseconds;
        }

        constexpr SessionTimeoutPolicy(std::uint64_t transportAndNegotiationNanoseconds,
            std::uint64_t authenticationInputNanoseconds, std::uint64_t authenticationProviderNanoseconds) noexcept
            : mTransportAndNegotiationNanoseconds(transportAndNegotiationNanoseconds)
            , mAuthenticationInputNanoseconds(authenticationInputNanoseconds)
            , mAuthenticationProviderNanoseconds(authenticationProviderNanoseconds)
        {
        }

        std::uint64_t mTransportAndNegotiationNanoseconds;
        std::uint64_t mAuthenticationInputNanoseconds;
        std::uint64_t mAuthenticationProviderNanoseconds;
    };

    constexpr std::optional<MonotonicInstant> sessionDeadline(
        MonotonicInstant now, std::uint64_t durationNanoseconds) noexcept
    {
        if (durationNanoseconds > std::numeric_limits<std::uint64_t>::max() - now.nanoseconds())
            return std::nullopt;
        return MonotonicInstant::fromNanoseconds(now.nanoseconds() + durationNanoseconds);
    }

    enum class SessionTransitionErrorCode : std::uint8_t
    {
        IllegalTransition,
        DeadlineOverflow,
    };

    struct SessionTransitionError
    {
        SessionTransitionErrorCode code;
        std::uint16_t observedState = 0;
        std::uint16_t observedEvent = 0;
        SessionStage stage = SessionStage::TransportAndNegotiation;

        friend constexpr bool operator==(SessionTransitionError, SessionTransitionError) noexcept = default;
    };
}

#endif
