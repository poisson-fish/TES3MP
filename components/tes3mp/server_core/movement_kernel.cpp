#include <tes3mp/movement_kernel.hpp>

#include <limits>

namespace TES3MP
{
    namespace
    {
        std::optional<std::int64_t> checkedAdd(std::int64_t left, std::int64_t right) noexcept
        {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
                return std::nullopt;
            return left + right;
        }
    }

    std::optional<ServerCollisionResult> UnobstructedServerCollisionQuery::resolve(
        const ServerCollisionRequest& request) noexcept
    {
        return ServerCollisionResult{ request.attemptedPosition, request.attemptedVelocity };
    }

    MovementKernelResult advanceMovementKernel(ContentManifestId contentManifest, MovementProfile profile,
        LocomotionMode mode, EntityId entity, ServerTick tick, const Transform& currentRoot,
        LinearVelocity3 desiredVelocity, ServerCollisionQuery& collision) noexcept
    {
        if (!profile.allows(mode, desiredVelocity))
            return MovementKernelError::MotionOutOfRange;
        const auto current = currentRoot.position();
        const auto x = checkedAdd(current.x(), desiredVelocity.x());
        const auto y = checkedAdd(current.y(), desiredVelocity.y());
        const auto z = checkedAdd(current.z(), desiredVelocity.z());
        if (!x || !y || !z)
            return MovementKernelError::IntegrationOverflow;
        const ServerCollisionRequest request{ contentManifest, entity, tick, currentRoot,
            Position3(*x, *y, *z), desiredVelocity, mode };
        const auto resolved = collision.resolve(request);
        if (!resolved)
            return MovementKernelError::CollisionUnavailable;
        if (!profile.allows(mode, resolved->velocity))
            return MovementKernelError::CollisionResultOutOfRange;
        return MovementKernelStep{ Transform(currentRoot.cell(), resolved->position, currentRoot.orientation()),
            resolved->velocity };
    }
}
