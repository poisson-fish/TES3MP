#include "effectmanager.hpp"

#include <osg/PositionAttitudeTransform>
#include <osg/Light>
#include <osg/FrameStamp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/controller.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <components/sceneutil/visitor.hpp>

#include "animation.hpp"
#include "util.hpp"
#include "vismask.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace MWRender
{
    namespace
    {
        class RotateMagicBolt : public SceneUtil::NodeCallback<RotateMagicBolt, osg::PositionAttitudeTransform*>
        {
        public:
            void operator()(osg::PositionAttitudeTransform* node, osg::NodeVisitor* visitor)
            {
                node->setAttitude(osg::Quat(visitor->getFrameStamp()->getSimulationTime() * osg::PI * 2,
                    osg::Vec3f(0, -1, 0)));
                traverse(node, visitor);
            }
        };
    }

    EffectManager::EffectManager(osg::ref_ptr<osg::Group> parent, Resource::ResourceSystem* resourceSystem)
        : mParentNode(std::move(parent))
        , mResourceSystem(resourceSystem)
    {
    }

    EffectManager::~EffectManager()
    {
        clear();
    }

    void EffectManager::addEffect(VFS::Path::NormalizedView model, std::string_view textureOverride,
        const osg::Vec3f& worldPosition, float scale, bool isMagicVFX, bool useAmbientLight, std::string_view effectId,
        bool loop)
    {
        osg::ref_ptr<osg::Node> node = mResourceSystem->getSceneManager()->getInstance(model);

        node->setNodeMask(Mask_Effect);

        Effect effect;
        effect.mAnimTime = std::make_shared<EffectAnimationTime>();
        effect.mLoop = loop;
        effect.mEffectId = effectId;

        SceneUtil::FindMaxControllerLengthVisitor findMaxLengthVisitor;
        node->accept(findMaxLengthVisitor);
        effect.mMaxControllerLength = findMaxLengthVisitor.getMaxLength();

        osg::ref_ptr<osg::PositionAttitudeTransform> trans = new osg::PositionAttitudeTransform;
        trans->setPosition(worldPosition);
        trans->setScale(osg::Vec3f(scale, scale, scale));
        trans->addChild(node);

        effect.mTransform = trans;

        SceneUtil::AssignControllerSourcesVisitor assignVisitor(effect.mAnimTime);
        node->accept(assignVisitor);

        if (isMagicVFX)
            overrideFirstRootTexture(VFS::Path::toNormalized(textureOverride), mResourceSystem, *node);
        else
            overrideTexture(VFS::Path::toNormalized(textureOverride), mResourceSystem, *node);

        mParentNode->addChild(trans);

        if (useAmbientLight)
        {
            // Morrowind has a white ambient light attached to the root VFX node of the scenegraph
            node->getOrCreateStateSet()->setAttributeAndModes(
                getVFXLightModelInstance(), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        }

        mResourceSystem->getSceneManager()->setUpNormalsRTForStateSet(node->getOrCreateStateSet(), false);

        mEffects.push_back(std::move(effect));
    }

    void EffectManager::addMagicBolt(const std::vector<VFS::Path::Normalized>& models,
        std::string_view textureOverride, const osg::Vec3f& worldPosition,
        const std::array<float, 4>& lightColor, std::string_view effectId)
    {
        if (models.empty()) return;
        Effect effect;
        effect.mAnimTime = std::make_shared<EffectAnimationTime>();
        effect.mLoop = true;
        effect.mEffectId = effectId;
        effect.mTransform = new osg::PositionAttitudeTransform;
        effect.mTransform->setNodeMask(Mask_Effect);
        effect.mTransform->setPosition(worldPosition);

        osg::ref_ptr<osg::PositionAttitudeTransform> rotating = new osg::PositionAttitudeTransform;
        rotating->addUpdateCallback(new RotateMagicBolt);
        effect.mTransform->addChild(rotating);
        osg::ref_ptr<osg::Node> projectile = mResourceSystem->getSceneManager()->getInstance(models.front(), rotating);
        if (models.size() > 1)
            for (size_t index = 1; index < models.size(); ++index)
            {
                std::ostringstream name;
                name << "Dummy" << std::setw(2) << std::setfill('0') << index;
                SceneUtil::FindByNameVisitor finder(name.str());
                rotating->accept(finder);
                if (finder.mFoundNode)
                    mResourceSystem->getSceneManager()->getInstance(models[index], finder.mFoundNode);
            }

        SceneUtil::FindMaxControllerLengthVisitor length;
        rotating->accept(length);
        effect.mMaxControllerLength = length.getMaxLength();
        SceneUtil::AssignControllerSourcesVisitor controllers(effect.mAnimTime);
        rotating->accept(controllers);
        overrideFirstRootTexture(VFS::Path::toNormalized(textureOverride), mResourceSystem, *projectile);
        projectile->getOrCreateStateSet()->setAttributeAndModes(
            getVFXLightModelInstance(), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        mResourceSystem->getSceneManager()->setUpNormalsRTForStateSet(projectile->getOrCreateStateSet(), false);

        osg::ref_ptr<osg::Light> light = new osg::Light;
        light->setAmbient(osg::Vec4(1, 1, 1, 1));
        light->setDiffuse(osg::Vec4(lightColor[0], lightColor[1], lightColor[2], lightColor[3]));
        light->setSpecular(osg::Vec4(0, 0, 0, 0));
        light->setConstantAttenuation(0.f);
        light->setLinearAttenuation(0.1f);
        light->setQuadraticAttenuation(0.f);
        light->setPosition(osg::Vec4(worldPosition, 1));
        auto* source = new SceneUtil::LightSource;
        source->setNodeMask(Mask_Lighting);
        source->setRadius(66.f);
        source->setLight(light);
        effect.mTransform->addChild(source);
        effect.mTransform->addCullCallback(new SceneUtil::LightListCallback);
        mParentNode->addChild(effect.mTransform);
        mEffects.push_back(std::move(effect));
    }

    void EffectManager::removeEffect(std::string_view effectId)
    {
        mEffects.erase(std::remove_if(mEffects.begin(), mEffects.end(),
                           [effectId, this](Effect& effect) {
                               if (effectId == effect.mEffectId)
                               {
                                   mParentNode->removeChild(effect.mTransform);
                                   return true;
                               }

                               return false;
                           }),
            mEffects.end());
    }

    bool EffectManager::moveEffect(std::string_view effectId, const osg::Vec3f& position,
        const osg::Quat& attitude)
    {
        const auto found = std::ranges::find(mEffects, effectId, &Effect::mEffectId);
        if (found == mEffects.end()) return false;
        found->mTransform->setPosition(position);
        found->mTransform->setAttitude(attitude);
        return true;
    }

    void EffectManager::update(float dt)
    {
        mEffects.erase(std::remove_if(mEffects.begin(), mEffects.end(),
                           [dt, this](Effect& effect) {
                               bool remove = false;
                               effect.mAnimTime->addTime(dt);
                               if (effect.mAnimTime->getTime() >= effect.mMaxControllerLength)
                               {
                                   if (effect.mLoop)
                                   {
                                       float remainder = effect.mAnimTime->getTime() - effect.mMaxControllerLength;
                                       effect.mAnimTime->resetTime(remainder);
                                   }
                                   else
                                   {
                                       mParentNode->removeChild(effect.mTransform);
                                       remove = true;
                                   }
                               }

                               return remove;
                           }),
            mEffects.end());
    }

    void EffectManager::clear()
    {
        for (const auto& effect : mEffects)
        {
            mParentNode->removeChild(effect.mTransform);
        }
        mEffects.clear();
    }

}
