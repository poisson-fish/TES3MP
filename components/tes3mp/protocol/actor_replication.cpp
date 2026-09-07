#include <tes3mp/actor_replication.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/latest_wins_actor_snapshot_generated.h"
#include "generated/reliable_actor_interest_baseline_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <optional>

namespace
{
    namespace BaselineSchema = TES3MP::Protocol::Schema::ActorBaseline;
    namespace SnapshotSchema = TES3MP::Protocol::Schema::ActorSnapshot;
    using Error = TES3MP::ActorReplicationDecodeError;
    using Code = TES3MP::ActorReplicationDecodeErrorCode;

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

    SnapshotSchema::Cell encodeCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
            return { interior->cellSpace().value(), 0, 0, SnapshotSchema::CellKind::Interior };
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(),
            SnapshotSchema::CellKind::Exterior };
    }

    std::variant<TES3MP::CellId, Error> decodeCell(const SnapshotSchema::Cell& cell)
    {
        auto space = strong<TES3MP::CellSpaceId>(cell.cell_space_id());
        if (const auto* failure = std::get_if<Error>(&space)) return *failure;
        if (cell.kind() == SnapshotSchema::CellKind::Interior)
        {
            if (cell.grid_x() != 0 || cell.grid_y() != 0)
                return error(Code::InvalidInteriorGrid);
            return TES3MP::CellId::interior(*value(space));
        }
        if (cell.kind() == SnapshotSchema::CellKind::Exterior)
            return TES3MP::CellId::exterior(*value(space), cell.grid_x(), cell.grid_y());
        return error(Code::InvalidCellKind, static_cast<std::size_t>(cell.kind()));
    }

    SnapshotSchema::ActorActivity encodeActivity(TES3MP::ActorActivity activity) noexcept
    {
        return static_cast<SnapshotSchema::ActorActivity>(static_cast<std::uint8_t>(activity) + 1);
    }

    std::optional<TES3MP::ActorActivity> decodeActivity(SnapshotSchema::ActorActivity activity) noexcept
    {
        switch (activity)
        {
            case SnapshotSchema::ActorActivity::Idle: return TES3MP::ActorActivity::Idle;
            case SnapshotSchema::ActorActivity::Travel: return TES3MP::ActorActivity::Travel;
            case SnapshotSchema::ActorActivity::Wander: return TES3MP::ActorActivity::Wander;
            default: return std::nullopt;
        }
    }
}

namespace TES3MP
{
    std::variant<ReliableActorInterestBaseline, ActorReplicationDecodeError>
    ReliableActorInterestBaseline::create(SessionId targetSessionId, SessionGeneration targetSessionGeneration,
        ServerTick serverTick, CanonicalRevision canonicalRevision, std::span<const ActorInterestMember> members)
    {
        if (members.size() > MaximumActorInterestMembers)
            return error(Code::TooManyEntries, members.size(), MaximumActorInterestMembers);
        for (std::size_t index = 1; index < members.size(); ++index)
        {
            if (members[index - 1].actorId >= members[index].actorId
                || std::ranges::any_of(members.first(index), [&](const auto& prior) {
                    return prior.entityId == members[index].entityId;
                }))
                return error(Code::EntriesNotStrictlySorted, members[index].actorId.value(),
                    members[index - 1].actorId.value(), index);
        }
        return ReliableActorInterestBaseline(targetSessionId, targetSessionGeneration, serverTick, canonicalRevision,
            std::vector<ActorInterestMember>(members.begin(), members.end()));
    }

