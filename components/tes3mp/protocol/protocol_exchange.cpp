#include <tes3mp/protocol_exchange.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/latest_wins_snapshot_generated.h"
#include "generated/reliable_interest_baseline_generated.h"
#include "generated/reliable_observation_batch_generated.h"
#include "generated/reliable_operation_generated.h"
#include "generated/session_resync_request_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>

namespace
{
    using TES3MP::ExchangeDecodeError;
    using TES3MP::ExchangeDecodeErrorCode;
    using TES3MP::ExchangeDecodeErrorStage;
    namespace ReliableSchema = TES3MP::Protocol::Schema::Reliable;
    namespace SnapshotSchema = TES3MP::Protocol::Schema::Snapshot;
    namespace ObservationSchema = TES3MP::Protocol::Schema::Observation;
    namespace BaselineSchema = TES3MP::Protocol::Schema::InterestBaseline;
    namespace ResyncSchema = TES3MP::Protocol::Schema::Resync;

    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumIdentifiedFlatBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;
    constexpr std::size_t MaximumVerifierDepth = 8;
    constexpr std::size_t MaximumVerifierTables = 8;

    constexpr ExchangeDecodeError error(ExchangeDecodeErrorStage stage, ExchangeDecodeErrorCode code,
        std::size_t observed = 0, std::size_t limit = 0, std::size_t index = 0) noexcept
    {
        return ExchangeDecodeError{ stage, code, observed, limit, index };
    }

