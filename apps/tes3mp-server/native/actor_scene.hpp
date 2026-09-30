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
#include "cast_animation.hpp"
#include "hit_animation.hpp"

namespace ESM { struct NPC; struct Race; struct Weapon; }

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
        bool mDrowning = false;
    };
    struct ActorMovement
    {
        bool enabled = false;
        float walkSpeed = 0;
        float swimSpeed = 0;
        float flySpeed = 0;
        float jumpSpeed = 0;
        float slowFall = 1;
        bool levitating = false;
        bool waterWalking = false;
        bool waterBreathing = false;
        bool unconscious = false;
        bool jumpRequested = false;
    };
    struct BoundMeleeAnimation
    {
        MeleeAnimation mAnimation;
        std::string mResourceIdentity;
    };

    // Detached, content-derived interior/exterior navigation and physics. References are
    // obstacles; bound doors receive transaction-owned angles. The selected
    // NPC and its bounded neighbors step against one collision world. The native host composes
    // prepared frames with its existing inventory owner and durable transaction.
    // Shared door avoidance can interrupt the retained destination. No complete
    // AI packages, scripts or presentation services are constructed here.
    class InteriorActorScene
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        struct Dormant;
        std::unique_ptr<Dormant> mDormant;
        std::unique_ptr<InteriorActorScene> mNeighbor;
        InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation,
            std::span<const uint64_t> neighbors, Impl* sharedParent);
        std::pair<std::vector<std::shared_ptr<const SceneUtil::TextKeyMap>>, std::string>
            bindAnimationSources(ESM::RefId actor);
    public:
        InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation);
        // V20: one interior or a fixed, contiguous exterior processing neighborhood
        // (at most 3x3). One collision world and one actor frame across cell edges.
        InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation, uint64_t neighbor = 0);
        InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation,
            std::span<const uint64_t> neighbors);
        bool contains(const std::array<float, 3>& position) const;
        bool pathUnavailable() const;
        ~InteriorActorScene();
        ActorSceneSnapshot snapshot() const;
        std::optional<ActorSceneSnapshot> neighborSnapshot() const;
        std::vector<ActorSceneSnapshot> neighborSnapshots() const;
        std::array<float, 4> transform() const noexcept;
        // One stock 60 Hz step. Velocity is local-space diagnostic input, not AI
        // or authenticated player input. Invalid input leaves the scene unchanged.
        ActorSceneSnapshot step(const std::array<float, 3>& velocity);
        size_t bodyCount() const;
        // First server-owned collision and impact point along a spell bolt
        // segment. Actor zero means world geometry; absence means clear.
        std::optional<ActorProjectileContact> projectileContact(const std::array<float, 3>& from,
            const std::array<float, 3>& to, uint64_t casterActor = 0) const;
        // Stock LOS collision mask. Player eye positions use the server proxy
        // until native player hulls are bound.
        bool lineOfSight(const std::array<float, 3>& from, const std::array<float, 3>& to) const;
        // Stock underwater cast gate against the selected or bound neighbor hull.
        bool waterWalkingCastable(uint64_t actor) const;
        uint64_t actorId() const noexcept;
        // The same resource hulls used by stock physics, for AiCombat's
        // distance-minus-half-extents flee gate.
        float selectedActorHalfExtentY() const;
        float npcHalfExtentY(ESM::RefId race, float scale) const;
        const std::string& fingerprint() const;
        bool enchantedWeaponsAreMagical() const;
        bool onlyAppropriateAmmunitionBypassesResistance() const;
        bool uncappedDamageFatigue() const;
        bool classicReflectedAbsorb() const;
        // Resolve the selected NPC's third-person animation source using stock
        // source priority. The returned identity must join a combat campaign's
        // content binding before a swing can become authoritative.
        BoundMeleeAnimation bindMeleeAnimation(std::string group, std::string attack, float speed);
        BoundCastAnimations bindCastAnimations(ESM::RefId actor = {});
        BoundHitAnimations bindHitAnimations(ESM::RefId actor, bool knockout = false);
        MeleeAnimation bindWeaponMeleeAnimation(ESM::RefId actor, const ESM::Weapon* weapon, const std::string& attack);
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
        void enableMovementEffects();
        std::vector<std::array<float, 3>> pathTo(const std::array<float, 3>& destination) const;
        // Stock AiCombat candidates: connected pathgrid points other than the
        // closest point. An empty result selects the one-second blind run.
        std::vector<std::array<float, 3>> fleePathgridDestinations() const;
        void travelTo(const std::array<float, 3>& destination, bool retainUnavailable = false);
        ActorSceneSnapshot navigate(float speed);
        bool arrived() const;
        class Prepared
        {
            friend class InteriorActorScene;
            struct State;
            std::unique_ptr<State> mState;
            std::unique_ptr<Prepared> mNeighbor;
            explicit Prepared(std::unique_ptr<State> state);
        public:
            ~Prepared();
            ActorSceneSnapshot snapshot() const;
            std::optional<ActorSceneSnapshot> neighborSnapshot() const;
            std::vector<ActorSceneSnapshot> neighborSnapshots() const;
            std::span<const char> image() const;
            bool pathUnavailable() const;
            bool pathCompleted() const;
        };
        // Two stock 60 Hz steps, isolated until a durable 30 Hz tick installs.
        std::unique_ptr<Prepared> prepareNavigation(float speed, std::span<const ActorSceneDoor> doors = {});
        std::unique_ptr<Prepared> prepareNavigation(const ActorMovement& movement,
            std::span<const ActorSceneDoor> doors = {},
            std::optional<std::array<float, 3>> destination = {});
        void prepareNeighborNavigation(Prepared& prepared, float speed,
            std::span<const ActorSceneDoor> doors,
            std::optional<std::array<float, 3>> destination);
        void prepareNeighborNavigation(Prepared& prepared, std::span<const float> speeds,
            std::span<const ActorSceneDoor> doors,
            std::span<const std::optional<std::array<float, 3>>> destinations);
        std::unique_ptr<Prepared> prepareBlindRun(const ActorMovement& movement,
            std::span<const ActorSceneDoor> doors, const std::array<float, 3>& enemy);
        void setFacing(Prepared& prepared, float yaw) const;
        bool canInstall(const Prepared& prepared) const noexcept;
        void install(Prepared& prepared) noexcept;
        std::vector<char> image() const;
        std::vector<char> selectedImage() const;
        std::vector<char> neighborImage() const;
        std::vector<char> neighborImage(size_t index) const;
        void restore(std::span<const char> bytes);
        std::unique_ptr<Prepared> prepareRestore(std::span<const char> bytes, std::span<const ActorSceneDoor> doors = {});
        std::unique_ptr<Prepared> prepareSelectedRestore(std::span<const char> bytes,
            std::span<const ActorSceneDoor> doors = {});
        std::unique_ptr<Prepared> prepareNeighborRestore(std::span<const char> bytes,
            std::span<const ActorSceneDoor> doors = {});
        std::unique_ptr<Prepared> prepareNeighborRestore(size_t index, std::span<const char> bytes,
            std::span<const ActorSceneDoor> doors = {});
    };
}

#endif
