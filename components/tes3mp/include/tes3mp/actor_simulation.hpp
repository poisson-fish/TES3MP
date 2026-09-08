#ifndef TES3MP_ACTOR_SIMULATION_HPP
#define TES3MP_ACTOR_SIMULATION_HPP

#include "actor_catalog.hpp"
#include "canonical_state.hpp"
#include "movement_kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    class CanonicalCombatWorld;
    class CanonicalActorEntityState
    {
    public:
        constexpr CanonicalActorEntityState(ActorId actorId, EntityId entityId, ActorPrototypeId prototypeId,
            Transform root, LinearVelocity3 velocity, EntityRevision revision, AuthorityEpoch authorityEpoch,
            ServerTick lastChangeTick, ActorActivity activity, std::uint16_t waypointIndex) noexcept
            : mActorId(actorId), mEntityId(entityId), mPrototypeId(prototypeId), mRoot(root), mVelocity(velocity),
              mRevision(revision), mAuthorityEpoch(authorityEpoch), mLastChangeTick(lastChangeTick),
              mActivity(activity), mWaypointIndex(waypointIndex) {}

        constexpr ActorId actorId() const noexcept { return mActorId; }
        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr ActorPrototypeId prototypeId() const noexcept { return mPrototypeId; }
        constexpr const Transform& root() const noexcept { return mRoot; }
        constexpr LinearVelocity3 velocity() const noexcept { return mVelocity; }
        constexpr EntityRevision revision() const noexcept { return mRevision; }
        constexpr AuthorityEpoch authorityEpoch() const noexcept { return mAuthorityEpoch; }
        constexpr ServerTick lastChangeTick() const noexcept { return mLastChangeTick; }
        constexpr ActorActivity activity() const noexcept { return mActivity; }
        constexpr std::uint16_t waypointIndex() const noexcept { return mWaypointIndex; }

        friend constexpr bool operator==(
            const CanonicalActorEntityState&, const CanonicalActorEntityState&) noexcept = default;

    private:
        ActorId mActorId;
        EntityId mEntityId;
        ActorPrototypeId mPrototypeId;
        Transform mRoot;
        LinearVelocity3 mVelocity;
        EntityRevision mRevision;
        AuthorityEpoch mAuthorityEpoch;
        ServerTick mLastChangeTick;
        ActorActivity mActivity;
        std::uint16_t mWaypointIndex;
    };

    enum class CanonicalActorWorldErrorCode : std::uint8_t
    {
        LimitExceeded,
        ActorIdsNotStrictlyOrdered,
        DuplicateEntityId,
        AllocationFailure,
    };

    struct CanonicalActorWorldError
    {
        CanonicalActorWorldErrorCode code;
        std::size_t index = 0;
        std::uint64_t value = 0;
        std::uint64_t relatedValue = 0;

        friend constexpr bool operator==(CanonicalActorWorldError, CanonicalActorWorldError) noexcept = default;
    };

    class CanonicalActorWorld
    {
    public:
        std::span<const CanonicalActorEntityState> actors() const noexcept { return mActors; }
        const CanonicalActorEntityState* find(ActorId actorId) const noexcept;

        friend bool operator==(const CanonicalActorWorld&, const CanonicalActorWorld&) noexcept = default;

    private:
        friend std::variant<CanonicalActorWorld, CanonicalActorWorldError> createCanonicalActorWorld(
            std::span<const CanonicalActorEntityState>);

        explicit CanonicalActorWorld(std::vector<CanonicalActorEntityState> actors) noexcept
            : mActors(std::move(actors)) {}

        std::vector<CanonicalActorEntityState> mActors;
    };

    using CanonicalActorWorldResult = std::variant<CanonicalActorWorld, CanonicalActorWorldError>;

    CanonicalActorWorldResult createCanonicalActorWorld(std::span<const CanonicalActorEntityState> actors);
    CanonicalActorWorldResult createInitialCanonicalActorWorld(const ActorCatalog& catalog);
    bool actorAndPlayerEntityIdsAreDisjoint(
        const CanonicalActorWorld& actors, const CanonicalServerState& players) noexcept;

    enum class ActorSimulationErrorCode : std::uint8_t
    {
        CatalogMismatch,
        TickRegression,
        RevisionExhausted,
        MovementRejected,
        InvalidResult,
    };

    struct ActorSimulationError
    {
        ActorSimulationErrorCode code;
        std::size_t actorIndex = 0;
        std::optional<MovementKernelError> movementError;

        friend constexpr bool operator==(ActorSimulationError, ActorSimulationError) noexcept = default;
    };

    using ActorSimulationResult = std::variant<CanonicalActorWorld, ActorSimulationError>;

    ActorSimulationResult advanceActorSimulation(const CanonicalActorWorld& current, const ActorCatalog& catalog,
        const CanonicalServerState& players, ServerTick tick, MovementProfile movementProfile,
        ServerCollisionQuery& collision);
    ActorSimulationResult advanceActorSimulation(const CanonicalActorWorld& current, const ActorCatalog& catalog,
        const CanonicalServerState& players, const CanonicalCombatWorld& combat, ServerTick tick,
        MovementProfile movementProfile, ServerCollisionQuery& collision);
}

#endif
