#ifndef OPENMW_MWPHYSICS_COLLISIONEFFECTS_H
#define OPENMW_MWPHYSICS_COLLISIONEFFECTS_H

class btCollisionObject;
class btVector3;

namespace MWPhysics
{
    // Borrowed collision identities. The owner decides whether effects apply to
    // live objects or isolated tick state; the solver never interprets user data.
    class CollisionEffects
    {
    public:
        virtual ~CollisionEffects() = default;
        virtual void objectCollision(const btCollisionObject* object, bool player) = 0;
        virtual bool projectileActive(const btCollisionObject* projectile) const = 0;
        virtual bool validProjectileTarget(const btCollisionObject* projectile, const btCollisionObject* target) const = 0;
        virtual const btCollisionObject* projectileCaster(const btCollisionObject* projectile) const = 0;
        virtual void hit(const btCollisionObject* projectile, const btCollisionObject* target,
            const btVector3& position, const btVector3& normal) = 0;
        virtual void hitWater(const btCollisionObject* projectile) = 0;
    };

    // Stock single-player semantics, including existing thread synchronization.
    CollisionEffects& stockCollisionEffects();
}

#endif
