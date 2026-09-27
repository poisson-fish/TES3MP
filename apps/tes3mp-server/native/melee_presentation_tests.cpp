#include <apps/openmw/mwrender/animation.hpp>
#include <components/files/collections.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <cmath>
#include <filesystem>
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
            explicit Pose(Resource::ResourceSystem& resources)
                : Animation({}, new osg::Group, &resources, Context::ReplicatedActor)
            {
                mObjectRoot = new osg::Group;
                addSingleAnimSource(std::string(Settings::models().mXbaseanimkf.get().value()), "pose test");
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

    void checkMeleePresentation(const std::filesystem::path& data, const std::filesystem::path& settings)
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
