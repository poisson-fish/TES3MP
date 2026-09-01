#ifndef TES3MP_TRANSPORT_GNS_DETAIL_HPP
#define TES3MP_TRANSPORT_GNS_DETAIL_HPP

#include "tes3mp/transport_gns.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace TES3MP::Detail
{
    enum class NumericAddressFamily
    {
        Ipv4,
        Ipv6,
    };

    struct NumericAddress
    {
        std::string host;
        std::uint16_t port = 0;
        NumericAddressFamily family = NumericAddressFamily::Ipv4;

        friend bool operator==(const NumericAddress&, const NumericAddress&) = default;
    };

    std::optional<AdmissionScopeId> deriveAdmissionScope(
        std::span<const std::byte> key, NumericAddressFamily family, std::span<const std::byte> address) noexcept;

    TransportFactoryResult makeGameNetworkingSocketsTransportWithAdmissionScopeKey(
        TransportLimits limits, std::span<const std::byte> key) noexcept;
    TransportFactoryResult makeGameNetworkingSocketsTransportWithAdmissionScopeKey(
        TransportLimits limits, std::span<const std::byte> key, TransportTelemetrySink& telemetry) noexcept;

    enum class ResolutionCompletion
    {
        Pending,
        Success,
        NoData,
        Failed,
    };

    struct CandidateLaunch
    {
        std::uint64_t ordinal = 0;
        NumericAddress address;

        friend bool operator==(const CandidateLaunch&, const CandidateLaunch&) = default;
    };

    class HappyEyeballsAttempt
    {
    public:
        using TimePoint = std::chrono::milliseconds;

        static constexpr auto Ipv6PreferenceDelay = std::chrono::milliseconds(50);
        static constexpr auto CandidateStagger = std::chrono::milliseconds(250);

        void addResolution(std::span<const NumericAddress> addresses, ResolutionCompletion completion, TimePoint now);
        std::optional<CandidateLaunch> nextLaunch(TimePoint now, bool retryImmediately = false);
        bool candidateFailed(std::uint64_t ordinal) noexcept;
        bool candidateSucceeded(std::uint64_t ordinal) noexcept;
        void cancel() noexcept;

        bool cancelled() const noexcept { return mCancelled; }
        bool resolutionDone() const noexcept { return mResolution != ResolutionCompletion::Pending; }
        ResolutionCompletion resolution() const noexcept { return mResolution; }
        bool shouldFail() const noexcept;
        std::size_t retainedAddressCount() const noexcept { return mCandidates.size(); }
        std::size_t activeCandidateCount() const noexcept;

    private:
        struct Candidate
        {
            NumericAddress address;
            std::uint64_t ordinal = 0;
            bool launched = false;
            bool active = false;
        };

        bool hasUnlaunched() const noexcept;
        bool hasIpv6() const noexcept;

        std::vector<Candidate> mCandidates;
        ResolutionCompletion mResolution = ResolutionCompletion::Pending;
        std::optional<TimePoint> mFirstIpv4Available;
        std::optional<TimePoint> mLastLaunch;
        std::uint64_t mNextOrdinal = 1;
        bool mCancelled = false;
        bool mSucceeded = false;
    };

    class GenerationCounter
    {
    public:
        explicit GenerationCounter(std::uint64_t next = 1) noexcept
            : mNext(next == 0 ? std::nullopt : std::optional(next))
        {
        }

        std::optional<std::uint64_t> allocate() noexcept;

    private:
        std::optional<std::uint64_t> mNext;
    };

    template <class Handle, class Owner>
    class GenerationBindingTable
    {
    public:
        bool bind(Handle handle, Owner owner, std::uint64_t generation,
            std::optional<std::uint64_t> inheritedGeneration = std::nullopt)
        {
            return mBindings.emplace(handle, Binding{ std::move(owner), generation, inheritedGeneration }).second;
        }

        const Owner* find(Handle handle, std::uint64_t generation) const noexcept
        {
            const auto found = mBindings.find(handle);
            if (found == mBindings.end()
                || (found->second.generation != generation && found->second.inheritedGeneration != generation))
                return nullptr;
            return &found->second.owner;
        }

        void retireInherited(Handle handle) noexcept
        {
            const auto found = mBindings.find(handle);
            if (found != mBindings.end())
                found->second.inheritedGeneration.reset();
        }

        bool erase(Handle handle) noexcept { return mBindings.erase(handle) != 0; }
        void clear() noexcept { mBindings.clear(); }

    private:
        struct Binding
        {
            Owner owner;
            std::uint64_t generation = 0;
            std::optional<std::uint64_t> inheritedGeneration;
        };

        std::map<Handle, Binding> mBindings;
    };
}

#endif
