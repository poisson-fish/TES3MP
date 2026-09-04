#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <tes3mp/protocol_pose.hpp>

#include "generated/client_vr_pose_sample_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    namespace ClientSchema = TES3MP::Protocol::Schema::ClientPose;

    template <class Value>
    Value value(std::uint64_t raw)
    {
        return *Value::fromValue(raw);
    }

    TES3MP::VrTrackedTransform transform(std::int64_t x, std::uint32_t turn)
    {
        return TES3MP::VrTrackedTransform(*TES3MP::VrPoseOffset3::create(x, -x, x / 2),
            TES3MP::Orientation3(TES3MP::Turn32::fromValue(turn), TES3MP::Turn32::fromValue(turn + 1),
                TES3MP::Turn32::fromValue(turn + 2)));
    }

    TES3MP::ClientVrPoseSample clientSample(std::uint64_t sequence = 9, std::uint64_t generation = 2,
        std::optional<TES3MP::VrTrackedTransform> left = transform(22, 200),
        std::optional<TES3MP::VrTrackedTransform> right = std::nullopt)
    {
        return TES3MP::ClientVrPoseSample(value<TES3MP::SessionId>(11), value<TES3MP::SessionGeneration>(generation),
            value<TES3MP::EntityId>(31), value<TES3MP::AuthorityEpoch>(4), value<TES3MP::PoseSampleSequence>(sequence),
            transform(10, 100), left, right);
    }

    TES3MP::ServerVrPoseSnapshot serverSample()
    {
        return TES3MP::ServerVrPoseSnapshot(value<TES3MP::SessionId>(71), value<TES3MP::SessionGeneration>(3),
            value<TES3MP::PlayerId>(41), value<TES3MP::SessionId>(11), value<TES3MP::SessionGeneration>(2),
            value<TES3MP::EntityId>(31), value<TES3MP::AuthorityEpoch>(4), value<TES3MP::PoseSampleSequence>(9),
            transform(10, 100), std::nullopt, transform(-33, 300));
    }

    bool hasError(const TES3MP::ClientVrPoseSampleDecodeResult& result, TES3MP::PoseDecodeErrorCode code)
    {
        const auto* failure = std::get_if<TES3MP::PoseDecodeError>(&result);
        return failure != nullptr && failure->code == code;
    }

    std::vector<std::byte> rawClient(std::uint64_t sequence, bool includeHead, std::int64_t x,
        bool includePosition = true, bool includeOrientation = true)
    {
        flatbuffers::FlatBufferBuilder builder;
        ClientSchema::RelativePosition3 position(x, 0, 0);
        ClientSchema::Orientation3 orientation(1, 2, 3);
        flatbuffers::Offset<ClientSchema::TrackedTransform> head;
        if (includeHead)
        {
            head = ClientSchema::CreateTrackedTransform(
                builder, includePosition ? &position : nullptr, includeOrientation ? &orientation : nullptr);
        }
        const auto root = ClientSchema::CreateClientVrPoseSample(builder, 11, 2, 31, 4, sequence, head);
        ClientSchema::FinishSizePrefixedClientVrPoseSampleBuffer(builder, root);
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return std::vector<std::byte>(begin, begin + builder.GetSize());
    }

    std::vector<std::byte> rawClientWithAdditiveField()
    {
        flatbuffers::FlatBufferBuilder builder;
        ClientSchema::RelativePosition3 position(10, -10, 5);
        ClientSchema::Orientation3 orientation(100, 101, 102);
        const auto head = ClientSchema::CreateTrackedTransform(builder, &position, &orientation);
        const auto start = builder.StartTable();
        builder.AddElement<std::uint32_t>(20, 0x12345678, 0);
        builder.AddElement<std::uint64_t>(ClientSchema::ClientVrPoseSample::VT_SAMPLE_SEQUENCE, 9, 0);
        builder.AddElement<std::uint64_t>(ClientSchema::ClientVrPoseSample::VT_ROOT_AUTHORITY_EPOCH, 4, 0);
        builder.AddElement<std::uint64_t>(ClientSchema::ClientVrPoseSample::VT_ROOT_ENTITY_ID, 31, 0);
        builder.AddElement<std::uint64_t>(ClientSchema::ClientVrPoseSample::VT_SOURCE_SESSION_GENERATION, 2, 0);
        builder.AddElement<std::uint64_t>(ClientSchema::ClientVrPoseSample::VT_SOURCE_SESSION_ID, 11, 0);
        builder.AddOffset(ClientSchema::ClientVrPoseSample::VT_HEAD, head);
        const auto root = flatbuffers::Offset<ClientSchema::ClientVrPoseSample>(builder.EndTable(start));
        builder.FinishSizePrefixed(root, ClientSchema::ClientVrPoseSampleIdentifier());
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return std::vector<std::byte>(begin, begin + builder.GetSize());
    }

    bool capability_is_stable_optional_and_fail_closed()
    {
        static_assert(TES3MP::VrPoseCapabilityValue == 1);
        static_assert(TES3MP::vrPoseCapability().value() == 1);
        auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 1));
        const std::array pose{ TES3MP::vrPoseCapability() };
        const std::array unknown{ *TES3MP::CapabilityId::fromValue(99) };
        const auto client = TES3MP::ClientHello::fromOffer(
            std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, pose, {})));
        const auto server = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, pose, {}));
        const auto accepted = TES3MP::negotiateClientHello(client, server);
        const auto* hello = std::get_if<TES3MP::ServerHello>(&accepted);
        if (hello == nullptr || hello->negotiatedCapabilities().size() != 1
            || hello->negotiatedCapabilities().front() != TES3MP::vrPoseCapability())
            return false;

        const auto unknownClient = TES3MP::ClientHello::fromOffer(
            std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, unknown, {})));
        const auto unknownResult = TES3MP::negotiateClientHello(unknownClient, server);
        const auto* unknownHello = std::get_if<TES3MP::ServerHello>(&unknownResult);
        if (unknownHello == nullptr || !unknownHello->negotiatedCapabilities().empty())
            return false;

        const auto requiredClient = TES3MP::ClientHello::fromOffer(
            std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, {}, pose)));
        const auto emptyServer = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, {}, {}));
        const auto rejected = TES3MP::negotiateClientHello(requiredClient, emptyServer);
        const auto* failure = std::get_if<TES3MP::SessionRejected>(&rejected);
        if (failure == nullptr || failure->reason() != TES3MP::SessionRejectionReason::UnsupportedRequiredCapability
            || failure->unsupportedCapability() != TES3MP::vrPoseCapability())
            return false;

        const auto currentVersions
            = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 1, 1));
        const auto previousVersions
            = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 0));
        const auto currentPayload = TES3MP::encodeClientHello(TES3MP::ClientHello::fromOffer(
            std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(currentVersions, pose, {}))));
        const auto previousPayload = TES3MP::encodeClientHello(TES3MP::ClientHello::fromOffer(
            std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(previousVersions, pose, {}))));
        const auto currentDecoded = TES3MP::decodeClientHello(currentPayload);
        const auto previousDecoded = TES3MP::decodeClientHello(previousPayload);
        const auto* currentHello = std::get_if<TES3MP::ClientHello>(&currentDecoded);
        const auto* previousHello = std::get_if<TES3MP::ClientHello>(&previousDecoded);
        return currentHello != nullptr && previousHello != nullptr && currentHello->optionalCapabilities().size() == 1
            && currentHello->optionalCapabilities().front() == TES3MP::vrPoseCapability()
            && previousHello->optionalCapabilities().size() == 1
            && previousHello->optionalCapabilities().front() == TES3MP::vrPoseCapability() && failure != nullptr
            && failure->reason() == TES3MP::SessionRejectionReason::UnsupportedRequiredCapability
            && failure->unsupportedCapability() == TES3MP::vrPoseCapability();
    }

    bool directional_roots_and_frames_are_distinct_and_bounded()
    {
        const auto clientDescriptor = TES3MP::messageDescriptor(TES3MP::MessageKind::ClientVrPoseSample);
        const auto serverDescriptor = TES3MP::messageDescriptor(TES3MP::MessageKind::ServerVrPoseSnapshot);
        if (!clientDescriptor || !serverDescriptor
            || clientDescriptor->messageClass != TES3MP::MessageClass::PresentationSample
            || serverDescriptor->messageClass != TES3MP::MessageClass::PresentationSample
            || clientDescriptor->kind == serverDescriptor->kind || clientDescriptor->maximumPayloadBytes != 1024
            || serverDescriptor->maximumPayloadBytes != 1024)
            return false;
        const auto payload = TES3MP::encodeClientVrPoseSample(clientSample());
        const auto frame = TES3MP::encodeProtocolFrame(
            TES3MP::MessageClass::PresentationSample, TES3MP::MessageKind::ClientVrPoseSample, payload);
        if (!std::holds_alternative<std::vector<std::byte>>(frame))
            return false;
        const std::vector<std::byte> tooLarge(TES3MP::PresentationSampleMaximumPayloadBytes + 1);
        const auto rejected = TES3MP::encodeProtocolFrame(
            TES3MP::MessageClass::PresentationSample, TES3MP::MessageKind::ClientVrPoseSample, tooLarge);
        const auto* failure = std::get_if<TES3MP::FrameError>(&rejected);
        return failure != nullptr && failure->code == TES3MP::FrameErrorCode::PayloadTooLarge;
    }

    bool pose_round_trips_as_owned_bounded_values()
    {
        const auto client = clientSample();
        auto clientBytes = TES3MP::encodeClientVrPoseSample(client);
        auto decodedClient = TES3MP::decodeClientVrPoseSample(clientBytes);
        std::fill(clientBytes.begin(), clientBytes.end(), std::byte{ 0 });
        const auto* ownedClient = std::get_if<TES3MP::ClientVrPoseSample>(&decodedClient);

        const auto server = serverSample();
        const auto serverBytes = TES3MP::encodeServerVrPoseSnapshot(server);
        const auto decodedServer = TES3MP::decodeServerVrPoseSnapshot(serverBytes);
        const auto* ownedServer = std::get_if<TES3MP::ServerVrPoseSnapshot>(&decodedServer);
        return ownedClient != nullptr && *ownedClient == client && ownedClient->leftHand() && !ownedClient->rightHand()
            && ownedServer != nullptr && *ownedServer == server && !ownedServer->leftHand() && ownedServer->rightHand()
            && serverBytes.size() <= TES3MP::PresentationSampleMaximumPayloadBytes;
    }

    bool semantic_failures_reject_without_partial_values()
    {
        const auto missingHead = TES3MP::decodeClientVrPoseSample(rawClient(1, false, 0));
        const auto zeroSequence = TES3MP::decodeClientVrPoseSample(rawClient(0, true, 0));
        const auto tooFar = TES3MP::decodeClientVrPoseSample(rawClient(1, true, TES3MP::MaximumVrPoseOffsetQuanta + 1));
        const auto missingPosition = TES3MP::decodeClientVrPoseSample(rawClient(1, true, 0, false, true));
        const auto missingOrientation = TES3MP::decodeClientVrPoseSample(rawClient(1, true, 0, true, false));
        return hasError(missingHead, TES3MP::PoseDecodeErrorCode::MissingHead)
            && hasError(zeroSequence, TES3MP::PoseDecodeErrorCode::InvalidStrongValue)
            && hasError(tooFar, TES3MP::PoseDecodeErrorCode::OffsetOutOfRange)
            && hasError(missingPosition, TES3MP::PoseDecodeErrorCode::MissingPosition)
            && hasError(missingOrientation, TES3MP::PoseDecodeErrorCode::MissingOrientation)
            && !TES3MP::VrPoseOffset3::create(TES3MP::MaximumVrPoseOffsetQuanta + 1, 0, 0)
            && TES3MP::VrPoseOffset3::create(-TES3MP::MaximumVrPoseOffsetQuanta, TES3MP::MaximumVrPoseOffsetQuanta, 0);
    }

    bool malformed_inputs_reject_without_partial_values()
    {
        const auto valid = TES3MP::encodeClientVrPoseSample(clientSample());
        for (std::size_t size = 0; size < valid.size(); ++size)
        {
            if (!std::holds_alternative<TES3MP::PoseDecodeError>(
                    TES3MP::decodeClientVrPoseSample(std::span(valid).first(size))))
                return false;
        }
        auto badIdentifier = valid;
        badIdentifier[8] ^= std::byte{ 1 };
        auto trailing = valid;
        trailing.push_back(std::byte{ 0 });
        const std::vector<std::byte> oversized(TES3MP::PresentationSampleMaximumPayloadBytes + 1);
        return hasError(TES3MP::decodeClientVrPoseSample(badIdentifier), TES3MP::PoseDecodeErrorCode::InvalidIdentifier)
            && hasError(TES3MP::decodeClientVrPoseSample(trailing), TES3MP::PoseDecodeErrorCode::PayloadLengthMismatch)
            && hasError(TES3MP::decodeClientVrPoseSample(oversized), TES3MP::PoseDecodeErrorCode::PayloadTooLarge);
    }

    bool verified_additive_optional_fields_remain_compatible()
    {
        const auto payload = rawClientWithAdditiveField();
        const auto decoded = TES3MP::decodeClientVrPoseSample(payload);
        const auto* sample = std::get_if<TES3MP::ClientVrPoseSample>(&decoded);
        return sample != nullptr && *sample == clientSample(9, 2, std::nullopt, std::nullopt)
            && TES3MP::encodeClientVrPoseSample(*sample) != payload;
    }

    bool recency_is_scoped_to_source_generation_and_content()
    {
        const auto previous = clientSample(9, 2);
        const auto duplicate = clientSample(9, 2);
        const auto conflict = clientSample(9, 2, std::nullopt);
        const auto newer = clientSample(10, 2);
        const auto stale = clientSample(8, 2);
        const auto newGeneration = clientSample(1, 3);
        const auto staleGeneration = clientSample(99, 1);
        const auto previousBytes = TES3MP::encodeClientVrPoseSample(previous);
        const auto classify = [&](const TES3MP::ClientVrPoseSample& sample) {
            const auto bytes = TES3MP::encodeClientVrPoseSample(sample);
            return TES3MP::classifyPoseSample(previous, previousBytes, sample, bytes);
        };
        auto nonEquivalentBytes = previousBytes;
        nonEquivalentBytes.back() ^= std::byte{ 1 };
        return classify(duplicate) == TES3MP::PoseSampleRecency::Duplicate
            && TES3MP::classifyPoseSample(previous, previousBytes, duplicate, nonEquivalentBytes)
            == TES3MP::PoseSampleRecency::ConflictingDuplicate
            && classify(conflict) == TES3MP::PoseSampleRecency::ConflictingDuplicate
            && classify(newer) == TES3MP::PoseSampleRecency::Newer
            && classify(stale) == TES3MP::PoseSampleRecency::Stale
            && classify(newGeneration) == TES3MP::PoseSampleRecency::SourceChanged
            && classify(staleGeneration) == TES3MP::PoseSampleRecency::Stale;
    }

    bool deterministic_pose_properties_round_trip()
    {
        for (std::uint64_t sequence = 1; sequence <= 128; ++sequence)
        {
            const auto sample = clientSample(sequence, 1 + sequence / 64,
                sequence % 2 == 0 ? std::optional(transform(static_cast<std::int64_t>(sequence), 5)) : std::nullopt,
                sequence % 3 == 0 ? std::optional(transform(-static_cast<std::int64_t>(sequence), 9)) : std::nullopt);
            const auto encoded = TES3MP::encodeClientVrPoseSample(sample);
            const auto decoded = TES3MP::decodeClientVrPoseSample(encoded);
            const auto* value = std::get_if<TES3MP::ClientVrPoseSample>(&decoded);
            if (value == nullptr || *value != sample || TES3MP::encodeClientVrPoseSample(*value) != encoded)
                return false;
        }
        return true;
    }

    bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::nullopt;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            return std::nullopt;
        return bytes;
    }

    bool writeCorpus(const std::filesystem::path& directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return !error && writeFile(directory / "valid-client-vr-pose", TES3MP::encodeClientVrPoseSample(clientSample()))
            && writeFile(directory / "valid-server-vr-pose", TES3MP::encodeServerVrPoseSnapshot(serverSample()))
            && writeFile(directory / "truncated", std::array<std::byte, 3>{});
    }

    bool verifyCorpus(const std::filesystem::path& directory)
    {
        const auto client = readFile(directory / "valid-client-vr-pose");
        const auto server = readFile(directory / "valid-server-vr-pose");
        const auto truncated = readFile(directory / "truncated");
        return client && *client == TES3MP::encodeClientVrPoseSample(clientSample())
            && std::holds_alternative<TES3MP::ClientVrPoseSample>(TES3MP::decodeClientVrPoseSample(*client)) && server
            && *server == TES3MP::encodeServerVrPoseSnapshot(serverSample())
            && std::holds_alternative<TES3MP::ServerVrPoseSnapshot>(TES3MP::decodeServerVrPoseSnapshot(*server))
            && truncated
            && std::holds_alternative<TES3MP::PoseDecodeError>(TES3MP::decodeClientVrPoseSample(*truncated));
    }

    void printBytes(std::span<const std::byte> bytes)
    {
        for (const auto byte : bytes)
            std::cout << std::hex << static_cast<unsigned>(std::to_integer<std::uint8_t>(byte));
        std::cout << '\n';
    }
}

