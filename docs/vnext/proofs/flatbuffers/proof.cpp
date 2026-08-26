#include "v1_generated.h"
#include "v2_generated.h"
#include "proof_codec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using vnext::proof::selection::AllocationProbe;
    using vnext::proof::selection::DecodeLimits;
    using vnext::proof::selection::OwnedMotion;
    using vnext::proof::selection::decodeV2;

    constexpr std::size_t sMaxFrameBytes = vnext::proof::selection::MaxFrameBytes;
    constexpr std::size_t sMaxNameBytes = vnext::proof::selection::MaxNameBytes;
    constexpr std::size_t sMaxSamples = vnext::proof::selection::MaxSamples;
    constexpr std::size_t sMaxTags = vnext::proof::selection::MaxTags;
    constexpr float sMaxSampleMagnitude = vnext::proof::selection::MaxSampleMagnitude;

    [[noreturn]] void fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    void check(bool condition, std::string_view message)
    {
        if (!condition)
            fail(message);
    }

    std::vector<std::uint8_t> copyBuffer(const flatbuffers::FlatBufferBuilder& builder)
    {
        return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
    }

    std::vector<std::uint8_t> makeV1(std::string_view name, const std::vector<float>& samples,
        const std::vector<std::string>& tags, std::uint64_t sequence = 17)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto encodedName = builder.CreateString(name.data(), name.size());
        const auto encodedSamples = builder.CreateVector(samples);
        const auto motion = vnext::proof::v1::CreateMotion(
            builder, encodedName, encodedSamples, vnext::proof::v1::TravelMode::Walk);
        std::vector<flatbuffers::Offset<flatbuffers::String>> encodedTags;
        for (const std::string& tag : tags)
            encodedTags.push_back(builder.CreateString(tag));
        const auto root = vnext::proof::v1::CreateEnvelope(builder, 1, sequence,
            vnext::proof::v1::Payload::Motion, motion.Union(), builder.CreateVector(encodedTags));
        vnext::proof::v1::FinishSizePrefixedEnvelopeBuffer(builder, root);
        return copyBuffer(builder);
    }

    std::vector<std::uint8_t> makeV2(std::string_view name, const std::vector<float>& samples,
        const std::vector<std::string>& tags, std::string_view capability = "movement", std::uint64_t sequence = 23)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto encodedName = builder.CreateString(name.data(), name.size());
        const auto encodedSamples = builder.CreateVector(samples);
        const auto motion = vnext::proof::v2::CreateMotion(
            builder, encodedName, encodedSamples, vnext::proof::v2::TravelMode::Run);
        std::vector<flatbuffers::Offset<flatbuffers::String>> encodedTags;
        for (const std::string& tag : tags)
            encodedTags.push_back(builder.CreateString(tag));
        const auto encodedTagVector = builder.CreateVector(encodedTags);
        const auto encodedCapability = builder.CreateString(capability.data(), capability.size());
        const auto root = vnext::proof::v2::CreateEnvelope(builder, 1, sequence,
            vnext::proof::v2::Payload::Motion, motion.Union(), encodedTagVector, encodedCapability);
        vnext::proof::v2::FinishSizePrefixedEnvelopeBuffer(builder, root);
        return copyBuffer(builder);
    }

    std::vector<std::uint8_t> makeUnknownUnion()
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto root = vnext::proof::v2::CreateEnvelope(
            builder, 1, 99, static_cast<vnext::proof::v2::Payload>(99), 0, builder.CreateVector(
                std::vector<flatbuffers::Offset<flatbuffers::String>>{}), 0);
        vnext::proof::v2::FinishSizePrefixedEnvelopeBuffer(builder, root);
        return copyBuffer(builder);
    }

    OwnedMotion sentinel()
    {
        return {9, 999, "sentinel", {9.f}, {"sentinel"}, "sentinel"};
    }

    void expectFailure(std::span<const std::uint8_t> frame, std::string_view scenario,
        const DecodeLimits& limits = {})
    {
        OwnedMotion output = sentinel();
        const OwnedMotion before = output;
        AllocationProbe allocations;
        check(!decodeV2(frame, limits, output, allocations), scenario);
        check(output == before, "failed decode partially changed the owned output");
    }

    void testValidAndLifetime()
    {
        auto frame = makeV2("player", {1.f, 2.f}, {"near", "safe"});
        OwnedMotion output;
        AllocationProbe allocations;
        check(decodeV2(frame, {}, output, allocations), "valid v2 message was rejected");
        check(output.mProtocolVersion == 1 && output.mSequence == 23, "valid scalar fields changed");
        check(output.mName == "player" && output.mSamples == std::vector<float>({1.f, 2.f}),
            "valid owned payload changed");
        check(output.mTags == std::vector<std::string>({"near", "safe"}) && output.mCapability == "movement",
            "valid owned collections changed");
        check(allocations.mDomainAllocations > 0, "valid conversion did not exercise owned allocation path");
        std::ranges::fill(frame, std::uint8_t{0});
        check(output.mName == "player" && output.mTags.front() == "near",
            "owned output retained a generated view into recycled input");
    }

    void testCompatibleEvolution()
    {
        const auto oldFrame = makeV1("old", {3.f}, {"v1"});
        OwnedMotion upgraded;
        AllocationProbe allocations;
        check(decodeV2(oldFrame, {}, upgraded, allocations), "v2 reader rejected compatible v1 data");
        check(upgraded.mName == "old" && upgraded.mCapability.empty(), "v1 default changed under v2 reader");

        const auto newFrame = makeV2("new", {4.f}, {"v2"}, "optional");
        flatbuffers::Verifier::Options options;
        options.max_depth = 8;
        options.max_tables = 16;
        options.max_size = sMaxFrameBytes + 1;
        options.check_nested_flatbuffers = false;
        flatbuffers::Verifier verifier(newFrame.data(), newFrame.size(), options);
        check(vnext::proof::v1::VerifySizePrefixedEnvelopeBuffer(verifier),
            "v1 reader rejected compatible v2 data");
        const auto* oldView = vnext::proof::v1::GetSizePrefixedEnvelope(newFrame.data());
        check(oldView->sequence() == 23 && oldView->payload_as_Motion()->name()->str() == "new",
            "v1 reader changed known v2 fields");
    }

    void testMalformedAndBounds()
    {
        const auto valid = makeV2("player", {1.f, 2.f}, {"one"});
        for (std::size_t size = 0; size < valid.size(); ++size)
            expectFailure(std::span(valid.data(), size), "a truncated frame was accepted");

        auto badLength = valid;
        flatbuffers::WriteScalar<flatbuffers::uoffset_t>(
            badLength.data(), flatbuffers::GetPrefixedSize(badLength.data()) - 1);
        expectFailure(badLength, "declared length mismatch was accepted");

        auto trailing = valid;
        trailing.push_back(0);
        expectFailure(trailing, "trailing byte was accepted");

        auto badIdentifier = valid;
        const std::array<std::uint8_t, 4> identifier{'V', 'N', 'X', 'P'};
        const auto found = std::search(badIdentifier.begin(), badIdentifier.end(), identifier.begin(), identifier.end());
        check(found != badIdentifier.end(), "proof buffer identifier was not found");
        *found = 'B';
        expectFailure(badIdentifier, "wrong root identifier was accepted");

        auto badRoot = valid;
        flatbuffers::WriteScalar<flatbuffers::uoffset_t>(badRoot.data() + sizeof(flatbuffers::uoffset_t),
            std::numeric_limits<flatbuffers::uoffset_t>::max());
        expectFailure(badRoot, "invalid root offset was accepted");

        DecodeLimits shallow;
        shallow.mMaxDepth = 1;
        expectFailure(valid, "excessive nesting was accepted", shallow);
        DecodeLimits fewTables;
        fewTables.mMaxTables = 1;
        expectFailure(valid, "excessive table work was accepted", fewTables);
        DecodeLimits overflowingSize;
        overflowingSize.mMaxFrameBytes = std::numeric_limits<std::size_t>::max();
        expectFailure(valid, "overflowing verifier size budget was accepted", overflowingSize);

        expectFailure(makeUnknownUnion(), "unknown union discriminant was accepted");
    }

    void testSemanticLimitsBeforeAllocation()
    {
        const auto oversizedName = makeV2(std::string(sMaxNameBytes + 1, 'n'), {1.f}, {"ok"});
        OwnedMotion output = sentinel();
        AllocationProbe allocations;
        check(!decodeV2(oversizedName, {}, output, allocations), "oversized name was accepted");
        check(allocations.mDomainAllocations == 0, "oversized name reached domain allocation");

        const auto oversizedVector = makeV2("ok", std::vector<float>(sMaxSamples + 1, 1.f), {"ok"});
        allocations = {};
        check(!decodeV2(oversizedVector, {}, output, allocations), "oversized vector was accepted");
        check(allocations.mDomainAllocations == 0, "oversized vector reached domain allocation");

        const auto oversizedTags = makeV2("ok", {1.f}, std::vector<std::string>(sMaxTags + 1, "tag"));
        allocations = {};
        check(!decodeV2(oversizedTags, {}, output, allocations), "oversized tag collection was accepted");
        check(allocations.mDomainAllocations == 0, "oversized tag collection reached domain allocation");

        const auto invalidUtf8 = makeV2(std::string("\xc3\x28", 2), {1.f}, {"ok"});
        allocations = {};
        check(!decodeV2(invalidUtf8, {}, output, allocations), "invalid UTF-8 was accepted");
        check(allocations.mDomainAllocations == 0, "invalid UTF-8 reached domain allocation");

        const auto nonFinite = makeV2("ok", {std::numeric_limits<float>::quiet_NaN()}, {"ok"});
        allocations = {};
        check(!decodeV2(nonFinite, {}, output, allocations), "non-finite numeric value was accepted");
        check(allocations.mDomainAllocations == 0, "non-finite numeric value reached domain allocation");

        const auto outOfRange = makeV2("ok", {sMaxSampleMagnitude + 1.f}, {"ok"});
        allocations = {};
        check(!decodeV2(outOfRange, {}, output, allocations), "out-of-range numeric value was accepted");
        check(allocations.mDomainAllocations == 0, "out-of-range numeric value reached domain allocation");
    }

    void writeCorpusFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& value)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            fail("cannot create fuzz corpus file");
        stream.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
        if (!stream)
            fail("cannot write fuzz corpus file");
    }

    void writeCorpus(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto validV1 = makeV1("old", {3.f}, {"v1"});
        const auto validV2 = makeV2("new", {4.f}, {"v2"}, "optional");
        auto truncated = validV2;
        truncated.resize(truncated.size() / 2);
        auto badIdentifier = validV2;
        const std::array<std::uint8_t, 4> identifier{'V', 'N', 'X', 'P'};
        const auto found = std::search(badIdentifier.begin(), badIdentifier.end(), identifier.begin(), identifier.end());
        check(found != badIdentifier.end(), "proof buffer identifier was not found for corpus");
        *found = 'B';
        writeCorpusFile(directory / "valid-v1.vnp", validV1);
        writeCorpusFile(directory / "valid-v2.vnp", validV2);
        writeCorpusFile(directory / "truncated-v2.vnp", truncated);
        writeCorpusFile(directory / "bad-identifier-v2.vnp", badIdentifier);
        writeCorpusFile(directory / "unknown-union-v2.vnp", makeUnknownUnion());
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        {
            writeCorpus(argv[2]);
            return 0;
        }
        if (argc != 1)
            fail("usage: vnext-flatbuffers-proof [--write-corpus DIRECTORY]");
        testValidAndLifetime();
        testCompatibleEvolution();
        testMalformedAndBounds();
        testSemanticLimitsBeforeAllocation();
        std::cout << "FlatBuffers bounded/evolution selection proof passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FlatBuffers selection proof failed: " << error.what() << '\n';
        return 1;
    }
}
