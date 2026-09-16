#include "test_allocations.hpp"
#include "transfer_save_codec.hpp"

#include <algorithm>
#include <limits>

namespace MWWorld::Testing
{
    void restorePair(
        const SerializedPair& input, const RestoreContent& content, std::unique_ptr<const RestoredPair>& output)
    {
        validateRestore(input, content);
        auto staged = std::make_unique<RestoredPair>();
        staged->mRestart = input.mRestart;
        const auto restore = [&](const SerializedInventory& saved, RestoredInventory& inventory) {
            inventory.mProposedIdentities = saved.mProposedIdentities;
            inventory.mViews.reserve(saved.mObjects.size());
            for (const auto& object : saved.mObjects)
            {
                const auto& base = suppliedBase(object.mRef.mRefID, content);
                inventory.mNodes.emplace_back(object.mRef, &base);
                auto& node = inventory.mNodes.back();
                node.mData = RefData::restore(object, base.mScript, content.mDeclarations);
                inventory.mViews.emplace_back(&node);
            }
        };
        restore(input.mSource, staged->mSource);
        restore(input.mDestination, staged->mDestination);
        static_assert(noexcept(output = std::move(staged)));
        output = std::move(staged);
    }

    DisposableTransferRehearsal::RestartBindings DisposableTransferRehearsal::restartBindings() const
    {
        RestartBindings result{ { mSourceOwner.getPtr(), mDestinationOwner.getPtr(), mOtherOwner.getPtr() },
            { &mSource, &mDestination, &mOther },
            { mSource.mStorageIdentity, mDestination.mStorageIdentity, mOther.mStorageIdentity }, {}, {} };
        if (otherStorage().size() > 1024)
            throw std::invalid_argument("Restart other-store bound exceeded");
        for (size_t i = 0; i < result.mStores.size(); ++i)
        {
            // Capture stock store witnesses while the caller owns this fixture;
            // preparation later uses only copies and cannot allocate a lazy token.
            const ContainerStoreResolution resolved(*result.mStores[i], result.mOwners[i]);
            result.mLifetimes[i] = result.mStores[i]->mResolutionLifetime;
        }
        result.mOther.reserve(otherStorage().size());
        for (const auto& node : otherStorage())
            result.mOther.emplace_back(node.mRef.getRefNum(), mModel.getPtr(node.mRef.getRefNum()));
        return result;
    }

    ConstPtr DisposableTransferRehearsal::RestartRegistry::getItem(ESM::RefNum id) const
    {
        // Neither an owner reference nor a retained storage token establishes
        // store liveness. Check each independent weak witness before addresses.
        for (const auto& owner : mFresh.mOwners)
            if (!owner.hasLiveReference())
                throw std::invalid_argument("Restart registry fixture lifetime changed");
        for (const auto& lifetime : mFresh.mLifetimes)
            if (lifetime.expired())
                throw std::invalid_argument("Restart registry store lifetime changed");
        for (size_t i = 0; i < mFresh.mStores.size(); ++i)
            if (mFresh.mStores[i]->mResolutionLifetime != mFresh.mLifetimes[i].lock()
                || mFresh.mStores[i]->mStorageIdentity != mFresh.mStorage[i])
                throw std::invalid_argument("Restart registry fixture storage changed");
        return mStorage->getItem(id);
    }

