#include "steering.hpp"

#include <components/misc/mathutil.hpp>
#include <components/settings/values.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwbase/environment.hpp"

#include "movement.hpp"

namespace MWMechanics
{

    bool smoothTurn(const MWWorld::Ptr& actor, float targetAngleRadians, int axis, float epsilonRadians)
    {
        MWMechanics::Movement& movement = actor.getClass().getMovementSettings(actor);
        const auto step = smoothTurnStep(actor.getRefData().getPosition().rot[axis], targetAngleRadians,
            actor.getClass().getMaxSpeed(actor), MWBase::Environment::get().getFrameDuration(),
            Settings::game().mSmoothMovement, epsilonRadians);
        if (step.mComplete)
            return true;
        movement.mRotation[axis] = step.mRotation;
        return false;
    }

    bool zTurn(const MWWorld::Ptr& actor, float targetAngleRadians, float epsilonRadians)
    {
        return smoothTurn(actor, targetAngleRadians, 2, epsilonRadians);
    }

}
