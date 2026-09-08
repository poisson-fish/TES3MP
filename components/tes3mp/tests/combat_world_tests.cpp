#include <tes3mp/combat_world.hpp>

#include <array>
#include <cmath>
#include <limits>

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
        attacker.weaponSkill = 100.f;
        attacker.agility = 100.f;
        attacker.luck = 100.f;
        attacker.strength = 50.f;
        attacker.fatigueTerm = 1.f;
        attacker.fatigue = 100.f;
        TES3MP::OpenMwMeleeWeapon weapon;
        weapon.chopMinimum = 10.f;
        weapon.chopMaximum = 10.f;
        weapon.slashMinimum = 10.f;
        weapon.slashMaximum = 10.f;
        weapon.thrustMinimum = 10.f;
        weapon.thrustMaximum = 10.f;
        const std::array players{ TES3MP::CanonicalPlayerCombatState{
            id<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), attacker, weapon, std::nullopt } };
        TES3MP::OpenMwMeleeVictim victim;
        victim.health = health;
        victim.fatigue = 50.f;
        const std::array actors{ TES3MP::CanonicalActorCombatState{
            id<TES3MP::ActorId>(2), TES3MP::CombatRevision::initial(), victim } };
        const auto key = *TES3MP::RandomStreamKey::fromValues(1, 2);
        const auto random = TES3MP::Xoshiro256StarStar::fromWorldSeed(3, key).snapshot();
        return std::get<TES3MP::CanonicalCombatWorld>(TES3MP::createCanonicalCombatWorld(players, actors, random));
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
        TES3MP::MeleeContactValidation validate(const TES3MP::ServerMeleeContactRequest&,
            const TES3MP::CanonicalPlayerEntityState&, const TES3MP::CanonicalActorEntityState&) noexcept override
        {
            ++calls;
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
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
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

    bool lethal_hit_marks_death_in_same_candidate()
    {
        Contact contact;
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(combatWorld(5.f), spatialPlayers(),
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
        const auto staleResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), stale);
        auto future = attack(6);
        const auto futureResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), future);
        const auto cellResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(8), spatialActors(7),
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
        const auto missing = TES3MP::prepareAuthoritativeMeleeAttack(combatWorld(), spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        contact.result = TES3MP::MeleeContactValidation::Accepted;
        auto first = TES3MP::prepareAuthoritativeMeleeAttack(combatWorld(), spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(5), attack());
        if (!first.candidate)
            return false;
        auto secondAttack = attack(6);
        secondAttack.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        secondAttack.expectedTargetRevision = id<TES3MP::CombatRevision>(2);
        const auto second = TES3MP::prepareAuthoritativeMeleeAttack(*first.candidate, spatialPlayers(), spatialActors(),
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
        const auto forgedResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), forged);
        auto staleAttacker = attack();
        staleAttacker.expectedAttackerRevision = id<TES3MP::CombatRevision>(2);
        const auto staleResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), staleAttacker);
        auto tooOld = attack(1);
        const auto rewindResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
            settings(), { 2, 8 }, contact, id<TES3MP::ServerTick>(10), tooOld);
        contact.result = TES3MP::MeleeContactValidation::NoContact;
        const auto reachResult = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
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
        const auto prepared = TES3MP::prepareAuthoritativeMeleeAttack(before, spatialPlayers(), spatialActors(),
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
            && stale_spatial_and_timing_fail_without_contact_or_mutation()
            && contact_history_and_cooldown_are_authoritative()
            && forged_targets_impossible_contact_and_stale_intent_fail_atomically()
            && empty_swing_spends_fatigue_without_contact_or_randomness()
        ? 0
        : 1;
}