    void DisposableTransferRehearsal::prepareRestartRegistry(const SerializedPair& decoded, const RestoredPair& restored,
        const SaveEnvelope& envelope, const RestartBindings& fresh,
        std::unique_ptr<const RestartRegistry>& output) const
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (mActive)
            throw std::invalid_argument("Restart fixture already active");
        const auto valid = [](bool condition) {
            if (!condition)
                throw std::invalid_argument("Invalid detached restart registry binding or metadata");
        };
        const auto& metadata = decoded.mRestart;
        valid(metadata.mRevision != 0 && metadata.mRevision <= std::numeric_limits<size_t>::max()
            && metadata.mLastGenerated.mContentFile == -1 && metadata.mLastGenerated.mIndex != 0);
        valid(restored.mRestart == metadata);
        const auto identity = [&](ESM::RefNum id) {
            valid(id.isSet() && id.mContentFile >= -1
                && (id.mContentFile != -1 || id.mIndex <= metadata.mLastGenerated.mIndex));
        };
        const std::array inventories{ &restored.mSource, &restored.mDestination };
        const std::array saved{ &decoded.mSource, &decoded.mDestination };
        for (size_t side = 0; side < inventories.size(); ++side)
        {
            const auto* inventory = inventories[side];
            valid(inventory->mNodes.size() <= 1024
                && inventory->mNodes.size() == inventory->mProposedIdentities.size()
                && inventory->mNodes.size() == inventory->mViews.size());
            valid(saved[side]->mObjects.size() <= 1024
                && saved[side]->mObjects.size() == saved[side]->mProposedIdentities.size()
                && inventory->mProposedIdentities == saved[side]->mProposedIdentities);
        }
        valid(fresh.mOther.size() <= 1024 && fresh.mOther.size() == otherStorage().size());
        const std::array owners{ mSourceOwner.getPtr(), mDestinationOwner.getPtr(), mOtherOwner.getPtr() };
        const std::array stores{ &mSource, &mDestination, &mOther };
        const auto same = [](const Ptr& a, const Ptr& b) {
            return a.mRef == b.mRef && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore
                && a.getReferenceLifetime() == b.getReferenceLifetime();
        };
        for (size_t i = 0; i < owners.size(); ++i)
        {
            const auto& owner = fresh.mOwners[i];
            valid(owner.hasLiveReference() && same(owner, owners[i]) && fresh.mStores[i] == stores[i]
                && !fresh.mLifetimes[i].expired() && fresh.mLifetimes[i].lock() == stores[i]->mResolutionLifetime
                && fresh.mStorage[i] == stores[i]->mStorageIdentity);
            const auto id = owner.getCellRef().getRefNum();
            identity(id);
            valid(owner.mRef->mWorldModel == &mModel && same(mModel.getPtr(id), owner)
                && same(stores[i]->getPtr(mModel), owner));
            const auto& lists = stores[i]->mLists;
            valid(lists.mPotions.mList.empty() && lists.mAppas.mList.empty() && lists.mArmors.mList.empty()
                && lists.mBooks.mList.empty() && lists.mClothes.mList.empty() && lists.mIngreds.mList.empty()
                && lists.mLights.mList.empty() && lists.mLockpicks.mList.empty() && lists.mProbes.mList.empty()
                && lists.mRepairs.mList.empty() && lists.mWeapons.mList.empty()
                && (i == 2 || lists.mMiscItems.mList.empty()));
        }
        valid(envelope.mSourceOwner == owners[0].getCellRef().getRefNum()
            && envelope.mDestinationOwner == owners[1].getCellRef().getRefNum()
            && envelope.mSourceOwner != envelope.mDestinationOwner
            && (envelope.mInitiator == envelope.mSourceOwner || envelope.mInitiator == envelope.mDestinationOwner));
        identity(envelope.mInitiator);
        valid(mModel.mPtrRegistry.mIndex.size() == owners.size() + fresh.mOther.size());
        for (const auto& [id, item] : fresh.mOther)
        {
            identity(id);
            valid(item.hasLiveReference()); // Never inspect a saved pointer before its witness.
            valid(item.getCellRef().getRefNum() == id && item.mRef->mWorldModel == &mModel
                && item.mCell == nullptr && item.mContainerStore == &mOther && same(mModel.getPtr(id), item));
            valid(std::count_if(fresh.mOther.begin(), fresh.mOther.end(),
                      [&](const auto& entry) { return entry.first == id; }) == 1);
            valid(std::count_if(otherStorage().begin(), otherStorage().end(),
                      [&](const auto& node) { return &node == item.mRef; }) == 1);
        }
        // Equal bounded sizes plus unique exact membership ensure no current
        // owner/other mapping is omitted, including zero-count nodes.
        for (const auto* inventory : inventories)
        {
            auto node = inventory->mNodes.begin();
            for (size_t i = 0; i < inventory->mViews.size(); ++i, ++node)
            {
                const auto& view = inventory->mViews[i];
                const auto id = inventory->mProposedIdentities[i];
                identity(id);
                valid(view.hasLiveReference() && view.mRef == &*node && !view.mCell && !view.mContainerStore);
                valid(!node->mWorldModel && !node->mRef.getRefNum().isSet());
                size_t count = 0;
                for (const auto* values : inventories)
                    count += std::count(values->mProposedIdentities.begin(), values->mProposedIdentities.end(), id);
                valid(count == 1 && !mModel.mPtrRegistry.mIndex.contains(id));
            }
        }

        phase.set(Allocations::Phase::Preparation);
        auto candidate = std::make_unique<RestartRegistry>();
        candidate->mFresh = fresh;
        auto& bindings = candidate->mBindings;
        bindings.mRevision = static_cast<size_t>(metadata.mRevision);
        bindings.mLastGenerated = metadata.mLastGenerated; // Never rebuild from surviving IDs.
        const auto bind = [&](ESM::RefNum id, const Ptr& item) {
            bindings.mEntries.emplace(id, PtrRegistry::binding(item.mRef, item.mCell, item.mContainerStore));
        };
        for (const auto& owner : owners)
            bind(owner.getCellRef().getRefNum(), owner);
        for (const auto& [id, item] : fresh.mOther)
            bind(id, item);
        for (size_t side = 0; side < inventories.size(); ++side)
            for (size_t i = 0; i < inventories[side]->mViews.size(); ++i)
            {
                const auto& view = inventories[side]->mViews[i];
                bindings.mEntries.emplace(inventories[side]->mProposedIdentities[i],
                    PtrRegistry::binding(view.mRef, nullptr, stores[side]));
            }
        candidate->mStorage.reset(new PtrRegistry::PreparedStorage(bindings));
        auto& index = candidate->mStorage->mIndex;
        index.reserve(bindings.mEntries.size());
        for (const auto& owner : owners)
            index.emplace(owner.getCellRef().getRefNum(), owner);
        for (const auto& entry : fresh.mOther)
            index.emplace(entry);
        for (size_t side = 0; side < inventories.size(); ++side)
            for (size_t i = 0; i < inventories[side]->mViews.size(); ++i)
            {
                // Validated, lifetime-witnessed detached nodes only. No assignment
                // to their identities, WorldModel or engine state takes place.
                Ptr item(const_cast<LiveCellRefBase*>(inventories[side]->mViews[i].mRef));
                item.setContainerStore(const_cast<ContainerStore*>(stores[side]));
                index.emplace(inventories[side]->mProposedIdentities[i], item);
            }
        phase.set(Allocations::Phase::Publication);
        std::unique_ptr<const RestartRegistry> ready = std::move(candidate);
        static_assert(noexcept(output.swap(ready)));
        output.swap(ready);
    }
}
