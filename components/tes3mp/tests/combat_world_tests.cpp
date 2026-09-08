#include <tes3mp/combat_world.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    template <class T>
    T id(std::uint64_t value) { return *T::fromValue(value); }

    TES3MP::Transform root(std::int64_t x, std::uint64_t cell = 7)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return TES3MP::Transform(TES3MP::CellId::interior(id<TES3MP::CellSpaceId>(cell)),
            TES3MP::Position3(x, 0, 0), TES3MP::Orientation3(zero, zero, zero));
    }

    TES3MP::CanonicalServerState spatialPlayers(std::uint64_t cell = 7)
    {
        const std::array players{ TES3MP::CanonicalPlayerEntityState(id<TES3MP::PlayerId>(1),
            id<TES3MP::EntityId>(100), id<TES3MP::AppearanceId>(1), root(0, cell),
            TES3MP::LinearVelocity3(0, 0, 0), TES3MP::EntityRevision::initial(),
            TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial()) };
        return std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState(players, {}));
    }

    TES3MP::CanonicalActorWorld spatialActors(std::uint64_t cell = 7)
    {
        const std::array actors{ TES3MP::CanonicalActorEntityState(id<TES3MP::ActorId>(2),
            id<TES3MP::EntityId>(200), id<TES3MP::ActorPrototypeId>(3), root(10, cell),
            TES3MP::LinearVelocity3(0, 0, 0), TES3MP::EntityRevision::initial(),
            TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial(), TES3MP::ActorActivity::Idle, 0) };
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
        const std::array players{ TES3MP::CanonicalPlayerCombatState{
            id<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), attacker, skills, 100,
            std::nullopt } };
        TES3MP::OpenMwMeleeVictim victim;
        victim.health = health;
        victim.fatigue = 50.f;
        const std::array actors{ TES3MP::CanonicalActorCombatState{
            id<TES3MP::ActorId>(2), TES3MP::CombatRevision::initial(), victim } };
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

    CombatSources combatSources(std::uint32_t condition = 100)
    {
        const auto manifest = TES3MP::testContentManifest();
        const std::array declarations{ TES3MP::ItemPrototypeDeclaration{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::ItemCategory::Weapon, 5, 1, 100, 0,
            TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedRight), false, std::nullopt } };
        auto items = *TES3MP::ItemPrototypeCatalog::create(manifest, declarations);
        TES3MP::CanonicalPlayerInventoryState player{ .player = id<TES3MP::PlayerId>(1),
            .stacks = { { id<TES3MP::ItemStackId>(5), id<TES3MP::ItemPrototypeId>(4), 1, condition, 0,
                std::nullopt } } };
        player.equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedRight)]
            = id<TES3MP::ItemStackId>(5);
        auto inventory = *TES3MP::CanonicalInventoryWorld::create(manifest, items, std::array{ player }, {});
        const std::array profiles{ TES3MP::MeleeWeaponProfile{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::MeleeWeaponSkill::LongBlade, 10.f, 10.f, 10.f, 10.f, 10.f, 10.f,
            5.f, 1.f, true } };
        auto weapons = *TES3MP::MeleeWeaponCatalog::create(items, profiles);
        return { std::move(items), std::move(inventory), std::move(weapons) };
    }

    TES3MP::PreparedMeleeAttack resolve(const TES3MP::CanonicalCombatWorld& combat,
        const TES3MP::CanonicalServerState& players, const TES3MP::CanonicalActorWorld& actors,
        const TES3MP::OpenMwMeleeSettings& meleeSettings, TES3MP::MeleeAuthorityPolicy policy,
        TES3MP::ServerMeleeContactQuery& contact, TES3MP::ServerTick tick,
        const TES3MP::AuthoritativeMeleeAttack& meleeAttack)
    {
        auto sources = combatSources();
        return TES3MP::prepareAuthoritativeMeleeAttack(combat, sources.inventory, sources.items,
            sources.weapons, players, actors, meleeSettings, policy, contact, tick, meleeAttack);
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
            TES3MP::CombatRevision::initial(), id<TES3MP::ServerTick>(sourceTick),
            TES3MP::MeleeAttackType::Chop, 1.f };
    }

    bool authoritative_hit_is_atomic_and_server_randomized()
    {
        const auto before = combatWorld();
        Contact contact;
        const auto prepared = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        if (prepared.disposition != TES3MP::AuthoritativeMeleeDisposition::Applied || !prepared.candidate
            || !prepared.event || contact.calls != 1)
            return false;
        const auto* player = prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1));
        const auto* actor = prepared.candidate->findActor(id<TES3MP::ActorId>(2));
        return before.findPlayer(id<TES3MP::PlayerId>(1))->stats.fatigue == 100.f
            && player && actor && player->revision.value() == 2 && actor->revision.value() == 2
            && player->stats.fatigue == 95.f && actor->stats.health == 10.f
            && prepared.event->resolution.hit && !prepared.event->resolution.victimDied;
    }

    bool player_template_initializes_once_and_resume_preserves_state()
    {
        const auto key = *TES3MP::RandomStreamKey::fromValues(7, 8);
        auto world = std::get<TES3MP::CanonicalCombatWorld>(TES3MP::createCanonicalCombatWorld({}, {},
            TES3MP::Xoshiro256StarStar::fromWorldSeed(9, key).snapshot()));
        TES3MP::CanonicalPlayerCombatTemplate source;
        source.stats.strength = 45.f;
        source.stats.fatigue = 80.f;
        source.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Spear)] = 30.f;
        source.maximumEncumbranceWeightUnits = 200;
        if (!world.ensurePlayer(id<TES3MP::PlayerId>(1), source, 50))
            return false;
        const auto initialized = *world.findPlayer(id<TES3MP::PlayerId>(1));
        source.stats.fatigue = 10.f;
        return initialized.stats.normalizedEncumbrance == 0.25f && initialized.stats.fatigue == 80.f
            && initialized.stats.weaponSkill == 0.f
            && world.ensurePlayer(id<TES3MP::PlayerId>(1), source, 150)
            && *world.findPlayer(id<TES3MP::PlayerId>(1)) == initialized;
    }

    bool equipped_weapon_stats_condition_and_wear_come_from_canonical_sources()
    {
        const auto before = combatWorld();
        auto sources = combatSources();
        Contact contact;
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(before, sources.inventory, sources.items,
            sources.weapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        const auto* stack = prepared.candidateInventory
            ? prepared.candidateInventory->findPlayer(id<TES3MP::PlayerId>(1))->findStack(id<TES3MP::ItemStackId>(5))
            : nullptr;
        if (prepared.disposition != TES3MP::AuthoritativeMeleeDisposition::Applied || !prepared.event
            || !contact.observedWeapon || contact.observedWeapon->chopMaximum != 10.f
            || contact.observedWeapon->condition != 100 || contact.observedWeaponReach != 1.f
            || !stack || stack->condition != 99
            || sources.inventory.findPlayer(id<TES3MP::PlayerId>(1))->findStack(id<TES3MP::ItemStackId>(5))->condition
                != 100)
            return false;

        auto breakingSources = combatSources(1);
        Contact breakingContact;
        const auto broken = TES3MP::prepareAuthoritativeMeleeAttack(before, breakingSources.inventory,
            breakingSources.items, breakingSources.weapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 },
            breakingContact, id<TES3MP::ServerTick>(5), attack());
        const auto* brokenPlayer = broken.candidateInventory
            ? broken.candidateInventory->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        return brokenPlayer && brokenPlayer->findStack(id<TES3MP::ItemStackId>(5))->condition == 0
            && !brokenPlayer->equipment[static_cast<std::size_t>(TES3MP::EquipmentSlot::CarriedRight)];
    }

    bool missing_weapon_profile_rejects_without_contact_or_mutation()
    {
        const auto before = combatWorld();
        auto sources = combatSources();
        const auto emptyWeapons = TES3MP::MeleeWeaponCatalog::create(sources.items, {});
        Contact contact;
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(before, sources.inventory, sources.items,
            *emptyWeapons, spatialPlayers(), spatialActors(), settings(), { 1, 8 }, contact,
            id<TES3MP::ServerTick>(5), attack());
        return prepared.disposition == TES3MP::AuthoritativeMeleeDisposition::InvalidAttempt
            && !prepared.candidate && !prepared.candidateInventory && contact.calls == 0 && before == combatWorld();
    }

    bool weapon_catalog_rejects_unbounded_or_stackable_records()
    {
        const auto manifest = TES3MP::testContentManifest();
        const std::array declarations{ TES3MP::ItemPrototypeDeclaration{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::ItemCategory::Weapon, 5, 1, 100, 0,
            TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedRight), true, std::nullopt } };
        const auto items = *TES3MP::ItemPrototypeCatalog::create(manifest, declarations);
        const TES3MP::MeleeWeaponProfile profile{ id<TES3MP::ItemPrototypeId>(4),
            TES3MP::MeleeWeaponSkill::LongBlade, 1.f, 2.f, 1.f, 2.f, 1.f, 2.f, 5.f, 1.f, true };
        if (TES3MP::MeleeWeaponCatalog::create(items, std::array{ profile }))
            return false;
        const std::vector<TES3MP::MeleeWeaponProfile> oversized(TES3MP::MaximumItemPrototypes + 1, profile);
        return !TES3MP::MeleeWeaponCatalog::create(items, oversized);
    }

    bool lethal_hit_marks_death_in_same_candidate()
    {
        Contact contact;
        const auto prepared = resolve(combatWorld(5.f), spatialPlayers(),
            spatialActors(), settings(), { 1, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        const auto* actor = prepared.candidate ? prepared.candidate->findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return actor && actor->stats.dead && actor->stats.health == 0.f
            && prepared.event && prepared.event->resolution.victimDied;
    }

    bool stale_spatial_and_timing_fail_without_contact_or_mutation()
    {
        const auto before = combatWorld();
        Contact contact;
        auto stale = attack();
        stale.expectedTargetRevision = id<TES3MP::CombatRevision>(2);
        const auto staleResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), stale);
        auto future = attack(6);
        const auto futureResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), future);
        const auto cellResult = resolve(before, spatialPlayers(8), spatialActors(7),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        return staleResult.disposition == TES3MP::AuthoritativeMeleeDisposition::StaleTargetRevision
            && futureResult.disposition == TES3MP::AuthoritativeMeleeDisposition::FutureSourceTick
            && cellResult.disposition == TES3MP::AuthoritativeMeleeDisposition::DifferentCell
            && !staleResult.candidate && !futureResult.candidate && !cellResult.candidate && contact.calls == 0;
    }

    bool contact_history_and_cooldown_are_authoritative()
    {
        Contact contact;
        contact.result = TES3MP::MeleeContactValidation::HistoryUnavailable;
        const auto missing = resolve(combatWorld(), spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        contact.result = TES3MP::MeleeContactValidation::Accepted;
        auto first = resolve(combatWorld(), spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        if (!first.candidate)
            return false;
        auto secondAttack = attack(6);
        secondAttack.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        secondAttack.expectedTargetRevision = id<TES3MP::CombatRevision>(2);
        const auto second = resolve(*first.candidate, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(6), secondAttack);
        return missing.disposition == TES3MP::AuthoritativeMeleeDisposition::HistoryUnavailable
            && second.disposition == TES3MP::AuthoritativeMeleeDisposition::RateLimited && !second.candidate;
    }

    bool forged_targets_impossible_contact_and_stale_intent_fail_atomically()
    {
        const auto before = combatWorld();
        Contact contact;
        auto forged = attack();
        forged.target = id<TES3MP::ActorId>(99);
        const auto forgedResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), forged);
        auto staleAttacker = attack();
        staleAttacker.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        const auto staleResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), staleAttacker);
        auto tooOld = attack(1);
        const auto rewindResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), tooOld);
        contact.result = TES3MP::MeleeContactValidation::NoContact;
        const auto reachResult = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        return forgedResult.disposition == TES3MP::AuthoritativeMeleeDisposition::UnknownTarget
            && staleResult.disposition == TES3MP::AuthoritativeMeleeDisposition::StaleAttackerRevision
            && rewindResult.disposition == TES3MP::AuthoritativeMeleeDisposition::RewindWindowExceeded
            && reachResult.disposition == TES3MP::AuthoritativeMeleeDisposition::NoContact
            && !forgedResult.candidate && !staleResult.candidate && !rewindResult.candidate
            && !reachResult.candidate && contact.calls == 1 && before == combatWorld();
    }

    bool empty_swing_spends_fatigue_without_contact_or_randomness()
    {
        const auto before = combatWorld();
        Contact contact;
        auto empty = attack();
        empty.target.reset();
        const auto prepared = resolve(before, spatialPlayers(), spatialActors(),
            settings(), { 1, 8 }, contact, id<TES3MP::ServerTick>(5), empty);
        const auto* player = prepared.candidate
            ? prepared.candidate->findPlayer(id<TES3MP::PlayerId>(1)) : nullptr;
        const auto* actor = prepared.candidate
            ? prepared.candidate->findActor(id<TES3MP::ActorId>(2)) : nullptr;
        return prepared.disposition == TES3MP::AuthoritativeMeleeDisposition::Applied
            && prepared.candidate && !prepared.event && contact.calls == 0 && player && actor
            && player->stats.fatigue == 95.f && player->revision.value() == 2
            && actor->revision == TES3MP::CombatRevision::initial()
            && prepared.candidate->randomState() == before.randomState();
    }
}

int main()
{
    return authoritative_hit_is_atomic_and_server_randomized() && lethal_hit_marks_death_in_same_candidate()
            && player_template_initializes_once_and_resume_preserves_state()
            && equipped_weapon_stats_condition_and_wear_come_from_canonical_sources()
            && missing_weapon_profile_rejects_without_contact_or_mutation()
            && weapon_catalog_rejects_unbounded_or_stackable_records()
            && stale_spatial_and_timing_fail_without_contact_or_mutation()
            && contact_history_and_cooldown_are_authoritative()
            && forged_targets_impossible_contact_and_stale_intent_fail_atomically()
            && empty_swing_spends_fatigue_without_contact_or_randomness()
        ? 0
        : 1;
}
