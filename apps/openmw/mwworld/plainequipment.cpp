#include "plainequipment.hpp"

#include <limits>
#include <tuple>

#include "class.hpp"
#include "esmstore.hpp"
#include "inventorystore.hpp"
#include "worldmodel.hpp"

#include <components/esm3/objectstate.hpp>

namespace MWWorld
{
    namespace
    {
        bool sameReference(const ConstPtr& a, const ConstPtr& b)
        {
            return a == b && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore
                && a.getReferenceLifetime() == b.getReferenceLifetime();
        }

        void registered(const ConstPtr& ptr, const WorldModel& world)
        {
            if (!ptr.hasLiveReference())
                throw std::invalid_argument("Equipment reference lifetime expired");
            if (ptr.mRef->mWorldModel != &world || !ptr.getCellRef().getRefNum().isSet()
                || !sameReference(ptr, world.getPtr(ptr.getCellRef().getRefNum())))
                throw std::invalid_argument("Equipment reference registry binding changed");
        }

        auto cellValues(const ESM::CellRef& ref)
        {
            return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
                ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
                ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
                ref.mTrap, ref.mReferenceBlocked, ref.mPos);
        }

        bool sameCell(const CellRef& a, const CellRef& b)
        {
            ESM::ObjectState x, y;
            a.writeState(x);
            b.writeState(y);
            return a.hasChanged() == b.hasChanged() && cellValues(x.mRef) == cellValues(y.mRef);
        }

