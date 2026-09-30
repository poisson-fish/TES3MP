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
#include <tuple>
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

    flatbuffers::Verifier verifier(std::span<const std::byte> payload, std::size_t maximum,
        flatbuffers::uoffset_t maximumTables = 8)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = 8;
        options.max_tables = maximumTables;
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
        std::span<const PlayerCombatSnapshot> players, std::span<const ActiveMagicEffectSnapshot> activeEffects,
        std::span<const PlayerSwingSnapshot> swings, KnockoutSnapshot selfKnockout,
        std::span<const ActorPresentationSnapshot> presentation,
        std::span<const PhysicalProjectileSnapshot> projectiles,
        std::span<const MagicProjectileSnapshot> magicProjectiles)
    {
        const auto validKnockout = [](KnockoutSnapshot pose, bool dead) {
            return pose.state <= 3 && pose.frame < 1800
                && (pose.state >= 2 || !pose.frame) && (!dead || (pose.state < 2 && !pose.paralyzed));
        };
        if (!validKnockout(selfKnockout, selfDead)) return error(Code::InvalidFloat);
        if (!std::isfinite(selfHealth) || !std::isfinite(selfMaximumHealth) || !std::isfinite(selfFatigue)
            || !std::isfinite(selfMaximumFatigue) || !std::isfinite(selfMagicka) || !std::isfinite(selfMaximumMagicka)
            || selfMaximumHealth <= 0.f || selfMaximumFatigue < 0.f || selfMaximumMagicka < 0.f || selfMagicka < -1e6f
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
                || actors[i].magicka < -1e6f || actors[i].magicka > actors[i].maximumMagicka)
                return error(Code::InvalidFloat, 0, 0, i);
            if (!validKnockout(actors[i].knockout, actors[i].dead)) return error(Code::InvalidFloat, 0, 0, i);
            const auto& cast = actors[i];
            if (cast.castId == 0 ? (cast.castPhase || cast.castRange || cast.castElapsed || cast.castRelease || cast.castStop)
                : (cast.dead || cast.castPhase < 1 || cast.castPhase > 5 || cast.castRange > 2
                    || !cast.castRelease || cast.castRelease >= cast.castStop || cast.castStop > 1800
                    || cast.castElapsed >= cast.castStop || (cast.castPhase <= 2 && cast.castElapsed)
                    || (cast.castPhase < 4 ? cast.castElapsed >= cast.castRelease : cast.castElapsed < cast.castRelease)))
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
            if (!validKnockout(player.knockout, player.dead)) return error(Code::InvalidFloat, 0, 0, i);
            if (!std::isfinite(player.health) || !std::isfinite(player.maximumHealth) || !std::isfinite(player.fatigue)
                || !std::isfinite(player.maximumFatigue) || !std::isfinite(player.magicka)
                || !std::isfinite(player.maximumMagicka) || player.maximumHealth <= 0.f || player.maximumFatigue < 0.f
                || player.maximumMagicka < 0.f || player.magicka < -1e6f || player.magicka > player.maximumMagicka)
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
                || skills[i].value > 1e6f || skills[i].progress < 0.f || skills[i].progress >= 1.f)
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
        if (swings.size() > MaximumCombatSnapshotPlayers + 1)
            return error(Code::TooManyEntries, swings.size(), MaximumCombatSnapshotPlayers + 1);
        if (!swings.empty() && swings.size() != players.size() + 1)
            return error(Code::TooManyEntries, swings.size(), players.size() + 1);
        for (std::size_t i = 0; i < swings.size(); ++i)
        {
            const auto& swing = swings[i];
            if ((i && swings[i - 1].playerId >= swing.playerId)
                || (swing.playerId != self && std::ranges::none_of(players,
                    [&](const auto& player) { return player.playerId == swing.playerId; })))
                return error(Code::EntriesNotStrictlySorted, swing.playerId.value(), 0, i);
            if (!std::isfinite(swing.strength) || swing.strength < 0 || swing.strength > 1
                || !std::isfinite(swing.completion) || swing.completion < 0 || swing.completion > 1)
                return error(Code::InvalidFloat, 0, 0, i);
            if (!swing.command ? (swing.source || swing.targetLife || swing.direction || swing.phase
                    || swing.interruption || swing.strength || swing.completion || !swing.group.empty())
                : (!swing.targetLife || swing.direction > 2 || swing.phase < 1 || swing.phase > 4
                    || swing.interruption > 4 || swing.group.empty() || swing.group.size() > 64
                    || !std::ranges::all_of(swing.group, [](unsigned char ch) {
                        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); })
                    || (swing.phase == 4 && swing.completion != 1)))
                return error(Code::InvalidAttackType, 0, 0, i);
        }
        if (presentation.size() > MaximumCombatSnapshotActors + MaximumCombatSnapshotPlayers + 1)
            return error(Code::TooManyEntries, presentation.size(), MaximumCombatSnapshotActors + MaximumCombatSnapshotPlayers + 1);
        for (size_t i = 0; i < presentation.size(); ++i)
        {
            const auto& p = presentation[i];
            if (!p.id || !p.life || (p.kind != 1 && p.kind != 2)
                || (i && std::pair(presentation[i-1].kind, presentation[i-1].id) >= std::pair(p.kind, p.id))
                || (p.kind == 1 ? (p.id != self.value() && std::ranges::none_of(players,
                    [&](const auto& v) { return v.playerId.value() == p.id; }))
                    : std::ranges::none_of(actors, [&](const auto& v) { return v.actorId.value() == p.id; })))
                return error(Code::EntriesNotStrictlySorted, p.id, 0, i);
            if (p.cast == 0 ? (p.castPhase || p.castRange || p.castElapsed || p.castRelease || p.castStop)
                : (p.dead || p.bodyState != 1 || p.castPhase < 1 || p.castPhase > 5 || p.castRange > 2
                    || !p.castRelease || p.castRelease >= p.castStop || p.castStop > 1800
                    || p.castElapsed >= p.castStop || (p.castPhase <= 2 && p.castElapsed)
                    || (p.castPhase < 4 ? p.castElapsed >= p.castRelease : p.castElapsed < p.castRelease)))
                return error(Code::InvalidFloat, 0, 0, i);
            if ((!p.movementOwned && std::ranges::any_of(p.movement, [](float magnitude) { return magnitude != 0.f; }))
                || std::ranges::any_of(p.visibility, [](float magnitude) {
                    return !std::isfinite(magnitude) || magnitude < 0.f || magnitude > 512000.f;
                }) || std::ranges::any_of(p.movement, [](float magnitude) {
                    return !std::isfinite(magnitude) || magnitude < 0.f || magnitude > 512000.f;
                })) return error(Code::InvalidFloat, 0, 0, i);
            if (p.phase > 4 || p.direction > 2 || p.bodyState < 1 || p.bodyState > 4 || p.hitGroup > 16
                || !std::isfinite(p.strength) || p.strength < 0 || p.strength > 1
                || !std::isfinite(p.completion) || p.completion < 0 || p.completion > 1
                || !std::isfinite(p.rate) || p.rate < 0 || p.rate > 1000
                || !std::isfinite(p.bodyFrame) || p.bodyFrame < 0 || p.bodyFrame > p.bodyStop
                || p.bodyStop >= 1800 || p.loopStart > p.loopStop || p.loopStop > p.bodyStop
                || (p.phase && (!p.action || p.group.empty())) || p.group.size() > 64
                || !std::ranges::all_of(p.group, [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); })
                || (p.bodyState >= 2 && (!p.bodyAction || !p.bodyStop))
                || (p.bodyState == 4 && !p.hitGroup) || (p.dead && (p.phase || p.bodyState != 1)))
                return error(Code::InvalidFloat, 0, 0, i);
        }
        if (projectiles.size() > MaximumReplicatedPhysicalProjectiles)
            return error(Code::TooManyEntries, projectiles.size(), MaximumReplicatedPhysicalProjectiles);
        for (size_t i = 0; i < projectiles.size(); ++i)
        {
            const auto& p = projectiles[i];
            const auto key = [](const auto& value) {
                return std::tuple(value.casterKind, value.caster, value.casterLife, value.command);
            };
            if ((i && key(projectiles[i - 1]) >= key(p)) || !p.caster || !p.casterLife || !p.command
                || !p.releaseTick || p.releaseTick > tick.value() || (p.casterKind != 1 && p.casterKind != 2)
                || p.terminal > 2 || p.record.empty() || p.record.size() > 256
                || p.record.find('\0') != std::string::npos)
                return error(Code::InvalidIdentifier, 0, 0, i);
            for (const float v : p.position)
                if (!std::isfinite(v) || std::abs(v) > 1e7f)
                    return error(Code::InvalidFloat, 0, 0, i);
            for (const float v : p.velocity)
                if (!std::isfinite(v) || std::abs(v) > 50000.f)
                    return error(Code::InvalidFloat, 0, 0, i);
        }
        if (magicProjectiles.size() > MaximumReplicatedMagicProjectiles)
            return error(Code::TooManyEntries, magicProjectiles.size(), MaximumReplicatedMagicProjectiles);
        for (size_t i = 0; i < magicProjectiles.size(); ++i)
        {
            const auto& p = magicProjectiles[i];
            const auto key = [](const auto& value) {
                return std::tuple(value.casterKind, value.caster, value.casterLife, value.command);
            };
            if ((i && key(magicProjectiles[i - 1]) >= key(p)) || !p.caster || !p.casterLife || !p.command
                || (p.casterKind != 1 && p.casterKind != 2) || p.sourceKind > 1
                || p.record.empty() || p.record.size() > 256 || p.record.find('\0') != std::string::npos)
                return error(Code::InvalidIdentifier, 0, 0, i);
            for (const float v : p.position)
                if (!std::isfinite(v) || std::abs(v) > 1e7f)
                    return error(Code::InvalidFloat, 0, 0, i);
            for (const float v : p.velocity)
                if (!std::isfinite(v) || std::abs(v) > 50000.f)
                    return error(Code::InvalidFloat, 0, 0, i);
        }
        return LatestWinsCombatSnapshot(session, generation, tick, canonicalRevision, self, selfRevision, selfHealth,
            selfMaximumHealth, selfFatigue, selfMaximumFatigue, selfMagicka, selfMaximumMagicka, selfDead,
            std::vector(actors.begin(), actors.end()), std::vector(skills.begin(), skills.end()),
            std::vector(players.begin(), players.end()), std::vector(activeEffects.begin(), activeEffects.end()),
            std::vector(swings.begin(), swings.end()), selfKnockout, std::vector(presentation.begin(), presentation.end()),
            std::vector(projectiles.begin(), projectiles.end()),
            std::vector(magicProjectiles.begin(), magicProjectiles.end()));
    }

    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> ReliableCombatEventBatch::create(
        SessionId session, SessionGeneration generation, ServerTick tick, CanonicalRevision revision,
        std::span<const MeleeCombatEvent> events, std::span<const ActorMeleeCombatEvent> actorEvents,
        std::span<const MagicUseCombatEvent> magicEvents, std::span<const MagicEffectCombatEvent> magicEffectEvents,
        std::span<const MagicImpactCue> magicImpactCues)
    {
        if (magicImpactCues.size() > MaximumReplicatedMagicImpactCues)
            return error(Code::TooManyEntries, magicImpactCues.size(), MaximumReplicatedMagicImpactCues);
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
                || !event.casterLife || (!event.actorCaster() && event.casterLife != 1)
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
        for (std::size_t i = 0; i < magicImpactCues.size(); ++i)
        {
            const auto& cue = magicImpactCues[i];
            if (!cue.caster || !cue.casterLife || !cue.command
                || (cue.casterKind != 1 && cue.casterKind != 2) || cue.sourceKind > 1 || cue.range > 2
                || cue.record.empty() || cue.record.size() > 256 || cue.record.find('\0') != std::string::npos)
                return error(Code::InvalidIdentifier, 0, 0, i);
            for (const float v : cue.position)
                if (!std::isfinite(v) || std::abs(v) > 1e7f)
                    return error(Code::InvalidFloat, 0, 0, i);
        }
        return ReliableCombatEventBatch(session, generation, tick, revision, std::vector(events.begin(), events.end()),
            std::vector(actorEvents.begin(), actorEvents.end()), std::vector(magicEvents.begin(), magicEvents.end()),
            std::vector(magicEffectEvents.begin(), magicEffectEvents.end()),
            std::vector(magicImpactCues.begin(), magicImpactCues.end()));
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
            static_cast<Command::MeleeAttackType>(input.attackType), input.attackStrength,
            input.aimPoint.has_value(), input.aimPoint ? (*input.aimPoint)[0] : 0.f,
            input.aimPoint ? (*input.aimPoint)[1] : 0.f,
            input.aimPoint ? (*input.aimPoint)[2] : 0.f);
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
            input.selfMaximumMagicka(), input.selfKnockout().state, input.selfKnockout().frame, input.selfKnockout().paralyzed);
        std::vector<Snapshot::ActorCombatSnapshot> actors;
        actors.reserve(input.actors().size());
        for (const auto& actor : input.actors())
            actors.emplace_back(actor.actorId.value(), actor.combatRevision.value(), actor.health, actor.maximumHealth,
                actor.fatigue, actor.maximumFatigue, actor.magicka, actor.maximumMagicka, actor.dead,
                actor.castId, actor.castPhase, actor.castRange, actor.castElapsed, actor.castRelease, actor.castStop, actor.knockout.state, actor.knockout.frame, actor.knockout.paralyzed);
        std::vector<Snapshot::CombatSkillSnapshot> skills;
        skills.reserve(input.selfSkills().size());
        for (const auto& skill : input.selfSkills())
            skills.emplace_back(static_cast<std::uint8_t>(skill.skill), skill.value, skill.progress);
        std::vector<Snapshot::PlayerCombatSnapshot> players;
        players.reserve(input.players().size());
        for (const auto& player : input.players())
            players.emplace_back(player.playerId.value(), player.combatRevision.value(), player.health,
                player.maximumHealth, player.fatigue, player.maximumFatigue, player.magicka, player.maximumMagicka,
                player.dead, player.knockout.state, player.knockout.frame, player.knockout.paralyzed);
        std::vector<Snapshot::ActiveMagicEffectSnapshot> activeEffects;
        activeEffects.reserve(input.activeEffects().size());
        for (const auto& effect : input.activeEffects())
            activeEffects.emplace_back(effect.instanceId.value(), effect.casterPlayerId.value(), effect.sourceId,
                effect.targetId, effect.startTick.value(), effect.endTick.value(), effect.magnitudePerSecond,
                static_cast<Snapshot::MagicUseSourceKind>(effect.sourceKind),
                static_cast<Snapshot::MagicUseTargetKind>(effect.targetKind),
                static_cast<std::uint8_t>(effect.effectKind));
        std::vector<flatbuffers::Offset<Snapshot::PlayerSwingSnapshot>> swings;
        for (const auto& swing : input.swings())
            swings.push_back(Snapshot::CreatePlayerSwingSnapshot(builder, swing.playerId.value(), swing.command,
                swing.source, swing.targetLife, swing.direction, swing.phase, swing.interruption,
                swing.strength, swing.completion, builder.CreateString(swing.group)));
        std::vector<flatbuffers::Offset<Snapshot::ActorPresentationSnapshot>> presentation;
        for (const auto& p : input.presentation())
            presentation.push_back(Snapshot::CreateActorPresentationSnapshot(builder, p.id, p.life, p.action,
                p.bodyAction, p.kind, p.phase, p.direction, p.bodyState, p.hitGroup, p.strength, p.completion,
                p.rate, p.bodyFrame, p.bodyStop, p.loopStart, p.loopStop, builder.CreateString(p.group), p.dead,
                p.cast, p.castPhase, p.castRange, p.castElapsed, p.castRelease, p.castStop,
                builder.CreateVector(p.visibility.data(), p.visibility.size()),
                builder.CreateVector(p.movement.data(), p.movement.size()), p.movementOwned));
        std::vector<flatbuffers::Offset<Snapshot::PhysicalProjectileSnapshot>> projectiles;
        for (const auto& p : input.projectiles())
            projectiles.push_back(Snapshot::CreatePhysicalProjectileSnapshot(builder, p.casterKind, p.terminal,
                p.caster, p.casterLife, p.command, p.releaseTick, builder.CreateString(p.record),
                p.position[0], p.position[1], p.position[2], p.velocity[0], p.velocity[1], p.velocity[2]));
        std::vector<flatbuffers::Offset<Snapshot::MagicProjectileSnapshot>> magicProjectiles;
        for (const auto& p : input.magicProjectiles())
            magicProjectiles.push_back(Snapshot::CreateMagicProjectileSnapshot(builder, p.casterKind, p.sourceKind,
                p.caster, p.casterLife, p.command, builder.CreateString(p.record),
                p.position[0], p.position[1], p.position[2], p.velocity[0], p.velocity[1], p.velocity[2]));
        const auto root
            = Snapshot::CreateLatestWinsCombatSnapshot(builder, header, builder.CreateVectorOfStructs(actors),
                builder.CreateVectorOfStructs(skills), builder.CreateVectorOfStructs(players),
                builder.CreateVectorOfStructs(activeEffects), builder.CreateVector(swings), builder.CreateVector(presentation),
                builder.CreateVector(projectiles), builder.CreateVector(magicProjectiles));
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
            magicEvents.emplace_back(event.casterId(), event.casterLife, event.sourceId, event.targetId,
                event.casterCombatRevision.value(), event.targetCombatRevision.value(), event.selfHealthDelta,
                event.selfFatigueDelta, event.selfMagickaDelta, event.targetHealthDelta, event.targetFatigueDelta,
                event.targetMagickaDelta, static_cast<Event::MagicUseSourceKind>(event.sourceKind),
                static_cast<Event::MagicUseTargetKind>(event.targetKind), event.castSucceeded, event.targetDied,
                event.actorCaster() ? 2 : 1);
        std::vector<Event::MagicEffectCombatEvent> magicEffectEvents;
        magicEffectEvents.reserve(input.magicEffectEvents().size());
        for (const auto& event : input.magicEffectEvents())
            magicEffectEvents.emplace_back(event.instanceId.value(), event.targetId, event.startTick.value(),
                event.endTick.value(), event.targetCombatRevision.value(), event.magnitudePerSecond,
                event.appliedDelta, static_cast<Event::MagicEffectCombatEventKind>(event.eventKind),
                static_cast<Event::MagicEffectCombatEndReason>(event.endReason),
                static_cast<Event::MagicUseTargetKind>(event.targetKind),
                static_cast<std::uint8_t>(event.effectKind));
        std::vector<flatbuffers::Offset<Event::MagicImpactCue>> magicImpactCues;
        for (const auto& cue : input.magicImpactCues())
            magicImpactCues.push_back(Event::CreateMagicImpactCue(builder, cue.casterKind, cue.sourceKind,
                cue.caster, cue.casterLife, cue.command, builder.CreateString(cue.record),
                cue.position[0], cue.position[1], cue.position[2], cue.range));
        const auto root = Event::CreateReliableCombatEventBatch(builder, header, builder.CreateVectorOfStructs(events),
            builder.CreateVectorOfStructs(actorEvents), builder.CreateVectorOfStructs(magicEvents),
            builder.CreateVectorOfStructs(magicEffectEvents), builder.CreateVector(magicImpactCues));
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
        std::optional<std::array<float, 3>> aim;
        if (root->has_aim())
        {
            aim = std::array{root->aim_x(), root->aim_y(), root->aim_z()};
            if (std::ranges::any_of(*aim, [](float v) { return !std::isfinite(v) || std::abs(v) > 10'000'000.f; }))
                return error(Code::InvalidFloat);
        }
        return ClientMeleeAttackCommand{ *value(session), *value(generation), *value(sequence), *value(command),
            *value(canonical), target, *value(tick), *value(attackerRevision), *value(targetRevision), *type,
            root->attack_strength(), aim };
    }

    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> decodeLatestWinsCombatSnapshot(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, LatestWinsSnapshotMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Snapshot::SizePrefixedLatestWinsCombatSnapshotBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, LatestWinsSnapshotMaximumPayloadBytes, MaximumCombatSnapshotPlayers + 3);
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
                    current.maximum_fatigue(), current.magicka(), current.maximum_magicka(), current.dead(),
                    current.cast_id(), current.cast_phase(), current.cast_range(), current.cast_elapsed(), current.cast_release(), current.cast_stop(), {current.knockout_state(), current.knockout_frame(), current.paralyzed()} });
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
                    current.maximum_fatigue(), current.magicka(), current.maximum_magicka(), current.dead(),
                    {current.knockout_state(), current.knockout_frame(), current.paralyzed()} });
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
        const auto swingCount = root->swings() ? root->swings()->size() : 0;
        if (swingCount > MaximumCombatSnapshotPlayers + 1)
            return error(Code::TooManyEntries, swingCount, MaximumCombatSnapshotPlayers + 1);
        std::vector<PlayerSwingSnapshot> swings;
        swings.reserve(swingCount);
        for (std::size_t i = 0; i < swingCount; ++i)
        {
            const auto* current = root->swings()->Get(static_cast<flatbuffers::uoffset_t>(i));
            if (!current || (current->group() && current->group()->size() > 64))
                return error(Code::TooManyEntries, 0, 64, i);
            auto owner = strong<PlayerId>(current->player_id(), i);
            if (auto* failure = std::get_if<Error>(&owner)) return *failure;
            swings.push_back({*value(owner), current->command(), current->source(), current->target_life(),
                current->direction(), current->phase(), current->interruption(), current->strength(),
                current->completion(), current->group() ? current->group()->str() : std::string{}});
        }
        const size_t presentationCount = root->presentation() ? root->presentation()->size() : 0;
        if (presentationCount > MaximumCombatSnapshotActors + MaximumCombatSnapshotPlayers + 1)
            return error(Code::TooManyEntries, presentationCount, MaximumCombatSnapshotActors + MaximumCombatSnapshotPlayers + 1);
        std::vector<ActorPresentationSnapshot> presentation;
        presentation.reserve(presentationCount);
        for (size_t i = 0; i < presentationCount; ++i)
        {
            const auto* p = root->presentation()->Get(flatbuffers::uoffset_t(i));
            if (!p || (p->group() && p->group()->size() > 64)
                || !p->visibility() || p->visibility()->size() != 8
                || !p->movement() || p->movement()->size() != 8)
                return error(Code::TooManyEntries, 0, 8, i);
            presentation.push_back({p->id(), p->life(), p->action(), p->body_action(), p->kind(), p->phase(),
                p->direction(), p->body_state(), p->hit_group(), p->strength(), p->completion(), p->rate(),
                p->body_frame(), p->body_stop(), p->loop_start(), p->loop_stop(),
                p->group() ? p->group()->str() : std::string{}, p->dead(),
                p->cast(), p->cast_phase(), p->cast_range(), p->cast_elapsed(), p->cast_release(), p->cast_stop()});
            std::copy(p->visibility()->begin(), p->visibility()->end(), presentation.back().visibility.begin());
            std::copy(p->movement()->begin(), p->movement()->end(), presentation.back().movement.begin());
            presentation.back().movementOwned = p->movement_owned();
        }
        const size_t projectileCount = root->projectiles() ? root->projectiles()->size() : 0;
        if (projectileCount > MaximumReplicatedPhysicalProjectiles)
            return error(Code::TooManyEntries, projectileCount, MaximumReplicatedPhysicalProjectiles);
        std::vector<PhysicalProjectileSnapshot> projectiles;
        projectiles.reserve(projectileCount);
        for (size_t i = 0; i < projectileCount; ++i)
        {
            const auto* p = root->projectiles()->Get(flatbuffers::uoffset_t(i));
            if (!p || !p->record() || p->record()->size() > 256)
                return error(Code::InvalidIdentifier, 0, 256, i);
            projectiles.push_back({p->caster_kind(), p->terminal(), p->caster(), p->caster_life(),
                p->command(), p->release_tick(), p->record()->str(),
                {p->x(), p->y(), p->z()}, {p->vx(), p->vy(), p->vz()}});
        }
        const size_t magicProjectileCount = root->magic_projectiles() ? root->magic_projectiles()->size() : 0;
        if (magicProjectileCount > MaximumReplicatedMagicProjectiles)
            return error(Code::TooManyEntries, magicProjectileCount, MaximumReplicatedMagicProjectiles);
        std::vector<MagicProjectileSnapshot> magicProjectiles;
        magicProjectiles.reserve(magicProjectileCount);
        for (size_t i = 0; i < magicProjectileCount; ++i)
        {
            const auto* p = root->magic_projectiles()->Get(flatbuffers::uoffset_t(i));
            if (!p || !p->record() || p->record()->size() > 256)
                return error(Code::InvalidIdentifier, 0, 256, i);
            magicProjectiles.push_back({p->caster_kind(), p->source_kind(), p->caster(), p->caster_life(),
                p->command(), p->record()->str(), {p->x(), p->y(), p->z()}, {p->vx(), p->vy(), p->vz()}});
        }
        return LatestWinsCombatSnapshot::create(*value(session), *value(generation), *value(tick), *value(canonical),
            *value(self), *value(selfRevision), root->header()->self_health(), root->header()->self_maximum_health(),
            root->header()->self_fatigue(), root->header()->self_maximum_fatigue(), root->header()->self_magicka(),
            root->header()->self_maximum_magicka(), root->header()->self_dead(), actors, skills, players,
            activeEffects, swings, {root->header()->self_knockout_state(), root->header()->self_knockout_frame(), root->header()->self_paralyzed()},
            presentation, projectiles, magicProjectiles);
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
            if ((current.caster_kind() != 1 && current.caster_kind() != 2) || !current.caster_life()
                || (current.caster_kind() == 1 && current.caster_life() != 1))
                return error(Code::InvalidMagicKind, 0, 0, i);
            auto caster = strong<PlayerId>(current.caster_id(), i);
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
            const std::variant<PlayerId, ActorId> identity = current.caster_kind() == 1
                ? std::variant<PlayerId, ActorId>(*value(caster))
                : std::variant<PlayerId, ActorId>(*ActorId::fromValue(current.caster_id()));
            magicEvents.push_back({ identity, static_cast<MagicUseSourceKind>(current.source_kind()),
                current.source_id(), static_cast<MagicUseTargetKind>(current.target_kind()), current.target_id(),
                *value(casterRevision), *value(targetRevision), current.cast_succeeded(), current.self_health_delta(),
                current.self_fatigue_delta(), current.self_magicka_delta(), current.target_health_delta(),
                current.target_fatigue_delta(), current.target_magicka_delta(), current.target_died(), current.caster_life() });
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
        const auto* encodedMagicImpactCues = root->magic_impact_cues();
        const size_t magicImpactCount = encodedMagicImpactCues ? encodedMagicImpactCues->size() : 0;
        if (magicImpactCount > MaximumReplicatedMagicImpactCues)
            return error(Code::TooManyEntries, magicImpactCount, MaximumReplicatedMagicImpactCues);
        std::vector<MagicImpactCue> magicImpactCues;
        magicImpactCues.reserve(magicImpactCount);
        for (size_t i = 0; i < magicImpactCount; ++i)
        {
            const auto* cue = encodedMagicImpactCues->Get(flatbuffers::uoffset_t(i));
            if (!cue || !cue->record() || cue->record()->size() > 256)
                return error(Code::InvalidIdentifier, 0, 256, i);
            magicImpactCues.push_back({cue->caster_kind(), cue->source_kind(), cue->caster(), cue->caster_life(),
                cue->command(), cue->record()->str(), {cue->x(), cue->y(), cue->z()}, cue->range()});
        }
        return ReliableCombatEventBatch::create(
            *value(session), *value(generation), *value(tick), *value(canonical), events, actorEvents, magicEvents,
            magicEffectEvents, magicImpactCues);
    }
}
