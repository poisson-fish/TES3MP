#include <tes3mp/interactive_object_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include "generated/client_interact_object_command_generated.h"
#include "generated/reliable_interactive_object_interest_baseline_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <optional>

namespace
{
    namespace BaselineSchema = TES3MP::Protocol::Schema::InteractiveObjectBaseline;
    namespace CommandSchema = TES3MP::Protocol::Schema::InteractObject;
    using Error = TES3MP::InteractiveObjectReplicationDecodeError;
    using Code = TES3MP::InteractiveObjectReplicationDecodeErrorCode;

    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;

    constexpr Error error(Code code, std::size_t observed = 0, std::size_t limit = 0,
        std::size_t index = 0) noexcept
    {
        return { code, observed, limit, index };
    }

    std::optional<Error> validatePrefix(std::span<const std::byte> payload, std::size_t maximum) noexcept
    {
        if (payload.size() < MinimumBytes)
            return error(Code::PayloadTooSmall, payload.size(), MinimumBytes);
        if (payload.size() > maximum)
            return error(Code::PayloadTooLarge, payload.size(), maximum);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const auto declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
            return error(Code::PayloadLengthMismatch, payload.size(), declared);
        return std::nullopt;
    }

    flatbuffers::Verifier verifier(std::span<const std::byte> payload, std::size_t maximum)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = 8;
        options.max_tables = 8;
        options.max_size = maximum + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(
            reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    template <class Value>
    std::variant<Value, Error> strong(std::uint64_t raw, std::size_t index = 0)
    {
        auto value = Value::fromValue(raw);
        return value ? std::variant<Value, Error>(*value)
                     : std::variant<Value, Error>(error(Code::InvalidStrongValue, raw, 0, index));
    }

    template <class Value>
    const Value* value(const std::variant<Value, Error>& decoded) { return std::get_if<Value>(&decoded); }

    CommandSchema::Cell encodeCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
            return { interior->cellSpace().value(), 0, 0, CommandSchema::CellKind::Interior };
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(),
            CommandSchema::CellKind::Exterior };
    }

    std::variant<TES3MP::CellId, Error> decodeCell(const CommandSchema::Cell& cell)
    {
        auto space = strong<TES3MP::CellSpaceId>(cell.cell_space_id());
        if (const auto* failure = std::get_if<Error>(&space)) return *failure;
        if (cell.kind() == CommandSchema::CellKind::Interior)
        {
            if (cell.grid_x() != 0 || cell.grid_y() != 0)
                return error(Code::InvalidInteriorGrid);
            return TES3MP::CellId::interior(*value(space));
        }
        if (cell.kind() == CommandSchema::CellKind::Exterior)
            return TES3MP::CellId::exterior(*value(space), cell.grid_x(), cell.grid_y());
        return error(Code::InvalidCellKind, static_cast<std::size_t>(cell.kind()));
    }
}

namespace TES3MP
{
    std::variant<ReliableInteractiveObjectInterestBaseline, InteractiveObjectReplicationDecodeError>
    ReliableInteractiveObjectInterestBaseline::create(SessionId targetSessionId,
        SessionGeneration targetSessionGeneration, ServerTick serverTick, CanonicalRevision canonicalRevision,
        std::span<const InteractiveObjectInterestMember> members)
    {
        if (members.size() > MaximumInteractiveObjectInterestMembers)
            return error(Code::TooManyEntries, members.size(), MaximumInteractiveObjectInterestMembers);
        for (std::size_t index = 0; index < members.size(); ++index)
        {
            if (index > 0 && members[index - 1].objectId >= members[index].objectId)
                return error(Code::EntriesNotStrictlySorted, members[index].objectId.value(),
                    members[index - 1].objectId.value(), index);
            if (static_cast<std::uint8_t>(members[index].doorState) > static_cast<std::uint8_t>(DoorState::Open))
                return error(Code::InvalidDoorState, static_cast<std::size_t>(members[index].doorState), 0, index);
            if (static_cast<std::uint8_t>(members[index].lockState) > static_cast<std::uint8_t>(LockState::Locked))
                return error(Code::InvalidLockState, static_cast<std::size_t>(members[index].lockState), 0, index);
            if (static_cast<std::uint8_t>(members[index].trapState) > static_cast<std::uint8_t>(TrapState::Armed))
                return error(Code::InvalidTrapState, static_cast<std::size_t>(members[index].trapState), 0, index);
        }
        return ReliableInteractiveObjectInterestBaseline(targetSessionId, targetSessionGeneration, serverTick,
            canonicalRevision, std::vector<InteractiveObjectInterestMember>(members.begin(), members.end()));
    }

