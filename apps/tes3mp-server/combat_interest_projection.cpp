#include "combat_interest_projection.hpp"

#include <array>
#include <vector>

namespace TES3MP::ServerApp
{
    namespace
    {
        const CanonicalPlayerEntityState* targetPlayer(const CanonicalServerState& players, SessionId target) noexcept
        {
            const auto* session = players.findActiveSession(target);
            return session ? players.findPlayer(session->playerId()) : nullptr;
        }
    }

    std::optional<LatestWinsCombatSnapshot> projectCombatSnapshot(const CanonicalServerState& players,
        const CanonicalActorWorld& spatialActors, const CanonicalCombatWorld& combat, SessionId target,
        ServerTick tick, CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        const auto* player = targetPlayer(players, target);
        const auto* self = session ? combat.findPlayer(session->playerId()) : nullptr;
        if (!session || !player || !self)
            return std::nullopt;
        std::vector<ActorCombatSnapshot> visible;
        for (const auto& actor : spatialActors.actors())
        {
            if (actor.root().cell() != player->transform().cell())
                continue;
            const auto* state = combat.findActor(actor.actorId());
            if (!state)
                return std::nullopt;
            visible.push_back({ state->actorId, state->revision, state->stats.health,
                state->maximumHealth, state->stats.fatigue, state->maximumFatigue, state->stats.dead });
        }
        const std::array skillValues{
            self->blockSkill,
            self->weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::ShortBlade)],
            self->weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::LongBlade)],
            self->weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::BluntWeapon)],
            self->weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Axe)],
            self->weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Spear)],
            self->stats.handToHandSkill,
            self->armorSkills[0],
            self->armorSkills[1],
            self->armorSkills[2],
            self->armorSkills[3],
        };
        std::array<CombatSkillSnapshot, ReplicatedCombatSkillCount> skills{};
        for (std::size_t index = 0; index < skills.size(); ++index)
        {
            skills[index] = { static_cast<ReplicatedCombatSkill>(index), skillValues[index],
                self->skillProgression[index].progress };
        }
        auto created = LatestWinsCombatSnapshot::create(target, session->sessionGeneration(), tick,
            canonicalRevision, session->playerId(), self->revision, self->victim.health,
            self->maximumHealth, self->stats.fatigue, self->maximumFatigue, self->magicka,
            self->maximumMagicka, self->victim.dead, visible, skills);
        auto* snapshot = std::get_if<LatestWinsCombatSnapshot>(&created);
        return snapshot ? std::optional<LatestWinsCombatSnapshot>(std::move(*snapshot)) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<ReliableCombatEventBatch> projectCombatEvents(const CanonicalServerState& players,
        const CanonicalActorWorld& spatialActors, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision, std::span<const AuthoritativeMeleeEvent> events,
        std::span<const AuthoritativeActorMeleeEvent> actorEvents)
    try
    {
        const auto* session = players.findActiveSession(target);
        const auto* player = targetPlayer(players, target);
        if (!session || !player)
            return std::nullopt;
        std::vector<MeleeCombatEvent> visible;
        std::vector<ActorMeleeCombatEvent> visibleActorEvents;
        for (const auto& event : events)
        {
            const auto* actor = spatialActors.find(event.target);
            if (!actor || actor->root().cell() != player->transform().cell())
                continue;
            visible.push_back({ event.attacker, event.target, event.attackerRevision, event.targetRevision,
                event.resolution.damage, event.resolution.damagedStat, event.resolution.hit,
                event.resolution.blocked, event.resolution.victimDied });
        }
        for (const auto& event : actorEvents)
        {
            const auto* actor = spatialActors.find(event.attacker);
            if (!actor || actor->root().cell() != player->transform().cell())
                continue;
            visibleActorEvents.push_back({ event.attacker, event.target, event.attackerRevision,
                event.targetRevision, event.resolution.damage, event.resolution.damagedStat,
                event.resolution.hit, event.resolution.blocked, event.resolution.victimDied });
        }
        auto created = ReliableCombatEventBatch::create(target, session->sessionGeneration(), tick,
            canonicalRevision, visible, visibleActorEvents);
        auto* batch = std::get_if<ReliableCombatEventBatch>(&created);
        return batch ? std::optional<ReliableCombatEventBatch>(std::move(*batch)) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
