#include <tes3mp/combat_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include "generated/client_melee_attack_command_generated.h"
#include "generated/latest_wins_combat_snapshot_generated.h"
#include "generated/reliable_combat_event_batch_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace
{
    namespace Command = TES3MP::Protocol::Schema::CombatCommand;
    namespace Snapshot = TES3MP::Protocol::Schema::CombatSnapshot;
    namespace Event = TES3MP::Protocol::Schema::CombatEvent;
    using Error = TES3MP::CombatReplicationDecodeError;
    using Code = TES3MP::CombatReplicationDecodeErrorCode;
    constexpr std::size_t Prefix = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t Minimum = Prefix + sizeof(flatbuffers::uoffset_t) + 4;

    template <class Struct>
    Struct copyStruct(const flatbuffers::Vector<const Struct*>* values, std::size_t index) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Struct>);
        Struct result{};
        std::memcpy(&result, values->Data() + index * sizeof(Struct), sizeof(Struct));
        return result;
    }

    Error error(Code code, std::size_t observed = 0, std::size_t limit = 0, std::size_t index = 0)
    {
        return { code, observed, limit, index };
    }

    std::optional<Error> prefix(std::span<const std::byte> payload, std::size_t maximum)
    {
        if (payload.size() < Minimum)
            return error(Code::PayloadTooSmall, payload.size(), Minimum);
        if (payload.size() > maximum)
            return error(Code::PayloadTooLarge, payload.size(), maximum);
        const auto declared
            = flatbuffers::GetSizePrefixedBufferLength(reinterpret_cast<const std::uint8_t*>(payload.data()));
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
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    template <class T>
    std::variant<T, Error> strong(std::uint64_t raw, std::size_t index = 0)
    {
        auto value = T::fromValue(raw);
        return value ? std::variant<T, Error>(*value)
                     : std::variant<T, Error>(error(Code::InvalidStrongValue, raw, 0, index));
    }
    template <class T>
    const T* value(const std::variant<T, Error>& result)
    {
        return std::get_if<T>(&result);
    }

    std::optional<TES3MP::MeleeAttackType> attackType(Command::MeleeAttackType type)
    {
        switch (type)
        {
            case Command::MeleeAttackType::Chop:
                return TES3MP::MeleeAttackType::Chop;
            case Command::MeleeAttackType::Slash:
                return TES3MP::MeleeAttackType::Slash;
            case Command::MeleeAttackType::Thrust:
                return TES3MP::MeleeAttackType::Thrust;
            default:
                return std::nullopt;
        }
    }
}

