#include "proof_codec.hpp"

#include "v2_generated.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace
{
    bool isValidUtf8(std::span<const std::uint8_t> value)
    {
        std::size_t index = 0;
        while (index < value.size())
        {
            const std::uint8_t first = value[index++];
            if (first <= 0x7f)
                continue;

            std::size_t trailing = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            if (first >= 0xc2 && first <= 0xdf)
            {
                trailing = 1;
                codePoint = first & 0x1f;
                minimum = 0x80;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                trailing = 2;
                codePoint = first & 0x0f;
                minimum = 0x800;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                trailing = 3;
                codePoint = first & 0x07;
                minimum = 0x10000;
            }
            else
                return false;

            if (value.size() - index < trailing)
                return false;
            for (std::size_t offset = 0; offset < trailing; ++offset)
            {
                const std::uint8_t byte = value[index++];
                if ((byte & 0xc0) != 0x80)
                    return false;
                codePoint = (codePoint << 6) | (byte & 0x3f);
            }
            if (codePoint < minimum || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff))
                return false;
        }
        return true;
    }

    bool isValidString(const flatbuffers::String* value, std::size_t maximum)
    {
        if (value == nullptr || value->size() > maximum)
            return false;
        return isValidUtf8(std::span(
            reinterpret_cast<const std::uint8_t*>(value->Data()), static_cast<std::size_t>(value->size())));
    }
}

namespace vnext::proof::selection
{
    bool decodeV2(std::span<const std::uint8_t> frame, const DecodeLimits& limits, OwnedMotion& output,
        AllocationProbe& allocations)
    {
        if (limits.mMaxFrameBytes == std::numeric_limits<std::size_t>::max()
            || frame.size() < sizeof(flatbuffers::uoffset_t) || frame.size() > limits.mMaxFrameBytes)
            return false;
        if (flatbuffers::GetSizePrefixedBufferLength(frame.data()) != frame.size())
            return false;

        flatbuffers::Verifier::Options options;
        options.max_depth = limits.mMaxDepth;
        options.max_tables = limits.mMaxTables;
        options.max_size = limits.mMaxFrameBytes + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        flatbuffers::Verifier verifier(frame.data(), frame.size(), options);
        if (!vnext::proof::v2::VerifySizePrefixedEnvelopeBuffer(verifier))
            return false;

        const auto* envelope = vnext::proof::v2::GetSizePrefixedEnvelope(frame.data());
        if (envelope->protocol_version() != 1 || envelope->payload_type() != vnext::proof::v2::Payload::Motion)
            return false;
        const auto* motion = envelope->payload_as_Motion();
        if (motion == nullptr || !isValidString(motion->name(), MaxNameBytes))
            return false;
        const auto* samples = motion->samples();
        if (samples == nullptr || samples->size() > MaxSamples)
            return false;
        for (float sample : *samples)
        {
            if (!std::isfinite(sample) || std::abs(sample) > MaxSampleMagnitude)
                return false;
        }
        if (motion->mode() != vnext::proof::v2::TravelMode::Walk
            && motion->mode() != vnext::proof::v2::TravelMode::Run)
            return false;

        const auto* tags = envelope->tags();
        if (tags == nullptr || tags->size() > MaxTags)
            return false;
        for (const auto* tag : *tags)
        {
            if (!isValidString(tag, MaxTagBytes))
                return false;
        }
        const auto* capability = envelope->capability();
        if (capability != nullptr && !isValidString(capability, MaxCapabilityBytes))
            return false;

        OwnedMotion candidate;
        candidate.mProtocolVersion = envelope->protocol_version();
        candidate.mSequence = envelope->sequence();
        allocations.record();
        candidate.mName.assign(motion->name()->c_str(), motion->name()->size());
        allocations.record();
        candidate.mSamples.assign(samples->begin(), samples->end());
        allocations.record();
        candidate.mTags.reserve(tags->size());
        for (const auto* tag : *tags)
        {
            allocations.record();
            candidate.mTags.emplace_back(tag->c_str(), tag->size());
        }
        if (capability != nullptr)
        {
            allocations.record();
            candidate.mCapability.assign(capability->c_str(), capability->size());
        }
        output = std::move(candidate);
        return true;
    }
}
