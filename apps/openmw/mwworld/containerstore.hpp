#ifndef GAME_MWWORLD_CONTAINERSTORE_H
#define GAME_MWWORLD_CONTAINERSTORE_H

#include <array>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include <components/misc/rng.hpp>

#include "cellreflist.hpp"
#include "localscripts.hpp"
#include "ptr.hpp"
#include "ptrregistry.hpp"

namespace ESM
{
    struct InventoryList;
    struct InventoryState;
}

namespace MWClass
{
    class Container;
}

namespace MWWorld
{
    class ContainerStore;
    class ESMStore;
    class LocalScripts;
    class WorldModel;
    template <class PtrType>
    class ContainerStoreIteratorBase;
    using ContainerStoreIterator = ContainerStoreIteratorBase<Ptr>;
    using ConstContainerStoreIterator = ContainerStoreIteratorBase<ConstPtr>;

    // Operation-local dependencies. Both script services are required for scripts;
    // nullptr rejects scripted items before mutation.
    // Presentation must be consumed explicitly, even by an offline diagnostic.
    struct ContainerStoreAddContext
    {
        const ESMStore& mStore;
        WorldModel& mWorldModel;
        Ptr mPlayer;
        Ptr mContainer;
        LocalScripts* mLocalScripts;
        MWBase::ScriptManager* mScriptManager;
        std::function<void(const Ptr&)> mInventoryUpdated;
    };

    struct ContainerStoreRemoveContext
    {
        const WorldModel& mWorldModel;
        Ptr mContainer;
        LocalScripts& mLocalScripts;
        std::function<void(const Ptr&)> mInventoryUpdated;
    };

    // Operation-owned destination state/effects. Destruction discards everything;
    // there is deliberately no transfer installation or effect execution API here.
    // The base record, owner and registration cell are borrowed for this operation.
    struct PreparedContainerAdd
    {
        std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> mItem;
        std::optional<LocalScripts::Registration> mScript;
        Ptr mOwner;
        int mCount = 0;
        bool mNotifyItemAdded = false;
        std::function<void(const Ptr&)> mInventoryUpdated;

        // Unset identity means a new detached stack; otherwise the proposed signed
        // count belongs to this existing destination stack; mItem still holds the
        // incoming value. No destination inventory Ptr/iterator is retained.
        ESM::RefNum getStackTarget() const { return mStackTarget; }
        int getStackCount() const { return mStackCount; }

    private:
        friend class ContainerStore;
        friend class PreparedContainerTransfer;
        // MISC has no enchantment, item health or timed usage. These are state
        // witnesses, not a second compatibility predicate: selection uses stacks().
        struct MiscState
        {
            ESM::RefNum mIdentity;
            const LiveCellRefBase* mReference; // Compared only, never dereferenced.
            const ESM::Miscellaneous* mBase;
            ESM::RefId mId, mSoul, mScript;
            int mCount;
            bool mDeleted;
            bool operator==(const MiscState&) const = default;
        };
        static MiscState miscState(const ConstPtr& item);
        std::vector<MiscState> mDestinationState;
        std::optional<MiscState> mItemState;
        const ContainerStore* mDestination = nullptr;
        const ESMStore* mStore = nullptr;
        const WorldModel* mWorldModel = nullptr;
        ESM::RefNum mOwnerIdentity, mStackTarget;
        CellStore* mOwnerCell = nullptr;
        int mStackCount = 0;
    };

    // Owned source decision and deferred deregistration only. No saved inventory
    // Ptr/iterator or installation API. The witness stays detached for full removal.
    class PreparedContainerRemove
    {
    public:
        ESM::RefNum getItemIdentity() const { return mItemIdentity; }
        int getCount() const { return mCount; }
        int getRemainingCount() const { return mRemainingCount; }
        const LocalScripts::Removal* getScriptRemoval() const
        {
            return mRemainingCount == 0 && mScriptState && mScriptState->hasRegistration() ? &*mScriptState : nullptr;
        }

