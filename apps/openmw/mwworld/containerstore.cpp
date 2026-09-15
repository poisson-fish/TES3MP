#include "containerstore.hpp"
#include "inventorystore.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>

#include <components/debug/debuglog.hpp>
#include <components/esm3/inventorystate.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/levelledlist.hpp"
#include "../mwmechanics/recharge.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "class.hpp"
#include "esmstore.hpp"
#include "localscripts.hpp"
#include "manualref.hpp"
#include "player.hpp"
#include "refdata.hpp"
#include "worldmodel.hpp"

namespace
{
    std::unique_ptr<MWWorld::LiveCellRef<ESM::Miscellaneous>> copyContainerTransferItem(const MWWorld::ConstPtr& item)
    {
        auto data = item.getRefData().copyForContainerTransfer();
        // Never copy LiveCellRefBase: even its destructor would retain a live
        // WorldModel link. Copy only values into a fresh unregistered reference.
        auto prepared = std::make_unique<MWWorld::LiveCellRef<ESM::Miscellaneous>>(
            ESM::makeBlankCellRef(), item.get<ESM::Miscellaneous>()->mBase);
        prepared->mRef = item.getCellRef();
        prepared->mRef.unsetRefNum();
        prepared->mData = std::move(data);
        return prepared;
    }

    auto miscTransferValues(const MWWorld::CellRef& ref)
    {
        // Identity is checked separately. These are owned MISC values, including
        // ownership fields that destination normalization would later reset.
        return std::tuple{ ref.getRefId(), ref.getCount(false), ref.getSoul(), ref.getCharge(),
            ref.getChargeIntRemainder(), ref.getEnchantmentCharge(), ref.getOwner(), ref.getGlobalVariable(),
            ref.getFaction(), ref.getFactionRank(), ref.getScale(), ref.getPosition() };
    }

    void addScripts(MWWorld::ContainerStore& store, MWWorld::CellStore* cell)
    {
        auto& scripts = MWBase::Environment::get().getWorld()->getLocalScripts();
        for (const auto&& ptr : store)
        {
            const auto& script = ptr.getClass().getScript(ptr);
            if (!script.empty())
            {
                MWWorld::Ptr item = ptr;
                item.mCell = cell;
                scripts.add(script, item);
            }
        }
    }

    template <typename T>
    float getTotalWeight(const MWWorld::CellRefList<T>& cellRefList)
    {
        float sum = 0;

        for (const MWWorld::LiveCellRef<T>& liveCellRef : cellRefList.mList)
        {
            if (const int count = liveCellRef.mRef.getCount(); count > 0)
                sum += count * liveCellRef.mBase->mData.mWeight;
        }

        return sum;
    }

    template <typename T>
    MWWorld::Ptr searchId(MWWorld::CellRefList<T>& list, const ESM::RefId& id, MWWorld::ContainerStore* store)
    {
        store->resolve();

        for (MWWorld::LiveCellRef<T>& liveCellRef : list.mList)
        {
            if ((liveCellRef.mBase->mId == id) && liveCellRef.mRef.getCount())
            {
                MWWorld::Ptr ptr(&liveCellRef, nullptr);
                ptr.setContainerStore(store);
                return ptr;
            }
        }

        return MWWorld::Ptr();
    }
}

MWWorld::ResolutionListener::~ResolutionListener()
{
    try
    {
        mStore.unresolve();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "Failed to clear temporary container contents: " << e.what();
    }
}

template <typename T>
MWWorld::ContainerStoreIterator MWWorld::ContainerStore::getState(
    CellRefList<T>& collection, const ESM::ObjectState& state)
{
    if (!LiveCellRef<T>::checkState(state))
        return ContainerStoreIterator(this); // not valid anymore with current content files -> skip

    const T* record = MWBase::Environment::get().getESMStore()->get<T>().search(state.mRef.mRefID);

    if (!record)
        return ContainerStoreIterator(this);

    LiveCellRef<T> ref(ESM::makeBlankCellRef(), record);
    ref.load(state);
    collection.mList.push_back(std::move(ref));
    auto it = ContainerStoreIterator(this, --collection.mList.end());
    MWBase::Environment::get().getWorldModel()->registerPtr(*it);

    return it;
}

void MWWorld::ContainerStore::storeEquipmentState(
    const MWWorld::LiveCellRefBase& ref, size_t index, ESM::InventoryState& inventory) const
{
    if (mSelectedEnchantItem.getType() != -1 && mSelectedEnchantItem->getBase() == &ref)
        inventory.mSelectedEnchantItem = static_cast<uint32_t>(index);
}

void MWWorld::ContainerStore::readEquipmentState(
    const MWWorld::ContainerStoreIterator& iter, size_t index, const ESM::InventoryState& inventory)
{
    if (index == inventory.mSelectedEnchantItem)
        mSelectedEnchantItem = iter;
}

std::ptrdiff_t MWWorld::ContainerStore::index(const ContainerStoreIterator& iter) const
{
    // A count of 0 means the item is being removed/teleported by Lua
    if (iter.getType() == -1 || !iter->getCellRef().getCount())
        return -1;
    return std::distance(cbegin(), ConstContainerStoreIterator(iter));
}

template <typename T>
void MWWorld::ContainerStore::storeState(const LiveCellRef<T>& ref, ESM::ObjectState& state) const
{
    ref.save(state);
}

template <typename T>
void MWWorld::ContainerStore::storeStates(
    const CellRefList<T>& collection, ESM::InventoryState& inventory, size_t& index, bool equipable) const
{
    for (const LiveCellRef<T>& liveCellRef : collection.mList)
    {
        if (liveCellRef.mRef.getCount() == 0)
            continue;
        ESM::ObjectState state;
        storeState(liveCellRef, state);
        if (equipable)
            storeEquipmentState(liveCellRef, index, inventory);
        inventory.mItems.push_back(std::move(state));
        ++index;
    }
}

const ESM::RefId MWWorld::ContainerStore::sGoldId = ESM::RefId::stringRefId("gold_001");

MWWorld::ContainerStore::ContainerStore()
    : mSelectedEnchantItem(end())
{
}

MWWorld::ContainerStore::ContainerStore(const MWWorld::ContainerStore& store)
    : mListener(store.mListener)
    , mSelectedEnchantItem(end())
    , mLists(store.mLists)
    , mCachedWeight(store.mCachedWeight)
    , mSeed(store.mSeed)
    , mPtr(store.mPtr)
    , mResolutionListener(store.mResolutionListener)
    , mWeightUpToDate(store.mWeightUpToDate)
    , mModified(store.mModified)
    , mResolved(store.mResolved)
    , mRechargingItemsUpToDate(false)
{
    const std::ptrdiff_t distance = store.index(store.mSelectedEnchantItem);
    if (distance != -1)
    {
        mSelectedEnchantItem = begin();
        std::advance(mSelectedEnchantItem, distance);
    }
}

MWWorld::ContainerStore::ContainerStore(MWWorld::ContainerStore&& store)
    : mListener(store.mListener)
    , mSelectedEnchantItem(end())
    , mCachedWeight(store.mCachedWeight)
    , mSeed(store.mSeed)
    , mPtr(store.mPtr)
    , mResolutionListener(std::move(store.mResolutionListener))
    , mWeightUpToDate(store.mWeightUpToDate)
    , mModified(store.mModified)
    , mResolved(store.mResolved)
    , mRechargingItemsUpToDate(false)
{
    store.mStorageIdentity = std::make_shared<const StorageIdentity>();
    const std::ptrdiff_t distance = store.index(store.mSelectedEnchantItem);
    mLists = std::move(store.mLists);
    if (distance != -1)
    {
        mSelectedEnchantItem = begin();
        std::advance(mSelectedEnchantItem, distance);
    }
}

MWWorld::ContainerStore& MWWorld::ContainerStore::operator=(const ContainerStore& store)
{
    if (this == &store)
        return *this;
    mStorageIdentity = std::make_shared<const StorageIdentity>();
    mListener = store.mListener;
    mLists = store.mLists;
    mCachedWeight = store.mCachedWeight;
    mSeed = store.mSeed;
    mPtr = store.mPtr;
    mResolutionListener = store.mResolutionListener;
    mWeightUpToDate = store.mWeightUpToDate;
    mModified = store.mModified;
    mResolved = store.mResolved;
    mRechargingItemsUpToDate = false;
    const std::ptrdiff_t distance = store.index(store.mSelectedEnchantItem);
    if (distance != -1)
    {
        mSelectedEnchantItem = begin();
        std::advance(mSelectedEnchantItem, distance);
    }
    else
        mSelectedEnchantItem = end();
    return *this;
}

MWWorld::ContainerStore& MWWorld::ContainerStore::operator=(ContainerStore&& store)
{
    if (this == &store)
        return *this;
    auto identity = std::make_shared<const StorageIdentity>();
    auto sourceIdentity = std::make_shared<const StorageIdentity>();
    mStorageIdentity = std::move(identity);
    store.mStorageIdentity = std::move(sourceIdentity);
    const std::ptrdiff_t distance = store.index(store.mSelectedEnchantItem);
    mListener = store.mListener;
    mLists = std::move(store.mLists);
    mCachedWeight = store.mCachedWeight;
    mSeed = store.mSeed;
    mPtr = store.mPtr;
    mResolutionListener = std::move(store.mResolutionListener);
    mWeightUpToDate = store.mWeightUpToDate;
    mModified = store.mModified;
    mResolved = store.mResolved;
    mRechargingItemsUpToDate = false;
    if (distance != -1)
    {
        mSelectedEnchantItem = begin();
        std::advance(mSelectedEnchantItem, distance);
    }
    else
        mSelectedEnchantItem = end();
    return *this;
}

MWWorld::ConstContainerStoreIterator MWWorld::ContainerStore::cbegin(int mask) const
{
    return ConstContainerStoreIterator(mask, this);
}

MWWorld::ConstContainerStoreIterator MWWorld::ContainerStore::cend() const
{
    return ConstContainerStoreIterator(this);
}

MWWorld::ConstContainerStoreIterator MWWorld::ContainerStore::begin(int mask) const
{
    return cbegin(mask);
}