    std::optional<ExchangeDecodeError> validatePayloadPrefix(
        std::span<const std::byte> payload, std::size_t maximum) noexcept
    {
        if (payload.size() < MinimumIdentifiedFlatBufferBytes)
        {
            return error(ExchangeDecodeErrorStage::SizePrefix, ExchangeDecodeErrorCode::PayloadTooSmall, payload.size(),
                MinimumIdentifiedFlatBufferBytes);
        }
        if (payload.size() > maximum)
        {
            return error(ExchangeDecodeErrorStage::SizePrefix, ExchangeDecodeErrorCode::PayloadTooLarge, payload.size(),
                maximum);
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const std::size_t declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
        {
            return error(ExchangeDecodeErrorStage::SizePrefix, ExchangeDecodeErrorCode::PayloadLengthMismatch,
                payload.size(), declared);
        }
        return std::nullopt;
    }

    flatbuffers::Verifier makeVerifier(std::span<const std::byte> payload, std::size_t maximum)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = MaximumVerifierDepth;
        options.max_tables = MaximumVerifierTables;
        options.max_size = maximum + 1;
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
    std::variant<Value, ExchangeDecodeError> strongValue(std::uint64_t raw, std::size_t index = 0)
    {
        const auto value = Value::fromValue(raw);
        if (!value)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::InvalidStrongValue, raw,
                0, index);
        }
        return *value;
    }

    template <class Value>
    const Value* decodedValue(const std::variant<Value, ExchangeDecodeError>& result)
    {
        return std::get_if<Value>(&result);
    }

    SnapshotSchema::Cell encodeCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
        {
            return { interior->cellSpace().value(), 0, 0, SnapshotSchema::CellKind::Interior };
        }
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(),
            SnapshotSchema::CellKind::Exterior };
    }

    ReliableSchema::Cell encodeReliableCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
            return { interior->cellSpace().value(), 0, 0, ReliableSchema::CellKind::Interior };
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(), ReliableSchema::CellKind::Exterior };
    }

    std::variant<TES3MP::CellId, ExchangeDecodeError> decodeReliableCell(const ReliableSchema::Cell& cell)
    {
        auto space = strongValue<TES3MP::CellSpaceId>(cell.cell_space_id());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&space)) return *failure;
        if (cell.kind() == ReliableSchema::CellKind::Interior)
        {
            if (cell.grid_x() != 0 || cell.grid_y() != 0)
                return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::InvalidInteriorGrid);
            return TES3MP::CellId::interior(*decodedValue(space));
        }
        if (cell.kind() == ReliableSchema::CellKind::Exterior)
            return TES3MP::CellId::exterior(*decodedValue(space), cell.grid_x(), cell.grid_y());
        return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::InvalidCellKind,
            static_cast<std::size_t>(cell.kind()));
    }

    SnapshotSchema::SpatialEntitySnapshot encodeEntry(const TES3MP::SpatialEntitySnapshot& entry)
    {
        const auto& transform = entry.transform();
        const auto position = transform.position();
        const auto orientation = transform.orientation();
        const auto velocity = entry.linearVelocity();
        return { entry.playerId().value(), entry.entityId().value(), entry.appearanceId().value(),
            entry.entityRevision().value(), entry.authorityEpoch().value(), entry.serverTick().value(),
            encodeCell(transform.cell()),
            SnapshotSchema::Position3(position.x(), position.y(), position.z()),
            SnapshotSchema::Orientation3(orientation.x().value(), orientation.y().value(), orientation.z().value()),
            SnapshotSchema::LinearVelocity3(velocity.x(), velocity.y(), velocity.z()) };
    }

    std::variant<TES3MP::SpatialEntitySnapshot, ExchangeDecodeError> decodeEntry(
        const SnapshotSchema::SpatialEntitySnapshot& entry, std::size_t index)
    {
        auto player = strongValue<TES3MP::PlayerId>(entry.player_id(), index);
        auto entity = strongValue<TES3MP::EntityId>(entry.entity_id(), index);
        auto appearance = strongValue<TES3MP::AppearanceId>(entry.appearance_id(), index);
        auto revision = strongValue<TES3MP::EntityRevision>(entry.entity_revision(), index);
        auto epoch = strongValue<TES3MP::AuthorityEpoch>(entry.authority_epoch(), index);
        auto tick = strongValue<TES3MP::ServerTick>(entry.server_tick(), index);
        auto cellSpace = strongValue<TES3MP::CellSpaceId>(entry.cell().cell_space_id(), index);
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&player))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&entity))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&appearance))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&revision))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&epoch))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&tick))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&cellSpace))
            return *failure;

        TES3MP::CellId cell = TES3MP::CellId::interior(*decodedValue(cellSpace));
        switch (entry.cell().kind())
        {
            case SnapshotSchema::CellKind::Interior:
                if (entry.cell().grid_x() != 0 || entry.cell().grid_y() != 0)
                {
                    return error(ExchangeDecodeErrorStage::SemanticValidation,
                        ExchangeDecodeErrorCode::InvalidInteriorGrid, 0, 0, index);
                }
                break;
            case SnapshotSchema::CellKind::Exterior:
                cell = TES3MP::CellId::exterior(*decodedValue(cellSpace), entry.cell().grid_x(), entry.cell().grid_y());
                break;
            default:
                return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::InvalidCellKind,
                    static_cast<std::size_t>(entry.cell().kind()), 0, index);
        }

        const auto& position = entry.position();
        const auto& orientation = entry.orientation();
        const auto& velocity = entry.linear_velocity();
        return TES3MP::SpatialEntitySnapshot(*decodedValue(tick), *decodedValue(player), *decodedValue(entity),
            *decodedValue(appearance), *decodedValue(revision), *decodedValue(epoch),
            TES3MP::Transform(cell, TES3MP::Position3(position.x(), position.y(), position.z()),
                TES3MP::Orientation3(TES3MP::Turn32::fromValue(orientation.x()),
                    TES3MP::Turn32::fromValue(orientation.y()), TES3MP::Turn32::fromValue(orientation.z()))),
            TES3MP::LinearVelocity3(velocity.x(), velocity.y(), velocity.z()));
    }
}

