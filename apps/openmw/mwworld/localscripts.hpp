#ifndef GAME_MWWORLD_LOCALSCRIPTS_H
#define GAME_MWWORLD_LOCALSCRIPTS_H

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "ptr.hpp"

namespace MWWorld
{
    class ESMStore;
    class CellStore;
    class RefData;

    /// \brief List of active local scripts
    class LocalScripts
    {
        // Immutable, operation-shareable identity. It owns values, never a live
        // reference or iterator; address keys are only compared during lookup.
        struct ScriptRegistration
        {
            ESM::RefId mScript;
            const CellRef* mReference;
            CellStore* mCell;
            ContainerStore* mContainer;
        };
        struct Entry
        {
            friend class LocalScripts;
            friend class ContainerStore;

        private:
            Ptr mItem;
            std::shared_ptr<const ScriptRegistration> mRegistration;
            PreparedNodeIdentity mPreparedIdentity;

        public:
            Entry(Ptr item, std::shared_ptr<const ScriptRegistration> registration)
                : mItem(item)
                , mRegistration(std::move(registration))
            {
            }
            ConstPtr getItem() const
            {
                if (!mItem.isEmpty() && !mItem.hasLiveReference())
                    throw std::invalid_argument("Local script prepared item lifetime changed");
                return mItem;
            }
            ESM::RefId getScript() const { return mRegistration->mScript; }
            const CellStore* getCell() const { return mRegistration->mCell; }
            const ContainerStore* getContainer() const { return mRegistration->mContainer; }
            bool references(const CellRef* ref) const { return mRegistration->mReference == ref; }
        };
        using Scripts = std::list<Entry>;
        Scripts mScripts;
        Scripts::iterator mIter;
        const MWWorld::ESMStore& mStore;

        Scripts::const_iterator find(const CellRef* ref) const;
        void erase(Scripts::const_iterator iter);

    public:
        // Read-only witness of stock remove's first match (including absence).
        // Sharing the immutable registration prevents identity reuse after erase.
        // No deregistration/installation API is exposed for this owned intent.
        class Removal
        {
            friend class LocalScripts;
            const LocalScripts* mScripts = nullptr;
            const CellRef* mReference = nullptr;
            std::shared_ptr<const ScriptRegistration> mRegistration;

        public:
            bool hasRegistration() const { return mRegistration != nullptr; }
            ESM::RefId getScript() const { return mRegistration ? mRegistration->mScript : ESM::RefId(); }
            CellStore* getCell() const { return mRegistration ? mRegistration->mCell : nullptr; }
            ContainerStore* getContainer() const { return mRegistration ? mRegistration->mContainer : nullptr; }
            // Address comparison only; never access a possibly destroyed item.
            bool references(const CellRef* ref) const { return mReference == ref; }
            bool operator==(const Removal&) const = default;
        };

        // Owned registration witnesses in stock order; cursor == size means end.
        // Neither the snapshot nor its entries retain live Ptrs or iterators.
        struct List
        {
            std::vector<Removal> mEntries;
            size_t mCursor = 0;
            bool operator==(const List&) const = default;
        };
        List snapshot() const;

        // Owns the same node type/list as the live service. Ptrs resolve pair-owned
        // items and lifetime-checked explicit contexts. All other entries retain
        // immutable compare-only bindings until a future resolution slice.
        class PreparedStorage
        {
            friend class LocalScripts;
            friend class ContainerStore;
            const LocalScripts* mService;
            Scripts mEntries;
            std::vector<const Entry*> mNodes;
            // Compare-only witness, nullptr for end. The protected transfer keeps
            // the stock iterator separately and validates these current nodes first.
            const Entry* mCursor = nullptr;
            explicit PreparedStorage(const LocalScripts* service)
                : mService(service)
            {
            }

        public:
            using Entries = Scripts;
            PreparedStorage(const PreparedStorage&) = delete;
            PreparedStorage& operator=(const PreparedStorage&) = delete;
            const Entries& getEntries() const { return mEntries; }
            const Entry* const& getCursor() const { return mCursor; }
        };

        Removal prepareRemove(const CellRef* ref) const;
        void validateRemoval(const Removal& prepared, const CellRef* ref) const;

        // An operation-owned intent, with no item pointer or link to the live list.
        // The cell is borrowed and must outlive preparation and any later installation.
        struct Registration
        {
            ESM::RefId mScript;
            CellStore* mCell;
        };

    private:
        friend class ContainerStore;
        friend class PreparedContainerTransfer;
        struct PreparedList
        {
            List mOriginal, mResult;
        };
        PreparedList prepareList(const Removal* removal) const;
        void prepareListAddition(
            PreparedList& prepared, const Registration& addition, const CellRef* ref, ContainerStore* container) const;
        void validateList(const PreparedList& prepared, const Removal* removal, const Registration* addition,
            const CellRef* ref, const ContainerStore* container) const;
        using Relocations = std::vector<std::pair<Removal, const CellRef*>>;
        // Rebind owned results only. Original registration identities remain in
        // the protected list as witnesses; no live Ptr or iterator is followed.
        static List relocateList(const List& original, const Relocations& bindings);
        static bool sameRelocatedList(const List& left, const List& right, const List& original);
        std::unique_ptr<PreparedStorage> prepareStorage(const List& relocated, const List& original,
            const std::vector<Ptr>& nodes, const std::vector<Ptr>& contexts) const;
        void validateStorage(const PreparedStorage& storage, const List& relocated, const List& original,
            const std::vector<ConstPtr>& nodes, const std::vector<ConstPtr>& contexts) const;
        void validateContextBindings(const std::vector<ConstPtr>& contexts) const;
        void validateInventoryBindings(const std::vector<ConstPtr>& nodes, CellStore* ownerCell) const;

    public:
        // Initializes only the supplied RefData. Exceptions propagate to the staging
        // owner; stock add retains its logging/catch and live-list ordering below.
        static Registration prepareAdd(
            const ESM::Script& script, RefData& data, CellStore* cell, MWBase::ScriptManager& scripts);

        LocalScripts(const MWWorld::ESMStore& store);

        bool usesStore(const ESMStore& store) const { return &mStore == &store; }

        void startIteration();
        ///< Set the iterator to the begin of the script list.

        bool getNext(std::pair<ESM::RefId, Ptr>& script);
        ///< Get next local script
        /// @return Did we get a script?

        void add(const ESM::RefId& scriptName, const Ptr& ptr);
        ///< Add script to collection of active local scripts.

        void add(const ESM::RefId& scriptName, const Ptr& ptr, MWBase::ScriptManager& scripts);
        ///< Same registration and initialization with an explicit script service.

        void addCell(CellStore* cell);
        ///< Add all local scripts in a cell.

        void clear();
        ///< Clear active local scripts collection.

        void clearCell(CellStore* cell);
        ///< Remove all scripts belonging to \a cell.

        void remove(const MWWorld::CellRef* ref);

        void remove(const Ptr& ptr);
        ///< Remove script for given reference (ignored if reference does not have a script listed).

        bool isRunning(const ESM::RefId&, const Ptr&) const;
        ///< Is the local script running?.
    };
}

#endif
