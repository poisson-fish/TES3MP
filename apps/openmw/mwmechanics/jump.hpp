#ifndef OPENMW_MWMECHANICS_JUMP_H
#define OPENMW_MWMECHANICS_JUMP_H

#include <algorithm>
#include <osg/Vec3f>

namespace MWMechanics
{
    inline bool forceJumpRequested(bool forceJump, bool forceMoveJump, bool moving,
        bool grounded, bool swimming, bool flying)
    { return grounded && !swimming && !flying && (forceJump || (forceMoveJump && moving)); }

    // CharacterController's launch and air-control rules, after speed selection.
    inline osg::Vec3f jumpMovement(osg::Vec3f velocity, float jumpSpeed, bool requested,
        bool grounded, bool swimming, bool flying, bool sneaking, float airControl)
    {
        if (swimming || flying) return velocity;
        velocity.z() = 0.f;
        if (!grounded)
        {
            velocity.x() *= airControl;
            velocity.y() *= airControl;
        }
        else if (requested && !sneaking && jumpSpeed > 0.f)
        {
            if (velocity.x() == 0.f && velocity.y() == 0.f) velocity.z() = jumpSpeed;
            else
            {
                velocity.normalize();
                velocity = osg::Vec3f(velocity.x(), velocity.y(), 1.f) * jumpSpeed * .707f;
            }
        }
        return velocity;
    }

    inline float fallDamage(float height, float acrobatics, float jumpBonus, float minimum,
        float acroBase, float acroMult, float distanceBase, float distanceMult)
    {
        if (height < minimum) return 0.f;
        const float distance = std::max(0.f, height - minimum - 1.5f * acrobatics - jumpBonus);
        return (distanceBase + distanceMult * distance) * (acroBase + acroMult * (100.f - acrobatics));
    }
}
#endif
