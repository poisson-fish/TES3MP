#include <tes3mp/combat_world.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace
{
    bool finite(float value) noexcept { return std::isfinite(value); }

    bool valid(const TES3MP::CanonicalPlayerCombatState& value) noexcept
    {
        const auto& s = value.stats;
        if (!(finite(s.agility) && finite(s.luck) && finite(s.strength) && finite(s.fatigueTerm)
                && finite(s.normalizedEncumbrance) && finite(s.fortifyAttack) && finite(s.blind)
                && finite(s.weaponSkill) && finite(s.handToHandSkill) && finite(s.fatigue)))
            return false;
        if (!value.equippedWeapon)
            return true;
        const auto& w = *value.equippedWeapon;
        return finite(w.chopMinimum) && finite(w.chopMaximum) && finite(w.slashMinimum)
            && finite(w.slashMaximum) && finite(w.thrustMinimum) && finite(w.thrustMaximum)
            && finite(w.weight) && finite(w.normalizedCondition) && w.weight >= 0.f
            && w.normalizedCondition >= 0.f && w.normalizedCondition <= 1.f && w.condition >= 0;
    }

    bool valid(const TES3MP::CanonicalActorCombatState& value) noexcept
    {
        const auto& s = value.stats;
        return finite(s.health) && finite(s.fatigue) && finite(s.evasion) && finite(s.chameleon)
            && finite(s.invisibility) && finite(s.normalWeaponResistance) && finite(s.normalWeaponWeakness);
    }
}

namespace TES3MP
{
    const CanonicalPlayerCombatState* CanonicalCombatWorld::findPlayer(PlayerId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        return found != mPlayers.end() && found->playerId == id ? &*found : nullptr;
    }

