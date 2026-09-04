#include "replicatedactor.hpp"

#include "actorutil.hpp"
#include "animation.hpp"
#include "npcanimation.hpp"
#include "objects.hpp"
#include "renderingmanager.hpp"
#include "vismask.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/attach.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/visitor.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>

namespace MWRender
{
    namespace
    {
        class BuildFailure final : public std::runtime_error
        {
        public:
            explicit BuildFailure(ReplicatedActorResult result)
                : std::runtime_error("replicated actor appearance build failed")
                , mResult(result)
            {
            }

            ReplicatedActorResult result() const noexcept { return mResult; }

        private:
            ReplicatedActorResult mResult;
        };

        int equipmentSlot(const ESM::Clothing& clothing)
        {
            switch (clothing.mData.mType)
            {
                case ESM::Clothing::Pants:
                    return 1;
                case ESM::Clothing::Shoes:
                    return 2;
                case ESM::Clothing::Shirt:
                    return 3;
                case ESM::Clothing::Belt:
                    return 4;
                case ESM::Clothing::Robe:
                    return 5;
                case ESM::Clothing::RGlove:
                    return 6;
                case ESM::Clothing::LGlove:
                    return 7;
                case ESM::Clothing::Skirt:
                    return 8;
                case ESM::Clothing::Ring:
                    return 9;
                case ESM::Clothing::Amulet:
                    return 10;
                default:
                    throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
            }
        }

        int equipmentSlot(const ESM::Armor& armor)
        {
            switch (armor.mData.mType)
            {
                case ESM::Armor::Helmet:
                    return 11;
                case ESM::Armor::Cuirass:
                    return 12;
                case ESM::Armor::LPauldron:
                    return 13;
                case ESM::Armor::RPauldron:
                    return 14;
                case ESM::Armor::Greaves:
                    return 15;
                case ESM::Armor::Boots:
                    return 16;
                case ESM::Armor::LGauntlet:
                case ESM::Armor::LBracer:
                    return 17;
                case ESM::Armor::RGauntlet:
                case ESM::Armor::RBracer:
                    return 18;
                case ESM::Armor::Shield:
                    return 19;
                default:
                    throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
            }
        }

        int clothingPriority(const ESM::Clothing& clothing)
        {
            if (clothing.mData.mType == ESM::Clothing::Robe)
                return 24;
            if (clothing.mData.mType == ESM::Clothing::Skirt)
                return 8;
            return 2;
        }

        struct AppearancePart
        {
            int priority = 0;
            std::optional<VFS::Path::Normalized> mesh;
        };

        class ReplicatedActorAnimation final : public Animation
        {
        public:
            ReplicatedActorAnimation(const MWWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode,
                Resource::ResourceSystem* resourceSystem, const MWWorld::ESMStore& store)
                : Animation(ptr, std::move(parentNode), resourceSystem, Context::ReplicatedActor)
                , mStaticControllerTime(std::make_shared<NullAnimationTime>())
            {
                const ESM::NPC* npc = mPtr.get<ESM::NPC>()->mBase;
                const ESM::Race* race = store.get<ESM::Race>().search(npc->mRace);
                if (race == nullptr)
                    throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);

                const bool female = !npc->isMale();
                const bool beast = (race->mData.mFlags & ESM::Race::Beast) != 0;
                const std::string defaultSkeleton = Misc::ResourceHelpers::correctActorModelPath(
                    VFS::Path::toNormalized(getActorSkeleton(false, female, beast, false)), mResourceSystem->getVFS());
                std::string skeleton = defaultSkeleton;
                bool customModel = false;
                if (!npc->mModel.empty())
                {
                    const VFS::Path::Normalized model
                        = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(npc->mModel));
                    customModel = !isDefaultActorSkeleton(model);
                    skeleton = Misc::ResourceHelpers::correctActorModelPath(model, mResourceSystem->getVFS());
                }
                requireResource(VFS::Path::Normalized(skeleton));

                std::array<AppearancePart, ESM::PRT_Count> parts;
                const auto& bodyParts = NpcAnimation::getBodyParts(npc->mRace, female, false, false);
                for (std::size_t index = ESM::PRT_Neck; index < ESM::PRT_Count; ++index)
                {
                    if (bodyParts[index] != nullptr)
                        assign(parts, static_cast<ESM::PartReferenceType>(index), 1, bodyParts[index]);
                }
                assignNamed(parts, ESM::PRT_Head, 1, npc->mHead, store);
                assignNamed(parts, ESM::PRT_Hair, 1, npc->mHair, store);
                applyEquipment(parts, *npc, female, store);

