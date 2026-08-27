#include <tes3mp/deterministic_random.hpp>

#include <bit>

namespace
{
    constexpr std::uint64_t SplitMixIncrement = 0x9e3779b97f4a7c15ULL;
    constexpr std::uint64_t RandomV1DomainSeparator = 0x544553334d505231ULL;
}

namespace TES3MP
{
    std::uint64_t SplitMix64::mix64(std::uint64_t value) noexcept
    {
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    std::uint64_t SplitMix64::nextU64() noexcept
    {
        mState += SplitMixIncrement;
        return mix64(mState);
    }

    Xoshiro256StarStar::Xoshiro256StarStar(RandomStateV1 state) noexcept
        : mState(state.words())
    {
    }

    Xoshiro256StarStar Xoshiro256StarStar::fromWorldSeed(std::uint64_t worldSeed, RandomStreamKey key) noexcept
    {
        std::uint64_t seed = SplitMix64::mix64(worldSeed ^ RandomV1DomainSeparator);
        seed = SplitMix64::mix64(seed ^ key.domainId());
        seed = SplitMix64::mix64(seed ^ key.subjectId());

        SplitMix64 splitMix(seed);
        const std::uint64_t word0 = splitMix.nextU64();
        const std::uint64_t word1 = splitMix.nextU64();
        const std::uint64_t word2 = splitMix.nextU64();
        const std::uint64_t word3 = splitMix.nextU64();
        return Xoshiro256StarStar(RandomStateV1::fromWords(word0, word1, word2, word3).value());
    }

    Xoshiro256StarStar Xoshiro256StarStar::restore(RandomStateV1 state) noexcept
    {
        return Xoshiro256StarStar(state);
    }

    std::uint64_t Xoshiro256StarStar::nextU64() noexcept
    {
        const std::uint64_t result = std::rotl(mState[1] * 5, 7) * 9;
        const std::uint64_t shifted = mState[1] << 17;

        mState[2] ^= mState[0];
        mState[3] ^= mState[1];
        mState[1] ^= mState[2];
        mState[0] ^= mState[3];
        mState[2] ^= shifted;
        mState[3] = std::rotl(mState[3], 45);
        return result;
    }

    std::optional<std::uint64_t> Xoshiro256StarStar::uniformBelow(std::uint64_t exclusiveUpperBound) noexcept
    {
        if (exclusiveUpperBound == 0)
            return std::nullopt;

        const std::uint64_t threshold = (0 - exclusiveUpperBound) % exclusiveUpperBound;
        for (;;)
        {
            const std::uint64_t value = nextU64();
            if (value >= threshold)
                return value % exclusiveUpperBound;
        }
    }

    RandomStateV1 Xoshiro256StarStar::snapshot() const noexcept
    {
        return RandomStateV1(mState);
    }
}
