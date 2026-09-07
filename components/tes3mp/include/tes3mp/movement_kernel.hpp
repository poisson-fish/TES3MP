#ifndef TES3MP_MOVEMENT_KERNEL_HPP
#define TES3MP_MOVEMENT_KERNEL_HPP

#include "content_identity.hpp"

#include <optional>
#include <variant>

namespace TES3MP
{
    struct ServerCollisionRequest
    {
        ContentManifestId contentManifest;
        EntityId entity;
        ServerTick tick;
        Transform currentRoot;
        Position3 attemptedPosition;
        LinearVelocity3 attemptedVelocity;
        LocomotionMode locomotionMode;
    };

    struct ServerCollisionResult
    {
        Position3 position;
        LinearVelocity3 velocity;

        friend constexpr bool operator==(ServerCollisionResult, ServerCollisionResult) noexcept = default;
    };

    class ServerCollisionQuery
    {
    public:
        virtual ~ServerCollisionQuery() = default;
        virtual std::optional<ServerCollisionResult> resolve(const ServerCollisionRequest& request) noexcept = 0;
    };

    // Version 1.2 compatibility adapter. Production content collision replaces
    // this at the same server-owned boundary without entering protocol or state.
    class UnobstructedServerCollisionQuery final : public ServerCollisionQuery
    {
    public:
        std::optional<ServerCollisionResult> resolve(const ServerCollisionRequest& request) noexcept override;
    };

    enum class MovementKernelError
    {
        MotionOutOfRange,
        IntegrationOverflow,
        CollisionUnavailable,
        CollisionResultOutOfRange,
    };

    struct MovementKernelStep
    {
        Transform root;
        LinearVelocity3 velocity;

        friend constexpr bool operator==(const MovementKernelStep&, const MovementKernelStep&) noexcept = default;
    };

    using MovementKernelResult = std::variant<MovementKernelStep, MovementKernelError>;

    MovementKernelResult advanceMovementKernel(ContentManifestId contentManifest, MovementProfile profile,
        LocomotionMode mode, EntityId entity, ServerTick tick, const Transform& currentRoot,
        LinearVelocity3 desiredVelocity, ServerCollisionQuery& collision) noexcept;
}

#endif