namespace TES3MP
{
    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> LatestWinsCombatSnapshot::create(
        SessionId session, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision,
        PlayerId self, CombatRevision selfRevision, float selfHealth, float selfMaximumHealth, float selfFatigue,
        float selfMaximumFatigue, float selfMagicka, float selfMaximumMagicka, bool selfDead,
        std::span<const ActorCombatSnapshot> actors, std::span<const CombatSkillSnapshot> skills,
        std::span<const PlayerCombatSnapshot> players, std::span<const ActiveMagicEffectSnapshot> activeEffects)
    {
        if (!std::isfinite(selfHealth) || !std::isfinite(selfMaximumHealth) || !std::isfinite(selfFatigue)
            || !std::isfinite(selfMaximumFatigue) || !std::isfinite(selfMagicka) || !std::isfinite(selfMaximumMagicka)
            || selfMaximumHealth <= 0.f || selfMaximumFatigue < 0.f || selfMaximumMagicka < 0.f || selfMagicka < 0.f
            || selfMagicka > selfMaximumMagicka)
            return error(Code::InvalidFloat);
        if (actors.size() > MaximumCombatSnapshotActors)
            return error(Code::TooManyEntries, actors.size(), MaximumCombatSnapshotActors);
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            if (!std::isfinite(actors[i].health) || !std::isfinite(actors[i].maximumHealth)
                || !std::isfinite(actors[i].fatigue) || !std::isfinite(actors[i].maximumFatigue)
                || !std::isfinite(actors[i].magicka) || !std::isfinite(actors[i].maximumMagicka)
                || actors[i].maximumHealth < 0.f || actors[i].maximumFatigue < 0.f || actors[i].maximumMagicka < 0.f
                || actors[i].magicka < 0.f || actors[i].magicka > actors[i].maximumMagicka)
                return error(Code::InvalidFloat, 0, 0, i);
            if (i && actors[i - 1].actorId >= actors[i].actorId)
                return error(
                    Code::EntriesNotStrictlySorted, actors[i].actorId.value(), actors[i - 1].actorId.value(), i);
        }
        if (players.size() > MaximumCombatSnapshotPlayers)
            return error(Code::TooManyEntries, players.size(), MaximumCombatSnapshotPlayers);
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            const auto& player = players[i];
            if (!std::isfinite(player.health) || !std::isfinite(player.maximumHealth) || !std::isfinite(player.fatigue)
                || !std::isfinite(player.maximumFatigue) || !std::isfinite(player.magicka)
                || !std::isfinite(player.maximumMagicka) || player.maximumHealth <= 0.f || player.maximumFatigue < 0.f
                || player.maximumMagicka < 0.f || player.magicka < 0.f || player.magicka > player.maximumMagicka)
                return error(Code::InvalidFloat, 0, 0, i);
            if (player.playerId == self || (i && players[i - 1].playerId >= player.playerId))
                return error(Code::EntriesNotStrictlySorted, player.playerId.value(), 0, i);
        }
        if (skills.size() != ReplicatedCombatSkillCount)
            return error(Code::TooManyEntries, skills.size(), ReplicatedCombatSkillCount);
        for (std::size_t i = 0; i < skills.size(); ++i)
        {
            if (static_cast<std::size_t>(skills[i].skill) != i)
                return error(Code::InvalidSkill, static_cast<std::size_t>(skills[i].skill), i, i);
            if (!std::isfinite(skills[i].value) || !std::isfinite(skills[i].progress) || skills[i].value < 0.f
                || skills[i].value > 100.f || skills[i].progress < 0.f || skills[i].progress >= 1.f)
                return error(Code::InvalidFloat, 0, 0, i);
        }
        if (activeEffects.size() > MaximumReplicatedActiveMagicEffects)
            return error(Code::TooManyEntries, activeEffects.size(), MaximumReplicatedActiveMagicEffects);
        for (std::size_t i = 0; i < activeEffects.size(); ++i)
        {
            const auto& effect = activeEffects[i];
            if (effect.sourceId == 0 || effect.targetId == 0 || effect.endTick <= effect.startTick
                || !std::isfinite(effect.magnitudePerSecond) || effect.magnitudePerSecond < 0.f
                || static_cast<std::uint8_t>(effect.sourceKind)
                    > static_cast<std::uint8_t>(MagicUseSourceKind::EnchantedItem)
                || (effect.targetKind != MagicUseTargetKind::Player && effect.targetKind != MagicUseTargetKind::Actor)
                || static_cast<std::uint8_t>(effect.effectKind)
                    >= static_cast<std::uint8_t>(DirectMagicEffectKind::Dispel)
                || (i && activeEffects[i - 1].instanceId >= effect.instanceId))
                return error(Code::InvalidMagicEffect, 0, 0, i);
        }
        return LatestWinsCombatSnapshot(session, generation, tick, canonicalRevision, self, selfRevision, selfHealth,
            selfMaximumHealth, selfFatigue, selfMaximumFatigue, selfMagicka, selfMaximumMagicka, selfDead,
            std::vector(actors.begin(), actors.end()), std::vector(skills.begin(), skills.end()),
            std::vector(players.begin(), players.end()), std::vector(activeEffects.begin(), activeEffects.end()));
    }

    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> ReliableCombatEventBatch::create(
        SessionId session, SessionGeneration generation, ServerTick tick, CanonicalRevision revision,
        std::span<const MeleeCombatEvent> events, std::span<const ActorMeleeCombatEvent> actorEvents,
        std::span<const MagicUseCombatEvent> magicEvents, std::span<const MagicEffectCombatEvent> magicEffectEvents)
    {
        if (events.size() > MaximumCombatEventsPerBatch || actorEvents.size() > MaximumCombatEventsPerBatch
            || magicEvents.size() > MaximumCombatEventsPerBatch
            || magicEffectEvents.size() > MaximumReplicatedMagicEffectEvents)
            return error(Code::TooManyEntries,
                std::max({ events.size(), actorEvents.size(), magicEvents.size(), magicEffectEvents.size() }),
                MaximumCombatEventsPerBatch);
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (!std::isfinite(events[i].damage))
                return error(Code::InvalidFloat, 0, 0, i);
            if (static_cast<std::uint8_t>(events[i].damagedStat) > static_cast<std::uint8_t>(MeleeDamageStat::Fatigue))
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(events[i].damagedStat), 0, i);
        }
        for (std::size_t i = 0; i < actorEvents.size(); ++i)
        {
            if (!std::isfinite(actorEvents[i].damage))
                return error(Code::InvalidFloat, 0, 0, i);
            if (static_cast<std::uint8_t>(actorEvents[i].damagedStat)
                > static_cast<std::uint8_t>(MeleeDamageStat::Fatigue))
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(actorEvents[i].damagedStat), 0, i);
        }
        for (std::size_t i = 0; i < magicEvents.size(); ++i)
        {
            const auto& event = magicEvents[i];
            const std::array values{ event.selfHealthDelta, event.selfFatigueDelta, event.selfMagickaDelta,
                event.targetHealthDelta, event.targetFatigueDelta, event.targetMagickaDelta };
            if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); }))
                return error(Code::InvalidFloat, 0, 0, i);
            if (static_cast<std::uint8_t>(event.sourceKind)
                    > static_cast<std::uint8_t>(MagicUseSourceKind::EnchantedItem)
                || static_cast<std::uint8_t>(event.targetKind) > static_cast<std::uint8_t>(MagicUseTargetKind::Actor)
                || event.sourceId == 0 || ((event.targetKind == MagicUseTargetKind::Self) != (event.targetId == 0)))
                return error(Code::InvalidMagicKind, 0, 0, i);
        }
        for (std::size_t i = 0; i < magicEffectEvents.size(); ++i)
        {
            const auto& event = magicEffectEvents[i];
            if (event.targetId == 0 || !std::isfinite(event.magnitudePerSecond)
                || !std::isfinite(event.appliedDelta) || event.magnitudePerSecond < 0.f
                || event.eventKind > MagicEffectCombatEventKind::Ended
                || event.endReason > MagicEffectCombatEndReason::TargetDied
                || (event.targetKind != MagicUseTargetKind::Player && event.targetKind != MagicUseTargetKind::Actor)
                || static_cast<std::uint8_t>(event.effectKind)
                    >= static_cast<std::uint8_t>(DirectMagicEffectKind::Dispel)
                || event.startTick > event.endTick
                || ((event.eventKind == MagicEffectCombatEventKind::Ended)
                    != (event.endReason != MagicEffectCombatEndReason::None))
                || (event.eventKind != MagicEffectCombatEventKind::Updated && event.startTick == event.endTick))
                return error(Code::InvalidMagicEffect, 0, 0, i);
        }
        return ReliableCombatEventBatch(session, generation, tick, revision, std::vector(events.begin(), events.end()),
            std::vector(actorEvents.begin(), actorEvents.end()), std::vector(magicEvents.begin(), magicEvents.end()),
            std::vector(magicEffectEvents.begin(), magicEffectEvents.end()));
    }

    std::vector<std::byte> encodeClientMeleeAttackCommand(const ClientMeleeAttackCommand& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header
            = Command::CreateClientCommandHeader(builder, input.sessionId.value(), input.sessionGeneration.value(),
                input.commandSequence.value(), input.commandId.value(), input.observedCanonicalRevision.value());
        const auto root = Command::CreateClientMeleeAttackCommand(builder, header,
            input.targetActorId ? input.targetActorId->value() : 0, input.sourceServerTick.value(),
            input.expectedAttackerRevision.value(), input.expectedTargetRevision.value(),
            static_cast<Command::MeleeAttackType>(input.attackType), input.attackStrength);
        Command::FinishSizePrefixedClientMeleeAttackCommandBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeLatestWinsCombatSnapshot(const LatestWinsCombatSnapshot& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = Snapshot::CreateCombatSnapshotHeader(builder, input.targetSessionId().value(),
            input.targetSessionGeneration().value(), input.serverTick().value(), input.canonicalRevision().value(),
            input.selfPlayerId().value(), input.selfCombatRevision().value(), input.selfFatigue(), input.selfHealth(),
            input.selfDead(), input.selfMaximumHealth(), input.selfMaximumFatigue(), input.selfMagicka(),
            input.selfMaximumMagicka());
        std::vector<Snapshot::ActorCombatSnapshot> actors;
        actors.reserve(input.actors().size());
        for (const auto& actor : input.actors())
            actors.emplace_back(actor.actorId.value(), actor.combatRevision.value(), actor.health, actor.maximumHealth,
                actor.fatigue, actor.maximumFatigue, actor.magicka, actor.maximumMagicka, actor.dead);
        std::vector<Snapshot::CombatSkillSnapshot> skills;
        skills.reserve(input.selfSkills().size());
        for (const auto& skill : input.selfSkills())
            skills.emplace_back(static_cast<std::uint8_t>(skill.skill), skill.value, skill.progress);
        std::vector<Snapshot::PlayerCombatSnapshot> players;
        players.reserve(input.players().size());
        for (const auto& player : input.players())
            players.emplace_back(player.playerId.value(), player.combatRevision.value(), player.health,
                player.maximumHealth, player.fatigue, player.maximumFatigue, player.magicka, player.maximumMagicka,
                player.dead);
        std::vector<Snapshot::ActiveMagicEffectSnapshot> activeEffects;
        activeEffects.reserve(input.activeEffects().size());
        for (const auto& effect : input.activeEffects())
            activeEffects.emplace_back(effect.instanceId.value(), effect.casterPlayerId.value(), effect.sourceId,
                effect.targetId, effect.startTick.value(), effect.endTick.value(), effect.magnitudePerSecond,
                static_cast<Snapshot::MagicUseSourceKind>(effect.sourceKind),
                static_cast<Snapshot::MagicUseTargetKind>(effect.targetKind),
                static_cast<std::uint8_t>(effect.effectKind));
        const auto root
            = Snapshot::CreateLatestWinsCombatSnapshot(builder, header, builder.CreateVectorOfStructs(actors),
                builder.CreateVectorOfStructs(skills), builder.CreateVectorOfStructs(players),
                builder.CreateVectorOfStructs(activeEffects));
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
        std::vector<Event::ActorMeleeCombatEvent> actorEvents;
        actorEvents.reserve(input.actorEvents().size());
        for (const auto& event : input.actorEvents())
            actorEvents.emplace_back(event.attackerActorId.value(), event.targetPlayerId.value(),
                event.attackerCombatRevision.value(), event.targetCombatRevision.value(), event.damage,
                static_cast<Event::MeleeDamageStat>(event.damagedStat), event.hit, event.blocked, event.targetDied);
        std::vector<Event::MagicUseCombatEvent> magicEvents;
        magicEvents.reserve(input.magicEvents().size());
        for (const auto& event : input.magicEvents())
            magicEvents.emplace_back(event.casterPlayerId.value(), event.sourceId, event.targetId,
                event.casterCombatRevision.value(), event.targetCombatRevision.value(), event.selfHealthDelta,
                event.selfFatigueDelta, event.selfMagickaDelta, event.targetHealthDelta, event.targetFatigueDelta,
                event.targetMagickaDelta, static_cast<Event::MagicUseSourceKind>(event.sourceKind),
                static_cast<Event::MagicUseTargetKind>(event.targetKind), event.castSucceeded, event.targetDied);
        std::vector<Event::MagicEffectCombatEvent> magicEffectEvents;
        magicEffectEvents.reserve(input.magicEffectEvents().size());
        for (const auto& event : input.magicEffectEvents())
            magicEffectEvents.emplace_back(event.instanceId.value(), event.targetId, event.startTick.value(),
                event.endTick.value(), event.targetCombatRevision.value(), event.magnitudePerSecond,
                event.appliedDelta, static_cast<Event::MagicEffectCombatEventKind>(event.eventKind),
                static_cast<Event::MagicEffectCombatEndReason>(event.endReason),
                static_cast<Event::MagicUseTargetKind>(event.targetKind),
                static_cast<std::uint8_t>(event.effectKind));
        const auto root = Event::CreateReliableCombatEventBatch(builder, header, builder.CreateVectorOfStructs(events),
            builder.CreateVectorOfStructs(actorEvents), builder.CreateVectorOfStructs(magicEvents),
            builder.CreateVectorOfStructs(magicEffectEvents));
        Event::FinishSizePrefixedReliableCombatEventBatchBuffer(builder, root);
        return take(builder);
    }

    std::variant<ClientMeleeAttackCommand, CombatReplicationDecodeError> decodeClientMeleeAttackCommand(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Command::SizePrefixedClientMeleeAttackCommandBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!Command::VerifySizePrefixedClientMeleeAttackCommandBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = Command::GetSizePrefixedClientMeleeAttackCommand(bytes);
        if (!root->header())
            return error(Code::MissingHeader);
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
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        if (!type)
            return error(Code::InvalidAttackType, static_cast<std::size_t>(root->attack_type()));
        if (!std::isfinite(root->attack_strength()))
            return error(Code::InvalidFloat);
        if (root->attack_strength() < 0.f || root->attack_strength() > 1.f)
            return error(Code::InvalidAttackStrength);
        return ClientMeleeAttackCommand{ *value(session), *value(generation), *value(sequence), *value(command),
            *value(canonical), target, *value(tick), *value(attackerRevision), *value(targetRevision), *type,
            root->attack_strength() };
    }

    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> decodeLatestWinsCombatSnapshot(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, LatestWinsSnapshotMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Snapshot::SizePrefixedLatestWinsCombatSnapshotBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, LatestWinsSnapshotMaximumPayloadBytes);
        if (!Snapshot::VerifySizePrefixedLatestWinsCombatSnapshotBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = Snapshot::GetSizePrefixedLatestWinsCombatSnapshot(bytes);
        if (!root->header())
            return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonical = strong<CanonicalRevision>(root->header()->canonical_revision());
        auto self = strong<PlayerId>(root->header()->self_player_id());
        auto selfRevision = strong<CombatRevision>(root->header()->self_combat_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&canonical), std::get_if<Error>(&self),
            std::get_if<Error>(&selfRevision) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        if (!std::isfinite(root->header()->self_health()) || !std::isfinite(root->header()->self_maximum_health())
            || !std::isfinite(root->header()->self_fatigue()) || !std::isfinite(root->header()->self_maximum_fatigue())
            || !std::isfinite(root->header()->self_magicka()) || !std::isfinite(root->header()->self_maximum_magicka()))
            return error(Code::InvalidFloat);
        const auto* encoded = root->actors();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumCombatSnapshotActors)
            return error(Code::TooManyEntries, count, MaximumCombatSnapshotActors);
        std::vector<ActorCombatSnapshot> actors;
        actors.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto current = copyStruct(encoded, i);
            auto actor = strong<ActorId>(current.actor_id(), i);
            auto revision = strong<CombatRevision>(current.combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&actor))
                return *failure;
            if (auto* failure = std::get_if<Error>(&revision))
                return *failure;
            actors.push_back(
                { *value(actor), *value(revision), current.health(), current.maximum_health(), current.fatigue(),
                    current.maximum_fatigue(), current.magicka(), current.maximum_magicka(), current.dead() });
        }
        const auto* encodedSkills = root->self_skills();
        const std::size_t skillCount = encodedSkills ? encodedSkills->size() : 0;
        if (skillCount != ReplicatedCombatSkillCount)
            return error(Code::TooManyEntries, skillCount, ReplicatedCombatSkillCount);
        std::vector<CombatSkillSnapshot> skills;
        skills.reserve(skillCount);
        for (std::size_t i = 0; i < skillCount; ++i)
        {
            const auto current = copyStruct(encodedSkills, i);
            if (current.skill() != i || current.skill() >= ReplicatedCombatSkillCount)
                return error(Code::InvalidSkill, current.skill(), i, i);
            skills.push_back(
                { static_cast<ReplicatedCombatSkill>(current.skill()), current.value(), current.progress() });
        }
        const auto* encodedPlayers = root->players();
        const std::size_t playerCount = encodedPlayers ? encodedPlayers->size() : 0;
        if (playerCount > MaximumCombatSnapshotPlayers)
            return error(Code::TooManyEntries, playerCount, MaximumCombatSnapshotPlayers);
        std::vector<PlayerCombatSnapshot> players;
        players.reserve(playerCount);
        for (std::size_t i = 0; i < playerCount; ++i)
        {
            const auto current = copyStruct(encodedPlayers, i);
            auto player = strong<PlayerId>(current.player_id(), i);
            auto revision = strong<CombatRevision>(current.combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&player))
                return *failure;
            if (auto* failure = std::get_if<Error>(&revision))
                return *failure;
            players.push_back(
                { *value(player), *value(revision), current.health(), current.maximum_health(), current.fatigue(),
                    current.maximum_fatigue(), current.magicka(), current.maximum_magicka(), current.dead() });
        }
        const auto* encodedEffects = root->active_effects();
        const std::size_t effectCount = encodedEffects ? encodedEffects->size() : 0;
        if (effectCount > MaximumReplicatedActiveMagicEffects)
            return error(Code::TooManyEntries, effectCount, MaximumReplicatedActiveMagicEffects);
        std::vector<ActiveMagicEffectSnapshot> activeEffects;
        activeEffects.reserve(effectCount);
        for (std::size_t i = 0; i < effectCount; ++i)
        {
            const auto current = copyStruct(encodedEffects, i);
            auto instance = strong<ActiveMagicEffectId>(current.instance_id(), i);
            auto caster = strong<PlayerId>(current.caster_player_id(), i);
            auto start = strong<ServerTick>(current.start_tick(), i);
            auto end = strong<ServerTick>(current.end_tick(), i);
            if (auto* failure = std::get_if<Error>(&instance))
                return *failure;
            if (auto* failure = std::get_if<Error>(&caster))
                return *failure;
            if (auto* failure = std::get_if<Error>(&start))
                return *failure;
            if (auto* failure = std::get_if<Error>(&end))
                return *failure;
            activeEffects.push_back({ *value(instance), *value(caster),
                static_cast<MagicUseSourceKind>(current.source_kind()), current.source_id(),
                static_cast<MagicUseTargetKind>(current.target_kind()), current.target_id(),
                static_cast<DirectMagicEffectKind>(current.effect_kind()), current.magnitude_per_second(),
                *value(start), *value(end) });
        }
        return LatestWinsCombatSnapshot::create(*value(session), *value(generation), *value(tick), *value(canonical),
            *value(self), *value(selfRevision), root->header()->self_health(), root->header()->self_maximum_health(),
            root->header()->self_fatigue(), root->header()->self_maximum_fatigue(), root->header()->self_magicka(),
            root->header()->self_maximum_magicka(), root->header()->self_dead(), actors, skills, players,
            activeEffects);
    }

    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> decodeReliableCombatEventBatch(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Event::SizePrefixedReliableCombatEventBatchBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!Event::VerifySizePrefixedReliableCombatEventBatchBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = Event::GetSizePrefixedReliableCombatEventBatch(bytes);
        if (!root->header())
            return error(Code::MissingHeader);
        auto session = strong<SessionId>(root->header()->target_session_id());
        auto generation = strong<SessionGeneration>(root->header()->target_session_generation());
        auto tick = strong<ServerTick>(root->header()->server_tick());
        auto canonical = strong<CanonicalRevision>(root->header()->canonical_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&canonical) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        const auto* encoded = root->events();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumCombatEventsPerBatch)
            return error(Code::TooManyEntries, count, MaximumCombatEventsPerBatch);
        std::vector<MeleeCombatEvent> events;
        events.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto current = copyStruct(encoded, i);
            auto attacker = strong<PlayerId>(current.attacker_player_id(), i);
            auto target = strong<ActorId>(current.target_actor_id(), i);
            auto attackerRevision = strong<CombatRevision>(current.attacker_combat_revision(), i);
            auto targetRevision = strong<CombatRevision>(current.target_combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&attacker))
                return *failure;
            if (auto* failure = std::get_if<Error>(&target))
                return *failure;
            if (auto* failure = std::get_if<Error>(&attackerRevision))
                return *failure;
            if (auto* failure = std::get_if<Error>(&targetRevision))
                return *failure;
            if (current.damage_stat() != Event::MeleeDamageStat::Health
                && current.damage_stat() != Event::MeleeDamageStat::Fatigue)
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(current.damage_stat()), 0, i);
            events.push_back({ *value(attacker), *value(target), *value(attackerRevision), *value(targetRevision),
                current.damage(), static_cast<MeleeDamageStat>(current.damage_stat()), current.hit(), current.blocked(),
                current.target_died() });
        }
        const auto* encodedActorEvents = root->actor_events();
        const std::size_t actorCount = encodedActorEvents ? encodedActorEvents->size() : 0;
        if (actorCount > MaximumCombatEventsPerBatch)
            return error(Code::TooManyEntries, actorCount, MaximumCombatEventsPerBatch);
        std::vector<ActorMeleeCombatEvent> actorEvents;
        actorEvents.reserve(actorCount);
        for (std::size_t i = 0; i < actorCount; ++i)
        {
            const auto current = copyStruct(encodedActorEvents, i);
            auto attacker = strong<ActorId>(current.attacker_actor_id(), i);
            auto target = strong<PlayerId>(current.target_player_id(), i);
            auto attackerRevision = strong<CombatRevision>(current.attacker_combat_revision(), i);
            auto targetRevision = strong<CombatRevision>(current.target_combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&attacker))
                return *failure;
            if (auto* failure = std::get_if<Error>(&target))
                return *failure;
            if (auto* failure = std::get_if<Error>(&attackerRevision))
                return *failure;
            if (auto* failure = std::get_if<Error>(&targetRevision))
                return *failure;
            if (current.damage_stat() != Event::MeleeDamageStat::Health
                && current.damage_stat() != Event::MeleeDamageStat::Fatigue)
                return error(Code::InvalidDamageStat, static_cast<std::size_t>(current.damage_stat()), 0, i);
            actorEvents.push_back({ *value(attacker), *value(target), *value(attackerRevision), *value(targetRevision),
                current.damage(), static_cast<MeleeDamageStat>(current.damage_stat()), current.hit(), current.blocked(),
                current.target_died() });
        }
        const auto* encodedMagicEvents = root->magic_events();
        const std::size_t magicCount = encodedMagicEvents ? encodedMagicEvents->size() : 0;
        if (magicCount > MaximumCombatEventsPerBatch)
            return error(Code::TooManyEntries, magicCount, MaximumCombatEventsPerBatch);
        std::vector<MagicUseCombatEvent> magicEvents;
        magicEvents.reserve(magicCount);
        for (std::size_t i = 0; i < magicCount; ++i)
        {
            const auto current = copyStruct(encodedMagicEvents, i);
            auto caster = strong<PlayerId>(current.caster_player_id(), i);
            auto casterRevision = strong<CombatRevision>(current.caster_combat_revision(), i);
            auto targetRevision = strong<CombatRevision>(current.target_combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&caster))
                return *failure;
            if (auto* failure = std::get_if<Error>(&casterRevision))
                return *failure;
            if (auto* failure = std::get_if<Error>(&targetRevision))
                return *failure;
            if (current.source_kind() != Event::MagicUseSourceKind::Spell
                && current.source_kind() != Event::MagicUseSourceKind::EnchantedItem)
                return error(Code::InvalidMagicKind, 0, 0, i);
            if (current.target_kind() != Event::MagicUseTargetKind::Self
                && current.target_kind() != Event::MagicUseTargetKind::Player
                && current.target_kind() != Event::MagicUseTargetKind::Actor)
                return error(Code::InvalidMagicKind, 0, 0, i);
            magicEvents.push_back({ *value(caster), static_cast<MagicUseSourceKind>(current.source_kind()),
                current.source_id(), static_cast<MagicUseTargetKind>(current.target_kind()), current.target_id(),
                *value(casterRevision), *value(targetRevision), current.cast_succeeded(), current.self_health_delta(),
                current.self_fatigue_delta(), current.self_magicka_delta(), current.target_health_delta(),
                current.target_fatigue_delta(), current.target_magicka_delta(), current.target_died() });
        }
        const auto* encodedMagicEffectEvents = root->magic_effect_events();
        const std::size_t magicEffectCount = encodedMagicEffectEvents ? encodedMagicEffectEvents->size() : 0;
        if (magicEffectCount > MaximumReplicatedMagicEffectEvents)
            return error(Code::TooManyEntries, magicEffectCount, MaximumReplicatedMagicEffectEvents);
        std::vector<MagicEffectCombatEvent> magicEffectEvents;
        magicEffectEvents.reserve(magicEffectCount);
        for (std::size_t i = 0; i < magicEffectCount; ++i)
        {
            const auto current = copyStruct(encodedMagicEffectEvents, i);
            auto instance = strong<ActiveMagicEffectId>(current.instance_id(), i);
            auto start = strong<ServerTick>(current.start_tick(), i);
            auto end = strong<ServerTick>(current.end_tick(), i);
            auto revision = strong<CombatRevision>(current.target_combat_revision(), i);
            if (auto* failure = std::get_if<Error>(&instance))
                return *failure;
            if (auto* failure = std::get_if<Error>(&start))
                return *failure;
            if (auto* failure = std::get_if<Error>(&end))
                return *failure;
            if (auto* failure = std::get_if<Error>(&revision))
                return *failure;
            magicEffectEvents.push_back({ *value(instance),
                static_cast<MagicEffectCombatEventKind>(current.event_kind()),
                static_cast<MagicEffectCombatEndReason>(current.end_reason()),
                static_cast<MagicUseTargetKind>(current.target_kind()), current.target_id(),
                static_cast<DirectMagicEffectKind>(current.effect_kind()), current.magnitude_per_second(),
                current.applied_delta(), *value(start), *value(end), *value(revision) });
        }
        return ReliableCombatEventBatch::create(
            *value(session), *value(generation), *value(tick), *value(canonical), events, actorEvents, magicEvents,
            magicEffectEvents);
    }
}
