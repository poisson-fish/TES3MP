#ifndef TES3MP_NATIVE_MELEE_ANIMATION_HPP
#define TES3MP_NATIVE_MELEE_ANIMATION_HPP

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <components/sceneutil/textkeymap.hpp>

namespace TES3MP::Native
{
    // Gameplay part of NpcAnimation::updateCarriedLeftVisible. The server
    // binds a CPU animation; shield sheathing is client presentation.
    bool carriedLeftVisibleForWeapon(int weaponType);

    // Detached CPU scheduling for one complete, non-looping directional melee
    // clip. Copy this value into a tick candidate; only install that copy after
    // durability. Events are proposals, never damage or presentation callbacks.
    // V21 binds its resource and persists the snapshot in the actor tick.
    // V22 validates contact in the host; V24 resolves the bounded NPC hit.
    class MeleeAnimation
    {
    public:
        enum class Phase { WindUp, Release, Follow, Complete };
        struct Snapshot
        {
            Phase mPhase = Phase::WindUp;
            float mTime = 0;
            float mStrength = 0;
            bool mReleased = false;
            bool mHit = false;
            bool operator==(const Snapshot&) const = default;
        };

        MeleeAnimation(const SceneUtil::TextKeyMap& keys, std::string group,
            std::string attack, float speed);
        float windUp() const;
        // Stock CharacterController plays one of the consecutive hit groups at
        // speed one. A missing group clears recovery on the next update.
        unsigned hitRecoveryTicks(unsigned group) const;
        unsigned hitRecoveryGroupCount() const { return mHitRecoveryCount; }
        // Supply the server-resolved strength (including zero on an accuracy
        // miss), as CharacterController::prepareHit does before release timing.
        // Repeated release cannot change an already chosen swing.
        bool release(float strength);
        std::optional<int> advance(float duration);
        const Snapshot& snapshot() const { return mState; }
        // Recovery accepts only a state reachable within this bound clip.
        // The caller checks the saved resource identity before restoring it.
        void restore(const Snapshot& state);

    private:
        struct Range { float mStart, mStop; };
        std::shared_ptr<const SceneUtil::TextKeyMap> mKeys;
        std::string mGroup;
        float mSpeed, mMinimumAttack, mMinimumHit;
        Range mWindUp, mRelease;
        std::array<Range, 3> mFollow;
        std::array<unsigned, 16> mHitRecoveryTicks{};
        unsigned mHitRecoveryCount = 0;
        Snapshot mState;
    };
}

#endif
