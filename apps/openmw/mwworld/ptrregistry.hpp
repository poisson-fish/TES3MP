#ifndef OPENMW_APPS_OPENMW_MWWORLD_PTRREGISTRY_H
#define OPENMW_APPS_OPENMW_MWWORLD_PTRREGISTRY_H

#include "ptr.hpp"

#include "components/esm3/cellref.hpp"

#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace MWWorld
{
    class PtrRegistry
    {
    public:
        // Owned mapping witnesses. Address keys are compared only: snapshotting
        // never dereferences registered objects, including unrelated stale Ptrs.
        class Binding
        {
            friend class PtrRegistry;
            const LiveCellRefBase* mReference = nullptr;
            CellStore* mCell = nullptr;
            const ContainerStore* mContainer = nullptr;

        public:
            bool references(const LiveCellRefBase* ref) const { return mReference == ref; }
            CellStore* getCell() const { return mCell; }
            const ContainerStore* getContainer() const { return mContainer; }
            bool operator==(const Binding&) const = default;
        };
        struct Snapshot
        {
            std::map<ESM::RefNum, Binding> mEntries;
            std::size_t mRevision = 0;
            ESM::RefNum mLastGenerated;
            bool operator==(const Snapshot&) const = default;
        };

        Snapshot snapshot() const
        {
            Snapshot result;
            result.mRevision = mRevision;
            result.mLastGenerated = mLastGenerated;
            for (const auto& [id, ptr] : mIndex)
                result.mEntries.emplace(id, binding(ptr.mRef, ptr.mCell, ptr.mContainerStore));
            return result;
        }

        std::size_t getRevision() const { return mRevision; }

        ESM::RefNum getLastGenerated() const { return mLastGenerated; }

        auto begin() const { return mIndex.cbegin(); }

        auto end() const { return mIndex.cend(); }

        Ptr getOrEmpty(ESM::RefNum refNum) const
        {
            const auto it = mIndex.find(refNum);
            if (it != mIndex.end())
                return it->second;
            return Ptr();
        }

        void setLastGenerated(ESM::RefNum v) { mLastGenerated = v; }

        void clear()
        {
            mIndex.clear();
            mLastGenerated = ESM::RefNum{};
            ++mRevision;
        }

        void insert(const Ptr& ptr)
        {
            mIndex[ptr.getCellRef().getOrAssignRefNum(mLastGenerated)] = ptr;
            ++mRevision;
        }

        void remove(const LiveCellRefBase& ref) noexcept
        {
            ESM::RefNum refNum = ref.mRef.getRefNum();
            if (!refNum.isSet())
                return;
            auto it = mIndex.find(refNum);
            if (it != mIndex.end() && it->second.mRef == &ref)
            {
                mIndex.erase(it);
                ++mRevision;
            }
        }

        // For fixing old saves
        void assign(ESM::CellRef& ref)
        {
            if (!ref.mRefNum.isSet())
            {
                CellRef temp(ref);
                temp.getOrAssignRefNum(mLastGenerated);
                ref.mRefNum = temp.getRefNum();
            }
        }

    private:
        friend class ContainerStore;
        static Binding binding(const LiveCellRefBase* ref, CellStore* cell, const ContainerStore* container)
        {
            Binding result;
            result.mReference = ref;
            result.mCell = cell;
            result.mContainer = container;
            return result;
        }

        // Mirror insert using stock CellRef identity generation on a scratch value
        // and an owned counter. Never assign an identity to the proposed item.
        static ESM::RefNum prepareInsert(Snapshot& result, CellRef identity, const Binding& target)
        {
            if (!identity.getRefNum().isSet())
            {
                const auto last = result.mLastGenerated;
                if (last.mContentFile >= 0
                    || (last.mIndex == std::numeric_limits<uint32_t>::max()
                        && last.mContentFile == std::numeric_limits<int32_t>::min()))
                    throw std::invalid_argument("Container transfer preparation identity counter invalid or exhausted");
                const auto generated = identity.getOrAssignRefNum(result.mLastGenerated);
                // Stock overwrites a collision. Preparation cannot preserve the
                // unaffected mappings in that case, so reject the invalid counter.
                if (result.mEntries.contains(generated))
                    throw std::invalid_argument("Container transfer preparation generated identity collision");
            }
            const auto id = identity.getRefNum();
            result.mEntries[id] = target;
            ++result.mRevision; // Stock insert increments even for the same binding.
            return id;
        }

        std::size_t mRevision = 0;
        std::unordered_map<ESM::RefNum, Ptr> mIndex;
        ESM::RefNum mLastGenerated;
    };

    class PtrRegistryView
    {
    public:
        explicit PtrRegistryView(const PtrRegistry& ref)
            : mPtr(&ref)
        {
        }

        auto begin() const { return mPtr->begin(); }

        auto end() const { return mPtr->end(); }

    private:
        const PtrRegistry* mPtr;
    };
}

#endif
