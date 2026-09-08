#include <tes3mp/combat_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include "generated/client_melee_attack_command_generated.h"
#include "generated/latest_wins_combat_snapshot_generated.h"
#include "generated/reliable_combat_event_batch_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <cmath>
#include <array>
#include <optional>

namespace
{
    namespace Command = TES3MP::Protocol::Schema::CombatCommand;
    namespace Snapshot = TES3MP::Protocol::Schema::CombatSnapshot;
    namespace Event = TES3MP::Protocol::Schema::CombatEvent;
    using Error = TES3MP::CombatReplicationDecodeError;
    using Code = TES3MP::CombatReplicationDecodeErrorCode;
    constexpr std::size_t Prefix = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t Minimum = Prefix + sizeof(flatbuffers::uoffset_t) + 4;

    Error error(Code code, std::size_t observed = 0, std::size_t limit = 0, std::size_t index = 0)
    { return { code, observed, limit, index }; }

    std::optional<Error> prefix(std::span<const std::byte> payload, std::size_t maximum)
    {
        if (payload.size() < Minimum) return error(Code::PayloadTooSmall, payload.size(), Minimum);
        if (payload.size() > maximum) return error(Code::PayloadTooLarge, payload.size(), maximum);
        const auto declared = flatbuffers::GetSizePrefixedBufferLength(
            reinterpret_cast<const std::uint8_t*>(payload.data()));
        if (declared != payload.size()) return error(Code::PayloadLengthMismatch, payload.size(), declared);
        return std::nullopt;
    }

    flatbuffers::Verifier verifier(std::span<const std::byte> payload, std::size_t maximum)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = 8; options.max_tables = 8; options.max_size = maximum + 1;
        options.check_alignment = true; options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    template <class T> std::variant<T, Error> strong(std::uint64_t raw, std::size_t index = 0)
    {
        auto value = T::fromValue(raw);
        return value ? std::variant<T, Error>(*value)
                     : std::variant<T, Error>(error(Code::InvalidStrongValue, raw, 0, index));
    }
    template <class T> const T* value(const std::variant<T, Error>& result) { return std::get_if<T>(&result); }

    std::optional<TES3MP::MeleeAttackType> attackType(Command::MeleeAttackType type)
    {
        switch (type)
        {
            case Command::MeleeAttackType::Chop: return TES3MP::MeleeAttackType::Chop;
            case Command::MeleeAttackType::Slash: return TES3MP::MeleeAttackType::Slash;
            case Command::MeleeAttackType::Thrust: return TES3MP::MeleeAttackType::Thrust;
            default: return std::nullopt;
        }
    }
}

