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
            Ptr mItem;
            std::shared_ptr<const ScriptRegistration> mRegistration;
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
