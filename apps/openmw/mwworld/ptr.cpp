#include "ptr.hpp"

#include "apps/openmw/mwbase/environment.hpp"

#include "worldmodel.hpp"

namespace MWWorld
{
    SafePtr::SafePtr(const Ptr& ptr)
        : mId(ptr.getCellRef().getRefNum())
        , mPtr(ptr)
        , mLastUpdate((MWBase::Environment::tryGet() && MWBase::Environment::tryGet()->getWorldModel())
                ? MWBase::Environment::tryGet()->getWorldModel()->getPtrRegistryRevision()
                : 0)
    {
    }

    std::string SafePtr::toString() const
    {
        update();
        if (mPtr.isEmpty())
            return "object" + mId.toString() + " (not found)";
        else
            return mPtr.toString();
    }

    void SafePtr::update() const
    {
        if (const MWBase::Environment* env = MWBase::Environment::tryGet())
        {
            if (const WorldModel* worldModel = env->getWorldModel())
            {
                if (mLastUpdate != worldModel->getPtrRegistryRevision())
                {
                    mPtr = worldModel->getPtr(mId);
                    mLastUpdate = worldModel->getPtrRegistryRevision();
                }
            }
        }
        if (!mPtr.isEmpty() && !mPtr.hasLiveReference())
            mPtr = Ptr();
    }
}
