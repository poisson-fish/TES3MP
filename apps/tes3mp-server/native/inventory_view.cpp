#include "inventory_view.hpp"

#include "test_allocations.hpp"
#include "transfer_rehearsal.hpp"

#include <apps/openmw/mwworld/class.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace MWWorld::Testing
{
    void InventoryOwnerView::swap(InventoryOwnerView& other) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<InventoryInstanceId>);
        std::swap(mOwner, other.mOwner);
        std::swap(mSelection, other.mSelection);
        std::swap(mRevision, other.mRevision);
        static_assert(noexcept(mItems.swap(other.mItems)));
        mItems.swap(other.mItems);
    }

    void InventoryViewSnapshot::swap(InventoryViewSnapshot& other) noexcept
    {
        for (size_t side = 0; side < mOwners.size(); ++side)
            mOwners[side].swap(other.mOwners[side]);
    }

    void InventoryViewConsumer::receive(InventoryNotificationIntent intent)
    {
        const auto valid = [](bool condition) {
            if (!condition)
                throw std::invalid_argument("Inventory view requires authoritative resynchronization");
        };
        while (mNext < mSuccess.mNotifications.size() && !mSuccess.mNotifications[mNext])
            ++mNext;
        valid(mNext < mSuccess.mNotifications.size() && mSuccess.mNotifications[mNext] == intent);
        const auto& command = mSuccess.mCommand;
        valid(command.mExpectedRevision != std::numeric_limits<uint64_t>::max()
            && mSuccess.mRevision == command.mExpectedRevision + 1 && intent.mRevision == mSuccess.mRevision
            && intent.mInitiator == command.mInitiator && command.mSourceOwner != command.mDestinationOwner);
        const bool destination = intent.mOwner == command.mDestinationOwner;
        valid(destination || intent.mOwner == command.mSourceOwner);
        auto& view = mViews.mOwners[destination ? 1 : 0];
        valid(view.mOwner == intent.mOwner);
        if (intent.mKind == InventoryNotificationKind::InventoryUpdated)
        {
            valid(view.mRevision == command.mExpectedRevision && view.mItems.size() <= MaxTransferInventoryItems);
            const auto id = destination ? mSuccess.mDestinationItem : command.mItem;
            valid(intent.mItem == id);
            const auto found = std::find_if(view.mItems.begin(), view.mItems.end(),
                [&](const auto& item) { return item.mItem == id; });
            const bool added = found == view.mItems.end();
            valid(!added || (destination && view.mItems.size() < MaxTransferInventoryItems));
            const auto selection = destination ? mSuccess.mDestinationSelection : mSuccess.mSourceSelection;
            valid(selection == InventoryInstanceId{} || selection == id
                || std::any_of(view.mItems.begin(), view.mItems.end(),
                    [&](const auto& item) { return item.mItem == selection; }));
            InventoryOwnerView staged;
            staged.mOwner = view.mOwner;
            staged.mRevision = mSuccess.mRevision;
            staged.mSelection = selection;
            staged.mItems.reserve(view.mItems.size() + (added ? 1u : 0u));
            staged.mItems.insert(staged.mItems.end(), view.mItems.begin(), view.mItems.end());
            const auto count = destination ? mSuccess.mDestinationCount : mSuccess.mSourceCount;
            if (added)
                staged.mItems.push_back({ id, count });
            else
                staged.mItems[static_cast<size_t>(found - view.mItems.begin())].mCount = count;
            Allocations::InPhase phase(Allocations::Phase::Publication);
            view.swap(staged);
        }
        ++mNext;
    }

    void DisposableTransferRehearsal::snapshotInventoryViews(InventoryViewSnapshot& output) const
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (mActive)
            throw std::invalid_argument("Inventory snapshot requires an inactive installed fixture");
        const auto valid = [](bool condition) {
            if (!condition)
                throw std::invalid_argument("Invalid installed inventory snapshot binding or bound");
        };
        const auto owned = [](ESM::RefNum id) { return InventoryInstanceId{ id.mIndex, id.mContentFile }; };
        const auto counter = mModel.getLastGeneratedRefNum();
        const auto revision = mModel.getPtrRegistryRevision();
        valid(revision != 0 && counter.mContentFile == -1 && counter.mIndex != 0);
        const auto identity = [&](ESM::RefNum id) {
            valid(id.isSet() && id.mContentFile >= -1
                && (id.mContentFile != -1 || id.mIndex <= counter.mIndex));
        };
        const std::array owners{ mSourceOwner.getPtr(), mDestinationOwner.getPtr(), mOtherOwner.getPtr() };
        const std::array stores{ &mSource, &mDestination, &mOther };
        std::array<ESM::RefNum, 3> selections;
        size_t members = owners.size();
        for (size_t side = 0; side < stores.size(); ++side)
        {
            const auto& owner = owners[side];
            const auto& store = *stores[side]; // Fixture owns the current store, no saved address is followed.
            valid(owner.hasLiveReference());
            const auto id = owner.getCellRef().getRefNum();
            identity(id);
            const auto current = mModel.getPtr(id);
            valid(current.hasLiveReference() && current == owner
                && current.mCell == owner.mCell && current.mContainerStore == owner.mContainerStore
                && current.getReferenceLifetime() == owner.getReferenceLifetime()
                && owner.mRef->mWorldModel == &mModel);
            store.validateExplicitOwner(owner, mModel);
            const auto& lists = store.mLists;
            valid(lists.mPotions.mList.empty() && lists.mAppas.mList.empty() && lists.mArmors.mList.empty()
                && lists.mBooks.mList.empty() && lists.mClothes.mList.empty() && lists.mIngreds.mList.empty()
                && lists.mLights.mList.empty() && lists.mLockpicks.mList.empty() && lists.mProbes.mList.empty()
                && lists.mRepairs.mList.empty() && lists.mWeapons.mList.empty()
                && lists.mMiscItems.mList.size() <= MaxTransferInventoryItems);
            members += lists.mMiscItems.mList.size();
            for (const auto& node : lists.mMiscItems.mList)
            {
                const auto itemId = node.mRef.getRefNum();
                identity(itemId);
                const auto item = mModel.getPtr(itemId);
                // Validate the registry witness before following it. Reading a
                // current owned node needs no lazy Ptr/lifetime allocation.
                valid(item.hasLiveReference() && item.mRef == &node && item.mCell == nullptr
                    && item.mContainerStore == &store && node.mWorldModel == &mModel
                    && node.mBase && !item.getClass().isGold(item));
            }
            // Stock guard compares the selection to current raw iterators before
            // reading it, including dormant nodes. Never dereference a saved iterator.
            selections[side] = store.transferSelection();
        }
        valid(mModel.mPtrRegistry.mIndex.size() == members);

        phase.set(Allocations::Phase::Preparation);
        InventoryViewSnapshot staged;
        for (size_t side = 0; side < staged.mOwners.size(); ++side)
        {
            auto& view = staged.mOwners[side];
            view.mOwner = owned(owners[side].getCellRef().getRefNum());
            view.mSelection = owned(selections[side]);
            view.mRevision = revision;
            const auto& nodes = stores[side]->mLists.mMiscItems.mList;
            view.mItems.reserve(nodes.size());
            for (const auto& node : nodes)
                view.mItems.push_back({ owned(node.mRef.getRefNum()), node.mRef.getCount(false) });
        }
        phase.set(Allocations::Phase::Publication);
        output.swap(staged);
    }
}
