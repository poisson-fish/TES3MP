#include <BulletCollision/CollisionDispatch/btCollisionObject.h>

#include "collisiontype.hpp"
#include "projectileconvexcallback.hpp"

namespace MWPhysics
{
    btScalar ProjectileConvexCallback::addSingleResult(
        btCollisionWorld::LocalConvexResult& result, bool normalInWorldSpace)
    {
        const auto* hitObject = result.m_hitCollisionObject;
        // don't hit the caster
        if (hitObject == mCaster)
            return 1.f;

        // don't hit the projectile
        if (hitObject == mMe)
            return 1.f;

        btCollisionWorld::ClosestConvexResultCallback::addSingleResult(result, normalInWorldSpace);
        switch (hitObject->getBroadphaseHandle()->m_collisionFilterGroup)
        {
            case CollisionType_Actor:
            {
                if (!mEffects.validProjectileTarget(mMe, hitObject))
                    return 1.f;
                break;
            }
            case CollisionType_Projectile:
            {
                if (!mEffects.validProjectileTarget(mMe, mEffects.projectileCaster(hitObject)))
                    return 1.f;
                mEffects.hit(hitObject, mMe, m_hitPointWorld, m_hitNormalWorld);
                break;
            }
            case CollisionType_Water:
            {
                mEffects.hitWater(mMe);
                break;
            }
        }
        mEffects.hit(mMe, hitObject, m_hitPointWorld, m_hitNormalWorld);

        return result.m_hitFraction;
    }

}
