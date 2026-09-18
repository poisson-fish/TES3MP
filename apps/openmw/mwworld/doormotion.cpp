#include "doormotion.hpp"

#include <osg/Math>
#include <osg/Quat>

#include <algorithm>

namespace MWWorld
{
    DoorState activatedDoorState(DoorState state, float closedAngle, float currentAngle)
    {
        switch (state)
        {
            case DoorState::Idle:
                return currentAngle == closedAngle ? DoorState::Opening : DoorState::Closing;
            case DoorState::Closing:
                return DoorState::Opening;
            case DoorState::Opening:
            default:
                return DoorState::Closing;
        }
    }

    float doorSoundOffset(DoorState movement, float closedAngle, float currentAngle)
    {
        const float progress = (currentAngle - closedAngle) / (osg::PIf * 0.5f);
        return movement == DoorState::Opening ? progress : std::max(1.0f - progress, 0.0f);
    }

    DoorMotion doorMotion(DoorState state, float closedAngle, float currentAngle, float duration)
    {
        const float maxRot = closedAngle + osg::DegreesToRadians(90.f);
        const float delta = duration * osg::DegreesToRadians(90.f) * (state == DoorState::Opening ? 1 : -1);
        const float target = std::clamp(currentAngle + delta, closedAngle, maxRot);
        return { delta, target, (target == maxRot && state != DoorState::Idle) || target == closedAngle };
    }

    bool doorContactBlocks(float delta, const osg::Vec3f& doorPosition,
        const osg::Vec3f& contactPoint, const osg::Vec3f& contactNormal)
    {
        const auto localPoint = doorPosition - contactPoint;
        osg::Vec3f direction = osg::Quat(delta, osg::Vec3f(0, 0, 1)) * localPoint - localPoint;
        direction.normalize();
        return !(direction * contactNormal < 0);
    }
}
