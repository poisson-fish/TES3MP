#ifndef TES3MP_DETERMINISTIC_RANDOM_HPP
#define TES3MP_DETERMINISTIC_RANDOM_HPP

#include <array>
#include <compare>
#include <cstdint>
#include <optional>

namespace TES3MP
{
    class RandomStreamKey
    {
    public:
        static constexpr std::optional<RandomStreamKey> fromValues(
            std::uint64_t domainId, std::uint64_t subjectId) noexcept
        {
            if (domainId == 0)
                return std::nullopt;
            return RandomStreamKey(domainId, subjectId);
        }

        constexpr std::uint64_t domainId() const noexcept { return mDomainId; }
        constexpr std::uint64_t subjectId() const noexcept { return mSubjectId; }

        friend constexpr bool operator==(RandomStreamKey, RandomStreamKey) noexcept = default;
        friend constexpr auto operator<=>(RandomStreamKey, RandomStreamKey) noexcept = default;

    private:
        constexpr RandomStreamKey(std::uint64_t domainId, std::uint64_t subjectId) noexcept
            : mDomainId(domainId)
            , mSubjectId(subjectId)
        {
        }

        std::uint64_t mDomainId;
        std::uint64_t mSubjectId;
    };

    class RandomStateV1
    {
    public:
        static constexpr std::optional<RandomStateV1> fromWords(
            std::uint64_t word0, std::uint64_t word1, std::uint64_t word2, std::uint64_t word3) noexcept
        {
            if ((word0 | word1 | word2 | word3) == 0)
                return std::nullopt;
            return RandomStateV1({ word0, word1, word2, word3 });
        }

        constexpr const std::array<std::uint64_t, 4>& words() const noexcept { return mWords; }

        friend constexpr bool operator==(RandomStateV1, RandomStateV1) noexcept = default;

    private:
        friend class Xoshiro256StarStar;

        constexpr explicit RandomStateV1(std::array<std::uint64_t, 4> words) noexcept
            : mWords(words)
        {
        }

        std::array<std::uint64_t, 4> mWords;
    };

    class SplitMix64
    {
    public:
        constexpr explicit SplitMix64(std::uint64_t state) noexcept
            : mState(state)
        {
        }

        std::uint64_t nextU64() noexcept;
        constexpr std::uint64_t state() const noexcept { return mState; }

        static std::uint64_t mix64(std::uint64_t value) noexcept;

    private:
        std::uint64_t mState;
    };

    class Xoshiro256StarStar
    {
    public:
        static Xoshiro256StarStar fromWorldSeed(std::uint64_t worldSeed, RandomStreamKey key) noexcept;
        static Xoshiro256StarStar restore(RandomStateV1 state) noexcept;

        std::uint64_t nextU64() noexcept;
        std::optional<std::uint64_t> uniformBelow(std::uint64_t exclusiveUpperBound) noexcept;
        RandomStateV1 snapshot() const noexcept;

    private:
        explicit Xoshiro256StarStar(RandomStateV1 state) noexcept;

        std::array<std::uint64_t, 4> mState;
    };
}

#endif