    private:
        friend class ContainerStore;
        std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> mItemState;
        // Partial removal also witnesses registration, but must preserve it.
        std::optional<LocalScripts::Removal> mScriptState;
        const LocalScripts* mLocalScripts = nullptr;
        const ContainerStore* mSource = nullptr;
        const WorldModel* mWorldModel = nullptr;
        const LiveCellRefBase* mItemReference = nullptr; // Compared only.
        const LiveCellRefBase* mOwnerReference = nullptr; // Compared only.
        ESM::RefNum mItemIdentity, mOwnerIdentity;
        CellStore* mOwnerCell = nullptr;
        int mCount = 0;
        int mRemainingCount = 0;
    };

    // One owned pair, constructed only by ContainerStore. Never expose the add
    // aggregate: even a const unique_ptr would allow independent item mutation.
    // Views are read-only and valid only while this decision owns its state.
    class PreparedContainerTransfer
    {
    public:
        PreparedContainerTransfer(PreparedContainerTransfer&&) noexcept;
        PreparedContainerTransfer& operator=(PreparedContainerTransfer&&) noexcept;
        ~PreparedContainerTransfer();

        const PreparedContainerRemove& getRemoval() const;
        // Owned post-removal source value, including zero for full removal.
        // Retains applicable source RefData; detached with no script cleanup.
        ConstPtr getSourceItem() const;
        struct InventoryItem
        {
            ESM::RefNum mIdentity; // Original identity, or unset for the new destination node.
            ConstPtr mItem;
        };
        // Owned non-gold MISC projection in stock iteration order, excluding
        // zero-count nodes. Copies only read-only views, never live references.
        std::vector<InventoryItem> getSourceInventory() const;
        // Existing stack replaced in place, or incoming value appended. Remaining
        // values preserve their state/identity associations; dormant nodes stay
        // owned but are excluded from this non-gold MISC membership view.
        // All values are detached; new membership does not allocate a live identity.
        std::vector<InventoryItem> getDestinationInventory() const;
        // Original identity to retain, or unset for no selection.
        ESM::RefNum getSourceSelection() const;
        // Stock addition retains the original selection, even on a replaced stack.
        ESM::RefNum getDestinationSelection() const;
        // Incoming source-derived value, with the removal quantity. New stacks
        // clear stock activation flags before script registration/OnPCAdd.
        ConstPtr getItem() const;
        // Owned post-add value: preserves the selected destination stack's state,
        // or returns the incoming value for a new stack. Always unregistered.
        ConstPtr getDestinationItem() const;
        ESM::RefNum getStackTarget() const;
        int getStackCount() const;
        std::optional<LocalScripts::Registration> getScriptAddition() const;
        // Entire service results, including unaffected world/other-owner entries.
        // Shared services expose the same combined remove-then-append result.
        // New registration references only the owned detached destination value;
        // all other entries preserve immutable registration identities/bindings.
        const LocalScripts::List& getSourceScripts() const;
        const LocalScripts::List& getDestinationScripts() const;
        // Entire WorldModel result. Existing references are compare-only keys;
        // the new destination binding references the owned detached incoming item.
        // Source identities remain registered even after full removal.
        const PtrRegistry::Snapshot& getRegistry() const;
        ESM::RefNum getDestinationIdentity() const;
        // Owned value associated with a proposed identity in either inventory,
        // including dormant nodes; empty for unaffected world/other-owner entries.
        // The value itself remains detached and has no assigned RefNum.
        ConstPtr getRegistryItem(ESM::RefNum identity) const;
        // Actual stock list storage, including dormant nodes. Values remain
        // detached; even proposed identities are never written into these nodes.
        using MiscList = CellRefList<ESM::Miscellaneous>::List;
        const MiscList& getSourceStorage() const;
        const MiscList& getDestinationStorage() const;
        struct Relocation
        {
            // Raw stock order (including zero counts); original IDs, or unset for
            // the appended node. Every view points into the owned stock lists.
            std::vector<InventoryItem> mSource, mDestination;
            ConstPtr mSourceSelection, mDestinationSelection;
            LocalScripts::List mSourceScripts;
            // Absent for a shared service; mSourceScripts is the combined result.
            std::optional<LocalScripts::List> mDestinationScripts;
            PtrRegistry::Snapshot mRegistry;
        };
        // Rebound associations preserve owner/container/cell hints separately
        // from detached values. Unrelated entries retain compare-only keys.
        const Relocation& getRelocation() const;
        // Stock LocalScripts nodes and cursor relocation, built from the same
        // owned inventory nodes. Shared services return the same storage object.
        // Explicit owners/initiator resolve through captured reference lifetimes.
        // Other unaffected entries keep empty items. No installation is exposed.
        const LocalScripts::PreparedStorage& getSourceScriptStorage() const;
        const LocalScripts::PreparedStorage& getDestinationScriptStorage() const;
        // Stock registry map with the relocated revision/counter and bindings.
        // Item views reference owned stock lists or lifetime-checked contexts;
        // other unaffected mappings retain empty items. No installation API.
        const PtrRegistry::PreparedStorage& getRegistryStorage() const;
        // Compare-only witnesses for private stock iterator positions. The exact
        // relocation object binds the pair, quantity, values and all service results.
        // No saved iterator is exposed by reference, including for fault injection.
        struct IteratorBindings
        {
            const Relocation* mRelocation;
            const MiscList* mSourceStorage;
            const MiscList* mDestinationStorage;
            const LocalScripts::PreparedStorage* mSourceScripts;
            const LocalScripts::PreparedStorage* mDestinationScripts;
            const PtrRegistry::PreparedStorage* mRegistry;
            int mCount;
            bool operator==(const IteratorBindings&) const = default;
        };
        const IteratorBindings& getIteratorBindings() const;
        struct ContextReference
        {
            ESM::RefNum mIdentity;
            ConstPtr mItem;
        };
        struct ContextBindings
        {
            const IteratorBindings* mIterators;
            // Source owner, destination owner, initiator (possibly absent).
            std::array<ContextReference, 3> mReferences;
        };
        // Borrowed read-only contexts, separate from detached inventory values.
        // Views expire on reference destruction; validateTransfer checks current
        // registry/script state before the pair can be accepted again.
        const ContextBindings& getContextBindings() const;
        // Check current owned storage before copying any saved iterator. These
        // read-only copies traverse isolated stock stores/lists and their own end
        // sentinels. They expire with the pair's state; script items may also be
        // borrowed explicit contexts guarded by their reference lifetimes,
        // and neither install a selection nor advance a service's live cursor.
        ConstContainerStoreIterator getSourceSelectionIterator() const;
        ConstContainerStoreIterator getDestinationSelectionIterator() const;
        ConstContainerStoreIterator getSourceEndIterator() const;
        ConstContainerStoreIterator getDestinationEndIterator() const;
        LocalScripts::PreparedStorage::Entries::const_iterator getSourceScriptCursorIterator() const;
        LocalScripts::PreparedStorage::Entries::const_iterator getDestinationScriptCursorIterator() const;
        bool hasRemovalNotification() const;
        bool hasAdditionNotification() const;