namespace TES3MP
{
    std::variant<SessionResyncRequest, ExchangeDecodeError> SessionResyncRequest::create(SessionId sessionId,
        SessionGeneration sessionGeneration, ResyncReason reason,
        CanonicalStateVersion lastObservedStateVersion) noexcept
    {
        if (reason != ResyncReason::LocalFeedGap && reason != ResyncReason::EntityRevisionMismatch
            && reason != ResyncReason::ChecksumMismatch)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::InvalidResyncReason,
                static_cast<std::size_t>(reason));
        return SessionResyncRequest(sessionId, sessionGeneration, reason, lastObservedStateVersion);
    }

    std::variant<ReliableInterestBaseline, ExchangeDecodeError> ReliableInterestBaseline::create(
        SessionId targetSessionId, SessionGeneration targetSessionGeneration, CanonicalRevision canonicalRevision,
        CanonicalStateVersion canonicalStateVersion, ServerTick serverTick,
        std::span<const InterestMember> members)
    {
        if (members.size() > MaximumInterestMembers)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManyInterestMembers,
                members.size(), MaximumInterestMembers);
        for (std::size_t index = 1; index < members.size(); ++index)
        {
            if (members[index - 1].playerId >= members[index].playerId
                || std::ranges::any_of(members.first(index), [&](const auto& prior) {
                    return prior.entityId == members[index].entityId;
                }))
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::InterestMembersNotStrictlySorted,
                    members[index].playerId.value(), members[index - 1].playerId.value(), index);
        }
        return ReliableInterestBaseline(targetSessionId, targetSessionGeneration, canonicalRevision,
            canonicalStateVersion, serverTick, std::vector<InterestMember>(members.begin(), members.end()));
    }

    std::variant<ReliableObservationBatch, ExchangeDecodeError> ReliableObservationBatch::create(
        SessionId targetSessionId, SessionGeneration targetSessionGeneration, CanonicalRevision canonicalRevision,
        std::span<const ObservationChange> changes)
    {
        if (changes.size() > MaximumObservationChanges)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManyObservationChanges,
                changes.size(), MaximumObservationChanges);
        for (std::size_t index = 0; index < changes.size(); ++index)
        {
            if (changes[index].kind != ObservationChangeKind::Enter
                && changes[index].kind != ObservationChangeKind::Leave)
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::InvalidObservationChangeKind, static_cast<std::size_t>(changes[index].kind), 0, index);
            if (index != 0 && changes[index - 1].entityId >= changes[index].entityId)
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::ObservationChangesNotStrictlySorted,
                    changes[index].entityId.value(), changes[index - 1].entityId.value(), index);
        }
        return ReliableObservationBatch(targetSessionId, targetSessionGeneration, canonicalRevision,
            std::vector<ObservationChange>(changes.begin(), changes.end()));
    }

    std::variant<ReliableOperation, ExchangeDecodeError> ReliableOperation::create(
        ReliableOperationHeader header, PlayerMotionIntent intent) noexcept
    {
        if (!header.entityPrecondition())
        {
            return error(
                ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingEntityPrecondition);
        }
        return ReliableOperation(header, intent);
    }

    std::variant<ReliableOperation, ExchangeDecodeError> ReliableOperation::create(
        ReliableOperationHeader header, CellTransition transition) noexcept
    {
        if (!header.entityPrecondition())
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingEntityPrecondition);
        return ReliableOperation(header, transition);
    }

    std::variant<ReliableOperation, ExchangeDecodeError> ReliableOperation::create(
        ReliableOperationHeader header, PlayerLocomotionInput input) noexcept
    {
        if (!header.entityPrecondition())
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingEntityPrecondition);
        return ReliableOperation(header, input);
    }

    std::variant<SpatialWorldView, ExchangeDecodeError> SpatialWorldView::create(
        std::span<const SpatialEntitySnapshot> entries)
    {
        if (entries.size() > MaximumSpatialWorldViewEntries)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManySnapshotEntries,
                entries.size(), MaximumSpatialWorldViewEntries);
        }
        for (std::size_t index = 1; index < entries.size(); ++index)
        {
            if (entries[index - 1].entityId() >= entries[index].entityId())
            {
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::SnapshotEntriesNotStrictlySorted, entries[index].entityId().value(),
                    entries[index - 1].entityId().value(), index);
            }
        }
        return SpatialWorldView(std::vector<SpatialEntitySnapshot>(entries.begin(), entries.end()));
    }

    std::vector<std::byte> encodeReliableOperation(const ReliableOperation& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& command = value.header().commandHeader();
        const auto commandHeader = ReliableSchema::CreateClientCommandHeader(builder, command.sessionId().value(),
            command.sessionGeneration().value(), command.commandSequence().value(), command.commandId().value(),
            command.observedCanonicalRevision().value());
        const auto& precondition = *value.header().entityPrecondition();
        const auto entityPrecondition
            = ReliableSchema::CreateEntityPrecondition(builder, precondition.entityId().value(),
                precondition.expectedRevision().value(), precondition.expectedAuthorityEpoch().value());
        flatbuffers::Offset<void> body;
        ReliableSchema::ReliableOperationBody bodyType;
        if (const auto* intent = std::get_if<PlayerMotionIntent>(&value.body()))
        {
            const auto velocity = intent->desiredVelocity();
            const ReliableSchema::LinearVelocity3 encodedVelocity(velocity.x(), velocity.y(), velocity.z());
            body = ReliableSchema::CreatePlayerMotionIntent(builder, &encodedVelocity).Union();
            bodyType = ReliableSchema::ReliableOperationBody::PlayerMotionIntent;
        }
        else if (const auto* input = std::get_if<PlayerLocomotionInput>(&value.body()))
        {
            const auto velocity = input->intent().desiredVelocity();
            const ReliableSchema::LinearVelocity3 encodedVelocity(velocity.x(), velocity.y(), velocity.z());
            const auto mode = static_cast<ReliableSchema::LocomotionMode>(
                static_cast<std::uint8_t>(input->intent().mode()) + 1);
            body = ReliableSchema::CreatePlayerLocomotionInput(builder, input->inputTick().value(),
                input->inputSequence().value(), mode, input->intent().rootFacing().value(), &encodedVelocity).Union();
            bodyType = ReliableSchema::ReliableOperationBody::PlayerLocomotionInput;
        }
        else
        {
            const auto cell = encodeReliableCell(std::get<CellTransition>(value.body()).requestedCell());
            body = ReliableSchema::CreateCellTransition(builder, &cell).Union();
            bodyType = ReliableSchema::ReliableOperationBody::CellTransition;
        }
        const auto root = ReliableSchema::CreateReliableOperation(builder, commandHeader, entityPrecondition, bodyType, body);
        ReliableSchema::FinishSizePrefixedReliableOperationBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeLatestWinsSnapshot(const LatestWinsSnapshot& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& header = value.header();
        const bool hasAcknowledgement = header.acknowledgedCommandSequence().has_value();
        const std::uint64_t acknowledgement = hasAcknowledgement ? header.acknowledgedCommandSequence()->value() : 0;
        const auto encodedHeader = SnapshotSchema::CreateLatestWinsSnapshotHeader(builder,
            header.targetSessionId().value(), header.targetSessionGeneration().value(), header.canonicalRevision().value(),
            hasAcknowledgement, acknowledgement, header.targetPlayerId().value(), header.targetEntityId().value());

        std::vector<SnapshotSchema::SpatialEntitySnapshot> entries;
        entries.reserve(value.view().entries().size());
        std::transform(value.view().entries().begin(), value.view().entries().end(), std::back_inserter(entries),
            [](const SpatialEntitySnapshot& entry) { return encodeEntry(entry); });
        const auto encodedEntries = builder.CreateVectorOfStructs(entries);
        const auto view = SnapshotSchema::CreateSpatialWorldView(builder, encodedEntries);
        const auto root = SnapshotSchema::CreateLatestWinsSnapshot(
            builder, encodedHeader, SnapshotSchema::LatestWinsSnapshotBody::SpatialWorldView, view.Union());
        SnapshotSchema::FinishSizePrefixedLatestWinsSnapshotBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeReliableObservationBatch(const ReliableObservationBatch& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = ObservationSchema::CreateReliableObservationHeader(builder,
            value.targetSessionId().value(), value.targetSessionGeneration().value(), value.canonicalRevision().value());
        std::vector<ObservationSchema::ObservationChange> changes;
        changes.reserve(value.changes().size());
        for (const auto& change : value.changes())
            changes.emplace_back(change.playerId.value(), change.entityId.value(),
                static_cast<ObservationSchema::ObservationChangeKind>(change.kind));
        const auto encodedChanges = builder.CreateVectorOfStructs(changes);
        const auto root = ObservationSchema::CreateReliableObservationBatch(builder, header, encodedChanges);
        ObservationSchema::FinishSizePrefixedReliableObservationBatchBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeReliableInterestBaseline(const ReliableInterestBaseline& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = BaselineSchema::CreateInterestBaselineHeader(builder,
            value.targetSessionId().value(), value.targetSessionGeneration().value(),
            value.canonicalRevision().value(), value.canonicalStateVersion().value(), value.serverTick().value());
        std::vector<BaselineSchema::InterestMember> members;
        members.reserve(value.members().size());
        for (const auto& member : value.members())
            members.emplace_back(member.playerId.value(), member.entityId.value());
        const auto encodedMembers = builder.CreateVectorOfStructs(members);
        const auto root = BaselineSchema::CreateReliableInterestBaseline(builder, header, encodedMembers);
        BaselineSchema::FinishSizePrefixedReliableInterestBaselineBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeSessionResyncRequest(const SessionResyncRequest& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto root = ResyncSchema::CreateSessionResyncRequest(builder, value.sessionId().value(),
            value.sessionGeneration().value(), static_cast<ResyncSchema::ResyncReason>(value.reason()),
            value.lastObservedStateVersion().value());
        ResyncSchema::FinishSizePrefixedSessionResyncRequestBuffer(builder, root);
        return takeBuffer(builder);
    }

    ReliableOperationDecodeResult decodeReliableOperation(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ReliableSchema::SizePrefixedReliableOperationBufferHasIdentifier(bytes))
            return error(ExchangeDecodeErrorStage::Identifier, ExchangeDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!ReliableSchema::VerifySizePrefixedReliableOperationBuffer(verifier))
            return error(ExchangeDecodeErrorStage::Verification, ExchangeDecodeErrorCode::VerificationFailed);

        const auto* root = ReliableSchema::GetSizePrefixedReliableOperation(bytes);
        const auto* command = root->command_header();
        if (command == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingCommandHeader);
        const auto* precondition = root->entity_precondition();
        if (precondition == nullptr)
        {
            return error(
                ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingEntityPrecondition);
        }
        if (root->body_type() == ReliableSchema::ReliableOperationBody::NONE || root->body() == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingBody);
        if (root->body_type() != ReliableSchema::ReliableOperationBody::PlayerMotionIntent
            && root->body_type() != ReliableSchema::ReliableOperationBody::CellTransition
            && root->body_type() != ReliableSchema::ReliableOperationBody::PlayerLocomotionInput)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::UnknownBody,
                static_cast<std::size_t>(root->body_type()));
        }

        auto session = strongValue<SessionId>(command->session_id());
        auto generation = strongValue<SessionGeneration>(command->session_generation());
        auto sequence = strongValue<CommandSequence>(command->command_sequence());
        auto commandId = strongValue<CommandId>(command->command_id());
        auto observedRevision = strongValue<CanonicalRevision>(command->observed_canonical_revision());
        auto entity = strongValue<EntityId>(precondition->entity_id());
        auto revision = strongValue<EntityRevision>(precondition->expected_revision());
        auto epoch = strongValue<AuthorityEpoch>(precondition->expected_authority_epoch());
        const std::array failures{ std::get_if<ExchangeDecodeError>(&session),
            std::get_if<ExchangeDecodeError>(&generation), std::get_if<ExchangeDecodeError>(&sequence),
            std::get_if<ExchangeDecodeError>(&commandId), std::get_if<ExchangeDecodeError>(&observedRevision),
            std::get_if<ExchangeDecodeError>(&entity), std::get_if<ExchangeDecodeError>(&revision),
            std::get_if<ExchangeDecodeError>(&epoch) };
        for (const auto* failure : failures)
        {
            if (failure != nullptr)
                return *failure;
        }

        ReliableOperationHeader header(
            ClientCommandHeader(*decodedValue(session), *decodedValue(generation), *decodedValue(sequence),
                *decodedValue(commandId), *decodedValue(observedRevision)),
            EntityPrecondition(*decodedValue(entity), *decodedValue(revision), *decodedValue(epoch)));
        if (root->body_type() == ReliableSchema::ReliableOperationBody::PlayerMotionIntent)
        {
            const auto* intent = root->body_as_PlayerMotionIntent();
            if (intent == nullptr || intent->desired_velocity() == nullptr)
                return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingDesiredVelocity);
            const auto* velocity = intent->desired_velocity();
            return ReliableOperation::create(header,
                PlayerMotionIntent(LinearVelocity3(velocity->x(), velocity->y(), velocity->z())));
        }
        if (root->body_type() == ReliableSchema::ReliableOperationBody::PlayerLocomotionInput)
        {
            const auto* input = root->body_as_PlayerLocomotionInput();
            if (input == nullptr || input->desired_velocity() == nullptr)
                return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingDesiredVelocity);
            const auto inputTick = LocomotionInputTick::fromValue(input->input_tick());
            if (!inputTick)
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::InvalidLocomotionInputTick, input->input_tick(),
                    MaximumLocomotionInputOrdinal);
            const auto inputSequence = LocomotionInputSequence::fromValue(input->input_sequence());
            if (!inputSequence)
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::InvalidLocomotionInputSequence, input->input_sequence(),
                    MaximumLocomotionInputOrdinal);
            if (input->locomotion_mode() < ReliableSchema::LocomotionMode::Sneak
                || input->locomotion_mode() > ReliableSchema::LocomotionMode::Jump)
                return error(ExchangeDecodeErrorStage::SemanticValidation,
                    ExchangeDecodeErrorCode::InvalidLocomotionMode,
                    static_cast<std::size_t>(input->locomotion_mode()));
            const auto mode = static_cast<LocomotionMode>(
                static_cast<std::uint8_t>(input->locomotion_mode()) - 1);
            const auto* velocity = input->desired_velocity();
            return ReliableOperation::create(header, PlayerLocomotionInput(*inputTick, *inputSequence,
                LocomotionIntent(mode, Turn32::fromValue(input->root_facing()),
                    LinearVelocity3(velocity->x(), velocity->y(), velocity->z()))));
        }
        const auto* transition = root->body_as_CellTransition();
        if (transition == nullptr || transition->requested_cell() == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingRequestedCell);
        auto cell = decodeReliableCell(*transition->requested_cell());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&cell)) return *failure;
        return ReliableOperation::create(header, CellTransition(std::get<CellId>(cell)));
    }

    LatestWinsSnapshotDecodeResult decodeLatestWinsSnapshot(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload, LatestWinsSnapshotMaximumPayloadBytes))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!SnapshotSchema::SizePrefixedLatestWinsSnapshotBufferHasIdentifier(bytes))
            return error(ExchangeDecodeErrorStage::Identifier, ExchangeDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload, LatestWinsSnapshotMaximumPayloadBytes);
        if (!SnapshotSchema::VerifySizePrefixedLatestWinsSnapshotBuffer(verifier))
            return error(ExchangeDecodeErrorStage::Verification, ExchangeDecodeErrorCode::VerificationFailed);

        const auto* root = SnapshotSchema::GetSizePrefixedLatestWinsSnapshot(bytes);
        const auto* header = root->header();
        if (header == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingSnapshotHeader);
        if (root->body_type() == SnapshotSchema::LatestWinsSnapshotBody::NONE || root->body() == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingBody);
        if (root->body_type() != SnapshotSchema::LatestWinsSnapshotBody::SpatialWorldView)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::UnknownBody,
                static_cast<std::size_t>(root->body_type()));
        }
        const auto* view = root->body_as_SpatialWorldView();
        if (view == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingBody);

        auto session = strongValue<SessionId>(header->target_session_id());
        auto generation = strongValue<SessionGeneration>(header->target_session_generation());
        auto player = strongValue<PlayerId>(header->target_player_id());
        auto entity = strongValue<EntityId>(header->target_entity_id());
        auto revision = strongValue<CanonicalRevision>(header->canonical_revision());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&session))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&generation))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&player))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&entity))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&revision))
            return *failure;

        std::optional<CommandSequence> acknowledgement;
        if (header->has_acknowledged_command_sequence())
        {
            auto decoded = strongValue<CommandSequence>(header->acknowledged_command_sequence());
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&decoded))
                return *failure;
            acknowledgement = *decodedValue(decoded);
        }
        else if (header->acknowledged_command_sequence() != 0)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation,
                ExchangeDecodeErrorCode::InvalidAcknowledgementPresence, header->acknowledged_command_sequence());
        }

        const auto* encodedEntries = view->entries();
        const std::size_t entryCount = encodedEntries == nullptr ? 0 : encodedEntries->size();
        if (entryCount > MaximumSpatialWorldViewEntries)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManySnapshotEntries,
                entryCount, MaximumSpatialWorldViewEntries);
        }
        std::vector<SpatialEntitySnapshot> entries;
        entries.reserve(entryCount);
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            auto decoded = decodeEntry(*encodedEntries->Get(static_cast<flatbuffers::uoffset_t>(index)), index);
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&decoded))
                return *failure;
            entries.push_back(std::get<SpatialEntitySnapshot>(std::move(decoded)));
        }
        auto worldView = SpatialWorldView::create(entries);
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&worldView))
            return *failure;

        return LatestWinsSnapshot(LatestWinsSnapshotHeader(*decodedValue(session), *decodedValue(generation),
                                      *decodedValue(player), *decodedValue(entity), *decodedValue(revision), acknowledgement),
            std::get<SpatialWorldView>(std::move(worldView)));
    }

    ReliableObservationBatchDecodeResult decodeReliableObservationBatch(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ObservationSchema::SizePrefixedReliableObservationBatchBufferHasIdentifier(bytes))
            return error(ExchangeDecodeErrorStage::Identifier, ExchangeDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!ObservationSchema::VerifySizePrefixedReliableObservationBatchBuffer(verifier))
            return error(ExchangeDecodeErrorStage::Verification, ExchangeDecodeErrorCode::VerificationFailed);
        const auto* root = ObservationSchema::GetSizePrefixedReliableObservationBatch(bytes);
        const auto* header = root->header();
        if (header == nullptr)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingObservationHeader);
        auto session = strongValue<SessionId>(header->target_session_id());
        auto generation = strongValue<SessionGeneration>(header->target_session_generation());
        auto revision = strongValue<CanonicalRevision>(header->canonical_revision());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&session)) return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&generation)) return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&revision)) return *failure;
        const auto* encoded = root->changes();
        const std::size_t count = encoded == nullptr ? 0 : encoded->size();
        if (count > MaximumObservationChanges)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManyObservationChanges,
                count, MaximumObservationChanges);
        std::vector<ObservationChange> changes;
        changes.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(index));
            auto player = strongValue<PlayerId>(current->player_id(), index);
            auto entity = strongValue<EntityId>(current->entity_id(), index);
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&player)) return *failure;
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&entity)) return *failure;
            changes.push_back({ *decodedValue(player), *decodedValue(entity),
                static_cast<ObservationChangeKind>(current->kind()) });
        }
        return ReliableObservationBatch::create(*decodedValue(session), *decodedValue(generation),
            *decodedValue(revision), changes);
    }

    ReliableInterestBaselineDecodeResult decodeReliableInterestBaseline(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!BaselineSchema::SizePrefixedReliableInterestBaselineBufferHasIdentifier(bytes))
            return error(ExchangeDecodeErrorStage::Identifier, ExchangeDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!BaselineSchema::VerifySizePrefixedReliableInterestBaselineBuffer(verifier))
            return error(ExchangeDecodeErrorStage::Verification, ExchangeDecodeErrorCode::VerificationFailed);
        const auto* root = BaselineSchema::GetSizePrefixedReliableInterestBaseline(bytes);
        const auto* header = root->header();
        if (!header)
            return error(ExchangeDecodeErrorStage::SemanticValidation,
                ExchangeDecodeErrorCode::MissingInterestBaselineHeader);
        auto session = strongValue<SessionId>(header->target_session_id());
        auto generation = strongValue<SessionGeneration>(header->target_session_generation());
        auto revision = strongValue<CanonicalRevision>(header->canonical_revision());
        auto stateVersion = strongValue<CanonicalStateVersion>(header->canonical_state_version());
        auto tick = strongValue<ServerTick>(header->server_tick());
        const std::array failures{ std::get_if<ExchangeDecodeError>(&session),
            std::get_if<ExchangeDecodeError>(&generation), std::get_if<ExchangeDecodeError>(&revision),
            std::get_if<ExchangeDecodeError>(&stateVersion), std::get_if<ExchangeDecodeError>(&tick) };
        for (const auto* failure : failures)
            if (failure) return *failure;
        const auto* encoded = root->members();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumInterestMembers)
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::TooManyInterestMembers,
                count, MaximumInterestMembers);
        std::vector<InterestMember> members;
        members.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(index));
            auto player = strongValue<PlayerId>(current->player_id(), index);
            auto entity = strongValue<EntityId>(current->entity_id(), index);
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&player)) return *failure;
            if (const auto* failure = std::get_if<ExchangeDecodeError>(&entity)) return *failure;
            members.push_back({ *decodedValue(player), *decodedValue(entity) });
        }
        return ReliableInterestBaseline::create(*decodedValue(session), *decodedValue(generation),
            *decodedValue(revision), *decodedValue(stateVersion), *decodedValue(tick), members);
    }

    SessionResyncRequestDecodeResult decodeSessionResyncRequest(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ResyncSchema::SizePrefixedSessionResyncRequestBufferHasIdentifier(bytes))
            return error(ExchangeDecodeErrorStage::Identifier, ExchangeDecodeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!ResyncSchema::VerifySizePrefixedSessionResyncRequestBuffer(verifier))
            return error(ExchangeDecodeErrorStage::Verification, ExchangeDecodeErrorCode::VerificationFailed);
        const auto* root = ResyncSchema::GetSizePrefixedSessionResyncRequest(bytes);
        auto session = strongValue<SessionId>(root->session_id());
        auto generation = strongValue<SessionGeneration>(root->session_generation());
        auto stateVersion = strongValue<CanonicalStateVersion>(root->last_observed_state_version());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&session)) return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&generation)) return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&stateVersion)) return *failure;
        return SessionResyncRequest::create(*decodedValue(session), *decodedValue(generation),
            static_cast<ResyncReason>(root->reason()), *decodedValue(stateVersion));
    }
}