    std::variant<ActorWorldView, ActorReplicationDecodeError> ActorWorldView::create(
        std::span<const ActorSpatialSnapshot> entries)
    {
        if (entries.size() > MaximumActorInterestMembers)
            return error(Code::TooManyEntries, entries.size(), MaximumActorInterestMembers);
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (static_cast<std::uint8_t>(entries[index].activity())
                > static_cast<std::uint8_t>(ActorActivity::Wander))
                return error(Code::InvalidActivity, static_cast<std::size_t>(entries[index].activity()),
                    static_cast<std::size_t>(ActorActivity::Wander), index);
            if (index != 0 && entries[index - 1].actorId() >= entries[index].actorId())
                return error(Code::EntriesNotStrictlySorted, entries[index].actorId().value(),
                    entries[index - 1].actorId().value(), index);
        }
        return ActorWorldView(std::vector<ActorSpatialSnapshot>(entries.begin(), entries.end()));
    }

    std::vector<std::byte> encodeReliableActorInterestBaseline(const ReliableActorInterestBaseline& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = BaselineSchema::CreateActorBaselineHeader(builder, input.targetSessionId().value(),
            input.targetSessionGeneration().value(), input.serverTick().value(), input.canonicalRevision().value());
        std::vector<BaselineSchema::ActorInterestMember> members;
        members.reserve(input.members().size());
        for (const auto& member : input.members())
            members.emplace_back(member.actorId.value(), member.entityId.value(), member.prototypeId.value());
        const auto encodedMembers = builder.CreateVectorOfStructs(members);
        const auto root = BaselineSchema::CreateReliableActorInterestBaseline(builder, header, encodedMembers);
        BaselineSchema::FinishSizePrefixedReliableActorInterestBaselineBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeLatestWinsActorSnapshot(const LatestWinsActorSnapshot& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = SnapshotSchema::CreateActorSnapshotHeader(builder, input.targetSessionId().value(),
            input.targetSessionGeneration().value(), input.serverTick().value(), input.canonicalRevision().value());
        std::vector<SnapshotSchema::ActorSpatialSnapshot> entries;
        entries.reserve(input.view().entries().size());
        for (const auto& entry : input.view().entries())
        {
            const auto cell = encodeCell(entry.transform().cell());
            const auto& position = entry.transform().position();
            const SnapshotSchema::Position3 encodedPosition(position.x(), position.y(), position.z());
            const auto& orientation = entry.transform().orientation();
            const SnapshotSchema::Orientation3 encodedOrientation(
                orientation.x().value(), orientation.y().value(), orientation.z().value());
            const auto velocity = entry.linearVelocity();
            const SnapshotSchema::LinearVelocity3 encodedVelocity(velocity.x(), velocity.y(), velocity.z());
            entries.emplace_back(entry.actorId().value(), entry.entityId().value(), entry.prototypeId().value(),
                entry.entityRevision().value(), entry.authorityEpoch().value(), entry.serverTick().value(), cell,
                encodedPosition, encodedOrientation, encodedVelocity, encodeActivity(entry.activity()));
        }
        const auto encodedEntries = builder.CreateVectorOfStructs(entries);
        const auto viewOffset = SnapshotSchema::CreateActorWorldView(builder, encodedEntries);
        const auto root = SnapshotSchema::CreateLatestWinsActorSnapshot(builder, header, viewOffset);
        SnapshotSchema::FinishSizePrefixedLatestWinsActorSnapshotBuffer(builder, root);
        return take(builder);
    }

    ReliableActorInterestBaselineDecodeResult decodeReliableActorInterestBaseline(
        std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!BaselineSchema::SizePrefixedReliableActorInterestBaselineBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!BaselineSchema::VerifySizePrefixedReliableActorInterestBaselineBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = BaselineSchema::GetSizePrefixedReliableActorInterestBaseline(bytes);
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
        if (count > MaximumActorInterestMembers)
            return error(Code::TooManyEntries, count, MaximumActorInterestMembers);
        std::vector<ActorInterestMember> members;
        members.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(index));
            auto actor = strong<ActorId>(current->actor_id(), index);
            auto entity = strong<EntityId>(current->entity_id(), index);
            auto prototype = strong<ActorPrototypeId>(current->prototype_id(), index);
            if (const auto* failure = std::get_if<Error>(&actor)) return *failure;
            if (const auto* failure = std::get_if<Error>(&entity)) return *failure;
            if (const auto* failure = std::get_if<Error>(&prototype)) return *failure;
            members.push_back({ *value(actor), *value(entity), *value(prototype) });
        }
        return ReliableActorInterestBaseline::create(
            *value(session), *value(generation), *value(tick), *value(canonicalRevision), members);
    }

    LatestWinsActorSnapshotDecodeResult decodeLatestWinsActorSnapshot(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, LatestWinsSnapshotMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!SnapshotSchema::SizePrefixedLatestWinsActorSnapshotBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, LatestWinsSnapshotMaximumPayloadBytes);
        if (!SnapshotSchema::VerifySizePrefixedLatestWinsActorSnapshotBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = SnapshotSchema::GetSizePrefixedLatestWinsActorSnapshot(bytes);
        if (!root->header()) return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonicalRevision = strong<CanonicalRevision>(root->header()->canonical_revision());
        if (const auto* failure = std::get_if<Error>(&session)) return *failure;
        if (const auto* failure = std::get_if<Error>(&generation)) return *failure;
        if (const auto* failure = std::get_if<Error>(&tick)) return *failure;
        if (const auto* failure = std::get_if<Error>(&canonicalRevision)) return *failure;
        const auto* encoded = root->view() ? root->view()->entries() : nullptr;
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumActorInterestMembers)
            return error(Code::TooManyEntries, count, MaximumActorInterestMembers);
        std::vector<ActorSpatialSnapshot> entries;
        entries.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(index));
            auto actor = strong<ActorId>(current->actor_id(), index);
            auto entity = strong<EntityId>(current->entity_id(), index);
            auto prototype = strong<ActorPrototypeId>(current->prototype_id(), index);
            auto revision = strong<EntityRevision>(current->entity_revision(), index);
            auto epoch = strong<AuthorityEpoch>(current->authority_epoch(), index);
            auto entryTick = strong<ServerTick>(current->server_tick(), index);
            auto cell = decodeCell(current->cell());
            const auto activity = decodeActivity(current->activity());
            const std::array failures{ std::get_if<Error>(&actor), std::get_if<Error>(&entity),
                std::get_if<Error>(&prototype), std::get_if<Error>(&revision), std::get_if<Error>(&epoch),
                std::get_if<Error>(&entryTick), std::get_if<Error>(&cell) };
            for (const auto* failure : failures) if (failure) return *failure;
            if (!activity) return error(Code::InvalidActivity, static_cast<std::size_t>(current->activity()), 0, index);
            const auto& position = current->position();
            const auto& orientation = current->orientation();
            const auto& velocity = current->linear_velocity();
            entries.emplace_back(*value(entryTick), *value(actor), *value(entity), *value(prototype),
                *value(revision), *value(epoch), Transform(std::get<CellId>(cell),
                    Position3(position.x(), position.y(), position.z()), Orientation3(
                        Turn32::fromValue(orientation.x()), Turn32::fromValue(orientation.y()),
                        Turn32::fromValue(orientation.z()))),
                LinearVelocity3(velocity.x(), velocity.y(), velocity.z()), *activity);
        }
        auto viewResult = ActorWorldView::create(entries);
        if (const auto* failure = std::get_if<Error>(&viewResult)) return *failure;
        return LatestWinsActorSnapshot(*value(session), *value(generation), *value(tick), *value(canonicalRevision),
            std::get<ActorWorldView>(std::move(viewResult)));
    }
}
