#include <tes3mp/protocol_pose.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/client_vr_pose_sample_generated.h"
#include "generated/server_vr_pose_snapshot_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <array>
#include <optional>
#include <ranges>

namespace
{
    namespace ClientSchema = TES3MP::Protocol::Schema::ClientPose;
    namespace ServerSchema = TES3MP::Protocol::Schema::ServerPose;

    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumIdentifiedFlatBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;
    constexpr std::size_t MaximumVerifierDepth = 6;
    constexpr std::size_t MaximumVerifierTables = 4;

    constexpr TES3MP::PoseDecodeError error(TES3MP::PoseDecodeErrorStage stage, TES3MP::PoseDecodeErrorCode code,
        std::size_t observed = 0, std::size_t limit = 0) noexcept
    {
        return TES3MP::PoseDecodeError{ stage, code, observed, limit };
    }

    std::optional<TES3MP::PoseDecodeError> validatePayloadPrefix(std::span<const std::byte> payload) noexcept
    {
        if (payload.size() < MinimumIdentifiedFlatBufferBytes)
        {
            return error(TES3MP::PoseDecodeErrorStage::SizePrefix, TES3MP::PoseDecodeErrorCode::PayloadTooSmall,
                payload.size(), MinimumIdentifiedFlatBufferBytes);
        }
        if (payload.size() > TES3MP::PresentationSampleMaximumPayloadBytes)
        {
            return error(TES3MP::PoseDecodeErrorStage::SizePrefix, TES3MP::PoseDecodeErrorCode::PayloadTooLarge,
                payload.size(), TES3MP::PresentationSampleMaximumPayloadBytes);
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const std::size_t declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
        {
            return error(TES3MP::PoseDecodeErrorStage::SizePrefix, TES3MP::PoseDecodeErrorCode::PayloadLengthMismatch,
                payload.size(), declared);
        }
        return std::nullopt;
    }

    flatbuffers::Verifier makeVerifier(std::span<const std::byte> payload)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = MaximumVerifierDepth;
        options.max_tables = MaximumVerifierTables;
        options.max_size = TES3MP::PresentationSampleMaximumPayloadBytes + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> takeBuffer(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return std::vector<std::byte>(begin, begin + builder.GetSize());
    }

    template <class Value>
    std::variant<Value, TES3MP::PoseDecodeError> strongValue(std::uint64_t raw)
    {
        const auto value = Value::fromValue(raw);
        if (!value)
        {
            return error(
                TES3MP::PoseDecodeErrorStage::SemanticValidation, TES3MP::PoseDecodeErrorCode::InvalidStrongValue, raw);
        }
        return *value;
    }

    template <class Value>
    const Value* decodedValue(const std::variant<Value, TES3MP::PoseDecodeError>& result)
    {
        return std::get_if<Value>(&result);
    }

    template <class EncodedTransform>
    std::variant<TES3MP::VrTrackedTransform, TES3MP::PoseDecodeError> decodeTransform(const EncodedTransform* encoded)
    {
        if (encoded->position() == nullptr)
        {
            return error(
                TES3MP::PoseDecodeErrorStage::SemanticValidation, TES3MP::PoseDecodeErrorCode::MissingPosition);
        }
        if (encoded->orientation() == nullptr)
        {
            return error(
                TES3MP::PoseDecodeErrorStage::SemanticValidation, TES3MP::PoseDecodeErrorCode::MissingOrientation);
        }
        const auto* position = encoded->position();
        const auto offset = TES3MP::VrPoseOffset3::create(position->x(), position->y(), position->z());
        if (!offset)
        {
            return error(
                TES3MP::PoseDecodeErrorStage::SemanticValidation, TES3MP::PoseDecodeErrorCode::OffsetOutOfRange);
        }
        const auto* orientation = encoded->orientation();
        return TES3MP::VrTrackedTransform(*offset,
            TES3MP::Orientation3(TES3MP::Turn32::fromValue(orientation->x()),
                TES3MP::Turn32::fromValue(orientation->y()), TES3MP::Turn32::fromValue(orientation->z())));
    }

    template <class EncodedTransform>
    std::variant<std::optional<TES3MP::VrTrackedTransform>, TES3MP::PoseDecodeError> decodeOptionalTransform(
        const EncodedTransform* encoded)
    {
        if (encoded == nullptr)
            return std::optional<TES3MP::VrTrackedTransform>{};
        auto decoded = decodeTransform(encoded);
        if (const auto* failure = std::get_if<TES3MP::PoseDecodeError>(&decoded))
            return *failure;
        return std::optional<TES3MP::VrTrackedTransform>(std::get<TES3MP::VrTrackedTransform>(decoded));
    }

    flatbuffers::Offset<ClientSchema::TrackedTransform> encodeClientTransform(
        flatbuffers::FlatBufferBuilder& builder, const TES3MP::VrTrackedTransform& value)
    {
        const auto offset = value.offset();
        const auto orientation = value.orientation();
        const ClientSchema::RelativePosition3 position(offset.x(), offset.y(), offset.z());
        const ClientSchema::Orientation3 rotation(
            orientation.x().value(), orientation.y().value(), orientation.z().value());
        return ClientSchema::CreateTrackedTransform(builder, &position, &rotation);
    }

    flatbuffers::Offset<ServerSchema::TrackedTransform> encodeServerTransform(
        flatbuffers::FlatBufferBuilder& builder, const TES3MP::VrTrackedTransform& value)
    {
        const auto offset = value.offset();
        const auto orientation = value.orientation();
        const ServerSchema::RelativePosition3 position(offset.x(), offset.y(), offset.z());
        const ServerSchema::Orientation3 rotation(
            orientation.x().value(), orientation.y().value(), orientation.z().value());
        return ServerSchema::CreateTrackedTransform(builder, &position, &rotation);
    }

    template <class Sample>
    TES3MP::PoseSampleRecency classify(const Sample& previous, std::span<const std::byte> previousPayload,
        const Sample& incoming, std::span<const std::byte> incomingPayload) noexcept
    {
        if (previous.sourceSessionId() != incoming.sourceSessionId()
            || incoming.sourceSessionGeneration() > previous.sourceSessionGeneration())
            return TES3MP::PoseSampleRecency::SourceChanged;
        if (incoming.sourceSessionGeneration() < previous.sourceSessionGeneration())
            return TES3MP::PoseSampleRecency::Stale;
        if (incoming.sampleSequence() > previous.sampleSequence())
            return TES3MP::PoseSampleRecency::Newer;
        if (incoming.sampleSequence() < previous.sampleSequence())
            return TES3MP::PoseSampleRecency::Stale;
        return incoming == previous && std::ranges::equal(previousPayload, incomingPayload)
            ? TES3MP::PoseSampleRecency::Duplicate
            : TES3MP::PoseSampleRecency::ConflictingDuplicate;
    }
}

namespace TES3MP
{
    std::vector<std::byte> encodeClientVrPoseSample(const ClientVrPoseSample& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto head = encodeClientTransform(builder, value.head());
        const auto left = value.leftHand() ? encodeClientTransform(builder, *value.leftHand())
                                           : flatbuffers::Offset<ClientSchema::TrackedTransform>{};
        const auto right = value.rightHand() ? encodeClientTransform(builder, *value.rightHand())
                                             : flatbuffers::Offset<ClientSchema::TrackedTransform>{};
        const auto root = ClientSchema::CreateClientVrPoseSample(builder, value.sourceSessionId().value(),
            value.sourceSessionGeneration().value(), value.rootEntityId().value(), value.rootAuthorityEpoch().value(),
            value.sampleSequence().value(), head, left, right);
        ClientSchema::FinishSizePrefixedClientVrPoseSampleBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeServerVrPoseSnapshot(const ServerVrPoseSnapshot& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto head = encodeServerTransform(builder, value.head());
        const auto left = value.leftHand() ? encodeServerTransform(builder, *value.leftHand())
                                           : flatbuffers::Offset<ServerSchema::TrackedTransform>{};
        const auto right = value.rightHand() ? encodeServerTransform(builder, *value.rightHand())
                                             : flatbuffers::Offset<ServerSchema::TrackedTransform>{};
        const auto root = ServerSchema::CreateServerVrPoseSnapshot(builder, value.targetSessionId().value(),
            value.targetSessionGeneration().value(), value.sourcePlayerId().value(), value.sourceSessionId().value(),
            value.sourceSessionGeneration().value(), value.rootEntityId().value(), value.rootAuthorityEpoch().value(),
            value.sampleSequence().value(), head, left, right);
        ServerSchema::FinishSizePrefixedServerVrPoseSnapshotBuffer(builder, root);
        return takeBuffer(builder);
    }

    ClientVrPoseSampleDecodeResult decodeClientVrPoseSample(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ClientSchema::SizePrefixedClientVrPoseSampleBufferHasIdentifier(bytes))
            return error(PoseDecodeErrorStage::Identifier, PoseDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload);
        if (!ClientSchema::VerifySizePrefixedClientVrPoseSampleBuffer(verifier))
            return error(PoseDecodeErrorStage::Verification, PoseDecodeErrorCode::VerificationFailed);
        const auto* root = ClientSchema::GetSizePrefixedClientVrPoseSample(bytes);
        if (root->head() == nullptr)
            return error(PoseDecodeErrorStage::SemanticValidation, PoseDecodeErrorCode::MissingHead);

        auto session = strongValue<SessionId>(root->source_session_id());
        auto generation = strongValue<SessionGeneration>(root->source_session_generation());
        auto entity = strongValue<EntityId>(root->root_entity_id());
        auto epoch = strongValue<AuthorityEpoch>(root->root_authority_epoch());
        auto sequence = strongValue<PoseSampleSequence>(root->sample_sequence());
        const std::array failures{ std::get_if<PoseDecodeError>(&session), std::get_if<PoseDecodeError>(&generation),
            std::get_if<PoseDecodeError>(&entity), std::get_if<PoseDecodeError>(&epoch),
            std::get_if<PoseDecodeError>(&sequence) };
        for (const auto* failure : failures)
        {
            if (failure != nullptr)
                return *failure;
        }
        auto head = decodeTransform(root->head());
        auto left = decodeOptionalTransform(root->left_hand());
        auto right = decodeOptionalTransform(root->right_hand());
        if (const auto* failure = std::get_if<PoseDecodeError>(&head))
            return *failure;
        if (const auto* failure = std::get_if<PoseDecodeError>(&left))
            return *failure;
        if (const auto* failure = std::get_if<PoseDecodeError>(&right))
            return *failure;
        return ClientVrPoseSample(*decodedValue(session), *decodedValue(generation), *decodedValue(entity),
            *decodedValue(epoch), *decodedValue(sequence), std::get<VrTrackedTransform>(head),
            std::get<std::optional<VrTrackedTransform>>(left), std::get<std::optional<VrTrackedTransform>>(right));
    }

