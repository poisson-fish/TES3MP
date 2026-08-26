#ifndef VNEXT_FLATBUFFERS_PROOF_CODEC_HPP
#define VNEXT_FLATBUFFERS_PROOF_CODEC_HPP

#include "flatbuffers/base.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vnext::proof::selection
{
    inline constexpr std::size_t MaxFrameBytes = 4096;
    inline constexpr std::size_t MaxNameBytes = 16;
    inline constexpr std::size_t MaxSamples = 8;
    inline constexpr std::size_t MaxTags = 4;
    inline constexpr std::size_t MaxTagBytes = 8;
    inline constexpr std::size_t MaxCapabilityBytes = 16;
    inline constexpr float MaxSampleMagnitude = 1000.f;

    struct DecodeLimits
    {
        std::size_t mMaxFrameBytes = MaxFrameBytes;
        flatbuffers::uoffset_t mMaxDepth = 8;
        flatbuffers::uoffset_t mMaxTables = 16;
    };

    struct AllocationProbe
    {
        std::size_t mDomainAllocations = 0;

        void record() { ++mDomainAllocations; }
    };

    struct OwnedMotion
    {
        std::uint16_t mProtocolVersion = 0;
        std::uint64_t mSequence = 0;
        std::string mName;
        std::vector<float> mSamples;
        std::vector<std::string> mTags;
        std::string mCapability;

        bool operator==(const OwnedMotion&) const = default;
    };

    bool decodeV2(std::span<const std::uint8_t> frame, const DecodeLimits& limits, OwnedMotion& output,
        AllocationProbe& allocations);
}

#endif
