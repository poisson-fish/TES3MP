#ifndef OPENMW_APPS_OPENMW_MWWORLD_PTRREGISTRY_H
#define OPENMW_APPS_OPENMW_MWWORLD_PTRREGISTRY_H

#include "ptr.hpp"

#include "components/esm3/cellref.hpp"

#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace MWWorld
{
    class PtrRegistry
    {
        using Index = std::unordered_map<ESM::RefNum, Ptr>;

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

        // Own the stock map type, with Ptrs only to stable pair-owned nodes.
        // Unaffected mappings remain compare-only bindings and occupy empty map
        // slots; resolving their live Ptrs is a separate, later prerequisite.
        class PreparedStorage
        {
            friend class PtrRegistry;
            const Snapshot* mResult; // Pair-owned identity, compared only.
            Snapshot mBindings;
            Index mIndex;
            explicit PreparedStorage(const Snapshot& result)
                : mResult(&result)
                , mBindings(result)
            {
            }

        public:
            PreparedStorage(const PreparedStorage&) = delete;
            PreparedStorage& operator=(const PreparedStorage&) = delete;
            const Snapshot& getBindings() const { return mBindings; }
            // Do not expose const Index: its Ptr values still allow mutation.
            ConstPtr getItem(ESM::RefNum id) const
            {
                const auto it = mIndex.find(id);
                return it == mIndex.end() ? ConstPtr() : ConstPtr(it->second);
            }
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
        static std::unique_ptr<PreparedStorage> prepareStorage(
            const Snapshot& relocated, const std::vector<std::pair<ESM::RefNum, Ptr>>& nodes)
        {
            auto storage = std::unique_ptr<PreparedStorage>(new PreparedStorage(relocated));
            storage->mIndex.reserve(relocated.mEntries.size());
            for (const auto& [id, target] : relocated.mEntries)
            {
                Ptr item;
                // The pair supplies identity membership. An unrelated stale
                // address can be reused by an owned node; it must stay unresolved.
                for (const auto& [identity, node] : nodes)
                    if (id == identity)
                    {
                        if (!target.references(node.mRef))
                            throw std::invalid_argument("Ptr registry storage preparation node mismatch");
                        item = node;
                        item.mCell = target.mCell;
                        item.mContainerStore = const_cast<ContainerStore*>(target.mContainer);
                        break;
                    }
                // Use the already proposed key, never insert()/identity generation
                // or a live mapping. In particular, zero-count nodes stay mapped.
                storage->mIndex.emplace(id, item);
            }
            return storage;
        }

        static void validateStorage(const PreparedStorage& storage, const Snapshot& relocated,
            const std::vector<std::pair<ESM::RefNum, ConstPtr>>& nodes)
        {
            if (storage.mResult != &relocated || storage.mBindings != relocated
                || storage.mIndex.size() != relocated.mEntries.size())
                throw std::invalid_argument(
                    "Ptr registry prepared storage binding, membership, revision or counter changed");
            for (const auto& [id, target] : relocated.mEntries)
            {
                ConstPtr expected;
                for (const auto& [identity, node] : nodes)
                    if (id == identity)
                    {
                        if (!target.references(node.mRef))
                            throw std::invalid_argument("Ptr registry prepared storage node changed");
                        expected = node;
                        expected.mCell = target.mCell;
                        expected.mContainerStore = target.mContainer;
                        break;
                    }
                const auto it = storage.mIndex.find(id);
                // Compare Ptr fields only. Neither stored map Ptrs nor saved
                // reference keys may be followed when rejecting stale storage.
                if (it == storage.mIndex.end() || it->second.mRef != expected.mRef || it->second.mCell != expected.mCell
                    || it->second.mContainerStore != expected.mContainerStore)
                    throw std::invalid_argument("Ptr registry prepared storage mapping changed");
            }
        }

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
        Index mIndex;
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