namespace TES3MP
{
    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> LatestWinsCombatSnapshot::create(
        SessionId session, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision,
        PlayerId self, CombatRevision selfRevision, float selfFatigue, std::span<const ActorCombatSnapshot> actors)
    {
        if (!std::isfinite(selfFatigue)) return error(Code::InvalidFloat);
        if (actors.size() > MaximumCombatSnapshotActors)
            return error(Code::TooManyEntries, actors.size(), MaximumCombatSnapshotActors);
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            if (!std::isfinite(actors[i].health) || !std::isfinite(actors[i].fatigue))
                return error(Code::InvalidFloat, 0, 0, i);
            if (i && actors[i - 1].actorId >= actors[i].actorId)
                return error(Code::EntriesNotStrictlySorted, actors[i].actorId.value(),
                    actors[i - 1].actorId.value(), i);
        }
        return LatestWinsCombatSnapshot(session, generation, tick, canonicalRevision, self, selfRevision,
            selfFatigue, std::vector(actors.begin(), actors.end()));
    }

    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> ReliableCombatEventBatch::create(
        SessionId session, SessionGeneration generation, ServerTick tick, CanonicalRevision revision,
        std::span<const MeleeCombatEvent> events)
    {
        if (events.size() > MaximumCombatEventsPerBatch)
            return error(Code::TooManyEntries, events.size(), MaximumCombatEventsPerBatch);
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (!std::isfinite(events[i].damage)) return error(Code::InvalidFloat, 0, 0, i);
            if (static_cast<std::uint8_t>(events[i].damagedStat) > static_cast<std::uint8_t>(MeleeDamageStat::Fatigue))
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(events[i].damagedStat), 0, i);
        }
        return ReliableCombatEventBatch(session, generation, tick, revision,
            std::vector(events.begin(), events.end()));
    }

    std::vector<std::byte> encodeClientMeleeAttackCommand(const ClientMeleeAttackCommand& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = Command::CreateClientCommandHeader(builder, input.sessionId.value(),
            input.sessionGeneration.value(), input.commandSequence.value(), input.commandId.value(),
            input.observedCanonicalRevision.value());
        const auto root = Command::CreateClientMeleeAttackCommand(builder, header,
            input.targetActorId ? input.targetActorId->value() : 0,
            input.sourceServerTick.value(), input.expectedAttackerRevision.value(),
            input.expectedTargetRevision.value(), static_cast<Command::MeleeAttackType>(input.attackType),
            input.attackStrength);
        Command::FinishSizePrefixedClientMeleeAttackCommandBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeLatestWinsCombatSnapshot(const LatestWinsCombatSnapshot& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = Snapshot::CreateCombatSnapshotHeader(builder, input.targetSessionId().value(),
            input.targetSessionGeneration().value(), input.serverTick().value(), input.canonicalRevision().value(),
            input.selfPlayerId().value(), input.selfCombatRevision().value(), input.selfFatigue());
        std::vector<Snapshot::ActorCombatSnapshot> actors;
        actors.reserve(input.actors().size());
        for (const auto& actor : input.actors())
            actors.emplace_back(actor.actorId.value(), actor.combatRevision.value(), actor.health, actor.fatigue,
                actor.dead);
        const auto root = Snapshot::CreateLatestWinsCombatSnapshot(
            builder, header, builder.CreateVectorOfStructs(actors));
        Snapshot::FinishSizePrefixedLatestWinsCombatSnapshotBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeReliableCombatEventBatch(const ReliableCombatEventBatch& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = Event::CreateCombatEventHeader(builder, input.targetSessionId().value(),
            input.targetSessionGeneration().value(), input.serverTick().value(), input.canonicalRevision().value());
        std::vector<Event::MeleeCombatEvent> events;
        events.reserve(input.events().size());
        for (const auto& event : input.events())
            events.emplace_back(event.attackerPlayerId.value(), event.targetActorId.value(),
                event.attackerCombatRevision.value(), event.targetCombatRevision.value(), event.damage,
                static_cast<Event::MeleeDamageStat>(event.damagedStat), event.hit, event.blocked, event.targetDied);
        const auto root = Event::CreateReliableCombatEventBatch(
            builder, header, builder.CreateVectorOfStructs(events));
        Event::FinishSizePrefixedReliableCombatEventBatchBuffer(builder, root);
        return take(builder);
    }

    std::variant<ClientMeleeAttackCommand, CombatReplicationDecodeError> decodeClientMeleeAttackCommand(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, ReliableOperationMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Command::SizePrefixedClientMeleeAttackCommandBufferHasIdentifier(bytes)) return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!Command::VerifySizePrefixedClientMeleeAttackCommandBuffer(checked)) return error(Code::VerificationFailed);
        const auto* root = Command::GetSizePrefixedClientMeleeAttackCommand(bytes);
        if (!root->header()) return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->session_id());
        auto generation = strong<SessionGeneration>(root->header()->session_generation());
        auto sequence = strong<CommandSequence>(root->header()->command_sequence());
        auto command = strong<CommandId>(root->header()->command_id());
        auto canonical = strong<CanonicalRevision>(root->header()->observed_canonical_revision());
        std::optional<ActorId> target;
        std::optional<Error> targetFailure;
        if (root->target_actor_id() != 0)
        {
            auto decodedTarget = strong<ActorId>(root->target_actor_id());
            if (auto* failure = std::get_if<Error>(&decodedTarget))
                targetFailure = *failure;
            else
                target = *value(decodedTarget);
        }
        auto tick = strong<ServerTick>(root->source_server_tick());
        auto attackerRevision = strong<CombatRevision>(root->expected_attacker_revision());
        auto targetRevision = strong<CombatRevision>(root->expected_target_revision());
        const auto type = attackType(root->attack_type());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&sequence), std::get_if<Error>(&command), std::get_if<Error>(&canonical),
            targetFailure ? &*targetFailure : nullptr, std::get_if<Error>(&tick), std::get_if<Error>(&attackerRevision),
            std::get_if<Error>(&targetRevision) };
        for (const auto* failure : failures) if (failure) return *failure;
        if (!type) return error(Code::InvalidAttackType, static_cast<std::size_t>(root->attack_type()));
        if (!std::isfinite(root->attack_strength())) return error(Code::InvalidFloat);
        if (root->attack_strength() < 0.f || root->attack_strength() > 1.f)
            return error(Code::InvalidAttackStrength);
        return ClientMeleeAttackCommand{ *value(session), *value(generation), *value(sequence), *value(command),
            *value(canonical), target, *value(tick), *value(attackerRevision), *value(targetRevision),
            *type, root->attack_strength() };
    }

    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> decodeLatestWinsCombatSnapshot(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, LatestWinsSnapshotMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Snapshot::SizePrefixedLatestWinsCombatSnapshotBufferHasIdentifier(bytes)) return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, LatestWinsSnapshotMaximumPayloadBytes);
        if (!Snapshot::VerifySizePrefixedLatestWinsCombatSnapshotBuffer(checked)) return error(Code::VerificationFailed);
        const auto* root = Snapshot::GetSizePrefixedLatestWinsCombatSnapshot(bytes);
        if (!root->header()) return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonical = strong<CanonicalRevision>(root->header()->canonical_revision());
        auto self = strong<PlayerId>(root->header()->self_player_id());
        auto selfRevision = strong<CombatRevision>(root->header()->self_combat_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&canonical), std::get_if<Error>(&self),
            std::get_if<Error>(&selfRevision) };
        for (const auto* failure : failures) if (failure) return *failure;
        if (!std::isfinite(root->header()->self_fatigue())) return error(Code::InvalidFloat);
        const auto* encoded = root->actors();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumCombatSnapshotActors) return error(Code::TooManyEntries, count, MaximumCombatSnapshotActors);
        std::vector<ActorCombatSnapshot> actors; actors.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(i));
            auto actor = strong<ActorId>(current->actor_id(), i);
            auto revision = strong<CombatRevision>(current->combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&actor)) return *failure;
            if (auto* failure = std::get_if<Error>(&revision)) return *failure;
            actors.push_back({ *value(actor), *value(revision), current->health(), current->fatigue(), current->dead() });
        }
        return LatestWinsCombatSnapshot::create(*value(session), *value(generation), *value(tick), *value(canonical),
            *value(self), *value(selfRevision), root->header()->self_fatigue(), actors);
    }

    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> decodeReliableCombatEventBatch(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, ReliableOperationMaximumPayloadBytes)) return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Event::SizePrefixedReliableCombatEventBatchBufferHasIdentifier(bytes)) return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!Event::VerifySizePrefixedReliableCombatEventBatchBuffer(checked)) return error(Code::VerificationFailed);
        const auto* root = Event::GetSizePrefixedReliableCombatEventBatch(bytes);
        if (!root->header()) return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonical = strong<CanonicalRevision>(root->header()->canonical_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&canonical) };
        for (const auto* failure : failures) if (failure) return *failure;
        const auto* encoded = root->events(); const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumCombatEventsPerBatch) return error(Code::TooManyEntries, count, MaximumCombatEventsPerBatch);
        std::vector<MeleeCombatEvent> events; events.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto* current = encoded->Get(static_cast<flatbuffers::uoffset_t>(i));
            auto attacker = strong<PlayerId>(current->attacker_player_id(), i);
            auto target = strong<ActorId>(current->target_actor_id(), i);
            auto attackerRevision = strong<CombatRevision>(current->attacker_combat_revision(), i);
            auto targetRevision = strong<CombatRevision>(current->target_combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&attacker)) return *failure;
            if (auto* failure = std::get_if<Error>(&target)) return *failure;
            if (auto* failure = std::get_if<Error>(&attackerRevision)) return *failure;
            if (auto* failure = std::get_if<Error>(&targetRevision)) return *failure;
            if (current->damage_stat() != Event::MeleeDamageStat::Health
                && current->damage_stat() != Event::MeleeDamageStat::Fatigue)
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(current->damage_stat()), 0, i);
            events.push_back({ *value(attacker), *value(target), *value(attackerRevision), *value(targetRevision),
                current->damage(), static_cast<MeleeDamageStat>(current->damage_stat()), current->hit(),
                current->blocked(), current->target_died() });
        }
        return ReliableCombatEventBatch::create(
            *value(session), *value(generation), *value(tick), *value(canonical), events);
    }
}
