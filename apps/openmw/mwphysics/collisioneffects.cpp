#include "collisioneffects.hpp"

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>

#include "object.hpp"
#include "projectile.hpp"

namespace MWPhysics
{
    namespace
    {
        Projectile& projectile(const btCollisionObject* object)
        {
            return *static_cast<Projectile*>(object->getUserPointer());
        }

        class StockCollisionEffects final : public CollisionEffects
        {
            void objectCollision(const btCollisionObject* object, bool player) override
            {
                auto* holder = static_cast<PtrHolder*>(object->getUserPointer());
                if (auto* hitObject = dynamic_cast<Object*>(holder))
                    hitObject->addCollision(player ? ScriptedCollisionType_Player : ScriptedCollisionType_Actor);
            }
            bool projectileActive(const btCollisionObject* object) const override { return projectile(object).isActive(); }
            bool validProjectileTarget(const btCollisionObject* object, const btCollisionObject* target) const override
            {
                return projectile(object).isValidTarget(target);
            }
            const btCollisionObject* projectileCaster(const btCollisionObject* object) const override
            {
                return projectile(object).getCasterCollisionObject();
            }
            void hit(const btCollisionObject* object, const btCollisionObject* target,
                const btVector3& position, const btVector3& normal) override
            {
                projectile(object).hit(target, position, normal);
            }
            void hitWater(const btCollisionObject* object) override { projectile(object).setHitWater(); }
        };
    }

    CollisionEffects& stockCollisionEffects()
    {
        static StockCollisionEffects effects;
        return effects;
    }
}
