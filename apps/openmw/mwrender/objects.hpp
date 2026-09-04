#ifndef GAME_RENDER_OBJECTS_H
#define GAME_RENDER_OBJECTS_H

#include <map>
#include <string>
#include <utility>

#include <osg/Object>
#include <osg/ref_ptr>

#include "../mwworld/ptr.hpp"
#include "replicatedactor.hpp"

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class CellStore;
}

namespace SceneUtil
{
    class UnrefQueue;
}

namespace MWRender
{

    class Animation;

    class PtrHolder : public osg::Object
    {
    public:
        PtrHolder(const MWWorld::Ptr& ptr)
            : mPtr(ptr)
        {
        }

        PtrHolder() {}

        PtrHolder(const PtrHolder& copy, const osg::CopyOp& copyop)
            : mPtr(copy.mPtr)
        {
        }

        META_Object(MWRender, PtrHolder)

        MWWorld::Ptr mPtr;
    };

    class Objects
    {
        using PtrAnimationMap = std::map<const MWWorld::LiveCellRefBase*, osg::ref_ptr<Animation>>;

        typedef std::map<const MWWorld::CellStore*, osg::ref_ptr<osg::Group>> CellMap;
        CellMap mCellSceneNodes;
        PtrAnimationMap mObjects;
        PtrAnimationMap mReplicatedActors;
        osg::ref_ptr<osg::Group> mRootNode;
        Resource::ResourceSystem* mResourceSystem;
        SceneUtil::UnrefQueue& mUnrefQueue;

        void insertBegin(const MWWorld::Ptr& ptr, bool interactive = true);

    public:
        Objects(Resource::ResourceSystem* resourceSystem, const osg::ref_ptr<osg::Group>& rootNode,
            SceneUtil::UnrefQueue& unrefQueue);
        ~Objects();

        /// @param allowLight If false, no lights will be created, and particles systems will be removed.
        void insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool allowLight = true);

        void insertNPC(const MWWorld::Ptr& ptr);
        void insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool weaponsShields);

        ReplicatedActorResult insertReplicatedActor(const MWWorld::Ptr& ptr, const MWWorld::ESMStore& store);
        ReplicatedActorResult advanceReplicatedActor(
            const MWWorld::Ptr& ptr, const ESM::Position& position, float animationSeconds) noexcept;
        bool removeReplicatedActor(const MWWorld::Ptr& ptr) noexcept;
        std::size_t replicatedActorCount() const noexcept { return mReplicatedActors.size(); }

        Animation* getAnimation(const MWWorld::Ptr& ptr);
        const Animation* getAnimation(const MWWorld::ConstPtr& ptr) const;

        bool removeObject(const MWWorld::Ptr& ptr);
        ///< \return found?

        void removeCell(const MWWorld::CellStore* store);

        /// Updates containing cell for object rendering data
        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur);

    private:
        void operator=(const Objects&);
        Objects(const Objects&);
    };
}
#endif