        LiveCellRef<ESM::Clothing> detached(const LiveCellRef<ESM::Clothing>& ref)
        {
            LiveCellRef<ESM::Clothing> value(ESM::makeBlankCellRef(), ref.mBase);
            value.mRef = ref.mRef;
            value.mData = ref.mData.copyForContainerTransfer();
            return value;
        }
    }

    struct PreparedPlainEquipment::State
    {
        struct Node
        {
            ConstPtr mLive;
            LiveCellRef<ESM::Clothing> mValue;
        };
        ContainerStoreResolution mResolution;
        PlainEquipmentContext mContext;
        std::weak_ptr<const void> mScriptsLifetime;
        PtrRegistry::Snapshot mRegistry;
        LocalScripts::List mScripts;
        std::vector<Node> mBefore;
        ESM::RefNum mShirt, mSelected;
        InventoryStoreListener* mEquipmentListener;
        ContainerStoreListener* mContainerListener;
        // Never copy an InventoryStore: it would retain live services, owner,
        // RefData aliases and iterators. Construct fresh storage and relocate slots.
        InventoryStore mCandidate;
        PlainEquipmentResult mResult;

        State(const ContainerStoreResolution& resolution, const PlainEquipmentContext& context)
            : mResolution(resolution)
            , mContext(context)
            , mScriptsLifetime(context.mLocalScripts.lifetimeWitness())
            , mEquipmentListener(nullptr)
            , mContainerListener(nullptr)
        {
        }

        static const InventoryStore& inventory(
            const ContainerStoreResolution& resolution, const PlainEquipmentContext& context)
        {
            const auto lifetime = resolution.mLifetime.lock();
            if (!lifetime || lifetime->mStore != resolution.mStore)
                throw std::invalid_argument("Equipment inventory lifetime expired");
            const auto& base = *resolution.mStore;
            if (base.mStorageIdentity != resolution.mStorage || base.mResolutionLifetime != lifetime)
                throw std::invalid_argument("Equipment inventory storage changed");
            registered(resolution.mOwner, context.mWorldModel);
            registered(context.mActor, context.mWorldModel);
            registered(context.mPlayer, context.mWorldModel);
            if (typeid(base) != typeid(InventoryStore) || !base.mResolved || !context.mActor.getClass().isNpc()
                || !sameReference(context.mActor, context.mPlayer) || !sameReference(resolution.mOwner, context.mActor)
                || !sameReference(base.getPtr(context.mWorldModel), context.mActor))
                throw std::invalid_argument("Equipment actor/player/resolved inventory mismatch");
            if (!context.mLocalScripts.usesStore(context.mStore))
                throw std::invalid_argument("Equipment script content service mismatch");
            const auto& inventory = static_cast<const InventoryStore&>(base);
            const auto& lists = base.mLists;
            if (!lists.mPotions.mList.empty() || !lists.mAppas.mList.empty() || !lists.mArmors.mList.empty()
                || !lists.mBooks.mList.empty() || !lists.mIngreds.mList.empty() || !lists.mLights.mList.empty()
                || !lists.mLockpicks.mList.empty() || !lists.mMiscItems.mList.empty() || !lists.mProbes.mList.empty()
                || !lists.mRepairs.mList.empty() || !lists.mWeapons.mList.empty()
                || lists.mClothes.mList.size() > MaxItems || !inventory.mUpdatesEnabled)
                throw std::invalid_argument(
                    "Equipment preparation requires bounded plain clothing storage and updates");
            return inventory;
        }

        static ESM::RefNum position(const InventoryStore& store, const ContainerStoreIterator& selection)
        {
            if (selection == store.end())
                return {};
            // Compare against current raw members. Never follow a saved iterator,
            // including a dormant, foreign, erased or replaced selection.
            for (auto it = store.mLists.mClothes.mList.begin(); it != store.mLists.mClothes.mList.end(); ++it)
                if (ConstContainerStoreIterator(&store, it) == selection)
                    return it->mRef.getRefNum();
            throw std::invalid_argument("Equipment iterator is not a current inventory member");
        }

        static void plain(
            const LiveCellRef<ESM::Clothing>& node, const InventoryStore& store, const PlainEquipmentContext& context)
        {
            ConstPtr item(&node, nullptr);
            item.mContainerStore = &store;
            registered(item, context.mWorldModel);
            if (context.mStore.get<ESM::Clothing>().search(node.mRef.getRefId()) != node.mBase
                || !node.mBase->mScript.empty() || !node.mBase->mEnchant.empty()
                || node.mBase->mData.mType != ESM::Clothing::Shirt || !node.mData.getLocals().getScriptId().empty()
                || !node.mData.getLocals().isEmpty() || node.mData.getLuaScripts() || node.mData.getCustomData()
                || node.mData.isDeletedByContentFile() || node.mRef.getCount(false) == std::numeric_limits<int>::min()
                || context.mLocalScripts.prepareRemove(&node.mRef).hasRegistration())
                throw std::invalid_argument("Equipment preparation supports only plain non-scripted shirts");
        }

        void capture(const InventoryStore& source)
        {
            mRegistry = mContext.mWorldModel.snapshotPtrRegistry();
            mScripts = mContext.mLocalScripts.snapshot();
            mEquipmentListener = source.mInventoryListener;
            mContainerListener = source.mListener;
            mShirt = position(source, source.mSlots[InventoryStore::Slot_Shirt]);
            mSelected = position(source, source.mSelectedEnchantItem);
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (slot != InventoryStore::Slot_Shirt && source.mSlots[slot] != source.end())
                    throw std::invalid_argument("Equipment preparation supports only the shirt slot");
            int64_t total = 0;
            for (const auto& ref : source.mLists.mClothes.mList)
            {
                plain(ref, source, mContext);
                total += std::abs(static_cast<int64_t>(ref.mRef.getCount(false)));
                if (total > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Equipment restack count bound exceeded");
                ConstPtr live(&ref, nullptr);
                live.mContainerStore = &source;
                mBefore.push_back({ live, detached(ref) });
                auto& node = mCandidate.mLists.mClothes.mList.emplace_back(detached(ref));
                auto it = ContainerStoreIterator(&mCandidate, std::prev(mCandidate.mLists.mClothes.mList.end()));
                if (node.mRef.getRefNum() == mShirt)
                {
                    if (node.mRef.getCount() != 1)
                        throw std::invalid_argument("Equipment shirt slot must contain one item");
                    mCandidate.mSlots[InventoryStore::Slot_Shirt] = it;
                }
                if (node.mRef.getRefNum() == mSelected)
                    mCandidate.mSelectedEnchantItem = it;
            }
            mCandidate.mResolved = true;
            mResult.mActor = mContext.mActor.getCellRef().getRefNum();
            mResult.mLastGenerated = mRegistry.mLastGenerated;
        }

        void check(const PlainEquipmentContext& context) const
        {
            if (mScriptsLifetime.expired() || &context.mStore != &mContext.mStore
                || &context.mWorldModel != &mContext.mWorldModel || &context.mLocalScripts != &mContext.mLocalScripts
                || !sameReference(context.mActor, mContext.mActor) || !sameReference(context.mPlayer, mContext.mPlayer))
                throw std::invalid_argument("Equipment preparation context/service changed");
            const auto& source = inventory(mResolution, context);
            if (context.mWorldModel.snapshotPtrRegistry() != mRegistry || context.mLocalScripts.snapshot() != mScripts
                || source.mInventoryListener != mEquipmentListener || source.mListener != mContainerListener
                || position(source, source.mSlots[InventoryStore::Slot_Shirt]) != mShirt
                || position(source, source.mSelectedEnchantItem) != mSelected
                || source.mLists.mClothes.mList.size() != mBefore.size())
                throw std::invalid_argument("Equipment preparation source state changed");
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (slot != InventoryStore::Slot_Shirt && source.mSlots[slot] != source.end())
                    throw std::invalid_argument("Equipment preparation other slot changed");
            size_t index = 0;
            for (const auto& ref : source.mLists.mClothes.mList)
            {
                const auto& before = mBefore[index++];
                ConstPtr current(&ref, nullptr);
                current.mContainerStore = &source;
                if (!before.mLive.hasLiveReference() || !sameReference(current, before.mLive))
                    throw std::invalid_argument("Equipment preparation item lifetime/membership changed");
                plain(ref, source, context);
                if (!sameCell(ref.mRef, before.mValue.mRef)
                    || !ref.mData.matchesContainerTransferState(before.mValue.mData))
                    throw std::invalid_argument("Equipment preparation item values changed");
            }
        }

        void effect(PlainEquipmentResult::EffectKind kind, const Ptr& item = {}, int count = 0)
        {
            mResult.mEffects.push_back(
                { kind, mResult.mActor, item.isEmpty() ? ESM::RefNum() : item.getCellRef().getRefNum(), count });
        }

        void run(ESM::RefNum identity, bool equip)
        {
            auto item = mCandidate.begin();
            while (item != mCandidate.end() && item->getCellRef().getRefNum() != identity)
                ++item;
            if (item == mCandidate.end() || (equip && mShirt == identity) || (!equip && mShirt != identity))
                throw std::invalid_argument("Equipment item is stale, dormant or already in requested state");
            using Kind = PlainEquipmentResult::EffectKind;
            InventoryStoreEquipmentContext context{
                { mContext.mStore,
                    [this](const Ptr& split) {
                        auto& counter = mResult.mLastGenerated;
                        if (counter.mContentFile >= 0
                            || (counter.mIndex == std::numeric_limits<uint32_t>::max()
                                && counter.mContentFile == std::numeric_limits<int32_t>::min()))
                            throw std::invalid_argument("Equipment identity counter exhausted or invalid");
                        const auto id = split.getCellRef().getOrAssignRefNum(counter);
                        if (mRegistry.mEntries.contains(id))
                            throw std::invalid_argument("Equipment split identity collision");
                        effect(Kind::RegisterSplit, split);
                    },
                    [this](const Ptr& original, int count) {
                        const auto removal = ContainerStore::prepareRemoveCount(original.getCellRef(), count);
                        if (removal.mFullRemoval)
                            throw std::logic_error("Equipment split unexpectedly removed a whole stack");
                        original.getCellRef() = original.getCellRef().copyWithCount(removal.mRemainingCount);
                        mCandidate.flagAsModified();
                        effect(Kind::InventoryUpdated);
                        if (mContainerListener)
                            effect(Kind::ItemRemoved, original, removal.mRemoved);
                        effect(Kind::InventoryUpdated);
                    },
                    [this](const Ptr& original) {
                        original.getCellRef() = original.getCellRef().copyWithCount(0);
                        effect(Kind::DeleteStackScript, original);
                    } },
                mContext.mActor, mContext.mPlayer,
                [](const Ptr&, const ESM::RefId&) { throw std::logic_error("Unexpected equipment script effect"); },
                [this](const Ptr&) { effect(Kind::EquipmentChanged); }
            };
            if (equip)
                mCandidate.equip(InventoryStore::Slot_Shirt, item, context);
            else
                mCandidate.unequipSlot(InventoryStore::Slot_Shirt, context);
            mResult.mShirt = position(mCandidate, mCandidate.mSlots[InventoryStore::Slot_Shirt]);
            mResult.mSelected = position(mCandidate, mCandidate.mSelectedEnchantItem);
            for (const auto& ref : mCandidate.mLists.mClothes.mList)
            {
                if (ref.mWorldModel || ref.mData.getBaseNode() || ref.mData.getLuaScripts()
                    || ref.mData.getCustomData())
                    throw std::logic_error("Equipment candidate retained a live service or mutable alias");
                mResult.mItems.push_back({ ref.mRef.getRefNum(), ref.mRef.getRefId(), ref.mRef.getCount(false) });
            }
        }
    };

    PreparedPlainEquipment::PreparedPlainEquipment(std::unique_ptr<State> state)
        : mState(std::move(state))
    {
    }
    PreparedPlainEquipment::PreparedPlainEquipment(PreparedPlainEquipment&&) noexcept = default;
    PreparedPlainEquipment& PreparedPlainEquipment::operator=(PreparedPlainEquipment&&) noexcept = default;
    PreparedPlainEquipment::~PreparedPlainEquipment() = default;

    PreparedPlainEquipment PreparedPlainEquipment::prepare(const ContainerStoreResolution& inventory,
        const ConstPtr& item, ESM::RefNum expectedIdentity, size_t expectedRegistryRevision, bool equip,
        const PlainEquipmentContext& context)
    {
        const auto& source = State::inventory(inventory, context);
        if (context.mWorldModel.getPtrRegistryRevision() != expectedRegistryRevision)
            throw std::invalid_argument("Equipment request registry revision changed");
        registered(item, context.mWorldModel);
        if (item.mContainerStore != &source || item.getCellRef().getRefNum() != expectedIdentity)
            throw std::invalid_argument("Equipment request item owner/identity mismatch");
        bool found = false;
        for (const auto& node : source.mLists.mClothes.mList)
            found = found || &node == item.mRef;
        if (!found)
            throw std::invalid_argument("Equipment request item is foreign");
        auto state = std::make_unique<State>(inventory, context);
        state->capture(source);
        state->run(expectedIdentity, equip);
        state->check(context);
        return PreparedPlainEquipment(std::move(state));
    }

    void PreparedPlainEquipment::validate(const PlainEquipmentContext& context) const
    {
        if (!mState)
            throw std::invalid_argument("Equipment preparation was moved");
        mState->check(context);
    }

    const PlainEquipmentResult& PreparedPlainEquipment::result() const
    {
        if (!mState)
            throw std::invalid_argument("Equipment preparation was moved");
        return mState->mResult;
    }
}
