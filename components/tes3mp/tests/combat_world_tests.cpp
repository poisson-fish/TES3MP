#include <tes3mp/combat_world.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    TES3MP::Transform root(std::int64_t x, std::uint64_t cell = 7)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return TES3MP::Transform(TES3MP::CellId::interior(id<TES3MP::CellSpaceId>(cell)), TES3MP::Position3(x, 0, 0),
            TES3MP::Orientation3(zero, zero, zero));
    }

    TES3MP::CanonicalServerState spatialPlayers(std::uint64_t cell = 7)
    {
        const std::array players{ TES3MP::CanonicalPlayerEntityState(id<TES3MP::PlayerId>(1), id<TES3MP::EntityId>(100),
            id<TES3MP::AppearanceId>(1), root(0, cell), TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial()) };
        return std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState(players, {}));
    }

    TES3MP::CanonicalServerState activeSpatialPlayers(std::uint64_t cell = 7)
    {
        const std::array players{ TES3MP::CanonicalPlayerEntityState(id<TES3MP::PlayerId>(1), id<TES3MP::EntityId>(100),
            id<TES3MP::AppearanceId>(1), root(0, cell), TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial()) };
        const std::array sessions{ TES3MP::CanonicalSessionProgress(id<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), id<TES3MP::PlayerId>(1), id<TES3MP::EntityId>(100), std::nullopt) };
        return std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState(players, sessions));
    }

    TES3MP::CanonicalServerState activeSpatialPlayersFacingAway()
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        const auto away = TES3MP::Turn32::fromValue(0x80000000u);
        const auto transform = TES3MP::Transform(TES3MP::CellId::interior(id<TES3MP::CellSpaceId>(7)),
            TES3MP::Position3(0, 0, 0), TES3MP::Orientation3(zero, zero, away));
        const std::array players{ TES3MP::CanonicalPlayerEntityState(id<TES3MP::PlayerId>(1), id<TES3MP::EntityId>(100),
            id<TES3MP::AppearanceId>(1), transform, TES3MP::LinearVelocity3(0, 0, 0), TES3MP::EntityRevision::initial(),
            TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial()) };
        const std::array sessions{ TES3MP::CanonicalSessionProgress(id<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), id<TES3MP::PlayerId>(1), id<TES3MP::EntityId>(100), std::nullopt) };
        return std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState(players, sessions));
    }

    TES3MP::CanonicalActorWorld spatialActors(std::uint64_t cell = 7)
    {
        const std::array actors{ TES3MP::CanonicalActorEntityState(id<TES3MP::ActorId>(2), id<TES3MP::EntityId>(200),
            id<TES3MP::ActorPrototypeId>(3), root(10, cell), TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial(),
            TES3MP::ActorActivity::Idle, 0) };
        return std::get<TES3MP::CanonicalActorWorld>(TES3MP::createCanonicalActorWorld(actors));
    }

    TES3MP::CanonicalActorWorld spatialActorsInFront()
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        const auto transform = TES3MP::Transform(TES3MP::CellId::interior(id<TES3MP::CellSpaceId>(7)),
            TES3MP::Position3(0, 10, 0), TES3MP::Orientation3(zero, zero, zero));
        const std::array actors{ TES3MP::CanonicalActorEntityState(id<TES3MP::ActorId>(2), id<TES3MP::EntityId>(200),
            id<TES3MP::ActorPrototypeId>(3), transform, TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial(),
            TES3MP::ActorActivity::Idle, 0) };
        return std::get<TES3MP::CanonicalActorWorld>(TES3MP::createCanonicalActorWorld(actors));
    }

    TES3MP::CanonicalCombatWorld combatWorld(float health = 20.f)
    {
        TES3MP::OpenMwMeleeAttacker attacker;
        // This cached field is deliberately hostile: equipped weapon skill must
        // be selected from the canonical skill table, not trusted here.
        attacker.weaponSkill = -100.f;
        attacker.agility = 100.f;
        attacker.luck = 100.f;
        attacker.strength = 50.f;
        attacker.fatigueTerm = 1.f;
        attacker.fatigue = 100.f;
        std::array<float, static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Count)> skills{};
        skills.fill(100.f);
        TES3MP::OpenMwMeleeVictim playerVictim;
        playerVictim.health = 100.f;
        playerVictim.fatigue = 100.f;
        const std::array players{ TES3MP::CanonicalPlayerCombatState{ .playerId = id<TES3MP::PlayerId>(1),
            .revision = TES3MP::CombatRevision::initial(),
            .stats = attacker,
            .weaponSkills = skills,
            .maximumEncumbranceWeightUnits = 100,
            .victim = playerVictim,
            .respawnVictim = playerVictim,
            .maximumHealth = 100.f,
            .maximumFatigue = 100.f } };
        TES3MP::OpenMwMeleeVictim victim;
        victim.health = health;
        victim.fatigue = 50.f;
        const std::array actors{ TES3MP::CanonicalActorCombatState{ .actorId = id<TES3MP::ActorId>(2),
            .revision = TES3MP::CombatRevision::initial(),
            .stats = victim,
            .respawnStats = victim,
            .maximumHealth = 20.f,
            .maximumFatigue = 50.f } };
        const auto key = *TES3MP::RandomStreamKey::fromValues(1, 2);
        const auto random = TES3MP::Xoshiro256StarStar::fromWorldSeed(3, key).snapshot();
        return std::get<TES3MP::CanonicalCombatWorld>(TES3MP::createCanonicalCombatWorld(players, actors, random));
    }

    struct CombatSources
    {
        TES3MP::ItemPrototypeCatalog items;
        TES3MP::CanonicalInventoryWorld inventory;
        TES3MP::MeleeWeaponCatalog weapons;
    };

    CombatSources combatSources(std::uint32_t condition = 100, bool withShield = false,
        std::uint32_t shieldCondition = 100, std::uint32_t maximumCharge = 0, std::uint32_t enchantmentCharge = 0)
    {
        const auto manifest = TES3MP::testContentManifest();
        std::vector declarations{ TES3MP::ItemPrototypeDeclaration{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::ItemCategory::Weapon, 5, 1, 100, maximumCharge,
            TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedRight), false, std::nullopt } };
        if (withShield)
            declarations.push_back({ id<TES3MP::ItemPrototypeId>(6), TES3MP::ItemCategory::Armor, 4, 1, 100, 0,
                TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedLeft), false, std::nullopt });
        auto items = *TES3MP::ItemPrototypeCatalog::create(manifest, declarations);
        TES3MP::CanonicalPlayerInventoryState player{ .player = id<TES3MP::PlayerId>(1),
            .stacks = { { id<TES3MP::ItemStackId>(5), id<TES3MP::ItemPrototypeId>(4), 1, condition, enchantmentCharge,
                std::nullopt } } };
        player.equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedRight)] = id<TES3MP::ItemStackId>(5);
        if (withShield)
        {
            player.stacks.push_back(
                { id<TES3MP::ItemStackId>(6), id<TES3MP::ItemPrototypeId>(6), 1, shieldCondition, 0, std::nullopt });
            player.equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedLeft)] = id<TES3MP::ItemStackId>(6);
        }
        auto inventory = *TES3MP::CanonicalInventoryWorld::create(manifest, items, std::array{ player }, {});
        const std::array profiles{ TES3MP::MeleeWeaponProfile{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::MeleeWeaponSkill::LongBlade, 10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 5.f, 1.f, true } };
        const std::array armor{ TES3MP::MeleeArmorProfile{
            id<TES3MP::ItemPrototypeId>(6), TES3MP::ArmorSkill::LightArmor, 30.f } };
        auto weapons = *TES3MP::MeleeWeaponCatalog::create(items, profiles,
            withShield ? std::span<const TES3MP::MeleeArmorProfile>(armor)
                       : std::span<const TES3MP::MeleeArmorProfile>{});
        return { std::move(items), std::move(inventory), std::move(weapons) };
    }

    TES3MP::PreparedMeleeAttack resolve(const TES3MP::CanonicalCombatWorld& combat,
        const TES3MP::CanonicalServerState& players, const TES3MP::CanonicalActorWorld& actors,
        const TES3MP::OpenMwMeleeSettings& meleeSettings, TES3MP::MeleeAuthorityPolicy policy,
        TES3MP::ServerMeleeContactQuery& contact, TES3MP::ServerTick tick,
        const TES3MP::AuthoritativeMeleeAttack& meleeAttack)
    {
        auto sources = combatSources();
        return TES3MP::prepareAuthoritativeMeleeAttack(combat, sources.inventory, sources.items, sources.weapons,
            players, actors, meleeSettings, policy, contact, tick, meleeAttack);
    }

    std::variant<TES3MP::CombatSimulationStep, TES3MP::CombatSimulationError> advance(
        const TES3MP::CanonicalCombatWorld& combat, const TES3MP::CanonicalServerState& players,
        const TES3MP::CanonicalActorWorld& actors, const TES3MP::OpenMwMeleeSettings& meleeSettings,
        TES3MP::CombatSimulationPolicy policy, TES3MP::ServerTick tick, bool withShield = false,
        std::uint32_t shieldCondition = 100)
    {
        auto sources = combatSources(100, withShield, shieldCondition);
        return TES3MP::advanceAuthoritativeCombat(
            combat, sources.inventory, sources.items, sources.weapons, players, actors, meleeSettings, policy, tick);
    }

    TES3MP::OpenMwMeleeSettings settings()
    {
        TES3MP::OpenMwMeleeSettings value;
        value.damageStrengthBase = 1.f;
        value.fatigueAttackBase = 5.f;
        return value;
    }

    struct Contact final : TES3MP::ServerMeleeContactQuery
    {
        TES3MP::MeleeContactValidation result = TES3MP::MeleeContactValidation::Accepted;
        std::size_t calls = 0;
        std::optional<TES3MP::OpenMwMeleeWeapon> observedWeapon;
        std::optional<float> observedWeaponReach;
        TES3MP::MeleeContactValidation validate(const TES3MP::ServerMeleeContactRequest& request,
            const TES3MP::CanonicalPlayerEntityState&, const TES3MP::CanonicalActorEntityState&) noexcept override
        {
            ++calls;
            observedWeapon = request.weapon;
            observedWeaponReach = request.weaponReach;
            return result;
        }
    };

    TES3MP::AuthoritativeMeleeAttack attack(std::uint64_t sourceTick = 5)
    {
        return { id<TES3MP::PlayerId>(1), id<TES3MP::ActorId>(2), TES3MP::CombatRevision::initial(),
            TES3MP::CombatRevision::initial(), id<TES3MP::ServerTick>(sourceTick), TES3MP::MeleeAttackType::Chop, 1.f };
    }

    bool authoritative_hit_is_atomic_and_server_randomized()
    {
        const auto before = combatWorld();
        Contact contact;
        const auto prepared = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        if (prepared.disposition != TES3MP::AuthoritativeMeleeDisposition::Applied || !prepared.candidate
            || !prepared.event || contact.calls != 1)
            return false;
        const auto* player = prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1));
        const auto* actor = prepared.candidate->findActor(id<TES3MP::ActorId>(2));
        return before.findPlayer(id<TES3MP::PlayerId>(1))->stats.fatigue == 100.f && player && actor
            && player->revision.value() == 2 && actor->revision.value() == 2 && player->stats.fatigue == 95.f
            && actor->stats.health == 10.f && actor->aggressionTarget == id<TES3MP::PlayerId>(1)
            && prepared.event->resolution.hit && !prepared.event->resolution.victimDied;
    }

    bool successful_melee_advances_only_the_server_selected_skill()
    {
        const auto baseline = combatWorld();
        std::vector<TES3MP::CanonicalPlayerCombatState> players(baseline.players().begin(), baseline.players().end());
        players[0].weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::LongBlade)] = 99.f;
        const auto longBlade = static_cast<std::size_t>(TES3MP::CombatProgressionSkill::LongBlade);
        players[0].skillRules[longBlade].useGain = 100.f;
        players[0].skillProgression[longBlade].requirementFactor = 1.f;
        const auto created = TES3MP::createCanonicalCombatWorld(players, baseline.actors(), baseline.randomState());
        const auto* world = std::get_if<TES3MP::CanonicalCombatWorld>(&created);
        if (!world)
            return false;
        Contact contact;
        const auto prepared = resolve(*world, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        const auto* player = prepared.candidate ? prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        return prepared.event && prepared.event->resolution.hit && player
            && player->weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::LongBlade)] == 100.f
            && player->skillProgression[longBlade].progress == 0.f && player->blockSkill == 0.f;
    }

    bool player_template_initializes_once_and_resume_preserves_state()
    {
        const auto key = *TES3MP::RandomStreamKey::fromValues(7, 8);
        auto world = std::get<TES3MP::CanonicalCombatWorld>(
            TES3MP::createCanonicalCombatWorld({}, {}, TES3MP::Xoshiro256StarStar::fromWorldSeed(9, key).snapshot()));
        TES3MP::CanonicalPlayerCombatTemplate source;
        source.stats.strength = 45.f;
        source.stats.fatigue = 80.f;
        source.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Spear)] = 30.f;
        source.maximumEncumbranceWeightUnits = 200;
        source.victim.health = 45.f;
        source.victim.fatigue = 80.f;
        source.maximumHealth = 45.f;
        source.maximumFatigue = 80.f;
        if (!world.ensurePlayer(id<TES3MP::PlayerId>(1), source, 50))
            return false;
        const auto initialized = *world.findPlayer(id<TES3MP::PlayerId>(1));
        source.stats.fatigue = 10.f;
        return initialized.stats.normalizedEncumbrance == 0.25f && initialized.stats.fatigue == 80.f
            && initialized.stats.weaponSkill == 0.f && world.ensurePlayer(id<TES3MP::PlayerId>(1), source, 150)
            && *world.findPlayer(id<TES3MP::PlayerId>(1)) == initialized;
    }

    bool character_profile_derives_and_initializes_combat_once()
    {
        TES3MP::CharacterDerivedState derived;
        derived.attributes = { 50, 30, 30, 40, 40, 45, 30, 35 };
        derived.skills[0] = 12;
        derived.skills[4] = 14;
        derived.skills[5] = 25;
        derived.skills[6] = 16;
        derived.skills[7] = 27;
        derived.skills[20] = 30;
        derived.skills[21] = 31;
        derived.skills[2] = 32;
        derived.skills[3] = 33;
        derived.skills[17] = 34;
        derived.skills[26] = 35;
        const auto profile = TES3MP::CharacterProfile::restore(TES3MP::CharacterLifecycle::EstablishedCharacter,
            TES3MP::CharacterCreationPhase::Complete, "Nerevar",
            TES3MP::CharacterAppearance{ id<TES3MP::RaceRecordId>(1), id<TES3MP::HeadRecordId>(2),
                id<TES3MP::HairRecordId>(3), TES3MP::CharacterSex::Female },
            TES3MP::CharacterClass{ TES3MP::CustomClassDefinition{ "Blade", "Combat test",
                TES3MP::ClassSpecialization::Combat, { 0, 1 }, { 20, 4, 6, 7, 26 }, { 5, 2, 3, 8, 9 } } },
            id<TES3MP::BirthsignRecordId>(5), derived, {}, id<TES3MP::CharacterProfileRevision>(6));
        TES3MP::CanonicalPlayerCombatTemplate base;
        base.stats.strength = 40.f;
        base.stats.endurance = 30.f;
        base.stats.fortifyAttack = 3.f;
        base.intelligence = 20.f;
        base.maximumMagicka = 40.f;
        base.magicka = 40.f;
        base.healthRecoveryPerSecond = 0.2f;
        base.magickaRecoveryPerSecond = 0.1f;
        base.skillSettings = { 1.25f, 1.f, 0.75f, 0.8f };
        base.maximumEncumbranceWeightUnits = 400;
        const auto character = profile ? TES3MP::deriveCharacterCombatTemplate(*profile, base) : std::nullopt;
        if (!character || character->stats.strength != 50.f || character->stats.agility != 40.f
            || character->stats.luck != 35.f || character->stats.fatigue != 165.f || character->stats.endurance != 45.f
            || character->maximumHealth != 47.5f || character->maximumFatigue != 165.f || character->magicka != 60.f
            || character->maximumMagicka != 60.f || std::abs(character->healthRecoveryPerSecond - 0.3f) > 0.0001f
            || std::abs(character->magickaRecoveryPerSecond - 0.15f) > 0.0001f || character->blockSkill != 12.f
            || character->stats.handToHandSkill != 35.f || character->stats.fortifyAttack != 3.f
            || character->weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::ShortBlade)] != 30.f
            || character->weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::LongBlade)] != 25.f
            || character->armorSkills != std::array<float, 4>{ 31.f, 32.f, 33.f, 34.f }
            || std::abs(character->skillProgression[static_cast<std::size_t>(TES3MP::CombatProgressionSkill::Block)]
                            .requirementFactor
                   - 1.f)
                > 0.0001f
            || std::abs(
                   character->skillProgression[static_cast<std::size_t>(TES3MP::CombatProgressionSkill::ShortBlade)]
                       .requirementFactor
                   - 0.8f)
                > 0.0001f
            || std::abs(character->skillProgression[static_cast<std::size_t>(TES3MP::CombatProgressionSkill::LongBlade)]
                            .requirementFactor
                   - 0.6f)
                > 0.0001f
            || character->maximumEncumbranceWeightUnits != 500)
            return false;
        const auto key = *TES3MP::RandomStreamKey::fromValues(7, 9);
        auto world = std::get<TES3MP::CanonicalCombatWorld>(
            TES3MP::createCanonicalCombatWorld({}, {}, TES3MP::Xoshiro256StarStar::fromWorldSeed(10, key).snapshot()));
        if (!world.initializePlayerFromCharacter(id<TES3MP::PlayerId>(1), *character, 125, profile->revision()))
            return false;
        const auto initialized = *world.findPlayer(id<TES3MP::PlayerId>(1));
        return initialized.stats.normalizedEncumbrance == 0.25f
            && initialized.initializedCharacterProfile == profile->revision()
            && world.initializePlayerFromCharacter(id<TES3MP::PlayerId>(1), *character, 250, profile->revision())
            && *world.findPlayer(id<TES3MP::PlayerId>(1)) == initialized
            && !world.initializePlayerFromCharacter(
                id<TES3MP::PlayerId>(1), *character, 250, id<TES3MP::CharacterProfileRevision>(7));
    }

    bool equipped_weapon_stats_condition_and_wear_come_from_canonical_sources()
    {
        const auto before = combatWorld();
        auto sources = combatSources();
        Contact contact;
        const auto prepared
            = TES3MP::prepareAuthoritativeMeleeAttack(before, sources.inventory, sources.items, sources.weapons,
                spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        const auto* stack = prepared.candidateInventory
            ? prepared.candidateInventory->findPlayer(id<TES3MP::PlayerId>(1))->findStack(id<TES3MP::ItemStackId>(5))
            : nullptr;
        if (prepared.disposition != TES3MP::AuthoritativeMeleeDisposition::Applied || !prepared.event
            || !contact.observedWeapon || contact.observedWeapon->chopMaximum != 10.f
            || contact.observedWeapon->condition != 100 || contact.observedWeaponReach != 1.f || !stack
            || stack->condition != 99
            || sources.inventory.findPlayer(id<TES3MP::PlayerId>(1))->findStack(id<TES3MP::ItemStackId>(5))->condition
                != 100)
            return false;

        auto breakingSources = combatSources(1);
        Contact breakingContact;
        const auto broken = TES3MP::prepareAuthoritativeMeleeAttack(before, breakingSources.inventory,
            breakingSources.items, breakingSources.weapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 },
            breakingContact, id<TES3MP::ServerTick>(5), attack());
        const auto* brokenPlayer
            = broken.candidateInventory ? broken.candidateInventory->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        return brokenPlayer && brokenPlayer->findStack(id<TES3MP::ItemStackId>(5))->condition == 0
            && !brokenPlayer->equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedRight)];
    }

    bool on_strike_magic_and_charge_commit_with_the_melee_transaction()
    {
        const auto before = combatWorld();
        auto sources = combatSources(100, false, 100, 20, 20);
        const std::array enchantments{ TES3MP::DirectEnchantmentProfile{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::DirectMagicEnchantmentKind::OnStrike, 5,
            { { TES3MP::DirectMagicTarget::Other, TES3MP::DirectMagicEffectKind::DamageHealth, 4.f, 4.f },
                { TES3MP::DirectMagicTarget::Self, TES3MP::DirectMagicEffectKind::DamageFatigue, 2.f, 2.f } } } };
        const auto magic = TES3MP::DirectMagicCatalog::create(
            TES3MP::testContentManifestId(), sources.items, {}, enchantments, {}, {});
        if (!magic)
            return false;
        Contact contact;
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(before, sources.inventory, sources.items,
            sources.weapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack(), &*magic);
        const auto* player = prepared.candidate ? prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* actor = prepared.candidate ? prepared.candidate->findActor(id<TES3MP::ActorId>(2)) : nullptr;
        const auto* inventory
            = prepared.candidateInventory ? prepared.candidateInventory->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* weapon = inventory ? inventory->findStack(id<TES3MP::ItemStackId>(5)) : nullptr;
        const bool committed = prepared.disposition == TES3MP::AuthoritativeMeleeDisposition::Applied && prepared.event
            && prepared.event->resolution.hit && player && actor && weapon && player->stats.fatigue == 93.f
            && actor->stats.health == 6.f && weapon->enchantmentCharge == 15 && weapon->condition == 99
            && sources.inventory.findPlayer(id<TES3MP::PlayerId>(1))
                    ->findStack(id<TES3MP::ItemStackId>(5))
                    ->enchantmentCharge
                == 20;

        auto exhaustedPlayer = *sources.inventory.findPlayer(id<TES3MP::PlayerId>(1));
        exhaustedPlayer.revision = id<TES3MP::InventoryRevision>(std::numeric_limits<std::uint64_t>::max());
        const auto exhaustedInventory = *TES3MP::CanonicalInventoryWorld::create(
            TES3MP::testContentManifest(), sources.items, std::array{ exhaustedPlayer }, {});
        const auto rejected = TES3MP::prepareAuthoritativeMeleeAttack(before, exhaustedInventory, sources.items,
            sources.weapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack(), &*magic);
        return committed && rejected.disposition == TES3MP::AuthoritativeMeleeDisposition::RevisionExhausted
            && !rejected.candidate && !rejected.candidateInventory
            && exhaustedInventory.findPlayer(id<TES3MP::PlayerId>(1))
                   ->findStack(id<TES3MP::ItemStackId>(5))
                   ->enchantmentCharge
            == 20
            && before.findActor(id<TES3MP::ActorId>(2))->stats.health == 20.f;
    }

    bool missing_weapon_profile_rejects_without_contact_or_mutation()
    {
        const auto before = combatWorld();
        auto sources = combatSources();
        const auto emptyWeapons = TES3MP::MeleeWeaponCatalog::create(sources.items, {});
        Contact contact;
        const auto prepared
            = TES3MP::prepareAuthoritativeMeleeAttack(before, sources.inventory, sources.items, *emptyWeapons,
                spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        return prepared.disposition == TES3MP::AuthoritativeMeleeDisposition::InvalidAttempt && !prepared.candidate
            && !prepared.candidateInventory && contact.calls == 0 && before == combatWorld();
    }

    bool weapon_catalog_rejects_unbounded_or_stackable_records()
    {
        const auto manifest = TES3MP::testContentManifest();
        const std::array declarations{ TES3MP::ItemPrototypeDeclaration{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::ItemCategory::Weapon, 5, 1, 100, 0, TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedRight), true,
            std::nullopt } };
        const auto items = *TES3MP::ItemPrototypeCatalog::create(manifest, declarations);
        const TES3MP::MeleeWeaponProfile profile{ id<TES3MP::ItemPrototypeId>(4), TES3MP::MeleeWeaponSkill::LongBlade,
            1.f, 2.f, 1.f, 2.f, 1.f, 2.f, 5.f, 1.f, true };
        if (TES3MP::MeleeWeaponCatalog::create(items, std::array{ profile }))
            return false;
        const std::vector<TES3MP::MeleeWeaponProfile> oversized(TES3MP::MaximumItemPrototypes + 1, profile);
        return !TES3MP::MeleeWeaponCatalog::create(items, oversized);
    }

    bool lethal_hit_marks_death_in_same_candidate()
    {
        Contact contact;
        const auto prepared = resolve(combatWorld(5.f), spatialPlayers(), spatialActors(), settings(), { 1, 8 },
            contact, id<TES3MP::ServerTick>(5), attack());
        const auto* actor = prepared.candidate ? prepared.candidate->findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return actor && actor->stats.dead && actor->stats.health == 0.f && prepared.event
            && prepared.event->resolution.victimDied;
    }

    bool stale_spatial_and_timing_fail_without_contact_or_mutation()
    {
        const auto before = combatWorld();
        Contact contact;
        auto stale = attack();
        stale.expectedTargetRevision = id<TES3MP::CombatRevision>(2);
        const auto staleResult = resolve(
            before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), stale);
        auto future = attack(6);
        const auto futureResult = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), future);
        const auto cellResult = resolve(before, spatialPlayers(8), spatialActors(7), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        return staleResult.disposition == TES3MP::AuthoritativeMeleeDisposition::StaleTargetRevision
            && futureResult.disposition == TES3MP::AuthoritativeMeleeDisposition::FutureSourceTick
            && cellResult.disposition == TES3MP::AuthoritativeMeleeDisposition::DifferentCell && !staleResult.candidate
            && !futureResult.candidate && !cellResult.candidate && contact.calls == 0;
    }

    bool contact_history_and_cooldown_are_authoritative()
    {
        Contact contact;
        contact.result = TES3MP::MeleeContactValidation::HistoryUnavailable;
        const auto missing = resolve(combatWorld(), spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        contact.result = TES3MP::MeleeContactValidation::Accepted;
        auto first = resolve(combatWorld(), spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        if (!first.candidate)
            return false;
        auto secondAttack = attack(6);
        secondAttack.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        secondAttack.expectedTargetRevision = id<TES3MP::CombatRevision>(2);
        const auto second = resolve(*first.candidate, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(6), secondAttack);
        return missing.disposition == TES3MP::AuthoritativeMeleeDisposition::HistoryUnavailable
            && second.disposition == TES3MP::AuthoritativeMeleeDisposition::RateLimited && !second.candidate;
    }

    bool forged_targets_impossible_contact_and_stale_intent_fail_atomically()
    {
        const auto before = combatWorld();
        Contact contact;
        auto forged = attack();
        forged.target = id<TES3MP::ActorId>(99);
        const auto forgedResult = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(10), forged);
        auto staleAttacker = attack();
        staleAttacker.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        const auto staleResult = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(10), staleAttacker);
        auto tooOld = attack(1);
        const auto rewindResult = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(10), tooOld);
        contact.result = TES3MP::MeleeContactValidation::NoContact;
        const auto reachResult = resolve(before, spatialPlayers(), spatialActors(), settings(), { 2, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        return forgedResult.disposition == TES3MP::AuthoritativeMeleeDisposition::UnknownTarget
            && staleResult.disposition == TES3MP::AuthoritativeMeleeDisposition::StaleAttackerRevision
            && rewindResult.disposition == TES3MP::AuthoritativeMeleeDisposition::RewindWindowExceeded
            && reachResult.disposition == TES3MP::AuthoritativeMeleeDisposition::NoContact && !forgedResult.candidate
            && !staleResult.candidate && !rewindResult.candidate && !reachResult.candidate && contact.calls == 1
            && before == combatWorld();
    }

    bool empty_swing_spends_fatigue_without_contact_or_randomness()
    {
        const auto before = combatWorld();
        Contact contact;
        auto empty = attack();
        empty.target.reset();
        const auto prepared = resolve(
            before, spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact, id<TES3MP::ServerTick>(5), empty);
        const auto* player = prepared.candidate ? prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* actor = prepared.candidate ? prepared.candidate->findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return prepared.disposition == TES3MP::AuthoritativeMeleeDisposition::Applied && prepared.candidate
            && !prepared.event && contact.calls == 0 && player && actor && player->stats.fatigue == 95.f
            && player->revision.value() == 2 && actor->revision == TES3MP::CombatRevision::initial()
            && prepared.candidate->randomState() == before.randomState();
    }

    TES3MP::CanonicalCombatWorld retaliatingCombatWorld(float playerHealth)
    {
        TES3MP::OpenMwMeleeAttacker playerAttacker;
        playerAttacker.fatigue = 20.f;
        TES3MP::OpenMwMeleeVictim playerVictim;
        playerVictim.health = playerHealth;
        playerVictim.fatigue = 20.f;
        const std::array players{ TES3MP::CanonicalPlayerCombatState{ .playerId = id<TES3MP::PlayerId>(1),
            .revision = TES3MP::CombatRevision::initial(),
            .stats = playerAttacker,
            .maximumEncumbranceWeightUnits = 100,
            .victim = playerVictim,
            .respawnVictim = playerVictim,
            .maximumHealth = playerHealth,
            .maximumFatigue = 20.f } };
        TES3MP::OpenMwMeleeVictim actorVictim;
        actorVictim.health = 20.f;
        actorVictim.fatigue = 20.f;
        TES3MP::OpenMwMeleeAttacker actorAttacker;
        actorAttacker.agility = 100.f;
        actorAttacker.luck = 100.f;
        actorAttacker.strength = 50.f;
        actorAttacker.fatigueTerm = 1.f;
        actorAttacker.weaponSkill = 100.f;
        actorAttacker.fatigue = 20.f;
        const TES3MP::OpenMwMeleeWeapon natural{ 10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 0.f, 1.f, 0, false, false };
        const std::array actors{ TES3MP::CanonicalActorCombatState{ .actorId = id<TES3MP::ActorId>(2),
            .revision = TES3MP::CombatRevision::initial(),
            .stats = actorVictim,
            .respawnStats = actorVictim,
            .attacker = actorAttacker,
            .naturalWeapon = natural,
            .attackReachQuanta = 128 * 1024,
            .aggressionTarget = id<TES3MP::PlayerId>(1),
            .maximumHealth = 20.f,
            .maximumFatigue = 20.f,
            .creature = true } };
        const auto key = *TES3MP::RandomStreamKey::fromValues(3, 4);
        return std::get<TES3MP::CanonicalCombatWorld>(TES3MP::createCanonicalCombatWorld(
            players, actors, TES3MP::Xoshiro256StarStar::fromWorldSeed(5, key).snapshot()));
    }

    bool actor_retaliation_damage_cooldown_and_player_respawn_are_authoritative()
    {
        const auto before = retaliatingCombatWorld(5.f);
        const auto advanced
            = advance(before, activeSpatialPlayers(), spatialActors(), settings(), { 2, 5 }, id<TES3MP::ServerTick>(5));
        const auto* first = std::get_if<TES3MP::CombatSimulationStep>(&advanced);
        if (!first || first->events.size() != 1 || !first->events[0].resolution.hit
            || !first->events[0].resolution.victimDied)
            return false;
        const auto* player = first->combat.findPlayer(id<TES3MP::PlayerId>(1));
        const auto* actor = first->combat.findActor(id<TES3MP::ActorId>(2));
        if (!player || !actor || !player->victim.dead || player->victim.health != 0.f
            || player->deathTick != id<TES3MP::ServerTick>(5) || actor->lastAttackTick != id<TES3MP::ServerTick>(5))
            return false;
        const auto cooldown = advance(
            first->combat, activeSpatialPlayers(), spatialActors(), settings(), { 2, 5 }, id<TES3MP::ServerTick>(6));
        const auto* second = std::get_if<TES3MP::CombatSimulationStep>(&cooldown);
        if (!second || !second->events.empty() || !second->combat.findPlayer(id<TES3MP::PlayerId>(1))->victim.dead)
            return false;
        const auto respawn = advance(
            second->combat, activeSpatialPlayers(), spatialActors(), settings(), { 2, 5 }, id<TES3MP::ServerTick>(10));
        const auto* third = std::get_if<TES3MP::CombatSimulationStep>(&respawn);
        const auto* respawned = third ? third->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        return respawned && !respawned->victim.dead && respawned->victim.health == 5.f && !respawned->deathTick
            && respawned->revision.value() == 3;
    }

    bool elemental_shield_and_disease_consequences_commit_with_actor_melee()
    {
        const auto baseline = retaliatingCombatWorld(20.f);
        std::vector<TES3MP::CanonicalPlayerCombatState> players(baseline.players().begin(), baseline.players().end());
        players[0].magicDefense.fireShield = 20.f;
        std::vector<TES3MP::CanonicalActorCombatState> actors(baseline.actors().begin(), baseline.actors().end());
        actors[0].magicDefense.fireResistance = -100.f;
        const auto before = std::get<TES3MP::CanonicalCombatWorld>(
            TES3MP::createCanonicalCombatWorld(players, actors, baseline.randomState()));
        auto sources = combatSources();
        const std::array magicActors{ TES3MP::DirectActorMagicProfile{ id<TES3MP::ActorId>(2), {},
            { { id<TES3MP::SpellRecordId>(9), TES3MP::DirectDiseaseKind::Common,
                { { TES3MP::DirectMagicTarget::Other, TES3MP::DirectMagicEffectKind::DamageHealth, 3.f, 3.f } } } } } };
        const auto magic = TES3MP::DirectMagicCatalog::create(TES3MP::testContentManifestId(), sources.items,
            { .elementalShieldMultiplier = 0.1f, .diseaseTransferChance = 100.f }, {}, {}, magicActors);
        if (!magic)
            return false;
        const auto result
            = TES3MP::advanceAuthoritativeCombat(before, sources.inventory, sources.items, sources.weapons,
                activeSpatialPlayers(), spatialActors(), settings(), { 2, 5 }, id<TES3MP::ServerTick>(5), &*magic);
        const auto* step = std::get_if<TES3MP::CombatSimulationStep>(&result);
        const auto* player = step ? step->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* actor = step ? step->combat.findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return step && step->events.size() == 1 && step->events[0].resolution.hit && player && actor
            && player->victim.health == 7.f && actor->stats.health == 16.f
            && player->contractedDiseases == std::vector{ id<TES3MP::SpellRecordId>(9) }
        && before.findPlayer(id<TES3MP::PlayerId>(1))->contractedDiseases.empty();
    }

    bool player_blocking_uses_server_roll_facing_and_canonical_shield_wear()
    {
        auto blockSettings = settings();
        blockSettings.blockMinimumChance = 100.f;
        blockSettings.blockMaximumChance = 100.f;
        blockSettings.combatBlockLeftAngle = -90.f;
        blockSettings.combatBlockRightAngle = 30.f;
        blockSettings.fatigueBlockBase = 2.f;
        const auto result = advance(retaliatingCombatWorld(20.f), activeSpatialPlayers(), spatialActorsInFront(),
            blockSettings, { 2, 5 }, id<TES3MP::ServerTick>(5), true, 5);
        const auto* step = std::get_if<TES3MP::CombatSimulationStep>(&result);
        const auto* player = step ? step->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* inventory
            = step && step->inventory ? step->inventory->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* shield = inventory ? inventory->findStack(id<TES3MP::ItemStackId>(6)) : nullptr;
        const auto facingAwayResult = advance(retaliatingCombatWorld(20.f), activeSpatialPlayersFacingAway(),
            spatialActorsInFront(), blockSettings, { 2, 5 }, id<TES3MP::ServerTick>(5), true, 5);
        const auto* facingAway = std::get_if<TES3MP::CombatSimulationStep>(&facingAwayResult);
        return step && step->events.size() == 1 && step->events[0].resolution.hit && step->events[0].resolution.blocked
            && step->events[0].resolution.damage == 0.f && player && player->victim.health == 20.f
            && player->stats.fatigue == 18.f && shield && shield->condition == 0
            && !inventory->equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedLeft)] && facingAway
            && facingAway->events.size() == 1 && !facingAway->events[0].resolution.blocked
            && facingAway->events[0].resolution.damage == 10.f && !facingAway->inventory
            && facingAway->combat.findPlayer(id<TES3MP::PlayerId>(1))->victim.health == 10.f;
    }

    bool difficulty_scaling_is_server_selected_for_both_damage_directions()
    {
        auto difficultySettings = settings();
        difficultySettings.difficultyMultiplier = 5.f;
        Contact contact;
        const auto outgoing = resolve(combatWorld(), spatialPlayers(), spatialActors(), difficultySettings,
            { 1, 8, 128 * 1024, 100 }, contact, id<TES3MP::ServerTick>(5), attack());
        const auto incomingResult = advance(retaliatingCombatWorld(100.f), activeSpatialPlayers(), spatialActors(),
            difficultySettings, { 2, 5, 0.016f, 100 }, id<TES3MP::ServerTick>(5));
        const auto* incoming = std::get_if<TES3MP::CombatSimulationStep>(&incomingResult);
        return outgoing.event && outgoing.event->resolution.damage == 8.f
            && outgoing.candidate->findActor(id<TES3MP::ActorId>(2))->stats.health == 12.f && incoming
            && incoming->events.size() == 1 && incoming->events[0].resolution.damage == 60.f
            && incoming->combat.findPlayer(id<TES3MP::PlayerId>(1))->victim.health == 40.f;
    }

    bool armor_mitigation_wear_and_progression_commit_atomically()
    {
        auto before = retaliatingCombatWorld(100.f);
        std::vector<TES3MP::CanonicalPlayerCombatState> players(before.players().begin(), before.players().end());
        players[0].armorSkills[0] = 30.f;
        const auto lightArmor = static_cast<std::size_t>(TES3MP::CombatProgressionSkill::LightArmor);
        players[0].skillRules[lightArmor].useGain = 1000.f;
        players[0].skillProgression[lightArmor].requirementFactor = 1.f;
        before = std::get<TES3MP::CanonicalCombatWorld>(
            TES3MP::createCanonicalCombatWorld(players, before.actors(), before.randomState()));

        constexpr std::array armorSlots{ TES3MP::EquipmentSlot::Helmet, TES3MP::EquipmentSlot::Cuirass,
            TES3MP::EquipmentSlot::Greaves, TES3MP::EquipmentSlot::LeftPauldron, TES3MP::EquipmentSlot::RightPauldron,
            TES3MP::EquipmentSlot::LeftGauntlet, TES3MP::EquipmentSlot::RightGauntlet, TES3MP::EquipmentSlot::Boots,
            TES3MP::EquipmentSlot::CarriedLeft };
        std::uint32_t slotMask = 0;
        for (const auto slot : armorSlots)
            slotMask |= TES3MP::slotToMask(slot);
        const auto manifest = TES3MP::testContentManifest();
        const std::array declarations{ TES3MP::ItemPrototypeDeclaration{ id<TES3MP::ItemPrototypeId>(6),
            TES3MP::ItemCategory::Armor, 10, 1, 100, 0, slotMask, false, std::nullopt } };
        const auto items = *TES3MP::ItemPrototypeCatalog::create(manifest, declarations);
        TES3MP::CanonicalPlayerInventoryState inventoryPlayer{ .player = id<TES3MP::PlayerId>(1) };
        for (std::size_t index = 0; index < armorSlots.size(); ++index)
        {
            const auto stackId = id<TES3MP::ItemStackId>(10 + index);
            inventoryPlayer.stacks.push_back({ stackId, id<TES3MP::ItemPrototypeId>(6), 1, 100, 0, std::nullopt });
            inventoryPlayer.equipment[static_cast<std::size_t>(armorSlots[index])] = stackId;
        }
        const auto inventory
            = *TES3MP::CanonicalInventoryWorld::create(manifest, items, std::array{ inventoryPlayer }, {});
        const std::array armor{ TES3MP::MeleeArmorProfile{
            id<TES3MP::ItemPrototypeId>(6), TES3MP::ArmorSkill::LightArmor, 30.f } };
        const auto weapons = *TES3MP::MeleeWeaponCatalog::create(items, {}, armor);
        auto armorSettings = settings();
        armorSettings.blockMinimumChance = 0.f;
        armorSettings.blockMaximumChance = 0.f;
        armorSettings.unarmedCreatureAttacksDamageArmor = true;
        const auto advanced = TES3MP::advanceAuthoritativeCombat(before, inventory, items, weapons,
            activeSpatialPlayers(), spatialActors(), armorSettings, { 2, 5 }, id<TES3MP::ServerTick>(5));
        const auto* step = std::get_if<TES3MP::CombatSimulationStep>(&advanced);
        const auto* player = step ? step->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* changed = step && step->inventory ? step->inventory->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        std::size_t worn = 0;
        if (changed)
            for (const auto& stack : changed->stacks)
                worn += stack.condition == 92 ? 1 : 0;
        const bool committed = step && step->events.size() == 1 && step->events[0].resolution.hit
            && !step->events[0].resolution.blocked && std::abs(step->events[0].resolution.damage - 2.5f) < 0.0001f
            && player && std::abs(player->victim.health - 97.5f) < 0.0001f && player->armorSkills[0] == 31.f
            && player->skillProgression[lightArmor].progress == 0.f && changed && changed->revision.value() == 2
            && worn == 1
            && inventory.findPlayer(id<TES3MP::PlayerId>(1))->revision == TES3MP::InventoryRevision::initial();

        std::vector<TES3MP::CanonicalActorCombatState> lowDamageActors(before.actors().begin(), before.actors().end());
        lowDamageActors[0].naturalWeapon
            = TES3MP::OpenMwMeleeWeapon{ 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 0, false, false };
        const auto lowDamageBefore = std::get<TES3MP::CanonicalCombatWorld>(
            TES3MP::createCanonicalCombatWorld(before.players(), lowDamageActors, before.randomState()));
        const auto lowDamageResult = TES3MP::advanceAuthoritativeCombat(lowDamageBefore, inventory, items, weapons,
            activeSpatialPlayers(), spatialActors(), armorSettings, { 2, 5 }, id<TES3MP::ServerTick>(5));
        const auto* lowDamage = std::get_if<TES3MP::CombatSimulationStep>(&lowDamageResult);
        std::size_t lowDamageWorn = 0;
        if (lowDamage && lowDamage->inventory)
            if (const auto* lowInventory = lowDamage->inventory->findPlayer(id<TES3MP::PlayerId>(1)))
                for (const auto& stack : lowInventory->stacks)
                    lowDamageWorn += stack.condition == 99 ? 1 : 0;

        inventoryPlayer.revision = id<TES3MP::InventoryRevision>(std::numeric_limits<std::uint64_t>::max());
        const auto exhaustedInventory
            = *TES3MP::CanonicalInventoryWorld::create(manifest, items, std::array{ inventoryPlayer }, {});
        const auto rejected = TES3MP::advanceAuthoritativeCombat(before, exhaustedInventory, items, weapons,
            activeSpatialPlayers(), spatialActors(), armorSettings, { 2, 5 }, id<TES3MP::ServerTick>(5));
        const auto* error = std::get_if<TES3MP::CombatSimulationError>(&rejected);
        return committed && lowDamage && lowDamage->events.size() == 1 && lowDamage->events[0].resolution.damage == 1.f
            && lowDamageWorn == 1 && error && error->code == TES3MP::CombatSimulationErrorCode::RevisionExhausted
            && before.findPlayer(id<TES3MP::PlayerId>(1))->victim.health == 100.f
            && exhaustedInventory.findPlayer(id<TES3MP::PlayerId>(1))->stacks[0].condition == 100;
    }

    bool actor_respawn_restores_baseline_and_clears_combat_intent()
    {
        const auto before = retaliatingCombatWorld(20.f);
        std::vector<TES3MP::CanonicalActorCombatState> actors(before.actors().begin(), before.actors().end());
        actors[0].stats.health = 0.f;
        actors[0].stats.dead = true;
        actors[0].deathTick = id<TES3MP::ServerTick>(5);
        actors[0].lastAttackTick = id<TES3MP::ServerTick>(4);
        const auto dead = TES3MP::createCanonicalCombatWorld(before.players(), actors, before.randomState());
        const auto advanced = advance(std::get<TES3MP::CanonicalCombatWorld>(dead), activeSpatialPlayers(),
            spatialActors(), settings(), { 2, 5 }, id<TES3MP::ServerTick>(10));
        const auto* step = std::get_if<TES3MP::CombatSimulationStep>(&advanced);
        const auto* actor = step ? step->combat.findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return actor && !actor->stats.dead && actor->stats.health == 20.f && actor->stats.fatigue == 20.f
            && actor->attacker.fatigue == 20.f && !actor->aggressionTarget && !actor->lastAttackTick
            && !actor->deathTick && actor->revision.value() == 2;
    }

    bool fatigue_recovery_is_tick_deterministic_bounded_and_active_only()
    {
        TES3MP::OpenMwMeleeAttacker playerAttacker;
        playerAttacker.fatigue = 50.f;
        playerAttacker.endurance = 40.f;
        playerAttacker.normalizedEncumbrance = 0.5f;
        TES3MP::OpenMwMeleeVictim playerVictim;
        playerVictim.health = 20.f;
        playerVictim.fatigue = 50.f;
        const std::array players{ TES3MP::CanonicalPlayerCombatState{ .playerId = id<TES3MP::PlayerId>(1),
            .stats = playerAttacker,
            .maximumEncumbranceWeightUnits = 100,
            .victim = playerVictim,
            .respawnVictim = playerVictim,
            .maximumHealth = 20.f,
            .maximumFatigue = 100.f } };
        TES3MP::OpenMwMeleeAttacker actorAttacker;
        actorAttacker.fatigue = 10.f;
        actorAttacker.endurance = 20.f;
        TES3MP::OpenMwMeleeVictim actorVictim;
        actorVictim.health = 20.f;
        actorVictim.fatigue = 10.f;
        const std::array actors{ TES3MP::CanonicalActorCombatState{ .actorId = id<TES3MP::ActorId>(2),
            .stats = actorVictim,
            .respawnStats = actorVictim,
            .attacker = actorAttacker,
            .maximumHealth = 20.f,
            .maximumFatigue = 20.f } };
        const auto key = *TES3MP::RandomStreamKey::fromValues(11, 12);
        const auto world = std::get<TES3MP::CanonicalCombatWorld>(TES3MP::createCanonicalCombatWorld(
            players, actors, TES3MP::Xoshiro256StarStar::fromWorldSeed(13, key).snapshot()));
        auto recoverySettings = settings();
        recoverySettings.fatigueReturnBase = 0.02f;
        recoverySettings.fatigueReturnMultiplier = 0.04f;
        recoverySettings.enduranceFatigueMultiplier = 0.1f;
        const auto firstResult = advance(world, activeSpatialPlayers(), spatialActors(), recoverySettings,
            { 2, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* first = std::get_if<TES3MP::CombatSimulationStep>(&firstResult);
        if (!first)
            return false;
        const auto sameTickResult = advance(first->combat, activeSpatialPlayers(), spatialActors(), recoverySettings,
            { 2, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* sameTick = std::get_if<TES3MP::CombatSimulationStep>(&sameTickResult);
        if (!sameTick || sameTick->combat != first->combat)
            return false;
        const auto laterResult = advance(sameTick->combat, activeSpatialPlayers(), spatialActors(), recoverySettings,
            { 2, 5, 0.5f }, id<TES3MP::ServerTick>(7));
        const auto* later = std::get_if<TES3MP::CombatSimulationStep>(&laterResult);
        const auto* firstPlayer = first->combat.findPlayer(id<TES3MP::PlayerId>(1));
        const auto* firstActor = first->combat.findActor(id<TES3MP::ActorId>(2));
        const auto* laterPlayer = later ? later->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* laterActor = later ? later->combat.findActor(id<TES3MP::ActorId>(2)) : nullptr;
        const auto inactiveResult = advance(
            world, spatialPlayers(), spatialActors(), recoverySettings, { 2, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* inactive = std::get_if<TES3MP::CombatSimulationStep>(&inactiveResult);
        return firstPlayer && firstActor && laterPlayer && laterActor && inactive
            && std::abs(firstPlayer->stats.fatigue - 50.08f) < 0.0001f
            && std::abs(firstActor->stats.fatigue - 10.06f) < 0.0001f
            && std::abs(laterPlayer->stats.fatigue - 50.24f) < 0.0001f
            && std::abs(laterActor->stats.fatigue - 10.18f) < 0.0001f
            && laterPlayer->revision == TES3MP::CombatRevision::initial()
            && laterActor->revision == TES3MP::CombatRevision::initial()
            && inactive->combat.findPlayer(id<TES3MP::PlayerId>(1))->stats.fatigue == 50.f
            && inactive->combat.findActor(id<TES3MP::ActorId>(2))->stats.fatigue == 10.f;
    }

    bool health_and_magicka_recovery_are_tick_bounded_active_and_out_of_combat()
    {
        const auto baseline = combatWorld();
        std::vector<TES3MP::CanonicalPlayerCombatState> players(baseline.players().begin(), baseline.players().end());
        players[0].victim.health = 10.f;
        players[0].magicka = 5.f;
        players[0].maximumMagicka = 20.f;
        players[0].healthRecoveryPerSecond = 2.f;
        players[0].magickaRecoveryPerSecond = 3.f;
        const auto created = TES3MP::createCanonicalCombatWorld(players, baseline.actors(), baseline.randomState());
        const auto* world = std::get_if<TES3MP::CanonicalCombatWorld>(&created);
        if (!world)
            return false;
        const auto recoveredResult = advance(
            *world, activeSpatialPlayers(), spatialActors(), settings(), { 2, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* recovered = std::get_if<TES3MP::CombatSimulationStep>(&recoveredResult);
        const auto* player = recovered ? recovered->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto inactiveResult
            = advance(*world, spatialPlayers(), spatialActors(), settings(), { 2, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* inactive = std::get_if<TES3MP::CombatSimulationStep>(&inactiveResult);

        auto engaged = retaliatingCombatWorld(20.f);
        std::vector<TES3MP::CanonicalPlayerCombatState> engagedPlayers(
            engaged.players().begin(), engaged.players().end());
        engagedPlayers[0].victim.health = 10.f;
        engagedPlayers[0].maximumHealth = 20.f;
        engagedPlayers[0].magicka = 5.f;
        engagedPlayers[0].maximumMagicka = 20.f;
        engagedPlayers[0].healthRecoveryPerSecond = 2.f;
        engagedPlayers[0].magickaRecoveryPerSecond = 3.f;
        std::vector<TES3MP::CanonicalActorCombatState> engagedActors(engaged.actors().begin(), engaged.actors().end());
        engagedActors[0].lastAttackTick = id<TES3MP::ServerTick>(5);
        const auto engagedWorld
            = TES3MP::createCanonicalCombatWorld(engagedPlayers, engagedActors, engaged.randomState());
        const auto engagedResult = advance(std::get<TES3MP::CanonicalCombatWorld>(engagedWorld), activeSpatialPlayers(),
            spatialActors(), settings(), { 100, 5, 0.5f }, id<TES3MP::ServerTick>(5));
        const auto* engagedStep = std::get_if<TES3MP::CombatSimulationStep>(&engagedResult);
        const auto* engagedPlayer = engagedStep ? engagedStep->combat.findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        return player && std::abs(player->victim.health - 11.f) < 0.0001f && std::abs(player->magicka - 6.5f) < 0.0001f
            && player->revision == TES3MP::CombatRevision::initial() && inactive
            && inactive->combat.findPlayer(id<TES3MP::PlayerId>(1))->victim.health == 10.f
            && inactive->combat.findPlayer(id<TES3MP::PlayerId>(1))->magicka == 5.f && engagedPlayer
            && engagedPlayer->victim.health == 10.f && engagedPlayer->magicka == 5.f;
    }
}
int main()
{
    return authoritative_hit_is_atomic_and_server_randomized()
            && successful_melee_advances_only_the_server_selected_skill() && lethal_hit_marks_death_in_same_candidate()
            && player_template_initializes_once_and_resume_preserves_state()
            && character_profile_derives_and_initializes_combat_once()
            && equipped_weapon_stats_condition_and_wear_come_from_canonical_sources()
            && on_strike_magic_and_charge_commit_with_the_melee_transaction()
            && missing_weapon_profile_rejects_without_contact_or_mutation()
            && weapon_catalog_rejects_unbounded_or_stackable_records()
            && stale_spatial_and_timing_fail_without_contact_or_mutation()
            && contact_history_and_cooldown_are_authoritative()
            && forged_targets_impossible_contact_and_stale_intent_fail_atomically()
            && empty_swing_spends_fatigue_without_contact_or_randomness()
            && actor_retaliation_damage_cooldown_and_player_respawn_are_authoritative()
            && elemental_shield_and_disease_consequences_commit_with_actor_melee()
            && player_blocking_uses_server_roll_facing_and_canonical_shield_wear()
            && difficulty_scaling_is_server_selected_for_both_damage_directions()
            && armor_mitigation_wear_and_progression_commit_atomically()
            && actor_respawn_restores_baseline_and_clears_combat_intent()
            && fatigue_recovery_is_tick_deterministic_bounded_and_active_only()
            && health_and_magicka_recovery_are_tick_bounded_active_and_out_of_combat()
        ? 0
        : 1;
}
