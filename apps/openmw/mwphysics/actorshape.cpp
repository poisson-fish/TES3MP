#include "actorshape.hpp"

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <components/misc/convert.hpp>

namespace MWPhysics
{
    Resource::CollisionBox actorCollisionBox(const Resource::BulletShape& shape, bool npc)
    {
        auto box = shape.mCollisionBox;
        if (!npc && box.mExtents.length2() == 0.f && shape.mCollisionShape)
        {
            btVector3 minimum, maximum;
            shape.mCollisionShape->getAabb(btTransform::getIdentity(), minimum, maximum);
            box.mExtents = Misc::Convert::toOsg((maximum - minimum) / 2);
            box.mCenter = osg::Vec3f(0, 0, box.mExtents.z());
        }
        return box;
    }

    ActorShape makeActorShape(const osg::Vec3f& halfExtents, const osg::Vec3f& translation,
        DetourNavigator::CollisionShapeType requested)
    {
        const float ratio = halfExtents.y() != 0.f ? halfExtents.x() / halfExtents.y() : 0.f;
        if (!(translation.x() == 0.f && translation.y() == 0.f && ratio >= 1.f / 1.1f && ratio <= 1.1f))
            requested = DetourNavigator::CollisionShapeType::RotatingBox;
        ActorShape result{ nullptr, requested, requested != DetourNavigator::CollisionShapeType::RotatingBox };
        if (requested == DetourNavigator::CollisionShapeType::Cylinder)
            result.mShape = std::make_unique<btCylinderShapeZ>(Misc::Convert::toBullet(halfExtents));
        else
            result.mShape = std::make_unique<btBoxShape>(Misc::Convert::toBullet(halfExtents));
        result.mShape->setMargin(0.001);
        return result;
    }
}
