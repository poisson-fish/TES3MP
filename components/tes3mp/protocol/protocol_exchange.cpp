#include <tes3mp/protocol_exchange.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/latest_wins_snapshot_generated.h"
#include "generated/reliable_operation_generated.h"

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

    SnapshotSchema::SpatialEntitySnapshot encodeEntry(const TES3MP::SpatialEntitySnapshot& entry)
    {
        const auto& transform = entry.transform();
        const auto position = transform.position();
        const auto orientation = transform.orientation();
        const auto velocity = entry.linearVelocity();
        return { entry.playerId().value(), entry.entityId().value(), entry.entityRevision().value(), entry.authorityEpoch().value(),
            entry.serverTick().value(), encodeCell(transform.cell()),
            SnapshotSchema::Position3(position.x(), position.y(), position.z()),
            SnapshotSchema::Orientation3(orientation.x().value(), orientation.y().value(), orientation.z().value()),
            SnapshotSchema::LinearVelocity3(velocity.x(), velocity.y(), velocity.z()) };
    }

    std::variant<TES3MP::SpatialEntitySnapshot, ExchangeDecodeError> decodeEntry(
        const SnapshotSchema::SpatialEntitySnapshot& entry, std::size_t index)
    {
        auto player = strongValue<TES3MP::PlayerId>(entry.player_id(), index);
        auto entity = strongValue<TES3MP::EntityId>(entry.entity_id(), index);
        auto revision = strongValue<TES3MP::EntityRevision>(entry.entity_revision(), index);
        auto epoch = strongValue<TES3MP::AuthorityEpoch>(entry.authority_epoch(), index);
        auto tick = strongValue<TES3MP::ServerTick>(entry.server_tick(), index);
        auto cellSpace = strongValue<TES3MP::CellSpaceId>(entry.cell().cell_space_id(), index);
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&player))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&entity))
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
        return TES3MP::SpatialEntitySnapshot(*decodedValue(tick), *decodedValue(player), *decodedValue(entity), *decodedValue(revision),
            *decodedValue(epoch),
            TES3MP::Transform(cell, TES3MP::Position3(position.x(), position.y(), position.z()),
                TES3MP::Orientation3(TES3MP::Turn32::fromValue(orientation.x()),
                    TES3MP::Turn32::fromValue(orientation.y()), TES3MP::Turn32::fromValue(orientation.z()))),
            TES3MP::LinearVelocity3(velocity.x(), velocity.y(), velocity.z()));
    }
}

namespace TES3MP
{
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
            command.observedServerTick().value());
        const auto& precondition = *value.header().entityPrecondition();
        const auto entityPrecondition
            = ReliableSchema::CreateEntityPrecondition(builder, precondition.entityId().value(),
                precondition.expectedRevision().value(), precondition.expectedAuthorityEpoch().value());
        const auto velocity = value.intent().desiredVelocity();
        const ReliableSchema::LinearVelocity3 encodedVelocity(velocity.x(), velocity.y(), velocity.z());
        const auto intent = ReliableSchema::CreatePlayerMotionIntent(builder, &encodedVelocity);
        const auto root = ReliableSchema::CreateReliableOperation(builder, commandHeader, entityPrecondition,
            ReliableSchema::ReliableOperationBody::PlayerMotionIntent, intent.Union());
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
            header.targetSessionId().value(), header.targetSessionGeneration().value(), header.serverTick().value(),
            hasAcknowledgement, acknowledgement);

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
        if (root->body_type() != ReliableSchema::ReliableOperationBody::PlayerMotionIntent)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::UnknownBody,
                static_cast<std::size_t>(root->body_type()));
        }
        const auto* intent = root->body_as_PlayerMotionIntent();
        if (intent == nullptr || intent->desired_velocity() == nullptr)
        {
            return error(ExchangeDecodeErrorStage::SemanticValidation, ExchangeDecodeErrorCode::MissingDesiredVelocity);
        }

        auto session = strongValue<SessionId>(command->session_id());
        auto generation = strongValue<SessionGeneration>(command->session_generation());
        auto sequence = strongValue<CommandSequence>(command->command_sequence());
        auto commandId = strongValue<CommandId>(command->command_id());
        auto observedTick = strongValue<ServerTick>(command->observed_server_tick());
        auto entity = strongValue<EntityId>(precondition->entity_id());
        auto revision = strongValue<EntityRevision>(precondition->expected_revision());
        auto epoch = strongValue<AuthorityEpoch>(precondition->expected_authority_epoch());
        const std::array failures{ std::get_if<ExchangeDecodeError>(&session),
            std::get_if<ExchangeDecodeError>(&generation), std::get_if<ExchangeDecodeError>(&sequence),
            std::get_if<ExchangeDecodeError>(&commandId), std::get_if<ExchangeDecodeError>(&observedTick),
            std::get_if<ExchangeDecodeError>(&entity), std::get_if<ExchangeDecodeError>(&revision),
            std::get_if<ExchangeDecodeError>(&epoch) };
        for (const auto* failure : failures)
        {
            if (failure != nullptr)
                return *failure;
        }

        const auto* velocity = intent->desired_velocity();
        ReliableOperationHeader header(
            ClientCommandHeader(*decodedValue(session), *decodedValue(generation), *decodedValue(sequence),
                *decodedValue(commandId), *decodedValue(observedTick)),
            EntityPrecondition(*decodedValue(entity), *decodedValue(revision), *decodedValue(epoch)));
        return ReliableOperation::create(
            header, PlayerMotionIntent(LinearVelocity3(velocity->x(), velocity->y(), velocity->z())));
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
        auto tick = strongValue<ServerTick>(header->server_tick());
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&session))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&generation))
            return *failure;
        if (const auto* failure = std::get_if<ExchangeDecodeError>(&tick))
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
                                      *decodedValue(tick), acknowledgement),
            std::get<SpatialWorldView>(std::move(worldView)));
    }
}
