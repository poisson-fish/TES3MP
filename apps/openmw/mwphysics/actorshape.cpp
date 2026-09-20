#include "actorshape.hpp"

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <components/misc/convert.hpp>

namespace MWPhysics
{
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