    private:
        friend class ContainerStore;
        struct State;
        std::unique_ptr<State> mState;
        explicit PreparedContainerTransfer(std::unique_ptr<State> state);
        const State& state() const;
    };

    class ResolutionListener
    {
        ContainerStore& mStore;

    public:
        ResolutionListener(ContainerStore& store)
            : mStore(store)
        {
        }
        ~ResolutionListener();
    };

    class ResolutionHandle
    {
        std::shared_ptr<ResolutionListener> mListener;

    public:
        ResolutionHandle(std::shared_ptr<ResolutionListener> listener)
            : mListener(std::move(listener))
        {
        }
        ResolutionHandle() = default;
    };

    class ContainerStoreListener
    {
    public:
        virtual void itemAdded(const ConstPtr& item, int count) {}
        virtual void itemRemoved(const ConstPtr& item, int count) {}
        virtual ~ContainerStoreListener() = default;
    };

    template <class PtrType>
    class ContainerStoreIteratorBase
    {
        template <class From, class To, class Dummy>
        struct IsConvertible
        {
            static constexpr bool value = true;
        };

        template <class Dummy>
        struct IsConvertible<ConstPtr, Ptr, Dummy>
        {
            static constexpr bool value = false;
        };

        template <class T, class U>
        struct IteratorTrait
        {
            typedef typename MWWorld::CellRefList<T>::List::iterator type;
        };