int main(int argc, char** argv)
{
    static_assert(!std::is_default_constructible_v<TES3MP::VrPoseOffset3>);
    static_assert(!std::is_default_constructible_v<TES3MP::ClientVrPoseSample>);
    static_assert(!std::is_default_constructible_v<TES3MP::ServerVrPoseSnapshot>);

    if (argc == 2 && std::string_view(argv[1]) == "--print-golden")
    {
        printBytes(TES3MP::encodeClientVrPoseSample(clientSample()));
        printBytes(TES3MP::encodeServerVrPoseSnapshot(serverSample()));
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        return writeCorpus(argv[2]) ? 0 : 1;
    if (argc == 3 && std::string_view(argv[1]) == "--verify-corpus")
        return verifyCorpus(argv[2]) ? 0 : 1;

    const auto check = [](bool passed, std::string_view name) {
        if (!passed)
            std::cerr << "failed: " << name << '\n';
        return passed;
    };
    bool passed = true;
    passed &= check(capability_is_stable_optional_and_fail_closed(), "capability negotiation");
    passed &= check(directional_roots_and_frames_are_distinct_and_bounded(), "directional bounded framing");
    passed &= check(pose_round_trips_as_owned_bounded_values(), "owned pose round trips");
    passed &= check(semantic_failures_reject_without_partial_values(), "semantic validation");
    passed &= check(malformed_inputs_reject_without_partial_values(), "malformed input rejection");
    passed &= check(verified_additive_optional_fields_remain_compatible(), "additive optional compatibility");
    passed &= check(recency_is_scoped_to_source_generation_and_content(), "pose recency");
    passed &= check(deterministic_pose_properties_round_trip(), "deterministic properties");
    return passed ? 0 : 1;
}
