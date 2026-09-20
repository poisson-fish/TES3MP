#ifndef OPENMW_MWPHYSICS_MOVEMENTDATA_H
#define OPENMW_MWPHYSICS_MOVEMENTDATA_H

#include <limits>

#include <osg/Vec2f>
#include <osg/Vec3f>

class btCollisionObject;

namespace MWPhysics
{
    class Projectile;

    // Explicit inputs/state for the shared movement solver. Engine context is
    // resolved by the caller before simulation, never by a physics worker.
    // Collision objects are borrowed for the duration of the simulation. These
    // are internal engine values, not a wire or persistence representation.
    struct ActorFrameData
    {
        osg::Vec3f mPosition;
        osg::Vec3f mInertia;
        const btCollisionObject* mStandingOn = nullptr;
        bool mIsOnGround = false;
        bool mIsOnSlope = false;
        bool mWalkingOnWater = false;
        const bool mInert = false;
        btCollisionObject* mCollisionObject = nullptr;
        const float mSwimLevel = -std::numeric_limits<float>::max();
        const float mSlowFall = 1.f;
        osg::Vec2f mRotation;
        osg::Vec3f mMovement;
        osg::Vec3f mLastStuckPosition;
        const float mWaterlevel = -std::numeric_limits<float>::max();
        const float mHalfExtentsZ = 0.f;
        float mOldHeight = 0.f;
        unsigned int mStuckFrames = 0;
        const bool mFlying = false;
        const bool mWasOnGround = false;
        const bool mIsAquatic = false;
        const bool mWaterCollision = false;
        const bool mSkipCollisionDetection = false;
        const bool mIsPlayer = false;
    };

    struct ProjectileFrameData
    {
        explicit ProjectileFrameData(Projectile& projectile);
        osg::Vec3f mPosition;
        osg::Vec3f mMovement;
        const btCollisionObject* mCaster;
        const btCollisionObject* mCollisionObject;
        Projectile* mProjectile;
    };

    struct WorldFrameData
    {
        bool mIsInStorm = false;
        osg::Vec3f mStormDirection;
        float mStormWalkMultiplier = 0.f;
    };
}

#endif
