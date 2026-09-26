#ifndef TES3MP_NATIVE_ACTOR_SCENE_HPP
#define TES3MP_NATIVE_ACTOR_SCENE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <components/esm/refid.hpp>
#include "melee_animation.hpp"

namespace TES3MP::Native
{
    class Loadout;
    struct ActorSceneDoor
    {
        uint64_t mId;
        float mAngle;
        bool mMoving = false;
        bool mAvoid = false; // Only the selected NPC's server-sensed contact.
    };
    struct ActorDoorContact
    {
        bool mBlocked = false;
        bool mSelectedActor = false;
    };
    struct ActorProjectileContact
    {
        uint64_t actor = 0; // Zero is world geometry.
        std::array<float, 3> position{};
    };
    struct ActorSceneSnapshot
    {
        uint64_t mActor = 0;
        std::array<float, 3> mPosition{};
        bool mGrounded = false;
        std::vector<uint64_t> mContacts;
        float mYaw = 0;
    };
    struct BoundMeleeAnimation
    {
        MeleeAnimation mAnimation;
        std::string mResourceIdentity;
    };

    // Detached, content-derived interior/exterior navigation and physics. References are
    // obstacles; bound doors receive transaction-owned angles and only the
    // selected NPC is stepped. The native host composes
    // prepared frames with its existing inventory owner and durable transaction.
    // Shared door avoidance can interrupt the retained destination. No complete
    // AI packages, scripts or presentation services are constructed here.
    class InteriorActorScene
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        struct Dormant;
        std::unique_ptr<Dormant> mDormant;
    public:
        InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation);
        // V20: one interior or a fixed, contiguous exterior processing neighborhood
        // (at most 3x3). One collision world and one actor frame across cell edges.
        InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation);
        bool contains(const std::array<float, 3>& position) const;
        bool pathUnavailable() const;
        ~InteriorActorScene();
        ActorSceneSnapshot snapshot() const;
        std::array<float, 4> transform() const noexcept;
        // One stock 60 Hz step. Velocity is local-space diagnostic input, not AI
        // or authenticated player input. Invalid input leaves the scene unchanged.
        ActorSceneSnapshot step(const std::array<float, 3>& velocity);
        size_t bodyCount() const;
        // First server-owned collision and impact point along a spell bolt
        // segment. Actor zero means world geometry; absence means clear.
        std::optional<ActorProjectileContact> projectileContact(const std::array<float, 3>& from,
            const std::array<float, 3>& to) const;
        uint64_t actorId() const noexcept;
        const std::string& fingerprint() const;
        bool enchantedWeaponsAreMagical() const;
        bool uncappedDamageFatigue() const;
        // Resolve the selected NPC's third-person animation source using stock
        // source priority. The returned identity must join a combat campaign's
        // content binding before a swing can become authoritative.
        BoundMeleeAnimation bindMeleeAnimation(std::string group, std::string attack, float speed);
        bool loaded() const noexcept { return bool(mImpl); }
        // Release collision/navigation resources, retaining the exact committed
        // image. Reload validates a freshly bound scene before swapping it in.
        // The host calls these between transactions, never during preparation.
        void unload();
        void reload(InteriorActorScene& fresh);
        // Bind a complete, bounded set of ordinary doors before navigation.
        // Angles are owned by the inventory/door transaction, never this image.
        void bindDoors(std::span<const uint64_t> doors, bool avoidance = false);
        ActorDoorContact doorContact(uint64_t door, float proposedAngle, float delta) const;
        // Build the engine navmesh from the retained collision resources. The
        // trusted settings file supplies stock navigation settings and is bound
        // into the scene identity. No World/player services are constructed.
        void enableNavigation(const std::string& settingsFile);
        std::vector<std::array<float, 3>> pathTo(const std::array<float, 3>& destination) const;
        void travelTo(const std::array<float, 3>& destination, bool retainUnavailable = false);
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
            bool pathUnavailable() const;
        };
        // Two stock 60 Hz steps, isolated until a durable 30 Hz tick installs.
        std::unique_ptr<Prepared> prepareNavigation(float speed, std::span<const ActorSceneDoor> doors = {});
        bool canInstall(const Prepared& prepared) const noexcept;
        void install(Prepared& prepared) noexcept;
        std::vector<char> image() const;
        void restore(std::span<const char> bytes);
        std::unique_ptr<Prepared> prepareRestore(std::span<const char> bytes, std::span<const ActorSceneDoor> doors = {});
    };
}

#endif
