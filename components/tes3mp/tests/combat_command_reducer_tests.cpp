#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <array>
#include <variant>

namespace
{
    using namespace TES3MP;
    template <class T> T id(std::uint64_t value) { return *T::fromValue(value); }

    Transform root(std::int64_t x)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(7)), Position3(x, 0, 0),
            Orientation3(zero, zero, zero));
    }

    struct Contact final : ServerMeleeContactQuery
    {
        MeleeContactValidation validate(const ServerMeleeContactRequest&, const CanonicalPlayerEntityState&,
            const CanonicalActorEntityState&) noexcept override
        {
            return MeleeContactValidation::Accepted;
        }
    };

    bool combat_commit_is_atomic_with_command_finalization()
    {
        const auto playerId = id<PlayerId>(1);
        const auto entityId = id<EntityId>(10);
        const auto sessionId = id<SessionId>(20);
        const std::array players{ CanonicalPlayerEntityState(playerId, entityId, id<AppearanceId>(1), root(0),
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()) };
        const std::array sessions{ CanonicalSessionProgress(sessionId, SessionGeneration::initial(), playerId,
            entityId, std::nullopt) };
        auto canonical = std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
        const std::array spatialActors{ CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(30),
            id<ActorPrototypeId>(3), root(10), LinearVelocity3(0, 0, 0), EntityRevision::initial(),
            AuthorityEpoch::initial(), ServerTick::initial(), ActorActivity::Idle, 0) };
        auto actorWorld = std::get<CanonicalActorWorld>(createCanonicalActorWorld(spatialActors));

        OpenMwMeleeAttacker attacker;
        attacker.weaponSkill = 100.f;
        attacker.fatigueTerm = 1.f;
        attacker.fatigue = 100.f;
        std::array<float, static_cast<std::size_t>(MeleeWeaponSkill::Count)> weaponSkills{};
        weaponSkills.fill(100.f);
        const std::array combatPlayers{ CanonicalPlayerCombatState{
            playerId, CombatRevision::initial(), attacker, weaponSkills, 100, std::nullopt } };
        OpenMwMeleeVictim victim;
        victim.health = 20.f;
        const std::array combatActors{ CanonicalActorCombatState{
            id<ActorId>(2), CombatRevision::initial(), victim } };
        const auto key = *RandomStreamKey::fromValues(1, 2);
        auto combat = std::get<CanonicalCombatWorld>(createCanonicalCombatWorld(combatPlayers, combatActors,
            Xoshiro256StarStar::fromWorldSeed(3, key).snapshot()));
        const std::array itemDeclarations{ ItemPrototypeDeclaration{ id<ItemPrototypeId>(4), ItemCategory::Weapon,
            5, 1, 100, 0, slotToMask(EquipmentSlot::CarriedRight), false, std::nullopt } };
        auto itemCatalog = *ItemPrototypeCatalog::create(testContentManifest(), itemDeclarations);
        CanonicalPlayerInventoryState playerInventory{ .player = playerId,
            .stacks = { { id<ItemStackId>(5), id<ItemPrototypeId>(4), 1, 100, 0, std::nullopt } } };
        playerInventory.equipment[static_cast<std::size_t>(EquipmentSlot::CarriedRight)] = id<ItemStackId>(5);
        auto inventory = *CanonicalInventoryWorld::create(
            testContentManifest(), itemCatalog, std::array{ playerInventory }, {});
        const std::array weaponProfiles{ MeleeWeaponProfile{ id<ItemPrototypeId>(4), MeleeWeaponSkill::LongBlade,
            10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 5.f, 1.f, true } };
        auto weapons = *MeleeWeaponCatalog::create(itemCatalog, weaponProfiles);

        TestSupport::ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(1),
            IngressOrdinal::initial());
        CanonicalCommandReducer reducer(std::move(canonical), observability);
        ClientMeleeAttackCommand wire{ sessionId, SessionGeneration::initial(), CommandSequence::initial(),
            id<CommandId>(1), CanonicalRevision::initial(), id<ActorId>(2), id<ServerTick>(1),
            CombatRevision::initial(), CombatRevision::initial(), MeleeAttackType::Chop, 1.f };
        ServerCommandProposal proposal(sessionId, SessionGeneration::initial(), CommandSequence::initial(),
            id<CommandId>(1), CanonicalRevision::initial(),
            EntityPrecondition(entityId, EntityRevision::initial(), AuthorityEpoch::initial()),
            MeleeAttackCommandProposal(wire));
        if (intake.submit(std::move(proposal)) != CommandSubmissionResult::Accepted)
            return false;
        clock.advance(33'333'334);
        const auto pumped = intake.pump();
        if (!pumped || pumped.batches().empty())
            return false;
        OpenMwMeleeSettings settings;
        settings.damageStrengthBase = 1.f;
        settings.fatigueAttackBase = 5.f;
        const MeleeAuthorityPolicy policy{ 1, 8 };
        Contact contact;
        CanonicalCommandWorlds worlds{ .inventory = &inventory, .itemCatalog = &itemCatalog,
            .combat = &combat, .actors = &actorWorld, .meleeWeapons = &weapons,
            .meleeSettings = &settings, .meleePolicy = &policy, .meleeContact = &contact };
        auto prepared = reducer.prepareTick(pumped.batches().front(), worlds);
        const auto* candidatePlayer = prepared.candidateCombat()
            ? prepared.candidateCombat()->findPlayer(playerId) : nullptr;
        const auto* candidateActor = prepared.candidateCombat()
            ? prepared.candidateCombat()->findActor(id<ActorId>(2)) : nullptr;
        if (!prepared.result() || prepared.result().dispositions().size() != 1
            || prepared.result().dispositions()[0].disposition() != CommandDisposition::Applied
            || prepared.combatEvents().size() != 1 || !candidatePlayer || !candidateActor
            || candidatePlayer->stats.fatigue != 95.f || candidateActor->stats.health != 10.f
            || combat.findActor(id<ActorId>(2))->stats.health != 20.f)
            return false;
        if (!reducer.commit(std::move(prepared), worlds))
            return false;
        const auto* progress = reducer.state().findActiveSession(sessionId);
        if (!progress || progress->highestContiguousFinalizedCommand() != CommandSequence::initial()
            || combat.findActor(id<ActorId>(2))->stats.health != 10.f
            || inventory.findPlayer(playerId)->findStack(id<ItemStackId>(5))->condition != 99)
            return false;

        const InventoryTransactionCommand unequip{ .player = playerId,
            .kind = InventoryTransactionKind::UnequipItem,
            .prototypeId = id<ItemPrototypeId>(4),
            .slot = EquipmentSlot::CarriedRight,
            .expectedInventoryRevision = id<InventoryRevision>(2),
            .interactionOrigin = Position3(0, 0, 0) };
        ServerCommandProposal inventoryProposal(sessionId, SessionGeneration::initial(), id<CommandSequence>(2),
            id<CommandId>(2), reducer.canonicalRevision(),
            EntityPrecondition(entityId, EntityRevision::initial(), AuthorityEpoch::initial()),
            InventoryCommandProposal(unequip));
        if (intake.submit(std::move(inventoryProposal)) != CommandSubmissionResult::Accepted)
            return false;
        clock.advance(33'333'334);
        const auto next = intake.pump();
        if (!next || next.batches().empty())
            return false;
        auto unequipPrepared = reducer.prepareTick(next.batches().front(), worlds);
        const auto* combatCandidate = unequipPrepared.candidateCombat()
            ? unequipPrepared.candidateCombat()->findPlayer(playerId) : nullptr;
        const auto* inventoryCandidate = unequipPrepared.candidateInventory()
            ? unequipPrepared.candidateInventory()->findPlayer(playerId) : nullptr;
        if (!unequipPrepared.result() || !combatCandidate || !inventoryCandidate
            || combatCandidate->revision != id<CombatRevision>(3)
            || inventoryCandidate->equipment[static_cast<std::size_t>(EquipmentSlot::CarriedRight)])
            return false;
        return reducer.commit(std::move(unequipPrepared), worlds)
            && combat.findPlayer(playerId)->revision == id<CombatRevision>(3)
            && !inventory.findPlayer(playerId)->equipment[static_cast<std::size_t>(EquipmentSlot::CarriedRight)];
    }
}

int main()
{
    return combat_commit_is_atomic_with_command_finalization() ? 0 : 1;
}
