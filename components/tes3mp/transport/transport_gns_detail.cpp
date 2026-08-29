#include "transport_gns_detail.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string_view>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>

namespace TES3MP::Detail
{
    std::optional<AdmissionScopeId> deriveAdmissionScope(
        std::span<const std::byte> key, NumericAddressFamily family, std::span<const std::byte> address) noexcept
    {
        constexpr std::string_view Domain = "TES3MP admission scope v1";
        std::size_t prefixBytes = 0;
        std::size_t exactAddressBytes = 0;
        unsigned char familyTag = 0;
        switch (family)
        {
            case NumericAddressFamily::Ipv4:
                prefixBytes = 4;
                exactAddressBytes = 4;
                familyTag = 4;
                break;
            case NumericAddressFamily::Ipv6:
                prefixBytes = 8;
                exactAddressBytes = 16;
                familyTag = 6;
                break;
            default:
                return std::nullopt;
        }
        if (key.size() != AdmissionScopeIdBytes || address.size() != exactAddressBytes)
            return std::nullopt;

        using Mac = std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)>;
        using Context = std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)>;
        Mac mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), &EVP_MAC_free);
        if (!mac)
            return std::nullopt;
        Context context(EVP_MAC_CTX_new(mac.get()), &EVP_MAC_CTX_free);
        if (!context)
            return std::nullopt;

        char digest[] = "SHA256";
        OSSL_PARAM parameters[]
            = { OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0), OSSL_PARAM_construct_end() };
        const auto* keyBytes = reinterpret_cast<const unsigned char*>(key.data());
        const auto* domainBytes = reinterpret_cast<const unsigned char*>(Domain.data());
        const auto* sourceBytes = reinterpret_cast<const unsigned char*>(address.data());
        std::array<std::byte, AdmissionScopeIdBytes> output{};
        std::size_t outputBytes = 0;
        const bool completed = EVP_MAC_init(context.get(), keyBytes, key.size(), parameters) == 1
            && EVP_MAC_update(context.get(), domainBytes, Domain.size()) == 1
            && EVP_MAC_update(context.get(), &familyTag, 1) == 1
            && EVP_MAC_update(context.get(), sourceBytes, prefixBytes) == 1
            && EVP_MAC_final(
                   context.get(), reinterpret_cast<unsigned char*>(output.data()), &outputBytes, output.size())
                == 1
            && outputBytes == output.size();
        const auto result = completed ? AdmissionScopeId::create(output) : std::nullopt;
        OPENSSL_cleanse(output.data(), output.size());
        return result;
    }

    void HappyEyeballsAttempt::addResolution(
        std::span<const NumericAddress> addresses, ResolutionCompletion completion, TimePoint now)
    {
        if (mCancelled || mSucceeded || resolutionDone())
            return;

        for (const NumericAddress& address : addresses)
        {
            if (mCandidates.size() >= TransportLimits::MaxResolvedAddresses)
                break;
            const bool duplicate = std::ranges::any_of(
                mCandidates, [&](const Candidate& candidate) { return candidate.address == address; });
            if (!duplicate)
            {
                const bool preferLateIpv6
                    = address.family == NumericAddressFamily::Ipv6 && mFirstIpv4Available && !mLastLaunch;
                if (preferLateIpv6)
                {
                    const auto firstIpv4 = std::ranges::find_if(mCandidates, [](const Candidate& candidate) {
                        return candidate.address.family == NumericAddressFamily::Ipv4;
                    });
                    mCandidates.insert(firstIpv4, Candidate{ address });
                }
                else
                    mCandidates.push_back({ address });
            }
        }

        if (!mFirstIpv4Available && !hasIpv6() && std::ranges::any_of(mCandidates, [](const Candidate& candidate) {
                return candidate.address.family == NumericAddressFamily::Ipv4;
            }))
            mFirstIpv4Available = now;

        if (completion != ResolutionCompletion::Pending)
            mResolution = completion;
    }

    std::optional<CandidateLaunch> HappyEyeballsAttempt::nextLaunch(TimePoint now, bool retryImmediately)
    {
        if (mCancelled || mSucceeded || activeCandidateCount() >= TransportLimits::MaxCandidateHandlesPerAttempt)
            return std::nullopt;

        const auto candidate
            = std::ranges::find_if(mCandidates, [](const Candidate& value) { return !value.launched; });
        if (candidate == mCandidates.end())
            return std::nullopt;

        if (!mLastLaunch)
        {
            const bool waitingForIpv6 = candidate->address.family == NumericAddressFamily::Ipv4 && !resolutionDone()
                && !hasIpv6() && mFirstIpv4Available && now < *mFirstIpv4Available + Ipv6PreferenceDelay;
            if (waitingForIpv6)
                return std::nullopt;
        }
        else if (!retryImmediately && activeCandidateCount() != 0 && now < *mLastLaunch + CandidateStagger)
            return std::nullopt;

        candidate->ordinal = mNextOrdinal++;
        candidate->launched = true;
        candidate->active = true;
        mLastLaunch = now;
        return CandidateLaunch{ candidate->ordinal, candidate->address };
    }

    bool HappyEyeballsAttempt::candidateFailed(std::uint64_t ordinal) noexcept
    {
        const auto candidate = std::ranges::find_if(
            mCandidates, [&](const Candidate& value) { return value.ordinal == ordinal && value.active; });
        if (candidate == mCandidates.end())
            return false;
        candidate->active = false;
        return true;
    }

    bool HappyEyeballsAttempt::candidateSucceeded(std::uint64_t ordinal) noexcept
    {
        const auto candidate = std::ranges::find_if(
            mCandidates, [&](const Candidate& value) { return value.ordinal == ordinal && value.active; });
        if (candidate == mCandidates.end())
            return false;
        candidate->active = false;
        mSucceeded = true;
        return true;
    }

    void HappyEyeballsAttempt::cancel() noexcept
    {
        mCancelled = true;
        for (Candidate& candidate : mCandidates)
            candidate.active = false;
    }

    bool HappyEyeballsAttempt::shouldFail() const noexcept
    {
        return !mCancelled && !mSucceeded && resolutionDone() && !hasUnlaunched() && activeCandidateCount() == 0;
    }

    std::size_t HappyEyeballsAttempt::activeCandidateCount() const noexcept
    {
        return std::ranges::count_if(mCandidates, [](const Candidate& candidate) { return candidate.active; });
    }

    bool HappyEyeballsAttempt::hasUnlaunched() const noexcept
    {
        return std::ranges::any_of(mCandidates, [](const Candidate& candidate) { return !candidate.launched; });
    }

    bool HappyEyeballsAttempt::hasIpv6() const noexcept
    {
        return std::ranges::any_of(mCandidates,
            [](const Candidate& candidate) { return candidate.address.family == NumericAddressFamily::Ipv6; });
    }

    std::optional<std::uint64_t> GenerationCounter::allocate() noexcept
    {
        if (!mNext)
            return std::nullopt;
        const std::uint64_t result = *mNext;
        mNext = result == std::numeric_limits<std::uint64_t>::max() ? std::nullopt : std::optional(result + 1);
        return result;
    }
}
