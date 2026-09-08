#include <tes3mp/combat_world.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace
{
    bool finite(float value) noexcept { return std::isfinite(value); }

    float normalizedEncumbrance(std::uint64_t weight, std::uint64_t maximum) noexcept
    {
        return maximum == 0 ? 0.f : static_cast<float>(static_cast<double>(weight) / static_cast<double>(maximum));
    }

    bool validProfile(const TES3MP::MeleeWeaponProfile& value) noexcept
    {
        const auto skill = static_cast<std::uint8_t>(value.skill);
        return skill < static_cast<std::uint8_t>(TES3MP::MeleeWeaponSkill::Count)
            && finite(value.chopMinimum) && finite(value.chopMaximum) && value.chopMinimum >= 0.f
            && value.chopMinimum <= value.chopMaximum && finite(value.slashMinimum)
            && finite(value.slashMaximum) && value.slashMinimum >= 0.f
            && value.slashMinimum <= value.slashMaximum && finite(value.thrustMinimum)
            && finite(value.thrustMaximum) && value.thrustMinimum >= 0.f
            && value.thrustMinimum <= value.thrustMaximum && finite(value.weight) && value.weight >= 0.f
            && finite(value.reach) && value.reach > 0.f;
    }

    bool valid(const TES3MP::CanonicalPlayerCombatState& value) noexcept
    {
        const auto& s = value.stats;
        if (!(finite(s.agility) && finite(s.luck) && finite(s.strength) && finite(s.fatigueTerm)
                && finite(s.normalizedEncumbrance) && finite(s.fortifyAttack) && finite(s.blind)
                && finite(s.weaponSkill) && finite(s.handToHandSkill) && finite(s.fatigue))
            || value.maximumEncumbranceWeightUnits == 0
            || !std::ranges::all_of(value.weaponSkills, [](float skill) { return finite(skill); }))
            return false;
        return true;
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
    std::optional<MeleeWeaponCatalog> MeleeWeaponCatalog::create(
        const ItemPrototypeCatalog& items, std::span<const MeleeWeaponProfile> profiles) noexcept
    try
    {
        if (profiles.size() > MaximumItemPrototypes)
            return std::nullopt;
        std::vector<MeleeWeaponProfile> values(profiles.begin(), profiles.end());
        std::ranges::sort(values, {}, &MeleeWeaponProfile::prototypeId);
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const auto* item = items.find(values[index].prototypeId);
            if (!validProfile(values[index]) || !item || item->category != ItemCategory::Weapon
                || item->stackable
                || (item->slotMask & slotToMask(EquipmentSlot::CarriedRight)) == 0
                || item->maxCondition > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                || (index != 0 && values[index - 1].prototypeId == values[index].prototypeId))
                return std::nullopt;
        }
        return MeleeWeaponCatalog(items.contentManifestId(), std::move(values));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const MeleeWeaponProfile* MeleeWeaponCatalog::find(ItemPrototypeId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mProfiles, id, {}, &MeleeWeaponProfile::prototypeId);
        return found != mProfiles.end() && found->prototypeId == id ? &*found : nullptr;
    }

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

    bool CanonicalCombatWorld::ensurePlayer(PlayerId id, const CanonicalPlayerCombatTemplate& source,
        std::uint64_t inventoryWeightUnits) noexcept
    try
    {
        if (findPlayer(id))
            return true;
        if (mPlayers.size() >= MaximumPlayerCombatants || source.maximumEncumbranceWeightUnits == 0)
            return false;
        CanonicalPlayerCombatState value{ id, CombatRevision::initial(), source.stats, source.weaponSkills,
            source.maximumEncumbranceWeightUnits, std::nullopt };
        value.stats.weaponSkill = 0.f;
        value.stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, source.maximumEncumbranceWeightUnits);
        if (!valid(value))
            return false;
        const auto position = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        mPlayers.insert(position, std::move(value));
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool CanonicalCombatWorld::advancePlayerInventoryBinding(
        PlayerId id, std::uint64_t inventoryWeightUnits) noexcept
    {
        auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        if (found == mPlayers.end() || found->playerId != id)
            return false;
        const auto revision = found->revision.next();
        if (!revision)
            return false;
        found->stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, found->maximumEncumbranceWeightUnits);
        found->revision = *revision;
        return valid(*found);
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
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& items,
        const MeleeWeaponCatalog& weapons, const CanonicalServerState& players,
        const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings, MeleeAuthorityPolicy policy,
        ServerMeleeContactQuery& contact, ServerTick serverTick, const AuthoritativeMeleeAttack& attack) noexcept
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
        const auto* attackerInventory = inventory.findPlayer(attack.attacker);
        if (!attackerInventory || inventory.contentManifestId() != items.contentManifestId()
            || inventory.contentManifestId() != weapons.contentManifestId())
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        std::optional<ItemStackId> equippedStackId;
        std::optional<OpenMwMeleeWeapon> equippedWeapon;
        std::optional<float> equippedWeaponReach;
        OpenMwMeleeAttacker resolvedAttacker = attackerCombat->stats;
        const auto carriedRight = static_cast<std::size_t>(EquipmentSlot::CarriedRight);
        if (attackerInventory->equipment[carriedRight])
        {
            equippedStackId = attackerInventory->equipment[carriedRight];
            const auto* stack = attackerInventory->findStack(*equippedStackId);
            const auto* item = stack ? items.find(stack->prototypeId) : nullptr;
            const auto* profile = stack ? weapons.find(stack->prototypeId) : nullptr;
            if (!stack || !item || !profile || item->category != ItemCategory::Weapon
                || stack->condition > item->maxCondition || (item->maxCondition != 0 && stack->condition == 0))
            {
                out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                return out;
            }
            OpenMwMeleeWeapon weapon{ profile->chopMinimum, profile->chopMaximum,
                profile->slashMinimum, profile->slashMaximum, profile->thrustMinimum,
                profile->thrustMaximum, profile->weight, 1.f, static_cast<std::int32_t>(stack->condition),
                item->maxCondition != 0, profile->normalWeapon };
            if (weapon.hasCondition)
                weapon.normalizedCondition = static_cast<float>(stack->condition)
                    / static_cast<float>(item->maxCondition);
            equippedWeapon = weapon;
            equippedWeaponReach = profile->reach;
            resolvedAttacker.weaponSkill
                = attackerCombat->weaponSkills[static_cast<std::size_t>(profile->skill)];
        }
        else
            resolvedAttacker.weaponSkill = 0.f;
        if (!finite(attack.attackStrength) || attack.attackStrength < 0.f || attack.attackStrength > 1.f
            || !finite(attack.werewolfClawMultiplier))
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        if (actorSpatial)
        {
            const auto contactResult = contact.validate(ServerMeleeContactRequest{ attack.attacker, *attack.target,
                attack.sourceTick, attack.attackType, equippedWeapon, equippedWeaponReach },
                *playerSpatial, *actorSpatial);
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
        CanonicalInventoryWorld inventoryCandidate = inventory;
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
            .weapon = equippedWeapon };
        const OpenMwMeleeVictim emptyVictim{};
        const auto resolution = resolveOpenMwMelee(
            settings, resolvedAttacker, mutableTarget ? mutableTarget->stats : emptyVictim, attempt);
        if (resolution.code == OpenMwMeleeResolutionCode::InvalidInput)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        mutableAttacker.stats.fatigue = resolution.attackerFatigue;
        mutableAttacker.lastAttackTick = serverTick;
        mutableAttacker.revision = *attackerRevision;
        if (equippedStackId && equippedWeapon && resolution.weaponCondition != equippedWeapon->condition)
        {
            const auto condition = static_cast<std::uint32_t>(resolution.weaponCondition);
            if (inventoryCandidate.setEquippedItemCondition(attack.attacker, EquipmentSlot::CarriedRight,
                    *equippedStackId, condition, resolution.weaponBroken, serverTick)
                != EquippedConditionResult::Applied)
            {
                out.disposition = AuthoritativeMeleeDisposition::RevisionExhausted;
                return out;
            }
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
        if (inventoryCandidate != inventory)
            out.candidateInventory = std::move(inventoryCandidate);
        return out;
    }
    catch (...)
    {
        return PreparedMeleeAttack{};
    }
}
