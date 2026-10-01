#ifndef TES3MP_NATIVE_ORDINARY_DOOR_HPP
#define TES3MP_NATIVE_ORDINARY_DOOR_HPP

#include <components/esm3/doorstate.hpp>
#include <components/esm3/loaddoor.hpp>

#include <functional>
#include <bit>
#include <cstdint>

namespace TES3MP::Native
{
    struct PreparedDoorChange
    {
        ESM::DoorState mState;
        // Fade the opposite sound over the stock 0.5 seconds after commit.
        ESM::RefId mPlaySound, mFadeSound, mStopSound;
        float mSoundOffset = 0;
    };

    // Exact bounded contact identity for the persisted angle, motion state and
    // lock. It survives recovery without turning the presentation motion counter
    // into a second gameplay revision.
    inline uint64_t doorContactRevision(const ESM::DoorState& state)
    {
        return ((uint64_t(std::bit_cast<uint32_t>(state.mPosition.rot[2])) << 12)
            | (uint64_t(state.mRef.mLockLevel) << 2) | uint64_t(state.mDoorState)) + 1;
    }

    // Preparation only: no live writer, network publication, file or singleton.
    // The canonical transaction commits position + motion together
    // with the inventory image before installing state or emitting these effects.
    class OrdinaryDoor
    {
        ESM::CellRef mPlacement;
        ESM::RefId mOpenSound, mCloseSound;

    public:
        OrdinaryDoor(const ESM::Door& base, const ESM::CellRef& placement);
        ESM::DoorState initialState() const;
        void validate(const ESM::DoorState& state) const;
        void validatePosition(const ESM::Position& position) const;
        PreparedDoorChange activate(const ESM::DoorState& state) const;
        enum class LockMagic { Lock, Open };
        // Stock Lock raises the level only; Open succeeds at or above the
        // current level. Stock ESM saves omit unlocked levels, so normalize
        // the transient negative value to its durable zero representation.
        // The returned state is detached until the owning door transaction commits.
        PreparedDoorChange applyLockMagic(const ESM::DoorState& state, LockMagic effect, int magnitude) const;

        // Query obstruction at the proposed transform without mutating live
        // physics. True cancels the whole step, preserving motion for retry.
        // M3 supplies aggregated fresh local-player reports, not verified server
        // physics. Replacing that source does not change the motion transaction.
        // Use MWWorld::doorContactBlocks for stock contact direction semantics.
        // An explicit query is required for every moving step; absence is never
        // treated as proof of a clear door. Idle doors are not advanced.
        using CollisionQuery = std::function<bool(const ESM::Position&, float delta)>;
        PreparedDoorChange advance(const ESM::DoorState& state, float seconds, const CollisionQuery& blocked) const;
        static constexpr float MaxStepSeconds = 1.f;
    };
}

#endif
