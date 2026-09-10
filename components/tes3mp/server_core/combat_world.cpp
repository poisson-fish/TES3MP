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
        const auto validVictim = [](const TES3MP::OpenMwMeleeVictim& victim) {
            return finite(victim.health) && finite(victim.fatigue) && finite(victim.evasion)
                && finite(victim.chameleon) && finite(victim.invisibility)
                && finite(victim.normalWeaponResistance) && finite(victim.normalWeaponWeakness);
        };
        if (!(finite(s.agility) && finite(s.luck) && finite(s.strength) && finite(s.fatigueTerm)
                && finite(s.normalizedEncumbrance) && finite(s.fortifyAttack) && finite(s.blind)
                && finite(s.weaponSkill) && finite(s.handToHandSkill) && finite(s.fatigue)
                && finite(s.endurance))
            || value.maximumEncumbranceWeightUnits == 0
            || !std::ranges::all_of(value.weaponSkills, [](float skill) { return finite(skill); })
            || !validVictim(value.victim) || !validVictim(value.respawnVictim)
            || !finite(value.maximumHealth) || value.maximumHealth <= 0.f
            || !finite(value.maximumFatigue) || value.maximumFatigue < 0.f)
            return false;
        return true;
    }

    bool valid(const TES3MP::CanonicalActorCombatState& value) noexcept
    {
        const auto& s = value.stats;
        const auto& spawn = value.respawnStats;
        const auto& attacker = value.attacker;
        const bool victimValid = finite(s.health) && finite(s.fatigue) && finite(s.evasion) && finite(s.chameleon)
            && finite(s.invisibility) && finite(s.normalWeaponResistance) && finite(s.normalWeaponWeakness);
        const bool spawnValid = finite(spawn.health) && finite(spawn.fatigue) && finite(spawn.evasion)
            && finite(spawn.chameleon) && finite(spawn.invisibility) && finite(spawn.normalWeaponResistance)
            && finite(spawn.normalWeaponWeakness);
        const bool attackerValid = finite(attacker.agility) && finite(attacker.luck) && finite(attacker.strength)
            && finite(attacker.fatigueTerm) && finite(attacker.normalizedEncumbrance)
            && finite(attacker.fortifyAttack) && finite(attacker.blind) && finite(attacker.weaponSkill)
            && finite(attacker.handToHandSkill) && finite(attacker.fatigue) && finite(attacker.endurance);
        return victimValid && spawnValid && attackerValid
            && finite(value.maximumHealth) && value.maximumHealth >= 0.f
            && finite(value.maximumFatigue) && value.maximumFatigue >= 0.f
            && (value.stats.dead || value.maximumHealth > 0.f)
            && (!value.naturalWeapon || validProfile(TES3MP::MeleeWeaponProfile{
                *TES3MP::ItemPrototypeId::fromValue(1), TES3MP::MeleeWeaponSkill::ShortBlade,
                value.naturalWeapon->chopMinimum, value.naturalWeapon->chopMaximum,
                value.naturalWeapon->slashMinimum, value.naturalWeapon->slashMaximum,
                value.naturalWeapon->thrustMinimum, value.naturalWeapon->thrustMaximum,
                value.naturalWeapon->weight, 1.f, value.naturalWeapon->normalWeapon }))
            && value.attackReachQuanta <= (1u << 30);
    }

    std::uint64_t distanceSquared(const TES3MP::Position3& lhs, const TES3MP::Position3& rhs,
        std::uint32_t maximum) noexcept
    {
        const auto delta = [](std::int64_t first, std::int64_t second) -> std::optional<std::uint64_t> {
            if ((first < 0) == (second < 0))
                return first < second ? static_cast<std::uint64_t>(second - first)
                                      : static_cast<std::uint64_t>(first - second);
            const auto magnitude = [](std::int64_t value) {
                return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                                 : static_cast<std::uint64_t>(value);
            };
            const auto a = magnitude(first);
            const auto b = magnitude(second);
            if (a > std::numeric_limits<std::uint64_t>::max() - b)
                return std::nullopt;
            return a + b;
        };
        const auto x = delta(lhs.x(), rhs.x());
        const auto y = delta(lhs.y(), rhs.y());
        const auto z = delta(lhs.z(), rhs.z());
        if (!x || !y || !z || *x > maximum || *y > maximum || *z > maximum)
            return std::numeric_limits<std::uint64_t>::max();
        return *x * *x + *y * *y + *z * *z;
    }
}