        template <class T>
        struct IteratorTrait<T, ConstPtr>
        {
            typedef typename MWWorld::CellRefList<T>::List::const_iterator type;
        };

        template <class T>
        struct Iterator : IteratorTrait<T, PtrType>
        {
        };

        template <class T, class Dummy>
        struct ContainerStoreTrait
        {
            typedef ContainerStore* type;
        };

        template <class Dummy>
        struct ContainerStoreTrait<ConstPtr, Dummy>
        {
            typedef const ContainerStore* type;
        };

        typedef typename ContainerStoreTrait<PtrType, void>::type ContainerStoreType;

        int mType;
        int mMask;
        ContainerStoreType mContainer;
        mutable PtrType mPtr;

        typename Iterator<ESM::Potion>::type mPotion;
        typename Iterator<ESM::Apparatus>::type mApparatus;
        typename Iterator<ESM::Armor>::type mArmor;
        typename Iterator<ESM::Book>::type mBook;
        typename Iterator<ESM::Clothing>::type mClothing;
        typename Iterator<ESM::Ingredient>::type mIngredient;
        typename Iterator<ESM::Light>::type mLight;
        typename Iterator<ESM::Lockpick>::type mLockpick;
        typename Iterator<ESM::Miscellaneous>::type mMiscellaneous;
        typename Iterator<ESM::Probe>::type mProbe;
        typename Iterator<ESM::Repair>::type mRepair;
        typename Iterator<ESM::Weapon>::type mWeapon;

        ContainerStoreIteratorBase(ContainerStoreType container);
        ///< End-iterator

        ContainerStoreIteratorBase(int mask, ContainerStoreType container);
        ///< Begin-iterator

        // construct iterator using a CellRefList iterator
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Potion>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Apparatus>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Armor>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Book>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Clothing>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Ingredient>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Light>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Lockpick>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Miscellaneous>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Probe>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Repair>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Weapon>::type);

        template <class T>
        void copy(const ContainerStoreIteratorBase<T>& src);

        void incType();

        void nextType();

        bool resetIterator();
        ///< Reset iterator for selected type.
        ///
        /// \return Type not empty?

        bool incIterator();
        ///< Increment iterator for selected type.
        ///
        /// \return reached the end?

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = PtrType;
        using difference_type = std::ptrdiff_t;
        using pointer = PtrType*;
        using reference = PtrType&;

        template <class T>
        ContainerStoreIteratorBase(const ContainerStoreIteratorBase<T>& other)
        {
            static_assert(IsConvertible<T, PtrType, void>::value);
            copy(other);
        }

        template <class T>
        bool isEqual(const ContainerStoreIteratorBase<T>& other) const;

        PtrType* operator->() const;
        PtrType operator*() const;

        ContainerStoreIteratorBase& operator++();
        ContainerStoreIteratorBase operator++(int);
        ContainerStoreIteratorBase& operator=(const ContainerStoreIteratorBase& rhs);
        ContainerStoreIteratorBase(const ContainerStoreIteratorBase& rhs) = default;

        int getType() const;
        const ContainerStore* getContainerStore() const;

