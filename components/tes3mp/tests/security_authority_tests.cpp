#include <tes3mp/security.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <variant>

namespace
{
    using namespace TES3MP;

    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "security_authority_tests assertion failed at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    struct Fixture
    {
        ContentManifest manifest;
        InteractiveObjectCatalog objectCatalog;
        CanonicalInteractiveObjectWorld objects;
        ItemPrototypeCatalog itemCatalog;
        CanonicalInventoryWorld inventory;
        CanonicalCombatWorld combat;
        CanonicalServerState players;
    };

    Fixture fixture(float security)
    {
        auto manifest = testContentManifest();
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const auto zero = Turn32::fromValue(0);
        const Transform root(cell, Position3(10, 0, 0), Orientation3(zero, zero, zero));
        const std::array objectDeclarations{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, cell, root, std::nullopt,
            ObjectLockDeclaration{ true, 10, std::nullopt },
            ObjectTrapDeclaration{ true, id<TrapPrototypeId>(2), 10 } } };
        auto objectCatalog = *InteractiveObjectCatalog::create(manifest, objectDeclarations);
        auto objects = std::get<CanonicalInteractiveObjectWorld>(
            createInitialCanonicalInteractiveObjectWorld(objectCatalog));

        const std::array itemDeclarations{
            ItemPrototypeDeclaration{ .id = id<ItemPrototypeId>(10), .category = ItemCategory::Lockpick,
                .weightUnits = 1, .value = 1, .maxCondition = 2, .stackable = false, .toolQuality = 1.f },
            ItemPrototypeDeclaration{ .id = id<ItemPrototypeId>(11), .category = ItemCategory::Probe,
                .weightUnits = 1, .value = 1, .maxCondition = 2, .stackable = false, .toolQuality = 1.f },
        };
        auto itemCatalog = *ItemPrototypeCatalog::create(manifest, itemDeclarations);
        const std::array inventoryPlayers{ CanonicalPlayerInventoryState{ .player = id<PlayerId>(1),
            .stacks = { CanonicalItemStack{ id<ItemStackId>(20), id<ItemPrototypeId>(10), 1, 2 },
                CanonicalItemStack{ id<ItemStackId>(21), id<ItemPrototypeId>(11), 1, 2 } } } };
        auto inventory = *CanonicalInventoryWorld::create(manifest, itemCatalog, inventoryPlayers, {});

        CanonicalPlayerCombatState combatPlayer{ .playerId = id<PlayerId>(1),
            .stats = OpenMwMeleeAttacker{ .agility = 0.f, .luck = 0.f, .fatigueTerm = 1.f },
            .maximumEncumbranceWeightUnits = 1,
            .victim = OpenMwMeleeVictim{ .health = 10.f },
            .respawnVictim = OpenMwMeleeVictim{ .health = 10.f },
            .maximumHealth = 10.f,
            .maximumFatigue = 10.f,
            .securitySkill = security };
        for (auto& rule : combatPlayer.skillRules)
            rule.useGain = 0.5f;
        for (auto& progression : combatPlayer.skillProgression)
            progression.requirementFactor = 1.f;
        const auto random = Xoshiro256StarStar::fromWorldSeed(42, *RandomStreamKey::fromValues(3, 4)).snapshot();
        const std::array combatPlayers{ combatPlayer };
        auto combat = std::get<CanonicalCombatWorld>(
            createCanonicalCombatWorld(combatPlayers, std::span<const CanonicalActorCombatState>{}, random));

        const std::array playerEntities{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(2),
            id<AppearanceId>(1), Transform(cell, Position3(0, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()) };
        const std::array sessions{ CanonicalSessionProgress(id<SessionId>(1), SessionGeneration::initial(),
            id<PlayerId>(1), id<EntityId>(2), std::nullopt) };
        auto players = std::get<CanonicalServerState>(createCanonicalServerState(playerEntities, sessions));
        return { std::move(manifest), std::move(objectCatalog), std::move(objects), std::move(itemCatalog),
            std::move(inventory), std::move(combat), std::move(players) };
    }

