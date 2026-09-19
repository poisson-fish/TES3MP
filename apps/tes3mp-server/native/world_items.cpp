#include "equipment_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    void EquipmentRuntime::validateWorldItems(const EquipmentSessionValues& values) const
    {
        if (bool(values.mWorldItems) != bool(mWorldItems))
            throw std::invalid_argument("World item domain changed");
        if (!values.mWorldItems) return;
        const auto& world = *values.mWorldItems;
        if (bool(values.mWorldCells) != bool(mWorldCells)
            || (values.mWorldCells && values.mWorldCells->size() != world.mObjects.size()))
            throw std::invalid_argument("World membership domain changed");
        if (world.mObjects.size() > mWorldCapacity || world.mNpcStats
            || world.mSelected.isSet() || std::ranges::any_of(world.mSlots, [](auto id) { return id.isSet(); }))
            throw std::invalid_argument("Invalid world item storage shape");
        world.validate(mStore, ownerPtr(0).getCellRef().getRefNum(), nullptr, mWorldCapacity);
        for (const auto& object : world.mObjects)
        {
            const auto& ref = object.mRef;
            if (values.mWorldCells)
            {
                const auto cell = values.mWorldCells->find(ref.mRefNum);
                const auto placedCell = mPlacedItemCells.find(ref.mRefNum);
                if (cell == values.mWorldCells->end() || cell->second >= mCellCount
                    || (ref.mRefNum.hasContentFile() && (placedCell == mPlacedItemCells.end() || placedCell->second != cell->second)))
                    throw std::invalid_argument("Saved world cell differs from bound content");
            }
            if (ref.mCount <= 0 || ref.mCount > 1000000)
                throw std::invalid_argument("Invalid active world count");
            for (float value : ref.mPos.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("World position outside wire bounds");
            if (ref.mRefNum.hasContentFile())
            {
                const auto placed = std::ranges::find(mPlacedItems, ref.mRefNum, &ESM::CellRef::mRefNum);
                if (placed == mPlacedItems.end() || !sameCellRef(ref, *placed))
                    throw std::invalid_argument("Saved placed item differs from bound content");
            }
        }
    }

    struct EquipmentRuntime::PreparedWorldTransfer::State
    {
        const EquipmentRuntime* mOwner;
        std::weak_ptr<const void> mLifetime;
        std::shared_ptr<const ESM::DoorState> mDoor;
        size_t mActor;
        uint64_t mBefore;
        std::unique_ptr<Installation> mInventory;
        PlainEquipmentValues mWorld;
        std::optional<EquipmentSessionValues::WorldCells> mCells;
        EquipmentBytes mImage;
    };
    EquipmentRuntime::PreparedWorldTransfer::PreparedWorldTransfer(std::unique_ptr<State> state) : mState(std::move(state)) {}
    EquipmentRuntime::PreparedWorldTransfer::PreparedWorldTransfer(PreparedWorldTransfer&&) noexcept = default;
    EquipmentRuntime::PreparedWorldTransfer::~PreparedWorldTransfer() = default;
    std::span<const char> EquipmentRuntime::PreparedWorldTransfer::image() const
    {
        if (!mState) throw std::invalid_argument("Consumed world transfer");
        return mState->mImage;
    }
    uint64_t EquipmentRuntime::PreparedWorldTransfer::revision() const
    {
        (void)image();
        return mState->mInventory->mRevision;
    }
    const PlainEquipmentValues& EquipmentRuntime::worldValues(const PreparedWorldTransfer* prepared) const
    {
        if (!mWorldItems) throw std::invalid_argument("World item domain unavailable");
        if (!prepared) return *mWorldItems;
        if (!prepared->mState || prepared->mState->mOwner != this
            || prepared->mState->mLifetime.lock() != mLifetime || prepared->mState->mDoor != mDoorState
            || prepared->mState->mBefore != mWorld.getPtrRegistryRevision())
            throw std::invalid_argument("Stale or foreign world transfer");
        return prepared->mState->mWorld;
    }
    PlainEquipmentValues EquipmentRuntime::preparedValues(const PreparedWorldTransfer& prepared, size_t owner) const
    {
        (void)worldValues(&prepared);
        return owner == prepared.mState->mActor ? prepared.mState->mInventory->mSaved : installedValues(owner);
    }
    uint8_t EquipmentRuntime::worldCell(ESM::RefNum ref, const PreparedWorldTransfer* prepared) const
    {
        (void)worldValues(prepared);
        const auto& cells = prepared ? prepared->mState->mCells : mWorldCells;
        return cells ? cells->at(ref) : 0;
    }
    EquipmentRuntime::PreparedWorldTransfer EquipmentRuntime::prepareWorldTransfer(size_t actor,
        InventoryInstanceId item, int count, bool pickup, ESM::Position position, uint64_t expected,
        const std::function<ESM::Position(const ESM::ObjectState&)>& placement, uint8_t cell)
    {
        if (actor >= 2 || !mConnected || mFailedClosed || mRestartActor || !mWorldItems
            || expected != mWorld.getPtrRegistryRevision() || count <= 0 || count > 1000000
            || cell >= mCellCount)
            throw std::invalid_argument("World transfer unavailable, stale or invalid");
        const ESM::RefNum identity{item.mIndex, item.mContentFile};
        const auto found = std::ranges::find(mWorldItems->mObjects, identity,
            [](const auto& object) { return object.mRef.mRefNum; });
        if ((pickup && (found == mWorldItems->mObjects.end() || worldCell(identity) != cell))
            || (!pickup && mWorldItems->mObjects.size() == mWorldCapacity))
            throw std::invalid_argument("World item missing or world capacity exhausted");
        for (size_t i = 0; i < ownerCount(); ++i) validateCaller(i, ownerPtr(i));
        auto [inventory, world] = PreparedPlainEquipment::prepareWorldTransfer(
            ContainerStoreResolution(storage(actor), ownerPtr(actor)), identity, expected, count,
            pickup ? &*found : nullptr, preparationContext(actor));
        auto state = std::make_unique<PreparedWorldTransfer::State>();
        state->mOwner = this;
        state->mLifetime = mLifetime;
        state->mDoor = mDoorState;
        state->mActor = actor;
        state->mBefore = expected;
        state->mInventory = stageInstallation(actor, ownerPtr(actor), std::move(inventory));
        state->mWorld = *mWorldItems;
        state->mCells = mWorldCells;
        if (pickup)
        {
            std::erase_if(state->mWorld.mObjects, [&](const auto& object) { return object.mRef.mRefNum == identity; });
            if (state->mCells) state->mCells->erase(identity);
        }
        else
        {
            if (placement) position = placement(world);
            world.mRef.mPos = position;
            world.mPosition = position;
            if (state->mCells) state->mCells->emplace(world.mRef.mRefNum, cell);
            state->mWorld.mObjects.push_back(std::move(world));
        }
        state->mWorld.mLastGenerated = state->mInventory->mSaved.mLastGenerated;
        EquipmentSessionValues values{{installedValues(0), installedValues(1)}, state->mInventory->mRevision};
        values.mActors[actor] = state->mInventory->mSaved;
        values.mWorldItems = state->mWorld;
        values.mWorldCells = state->mCells;
        encodeSession(std::move(values), state->mImage);
        return PreparedWorldTransfer(std::move(state));
    }
    PersistenceResult EquipmentRuntime::commit(PreparedWorldTransfer& prepared,
        EquipmentSessionCommitter& durability, EquipmentBytes& bytes)
    {
        if (mFailedClosed) return PersistenceResult::Uncertain;
        (void)worldValues(&prepared);
        if (mRestartActor) throw std::invalid_argument("World transfer during recovery");
        auto& state = *prepared.mState;
        for (size_t i = 0; i < ownerCount(); ++i) validateCaller(i, ownerPtr(i));
        auto& candidate = state.mInventory->mPrepared.installationCandidate(
            preparationContext(state.mActor), storage(state.mActor));
        const auto result = durability.commit(state.mImage);
        if (result != PersistenceResult::Accepted)
        {
            mFailedClosed = result == PersistenceResult::Uncertain;
            return result;
        }
        const auto install = [&]() noexcept {
            installPrepared(state.mActor, *state.mInventory, candidate);
            mWorld.mPtrRegistry.mIndex.swap(state.mInventory->mRegistry);
            mWorld.mPtrRegistry.mRevision = state.mInventory->mRevision;
            mWorld.mPtrRegistry.mLastGenerated = state.mWorld.mLastGenerated;
            mWorldItems->swap(state.mWorld);
            mWorldCells.swap(state.mCells);
            bytes.swap(state.mImage);
        };
        install();
        prepared.mState.reset();
        return result;
    }
}
