#include "test_allocations.hpp"
#include "transfer_save_codec.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/compiler/locals.hpp>

namespace MWWorld::Testing
{
    void restorePair(
        const SerializedPair& input, const RestoreContent& content, std::unique_ptr<const RestoredPair>& output)
    {
        validateRestore(input, content);
        auto staged = std::make_unique<RestoredPair>();
        staged->mRestart = input.mRestart;
        staged->mScripts = input.mScripts;
        const auto restore = [&](const SerializedInventory& saved, RestoredInventory& inventory) {
            inventory.mProposedIdentities = saved.mProposedIdentities;
            inventory.mSelection = saved.mSelection;
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

    void DisposableTransferRehearsal::validateRestartRegistry(const SerializedPair& decoded, const RestoredPair& restored,
        const SaveEnvelope& envelope, const RestartBindings& fresh) const
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (mActive || mRestartInstalled)
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
            valid(inventory->mSelection == saved[side]->mSelection);
            validateTransferSelection(inventory->mSelection, inventory->mProposedIdentities);
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
            // Empty receiving inventories cannot own a selection. Compare only;
            // never follow a possibly foreign or stale saved iterator.
            if (i != 2)
                valid(stores[i]->mSelectedEnchantItem == stores[i]->end());
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

    }

    void DisposableTransferRehearsal::prepareRestartRegistry(const SerializedPair& decoded, const RestoredPair& restored,
        const SaveEnvelope& envelope, const RestartBindings& fresh,
        std::unique_ptr<const RestartRegistry>& output) const
    {
        validateRestartRegistry(decoded, restored, envelope, fresh);
        Allocations::InPhase phase(Allocations::Phase::Preparation);
        const auto& metadata = decoded.mRestart;
        const std::array inventories{ &restored.mSource, &restored.mDestination };
        const auto& owners = fresh.mOwners;
        const auto& stores = fresh.mStores;
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
    DisposableTransferRehearsal::RestartScriptBindings DisposableTransferRehearsal::restartScriptBindings() const
    {
        if (mSourceAdd.mLocalScripts != &mSourceScripts || mOtherAdd.mLocalScripts != &mSourceScripts
            || (mDestinationAdd.mLocalScripts != &mSourceScripts
                && mDestinationAdd.mLocalScripts != &mDestinationScripts))
            throw std::invalid_argument("Restart script service association changed");
        RestartScriptBindings result;
        result.mRegistry = restartBindings();
        result.mServices = { &mSourceScripts, mDestinationAdd.mLocalScripts, &mSourceScripts };
        for (size_t i = 0; i < result.mServices.size(); ++i)
        {
            const auto& service = *result.mServices[i];
            if (service.mScripts.size() > MaxTransferScriptEntries)
                throw std::invalid_argument("Restart script service bound exceeded");
            result.mLifetimes[i] = service.mRestartLifetime.bind();
            result.mOriginal[i] = service.snapshot();
        }
        return result;
    }

    void DisposableTransferRehearsal::validateRestartScriptLifetimes(const RestartScriptBindings& fresh)
    {
        for (const auto& owner : fresh.mRegistry.mOwners)
            if (!owner.hasLiveReference())
                throw std::invalid_argument("Restart script owner lifetime changed");
        for (size_t i = 0; i < fresh.mRegistry.mStores.size(); ++i)
        {
            const auto& lifetime = fresh.mRegistry.mLifetimes[i];
            const auto* store = fresh.mRegistry.mStores[i];
            if (lifetime.expired() || !store || store->mResolutionLifetime != lifetime.lock()
                || store->mStorageIdentity != fresh.mRegistry.mStorage[i])
                throw std::invalid_argument("Restart script store lifetime/storage changed");
        }
        for (size_t i = 0; i < fresh.mServices.size(); ++i)
        {
            const auto* service = fresh.mServices[i];
            if (fresh.mLifetimes[i].expired() || !service
                || !service->mRestartLifetime.matches(fresh.mLifetimes[i].lock()))
                throw std::invalid_argument("Restart script service lifetime changed");
        }
    }

    const LocalScripts::PreparedStorage& DisposableTransferRehearsal::RestartScripts::getSourceStorage() const
    {
        validateRestartScriptLifetimes(mFresh);
        for (const auto& item : mItems)
            if (!item.hasLiveReference())
                throw std::invalid_argument("Restart script item lifetime changed");
        return *mStorage[0];
    }

    const LocalScripts::PreparedStorage& DisposableTransferRehearsal::RestartScripts::getDestinationStorage() const
    {
        const auto& source = getSourceStorage();
        return mShared ? source : *mStorage[1];
    }

    void DisposableTransferRehearsal::validateRestartScripts(const SerializedPair& decoded, const RestoredPair& restored,
        const RestoreContent& content, const SaveEnvelope& envelope, const RestartRegistry& registry,
        const RestartScriptBindings& fresh) const
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        // Reuse the complete registry input checks without constructing another
        // registry. No service address is followed before its exact role validates.
        validateRestartRegistry(decoded, restored, envelope, fresh.mRegistry);
        validateRestore(decoded, content);
        const auto valid = [](bool condition) {
            if (!condition)
                throw std::invalid_argument("Invalid detached restart script binding or metadata");
        };
        const auto& scripts = decoded.mScripts;
        valid(restored.mScripts == scripts);
        const std::array<const LocalScripts*, 3> services{
            &mSourceScripts, scripts.mShared ? &mSourceScripts : &mDestinationScripts, &mSourceScripts };
        valid(fresh.mServices == services && mSourceAdd.mLocalScripts == services[0]
            && mDestinationAdd.mLocalScripts == services[1] && mOtherAdd.mLocalScripts == services[2]);
        validateRestartScriptLifetimes(fresh);
        for (size_t role = 0; role < services.size(); ++role)
        {
            const auto& service = *services[role];
            const auto& original = fresh.mOriginal[role];
            valid(service.usesStore(mSourceAdd.mStore) && original.mEntries.size() <= MaxTransferScriptEntries
                && original.mEntries.size() == service.mScripts.size() && original.mCursor <= original.mEntries.size());
            size_t i = 0;
            size_t cursor = service.mScripts.size();
            for (auto it = service.mScripts.begin(); it != service.mScripts.end(); ++it, ++i)
            {
                const auto item = it->getItem();
                valid(item.hasLiveReference());
                valid(original.mEntries[i] == service.prepareRemove(&item.getCellRef()));
                if (it == service.mIter)
                    cursor = i;
            }
            valid(cursor == original.mCursor);
        }

        const auto& bindings = registry.mBindings;
        valid(registry.mStorage && bindings.mRevision == decoded.mRestart.mRevision
            && bindings.mLastGenerated == decoded.mRestart.mLastGenerated
            && registry.mStorage->mBindings == bindings && registry.mStorage->mResult == &bindings);
        const auto& rf = registry.mFresh;
        const auto& supplied = fresh.mRegistry;
        valid(rf.mStores == supplied.mStores && rf.mStorage == supplied.mStorage);
        // Compare lifetime identities, not only pointers, including empty stores.
        for (size_t i = 0; i < rf.mOwners.size(); ++i)
            valid(rf.mOwners[i].getReferenceLifetime() == supplied.mOwners[i].getReferenceLifetime()
                && !rf.mLifetimes[i].expired() && rf.mLifetimes[i].lock() == supplied.mLifetimes[i].lock());
        const std::array inventories{ &restored.mSource, &restored.mDestination };
        const std::array saved{ &decoded.mSource, &decoded.mDestination };
        const auto count = 3 + supplied.mOther.size() + restored.mSource.mNodes.size() + restored.mDestination.mNodes.size();
        valid(bindings.mEntries.size() == count && registry.mStorage->mIndex.size() == count);
        const auto exact = [&](ESM::RefNum id, const ConstPtr& expected, const ContainerStore* container) {
            const auto item = registry.getItem(id);
            const auto found = bindings.mEntries.find(id);
            valid(item.hasLiveReference() && item.mRef == expected.mRef && item.mCell == expected.mCell
                && item.mContainerStore == container && item.getReferenceLifetime() == expected.getReferenceLifetime()
                && found != bindings.mEntries.end() && found->second.references(item.mRef)
                && found->second.getCell() == item.mCell && found->second.getContainer() == container);
        };
        for (const auto& owner : supplied.mOwners)
            exact(owner.getCellRef().getRefNum(), owner, owner.mContainerStore);
        const auto itemState = [&](const ConstPtr& item, ESM::RefId baseId, bool configured) {
            const auto* node = item.get<ESM::Miscellaneous>();
            valid(node && node->mBase == &suppliedBase(baseId, content) && node->mRef.getRefId() == baseId);
            const auto& locals = node->mData.getLocals();
            valid(locals.getScriptId() == (configured ? node->mBase->mScript : ESM::RefId())
                && (!configured || !locals.getScriptId().empty()));
            valid(locals.mShorts.size() == (configured ? content.mDeclarations.get('s').size() : 0)
                && locals.mLongs.size() == (configured ? content.mDeclarations.get('l').size() : 0)
                && locals.mFloats.size() == (configured ? content.mDeclarations.get('f').size() : 0));
            for (const auto value : locals.mShorts)
                valid(value >= -32768 && value <= 32767);
            for (const auto value : locals.mFloats)
                valid(std::isfinite(value));
        };
        for (size_t side = 0; side < inventories.size(); ++side)
            for (size_t i = 0; i < inventories[side]->mViews.size(); ++i)
            {
                const auto& item = inventories[side]->mViews[i];
                exact(inventories[side]->mProposedIdentities[i], item, supplied.mStores[side]);
                itemState(item, saved[side]->mObjects[i].mRef.mRefID, saved[side]->mObjects[i].mHasLocals != 0);
            }
        valid(scripts.mOther.size() == supplied.mOther.size());
        for (const auto& [id, item] : supplied.mOther)
        {
            exact(id, item, supplied.mStores[2]);
            const auto found = std::find_if(scripts.mOther.begin(), scripts.mOther.end(),
                [&](const auto& value) { return value.mIdentity == id; });
            valid(found != scripts.mOther.end());
            itemState(item, found->mBase, found->mConfigured);
        }

    }

    void DisposableTransferRehearsal::prepareRestartScripts(const SerializedPair& decoded, const RestoredPair& restored,
        const RestoreContent& content, const SaveEnvelope& envelope, const RestartRegistry& registry,
        const RestartScriptBindings& fresh, std::unique_ptr<const RestartScripts>& output) const
    {
        validateRestartScripts(decoded, restored, content, envelope, registry, fresh);
        Allocations::InPhase phase(Allocations::Phase::Preparation);
        const auto& scripts = decoded.mScripts;
        const auto& supplied = fresh.mRegistry;
        const auto& services = fresh.mServices;
        const std::array inventories{ &restored.mSource, &restored.mDestination };
        const auto count = 3 + supplied.mOther.size() + restored.mSource.mNodes.size() + restored.mDestination.mNodes.size();
        auto candidate = std::make_unique<RestartScripts>();
        candidate->mFresh = fresh;
        candidate->mRestart = decoded.mRestart;
        candidate->mShared = scripts.mShared;
        candidate->mItems.reserve(count - 3);
        for (const auto* inventory : inventories)
            for (const auto& item : inventory->mViews)
                candidate->mItems.push_back(item);
        for (const auto& [id, item] : supplied.mOther)
            candidate->mItems.push_back(item);
        for (size_t side = 0; side < (scripts.mShared ? 1u : 2u); ++side)
        {
            const auto* service = services[side];
            const auto& savedService = scripts.mServices[side];
            LocalScripts::PreparedList list;
            list.mResult.mEntries.reserve(savedService.mEntries.size());
            std::vector<Ptr> items;
            items.reserve(savedService.mEntries.size());
            for (const auto& entry : savedService.mEntries)
            {
                // Validated stock Ptr copies preserve witnesses; no lazy capture,
                // locals initialization, live registration or script call occurs.
                const auto item = registry.mStorage->mIndex.at(entry.mIdentity);
                // Stock relocation creates immutable registration tokens through
                // prepareListAddition; prepareStorage owns the normal list nodes.
                service->prepareListAddition(list, { entry.mScript, nullptr }, &item.getCellRef(), item.mContainerStore);
                items.push_back(item);
            }
            list.mResult.mCursor = savedService.mCursor;
            candidate->mStorage[side] = service->prepareStorage(list.mResult, list.mResult, {}, items);
            candidate->mLists[side] = std::move(list.mResult);
        }
        phase.set(Allocations::Phase::Publication);
        std::unique_ptr<const RestartScripts> ready = std::move(candidate);
        static_assert(noexcept(output.swap(ready)));
        output.swap(ready);
    }

    void DisposableTransferRehearsal::validateRestartInstallation(const SerializedPair& decoded,
        const RestoredPair& restored, const SaveBindings& bindings, const RestartRegistry& registry,
        const RestartScripts& scripts) const
    {
        validateRestartScripts(decoded, restored, bindings.mContent, bindings.mEnvelope, registry, scripts.mFresh);
        const auto valid = [](bool condition) {
            if (!condition)
                throw std::invalid_argument("Invalid restart installation storage or binding");
        };
        valid(scripts.mRestart == decoded.mRestart && scripts.mShared == decoded.mScripts.mShared);
        valid(scripts.mItems.size()
            == restored.mSource.mViews.size() + restored.mDestination.mViews.size()
                + scripts.mFresh.mRegistry.mOther.size());
        size_t index = 0;
        const auto exact = [&](const ConstPtr& expected) {
            const auto& item = scripts.mItems[index++];
            valid(item.hasLiveReference() && item.mRef == expected.mRef && item.mCell == expected.mCell
                && item.mContainerStore == expected.mContainerStore
                && item.getReferenceLifetime() == expected.getReferenceLifetime());
        };
        for (const auto* inventory : { &restored.mSource, &restored.mDestination })
            for (const auto& item : inventory->mViews)
            {
                exact(item); // Includes configured but unregistered nodes.
                const auto& data = item.getRefData();
                valid(!data.getCustomData() && !data.getLuaScripts() && !data.getBaseNode() && !data.mPhysicsPostponed
                    && !data.isDeletedByContentFile());
            }
        for (const auto& [id, item] : scripts.mFresh.mRegistry.mOther)
            exact(item);
        for (size_t side = 0; side < 2; ++side)
        {
            if (side == 1 && scripts.mShared)
            {
                valid(!scripts.mStorage[side] && scripts.mLists[side].mEntries.empty()
                    && scripts.mLists[side].mCursor == 0);
                continue;
            }
            const auto& list = scripts.mLists[side];
            const auto& saved = decoded.mScripts.mServices[side];
            valid(scripts.mStorage[side] && list.mCursor == saved.mCursor
                && list.mEntries.size() == saved.mEntries.size());
            for (size_t i = 0; i < saved.mEntries.size(); ++i)
            {
                const auto item = registry.getItem(saved.mEntries[i].mIdentity);
                const auto& entry = list.mEntries[i];
                valid(entry.hasRegistration() && entry.references(&item.getCellRef())
                    && entry.getScript() == saved.mEntries[i].mScript && entry.getCell() == item.mCell
                    && entry.getContainer() == item.mContainerStore);
            }
            // Validates immutable registration identities, exact list nodes and
            // cursor address without following a saved iterator or stale pointer.
            scripts.mFresh.mServices[side]->validateStorage(*scripts.mStorage[side], list, list, {}, scripts.mItems);
        }
    }

    void DisposableTransferRehearsal::installRestart(std::span<const char> accepted, const SerializedPair& decoded,
        const SaveBindings& bindings, std::unique_ptr<const RestoredPair>& restored,
        std::unique_ptr<const RestartRegistry>& registry, std::unique_ptr<const RestartScripts>& scripts,
        std::unique_ptr<const SerializedPair>& output)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (!restored || !registry || !scripts || accepted.empty() || accepted.size() > MaxTransferSaveBytes)
            throw std::invalid_argument("Restart installation requires complete owned inputs and accepted bytes");
        validateRestartInstallation(decoded, *restored, bindings, *registry, *scripts);

        phase.set(Allocations::Phase::Result);
        auto staged = std::make_unique<SerializedPair>();
        staged->mRestart = restored->mRestart;
        staged->mScripts = restored->mScripts;
        const auto save = [&](const RestoredInventory& inventory, SerializedInventory& result) {
            result.mSelection = inventory.mSelection;
            serializeInventory(
                inventory.mNodes, [&](size_t i) { return inventory.mProposedIdentities[i]; },
                bindings.mContent.mDeclarations, result);
        };
        save(restored->mSource, staged->mSource);
        save(restored->mDestination, staged->mDestination);
        TransferSaveBytes encoded;
        encodeTransferSave(*staged, bindings, encoded);
        if (!std::equal(encoded.begin(), encoded.end(), accepted.begin(), accepted.end()))
            throw std::invalid_argument("Restored engine values differ from accepted restart save");
        encodeTransferSave(decoded, bindings, encoded);
        if (!std::equal(encoded.begin(), encoded.end(), accepted.begin(), accepted.end()))
            throw std::invalid_argument("Decoded values differ from accepted restart save");
        std::unique_ptr<const SerializedPair> ready = std::move(staged);

        phase.set(Allocations::Phase::Setup);
        // Only preparation-produced objects may be consumed. They were allocated
        // mutable and published const; mutation starts after all fallible work.
        auto& nodes = const_cast<RestoredPair&>(*restored);
        const std::array inventories{ &nodes.mSource, &nodes.mDestination };
        const std::array stores{ &mSource, &mDestination };
        std::array<std::vector<CellRef>, 2> references;
        std::array<std::shared_ptr<const ContainerStore::StorageIdentity>, 2> storageIdentities;
        std::array<std::optional<LocalScripts::PreparedStorage::Entries::iterator>, 2> cursors;
        // Stage the receiving owner with a raw node iterator that survives swap.
        // Public begin/++ would skip valid dormant selections.
        std::array selections{ mSource.end(), mDestination.end() };
        for (size_t side = 0; side < 2; ++side)
        {
            storageIdentities[side] = std::make_shared<const ContainerStore::StorageIdentity>();
            auto& inventory = *inventories[side];
            auto& refs = references[side];
            refs.reserve(inventory.mNodes.size());
            for (auto it = inventory.mNodes.begin(); it != inventory.mNodes.end(); ++it)
            {
                refs.push_back(it->mRef);
                refs.back().setRefNum(inventory.mProposedIdentities[refs.size() - 1]);
                if (inventory.mSelection.isSet() && refs.back().getRefNum() == inventory.mSelection)
                    selections[side] = ContainerStoreIterator(stores[side], it);
            }
            if (side == 1 && scripts->mShared)
                continue;
            auto& list = scripts->mStorage[side]->mEntries;
            const auto cursor = scripts->mLists[side].mCursor;
            if (cursor < list.size())
                cursors[side] = std::next(list.begin(), cursor);
        }
        phase.set(Allocations::Phase::Revalidation);
        validateRestartInstallation(decoded, *restored, bindings, *registry, *scripts);

        // Serialized access; no callback, validation, allocation or throwing
        // setter remains after the first write. Saved counters are assigned once.
        const auto install = [&]() noexcept {
            phase.set(Allocations::Phase::Installation);
            for (size_t side = 0; side < 2; ++side)
            {
                auto& inventory = *inventories[side];
                size_t i = 0;
                for (auto& node : inventory.mNodes)
                {
                    static_assert(std::is_nothrow_swappable_v<CellRef>);
                    std::swap(node.mRef, references[side][i++]);
                    node.mWorldModel = &mModel;
                }
                auto& store = *stores[side];
                static_assert(noexcept(store.mLists.mMiscItems.mList.swap(inventory.mNodes)));
                store.mLists.mMiscItems.mList.swap(inventory.mNodes);
                store.mStorageIdentity.swap(storageIdentities[side]);
                store.mSelectedEnchantItem = selections[side];
                store.mRechargingItems.clear();
                store.mWeightUpToDate = store.mRechargingItemsUpToDate = false;
                store.mModified = true;
            }
            for (size_t side = 0; side < (scripts->mShared ? 1u : 2u); ++side)
            {
                auto& service = side == 0 ? mSourceScripts : mDestinationScripts;
                static_assert(noexcept(service.mScripts.swap(scripts->mStorage[side]->mEntries)));
                service.mScripts.swap(scripts->mStorage[side]->mEntries);
                // End iterators do not survive list swap; materialize receiving end.
                service.mIter = cursors[side] ? *cursors[side] : service.mScripts.end();
            }
            auto& live = mModel.mPtrRegistry;
            static_assert(noexcept(live.mIndex.swap(registry->mStorage->mIndex)));
            live.mIndex.swap(registry->mStorage->mIndex);
            live.mRevision = static_cast<size_t>(decoded.mRestart.mRevision);
            live.mLastGenerated = decoded.mRestart.mLastGenerated;
            mRestartInstalled = true;

            phase.set(Allocations::Phase::Retirement);
            // Old registry/script nodes only reference retained owners/other items;
            // original source/destination storage was validated empty. Release all
            // borrowed iterators before destroying consumed list owners.
            cursors = {};
            selections = { mSource.end(), mDestination.end() };
            scripts.reset();
            registry.reset();
            restored.reset();
            references = {};
            storageIdentities = {};
            encoded.clear();
            phase.set(Allocations::Phase::Publication);
            static_assert(noexcept(output.swap(ready)));
            output.swap(ready);
            ready.reset();
        };
        install();
    }
}