namespace TES3MP
{
    std::optional<CanonicalPlayerCombatTemplate> deriveCharacterCombatTemplate(
        const CharacterProfile& profile, const CanonicalPlayerCombatTemplate& base) noexcept
    {
        if (profile.lifecycle() != CharacterLifecycle::EstablishedCharacter
            || !finite(base.stats.strength) || base.stats.strength <= 0.f
            || base.maximumEncumbranceWeightUnits == 0)
            return std::nullopt;
        const auto& attributes = profile.derived().attributes;
        const auto& skills = profile.derived().skills;
        CanonicalPlayerCombatTemplate result = base;
        result.stats.strength = static_cast<float>(attributes[0]);
        result.stats.agility = static_cast<float>(attributes[3]);
        result.stats.luck = static_cast<float>(attributes[7]);
        result.stats.endurance = static_cast<float>(attributes[5]);
        result.stats.fatigueTerm = 1.f;
        result.stats.handToHandSkill = static_cast<float>(skills[26]);
        result.stats.fatigue = static_cast<float>(attributes[0]) + static_cast<float>(attributes[2])
            + static_cast<float>(attributes[3]) + static_cast<float>(attributes[5]);
        result.victim.health = (static_cast<float>(attributes[0]) + static_cast<float>(attributes[5])) * 0.5f;
        result.victim.fatigue = result.stats.fatigue;
        result.victim.evasion = (result.stats.agility / 5.f + result.stats.luck / 10.f) * result.stats.fatigueTerm;
        result.victim.dead = false;
        result.maximumHealth = result.victim.health;
        result.maximumFatigue = result.victim.fatigue;
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::ShortBlade)]
            = static_cast<float>(skills[20]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::LongBlade)]
            = static_cast<float>(skills[5]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::BluntWeapon)]
            = static_cast<float>(skills[4]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Axe)] = static_cast<float>(skills[6]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Spear)] = static_cast<float>(skills[7]);
        const auto scaledEncumbrance = static_cast<long double>(base.maximumEncumbranceWeightUnits)
            * static_cast<long double>(attributes[0]) / static_cast<long double>(base.stats.strength);
        if (scaledEncumbrance < 1.L
            || scaledEncumbrance > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            return std::nullopt;
        result.maximumEncumbranceWeightUnits = static_cast<std::uint64_t>(scaledEncumbrance);
        return result;
    }

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
        if (mPlayers.size() >= MaximumPlayerCombatants || source.maximumEncumbranceWeightUnits == 0
            || !finite(source.maximumHealth) || source.maximumHealth <= 0.f
            || !finite(source.maximumFatigue) || source.maximumFatigue < 0.f)
            return false;
        CanonicalPlayerCombatState value{ id, CombatRevision::initial(), source.stats, source.weaponSkills,
            source.maximumEncumbranceWeightUnits, std::nullopt, std::nullopt, source.victim, source.victim,
            std::nullopt, source.maximumHealth, source.maximumFatigue };
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

    bool CanonicalCombatWorld::initializePlayerFromCharacter(PlayerId id,
        const CanonicalPlayerCombatTemplate& source, std::uint64_t inventoryWeightUnits,
        CharacterProfileRevision profileRevision) noexcept
    try
    {
        auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        if (found == mPlayers.end() || found->playerId != id)
        {
            if (!ensurePlayer(id, source, inventoryWeightUnits))
                return false;
            found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
            found->initializedCharacterProfile = profileRevision;
            return true;
        }
        if (found->initializedCharacterProfile)
            return *found->initializedCharacterProfile == profileRevision;
        const auto revision = found->revision.next();
        if (!revision || source.maximumEncumbranceWeightUnits == 0)
            return false;
        CanonicalPlayerCombatState value{ id, *revision, source.stats, source.weaponSkills,
            source.maximumEncumbranceWeightUnits, std::nullopt, profileRevision, source.victim, source.victim,
            std::nullopt, source.maximumHealth, source.maximumFatigue };
        value.stats.weaponSkill = 0.f;
        value.stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, source.maximumEncumbranceWeightUnits);
        if (!valid(value))
            return false;
        *found = std::move(value);
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
        RandomStateV1 randomState, std::optional<ServerTick> lastSimulationTick)
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
            std::vector(actors.begin(), actors.end()), randomState, lastSimulationTick);
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
        if (attackerCombat->victim.dead)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
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
        resolvedAttacker.fatigueTerm = openMwFatigueTerm(
            settings, attackerCombat->stats.fatigue, attackerCombat->maximumFatigue);
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
        mutableAttacker.victim.fatigue = resolution.attackerFatigue;
        mutableAttacker.victim.fatigueNonNegative = resolution.attackerFatigue >= 0.f;
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
            mutableTarget->stats.fatigueNonNegative = resolution.victimFatigue >= 0.f;
            mutableTarget->attacker.fatigue = resolution.victimFatigue;
            mutableTarget->stats.dead = resolution.victimDied || mutableTarget->stats.dead;
            mutableTarget->aggressionTarget = attack.attacker;
            if (mutableTarget->stats.dead && !mutableTarget->deathTick)
                mutableTarget->deathTick = serverTick;
            mutableTarget->revision = *targetRevision;
        }
        auto created = createCanonicalCombatWorld(
            playerStates, actorStates, random.snapshot(), combat.lastSimulationTick());
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

    std::variant<CombatSimulationStep, CombatSimulationError> advanceAuthoritativeCombat(
        const CanonicalCombatWorld& combat, const CanonicalServerState& players,
        const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings,
        CombatSimulationPolicy policy, ServerTick tick) noexcept
    try
    {
        if (policy.minimumActorAttackIntervalTicks == 0 || policy.respawnDelayTicks == 0
            || !finite(policy.secondsPerTick) || policy.secondsPerTick <= 0.f)
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        if (combat.lastSimulationTick() && tick < *combat.lastSimulationTick())
            return CombatSimulationError{ CombatSimulationErrorCode::TickRegression };
        const std::uint64_t elapsedTicks = combat.lastSimulationTick()
            ? tick.value() - combat.lastSimulationTick()->value() : 1;
        std::vector<CanonicalPlayerCombatState> playerStates(combat.players().begin(), combat.players().end());
        std::vector<CanonicalActorCombatState> actorStates(combat.actors().begin(), combat.actors().end());
        std::vector<AuthoritativeActorMeleeEvent> events;
        auto random = Xoshiro256StarStar::restore(combat.randomState());

        const auto activePlayer = [&](PlayerId id) {
            return std::ranges::any_of(players.activeSessions(), [&](const CanonicalSessionProgress& session) {
                return session.playerId() == id;
            });
        };
        for (std::size_t index = 0; index < actorStates.size(); ++index)
        {
            auto& actor = actorStates[index];
            const auto* spatialActor = actors.find(actor.actorId);
            if (!spatialActor)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            if (actor.stats.dead)
            {
                if (!actor.deathTick)
                    continue;
                if (tick < *actor.deathTick)
                    return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
                if (actor.respawnStats.health > 0.f
                    && tick.value() - actor.deathTick->value() >= policy.respawnDelayTicks)
                {
                    const auto revision = actor.revision.next();
                    if (!revision)
                        return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                    actor.stats = actor.respawnStats;
                    actor.attacker.fatigue = actor.respawnStats.fatigue;
                    actor.aggressionTarget.reset();
                    actor.lastAttackTick.reset();
                    actor.deathTick.reset();
                    actor.revision = *revision;
                }
                continue;
            }
            if (!actor.aggressionTarget)
                continue;
            auto target = std::ranges::lower_bound(
                playerStates, *actor.aggressionTarget, {}, &CanonicalPlayerCombatState::playerId);
            const auto* spatialPlayer = players.findPlayer(*actor.aggressionTarget);
            const bool validTarget = target != playerStates.end() && target->playerId == *actor.aggressionTarget
                && spatialPlayer && activePlayer(target->playerId) && !target->victim.dead
                && spatialPlayer->transform().cell() == spatialActor->root().cell();
            if (!validTarget)
            {
                const auto revision = actor.revision.next();
                if (!revision)
                    return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                actor.aggressionTarget.reset();
                actor.revision = *revision;
                continue;
            }
            if (actor.attackReachQuanta == 0
                || (actor.lastAttackTick && tick >= *actor.lastAttackTick
                    && tick.value() - actor.lastAttackTick->value() < policy.minimumActorAttackIntervalTicks))
                continue;
            if (actor.lastAttackTick && tick < *actor.lastAttackTick)
                return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
            const auto rangeSquared = static_cast<std::uint64_t>(actor.attackReachQuanta) * actor.attackReachQuanta;
            if (distanceSquared(spatialActor->root().position(), spatialPlayer->transform().position(),
                    actor.attackReachQuanta) > rangeSquared)
                continue;
            const auto roll = random.uniformBelow(100);
            if (!roll)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            auto resolvedActor = actor.attacker;
            resolvedActor.fatigueTerm = openMwFatigueTerm(
                settings, actor.attacker.fatigue, actor.maximumFatigue);
            const auto resolution = resolveOpenMwMelee(settings, resolvedActor, target->victim,
                OpenMwMeleeAttempt{ .type = MeleeAttackType::Chop, .attackStrength = 1.f,
                    .hitRoll0To99 = static_cast<std::uint8_t>(*roll), .contact = true,
                    .weapon = actor.naturalWeapon });
            if (resolution.code == OpenMwMeleeResolutionCode::InvalidInput)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            if (resolution.code == OpenMwMeleeResolutionCode::DeadVictim)
                continue;
            const auto actorRevision = actor.revision.next();
            const auto targetRevision = target->revision.next();
            if (!actorRevision || !targetRevision)
                return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
            actor.attacker.fatigue = resolution.attackerFatigue;
            actor.stats.fatigue = resolution.attackerFatigue;
            actor.stats.fatigueNonNegative = resolution.attackerFatigue >= 0.f;
            actor.lastAttackTick = tick;
            actor.revision = *actorRevision;
            target->victim.health = resolution.victimHealth;
            target->victim.fatigue = resolution.victimFatigue;
            target->victim.fatigueNonNegative = resolution.victimFatigue >= 0.f;
            target->victim.dead = resolution.victimDied || target->victim.dead;
            target->stats.fatigue = resolution.victimFatigue;
            if (target->victim.dead && !target->deathTick)
                target->deathTick = tick;
            target->revision = *targetRevision;
            events.push_back({ tick, actor.actorId, target->playerId, *actorRevision, *targetRevision, resolution });
            if (events.size() > MaximumAuthoritativeActorMeleeEventsPerTick)
                return CombatSimulationError{ CombatSimulationErrorCode::EventLimitExceeded, index };
        }

        for (std::size_t index = 0; index < playerStates.size(); ++index)
        {
            auto& player = playerStates[index];
            if (!player.victim.dead)
                continue;
            if (!player.deathTick)
                continue;
            if (tick < *player.deathTick)
                return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
            if (player.respawnVictim.health <= 0.f
                || tick.value() - player.deathTick->value() < policy.respawnDelayTicks)
                continue;
            const auto revision = player.revision.next();
            if (!revision)
                return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
            player.victim = player.respawnVictim;
            player.stats.fatigue = player.respawnVictim.fatigue;
            player.lastAttackTick.reset();
            player.deathTick.reset();
            player.revision = *revision;
        }

        const float elapsedSeconds = static_cast<float>(elapsedTicks) * policy.secondsPerTick;
        if (!finite(elapsedSeconds))
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        const auto recoverFatigue = [&](float current, float maximum, const OpenMwMeleeAttacker& attacker) {
            if (current >= maximum)
                return current;
            const float rate = openMwFatigueRecoveryPerSecond(
                settings, attacker.endurance, attacker.normalizedEncumbrance);
            if (!finite(rate) || rate <= 0.f)
                return current;
            return std::min(maximum, current + rate * elapsedSeconds);
        };
        // Latest-wins snapshots order passive recovery by server tick. Keeping the
        // action revision stable prevents normal network delay from making every
        // attack stale while fatigue is recovering.
        for (std::size_t index = 0; index < playerStates.size(); ++index)
        {
            auto& player = playerStates[index];
            if (player.victim.dead || !activePlayer(player.playerId))
                continue;
            const float recovered = recoverFatigue(player.stats.fatigue, player.maximumFatigue, player.stats);
            if (recovered == player.stats.fatigue)
                continue;
            player.stats.fatigue = recovered;
            player.victim.fatigue = recovered;
            player.victim.fatigueNonNegative = recovered >= 0.f;
        }
        for (std::size_t index = 0; index < actorStates.size(); ++index)
        {
            auto& actor = actorStates[index];
            const auto* spatialActor = actors.find(actor.actorId);
            const bool activeCell = spatialActor && std::ranges::any_of(players.activeSessions(),
                [&](const CanonicalSessionProgress& session) {
                    const auto* player = players.findPlayer(session.playerId());
                    return player && player->transform().cell() == spatialActor->root().cell();
                });
            if (actor.stats.dead || !activeCell)
                continue;
            const float recovered = recoverFatigue(actor.attacker.fatigue, actor.maximumFatigue, actor.attacker);
            if (recovered == actor.attacker.fatigue)
                continue;
            actor.attacker.fatigue = recovered;
            actor.stats.fatigue = recovered;
            actor.stats.fatigueNonNegative = recovered >= 0.f;
        }

        auto created = createCanonicalCombatWorld(playerStates, actorStates, random.snapshot(), tick);
        auto* candidate = std::get_if<CanonicalCombatWorld>(&created);
        if (!candidate)
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        return CombatSimulationStep{ std::move(*candidate), std::move(events) };
    }
    catch (...)
    {
        return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
    }
}
