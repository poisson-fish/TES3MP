#include <apps/openmw/mwrender/animation.hpp>
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
                addSingleAnimSource(std::string(Settings::models().mXbaseanimkf.get().value()), "pose test");
                if (body == 1)
                    addSingleAnimSource(std::string(Settings::models().mXbaseanimfemalekf.get().value()), "female pose test");
                if (body >= 2)
                {
                    auto beast = Misc::ResourceHelpers::correctActorModelPath(
                        Settings::models().mBaseanimkna.get(), resources.getVFS());
                    beast.changeExtension(VFS::Path::ExtensionView("kf"));
                    require(resources.getVFS()->exists(beast), "Beast animation layer missing");
                    addSingleAnimSource(std::string(beast.value()), "beast pose test");
                    if (body == 3)
                        addSingleAnimSource(std::string(Settings::models().mXargonianswimknakf.get().value()), "argonian pose test");
                }
                mContext = Context::Gameplay;
            }
            bool listenerIs(TextKeyListener* value) const { return mTextKeyListener == value; }
        };
        struct Listener : MWRender::Animation::TextKeyListener
        {
            unsigned calls = 0;
            void handleTextKey(std::string_view, SceneUtil::TextKeyMap::ConstIterator,
                const SceneUtil::TextKeyMap&) override { ++calls; }
        };
    }

    void checkMeleePresentation(const std::filesystem::path& data, const std::filesystem::path& settings, bool knockout)
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
        if (knockout)
        {
            for (unsigned body = 0; body < 4; ++body)
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