                for (const AppearancePart& part : parts)
                    if (part.mesh)
                        requireResource(*part.mesh);

                setObjectRoot(skeleton, true, true, false);
                for (std::size_t index = 0; index < parts.size(); ++index)
                    if (parts[index].mesh)
                        attachPart(static_cast<ESM::PartReferenceType>(index), *parts[index].mesh);

                const std::string_view base = Settings::models().mXbaseanim.get().value();
                if (!base.empty())
                    addAnimSource(base, skeleton);
                if (defaultSkeleton != base)
                    addAnimSource(defaultSkeleton, skeleton);
                if (customModel)
                    addAnimSource(skeleton, skeleton);

                setAccumulation(osg::Vec3f(0.f, 0.f, 0.f));
                mAnimationFallback = !hasAnimation("idle");
                if (!mAnimationFallback)
                    play("idle", 1, BlendMask_All, false, 1.f, "start", "stop", 0.f,
                        std::numeric_limits<std::uint32_t>::max(), true);
            }

            bool animationFallback() const noexcept { return mAnimationFallback; }

        private:
            void requireResource(VFS::Path::NormalizedView mesh) const
            {
                if (!mResourceSystem->getVFS()->exists(mesh))
                    throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
            }

            static void assign(std::array<AppearancePart, ESM::PRT_Count>& parts, ESM::PartReferenceType type,
                int priority, const ESM::BodyPart* bodyPart)
            {
                if (static_cast<std::size_t>(type) >= parts.size())
                    throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
                if (priority <= parts[type].priority)
                    return;
                parts[type].priority = priority;
                parts[type].mesh.reset();
                if (bodyPart != nullptr)
                {
                    if (bodyPart->mModel.empty())
                        throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
                    parts[type].mesh
                        = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(bodyPart->mModel));
                }
            }

            static void assignNamed(std::array<AppearancePart, ESM::PRT_Count>& parts,
                ESM::PartReferenceType type, int priority, const ESM::RefId& id, const MWWorld::ESMStore& store)
            {
                if (id.empty())
                    return;
                const ESM::BodyPart* bodyPart = store.get<ESM::BodyPart>().search(id);
                if (bodyPart == nullptr)
                    throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
                assign(parts, type, priority, bodyPart);
            }

            static void reserve(std::array<AppearancePart, ESM::PRT_Count>& parts, ESM::PartReferenceType type,
                int priority)
            {
                assign(parts, type, priority, nullptr);
            }

            static void applyPartReferences(std::array<AppearancePart, ESM::PRT_Count>& parts,
                const std::vector<ESM::PartReference>& references, bool female, int priority,
                const MWWorld::ESMStore& store)
            {
                for (const ESM::PartReference& reference : references)
                {
                    if (reference.mPart >= ESM::PRT_Count)
                        throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
                    const ESM::RefId& id = female && !reference.mFemale.empty() ? reference.mFemale : reference.mMale;
                    if (id.empty())
                    {
                        reserve(parts, static_cast<ESM::PartReferenceType>(reference.mPart), priority);
                        continue;
                    }
                    const ESM::BodyPart* bodyPart = store.get<ESM::BodyPart>().search(id);
                    if (bodyPart == nullptr)
                        throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
                    assign(parts, static_cast<ESM::PartReferenceType>(reference.mPart), priority, bodyPart);
                }
            }

            static void applyEquipment(std::array<AppearancePart, ESM::PRT_Count>& parts, const ESM::NPC& npc,
                bool female, const MWWorld::ESMStore& store)
            {
                std::set<int> occupiedSlots;
                for (const ESM::ContItem& item : npc.mInventory.mList)
                {
                    if (item.mCount <= 0)
                        continue;
                    const int type = store.find(item.mItem);
                    if (type == 0)
                        throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
                    if (type == ESM::Clothing::sRecordId)
                    {
                        const ESM::Clothing* clothing = store.get<ESM::Clothing>().search(item.mItem);
                        if (clothing == nullptr || !occupiedSlots.insert(equipmentSlot(*clothing)).second)
                            throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
                        const int priority = clothingPriority(*clothing);
                        applyPartReferences(parts, clothing->mParts.mParts, female, priority, store);
                        if (clothing->mData.mType == ESM::Clothing::Robe)
                        {
                            constexpr ESM::PartReferenceType covered[] = { ESM::PRT_Groin, ESM::PRT_Skirt,
                                ESM::PRT_RLeg, ESM::PRT_LLeg, ESM::PRT_RUpperarm, ESM::PRT_LUpperarm,
                                ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm, ESM::PRT_LForearm,
                                ESM::PRT_Cuirass };
                            for (const auto part : covered)
                                reserve(parts, part, priority);
                        }
                        else if (clothing->mData.mType == ESM::Clothing::Skirt)
                        {
                            reserve(parts, ESM::PRT_Groin, priority);
                            reserve(parts, ESM::PRT_RLeg, priority);
                            reserve(parts, ESM::PRT_LLeg, priority);
                        }
                    }
                    else if (type == ESM::Armor::sRecordId)
                    {
                        const ESM::Armor* armor = store.get<ESM::Armor>().search(item.mItem);
                        if (armor == nullptr || !occupiedSlots.insert(equipmentSlot(*armor)).second)
                            throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
                        applyPartReferences(parts, armor->mParts.mParts, female, 3, store);
                        if (armor->mData.mType == ESM::Armor::Helmet)
                            reserve(parts, ESM::PRT_Hair, 3);
                    }
                }
            }

            void attachPart(ESM::PartReferenceType type, VFS::Path::NormalizedView mesh)
            {
                const std::string_view boneName = NpcAnimation::getBodyPartBone(type);
                const NodeMap& nodes = getNodeMap();
                const auto found = nodes.find(boneName);
                if (found == nodes.end())
                    throw BuildFailure(ReplicatedActorResult::MissingAppearanceDependency);
                osg::ref_ptr<const osg::Node> templateNode = mResourceSystem->getSceneManager()->getTemplate(mesh);
                const std::string_view filter = type == ESM::PRT_Hair ? std::string_view("hair") : boneName;
                osg::ref_ptr<osg::Node> attached = SceneUtil::attach(
                    std::move(templateNode), mObjectRoot, filter, found->second, mResourceSystem->getSceneManager());
                if (attached->getNumChildrenRequiringUpdateTraversal() > 0)
                {
                    if (type == ESM::PRT_Head)
                    {
                        SceneUtil::ForceControllerSourcesVisitor visitor(mStaticControllerTime);
                        attached->accept(visitor);
                    }
                    else
                    {
                        SceneUtil::AssignControllerSourcesVisitor visitor(mAnimationTimePtr[0]);
                        attached->accept(visitor);
                    }
                }
            }

            std::shared_ptr<NullAnimationTime> mStaticControllerTime;
            bool mAnimationFallback = false;
        };
    }

    bool isValidReplicatedActorPose(const ESM::Position& position) noexcept
    {
        return std::ranges::all_of(position.pos, [](float value) { return std::isfinite(value); })
            && std::ranges::all_of(position.rot, [](float value) { return std::isfinite(value); });
    }

    ReplicatedActorResult Objects::insertReplicatedActor(const MWWorld::Ptr& ptr, const MWWorld::ESMStore& store)
    {
        const ReplicatedActorResult capacity = replicatedActorCapacityResult(mReplicatedActors.size());
        if (capacity != ReplicatedActorResult::Accepted)
            return capacity;
        if (mReplicatedActors.contains(ptr.mRef) || mObjects.contains(ptr.mRef))
            return ReplicatedActorResult::LifecycleViolation;

        insertBegin(ptr, false);
        ptr.getRefData().getBaseNode()->setNodeMask(Mask_ReplicatedActor);
        try
        {
            osg::ref_ptr<ReplicatedActorAnimation> animation(new ReplicatedActorAnimation(ptr,
                osg::ref_ptr<osg::Group>(ptr.getRefData().getBaseNode()), mResourceSystem, store));
            const bool fallback = animation->animationFallback();
            mReplicatedActors.emplace(ptr.mRef, std::move(animation));
            return fallback ? ReplicatedActorResult::AnimationFallback : ReplicatedActorResult::Accepted;
        }
        catch (const BuildFailure& failure)
        {
            if (ptr.getRefData().getBaseNode()->getNumParents() != 0)
                ptr.getRefData().getBaseNode()->getParent(0)->removeChild(ptr.getRefData().getBaseNode());
            ptr.getRefData().setBaseNode(nullptr);
            return failure.result();
        }
        catch (...)
        {
            if (ptr.getRefData().getBaseNode()->getNumParents() != 0)
                ptr.getRefData().getBaseNode()->getParent(0)->removeChild(ptr.getRefData().getBaseNode());
            ptr.getRefData().setBaseNode(nullptr);
            return ReplicatedActorResult::ResourceLoadFailed;
        }
    }

    ReplicatedActorResult Objects::advanceReplicatedActor(
        const MWWorld::Ptr& ptr, const ESM::Position& position, float animationSeconds) noexcept
    {
        const auto found = mReplicatedActors.find(ptr.mRef);
        if (found == mReplicatedActors.end() || ptr.getRefData().getBaseNode() == nullptr)
            return ReplicatedActorResult::LifecycleViolation;
        if (!isValidReplicatedActorPose(position) || !std::isfinite(animationSeconds) || animationSeconds < 0.f)
            return ReplicatedActorResult::InvalidPose;

        ptr.getRefData().setPosition(position);
        ptr.getRefData().getBaseNode()->setPosition(position.asVec3());
        ptr.getRefData().getBaseNode()->setAttitude(Misc::Convert::makeOsgQuat(position.rot));
        found->second->runAnimation(animationSeconds);
        return ReplicatedActorResult::Accepted;
    }

    bool Objects::removeReplicatedActor(const MWWorld::Ptr& ptr) noexcept
    {
        const auto found = mReplicatedActors.find(ptr.mRef);
        if (found == mReplicatedActors.end())
            return false;
        found->second->removeFromScene();
        mUnrefQueue.push(std::move(found->second));
        mReplicatedActors.erase(found);
        if (ptr.getRefData().getBaseNode() != nullptr)
        {
            if (ptr.getRefData().getBaseNode()->getNumParents() != 0)
                ptr.getRefData().getBaseNode()->getParent(0)->removeChild(ptr.getRefData().getBaseNode());
            ptr.getRefData().setBaseNode(nullptr);
        }
        return true;
    }

    class ReplicatedActor::Impl
    {
    public:
        Impl(RenderingManager& rendering, const MWWorld::ESMStore& store, const ESM::RefId& npcRecord,
            MWWorld::CellStore& cell, const ESM::Position& position)
            : mRendering(rendering)
            , mReference(store, npcRecord)
            , mPtr(mReference.getPtr().mRef, &cell)
        {
            if (mPtr.getType() != ESM::REC_NPC_)
                throw BuildFailure(ReplicatedActorResult::InvalidAppearanceRecord);
            if (!isValidReplicatedActorPose(position))
                throw BuildFailure(ReplicatedActorResult::InvalidPose);
            mPtr.getRefData().setPosition(position);
            mCreateResult = mRendering.getObjects().insertReplicatedActor(mPtr, store);
            if (!replicatedActorResultAccepted(mCreateResult))
                throw BuildFailure(mCreateResult);
        }

        ~Impl() { mRendering.getObjects().removeReplicatedActor(mPtr); }

        ReplicatedActorResult update(const ESM::Position& position, float animationSeconds) noexcept
        {
            return mRendering.getObjects().advanceReplicatedActor(mPtr, position, animationSeconds);
        }

        ReplicatedActorResult createResult() const noexcept { return mCreateResult; }

    private:
        RenderingManager& mRendering;
        MWWorld::ManualRef mReference;
        MWWorld::Ptr mPtr;
        ReplicatedActorResult mCreateResult = ReplicatedActorResult::LifecycleViolation;
    };

    ReplicatedActor::ReplicatedActor(std::unique_ptr<Impl> impl) noexcept
        : mImpl(std::move(impl))
    {
    }

    ReplicatedActor::~ReplicatedActor() = default;

    ReplicatedActorResult ReplicatedActor::update(const ESM::Position& position, float animationSeconds) noexcept
    {
        if (!mImpl)
            return ReplicatedActorResult::LifecycleViolation;
        return mImpl->update(position, animationSeconds);
    }

    ReplicatedActor::CreateResult ReplicatedActor::create(RenderingManager& rendering,
        const MWWorld::ESMStore& store, const ESM::RefId& npcRecord, MWWorld::CellStore& cell,
        const ESM::Position& position) noexcept
    {
        try
        {
            if (npcRecord.empty() || store.find(npcRecord) != ESM::NPC::sRecordId)
                return { ReplicatedActorResult::InvalidAppearanceRecord, nullptr };
            auto impl = std::make_unique<Impl>(rendering, store, npcRecord, cell, position);
            const ReplicatedActorResult result = impl->createResult();
            return { result, std::unique_ptr<ReplicatedActor>(new ReplicatedActor(std::move(impl))) };
        }
        catch (const BuildFailure& failure)
        {
            return { failure.result(), nullptr };
        }
        catch (...)
        {
            return { ReplicatedActorResult::ResourceLoadFailed, nullptr };
        }
    }
}
