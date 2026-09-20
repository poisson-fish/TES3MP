#ifndef OPENMW_MWMECHANICS_DOORAVOIDANCE_H
#define OPENMW_MWMECHANICS_DOORAVOIDANCE_H

#include <components/misc/rng.hpp>
#include <osg/Vec3f>
#include <optional>

namespace MWMechanics
{
    // Simulation part of AiAvoidDoor. The caller owns actor/door identity,
    // steering, neighbor propagation and the random stream.
    struct DoorAvoidance
    {
        float mDuration = 1;
        osg::Vec3f mLastPos;
        int mDirection = 0;

        std::optional<float> update(const osg::Vec3f& actor, const osg::Vec3f& door,
            bool moving, float duration, Misc::Rng::Generator& prng)
        {
            if (mDuration == 1)
                mLastPos = actor;
            mDuration -= duration;
            if (mDuration < 0)
            {
                if ((actor - mLastPos).length2() < 10 * 10)
                {
                    mDirection = Misc::Rng::rollDice(4, prng);
                    mDuration = 1;
                }
                else
                    return {};
            }
            if (!moving)
                return {};
            return std::atan2(actor.x() - door.x(), actor.y() - door.y())
                + 2 * osg::PIf / 4 * mDirection;
        }
    };
}
#endif