    ServerVrPoseSnapshotDecodeResult decodeServerVrPoseSnapshot(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ServerSchema::SizePrefixedServerVrPoseSnapshotBufferHasIdentifier(bytes))
            return error(PoseDecodeErrorStage::Identifier, PoseDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload);
        if (!ServerSchema::VerifySizePrefixedServerVrPoseSnapshotBuffer(verifier))
            return error(PoseDecodeErrorStage::Verification, PoseDecodeErrorCode::VerificationFailed);
        const auto* root = ServerSchema::GetSizePrefixedServerVrPoseSnapshot(bytes);
        if (root->head() == nullptr)
            return error(PoseDecodeErrorStage::SemanticValidation, PoseDecodeErrorCode::MissingHead);

        auto targetSession = strongValue<SessionId>(root->target_session_id());
        auto targetGeneration = strongValue<SessionGeneration>(root->target_session_generation());
        auto player = strongValue<PlayerId>(root->source_player_id());
        auto sourceSession = strongValue<SessionId>(root->source_session_id());
        auto sourceGeneration = strongValue<SessionGeneration>(root->source_session_generation());
        auto entity = strongValue<EntityId>(root->root_entity_id());
        auto epoch = strongValue<AuthorityEpoch>(root->root_authority_epoch());
        auto sequence = strongValue<PoseSampleSequence>(root->sample_sequence());
        const std::array failures{ std::get_if<PoseDecodeError>(&targetSession),
            std::get_if<PoseDecodeError>(&targetGeneration), std::get_if<PoseDecodeError>(&player),
            std::get_if<PoseDecodeError>(&sourceSession), std::get_if<PoseDecodeError>(&sourceGeneration),
            std::get_if<PoseDecodeError>(&entity), std::get_if<PoseDecodeError>(&epoch),
            std::get_if<PoseDecodeError>(&sequence) };
        for (const auto* failure : failures)
        {
            if (failure != nullptr)
                return *failure;
        }
        auto head = decodeTransform(root->head());
        auto left = decodeOptionalTransform(root->left_hand());
        auto right = decodeOptionalTransform(root->right_hand());
        if (const auto* failure = std::get_if<PoseDecodeError>(&head))
            return *failure;
        if (const auto* failure = std::get_if<PoseDecodeError>(&left))
            return *failure;
        if (const auto* failure = std::get_if<PoseDecodeError>(&right))
            return *failure;
        return ServerVrPoseSnapshot(*decodedValue(targetSession), *decodedValue(targetGeneration),
            *decodedValue(player), *decodedValue(sourceSession), *decodedValue(sourceGeneration), *decodedValue(entity),
            *decodedValue(epoch), *decodedValue(sequence), std::get<VrTrackedTransform>(head),
            std::get<std::optional<VrTrackedTransform>>(left), std::get<std::optional<VrTrackedTransform>>(right));
    }

    PoseSampleRecency classifyPoseSample(const ClientVrPoseSample& previous, std::span<const std::byte> previousPayload,
        const ClientVrPoseSample& incoming, std::span<const std::byte> incomingPayload) noexcept
    {
        return classify(previous, previousPayload, incoming, incomingPayload);
    }

    PoseSampleRecency classifyPoseSample(const ServerVrPoseSnapshot& previous,
        std::span<const std::byte> previousPayload, const ServerVrPoseSnapshot& incoming,
        std::span<const std::byte> incomingPayload) noexcept
    {
        return classify(previous, previousPayload, incoming, incomingPayload);
    }
}