    InteractObjectCommand command(const Fixture& value, ObjectInteractionKind kind, ItemStackId tool)
    {
        return { id<PlayerId>(1), id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(7)),
            Position3(0, 0, 0), value.objects.find(id<InteractiveObjectId>(1))->revision(), kind, std::nullopt,
            tool, value.inventory.findPlayer(id<PlayerId>(1))->revision,
            value.combat.findPlayer(id<PlayerId>(1))->revision };
    }

    void successful_attempt_commits_roll_wear_progress_and_object_as_one_candidate()
    {
        auto value = fixture(99.f);
        const auto beforeRandom = value.combat.randomState();
        auto prepared = prepareAuthoritativeSecurityAttempt(value.objects, value.objectCatalog, value.inventory,
            value.itemCatalog, value.combat, value.players,
            command(value, ObjectInteractionKind::PickLock, id<ItemStackId>(20)), OpenMwSecuritySettings{},
            id<ServerTick>(1));
        assert(prepared.succeeded() && prepared.objects && prepared.inventory && prepared.combat);
        assert(value.objects.find(id<InteractiveObjectId>(1))->lockState() == LockState::Locked);
        assert(value.inventory.findPlayer(id<PlayerId>(1))->findStack(id<ItemStackId>(20))->condition == 2);
        assert(value.combat.findPlayer(id<PlayerId>(1))->revision == CombatRevision::initial());
        assert(prepared.objects->find(id<InteractiveObjectId>(1))->lockState() == LockState::Unlocked);
        assert(prepared.objects->find(id<InteractiveObjectId>(1))->revision() == id<ObjectRevision>(2));
        assert(prepared.inventory->findPlayer(id<PlayerId>(1))->revision == id<InventoryRevision>(2));
        assert(prepared.inventory->findPlayer(id<PlayerId>(1))->findStack(id<ItemStackId>(20))->condition == 1);
        const auto* progressed = prepared.combat->findPlayer(id<PlayerId>(1));
        assert(progressed->revision == id<CombatRevision>(2));
        assert(progressed->skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Security)].progress
            == 0.005f);
        assert(prepared.combat->randomState() != beforeRandom);
    }

    void failed_roll_still_commits_rng_and_wear_without_object_or_skill_mutation()
    {
        auto value = fixture(1.f);
        const auto beforeRandom = value.combat.randomState();
        const auto beforeObject = *value.objects.find(id<InteractiveObjectId>(1));
        auto settings = OpenMwSecuritySettings{};
        auto prepared = prepareAuthoritativeSecurityAttempt(value.objects, value.objectCatalog, value.inventory,
            value.itemCatalog, value.combat, value.players,
            command(value, ObjectInteractionKind::PickLock, id<ItemStackId>(20)), settings, id<ServerTick>(1));
        assert(prepared.applied() && !prepared.succeeded() && prepared.objects && prepared.inventory && prepared.combat);
        assert(*prepared.objects->find(id<InteractiveObjectId>(1)) == beforeObject);
        assert(prepared.inventory->findPlayer(id<PlayerId>(1))->findStack(id<ItemStackId>(20))->condition == 1);
        const auto* progressed = prepared.combat->findPlayer(id<PlayerId>(1));
        assert(progressed->revision == id<CombatRevision>(2));
        assert(progressed->skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Security)].progress == 0.f);
        assert(prepared.combat->randomState() != beforeRandom);
    }

    void stale_precondition_rejects_without_any_candidate()
    {
        auto value = fixture(99.f);
        auto attempt = command(value, ObjectInteractionKind::DisarmTrap, id<ItemStackId>(21));
        attempt.expectedInventoryRevision = id<InventoryRevision>(2);
        const auto prepared = prepareAuthoritativeSecurityAttempt(value.objects, value.objectCatalog, value.inventory,
            value.itemCatalog, value.combat, value.players, attempt, OpenMwSecuritySettings{}, id<ServerTick>(1));
        assert(prepared.disposition == AuthoritativeSecurityDisposition::StaleInventoryRevision);
        assert(!prepared.objects && !prepared.inventory && !prepared.combat);
    }
}

int main()
{
    successful_attempt_commits_roll_wear_progress_and_object_as_one_candidate();
    failed_roll_still_commits_rng_and_wear_without_object_or_skill_mutation();
    stale_precondition_rejects_without_any_candidate();
}
