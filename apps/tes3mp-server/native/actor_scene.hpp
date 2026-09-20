#ifndef TES3MP_NATIVE_ACTOR_SCENE_HPP
#define TES3MP_NATIVE_ACTOR_SCENE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace TES3MP::Native
{
    class Loadout;
    struct ActorSceneDoor
    {
        uint64_t mId;
        float mAngle;
    };
    struct ActorSceneSnapshot
    {
        uint64_t mActor = 0;
        std::array<float, 3> mPosition{};
        bool mGrounded = false;
        std::vector<uint64_t> mContacts;
        float mYaw = 0;
    };

    // Detached, content-derived interior navigation/physics. References are
    // obstacles; bound doors receive transaction-owned angles and only the
    // selected NPC is stepped. The native host composes
    // prepared frames with its existing inventory owner and durable transaction.
    // No AI packages, scripts or presentation services are constructed here.
    class InteriorActorScene
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    public:
        InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation);
        ~InteriorActorScene();
        ActorSceneSnapshot snapshot() const;
        std::array<float, 4> transform() const noexcept;
        // One stock 60 Hz step. Velocity is local-space diagnostic input, not AI
        // or authenticated player input. Invalid input leaves the scene unchanged.
        ActorSceneSnapshot step(const std::array<float, 3>& velocity);
        size_t bodyCount() const;
        uint64_t actorId() const noexcept;
        const std::string& fingerprint() const;
        // Bind a complete, bounded set of ordinary doors before navigation.
        // Angles are owned by the inventory/door transaction, never this image.
        void bindDoors(std::span<const uint64_t> doors);
        bool doorBlocked(uint64_t door, float proposedAngle, float delta) const;
        // Build the engine navmesh from the retained collision resources. The
        // trusted settings file supplies stock navigation settings and is bound
        // into the scene identity. No World/player services are constructed.
        void enableNavigation(const std::string& settingsFile);
        std::vector<std::array<float, 3>> pathTo(const std::array<float, 3>& destination) const;
        void travelTo(const std::array<float, 3>& destination);
        ActorSceneSnapshot navigate(float speed);
        bool arrived() const;
        class Prepared
        {
            friend class InteriorActorScene;
            struct State;
            std::unique_ptr<State> mState;
            explicit Prepared(std::unique_ptr<State> state);
        public:
            ~Prepared();
            ActorSceneSnapshot snapshot() const;
            std::span<const char> image() const;
        };
        // Two stock 60 Hz steps, isolated until a durable 30 Hz tick installs.
        std::unique_ptr<Prepared> prepareNavigation(float speed, std::span<const ActorSceneDoor> doors = {});
        void install(Prepared& prepared) noexcept;
        std::vector<char> image() const;
        void restore(std::span<const char> bytes);
        std::unique_ptr<Prepared> prepareRestore(std::span<const char> bytes, std::span<const ActorSceneDoor> doors = {});
    };
}

#endif