        friend class ContainerStore;
        friend class ContainerStoreIteratorBase<Ptr>;
        friend class ContainerStoreIteratorBase<ConstPtr>;
    };

    class ContainerStore
    {
        struct Lists
        {
            MWWorld::CellRefList<ESM::Potion> mPotions;
            MWWorld::CellRefList<ESM::Apparatus> mAppas;
            MWWorld::CellRefList<ESM::Armor> mArmors;
            MWWorld::CellRefList<ESM::Book> mBooks;
            MWWorld::CellRefList<ESM::Clothing> mClothes;
            MWWorld::CellRefList<ESM::Ingredient> mIngreds;
            MWWorld::CellRefList<ESM::Light> mLights;
            MWWorld::CellRefList<ESM::Lockpick> mLockpicks;
            MWWorld::CellRefList<ESM::Miscellaneous> mMiscItems;
            MWWorld::CellRefList<ESM::Probe> mProbes;
            MWWorld::CellRefList<ESM::Repair> mRepairs;
            MWWorld::CellRefList<ESM::Weapon> mWeapons;
        };

    public:
        static constexpr int Type_Potion = 0x0001;
        static constexpr int Type_Apparatus = 0x0002;
        static constexpr int Type_Armor = 0x0004;
        static constexpr int Type_Book = 0x0008;
        static constexpr int Type_Clothing = 0x0010;
        static constexpr int Type_Ingredient = 0x0020;
        static constexpr int Type_Light = 0x0040;
        static constexpr int Type_Lockpick = 0x0080;
        static constexpr int Type_Miscellaneous = 0x0100;
        static constexpr int Type_Probe = 0x0200;
        static constexpr int Type_Repair = 0x0400;
        static constexpr int Type_Weapon = 0x0800;

        static constexpr int Type_Last = Type_Weapon;

        static constexpr int Type_All = 0xffff;

        static const ESM::RefId sGoldId;

        static constexpr bool isStorableType(unsigned int t)
        {
            return t == ESM::Potion::sRecordId || t == ESM::Apparatus::sRecordId || t == ESM::Armor::sRecordId
                || t == ESM::Book::sRecordId || t == ESM::Clothing::sRecordId || t == ESM::Ingredient::sRecordId
                || t == ESM::Light::sRecordId || t == ESM::Lockpick::sRecordId || t == ESM::Miscellaneous::sRecordId
                || t == ESM::Probe::sRecordId || t == ESM::Repair::sRecordId || t == ESM::Weapon::sRecordId;
        }
        template <typename T>
        static constexpr bool isStorableType()
        {
            return isStorableType(T::sRecordId);
        }

    protected:
        ContainerStoreListener* mListener = nullptr;

        // Used in clone() to unset refnums of copies.
        // (RefNum should be unique, copy can not have the same RefNum).
        void updateRefNums();

        // (item, max charge)
        typedef std::vector<std::pair<ContainerStoreIterator, float>> TRechargingItems;
        TRechargingItems mRechargingItems;

        // selected magic item (for using enchantments of type "Cast once" or "Cast when used")
        ContainerStoreIterator mSelectedEnchantItem;

    private:
        Lists mLists;

        // Assigning/moving storage may destroy or reuse list node addresses and
        // item IDs. A decision owns this token so replacement cannot reuse it.
        struct StorageIdentity
        {
        };
        std::shared_ptr<const StorageIdentity> mStorageIdentity = std::make_shared<const StorageIdentity>();

        mutable float mCachedWeight = 0;
        unsigned int mSeed = 0;
        MWWorld::SafePtr mPtr; // Container or actor that holds this store.
        std::weak_ptr<ResolutionListener> mResolutionListener;

        mutable bool mWeightUpToDate = false;
        bool mModified = false;
        bool mResolved = false;

    protected:
        bool mRechargingItemsUpToDate = false;

        virtual void storeEquipmentState(
            const MWWorld::LiveCellRefBase& ref, size_t index, ESM::InventoryState& inventory) const;

        virtual void readEquipmentState(
            const MWWorld::ContainerStoreIterator& iter, size_t index, const ESM::InventoryState& inventory);

        std::ptrdiff_t index(const ContainerStoreIterator& iter) const;

    private:
        ContainerStoreIterator addImp(const ConstPtr& ptr, int count, const ESMStore& store);
        void validateTransferCount(const ConstPtr& item, int count) const;
        void validateTransferSource(const ConstPtr& item, int count, const WorldModel& worldModel) const;
        ESM::RefNum transferSelection() const;
        static PreparedContainerTransfer::Relocation relocateTransfer(const PreparedContainerTransfer& prepared);
        static void validateTransferStorage(const PreparedContainerTransfer& prepared);
        static void prepareTransferIterators(PreparedContainerTransfer& prepared);
        static void validateTransferIterators(const PreparedContainerTransfer& prepared);
        static void validateTransferContextBindings(const PreparedContainerTransfer& prepared);
        PreparedContainerAdd prepareTransferAdd(std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> item,
            const ContainerStoreAddContext& context, LocalScripts::PreparedList* scriptList);
        struct ItemRemoval
        {
            int mRemoved;
            int mRemainingCount;
            bool mFullRemoval;
        };
        static ItemRemoval prepareRemoveCount(const CellRef& item, int count);
        ContainerStoreIterator addWithContext(
            const ConstPtr& ptr, int count, const ContainerStoreAddContext& context, bool resolve);
        int removeWithContext(const Ptr& item, int count, const ContainerStoreRemoveContext& context, bool resolve);
        void validateExplicitOwner(const ConstPtr& owner, const WorldModel& worldModel) const;
        void resolve(const Ptr& container);
        void addInitialItem(
            const ESM::RefId& id, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng, bool topLevel = true);
        void addInitialItemImp(const MWWorld::Ptr& ptr, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng,
            bool topLevel = true);

        template <typename T>
        ContainerStoreIterator getState(CellRefList<T>& collection, const ESM::ObjectState& state);

        template <typename T>
        void storeState(const LiveCellRef<T>& ref, ESM::ObjectState& state) const;

        template <typename T>
        void storeStates(const CellRefList<T>& collection, ESM::InventoryState& inventory, size_t& index,
            bool equipable = false) const;

        void updateRechargingItems();

    public:
        ContainerStore();
        ContainerStore(const ContainerStore& store);
        ContainerStore(ContainerStore&& store);

        ContainerStore& operator=(const ContainerStore&);
        ContainerStore& operator=(ContainerStore&&);

        virtual ~ContainerStore() = default;

        virtual std::unique_ptr<ContainerStore> clone()
        {
            auto res = std::make_unique<ContainerStore>(*this);
            res->updateRefNums();
            return res;
        }

        // Container or actor that holds this store.
        const Ptr& getPtr() const { return mPtr.ptrOrEmpty(); }
        void setPtr(const Ptr& ptr) { mPtr = SafePtr(ptr); }
        Ptr getPtr(const WorldModel& worldModel) const;
        void setPtr(const Ptr& ptr, const WorldModel& worldModel);

        ConstContainerStoreIterator cbegin(int mask = Type_All) const;
        ConstContainerStoreIterator cend() const;
        ConstContainerStoreIterator begin(int mask = Type_All) const;
        ConstContainerStoreIterator end() const;

        ContainerStoreIterator begin(int mask = Type_All);
        ContainerStoreIterator end();

        bool hasVisibleItems() const;

        virtual ContainerStoreIterator add(
            const ConstPtr& itemPtr, int count, bool allowAutoEquip = true, bool resolve = true);
        ///< Add the item pointed to by \a ptr to this container. (Stacks automatically if needed)
        ///
        /// \note The item pointed to is not required to exist beyond this function call.
        ///
        /// \attention Do not add items to an existing stack by increasing the count instead of
        /// calling this function!
        ///
        /// @return if stacking happened, return iterator to the item that was stacked against, otherwise iterator to
        /// the newly inserted item.

        ContainerStoreIterator add(const ESM::RefId& id, int count, bool allowAutoEquip = true);
        ///< Utility to construct a ManualRef and call add(ptr, count, actorPtr, true)

        // Explicit-context entries require a resolved base store bound to a registered
        // owner using setPtr. Counts must be positive; additions must not overflow.
        // InventoryStore's equipment, listeners and actor services have not yet been separated.
        // This is shared engine mutation, not a transactional server command API.
        ContainerStoreIterator add(const ConstPtr& item, int count, const ContainerStoreAddContext& context);

        // Removes up to count, as in stock OpenMW. Empty/dead/foreign items reject.
        // Effects run synchronously after mutation; failure/transfer staging is separate work.
        int remove(const Ptr& item, int count, const ContainerStoreRemoveContext& context);

        // Preparation only, for distinct registered owners of resolved base stores.
        // Currently accepts non-gold MISC, including already initialized MWScript locals.
        // Returns a fresh, unregistered reference with count items and no live store/cell
        // or scene links. The ESM base record remains borrowed from the loaded store.
        // No stacking, script registration/OnPCAdd, installation, or notifications occur.
        // The caller owns this temporary until discarded; it is NOT a commit-ready transfer.
        std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> prepareTransferItem(const ConstPtr& item, int count,
            const ContainerStore& destination, const ConstPtr& sourceOwner, const ConstPtr& destinationOwner,
            const WorldModel& worldModel) const;

        // Consume a detached item from prepareTransferItem and prepare stock add
        // stacking decision, normalization, locals/OnPCAdd and deferred effects.
        // Destination MISC nodes must be registered and free of Lua/custom state.
        // Failure destroys the consumed temporary; neither live inventory is changed.
        // No live stacking, registration, iteration, script execution or success emission.
        // This remains operation-local staging, NOT a validated commit-ready transfer.
        PreparedContainerAdd prepareTransferAdd(
            std::unique_ptr<LiveCellRef<ESM::Miscellaneous>> item, const ContainerStoreAddContext& context);

        // Narrow state validation only, not a transfer/commit precondition. Dependencies
        // (this store, content, world model, owner/cell) must still be alive. Reacquires
        // destination members instead of dereferencing saved inventory references.
        // Does not validate source state, script/effect intents, or durability.
        void validateTransferStacking(
            const PreparedContainerAdd& prepared, const ContainerStoreAddContext& context) const;

        // Non-gold MISC with matching initialized MWScript locals. Scripted items
        // require explicit LocalScripts; Lua/custom state and equipment reject.
        // Uses stock removal counts without applying them, including zero. These
        // independent decisions do not bind destination state or detached add values.
        PreparedContainerRemove prepareTransferRemove(const ConstPtr& item, int count, const ConstPtr& sourceOwner,
            const WorldModel& worldModel, const LocalScripts* localScripts = nullptr) const;

        // Current-state validation, not a mutation-history or atomic-transfer check.
        // Source store, content, world model, LocalScripts and owner/cell must remain
        // alive; inventory nodes/script entries may be destroyed. Only current
        // members are read. Registration replacement rejects even at the same address.
        void validateTransferRemoval(const PreparedContainerRemove& prepared, const ConstPtr& sourceOwner,
            const WorldModel& worldModel, const LocalScripts* localScripts = nullptr) const;

        // Paired preparation only. Captures removal, detached source/incoming/destination
        // inventory results/selections, stacking, script intents and both notification
        // consumers as one unit.
        // Both contexts require LocalScripts, even for plain registration absence.
        // No live removal, installation, effects, persistence or atomic transfer.
        PreparedContainerTransfer prepareTransfer(const ConstPtr& item, int count, ContainerStore& destination,
            const ContainerStoreRemoveContext& sourceContext, const ContainerStoreAddContext& destinationContext) const;

        // Narrow current-state check; re-acquires inventory nodes before reading.
        // Stores/content/owners/cells/services must outlive the decision. Consumers
        // are owned snapshots, not re-sourced from the validation contexts; service,
        // player, owner and listener bindings must still match. No script calls.
        void validateTransfer(const PreparedContainerTransfer& prepared, const ContainerStore& destination,
            const ContainerStoreRemoveContext& sourceContext, const ContainerStoreAddContext& destinationContext) const;

        int remove(const ESM::RefId& itemId, int count, bool equipReplacement = 0, bool resolve = true);
        ///< Remove \a count item(s) designated by \a itemId from this container.
        ///
        /// @return the number of items actually removed

        virtual int remove(const Ptr& item, int count, bool equipReplacement = 0, bool resolve = true);
        ///< Remove \a count item(s) designated by \a item from this inventory.
        ///
        /// @return the number of items actually removed

        void setSelectedEnchantItem(const ContainerStoreIterator& iterator);
        ///< set the selected magic item (for using enchantments of type "Cast once" or "Cast when used")
        /// \note to unset the selected item, call this method with end() iterator

        ContainerStoreIterator getSelectedEnchantItem();
        ConstContainerStoreIterator getSelectedEnchantItem() const;
        ///< @return selected magic item (for using enchantments of type "Cast once" or "Cast when used")
        /// \note if no item selected, return end() iterator

        void rechargeItems(float duration);
        ///< Restore charge on enchanted items. Note this should only be done for the player.

        ContainerStoreIterator unstack(const Ptr& ptr, int count = 1);
        ///< Unstack an item in this container. The item's count will be set to count, then a new stack will be added
        ///< with (origCount-count).
        ///
        /// @return an iterator to the new stack, or end() if no new stack was created.

        MWWorld::ContainerStoreIterator restack(const MWWorld::Ptr& item);
        ///< Attempt to re-stack an item in this container.
        /// If a compatible stack is found, the item's count is added to that stack, then the original is deleted.
        /// @return If the item was stacked, return the stack, otherwise return the old (untouched) item.

        int count(const ESM::RefId& id) const;
        ///< @return How many items with refID \a id are in this container?

        ContainerStoreListener* getContListener() const;
        void setContListener(ContainerStoreListener* listener);

    protected:
        ContainerStoreIterator addNewStack(const ConstPtr& ptr, int count);
        ///< Add the item to this container (do not try to stack it onto existing items)

        virtual void flagAsModified();

        /// + and - operations that can deal with negative stacks
        /// Note that negativity is infectious
        static int addItems(int count1, int count2);
        static int subtractItems(int count1, int count2);

    public:
        bool stacks(const ConstPtr& ptr1, const ConstPtr& ptr2) const;
        virtual bool stacks(const ConstPtr& ptr1, const ConstPtr& ptr2, const ESMStore& store) const;
        ///< @return true if the two specified objects can stack with each other

        void fill(const ESM::InventoryList& items, const ESM::RefId& owner, Misc::Rng::Generator& seed);
        ///< Insert items into *this.

        void fillNonRandom(const ESM::InventoryList& items, const ESM::RefId& owner, unsigned int seed);
        ///< Insert items into *this, excluding leveled items

        virtual void clear();
        ///< Empty container.

        float getWeight() const;
        ///< Return total weight of the items contained in *this.

        static int getType(const ConstPtr& ptr);
        ///< This function throws an exception, if ptr does not point to an object, that can be
        /// put into a container.

        Ptr findReplacement(const ESM::RefId& id);
        ///< Returns replacement for object with given id. Prefer used items (with low durability left).

        Ptr search(const ESM::RefId& id);

        virtual void writeState(ESM::InventoryState& state) const;

        virtual void readState(const ESM::InventoryState& state);

        bool isResolved() const;

        void resolve();
        ResolutionHandle resolveTemporarily();
        void unresolve();

        friend class ContainerStoreIteratorBase<Ptr>;
        friend class ContainerStoreIteratorBase<ConstPtr>;
        friend class ResolutionListener;
        friend class MWClass::Container;
        friend class PreparedContainerTransfer;
    };

    template <class T, class U>
    bool operator==(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right);
    template <class T, class U>
    bool operator!=(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right);
}
#endif
