#ifndef TES3MP_COMBAT_WORLD_HPP
#define TES3MP_COMBAT_WORLD_HPP

#include "actor_simulation.hpp"
#include "canonical_state.hpp"
#include "deterministic_random.hpp"
#include "melee_combat.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumPlayerCombatants = 256;
    inline constexpr std::size_t MaximumActorCombatants = MaximumActorCatalogEntries;

    struct CanonicalPlayerCombatState
    {
        PlayerId playerId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeAttacker stats;
        std::optional<OpenMwMeleeWeapon> equippedWeapon;
        std::optional<ServerTick> lastAttackTick;

        friend constexpr bool operator==(const CanonicalPlayerCombatState&,
            const CanonicalPlayerCombatState&) noexcept = default;
    };

    struct CanonicalActorCombatState
    {
        ActorId actorId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeVictim stats;

        friend constexpr bool operator==(const CanonicalActorCombatState&,
            const CanonicalActorCombatState&) noexcept = default;
    };

    enum class CanonicalCombatWorldErrorCode : std::uint8_t
    {
        PlayerLimitExceeded,
        ActorLimitExceeded,
        PlayersNotStrictlySorted,
        ActorsNotStrictlySorted,
        InvalidStat,
    };

    struct CanonicalCombatWorldError
    {
        CanonicalCombatWorldErrorCode code;
        std::size_t index = 0;

        friend constexpr bool operator==(CanonicalCombatWorldError, CanonicalCombatWorldError) noexcept = default;
    };

    class CanonicalCombatWorld
    {
    public:
        std::span<const CanonicalPlayerCombatState> players() const noexcept { return mPlayers; }
        std::span<const CanonicalActorCombatState> actors() const noexcept { return mActors; }
        const CanonicalPlayerCombatState* findPlayer(PlayerId id) const noexcept;
        const CanonicalActorCombatState* findActor(ActorId id) const noexcept;
        RandomStateV1 randomState() const noexcept { return mRandomState; }

        friend bool operator==(const CanonicalCombatWorld&, const CanonicalCombatWorld&) noexcept = default;

    private:
        friend std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
            std::span<const CanonicalPlayerCombatState>, std::span<const CanonicalActorCombatState>, RandomStateV1);
        CanonicalCombatWorld(std::vector<CanonicalPlayerCombatState> players,
            std::vector<CanonicalActorCombatState> actors, RandomStateV1 randomState) noexcept
            : mPlayers(std::move(players)), mActors(std::move(actors)), mRandomState(randomState) {}

        std::vector<CanonicalPlayerCombatState> mPlayers;
        std::vector<CanonicalActorCombatState> mActors;
        RandomStateV1 mRandomState;
    };

    std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
        std::span<const CanonicalPlayerCombatState> players, std::span<const CanonicalActorCombatState> actors,
        RandomStateV1 randomState);

    enum class MeleeContactValidation : std::uint8_t
    {
        Accepted,
        NoContact,
        HistoryUnavailable,
    };

    struct ServerMeleeContactRequest
    {
        PlayerId attacker;
        ActorId target;
        ServerTick sourceTick;
        MeleeAttackType attackType;
        std::optional<OpenMwMeleeWeapon> weapon;
    };

    class ServerMeleeContactQuery
    {
    public:
        virtual ~ServerMeleeContactQuery() = default;
        virtual MeleeContactValidation validate(const ServerMeleeContactRequest& request,
            const CanonicalPlayerEntityState& attacker, const CanonicalActorEntityState& target) noexcept = 0;
    };

    struct MeleeAuthorityPolicy
    {
        std::uint64_t minimumAttackIntervalTicks = 1;
        std::uint64_t maximumRewindTicks = 8;
    };

    struct AuthoritativeMeleeAttack
    {
        PlayerId attacker;
        std::optional<ActorId> target;
        CombatRevision expectedAttackerRevision = CombatRevision::initial();
        CombatRevision expectedTargetRevision = CombatRevision::initial();
        ServerTick sourceTick = ServerTick::initial();
        MeleeAttackType attackType = MeleeAttackType::Chop;
        float attackStrength = 0.f;
        bool blocked = false;
        bool strengthInfluencesHandToHand = false;
        float werewolfClawMultiplier = 1.f;
    };

    enum class AuthoritativeMeleeDisposition : std::uint8_t
    {
        Applied,
        UnknownAttacker,
        UnknownTarget,
        StaleAttackerRevision,
        StaleTargetRevision,
        SpatialIdentityMismatch,
        DifferentCell,
        FutureSourceTick,
        RewindWindowExceeded,
        RateLimited,
        InvalidAttempt,
        NoContact,
        HistoryUnavailable,
        RevisionExhausted,
    };

    struct AuthoritativeMeleeEvent
    {
        ServerTick serverTick = ServerTick::initial();
        PlayerId attacker;
        ActorId target;
        CombatRevision attackerRevision = CombatRevision::initial();
        CombatRevision targetRevision = CombatRevision::initial();
        OpenMwMeleeResolution resolution;
    };

    struct PreparedMeleeAttack
    {
        AuthoritativeMeleeDisposition disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
        std::optional<CanonicalCombatWorld> candidate;
        std::optional<AuthoritativeMeleeEvent> event;
    };

    PreparedMeleeAttack prepareAuthoritativeMeleeAttack(const CanonicalCombatWorld& combat,
        const CanonicalServerState& players, const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings,
        MeleeAuthorityPolicy policy, ServerMeleeContactQuery& contact, ServerTick serverTick,
        const AuthoritativeMeleeAttack& attack) noexcept;
}

#endif
