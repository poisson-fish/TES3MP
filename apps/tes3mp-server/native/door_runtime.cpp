#include "equipment_runtime.hpp"
#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    EquipmentRuntime::PreparedDoor::PreparedDoor(
        const EquipmentRuntime& owner, PreparedDoorChange change, bool activation, bool blocked)
        : mOwner(&owner), mLifetime(owner.mLifetime), mBefore(owner.mDoorState),
          mAfter(std::make_shared<const ESM::DoorState>(std::move(change.mState))),
          mRegistryRevision(owner.mWorld.getPtrRegistryRevision()),
          mMotion(owner.mDoorMotion + uint64_t(activation)), mBlocked(blocked)
    {
        EquipmentSessionValues values{{owner.installedValues(0), owner.installedValues(1)}, mRegistryRevision};
        values.mDoor = mAfter;
        owner.encodeSession(std::move(values), mImage);
    }

    EquipmentRuntime::PreparedDoor EquipmentRuntime::prepareDoor(bool activation, float seconds, bool blocked)
    {
        if (!mConnected || mFailedClosed || mRestartActor || !mDoorState || !mDoorBinding
            || (activation && mDoorMotion == std::numeric_limits<uint64_t>::max()))
            throw std::invalid_argument("Native door unavailable or motion identity exhausted");
        const auto& door = mDoorBinding->door();
        return PreparedDoor(*this, activation ? door.activate(*mDoorState)
            : door.advance(*mDoorState, seconds, [blocked](const auto&, float) { return blocked; }),
            activation, !activation && blocked && mDoorState->mDoorState != 0);
    }

    PersistenceResult EquipmentRuntime::commit(PreparedDoor& prepared,
        EquipmentSessionCommitter& durability, EquipmentBytes& bytes)
    {
        if (mFailedClosed) return PersistenceResult::Uncertain;
        if (mRestartActor || prepared.mOwner != this || prepared.mLifetime.lock() != mLifetime
            || prepared.mBefore != mDoorState || prepared.mRegistryRevision != mWorld.getPtrRegistryRevision()
            || prepared.mImage.empty()) throw std::invalid_argument("Stale or consumed native door transaction");
        const auto result = durability.commit(prepared.mImage);
        if (result != PersistenceResult::Accepted)
        {
            mFailedClosed = result == PersistenceResult::Uncertain;
            return result;
        }
        mDoorState.swap(prepared.mAfter);
        mDoorMotion = prepared.mMotion;
        mDoorBlocked = prepared.mBlocked;
        bytes.swap(prepared.mImage);
        return result;
    }
}
