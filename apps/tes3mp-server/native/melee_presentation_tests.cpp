#include "actor_scene.hpp"
#include "loadout.hpp"
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <apps/openmw/mwrender/animation.hpp>
#include <apps/openmw/mwrender/replicatedactor.hpp>
#include <components/files/collections.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace TES3MP::Native::Testing
{
    namespace
    {
        void require(bool condition, const char* message)
        { if (!condition) throw std::runtime_error(message); }

        // Real KF sections, with no scene or gameplay services. Any accidental
        // gameplay/Lua callback therefore also fails this isolated presentation test.
        class Pose : public MWRender::Animation
        {
        public:
            explicit Pose(Resource::ResourceSystem& resources, unsigned body = 0)
                : Animation({}, new osg::Group, &resources, Context::ReplicatedActor)
            {
                mObjectRoot = new osg::Group;
                reloadSources(body);
                mContext = Context::Gameplay;
            }
            void reloadSources(unsigned body)
            {
                clearAnimSources();
                addSingleAnimSource(std::string(Settings::models().mXbaseanimkf.get().value()), "pose test");
                if (body == 1)
                    addSingleAnimSource(std::string(Settings::models().mXbaseanimfemalekf.get().value()), "female pose test");
                if (body == 4)
                    addSingleAnimSource("meshes/r/xdremora.kf", "Dremora pose test");
                if (body >= 2 && body < 4)
                {
                    auto beast = Misc::ResourceHelpers::correctActorModelPath(
                        Settings::models().mBaseanimkna.get(), mResourceSystem->getVFS());
                    beast.changeExtension(VFS::Path::ExtensionView("kf"));
                    require(mResourceSystem->getVFS()->exists(beast), "Beast animation layer missing");
                    addSingleAnimSource(std::string(beast.value()), "beast pose test");
                    if (body == 3)
                        addSingleAnimSource(std::string(Settings::models().mXargonianswimknakf.get().value()), "argonian pose test");
                }
            }
            void npcSources(const ESM::NPC& npc, bool beast)
            {
                clearAnimSources();
                const auto base = Settings::models().mXbaseanim.get();
                const auto normal = beast ? Settings::models().mBaseanimkna.get()
                    : npc.isMale() ? Settings::models().mBaseanim.get() : Settings::models().mBaseanimfemale.get();
                const std::string defaultSkeleton = Misc::ResourceHelpers::correctActorModelPath(normal, mResourceSystem->getVFS());
                const auto model = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(npc.mModel));
                const std::string skeleton = Misc::ResourceHelpers::correctActorModelPath(model, mResourceSystem->getVFS());
                addAnimSource(base, skeleton);
                if (defaultSkeleton != base.value()) addAnimSource(defaultSkeleton, skeleton);
                addAnimSource(skeleton, skeleton);
                if (beast && npc.mRace.contains("argonian"))
                    addAnimSource(Settings::models().mXargonianswimkna.get(), skeleton);
            }
            void clearSources() { clearAnimSources(); }
            void useReplicaContext() { mContext = Context::ReplicatedActor; }
            bool listenerIs(TextKeyListener* value) const { return mTextKeyListener == value; }
        };
        struct Listener : MWRender::Animation::TextKeyListener
        {
            unsigned calls = 0;
            void handleTextKey(std::string_view, SceneUtil::TextKeyMap::ConstIterator,
                const SceneUtil::TextKeyMap&) override { ++calls; }
        };
    }

    void checkCastPresentation(const std::filesystem::path& config, const std::filesystem::path& settings)
    {
        const auto directory = config.string();
        const char* arguments[]{"cast-presentation", "--config", directory.c_str()};
        Loadout loadout(readLoadoutOptions(3, arguments));
        const auto& npc = *loadout.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("npc_door_actor"));
        const auto placed = loadout.placedActors(ESM::RefId::stringRefId("NPC Door Contact Test")).at(0);
        InteriorActorScene scene(loadout, "NPC Door Contact Test", placed.mIdentity,
            "meshes/base_anim.nif", "meshes/base_animkna.nif");
        scene.enableNavigation(settings.string());
        const auto casts = scene.bindCastAnimations();
        require(casts.resourceIdentity == scene.bindHitAnimations(npc.mId, true).resourceIdentity,
            "Cast/body resource identities differ");
        Settings::SettingsFileParser().loadSettingsFile(settings, Settings::Manager::mDefaultSettings);
        Settings::StaticValues::initDefaults(); Settings::StaticValues::init();
        Settings::game().mSmoothAnimTransitions.set(false);
        VFS::Manager vfs;
        VFS::registerArchives(&vfs, Files::Collections(loadout.options().mDataPaths),
            loadout.options().mArchives, true, &loadout.encoder());
        Resource::ResourceSystem resources(&vfs, 0, &loadout.encoder());
        const bool beast = loadout.store().get<ESM::Race>().find(npc.mRace)->mData.mFlags & ESM::Race::Beast;
        Pose local(resources), peer(resources), reconnect(resources);
        for (auto* pose : {&local, &peer, &reconnect})
            if (!npc.mModel.empty()) pose->npcSources(npc, beast);
            else pose->reloadSources(beast ? (npc.mRace.contains("argonian") ? 3 : 2) : npc.isMale() ? 0 : 1);
        Listener listener; local.setTextKeyListener(&listener);
        const std::array<std::string, 3> ranges{"self", "touch", "target"};
        for (unsigned range = 0; range < ranges.size(); ++range)
        {
            const auto timing = casts.ranges[range];
            const std::string prefix = "spellcast: " + ranges[range];
            const float start = local.getTextKeyTime(prefix + " start");
            const float release = local.getTextKeyTime(prefix + " release");
            const float stop = local.getTextKeyTime(prefix + " stop");
            require(start >= 0 && release > start && stop > release
                && timing.releaseTicks == unsigned(std::ceil((release - start) * 30.f))
                && timing.stopTicks == unsigned(std::ceil((stop - start) * 30.f)),
                "Server/desktop cast range keys differ");
            for (float frame = 0; frame < timing.stopTicks; frame += .25f)
            {
                for (auto* pose : {&local, &peer, &reconnect})
                    require(pose->setCommittedCast(true, range, frame, timing.releaseTicks, timing.stopTicks),
                        "Cast pose rejected");
                const float expected = frame < timing.releaseTicks
                    ? std::lerp(start, release, frame / timing.releaseTicks)
                    : std::lerp(release, stop, (frame - timing.releaseTicks) / (timing.stopTicks - timing.releaseTicks));
                local.runAnimation(10.f); peer.runAnimation(.001f);
                for (auto* pose : {&local, &peer, &reconnect})
                    require(std::abs(pose->getCurrentTime("spellcast") - expected) < .001f,
                        "Cast advanced locally or sampled the wrong section");
                require(reconnect.setCommittedCast(false, 0, 0, 0, 0), "Cast reset failed");
            }
            require(local.setCommittedBody(4, 0, 1) && !local.getInfo("spellcast"),
                "Interruption retained cast priority");
            local.setCommittedBody(1, 0);
            require(!local.setCommittedCast(true, range, 0, 0, timing.stopTicks)
                && !local.setCommittedCast(true, range, float(timing.stopTicks), timing.releaseTicks, timing.stopTicks)
                && listener.calls == 0 && local.listenerIs(&listener), "Invalid pose or callbacks escaped");
        }
        std::cout << casts.resourceIdentity << "cast ranges=3 fractional=120Hz release+recovery+interruption+reconnect callbacks=none\n";
    }

    void checkCustomBodyResources(const std::filesystem::path& config, const std::filesystem::path& settings)
    {
        const auto directory = config.string();
        const char* arguments[]{"custom-body-resources", "--config", directory.c_str()};
        Loadout loadout(readLoadoutOptions(3, arguments));
        const auto& npc = *loadout.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("npc_door_actor"));
        const auto placed = loadout.placedActors(ESM::RefId::stringRefId("NPC Door Contact Test")).at(0);
        InteriorActorScene scene(loadout, "NPC Door Contact Test", placed.mIdentity,
            "meshes/base_anim.nif", "meshes/base_animkna.nif");
        scene.enableNavigation(settings.string());
        const auto hits = scene.bindHitAnimations(npc.mId, true);
        require(hits.resourceIdentity.find("absent") == std::string::npos,
            "Custom fixture contains a missing animation source");
        std::cout << hits.resourceIdentity;
        Settings::SettingsFileParser().loadSettingsFile(settings, Settings::Manager::mDefaultSettings);
        Settings::StaticValues::initDefaults();
        Settings::StaticValues::init();
        Settings::game().mSmoothAnimTransitions.set(false);
        VFS::Manager vfs;
        VFS::registerArchives(&vfs, Files::Collections(loadout.options().mDataPaths),
            loadout.options().mArchives, true, &loadout.encoder());
        Resource::ResourceSystem resources(&vfs, 0, &loadout.encoder());
        const bool beast = loadout.store().get<ESM::Race>().find(npc.mRace)->mData.mFlags & ESM::Race::Beast;
        Pose pose(resources); pose.npcSources(npc, beast);
        Listener listener; pose.setTextKeyListener(&listener);
        for (const auto group : {"handtohand", "weapononehand", "weapontwohand", "weapontwowide"})
            for (const auto direction : {"chop", "slash", "thrust"})
                for (float strength : {0.f, .5f, 1.f})
                {
                    auto bound = scene.bindMeleeAnimation(group, direction, 1.f);
                    require(bound.mResourceIdentity == hits.resourceIdentity, "Melee/body resource layers differ");
                    auto& clip = bound.mAnimation;
                    for (unsigned frame = 0; frame < 600 && clip.snapshot().mPhase != MeleeAnimation::Phase::Complete; ++frame)
                    {
                        if (clip.snapshot().mPhase == MeleeAnimation::Phase::WindUp && clip.phaseCompletion() == 1.f)
                            clip.release(strength);
                        require(pose.setCommittedMelee(group, unsigned(clip.snapshot().mPhase) + 1,
                            clip.direction(), clip.snapshot().mStrength, clip.phaseCompletion()), "Custom melee pose missing");
                        const float time = pose.getCurrentTime(group);
                        require(std::abs(time - clip.snapshot().mTime) < .001f, "Native/custom stock melee times differ");
                        pose.runAnimation(.2f);
                        require(pose.getCurrentTime(group) == time && listener.calls == 0, "Custom pose advanced or replayed keys");
                        clip.advance(1.f / 120);
                    }
                    require(clip.snapshot().mPhase == MeleeAnimation::Phase::Complete, "Custom melee never completed");
                    pose.setCommittedMelee({}, 0, 0, 0, 0);
                }
        for (unsigned body : {2u, 3u})
        {
            const std::string group = body == 2 ? "knockout" : "knockdown";
            const auto& clip = body == 2 ? hits.knockout : hits.knockdown;
            const float start = pose.getTextKeyTime(group + ": start"), stop = pose.getTextKeyTime(group + ": stop");
            require(clip.stop == unsigned(std::ceil((stop - start) * 30.f)), "Custom body duration differs");
            for (float frame = 0; frame < clip.stop; frame += .25f)
            {
                require(pose.setCommittedBody(body, frame), "Custom body pose missing");
                const float time = pose.getCurrentTime(group);
                require(std::abs(time - std::min(stop, start + frame / 30)) < .001f, "Custom body clock differs");
                pose.runAnimation(.2f);
                require(pose.getCurrentTime(group) == time && listener.calls == 0, "Custom body replayed callbacks");
            }
            pose.setCommittedBody(1, 0);
        }
        for (unsigned i = 0; i < hits.animations.count; ++i)
        {
            const std::string group = "hit" + std::to_string(i + 1);
            const float start = pose.getTextKeyTime(group + ": start"), stop = pose.getTextKeyTime(group + ": stop");
            require(hits.animations.ticks[i] == std::max(1u, unsigned(std::ceil(
                (stop - start) * 30.f))),
                "Custom hit duration differs");
            for (float frame = 0; frame < hits.animations.ticks[i]; frame += .25f)
            {
                require(pose.setCommittedBody(4, frame, i + 1), "Custom hit pose missing");
                const float time = pose.getCurrentTime(group);
                require(std::abs(time - std::min(stop, start + frame / 30)) < .001f, "Custom hit clock differs");
                pose.runAnimation(.2f);
                require(pose.getCurrentTime(group) == time && listener.calls == 0, "Custom hit replayed callbacks");
            }
            pose.setCommittedBody(1, 0);
        }
        std::cout << "custom layers=matching melee=4x3 strengths=0+.5+1 body=hit+knockout+knockdown callbacks=none\n";
    }

    void checkMeleePresentation(const std::filesystem::path& data, const std::filesystem::path& settings,
        bool knockout, bool actions)
    {
        Settings::SettingsFileParser().loadSettingsFile(settings, Settings::Manager::mDefaultSettings);
        Settings::StaticValues::initDefaults();
        Settings::StaticValues::init();
        Settings::game().mSmoothAnimTransitions.set(false);
        VFS::Manager vfs;
        ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        VFS::registerArchives(&vfs, Files::Collections({data}), {"Morrowind.bsa"}, true,
            &encoder.getStatelessEncoder());
        Resource::ResourceSystem resources(&vfs, 0, &encoder.getStatelessEncoder());
        if (actions)
        {
            for (unsigned body = 0; body < 5; ++body)
            {
                Pose replica(resources, body);
                replica.useReplicaContext();
                Listener listener;
                replica.setTextKeyListener(&listener);
                for (const auto action : {MWRender::ReplicatedActorAction::Attack, MWRender::ReplicatedActorAction::Hit})
                {
                    const std::string group = action == MWRender::ReplicatedActorAction::Attack ? "weapononehand" : "hit1";
                    const std::string key = action == MWRender::ReplicatedActorAction::Attack ? "chop min attack" : "start";
                    const float start = replica.getTextKeyTime(group + ": " + key);
                    for (unsigned repeat = 0; repeat < 2; ++repeat)
                    {
                        require(MWRender::playReplicatedActorAction(replica, action), "Replica action rejected");
                        require(replica.getInfo(group) && replica.getCurrentTime(group) == start,
                            "Replica action did not begin at its stock section");
                        replica.runAnimation(.1f);
                        require(replica.getCurrentTime(group) > start, "Replica action failed to advance");
                        require(MWRender::playReplicatedActorAction(replica, action)
                            && replica.getCurrentTime(group) == start, "New action did not restart the live clip");
                        replica.runAnimation(10.f);
                        require(!replica.getInfo(group), "Completed action retained animation priority");
                    }
                    require(listener.calls == 0, "Cosmetic NPC event replayed gameplay keys");
                }
                std::cout << "npc-action body=" << body << " attack+hit=advance+repeat+finish callbacks=none\n";
            }
            return;
        }
        if (knockout)
        {
            for (unsigned body = 0; body < 5; ++body)
            {
                Listener listener;
                Pose local(resources, body), peer(resources, body), reconnect(resources, body);
                local.setTextKeyListener(&listener);
                for (unsigned pose : {2u, 3u})
                {
                    const std::string group = pose == 2 ? "knockout" : "knockdown";
                    const float start = local.getTextKeyTime(group + ": start");
                    const float stop = local.getTextKeyTime(group + ": stop");
                    require(start >= 0 && stop > start, "Knockout resource missing");
                    const unsigned frames = unsigned(std::ceil((stop - start) * 30.f));
                    // Late baselines, loop rewind, get-up tail, repeated/lost commits.
                    for (unsigned frame : {0u, frames / 2, 1u, frames - 1})
                    {
                        require(local.setCommittedMelee("handtohand", 1, 0, .5f, .5f)
                            && local.setCommittedKnockout(pose, frame)
                            && peer.setCommittedKnockout(pose, frame), "Committed knockout rejected");
                        const float time = local.getCurrentTime(group);
                        require(std::abs(time - std::min(stop, start + float(frame) / 30.f)) < .0001f,
                            "Knockout sampled wrong committed frame");
                        local.runAnimation(10.f); peer.runAnimation(.01f);
                        require(!local.hasCommittedMelee() && local.getCurrentTime(group) == time
                            && peer.getCurrentTime(group) == time, "Wall time advanced knockout or retained swing");
                        require(reconnect.setCommittedKnockout(0, 0) && reconnect.setCommittedKnockout(pose, frame)
                            && reconnect.getCurrentTime(group) == time, "Reconnect restarted knockout");
                        // A POV/model rebuild can briefly have no matching clip.
                        // Reinstalling layered resources must recover the exact
                        // last server sample without another packet or callbacks.
                        local.clearSources();
                        require(local.restoreCommittedKnockout(), "Missing clip lost committed authority");
                        local.reloadSources(body);
                        require(local.restoreCommittedKnockout() && local.getCurrentTime(group) == time,
                            "Skeleton rebuild reset the committed knockout frame");
                        require(local.setCommittedBody(pose, float(frame) + .5f)
                            && std::abs(local.getCurrentTime(group) - std::min(stop, start + (float(frame) + .5f) / 30.f)) < .0001f,
                            "Render-frame sampling did not progress at stock clip speed");
                        require(listener.calls == 0 && local.listenerIs(&listener), "Knockout replayed gameplay callbacks");
                    }
                    require(local.setCommittedKnockout(1, 0) && !local.getInfo(group)
                        && local.committedKnockoutState() == 1, "Upright commit retained knockout");
                }
                require(!local.setCommittedKnockout(4, 0) && !local.setCommittedKnockout(1, 1)
                    && !local.setCommittedKnockout(2, 1800) && local.committedKnockoutState() == 1,
                    "Invalid knockout mutated presentation");
                require(local.setCommittedKnockout(0, 0) && !local.committedKnockoutState(),
                    "Disconnect retained knockout authority");
                std::cout << "knockout+knockdown body=" << body << " committed=reconnect+rewind+getup callbacks=none\n";
            }
            return;
        }
        Listener listener;
        Pose local(resources), peer(resources), reconnect(resources);
        local.setTextKeyListener(&listener);
        for (const auto group : {"handtohand", "weapononehand", "weapontwohand", "weapontwowide", "bowandarrow", "crossbow", "throwweapon"})
            for (unsigned direction = 0; direction < 3; ++direction)
                for (float strength : {0.f, .5f, 1.f})
                    for (unsigned phase = 1; phase <= 3; ++phase)
                        for (float completion : {0.f, .4f, 1.f})
                        {
                            require(local.setCommittedMelee(group, phase, direction, strength, completion)
                                && peer.setCommittedMelee(group, phase, direction, strength, completion),
                                "Real melee pose section missing");
                            const float time = local.getCurrentTime(group);
                            local.runAnimation(1.f);
                            peer.runAnimation(.1f);
                            require(local.getCurrentTime(group) == time && peer.getCurrentTime(group) == time,
                                "Client wall time advanced a committed melee pose");
                            require(reconnect.setCommittedMelee({}, 0, 0, 0, 0)
                                && reconnect.setCommittedMelee(group, phase, direction, strength, completion)
                                && reconnect.getCurrentTime(group) == time,
                                "Reconnect did not restore the committed pose");
                            require(listener.calls == 0 && local.listenerIs(&listener),
                                "Committed melee replayed gameplay keys or changed listener ownership");
                        }
        require(local.setCommittedMelee({}, 0, 0, 0, 0) && !local.hasCommittedMelee(),
            "Interruption retained committed pose");
        require(peer.setCommittedMelee({}, 4, 0, 0, 1) && !peer.hasCommittedMelee(),
            "Completion retained committed pose");
        require(!local.setCommittedMelee("handtohand", 2, 3, .5f, .5f),
            "Invalid melee direction accepted");
        require(!local.setCommittedMelee("handtohand", 2, 0, .5f, 2.f),
            "Invalid melee completion accepted");
    }
}