    const CanonicalActorCombatState* CanonicalCombatWorld::findActor(ActorId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mActors, id, {}, &CanonicalActorCombatState::actorId);
        return found != mActors.end() && found->actorId == id ? &*found : nullptr;
    }

    std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
        std::span<const CanonicalPlayerCombatState> players, std::span<const CanonicalActorCombatState> actors,
        RandomStateV1 randomState)
    {
        if (players.size() > MaximumPlayerCombatants)
            return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::PlayerLimitExceeded };
        if (actors.size() > MaximumActorCombatants)
            return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::ActorLimitExceeded };
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            if (!valid(players[i]))
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::InvalidStat, i };
            if (i && players[i - 1].playerId >= players[i].playerId)
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::PlayersNotStrictlySorted, i };
        }
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            if (!valid(actors[i]))
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::InvalidStat, i };
            if (i && actors[i - 1].actorId >= actors[i].actorId)
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::ActorsNotStrictlySorted, i };
        }
        return CanonicalCombatWorld(std::vector(players.begin(), players.end()),
            std::vector(actors.begin(), actors.end()), randomState);
    }

    PreparedMeleeAttack prepareAuthoritativeMeleeAttack(const CanonicalCombatWorld& combat,
        const CanonicalServerState& players, const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings,
        MeleeAuthorityPolicy policy, ServerMeleeContactQuery& contact, ServerTick serverTick,
        const AuthoritativeMeleeAttack& attack) noexcept
    try
    {
        PreparedMeleeAttack out;
        const auto* attackerCombat = combat.findPlayer(attack.attacker);
        if (!attackerCombat)
        {
            out.disposition = AuthoritativeMeleeDisposition::UnknownAttacker;
            return out;
        }
        if (attackerCombat->revision != attack.expectedAttackerRevision)
        {
            out.disposition = AuthoritativeMeleeDisposition::StaleAttackerRevision;
            return out;
        }
        const auto* targetCombat = attack.target ? combat.findActor(*attack.target) : nullptr;
        if (attack.target && !targetCombat)
        {
            out.disposition = AuthoritativeMeleeDisposition::UnknownTarget;
            return out;
        }
        if (targetCombat && targetCombat->revision != attack.expectedTargetRevision)
        {
            out.disposition = AuthoritativeMeleeDisposition::StaleTargetRevision;
            return out;
        }
        const auto* playerSpatial = players.findPlayer(attack.attacker);
        const auto* actorSpatial = attack.target ? actors.find(*attack.target) : nullptr;
        if (!playerSpatial || (attack.target && !actorSpatial))
        {
            out.disposition = AuthoritativeMeleeDisposition::SpatialIdentityMismatch;
            return out;
        }
        if (actorSpatial && playerSpatial->transform().cell() != actorSpatial->root().cell())
        {
            out.disposition = AuthoritativeMeleeDisposition::DifferentCell;
            return out;
        }
        if (attack.sourceTick > serverTick)
        {
            out.disposition = AuthoritativeMeleeDisposition::FutureSourceTick;
            return out;
        }
        if (serverTick.value() - attack.sourceTick.value() > policy.maximumRewindTicks)
        {
            out.disposition = AuthoritativeMeleeDisposition::RewindWindowExceeded;
            return out;
        }
        if (attackerCombat->lastAttackTick
            && serverTick.value() - attackerCombat->lastAttackTick->value() < policy.minimumAttackIntervalTicks)
        {
            out.disposition = AuthoritativeMeleeDisposition::RateLimited;
            return out;
        }
        if (!finite(attack.attackStrength) || attack.attackStrength < 0.f || attack.attackStrength > 1.f
            || !finite(attack.werewolfClawMultiplier))
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        if (actorSpatial)
        {
            const auto contactResult = contact.validate(ServerMeleeContactRequest{ attack.attacker, *attack.target,
                attack.sourceTick, attack.attackType, attackerCombat->equippedWeapon }, *playerSpatial, *actorSpatial);
            if (contactResult != MeleeContactValidation::Accepted)
            {
                out.disposition = contactResult == MeleeContactValidation::NoContact
                    ? AuthoritativeMeleeDisposition::NoContact
                    : AuthoritativeMeleeDisposition::HistoryUnavailable;
                return out;
            }
        }

        auto attackerRevision = attackerCombat->revision.next();
        auto targetRevision = targetCombat ? targetCombat->revision.next() : std::optional<CombatRevision>{};
        if (!attackerRevision || (targetCombat && !targetRevision))
        {
            out.disposition = AuthoritativeMeleeDisposition::RevisionExhausted;
            return out;
        }
        std::vector<CanonicalPlayerCombatState> playerStates(combat.players().begin(), combat.players().end());
        std::vector<CanonicalActorCombatState> actorStates(combat.actors().begin(), combat.actors().end());
        auto& mutableAttacker = *std::ranges::lower_bound(
            playerStates, attack.attacker, {}, &CanonicalPlayerCombatState::playerId);
        auto* mutableTarget = targetCombat ? &*std::ranges::lower_bound(
            actorStates, *attack.target, {}, &CanonicalActorCombatState::actorId) : nullptr;
        auto random = Xoshiro256StarStar::restore(combat.randomState());
        std::uint8_t roll = 0;
        if (mutableTarget)
        {
            const auto generated = random.uniformBelow(100);
            if (!generated)
            {
                out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                return out;
            }
            roll = static_cast<std::uint8_t>(*generated);
        }
        OpenMwMeleeAttempt attempt{ .type = attack.attackType,
            .attackStrength = attack.attackStrength,
            .hitRoll0To99 = roll,
            .contact = mutableTarget != nullptr,
            .blocked = attack.blocked,
            .strengthInfluencesHandToHand = attack.strengthInfluencesHandToHand,
            .werewolfClawMultiplier = attack.werewolfClawMultiplier,
            .weapon = mutableAttacker.equippedWeapon };
        const OpenMwMeleeVictim emptyVictim{};
        const auto resolution = resolveOpenMwMelee(
            settings, mutableAttacker.stats, mutableTarget ? mutableTarget->stats : emptyVictim, attempt);
        if (resolution.code == OpenMwMeleeResolutionCode::InvalidInput)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        mutableAttacker.stats.fatigue = resolution.attackerFatigue;
        mutableAttacker.lastAttackTick = serverTick;
        mutableAttacker.revision = *attackerRevision;
        if (mutableAttacker.equippedWeapon)
        {
            mutableAttacker.equippedWeapon->condition = resolution.weaponCondition;
            if (resolution.weaponBroken)
                mutableAttacker.equippedWeapon.reset();
        }
        if (mutableTarget && resolution.code == OpenMwMeleeResolutionCode::Resolved)
        {
            mutableTarget->stats.health = resolution.victimHealth;
            mutableTarget->stats.fatigue = resolution.victimFatigue;
            mutableTarget->stats.dead = resolution.victimDied || mutableTarget->stats.dead;
            mutableTarget->revision = *targetRevision;
        }
        auto created = createCanonicalCombatWorld(playerStates, actorStates, random.snapshot());
        auto* candidate = std::get_if<CanonicalCombatWorld>(&created);
        if (!candidate)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        out.disposition = AuthoritativeMeleeDisposition::Applied;
        if (mutableTarget && resolution.code == OpenMwMeleeResolutionCode::Resolved)
            out.event = AuthoritativeMeleeEvent{ serverTick, attack.attacker, *attack.target,
                *attackerRevision, *targetRevision, resolution };
        out.candidate = std::move(*candidate);
        return out;
    }
    catch (...)
    {
        return PreparedMeleeAttack{};
    }
}