MWWorld::ConstContainerStoreIterator MWWorld::ContainerStore::end() const
{
    return cend();
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::begin(int mask)
{
    return ContainerStoreIterator(mask, this);
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::end()
{
    return ContainerStoreIterator(this);
}

int MWWorld::ContainerStore::count(const ESM::RefId& id) const
{
    int total = 0;
    for (const auto&& iter : *this)
        if (iter.getCellRef().getRefId() == id)
            total += iter.getCellRef().getCount();
    return total;
}

void MWWorld::ContainerStore::updateRefNums()
{
    for (const auto& iter : *this)
    {
        iter.getCellRef().unsetRefNum();
        iter.getRefData().setLuaScripts(nullptr);
        MWBase::Environment::get().getWorldModel()->registerPtr(iter);
    }
}

MWWorld::ContainerStoreListener* MWWorld::ContainerStore::getContListener() const
{
    return mListener;
}

void MWWorld::ContainerStore::setContListener(MWWorld::ContainerStoreListener* listener)
{
    mListener = listener;
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::unstack(const Ptr& ptr, int count)
{
    resolve();
    if (ptr.getCellRef().getCount() <= count)
        return end();
    MWWorld::ContainerStoreIterator it = addNewStack(ptr, subtractItems(ptr.getCellRef().getCount(false), count));

    MWWorld::Ptr newPtr = *it;
    newPtr.getCellRef().unsetRefNum();
    newPtr.getRefData().setLuaScripts(nullptr);
    MWBase::Environment::get().getWorldModel()->registerPtr(newPtr);

    const ESM::RefId& script = it->getClass().getScript(*it);
    if (!script.empty())
        MWBase::Environment::get().getWorld()->getLocalScripts().add(script, *it);

    remove(ptr, ptr.getCellRef().getCount() - count);

    return it;
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::restack(const MWWorld::Ptr& item)
{
    resolve();
    MWWorld::ContainerStoreIterator retval = end();
    for (MWWorld::ContainerStoreIterator iter(begin()); iter != end(); ++iter)
    {
        if (item == *iter)
        {
            retval = iter;
            break;
        }
    }

    if (retval == end())
        throw std::runtime_error("item is not from this container");

    for (MWWorld::ContainerStoreIterator iter(begin()); iter != end(); ++iter)
    {
        if (stacks(*iter, item))
        {
            iter->getCellRef().setCount(
                addItems(iter->getCellRef().getCount(false), item.getCellRef().getCount(false)));
            item.getCellRef().setCount(0);
            retval = iter;
            break;
        }
    }
    return retval;
}

bool MWWorld::ContainerStore::stacks(const ConstPtr& ptr1, const ConstPtr& ptr2) const
{
    return stacks(ptr1, ptr2, *MWBase::Environment::get().getESMStore());
}

bool MWWorld::ContainerStore::stacks(const ConstPtr& ptr1, const ConstPtr& ptr2, const ESMStore& store) const
{
    const MWWorld::Class& cls1 = ptr1.getClass();
    const MWWorld::Class& cls2 = ptr2.getClass();

    if (!(ptr1.getCellRef().getRefId() == ptr2.getCellRef().getRefId()))
        return false;

    // If it has an enchantment, don't stack when some of the charge is already used
    if (!ptr1.getClass().getEnchantment(ptr1).empty())
    {
        const ESM::Enchantment* enchantment = store.get<ESM::Enchantment>().find(ptr1.getClass().getEnchantment(ptr1));
        const float maxCharge = static_cast<float>(MWMechanics::getEnchantmentCharge(*enchantment));
        float enchantCharge1
            = ptr1.getCellRef().getEnchantmentCharge() == -1 ? maxCharge : ptr1.getCellRef().getEnchantmentCharge();
        float enchantCharge2
            = ptr2.getCellRef().getEnchantmentCharge() == -1 ? maxCharge : ptr2.getCellRef().getEnchantmentCharge();
        if (enchantCharge1 != maxCharge || enchantCharge2 != maxCharge)
            return false;
    }

    return ptr1 != ptr2 // an item never stacks onto itself
        && ptr1.getCellRef().getSoul() == ptr2.getCellRef().getSoul()

        && ptr1.getClass().getRemainingUsageTime(ptr1) == ptr2.getClass().getRemainingUsageTime(ptr2)

        // Items with scripts never stack
        && cls1.getScript(ptr1).empty()
        && cls2.getScript(ptr2).empty()

        // item that is already partly used up never stacks
        && (!cls1.hasItemHealth(ptr1)
            || (cls1.getItemHealth(ptr1) == cls1.getItemMaxHealth(ptr1)
                && cls2.getItemHealth(ptr2) == cls2.getItemMaxHealth(ptr2)));
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::add(const ESM::RefId& id, int count, bool allowAutoEquip)
{
    MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), id, count);
    return add(ref.getPtr(), count, allowAutoEquip);
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::add(
    const ConstPtr& itemPtr, int count, bool /*allowAutoEquip*/, bool resolve)
{
    auto& environment = MWBase::Environment::get();
    const ContainerStoreAddContext context{ *environment.getESMStore(), *environment.getWorldModel(),
        environment.getWorld()->getPlayerPtr(), getPtr(), &environment.getWorld()->getLocalScripts(),
        environment.getScriptManager(),
        [&environment](const Ptr& owner) { environment.getWindowManager()->inventoryUpdated(owner); } };
    return addWithContext(itemPtr, count, context, resolve);
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::add(
    const ConstPtr& itemPtr, int count, const ContainerStoreAddContext& context)
{
    validateExplicitOwner(context.mContainer, context.mWorldModel);
    if (count <= 0)
        throw std::invalid_argument("Explicit add count must be positive");
    getType(itemPtr);
    const auto& id = itemPtr.getClass().isGold(itemPtr) ? sGoldId : itemPtr.getCellRef().getRefId();
    // Bound the aggregate as well as each stack: count(id) also returns an int.
    std::int64_t total = count;
    for (const auto& item : *this)
        if (item.getCellRef().getRefId() == id)
        {
            total += std::abs(static_cast<std::int64_t>(item.getCellRef().getCount(false)));
            if (total > std::numeric_limits<int>::max())
                throw std::invalid_argument("Explicit add count would overflow inventory");
        }
    return addWithContext(itemPtr, count, context, true);
}

MWWorld::Ptr MWWorld::ContainerStore::getPtr(const WorldModel& worldModel) const
{
    return worldModel.getPtr(mPtr.id());
}

void MWWorld::ContainerStore::setPtr(const Ptr& ptr, const WorldModel& worldModel)
{
    if (ptr.isEmpty() || !ptr.getCellRef().getRefNum().isSet()
        || worldModel.getPtr(ptr.getCellRef().getRefNum()) != ptr)
        throw std::invalid_argument("Explicit container owner must be registered");
    mPtr = SafePtr(ptr.getCellRef().getRefNum());
}

void MWWorld::ContainerStore::validateExplicitOwner(const ConstPtr& owner, const WorldModel& worldModel) const
{
    if (typeid(*this) != typeid(ContainerStore))
        throw std::logic_error("Explicit context does not yet support InventoryStore or other derived stores");
    if (!mResolved)
        throw std::logic_error("Explicit context does not yet support unresolved container contents");
    if (owner.isEmpty() || getPtr(worldModel) != owner)
        throw std::invalid_argument("Explicit container owner mismatch");
}

std::unique_ptr<MWWorld::LiveCellRef<ESM::Miscellaneous>> MWWorld::ContainerStore::prepareTransferItem(
    const ConstPtr& item, int count, const ContainerStore& destination, const ConstPtr& sourceOwner,
    const ConstPtr& destinationOwner, const WorldModel& worldModel) const
{
    validateExplicitOwner(sourceOwner, worldModel);
    destination.validateExplicitOwner(destinationOwner, worldModel);
    if (this == &destination || sourceOwner == destinationOwner)
        throw std::invalid_argument("Container transfer preparation requires two distinct owners and stores");
    validateTransferSource(item, count, worldModel);
    destination.validateTransferCount(item, count);

    auto prepared = copyContainerTransferItem(item);
    prepared->mRef.setCount(count); // Strictly positive: no global zero-count cleanup.
    return prepared;
}

void MWWorld::ContainerStore::validateTransferSource(
    const ConstPtr& item, int count, const WorldModel& worldModel) const
{
    if (count <= 0)
        throw std::invalid_argument("Container transfer preparation count must be positive");
    if (item.isEmpty() || item.getContainerStore() != this || std::find(begin(), end(), item) == end()
        || item.mRef->isDeleted())
        throw std::invalid_argument("Container transfer preparation item ownership mismatch");
    const auto available = std::abs(static_cast<std::int64_t>(item.getCellRef().getCount(false)));
    if (available > std::numeric_limits<int>::max() || count > available)
        throw std::invalid_argument("Container transfer preparation item count is invalid");
    if (item.mRef->mWorldModel != &worldModel || !item.getCellRef().getRefNum().isSet()
        || worldModel.getPtr(item.getCellRef().getRefNum()) != item)
        throw std::invalid_argument("Container transfer preparation item must be registered");
    if (item.getType() != ESM::Miscellaneous::sRecordId || item.getClass().isGold(item))
        throw std::logic_error("Container transfer preparation currently requires non-gold MISC");
    if (item.getRefData().getLocals().getScriptId() != item.getClass().getScript(item))
        throw std::logic_error("Container transfer preparation requires matching initialized script locals");
}

MWWorld::PreparedContainerRemove MWWorld::ContainerStore::prepareTransferRemove(const ConstPtr& item, int count,
    const ConstPtr& sourceOwner, const WorldModel& worldModel, const LocalScripts* localScripts) const
{
    validateExplicitOwner(sourceOwner, worldModel);
    if (getPtr(worldModel).mCell != sourceOwner.mCell)
        throw std::invalid_argument("Container removal preparation owner cell mismatch");
    validateTransferSource(item, count, worldModel);
    if (!item.getClass().getScript(item).empty() && !localScripts)
        throw std::logic_error("Container removal preparation requires LocalScripts for scripted items");

    PreparedContainerRemove prepared;
    prepared.mSource = this;
    prepared.mWorldModel = &worldModel;
    prepared.mItemReference = item.mRef;
    prepared.mItemIdentity = item.getCellRef().getRefNum();
    prepared.mOwnerReference = sourceOwner.mRef;
    prepared.mOwnerIdentity = getPtr(worldModel).getCellRef().getRefNum();
    prepared.mOwnerCell = getPtr(worldModel).mCell;
    prepared.mCount = count;
    prepared.mRemainingCount = prepareRemoveCount(item.getCellRef(), count).mRemainingCount;
    // Keep the original signed count in the witness. In particular, never apply
    // a full-removal zero through CellRef::setCount (which cleans live scripts).
    prepared.mItemState = copyContainerTransferItem(item);
    if (localScripts)
    {
        prepared.mLocalScripts = localScripts;
        // Zero-count cleanup uses this same first-match lookup even if no script
        // is currently registered. Capture absence too, and bind partial removal
        // to the registration it must preserve. No cursor is retained or read.
        prepared.mScriptState = localScripts->prepareRemove(&item.getCellRef());
        if (prepared.mScriptState->hasRegistration()
            && (prepared.mScriptState->getScript() != item.getClass().getScript(item)
                || prepared.mScriptState->getContainer() != this))
            throw std::invalid_argument("Container removal preparation script registration mismatch");
    }
    return prepared;
}

void MWWorld::ContainerStore::validateTransferRemoval(const PreparedContainerRemove& prepared,
    const ConstPtr& sourceOwner, const WorldModel& worldModel, const LocalScripts* localScripts) const
{
    validateExplicitOwner(sourceOwner, worldModel);
    const auto owner = worldModel.getPtr(prepared.mOwnerIdentity);
    if (prepared.mSource != this || prepared.mWorldModel != &worldModel || !prepared.mItemState || owner.isEmpty()
        || owner != sourceOwner || owner.mRef != prepared.mOwnerReference || owner.mCell != prepared.mOwnerCell
        || sourceOwner.mCell != prepared.mOwnerCell || localScripts != prepared.mLocalScripts)
        throw std::invalid_argument("Container removal preparation source context changed");

    for (const auto& ref : mLists.mMiscItems.mList)
    {
        // Only current list members may be dereferenced. Neither a stale caller
        // Ptr nor a saved list iterator can establish that a node is still alive.
        if (&ref != prepared.mItemReference || ref.mRef.getRefNum() != prepared.mItemIdentity)
            continue;
        const auto registered = worldModel.getPtr(prepared.mItemIdentity);
        const auto& saved = *prepared.mItemState;
        if (ref.mWorldModel != &worldModel || registered.mRef != &ref || registered.mContainerStore != this
            || ref.mBase != saved.mBase || ref.mBase->mScript != saved.mData.getLocals().getScriptId()
            || ref.isDeleted() || miscTransferValues(ref.mRef) != miscTransferValues(saved.mRef)
            || !ref.mData.matchesContainerTransferState(saved.mData))
            throw std::invalid_argument("Container removal preparation source state changed");
        if (prepared.mScriptState)
            localScripts->validateRemoval(*prepared.mScriptState, &ref.mRef);
        return;
    }
    throw std::invalid_argument("Container removal preparation source membership changed");
}

void MWWorld::ContainerStore::validateTransferCount(const ConstPtr& item, int count) const
{
    // Bound stock addItems before calling it, and the aggregate returned by count(id).
    // Inspect raw MISC nodes, including dormant ones, before stock abs(int)
    // arithmetic. No temporary allocations or mutations here.
    std::int64_t total = count;
    for (const auto& target : mLists.mMiscItems.mList)
    {
        const auto available = std::abs(static_cast<std::int64_t>(target.mRef.getCount(false)));
        if (available > std::numeric_limits<int>::max())
            throw std::invalid_argument("Container transfer preparation count would overflow inventory");
        if (target.mRef.getRefId() == item.getCellRef().getRefId())
        {
            total += available;
            if (total > std::numeric_limits<int>::max())
                throw std::invalid_argument("Container transfer preparation count would overflow inventory");
        }
    }
}

namespace
{
    MWWorld::ContainerStoreIterator findContainerStack(
        MWWorld::ContainerStore& destination, const MWWorld::ConstPtr& item, const MWWorld::ESMStore& store)
    {
        auto iter = destination.begin(MWWorld::ContainerStore::getType(item));
        for (; iter != destination.end(); ++iter)
        {
            // Keep stock equipment exclusion and virtual compatibility dispatch.
            if (auto* inventory = dynamic_cast<MWWorld::InventoryStore*>(&destination))
                if (inventory->isEquipped(*iter))
                    continue;
            if (destination.stacks(*iter, item, store))
                break;
        }
        return iter;
    }

    void normalizeContainerAddReference(MWWorld::CellRef& ref)
    {
        ref.setPosition({});
        ref.setOwner(ESM::RefId());
        ref.resetGlobalVariable();
        ref.setFaction(ESM::RefId());
        ref.setFactionRank(-2);
    }

    // The registration consumer chooses immediate stock registration or an owned
    // intent. Keep OnPCAdd after that step: stock registration catches exceptions,
    // whereas deferred preparation must unwind without reporting success.
    template <class RegisterScript>
    MWWorld::Ptr prepareContainerAdd(MWWorld::Ptr item, MWWorld::ContainerStore& destination,
        const MWWorld::ContainerStoreAddContext& context, const RegisterScript& registerScript)
    {
        item.getRefData().setBaseNode(nullptr);
        normalizeContainerAddReference(item.getCellRef());

        const ESM::RefId& script = item.getClass().getScript(item);
        const auto& owner = context.mContainer;
        const bool playerContainer = !context.mPlayer.isEmpty() && owner == context.mPlayer;
        if (!script.empty())
        {
            // Player scripts survive cell unload. Other inventory scripts belong
            // to their owner cell. These hints are on a local Ptr, never the ref.
            if (playerContainer)
                item.mCell = nullptr;
            else if (!owner.isEmpty())
                item.mCell = owner.getCell();
            item.mContainerStore = &destination;
            registerScript(script, item);

            if (playerContainer)
                item.getRefData().getLocals().setVar(
                    *context.mStore.get<ESM::Script>().find(script), "onpcadd", 1, *context.mScriptManager);
        }
        return item;
    }

    void validateAddServices(const MWWorld::ConstPtr& item, const MWWorld::ContainerStoreAddContext& context)
    {
        if (!context.mInventoryUpdated)
            throw std::logic_error("ContainerStore::add requires an inventory presentation consumer");
        if ((!context.mLocalScripts || !context.mScriptManager)
            && (!item.getClass().getScript(item).empty()
                || (item.getClass().isGold(item)
                    && !context.mStore.get<ESM::Miscellaneous>()
                        .find(MWWorld::ContainerStore::sGoldId)
                        ->mScript.empty())))
            throw std::runtime_error(
                "ContainerStore::add requires LocalScripts and ScriptManager for scripted items "
                "(including OnPCAdd); rejected before mutation");
    }
}

MWWorld::PreparedContainerAdd::MiscState MWWorld::PreparedContainerAdd::miscState(const ConstPtr& item)
{
    if (item.getRefData().getLuaScripts() || item.getRefData().getCustomData())
        throw std::logic_error("Container stacking preparation excludes Lua or custom state");
    const auto script = item.getClass().getScript(item);
    if (item.getRefData().getLocals().getScriptId() != script)
        throw std::logic_error("Container stacking preparation requires matching initialized script locals");
    return { item.getCellRef().getRefNum(), item.mRef, item.get<ESM::Miscellaneous>()->mBase,
        item.getCellRef().getRefId(), item.getCellRef().getSoul(), script, item.getCellRef().getCount(false),
        item.mRef->isDeleted() };
}

MWWorld::PreparedContainerAdd MWWorld::ContainerStore::prepareTransferAdd(
    std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> item, const ContainerStoreAddContext& context)
{
    validateExplicitOwner(context.mContainer, context.mWorldModel);
    if (getPtr(context.mWorldModel).mCell != context.mContainer.mCell)
        throw std::invalid_argument("Container add preparation owner cell mismatch");
    if (!item || item->mWorldModel || item->mRef.getRefNum().isSet() || item->mRef.getCount(false) <= 0
        || item->isDeleted())
        throw std::invalid_argument("Container add preparation requires a detached positive-count item");
    const Ptr temporary(item.get());
    if (temporary.getClass().isGold(temporary) || item->mData.getLuaScripts() || item->mData.getCustomData())
        throw std::logic_error("Container add preparation excludes gold, Lua or custom state");
    if (item->mData.getLocals().getScriptId() != temporary.getClass().getScript(temporary))
        throw std::logic_error("Container add preparation requires matching initialized script locals");
    validateAddServices(temporary, context);
    validateTransferCount(temporary, temporary.getCellRef().getCount(false));

    PreparedContainerAdd prepared;
    prepared.mItem = std::move(item);
    prepared.mOwner = context.mContainer;
    prepared.mCount = temporary.getCellRef().getCount(false);
    prepared.mNotifyItemAdded = mListener != nullptr;
    prepared.mDestination = this;
    prepared.mStore = &context.mStore;
    prepared.mWorldModel = &context.mWorldModel;
    prepared.mOwnerIdentity = context.mContainer.getCellRef().getRefNum();
    prepared.mOwnerCell = context.mContainer.mCell;
    // Include dormant nodes too: reviving one can change first-match selection.
    for (const auto& ref : mLists.mMiscItems.mList)
    {
        const auto registered = context.mWorldModel.getPtr(ref.mRef.getRefNum());
        if (!ref.mRef.getRefNum().isSet() || ref.mWorldModel != &context.mWorldModel || registered.mRef != &ref
            || registered.mContainerStore != this)
            throw std::invalid_argument("Container stacking preparation destination item must be registered");
        prepared.mDestinationState.push_back(PreparedContainerAdd::miscState(ConstPtr(&ref)));
    }
    // Selection precedes normalization, just as in stock addImp/addWithContext.
    const auto stack = findContainerStack(*this, temporary, context.mStore);
    if (stack != end())
    {
        prepared.mStackTarget = stack->getCellRef().getRefNum();
        prepared.mStackCount = addItems(stack->getCellRef().getCount(false), prepared.mCount);
    }
    else
    {
        prepared.mStackCount = prepared.mCount;
        // Stock addNewStack copy-constructs RefData before registration/OnPCAdd.
        // Clear only the new destination's activation flags, never source values
        // or an existing stack's RefData. The detached copy owns every buffer.
        temporary.getRefData().clearActivationFlags();
    }
    prepareContainerAdd(temporary, *this, context, [&](const ESM::RefId& script, const Ptr& scriptItem) {
        // Unlike stock LocalScripts::add, missing records or preparation errors
        // must propagate. Never insert the temporary into a LocalScripts list.
        prepared.mScript = LocalScripts::prepareAdd(*context.mStore.get<ESM::Script>().find(script),
            scriptItem.getRefData(), scriptItem.mCell, *context.mScriptManager);
    });
    prepared.mItemState = PreparedContainerAdd::miscState(temporary);
    // Copying the consumer may allocate or throw. Own all prepared state/intents
    // before this final effect-preparation step, so any failure discards them.
    prepared.mInventoryUpdated = context.mInventoryUpdated;
    return prepared;
}

void MWWorld::ContainerStore::validateTransferStacking(
    const PreparedContainerAdd& prepared, const ContainerStoreAddContext& context) const
{
    validateExplicitOwner(context.mContainer, context.mWorldModel);
    if (prepared.mDestination != this || prepared.mStore != &context.mStore
        || prepared.mWorldModel != &context.mWorldModel
        || prepared.mOwnerIdentity != context.mContainer.getCellRef().getRefNum()
        || prepared.mOwner != context.mContainer || prepared.mOwnerCell != context.mContainer.mCell
        || prepared.mOwnerCell != getPtr(context.mWorldModel).mCell)
        throw std::invalid_argument("Container stacking preparation destination context changed");
    if (!prepared.mItem || prepared.mItem->mWorldModel || !prepared.mItemState
        || prepared.mItemState != PreparedContainerAdd::miscState(ConstPtr(prepared.mItem.get()))
        || prepared.mCount != prepared.mItemState->mCount)
        throw std::invalid_argument("Container stacking preparation item changed");

    auto saved = prepared.mDestinationState.begin();
    for (const auto& ref : mLists.mMiscItems.mList)
    {
        // Only dereference current list members. Store replacement may have destroyed
        // every reference that existed during preparation, invalidating all iterators.
        const auto registered = context.mWorldModel.getPtr(ref.mRef.getRefNum());
        if (saved == prepared.mDestinationState.end() || ref.mWorldModel != &context.mWorldModel
            || registered.mRef != &ref || registered.mContainerStore != this
            || *saved != PreparedContainerAdd::miscState(ConstPtr(&ref)))
            throw std::invalid_argument("Container stacking preparation destination state changed");
        ++saved;
    }
    if (saved != prepared.mDestinationState.end())
        throw std::invalid_argument("Container stacking preparation destination membership changed");
    // Content records are immutable for this operation. The witnesses bind all MISC
    // stacking inputs and signed counts, so no add, registration or effects run here.
}

struct MWWorld::PreparedContainerTransfer::State
{
    PreparedContainerRemove mRemoval;
    struct InventoryState
    {
        PreparedContainerAdd::MiscState mIdentity;
        std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> mValues;
        std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> mResult;
        LocalScripts::Removal mScript;
    };
    // Raw nodes include dormant values; public iteration omits zero counts.
    // The removal witness remains separate from every proposed source value.
    std::vector<InventoryState> mSourceValues;
    size_t mSourceItemIndex = 0;
    ESM::RefNum mOriginalSourceSelection, mSourceSelection;
    PreparedContainerAdd mAddition;
    std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> mAddedValues;
    // Existing nodes own separate original/proposed values. For new membership,
    // mAddition owns the appended value and its prepared script locals/intents.
    std::vector<InventoryState> mDestinationValues;
    std::optional<size_t> mDestinationItemIndex;
    ESM::RefNum mDestinationSelection;
    std::shared_ptr<const void> mSourceStorage, mDestinationStorage;
    const LocalScripts* mDestinationScripts = nullptr;
    const MWBase::ScriptManager* mScriptManager = nullptr;
    Ptr mPlayer;
    ESM::RefNum mPlayerIdentity;
    ContainerStoreListener* mSourceListener = nullptr;
    ContainerStoreListener* mDestinationListener = nullptr;
    std::function<void(const Ptr&)> mSourceUpdated;
};

MWWorld::PreparedContainerTransfer::PreparedContainerTransfer(std::unique_ptr<State> state)
    : mState(std::move(state))
{
}

MWWorld::PreparedContainerTransfer::PreparedContainerTransfer(PreparedContainerTransfer&&) noexcept = default;
MWWorld::PreparedContainerTransfer& MWWorld::PreparedContainerTransfer::operator=(PreparedContainerTransfer&&) noexcept
    = default;
MWWorld::PreparedContainerTransfer::~PreparedContainerTransfer() = default;

const MWWorld::PreparedContainerTransfer::State& MWWorld::PreparedContainerTransfer::state() const
{
    if (!mState)
        throw std::invalid_argument("Container transfer preparation was moved from");
    return *mState;
}

const MWWorld::PreparedContainerRemove& MWWorld::PreparedContainerTransfer::getRemoval() const
{
    return state().mRemoval;
}

MWWorld::ConstPtr MWWorld::PreparedContainerTransfer::getItem() const
{
    return ConstPtr(state().mAddition.mItem.get());
}

MWWorld::ConstPtr MWWorld::PreparedContainerTransfer::getSourceItem() const
{
    const auto& owned = state();
    return ConstPtr(owned.mSourceValues.at(owned.mSourceItemIndex).mResult.get());
}

std::vector<MWWorld::PreparedContainerTransfer::InventoryItem>
MWWorld::PreparedContainerTransfer::getSourceInventory() const
{
    std::vector<InventoryItem> result;
    for (const auto& item : state().mSourceValues)
        if (item.mResult->mRef.getCount(false))
            result.push_back({ item.mIdentity.mIdentity, ConstPtr(item.mResult.get()) });
    return result;
}

std::vector<MWWorld::PreparedContainerTransfer::InventoryItem>
MWWorld::PreparedContainerTransfer::getDestinationInventory() const
{
    const auto& owned = state();
    std::vector<InventoryItem> result;
    for (const auto& item : owned.mDestinationValues)
        if (item.mResult->mRef.getCount(false))
            result.push_back({ item.mIdentity.mIdentity, ConstPtr(item.mResult.get()) });
    if (!owned.mDestinationItemIndex)
        result.push_back({ {}, ConstPtr(owned.mAddition.mItem.get()) });
    return result;
}

ESM::RefNum MWWorld::PreparedContainerTransfer::getSourceSelection() const
{
    return state().mSourceSelection;
}

ESM::RefNum MWWorld::PreparedContainerTransfer::getDestinationSelection() const
{
    return state().mDestinationSelection;
}

MWWorld::ConstPtr MWWorld::PreparedContainerTransfer::getDestinationItem() const
{
    const auto& owned = state();
    return ConstPtr(owned.mDestinationItemIndex
            ? owned.mDestinationValues.at(*owned.mDestinationItemIndex).mResult.get()
            : owned.mAddition.mItem.get());
}

ESM::RefNum MWWorld::PreparedContainerTransfer::getStackTarget() const
{
    return state().mAddition.getStackTarget();
}

int MWWorld::PreparedContainerTransfer::getStackCount() const
{
    return state().mAddition.getStackCount();
}

std::optional<MWWorld::LocalScripts::Registration> MWWorld::PreparedContainerTransfer::getScriptAddition() const
{
    return state().mAddition.mScript;
}

bool MWWorld::PreparedContainerTransfer::hasRemovalNotification() const
{
    return state().mSourceListener != nullptr;
}

bool MWWorld::PreparedContainerTransfer::hasAdditionNotification() const
{
    return state().mAddition.mNotifyItemAdded;
}

namespace
{
    bool sameBinding(const MWWorld::ConstPtr& a, const MWWorld::ConstPtr& b)
    {
        // Ptr equality alone deliberately ignores cell and container hints.
        return a.mRef == b.mRef && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore;
    }

    void validateTransferRegistration(const MWWorld::LocalScripts::Removal& registration, const MWWorld::ConstPtr& item,
        const MWWorld::ContainerStore& container, MWWorld::CellStore* ownerCell)
    {
        if (registration.hasRegistration()
            && (registration.getScript() != item.getClass().getScript(item) || registration.getContainer() != &container
                || (registration.getCell() && registration.getCell() != ownerCell)))
            throw std::invalid_argument("Container transfer preparation script registration mismatch");
    }

    bool sameTransferValues(
        const MWWorld::LiveCellRef<ESM::Miscellaneous>& item, const MWWorld::LiveCellRef<ESM::Miscellaneous>& saved)
    {
        return item.mBase == saved.mBase && miscTransferValues(item.mRef) == miscTransferValues(saved.mRef)
            && item.mData.matchesContainerTransferState(saved.mData);
    }
}

ESM::RefNum MWWorld::ContainerStore::transferSelection() const
{
    if (mSelectedEnchantItem == end())
        return {};
    // Read only a current member, never dereference the stored selection. A
    // dormant or foreign selection cannot describe this MISC inventory result.
    for (auto iter = begin(Type_Miscellaneous); iter != end(); ++iter)
        if (iter == mSelectedEnchantItem)
            return iter->getCellRef().getRefNum();
    throw std::invalid_argument("Container transfer preparation selection changed or unsupported");
}

MWWorld::PreparedContainerTransfer MWWorld::ContainerStore::prepareTransfer(const ConstPtr& item, int count,
    ContainerStore& destination, const ContainerStoreRemoveContext& sourceContext,
    const ContainerStoreAddContext& destinationContext) const
{
    const auto& worldModel = sourceContext.mWorldModel;
    if (&worldModel != &destinationContext.mWorldModel)
        throw std::invalid_argument("Container transfer preparation world context mismatch");
    validateExplicitOwner(sourceContext.mContainer, worldModel);
    destination.validateExplicitOwner(destinationContext.mContainer, worldModel);
    if (this == &destination || sourceContext.mContainer == destinationContext.mContainer)
        throw std::invalid_argument("Container transfer preparation requires two distinct owners and stores");
    if (!sameBinding(getPtr(worldModel), sourceContext.mContainer)
        || !sameBinding(destination.getPtr(worldModel), destinationContext.mContainer))
        throw std::invalid_argument("Container transfer preparation owner binding mismatch");
    if (!sourceContext.mInventoryUpdated || !destinationContext.mInventoryUpdated)
        throw std::logic_error("Container transfer preparation requires both presentation consumers");
    if (!destinationContext.mLocalScripts || !sourceContext.mLocalScripts.usesStore(destinationContext.mStore)
        || !destinationContext.mLocalScripts->usesStore(destinationContext.mStore))
        throw std::invalid_argument("Container transfer preparation requires LocalScripts bound to the content store");
    validateTransferSource(item, count, worldModel);
    destination.validateTransferCount(item, count);

    auto state = std::make_unique<PreparedContainerTransfer::State>();
    state->mSourceStorage = mStorageIdentity;
    state->mDestinationStorage = destination.mStorageIdentity;
    state->mDestinationScripts = destinationContext.mLocalScripts;
    state->mScriptManager = destinationContext.mScriptManager;
    state->mSourceListener = mListener;
    state->mDestinationListener = destination.mListener;
    state->mPlayer = destinationContext.mPlayer;
    if (!state->mPlayer.isEmpty())
    {
        // Find the caller's binding without dereferencing a possibly stale Ptr.
        for (const auto& [id, ptr] : worldModel.getPtrRegistryView())
            if (sameBinding(ptr, state->mPlayer))
            {
                state->mPlayerIdentity = id;
                break;
            }
        if (!state->mPlayerIdentity.isSet())
            throw std::invalid_argument("Container transfer preparation player binding mismatch");
    }
    state->mRemoval
        = prepareTransferRemove(item, count, sourceContext.mContainer, worldModel, &sourceContext.mLocalScripts);
    const auto& source = *state->mRemoval.mItemState;
    if (destinationContext.mStore.get<ESM::Miscellaneous>().search(source.mRef.getRefId()) != source.mBase)
        throw std::invalid_argument("Container transfer preparation source content mismatch");
    validateTransferRegistration(*state->mRemoval.mScriptState, item, *this, sourceContext.mContainer.mCell);
    state->mOriginalSourceSelection = transferSelection();
    state->mSourceSelection = state->mRemoval.getRemainingCount() == 0
            && state->mOriginalSourceSelection == state->mRemoval.getItemIdentity()
        ? ESM::RefNum()
        : state->mOriginalSourceSelection;
    for (const auto& ref : mLists.mMiscItems.mList)
    {
        const ConstPtr current(&ref);
        const auto registered = worldModel.getPtr(ref.mRef.getRefNum());
        if (!ref.mRef.getRefNum().isSet() || ref.mWorldModel != &worldModel || registered.mRef != &ref
            || registered.mContainerStore != this)
            throw std::invalid_argument("Container transfer preparation source inventory item must be registered");
        if (destinationContext.mStore.get<ESM::Miscellaneous>().search(ref.mRef.getRefId()) != ref.mBase)
            throw std::invalid_argument("Container transfer preparation source content mismatch");
        if (current.getClass().isGold(current))
            throw std::logic_error("Container transfer preparation source inventory excludes gold");
        if (ref.mRef.getCount(false) == std::numeric_limits<int>::min())
            throw std::invalid_argument("Container transfer preparation source inventory count is invalid");
        auto identity = PreparedContainerAdd::miscState(current);
        auto registration = sourceContext.mLocalScripts.prepareRemove(&ref.mRef);
        validateTransferRegistration(registration, current, *this, sourceContext.mContainer.mCell);
        auto values = copyContainerTransferItem(current);
        auto result = copyContainerTransferItem(ConstPtr(values.get()));
        if (&ref == state->mRemoval.mItemReference)
        {
            state->mSourceItemIndex = state->mSourceValues.size();
            result->mRef = source.mRef.copyWithCount(state->mRemoval.getRemainingCount());
        }
        state->mSourceValues.push_back(
            { std::move(identity), std::move(values), std::move(result), std::move(registration) });
    }
    state->mDestinationSelection = destination.transferSelection();
    for (const auto& ref : destination.mLists.mMiscItems.mList)
    {
        const ConstPtr current(&ref);
        if (destinationContext.mStore.get<ESM::Miscellaneous>().search(ref.mRef.getRefId()) != ref.mBase)
            throw std::invalid_argument("Container transfer preparation destination content mismatch");
        if (current.getClass().isGold(current))
            throw std::logic_error("Container transfer preparation destination inventory excludes gold");
        if (ref.mRef.getCount(false) == std::numeric_limits<int>::min())
            throw std::invalid_argument("Container transfer preparation destination inventory count is invalid");
        auto identity = PreparedContainerAdd::miscState(current);
        auto registration = destinationContext.mLocalScripts->prepareRemove(&ref.mRef);
        validateTransferRegistration(registration, current, destination, destinationContext.mContainer.mCell);
        auto values = copyContainerTransferItem(current);
        auto result = copyContainerTransferItem(ConstPtr(values.get()));
        state->mDestinationValues.push_back(
            { std::move(identity), std::move(values), std::move(result), std::move(registration) });
    }
    // Derive the incoming value from the owned removal witness, never from a
    // separately supplied item/count. Keep the independent incoming preparation.
    auto detached = copyContainerTransferItem(ConstPtr(&source));
    detached->mRef.setCount(state->mRemoval.getCount());
    state->mAddition = destination.prepareTransferAdd(std::move(detached), destinationContext);
    state->mAddedValues = copyContainerTransferItem(ConstPtr(state->mAddition.mItem.get()));
    if (state->mAddition.getStackTarget().isSet())
    {
        // Stock addImp changes the selected destination, not the incoming item.
        // Reuse that exact selection and its owned witness, including RefData that
        // stacks() need not compare. Scripted items never take this branch.
        for (size_t i = 0; i < state->mDestinationValues.size(); ++i)
            if (state->mDestinationValues[i].mIdentity.mIdentity == state->mAddition.getStackTarget())
            {
                state->mDestinationItemIndex = i;
                auto& result = *state->mDestinationValues[i].mResult;
                result.mRef.setCount(state->mAddition.getStackCount());
                normalizeContainerAddReference(result.mRef);
                break;
            }
        if (!state->mDestinationItemIndex)
            throw std::logic_error("Container transfer preparation stack witness missing");
    }
    // The last fallible consumer copy occurs after both decisions, values and
    // script/notification intents exist. Any throw destroys the entire pair.
    state->mSourceUpdated = sourceContext.mInventoryUpdated;
    PreparedContainerTransfer prepared(std::move(state));
    validateTransfer(prepared, destination, sourceContext, destinationContext);
    return prepared;
}

void MWWorld::ContainerStore::validateTransfer(const PreparedContainerTransfer& prepared,
    const ContainerStore& destination, const ContainerStoreRemoveContext& sourceContext,
    const ContainerStoreAddContext& destinationContext) const
{
    const auto& state = prepared.state();
    const auto& worldModel = sourceContext.mWorldModel;
    if (state.mSourceStorage != mStorageIdentity || state.mDestinationStorage != destination.mStorageIdentity)
        throw std::invalid_argument("Container transfer preparation storage changed");
    if (&worldModel != &destinationContext.mWorldModel || state.mDestinationScripts != destinationContext.mLocalScripts
        || state.mScriptManager != destinationContext.mScriptManager || state.mSourceListener != mListener
        || state.mDestinationListener != destination.mListener
        || !sameBinding(state.mPlayer, destinationContext.mPlayer)
        || (!state.mPlayer.isEmpty() && !sameBinding(worldModel.getPtr(state.mPlayerIdentity), state.mPlayer))
        || !sameBinding(getPtr(worldModel), sourceContext.mContainer)
        || !sameBinding(destination.getPtr(worldModel), destinationContext.mContainer))
        throw std::invalid_argument("Container transfer preparation context changed");
    validateTransferRemoval(state.mRemoval, sourceContext.mContainer, worldModel, &sourceContext.mLocalScripts);
    destination.validateTransferStacking(state.mAddition, destinationContext);
    const auto& source = *state.mRemoval.mItemState;
    if (state.mRemoval.getCount() <= 0)
        throw std::invalid_argument("Container transfer preparation removal count changed");
    const auto removal = prepareRemoveCount(source.mRef, state.mRemoval.getCount());
    const auto expectedSource = source.mRef.copyWithCount(removal.mRemainingCount);
    const auto* sourceResult = prepared.getSourceItem().get<ESM::Miscellaneous>();
    if (removal.mRemoved != state.mRemoval.getCount() || removal.mRemainingCount != state.mRemoval.getRemainingCount()
        || !sourceResult || sourceResult->mBase != source.mBase || sourceResult->mWorldModel
        || sourceResult->mRef.getRefNum().isSet() || sourceResult->mData.getBaseNode()
        || miscTransferValues(sourceResult->mRef) != miscTransferValues(expectedSource)
        || sourceResult->mRef.hasChanged() != expectedSource.hasChanged()
        || !sourceResult->mData.matchesContainerTransferState(source.mData))
        throw std::invalid_argument("Container transfer preparation source item values changed");
    const auto& item = *state.mAddition.mItem;
    if (!sameTransferValues(item, *state.mAddedValues) || item.mData.getBaseNode() || item.mRef.getRefNum().isSet()
        || item.mWorldModel || item.mRef.getCount(false) != state.mRemoval.getCount())
        throw std::invalid_argument("Container transfer preparation item values changed");
    const auto expectedSelection
        = removal.mFullRemoval && state.mOriginalSourceSelection == state.mRemoval.getItemIdentity()
        ? ESM::RefNum()
        : state.mOriginalSourceSelection;
    if (transferSelection() != state.mOriginalSourceSelection || state.mSourceSelection != expectedSelection)
        throw std::invalid_argument("Container transfer preparation source selection changed");
    if (destination.transferSelection() != state.mDestinationSelection)
        throw std::invalid_argument("Container transfer preparation destination selection changed");
    size_t sourceIndex = 0;
    for (const auto& ref : mLists.mMiscItems.mList)
    {
        if (sourceIndex == state.mSourceValues.size())
            throw std::invalid_argument("Container transfer preparation source inventory membership changed");
        const auto& savedSource = state.mSourceValues[sourceIndex];
        const auto registered = worldModel.getPtr(ref.mRef.getRefNum());
        if (ref.mWorldModel != &worldModel || registered.mRef != &ref || registered.mContainerStore != this
            || savedSource.mIdentity != PreparedContainerAdd::miscState(ConstPtr(&ref)))
            throw std::invalid_argument("Container transfer preparation source inventory membership changed");
        if (!sameTransferValues(ref, *savedSource.mValues)
            || ref.mRef.hasChanged() != savedSource.mValues->mRef.hasChanged())
            throw std::invalid_argument("Container transfer preparation source inventory values changed");
        sourceContext.mLocalScripts.validateRemoval(savedSource.mScript, &ref.mRef);
        const bool removed = sourceIndex == state.mSourceItemIndex;
        if (removed != (savedSource.mIdentity.mIdentity == state.mRemoval.getItemIdentity()))
            throw std::invalid_argument("Container transfer preparation source inventory result changed");
        const auto& witness = removed ? source : *savedSource.mValues;
        const auto expected = removed ? expectedSource : witness.mRef;
        const auto* result = savedSource.mResult.get();
        if (!result || result->mBase != witness.mBase || result->mWorldModel || result->mRef.getRefNum().isSet()
            || result->mData.getBaseNode() || miscTransferValues(result->mRef) != miscTransferValues(expected)
            || result->mRef.hasChanged() != expected.hasChanged()
            || !result->mData.matchesContainerTransferState(witness.mData))
            throw std::invalid_argument("Container transfer preparation source inventory result changed");
        ++sourceIndex;
    }
    if (sourceIndex != state.mSourceValues.size())
        throw std::invalid_argument("Container transfer preparation source inventory membership changed");
    auto saved = state.mDestinationValues.begin();
    bool foundStack = false;
    for (const auto& ref : destination.mLists.mMiscItems.mList)
    {
        // Stacking validation has already established current membership/order.
        if (saved == state.mDestinationValues.end()
            || saved->mIdentity != PreparedContainerAdd::miscState(ConstPtr(&ref))
            || !sameTransferValues(ref, *saved->mValues) || ref.mRef.hasChanged() != saved->mValues->mRef.hasChanged())
            throw std::invalid_argument("Container transfer preparation destination values changed");
        state.mDestinationScripts->validateRemoval(saved->mScript, &ref.mRef);
        const bool stacked = ref.mRef.getRefNum() == state.mAddition.getStackTarget();
        if (stacked
            != (state.mDestinationItemIndex
                && *state.mDestinationItemIndex == static_cast<size_t>(saved - state.mDestinationValues.begin())))
            throw std::invalid_argument("Container transfer preparation destination inventory result changed");
        auto expected = saved->mValues->mRef;
        if (stacked)
        {
            foundStack = true;
            // Derive the expected result from the selected destination witness,
            // using the same removal quantity and stock signed arithmetic. Never
            // re-run script preparation or read a saved inventory/script iterator.
            expected.setCount(addItems(expected.getCount(false), state.mRemoval.getCount()));
            normalizeContainerAddReference(expected);
        }
        const auto* result = saved->mResult.get();
        if (!result || result->mBase != saved->mValues->mBase || result->mWorldModel || result->mRef.getRefNum().isSet()
            || result->mData.getBaseNode() || miscTransferValues(result->mRef) != miscTransferValues(expected)
            || result->mRef.hasChanged() != expected.hasChanged()
            || !result->mData.matchesContainerTransferState(saved->mValues->mData)
            || (stacked
                && (!result->mBase->mScript.empty()
                    || result->mRef.getCount(false) != state.mAddition.getStackCount())))
            throw std::invalid_argument(stacked
                    ? "Container transfer preparation destination item values changed"
                    : "Container transfer preparation destination inventory result changed");
        ++saved;
    }
    if (saved != state.mDestinationValues.end())
        throw std::invalid_argument("Container transfer preparation destination membership changed");
    if (foundStack != state.mAddition.getStackTarget().isSet() || foundStack != state.mDestinationItemIndex.has_value()
        || (!foundStack && state.mAddition.getStackCount() != state.mRemoval.getCount()))
        throw std::invalid_argument("Container transfer preparation destination result changed");
    const auto& script = state.mAddition.mScript;
    const auto scriptId = item.mBase->mScript;
    const bool player = !state.mPlayer.isEmpty() && state.mAddition.mOwner == state.mPlayer;
    if (script.has_value() != !scriptId.empty()
        || (script
            && (script->mScript != scriptId || script->mCell != (player ? nullptr : state.mAddition.mOwner.mCell)))
        || state.mAddition.mNotifyItemAdded != (state.mDestinationListener != nullptr)
        || !state.mAddition.mInventoryUpdated || !state.mSourceUpdated)
        throw std::invalid_argument("Container transfer preparation effects changed");
    // The immutable pair binds its derived item and intents; this is neither an
    // installation precondition nor a durability or mutation-history guarantee.
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::addWithContext(
    const ConstPtr& itemPtr, int count, const ContainerStoreAddContext& context, bool resolve)
{
    getType(itemPtr);
    validateAddServices(itemPtr, context);
    if (resolve)
        this->resolve(context.mContainer);

    MWWorld::ContainerStoreIterator it = addImp(itemPtr, count, context.mStore);

    // The selected existing stack or the new copy of the incoming item.
    MWWorld::Ptr item = *it;
    context.mWorldModel.registerPtr(item);

    item = prepareContainerAdd(item, *this, context, [&](const ESM::RefId& script, const Ptr& scriptItem) {
        context.mLocalScripts->add(script, scriptItem, *context.mScriptManager);
    });

    // we should not fire event for InventoryStore yet - it has some custom logic
    if (mListener && typeid(*this) == typeid(ContainerStore))
        mListener->itemAdded(item, count);
    context.mInventoryUpdated(context.mContainer);

    return it;
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::addImp(
    const ConstPtr& ptr, int count, const ESMStore& esmStore)
{
    int type = getType(ptr);

    // gold needs special handling: when it is inserted into a container, the base object automatically becomes Gold_001
    // this ensures that gold piles of different sizes stack with each other (also, several scripts rely on Gold_001 for
    // detecting player gold)
    // Note that adding 1 gold_100 is equivalent to adding 1 gold_001. Morrowind.exe resolves gold in leveled lists to
    // gold_001 and TESCS disallows adding gold other than gold_001 to inventories. If a content file defines a
    // container containing gold_100 anyway, the item is not turned to gold_001 until the player puts it down in the
    // world and picks it up again. We just turn it into gold_001 here and ignore that oddity.
    if (ptr.getClass().isGold(ptr))
    {
        for (MWWorld::ContainerStoreIterator iter(begin(type)); iter != end(); ++iter)
        {
            if (iter->getCellRef().getRefId() == MWWorld::ContainerStore::sGoldId)
            {
                iter->getCellRef().setCount(addItems(iter->getCellRef().getCount(false), count));
                flagAsModified();
                return iter;
            }
        }

        MWWorld::ManualRef ref(esmStore, MWWorld::ContainerStore::sGoldId, count);
        return addNewStack(ref.getPtr(), count);
    }

    if (const auto iter = findContainerStack(*this, ptr, esmStore); iter != end())
    {
        iter->getCellRef().setCount(addItems(iter->getCellRef().getCount(false), count));
        flagAsModified();
        return iter;
    }
    // if we got here, this means no stacking
    return addNewStack(ptr, count);
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::addNewStack(const ConstPtr& ptr, int count)
{
    // As with MWWorld::CellStore::insert, the caller is expected to deal with LiveCellRefBase's copy constructor
    // copying RefData and CellRef, thereby creating another instance with the same ESM::RefNum and MWLua::LocalScripts.
    // In practice this means the caller MUST ensure the object that gets copied into this inventory is removed from the
    // world.
    ContainerStoreIterator it = begin();

    switch (getType(ptr))
    {
        case Type_Potion:
            mLists.mPotions.mList.push_back(*ptr.get<ESM::Potion>());
            it = ContainerStoreIterator(this, --mLists.mPotions.mList.end());
            break;
        case Type_Apparatus:
            mLists.mAppas.mList.push_back(*ptr.get<ESM::Apparatus>());
            it = ContainerStoreIterator(this, --mLists.mAppas.mList.end());
            break;
        case Type_Armor:
            mLists.mArmors.mList.push_back(*ptr.get<ESM::Armor>());
            it = ContainerStoreIterator(this, --mLists.mArmors.mList.end());
            break;
        case Type_Book:
            mLists.mBooks.mList.push_back(*ptr.get<ESM::Book>());
            it = ContainerStoreIterator(this, --mLists.mBooks.mList.end());
            break;
        case Type_Clothing:
            mLists.mClothes.mList.push_back(*ptr.get<ESM::Clothing>());
            it = ContainerStoreIterator(this, --mLists.mClothes.mList.end());
            break;
        case Type_Ingredient:
            mLists.mIngreds.mList.push_back(*ptr.get<ESM::Ingredient>());
            it = ContainerStoreIterator(this, --mLists.mIngreds.mList.end());
            break;
        case Type_Light:
            mLists.mLights.mList.push_back(*ptr.get<ESM::Light>());
            it = ContainerStoreIterator(this, --mLists.mLights.mList.end());
            break;
        case Type_Lockpick:
            mLists.mLockpicks.mList.push_back(*ptr.get<ESM::Lockpick>());
            it = ContainerStoreIterator(this, --mLists.mLockpicks.mList.end());
            break;
        case Type_Miscellaneous:
            mLists.mMiscItems.mList.push_back(*ptr.get<ESM::Miscellaneous>());
            it = ContainerStoreIterator(this, --mLists.mMiscItems.mList.end());
            break;
        case Type_Probe:
            mLists.mProbes.mList.push_back(*ptr.get<ESM::Probe>());
            it = ContainerStoreIterator(this, --mLists.mProbes.mList.end());
            break;
        case Type_Repair:
            mLists.mRepairs.mList.push_back(*ptr.get<ESM::Repair>());
            it = ContainerStoreIterator(this, --mLists.mRepairs.mList.end());
            break;
        case Type_Weapon:
            mLists.mWeapons.mList.push_back(*ptr.get<ESM::Weapon>());
            it = ContainerStoreIterator(this, --mLists.mWeapons.mList.end());
            break;
    }

    it->getCellRef().setCount(count);

    flagAsModified();
    return it;
}

void MWWorld::ContainerStore::setSelectedEnchantItem(const ContainerStoreIterator& iterator)
{
    mSelectedEnchantItem = iterator;
}

MWWorld::ContainerStoreIterator MWWorld::ContainerStore::getSelectedEnchantItem()
{
    return mSelectedEnchantItem;
}

MWWorld::ConstContainerStoreIterator MWWorld::ContainerStore::getSelectedEnchantItem() const
{
    return mSelectedEnchantItem;
}

void MWWorld::ContainerStore::rechargeItems(float duration)
{
    if (!mRechargingItemsUpToDate)
    {
        updateRechargingItems();
        mRechargingItemsUpToDate = true;
    }
    for (auto& it : mRechargingItems)
    {
        if (!MWMechanics::rechargeItem(*it.first, it.second, duration))
            continue;

        // attempt to restack when fully recharged
        if (it.first->getCellRef().getEnchantmentCharge() == it.second)
            it.first = restack(*it.first);
    }
}

void MWWorld::ContainerStore::updateRechargingItems()
{
    mRechargingItems.clear();
    for (ContainerStoreIterator it = begin(); it != end(); ++it)
    {
        const auto& enchantmentId = it->getClass().getEnchantment(*it);
        if (!enchantmentId.empty())
        {
            const ESM::Enchantment* enchantment
                = MWBase::Environment::get().getESMStore()->get<ESM::Enchantment>().search(enchantmentId);
            if (!enchantment)
            {
                Log(Debug::Warning) << "Warning: Can't find enchantment '" << enchantmentId << "' on item "
                                    << it->getCellRef().getRefId();
                continue;
            }

            if (enchantment->mData.mType == ESM::Enchantment::WhenUsed
                || enchantment->mData.mType == ESM::Enchantment::WhenStrikes)
                mRechargingItems.emplace_back(it, static_cast<float>(MWMechanics::getEnchantmentCharge(*enchantment)));
        }
    }
}

int MWWorld::ContainerStore::remove(const ESM::RefId& itemId, int count, bool equipReplacement, bool resolveFirst)
{
    if (resolveFirst)
        resolve();
    int toRemove = count;

    for (ContainerStoreIterator iter(begin()); iter != end() && toRemove > 0; ++iter)
        if (iter->getCellRef().getRefId() == itemId)
            toRemove -= remove(*iter, toRemove, equipReplacement, resolveFirst);

    flagAsModified();

    // number of removed items
    return count - toRemove;
}

bool MWWorld::ContainerStore::hasVisibleItems() const
{
    for (const auto&& iter : *this)
    {
        if (iter.getClass().showsInInventory(iter))
            return true;
    }

    return false;
}

int MWWorld::ContainerStore::remove(const Ptr& item, int count, bool /*equipReplacement*/, bool resolveFirst)
{
    assert(this == item.getContainerStore());
    auto& environment = MWBase::Environment::get();
    const ContainerStoreRemoveContext context{ *environment.getWorldModel(), getPtr(),
        environment.getWorld()->getLocalScripts(),
        [&environment](const Ptr& owner) { environment.getWindowManager()->inventoryUpdated(owner); } };
    return removeWithContext(item, count, context, resolveFirst);
}

int MWWorld::ContainerStore::remove(const Ptr& item, int count, const ContainerStoreRemoveContext& context)
{
    validateExplicitOwner(context.mContainer, context.mWorldModel);
    if (count <= 0)
        throw std::invalid_argument("Explicit remove count must be positive");
    // Ptr's container field is public; check actual membership as well as its hint.
    if (item.isEmpty() || item.getContainerStore() != this)
        throw std::invalid_argument("Explicit remove item ownership mismatch");
    if (item.getCellRef().getCount(false) == std::numeric_limits<int>::min())
        throw std::invalid_argument("Explicit remove item count is invalid");
    if (std::find(begin(), end(), item) == end())
        throw std::invalid_argument("Explicit remove item ownership mismatch");
    if (!context.mInventoryUpdated)
        throw std::logic_error("ContainerStore::remove requires an inventory presentation consumer");
    return removeWithContext(item, count, context, true);
}

MWWorld::ContainerStore::ItemRemoval MWWorld::ContainerStore::prepareRemoveCount(const CellRef& item, int count)
{
    const int available = item.getCount();
    if (available <= count)
        return { available, 0, true };
    return { count, subtractItems(item.getCount(false), count), false };
}

int MWWorld::ContainerStore::removeWithContext(
    const Ptr& item, int count, const ContainerStoreRemoveContext& context, bool resolveFirst)
{
    if (resolveFirst)
        resolve(context.mContainer);

    CellRef& itemRef = item.getCellRef();
    const auto removal = prepareRemoveCount(itemRef, count);
    itemRef.setCount(removal.mRemainingCount, context.mLocalScripts);
    if (removal.mFullRemoval)
    {
        if (mSelectedEnchantItem != end() && *mSelectedEnchantItem == item)
            mSelectedEnchantItem = end();
    }

    flagAsModified();

    // we should not fire event for InventoryStore yet - it has some custom logic
    if (mListener && typeid(*this) == typeid(ContainerStore))
        mListener->itemRemoved(item, removal.mRemoved);
    context.mInventoryUpdated(context.mContainer);

    // number of removed items
    return removal.mRemoved;
}

void MWWorld::ContainerStore::fill(const ESM::InventoryList& items, const ESM::RefId& owner, Misc::Rng::Generator& prng)
{
    for (const ESM::ContItem& iter : items.mList)
    {
        addInitialItem(iter.mItem, owner, iter.mCount, &prng);
    }

    flagAsModified();
    mResolved = true;
}

void MWWorld::ContainerStore::fillNonRandom(const ESM::InventoryList& items, const ESM::RefId& owner, unsigned int seed)
{
    mSeed = seed;
    for (const ESM::ContItem& iter : items.mList)
    {
        addInitialItem(iter.mItem, owner, iter.mCount, nullptr);
    }

    flagAsModified();
    mResolved = false;
}

void MWWorld::ContainerStore::addInitialItem(
    const ESM::RefId& id, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng, bool topLevel)
{
    if (count == 0)
        return; // Don't restock with nothing.
    try
    {
        ManualRef ref(*MWBase::Environment::get().getESMStore(), id, count);
        if (ref.getPtr().getClass().getScript(ref.getPtr()).empty())
        {
            addInitialItemImp(ref.getPtr(), owner, count, prng, topLevel);
        }
        else
        {
            // Adding just one item per time to make sure there isn't a stack of scripted items
            for (int i = 0; i < std::abs(count); i++)
                addInitialItemImp(ref.getPtr(), owner, count < 0 ? -1 : 1, prng, topLevel);
        }
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "Warning: MWWorld::ContainerStore::addInitialItem: " << e.what();
    }
}

void MWWorld::ContainerStore::addInitialItemImp(
    const MWWorld::Ptr& ptr, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng, bool topLevel)
{
    if (ptr.getType() == ESM::ItemLevList::sRecordId)
    {
        if (!prng)
            return;
        const ESM::ItemLevList* levItemList = ptr.get<ESM::ItemLevList>()->mBase;

        if (topLevel && std::abs(count) > 1 && levItemList->mFlags & ESM::ItemLevList::Each)
        {
            for (int i = 0; i < std::abs(count); ++i)
                addInitialItem(ptr.getCellRef().getRefId(), owner, count > 0 ? 1 : -1, prng, true);
            return;
        }
        else
        {
            const auto& itemId = MWMechanics::getLevelledItem(ptr.get<ESM::ItemLevList>()->mBase, false, *prng);
            if (itemId.empty())
                return;
            addInitialItem(itemId, owner, count, prng, false);
        }
    }
    else
    {
        ptr.getCellRef().setOwner(owner);
        MWWorld::ContainerStoreIterator it = addImp(ptr, count, *MWBase::Environment::get().getESMStore());
        MWBase::Environment::get().getWorldModel()->registerPtr(*it);
    }
}

void MWWorld::ContainerStore::clear()
{
    for (auto&& iter : *this)
        iter.getCellRef().setCount(0);

    flagAsModified();
    mModified = true;
}

void MWWorld::ContainerStore::flagAsModified()
{
    mWeightUpToDate = false;
    mRechargingItemsUpToDate = false;
}

bool MWWorld::ContainerStore::isResolved() const
{
    return mResolved;
}

void MWWorld::ContainerStore::resolve()
{
    resolve(getPtr());
}

void MWWorld::ContainerStore::resolve(const Ptr& container)
{
    if (!mResolved && !container.isEmpty() && container.getType() == ESM::REC_CONT)
    {
        for (const auto&& ptr : *this)
            ptr.getCellRef().setCount(0);
        Misc::Rng::Generator prng{ mSeed };
        fill(container.get<ESM::Container>()->mBase->mInventory, ESM::RefId(), prng);
        addScripts(*this, container.mCell);
    }
    mModified = true;
}

MWWorld::ResolutionHandle MWWorld::ContainerStore::resolveTemporarily()
{
    if (mModified)
        return {};
    std::shared_ptr<ResolutionListener> listener = mResolutionListener.lock();
    if (!listener)
    {
        listener = std::make_shared<ResolutionListener>(*this);
        mResolutionListener = listener;
    }
    const Ptr& container = getPtr();
    if (!mResolved && !container.isEmpty() && container.getType() == ESM::REC_CONT)
    {
        for (const auto&& ptr : *this)
            ptr.getCellRef().setCount(0);
        Misc::Rng::Generator prng{ mSeed };
        fill(container.get<ESM::Container>()->mBase->mInventory, ESM::RefId(), prng);
        addScripts(*this, container.mCell);
    }
    return { std::move(listener) };
}

void MWWorld::ContainerStore::unresolve()
{
    if (mModified)
        return;

    const Ptr& container = getPtr();
    if (mResolved && !container.isEmpty() && container.getType() == ESM::REC_CONT)
    {
        for (const auto&& ptr : *this)
            ptr.getCellRef().setCount(0);
        fillNonRandom(container.get<ESM::Container>()->mBase->mInventory, ESM::RefId(), mSeed);
        addScripts(*this, container.mCell);
        mResolved = false;
    }
}

float MWWorld::ContainerStore::getWeight() const
{
    if (!mWeightUpToDate)
    {
        mCachedWeight = 0;

        mCachedWeight += getTotalWeight(mLists.mPotions);
        mCachedWeight += getTotalWeight(mLists.mAppas);
        mCachedWeight += getTotalWeight(mLists.mArmors);
        mCachedWeight += getTotalWeight(mLists.mBooks);
        mCachedWeight += getTotalWeight(mLists.mClothes);
        mCachedWeight += getTotalWeight(mLists.mIngreds);
        mCachedWeight += getTotalWeight(mLists.mLights);
        mCachedWeight += getTotalWeight(mLists.mLockpicks);
        mCachedWeight += getTotalWeight(mLists.mMiscItems);
        mCachedWeight += getTotalWeight(mLists.mProbes);
        mCachedWeight += getTotalWeight(mLists.mRepairs);
        mCachedWeight += getTotalWeight(mLists.mWeapons);

        mWeightUpToDate = true;
    }

    return mCachedWeight;
}

int MWWorld::ContainerStore::getType(const ConstPtr& ptr)
{
    if (ptr.isEmpty())
        throw std::runtime_error("can't put a non-existent object into a container");

    if (ptr.getType() == ESM::Potion::sRecordId)
        return Type_Potion;

    if (ptr.getType() == ESM::Apparatus::sRecordId)
        return Type_Apparatus;

    if (ptr.getType() == ESM::Armor::sRecordId)
        return Type_Armor;

    if (ptr.getType() == ESM::Book::sRecordId)
        return Type_Book;

    if (ptr.getType() == ESM::Clothing::sRecordId)
        return Type_Clothing;

    if (ptr.getType() == ESM::Ingredient::sRecordId)
        return Type_Ingredient;

    if (ptr.getType() == ESM::Light::sRecordId)
        return Type_Light;

    if (ptr.getType() == ESM::Lockpick::sRecordId)
        return Type_Lockpick;

    if (ptr.getType() == ESM::Miscellaneous::sRecordId)
        return Type_Miscellaneous;

    if (ptr.getType() == ESM::Probe::sRecordId)
        return Type_Probe;

    if (ptr.getType() == ESM::Repair::sRecordId)
        return Type_Repair;

    if (ptr.getType() == ESM::Weapon::sRecordId)
        return Type_Weapon;

    throw std::runtime_error("Object " + ptr.getCellRef().getRefId().toDebugString() + " of type "
        + std::string(ptr.getTypeDescription()) + " can not be placed into a container");
}

MWWorld::Ptr MWWorld::ContainerStore::findReplacement(const ESM::RefId& id)
{
    MWWorld::Ptr item;
    int itemHealth = 1;
    for (auto&& iter : *this)
    {
        int iterHealth = iter.getClass().hasItemHealth(iter) ? iter.getClass().getItemHealth(iter) : 1;
        if (iter.getCellRef().getRefId() == id)
        {
            // Prefer the stack with the lowest remaining uses
            // Try to get item with zero durability only if there are no other items found
            if (item.isEmpty() || (iterHealth > 0 && iterHealth < itemHealth) || (itemHealth <= 0 && iterHealth > 0))
            {
                item = iter;
                itemHealth = iterHealth;
            }
        }
    }

    return item;
}

MWWorld::Ptr MWWorld::ContainerStore::search(const ESM::RefId& id)
{
    resolve();
    {
        Ptr ptr = searchId(mLists.mPotions, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mAppas, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mArmors, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mBooks, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mClothes, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mIngreds, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mLights, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mLockpicks, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mMiscItems, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mProbes, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mRepairs, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    {
        Ptr ptr = searchId(mLists.mWeapons, id, this);
        if (!ptr.isEmpty())
            return ptr;
    }

    return Ptr();
}

int MWWorld::ContainerStore::addItems(int count1, int count2)
{
    int sum = std::abs(count1) + std::abs(count2);
    if (count1 < 0 || count2 < 0)
        return -sum;
    return sum;
}

int MWWorld::ContainerStore::subtractItems(int count1, int count2)
{
    int sum = std::abs(count1) - std::abs(count2);
    if (count1 < 0 || count2 < 0)
        return -sum;
    return sum;
}

void MWWorld::ContainerStore::writeState(ESM::InventoryState& state) const
{
    state.mItems.clear();

    size_t index = 0;
    storeStates(mLists.mPotions, state, index);
    storeStates(mLists.mAppas, state, index);
    storeStates(mLists.mArmors, state, index, true);
    storeStates(mLists.mBooks, state, index, true); // not equipable as such, but for selectedEnchantItem
    storeStates(mLists.mClothes, state, index, true);
    storeStates(mLists.mIngreds, state, index);
    storeStates(mLists.mLockpicks, state, index, true);
    storeStates(mLists.mMiscItems, state, index);
    storeStates(mLists.mProbes, state, index, true);
    storeStates(mLists.mRepairs, state, index);
    storeStates(mLists.mWeapons, state, index, true);
    storeStates(mLists.mLights, state, index, true);
}

void MWWorld::ContainerStore::readState(const ESM::InventoryState& inventory)
{
    clear();
    mModified = true;
    mResolved = true;

    size_t index = 0;
    for (const ESM::ObjectState& state : inventory.mItems)
    {
        int type = MWBase::Environment::get().getESMStore()->find(state.mRef.mRefID);

        size_t thisIndex = index++;

        switch (type)
        {
            case ESM::REC_ALCH:
                getState(mLists.mPotions, state);
                break;
            case ESM::REC_APPA:
                getState(mLists.mAppas, state);
                break;
            case ESM::REC_ARMO:
                readEquipmentState(getState(mLists.mArmors, state), thisIndex, inventory);
                break;
            case ESM::REC_BOOK:
                readEquipmentState(getState(mLists.mBooks, state), thisIndex, inventory);
                break; // not equipable as such, but for selectedEnchantItem
            case ESM::REC_CLOT:
                readEquipmentState(getState(mLists.mClothes, state), thisIndex, inventory);
                break;
            case ESM::REC_INGR:
                getState(mLists.mIngreds, state);
                break;
            case ESM::REC_LOCK:
                readEquipmentState(getState(mLists.mLockpicks, state), thisIndex, inventory);
                break;
            case ESM::REC_MISC:
                getState(mLists.mMiscItems, state);
                break;
            case ESM::REC_PROB:
                readEquipmentState(getState(mLists.mProbes, state), thisIndex, inventory);
                break;
            case ESM::REC_REPA:
                getState(mLists.mRepairs, state);
                break;
            case ESM::REC_WEAP:
                readEquipmentState(getState(mLists.mWeapons, state), thisIndex, inventory);
                break;
            case ESM::REC_LIGH:
                readEquipmentState(getState(mLists.mLights, state), thisIndex, inventory);
                break;
            case 0:
                Log(Debug::Warning) << "Dropping inventory reference to '" << state.mRef.mRefID
                                    << "' (object no longer exists)";
                break;
            default:
                Log(Debug::Warning) << "Warning: Invalid item type in inventory state, refid " << state.mRef.mRefID;
                break;
        }
    }
}

template <class PtrType>
template <class T>
void MWWorld::ContainerStoreIteratorBase<PtrType>::copy(const ContainerStoreIteratorBase<T>& src)
{
    mType = src.mType;
    mMask = src.mMask;
    mContainer = src.mContainer;
    mPtr = src.mPtr;

    switch (src.mType)
    {
        case MWWorld::ContainerStore::Type_Potion:
            mPotion = src.mPotion;
            break;
        case MWWorld::ContainerStore::Type_Apparatus:
            mApparatus = src.mApparatus;
            break;
        case MWWorld::ContainerStore::Type_Armor:
            mArmor = src.mArmor;
            break;
        case MWWorld::ContainerStore::Type_Book:
            mBook = src.mBook;
            break;
        case MWWorld::ContainerStore::Type_Clothing:
            mClothing = src.mClothing;
            break;
        case MWWorld::ContainerStore::Type_Ingredient:
            mIngredient = src.mIngredient;
            break;
        case MWWorld::ContainerStore::Type_Light:
            mLight = src.mLight;
            break;
        case MWWorld::ContainerStore::Type_Lockpick:
            mLockpick = src.mLockpick;
            break;
        case MWWorld::ContainerStore::Type_Miscellaneous:
            mMiscellaneous = src.mMiscellaneous;
            break;
        case MWWorld::ContainerStore::Type_Probe:
            mProbe = src.mProbe;
            break;
        case MWWorld::ContainerStore::Type_Repair:
            mRepair = src.mRepair;
            break;
        case MWWorld::ContainerStore::Type_Weapon:
            mWeapon = src.mWeapon;
            break;
        case -1:
            break;
        default:
            assert(0);
    }
}

template <class PtrType>
void MWWorld::ContainerStoreIteratorBase<PtrType>::incType()
{
    if (mType == 0)
        mType = 1;
    else if (mType != -1)
    {
        mType <<= 1;

        if (mType > ContainerStore::Type_Last)
            mType = -1;
    }
}

template <class PtrType>
void MWWorld::ContainerStoreIteratorBase<PtrType>::nextType()
{
    while (mType != -1)
    {
        incType();

        if ((mType & mMask) && mType > 0)
            if (resetIterator())
                break;
    }
}

template <class PtrType>
bool MWWorld::ContainerStoreIteratorBase<PtrType>::resetIterator()
{
    switch (mType)
    {
        case ContainerStore::Type_Potion:

            mPotion = mContainer->mLists.mPotions.mList.begin();
            return mPotion != mContainer->mLists.mPotions.mList.end();

        case ContainerStore::Type_Apparatus:

            mApparatus = mContainer->mLists.mAppas.mList.begin();
            return mApparatus != mContainer->mLists.mAppas.mList.end();

        case ContainerStore::Type_Armor:

            mArmor = mContainer->mLists.mArmors.mList.begin();
            return mArmor != mContainer->mLists.mArmors.mList.end();

        case ContainerStore::Type_Book:

            mBook = mContainer->mLists.mBooks.mList.begin();
            return mBook != mContainer->mLists.mBooks.mList.end();

        case ContainerStore::Type_Clothing:

            mClothing = mContainer->mLists.mClothes.mList.begin();
            return mClothing != mContainer->mLists.mClothes.mList.end();

        case ContainerStore::Type_Ingredient:

            mIngredient = mContainer->mLists.mIngreds.mList.begin();
            return mIngredient != mContainer->mLists.mIngreds.mList.end();

        case ContainerStore::Type_Light:

            mLight = mContainer->mLists.mLights.mList.begin();
            return mLight != mContainer->mLists.mLights.mList.end();

        case ContainerStore::Type_Lockpick:

            mLockpick = mContainer->mLists.mLockpicks.mList.begin();
            return mLockpick != mContainer->mLists.mLockpicks.mList.end();

        case ContainerStore::Type_Miscellaneous:

            mMiscellaneous = mContainer->mLists.mMiscItems.mList.begin();
            return mMiscellaneous != mContainer->mLists.mMiscItems.mList.end();

        case ContainerStore::Type_Probe:

            mProbe = mContainer->mLists.mProbes.mList.begin();
            return mProbe != mContainer->mLists.mProbes.mList.end();

        case ContainerStore::Type_Repair:

            mRepair = mContainer->mLists.mRepairs.mList.begin();
            return mRepair != mContainer->mLists.mRepairs.mList.end();

        case ContainerStore::Type_Weapon:

            mWeapon = mContainer->mLists.mWeapons.mList.begin();
            return mWeapon != mContainer->mLists.mWeapons.mList.end();
    }

    return false;
}

template <class PtrType>
bool MWWorld::ContainerStoreIteratorBase<PtrType>::incIterator()
{
    switch (mType)
    {
        case ContainerStore::Type_Potion:

            ++mPotion;
            return mPotion == mContainer->mLists.mPotions.mList.end();

        case ContainerStore::Type_Apparatus:

            ++mApparatus;
            return mApparatus == mContainer->mLists.mAppas.mList.end();

        case ContainerStore::Type_Armor:

            ++mArmor;
            return mArmor == mContainer->mLists.mArmors.mList.end();

        case ContainerStore::Type_Book:

            ++mBook;
            return mBook == mContainer->mLists.mBooks.mList.end();

        case ContainerStore::Type_Clothing:

            ++mClothing;
            return mClothing == mContainer->mLists.mClothes.mList.end();

        case ContainerStore::Type_Ingredient:

            ++mIngredient;
            return mIngredient == mContainer->mLists.mIngreds.mList.end();

        case ContainerStore::Type_Light:

            ++mLight;
            return mLight == mContainer->mLists.mLights.mList.end();

        case ContainerStore::Type_Lockpick:

            ++mLockpick;
            return mLockpick == mContainer->mLists.mLockpicks.mList.end();

        case ContainerStore::Type_Miscellaneous:

            ++mMiscellaneous;
            return mMiscellaneous == mContainer->mLists.mMiscItems.mList.end();

        case ContainerStore::Type_Probe:

            ++mProbe;
            return mProbe == mContainer->mLists.mProbes.mList.end();

        case ContainerStore::Type_Repair:

            ++mRepair;
            return mRepair == mContainer->mLists.mRepairs.mList.end();

        case ContainerStore::Type_Weapon:

            ++mWeapon;
            return mWeapon == mContainer->mLists.mWeapons.mList.end();
    }

    return true;
}

template <class PtrType>
template <class T>
bool MWWorld::ContainerStoreIteratorBase<PtrType>::isEqual(const ContainerStoreIteratorBase<T>& other) const
{
    if (mContainer != other.mContainer)
        return false;

    if (mType != other.mType)
        return false;

    switch (mType)
    {
        case ContainerStore::Type_Potion:
            return mPotion == other.mPotion;
        case ContainerStore::Type_Apparatus:
            return mApparatus == other.mApparatus;
        case ContainerStore::Type_Armor:
            return mArmor == other.mArmor;
        case ContainerStore::Type_Book:
            return mBook == other.mBook;
        case ContainerStore::Type_Clothing:
            return mClothing == other.mClothing;
        case ContainerStore::Type_Ingredient:
            return mIngredient == other.mIngredient;
        case ContainerStore::Type_Light:
            return mLight == other.mLight;
        case ContainerStore::Type_Lockpick:
            return mLockpick == other.mLockpick;
        case ContainerStore::Type_Miscellaneous:
            return mMiscellaneous == other.mMiscellaneous;
        case ContainerStore::Type_Probe:
            return mProbe == other.mProbe;
        case ContainerStore::Type_Repair:
            return mRepair == other.mRepair;
        case ContainerStore::Type_Weapon:
            return mWeapon == other.mWeapon;
        case -1:
            return true;
    }

    return false;
}

template <class PtrType>
PtrType* MWWorld::ContainerStoreIteratorBase<PtrType>::operator->() const
{
    mPtr = **this;
    return &mPtr;
}

template <class PtrType>
PtrType MWWorld::ContainerStoreIteratorBase<PtrType>::operator*() const
{
    PtrType ptr;

    switch (mType)
    {
        case ContainerStore::Type_Potion:
            ptr = PtrType(&*mPotion, nullptr);
            break;
        case ContainerStore::Type_Apparatus:
            ptr = PtrType(&*mApparatus, nullptr);
            break;
        case ContainerStore::Type_Armor:
            ptr = PtrType(&*mArmor, nullptr);
            break;
        case ContainerStore::Type_Book:
            ptr = PtrType(&*mBook, nullptr);
            break;
        case ContainerStore::Type_Clothing:
            ptr = PtrType(&*mClothing, nullptr);
            break;
        case ContainerStore::Type_Ingredient:
            ptr = PtrType(&*mIngredient, nullptr);
            break;
        case ContainerStore::Type_Light:
            ptr = PtrType(&*mLight, nullptr);
            break;
        case ContainerStore::Type_Lockpick:
            ptr = PtrType(&*mLockpick, nullptr);
            break;
        case ContainerStore::Type_Miscellaneous:
            ptr = PtrType(&*mMiscellaneous, nullptr);
            break;
        case ContainerStore::Type_Probe:
            ptr = PtrType(&*mProbe, nullptr);
            break;
        case ContainerStore::Type_Repair:
            ptr = PtrType(&*mRepair, nullptr);
            break;
        case ContainerStore::Type_Weapon:
            ptr = PtrType(&*mWeapon, nullptr);
            break;
    }

    if (ptr.isEmpty())
        throw std::runtime_error("invalid iterator");

    ptr.setContainerStore(mContainer);

    return ptr;
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>& MWWorld::ContainerStoreIteratorBase<PtrType>::operator++()
{
    do
    {
        if (incIterator())
            nextType();
    } while (mType != -1 && !(**this).getCellRef().getCount(false));

    return *this;
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType> MWWorld::ContainerStoreIteratorBase<PtrType>::operator++(int)
{
    ContainerStoreIteratorBase<PtrType> iter(*this);
    ++*this;
    return iter;
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>& MWWorld::ContainerStoreIteratorBase<PtrType>::operator=(
    const ContainerStoreIteratorBase<PtrType>& rhs)
{
    if (this != &rhs)
    {
        copy(rhs);
    }
    return *this;
}

template <class PtrType>
int MWWorld::ContainerStoreIteratorBase<PtrType>::getType() const
{
    return mType;
}

template <class PtrType>
const MWWorld::ContainerStore* MWWorld::ContainerStoreIteratorBase<PtrType>::getContainerStore() const
{
    return mContainer;
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(ContainerStoreType container)
    : mType(-1)
    , mMask(0)
    , mContainer(container)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(int mask, ContainerStoreType container)
    : mType(0)
    , mMask(mask)
    , mContainer(container)
{
    nextType();

    if (mType == -1 || (**this).getCellRef().getCount(false))
        return;

    ++*this;
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Potion>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Potion)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mPotion(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Apparatus>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Apparatus)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mApparatus(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Armor>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Armor)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mArmor(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Book>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Book)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mBook(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Clothing>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Clothing)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mClothing(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Ingredient>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Ingredient)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mIngredient(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Light>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Light)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mLight(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Lockpick>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Lockpick)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mLockpick(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Miscellaneous>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Miscellaneous)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mMiscellaneous(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Probe>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Probe)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mProbe(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Repair>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Repair)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mRepair(iterator)
{
}

template <class PtrType>
MWWorld::ContainerStoreIteratorBase<PtrType>::ContainerStoreIteratorBase(
    ContainerStoreType container, typename Iterator<ESM::Weapon>::type iterator)
    : mType(MWWorld::ContainerStore::Type_Weapon)
    , mMask(MWWorld::ContainerStore::Type_All)
    , mContainer(container)
    , mWeapon(iterator)
{
}

template <class T, class U>
bool MWWorld::operator==(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right)
{
    return left.isEqual(right);
}

template <class T, class U>
bool MWWorld::operator!=(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right)
{
    return !(left == right);
}

template class MWWorld::ContainerStoreIteratorBase<MWWorld::Ptr>;
template class MWWorld::ContainerStoreIteratorBase<MWWorld::ConstPtr>;

template bool MWWorld::operator==(
    const ContainerStoreIteratorBase<Ptr>& left, const ContainerStoreIteratorBase<Ptr>& right);
template bool MWWorld::operator!=(
    const ContainerStoreIteratorBase<Ptr>& left, const ContainerStoreIteratorBase<Ptr>& right);
template bool MWWorld::operator==(
    const ContainerStoreIteratorBase<ConstPtr>& left, const ContainerStoreIteratorBase<ConstPtr>& right);
template bool MWWorld::operator!=(
    const ContainerStoreIteratorBase<ConstPtr>& left, const ContainerStoreIteratorBase<ConstPtr>& right);
template bool MWWorld::operator==(
    const ContainerStoreIteratorBase<ConstPtr>& left, const ContainerStoreIteratorBase<Ptr>& right);
template bool MWWorld::operator!=(
    const ContainerStoreIteratorBase<ConstPtr>& left, const ContainerStoreIteratorBase<Ptr>& right);
template bool MWWorld::operator==(
    const ContainerStoreIteratorBase<Ptr>& left, const ContainerStoreIteratorBase<ConstPtr>& right);
template bool MWWorld::operator!=(
    const ContainerStoreIteratorBase<Ptr>& left, const ContainerStoreIteratorBase<ConstPtr>& right);

template void MWWorld::ContainerStoreIteratorBase<MWWorld::Ptr>::copy(const ContainerStoreIteratorBase<Ptr>& src);
template void MWWorld::ContainerStoreIteratorBase<MWWorld::ConstPtr>::copy(const ContainerStoreIteratorBase<Ptr>& src);
template void MWWorld::ContainerStoreIteratorBase<MWWorld::ConstPtr>::copy(
    const ContainerStoreIteratorBase<ConstPtr>& src);