    std::vector<std::byte> encodeReliableInteractiveObjectInterestBaseline(
        const ReliableInteractiveObjectInterestBaseline& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = BaselineSchema::CreateInteractiveObjectBaselineHeader(builder,
            input.targetSessionId().value(), input.targetSessionGeneration().value(), input.serverTick().value(),
            input.canonicalRevision().value());
        std::vector<BaselineSchema::InteractiveObjectMember> members;
        members.reserve(input.members().size());
        for (const auto& member : input.members())
        {
            members.emplace_back(member.objectId.value(), member.revision.value(),
                static_cast<BaselineSchema::DoorState>(member.doorState),
                static_cast<BaselineSchema::LockState>(member.lockState),
                static_cast<BaselineSchema::TrapState>(member.trapState),
                static_cast<std::uint8_t>(0));
        }
        const auto encodedMembers = builder.CreateVectorOfStructs(members);
        const auto root = BaselineSchema::CreateReliableInteractiveObjectInterestBaseline(builder, header, encodedMembers);
        BaselineSchema::FinishSizePrefixedReliableInteractiveObjectInterestBaselineBuffer(builder, root);
        return take(builder);
    }

    ReliableInteractiveObjectInterestBaselineDecodeResult decodeReliableInteractiveObjectInterestBaseline(
        std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!BaselineSchema::SizePrefixedReliableInteractiveObjectInterestBaselineBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!BaselineSchema::VerifySizePrefixedReliableInteractiveObjectInterestBaselineBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = BaselineSchema::GetSizePrefixedReliableInteractiveObjectInterestBaseline(bytes);
        if (!root->header()) return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonicalRevision = strong<CanonicalRevision>(root->header()->canonical_revision());
        if (const auto* failure = std::get_if<Error>(&session)) return *failure;
        if (const auto* failure = std::get_if<Error>(&generation)) return *failure;
        if (const auto* failure = std::get_if<Error>(&tick)) return *failure;
        if (const auto* failure = std::get_if<Error>(&canonicalRevision)) return *failure;

        const auto* encoded = root->members();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumInteractiveObjectInterestMembers)
            return error(Code::TooManyEntries, count, MaximumInteractiveObjectInterestMembers);

