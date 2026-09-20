#ifndef TES3MP_NATIVE_ACTOR_SCENE_HPP
#define TES3MP_NATIVE_ACTOR_SCENE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TES3MP::Native
{
    class Loadout;
    struct ActorSceneSnapshot
    {
        uint64_t mActor = 0;
        std::array<float, 3> mPosition{};
        bool mGrounded = false;
        std::vector<uint64_t> mContacts;
    };

    // A detached, content-derived interior physics binding. No inventory, AI,
    // script, live host or persistence writer is installed by this diagnostic slice.
    // Frozen references are obstacles; only the selected NPC is stepped.
    class InteriorActorScene
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    public:
        InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation);
        ~InteriorActorScene();
        ActorSceneSnapshot snapshot() const;
        // One stock 60 Hz step. Velocity is local-space diagnostic input, not AI
        // or authenticated player input. Invalid input leaves the scene unchanged.
        ActorSceneSnapshot step(const std::array<float, 3>& velocity);
        size_t bodyCount() const;
        const std::string& fingerprint() const;
    };
}

#endif