        std::vector<InteractiveObjectInterestMember> members;
        members.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(index));
            auto objectId = strong<InteractiveObjectId>(current->object_id(), index);
            auto revision = strong<ObjectRevision>(current->revision(), index);
            if (const auto* failure = std::get_if<Error>(&objectId)) return *failure;
            if (const auto* failure = std::get_if<Error>(&revision)) return *failure;
            if (static_cast<std::uint8_t>(current->door_state()) > static_cast<std::uint8_t>(DoorState::Open))
                return error(Code::InvalidDoorState, static_cast<std::size_t>(current->door_state()), 0, index);
            if (static_cast<std::uint8_t>(current->lock_state()) > static_cast<std::uint8_t>(LockState::Locked))
                return error(Code::InvalidLockState, static_cast<std::size_t>(current->lock_state()), 0, index);
            if (static_cast<std::uint8_t>(current->trap_state()) > static_cast<std::uint8_t>(TrapState::Armed))
                return error(Code::InvalidTrapState, static_cast<std::size_t>(current->trap_state()), 0, index);
            members.push_back({
                *value(objectId),
                *value(revision),
                static_cast<DoorState>(current->door_state()),
                static_cast<LockState>(current->lock_state()),
                static_cast<TrapState>(current->trap_state())
            });
        }
        return ReliableInteractiveObjectInterestBaseline::create(
            *value(session), *value(generation), *value(tick), *value(canonicalRevision), members);
    }

    std::vector<std::byte> encodeClientInteractObjectCommand(
        const ClientInteractObjectCommand& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = CommandSchema::CreateClientCommandHeader(builder,
            input.sessionId.value(), input.sessionGeneration.value(), input.commandSequence.value(),
            input.commandId.value(), input.observedCanonicalRevision.value());
        const auto cell = encodeCell(input.targetCell);
        const CommandSchema::Position3 origin(
            input.interactionOrigin.x(), input.interactionOrigin.y(), input.interactionOrigin.z());
        const auto kind = static_cast<CommandSchema::ObjectInteractionKind>(input.kind);
        const auto root = CommandSchema::CreateClientInteractObjectCommand(builder,
            header, input.objectId.value(), &cell, &origin, input.expectedRevision.value(),
            kind, input.requestedKey.has_value(), input.requestedKey ? input.requestedKey->value() : 0);
        CommandSchema::FinishSizePrefixedClientInteractObjectCommandBuffer(builder, root);
        return take(builder);
    }

    ClientInteractObjectCommandDecodeResult decodeClientInteractObjectCommand(
        std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!CommandSchema::SizePrefixedClientInteractObjectCommandBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!CommandSchema::VerifySizePrefixedClientInteractObjectCommandBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = CommandSchema::GetSizePrefixedClientInteractObjectCommand(bytes);
        if (!root->header() || !root->target_cell() || !root->interaction_origin())
            return error(Code::MissingHeader);

        auto session = strong<SessionId>(root->header()->session_id());
        auto generation = strong<SessionGeneration>(root->header()->session_generation());
        auto sequence = strong<CommandSequence>(root->header()->command_sequence());
        auto commandId = strong<CommandId>(root->header()->command_id());
        auto observedRevision = strong<CanonicalRevision>(root->header()->observed_canonical_revision());
        auto objectId = strong<InteractiveObjectId>(root->object_id());
        auto cell = decodeCell(*root->target_cell());
        auto expectedRevision = strong<ObjectRevision>(root->expected_revision());

        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&sequence), std::get_if<Error>(&commandId),
            std::get_if<Error>(&observedRevision), std::get_if<Error>(&objectId),
            std::get_if<Error>(&cell), std::get_if<Error>(&expectedRevision) };
        for (const auto* failure : failures) if (failure) return *failure;

        if (static_cast<std::uint8_t>(root->kind()) > static_cast<std::uint8_t>(ObjectInteractionKind::UnlockWithKey))
            return error(Code::InvalidInteractionKind, static_cast<std::size_t>(root->kind()));

        std::optional<KeyPrototypeId> requestedKey;
        if (root->has_requested_key())
        {
            auto key = strong<KeyPrototypeId>(root->requested_key_id());
            if (const auto* failure = std::get_if<Error>(&key)) return *failure;
            requestedKey = *value(key);
        }

        const auto* origin = root->interaction_origin();
        return ClientInteractObjectCommand{
            .sessionId = *value(session),
            .sessionGeneration = *value(generation),
            .commandSequence = *value(sequence),
            .commandId = *value(commandId),
            .observedCanonicalRevision = *value(observedRevision),
            .objectId = *value(objectId),
            .targetCell = std::get<CellId>(cell),
            .interactionOrigin = Position3(origin->x(), origin->y(), origin->z()),
            .expectedRevision = *value(expectedRevision),
            .kind = static_cast<ObjectInteractionKind>(root->kind()),
            .requestedKey = requestedKey
        };
    }
}
