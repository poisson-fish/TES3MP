#include "localscripts.hpp"

#include <algorithm>
#include <stdexcept>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadscpt.hpp>

#include "../mwbase/environment.hpp"

#include "cellstore.hpp"
#include "class.hpp"
#include "containerstore.hpp"
#include "esmstore.hpp"

namespace
{

    struct AddScriptsVisitor
    {
        AddScriptsVisitor(MWWorld::LocalScripts& scripts)
            : mScripts(scripts)
        {
        }
        MWWorld::LocalScripts& mScripts;

        bool operator()(const MWWorld::Ptr& ptr)
        {
            if (ptr.mRef->isDeleted())
                return true;

            const ESM::RefId& script = ptr.getClass().getScript(ptr);

            if (!script.empty())
                mScripts.add(script, ptr);

            return true;
        }
    };

    struct AddContainerItemScriptsVisitor
    {
        AddContainerItemScriptsVisitor(MWWorld::LocalScripts& scripts)
            : mScripts(scripts)
        {
        }
        MWWorld::LocalScripts& mScripts;

        bool operator()(const MWWorld::Ptr& containerPtr)
        {
            // Ignore containers without generated content
            if (containerPtr.getType() == ESM::Container::sRecordId
                && containerPtr.getRefData().getCustomData() == nullptr)
                return true;

            MWWorld::ContainerStore& container = containerPtr.getClass().getContainerStore(containerPtr);
            for (const auto& ptr : container)
            {
                const ESM::RefId& script = ptr.getClass().getScript(ptr);
                if (!script.empty())
                {
                    MWWorld::Ptr item = ptr;
                    item.mCell = containerPtr.getCell();
                    mScripts.add(script, item);
                }
            }
            return true;
        }
    };

}

MWWorld::LocalScripts::LocalScripts(const MWWorld::ESMStore& store)
    : mStore(store)
{
    mIter = mScripts.end();
}

void MWWorld::LocalScripts::startIteration()
{
    mIter = mScripts.begin();
}

bool MWWorld::LocalScripts::getNext(std::pair<ESM::RefId, Ptr>& script)
{
    if (mIter != mScripts.end())
    {
        auto iter = mIter++;
        script = { iter->mRegistration->mScript, iter->mItem };
        return true;
    }
    return false;
}

void MWWorld::LocalScripts::add(const ESM::RefId& scriptName, const Ptr& ptr)
{
    add(scriptName, ptr, *MWBase::Environment::get().getScriptManager());
}

MWWorld::LocalScripts::Registration MWWorld::LocalScripts::prepareAdd(
    const ESM::Script& script, RefData& data, CellStore* cell, MWBase::ScriptManager& scripts)
{
    data.setLocals(script, scripts);
    return { script.mId, cell };
}

void MWWorld::LocalScripts::add(const ESM::RefId& scriptName, const Ptr& ptr, MWBase::ScriptManager& scripts)
{
    if (const ESM::Script* script = mStore.get<ESM::Script>().search(scriptName))
    {
        try
        {
            const auto prepared = prepareAdd(*script, ptr.getRefData(), ptr.mCell, scripts);

            for (auto iter = mScripts.begin(); iter != mScripts.end(); ++iter)
                if (iter->mItem == ptr)
                {
                    Log(Debug::Warning) << "Error: tried to add local script twice for " << ptr.getCellRef().getRefId();
                    remove(ptr);
                    break;
                }

            auto registered = ptr;
            registered.mCell = prepared.mCell;
            mScripts.push_back({ registered,
                std::make_shared<const ScriptRegistration>(
                    ScriptRegistration{ prepared.mScript, &ptr.getCellRef(), prepared.mCell, ptr.mContainerStore }) });
        }
        catch (const std::exception& exception)
        {
            Log(Debug::Error) << "failed to add local script " << scriptName
                              << " because an exception has been thrown: " << exception.what();
        }
    }
    else
        Log(Debug::Warning) << "failed to add local script " << scriptName << " because the script does not exist.";
}

void MWWorld::LocalScripts::addCell(CellStore* cell)
{
    AddScriptsVisitor addScriptsVisitor(*this);
    cell->forEach(addScriptsVisitor);

    AddContainerItemScriptsVisitor addContainerItemScriptsVisitor(*this);
    cell->forEachType<ESM::NPC>(addContainerItemScriptsVisitor);
    cell->forEachType<ESM::Creature>(addContainerItemScriptsVisitor);
    cell->forEachType<ESM::Container>(addContainerItemScriptsVisitor);
}

void MWWorld::LocalScripts::clear()
{
    mScripts.clear();
    mIter = mScripts.end();
}

void MWWorld::LocalScripts::clearCell(CellStore* cell)
{
    auto iter = mScripts.begin();

    while (iter != mScripts.end())
    {
        if (iter->mItem.mCell == cell)
        {
            if (iter == mIter)
                ++mIter;

            mScripts.erase(iter++);
        }
        else
            ++iter;
    }
}

void MWWorld::LocalScripts::remove(const MWWorld::CellRef* ref)
{
    erase(find(ref));
}

void MWWorld::LocalScripts::remove(const Ptr& ptr)
{
    erase(std::find_if(mScripts.begin(), mScripts.end(), [&](const Entry& entry) { return entry.mItem == ptr; }));
}

MWWorld::LocalScripts::Scripts::const_iterator MWWorld::LocalScripts::find(const CellRef* ref) const
{
    // A registry Ptr may outlive a destroyed inventory node. Use the address
    // captured at registration, never getCellRef() through that borrowed Ptr.
    return std::find_if(
        mScripts.begin(), mScripts.end(), [&](const Entry& entry) { return entry.mRegistration->mReference == ref; });
}

void MWWorld::LocalScripts::erase(Scripts::const_iterator iter)
{
    if (iter == mScripts.end())
        return;
    if (iter == mIter)
        ++mIter;
    mScripts.erase(iter);
}

MWWorld::LocalScripts::Removal MWWorld::LocalScripts::prepareRemove(const CellRef* ref) const
{
    Removal prepared;
    prepared.mScripts = this;
    prepared.mReference = ref;
    const auto iter = find(ref);
    if (iter != mScripts.end())
        prepared.mRegistration = iter->mRegistration;
    return prepared;
}

void MWWorld::LocalScripts::validateRemoval(const Removal& prepared, const CellRef* ref) const
{
    const auto iter = find(ref);
    if (prepared.mScripts != this || prepared.mReference != ref
        || prepared.mRegistration != (iter == mScripts.end() ? nullptr : iter->mRegistration))
        throw std::invalid_argument("Local script removal preparation registration changed");
}

MWWorld::LocalScripts::List MWWorld::LocalScripts::snapshot() const
{
    List result;
    for (auto iter = mScripts.begin(); iter != mScripts.end(); ++iter)
    {
        if (iter == mIter)
            result.mCursor = result.mEntries.size();
        Removal entry;
        entry.mScripts = this;
        entry.mReference = iter->mRegistration->mReference;
        entry.mRegistration = iter->mRegistration;
        result.mEntries.push_back(std::move(entry));
    }
    if (mIter == mScripts.end())
        result.mCursor = result.mEntries.size();
    return result;
}

MWWorld::LocalScripts::PreparedList MWWorld::LocalScripts::prepareList(const Removal* removal) const
{
    PreparedList prepared;
    prepared.mOriginal = snapshot();
    prepared.mResult = prepared.mOriginal;
    if (removal)
    {
        validateRemoval(*removal, removal->mReference);
        const auto iter = std::find(prepared.mResult.mEntries.begin(), prepared.mResult.mEntries.end(), *removal);
        if (iter != prepared.mResult.mEntries.end())
        {
            const auto index = static_cast<size_t>(iter - prepared.mResult.mEntries.begin());
            // Stock erase advances a cursor on this entry to its successor. In
            // the vector that successor has the same index; earlier erases shift it.
            if (index < prepared.mResult.mCursor)
                --prepared.mResult.mCursor;
            prepared.mResult.mEntries.erase(iter);
        }
    }
    return prepared;
}

void MWWorld::LocalScripts::prepareListAddition(
    PreparedList& prepared, const Registration& addition, const CellRef* ref, ContainerStore* container) const
{
    // The caller validates the detached node or explicit context lifetime. Only
    // references absent from the original list may enter this owned preparation.
    // Stock duplicate replacement remains in add(); this never installs a list.
    if (!ref || std::ranges::any_of(prepared.mOriginal.mEntries, [&](const Removal& entry) {
            return entry.references(ref);
        }))
        throw std::invalid_argument("Local script list preparation requires a new reference");
    Removal entry;
    entry.mScripts = this;
    entry.mReference = ref;
    entry.mRegistration = std::make_shared<const ScriptRegistration>(
        ScriptRegistration{ addition.mScript, ref, addition.mCell, container });
    const bool atEnd = prepared.mResult.mCursor == prepared.mResult.mEntries.size();
    prepared.mResult.mEntries.push_back(std::move(entry));
    // std::list::push_back leaves the end iterator at end, not at the new entry.
    if (atEnd)
        ++prepared.mResult.mCursor;
}

void MWWorld::LocalScripts::validateList(const PreparedList& prepared, const Removal* removal,
    const Registration* addition, const CellRef* ref, const ContainerStore* container) const
{
    if (snapshot() != prepared.mOriginal)
        throw std::invalid_argument("Local script list preparation membership, registration or cursor changed");
    // Recompute order/cursor from the protected original and transfer removal.
    // No locals initialization, item dereference, registration or effects here.
    const auto expected = prepareList(removal);
    const auto& result = prepared.mResult;
    const auto size = expected.mResult.mEntries.size();
    const auto cursor = expected.mResult.mCursor;
    if (result.mEntries.size() != size + bool(addition) || result.mCursor != cursor + (addition && cursor == size)
        || !std::equal(expected.mResult.mEntries.begin(), expected.mResult.mEntries.end(), result.mEntries.begin()))
        throw std::invalid_argument("Local script list preparation result changed");
    if (addition)
    {
        const auto& entry = result.mEntries.back();
        if (entry.mScripts != this || !entry.references(ref) || !entry.hasRegistration()
            || entry.getScript() != addition->mScript || entry.getCell() != addition->mCell
            || entry.getContainer() != container
            || std::ranges::any_of(
                prepared.mOriginal.mEntries, [&](const Removal& original) { return original.references(ref); }))
            throw std::invalid_argument("Local script list preparation addition result changed");
    }
}

MWWorld::LocalScripts::List MWWorld::LocalScripts::relocateList(const List& original, const Relocations& bindings)
{
    auto result = original;
    for (auto& entry : result.mEntries)
        for (const auto& [from, to] : bindings)
            if (from.references(entry.mReference))
            {
                entry.mReference = to;
                entry.mRegistration = std::make_shared<const ScriptRegistration>(
                    ScriptRegistration{ entry.getScript(), to, entry.getCell(), entry.getContainer() });
                break;
            }
    return result;
}

bool MWWorld::LocalScripts::sameRelocatedList(const List& left, const List& right, const List& original)
{
    if (left.mCursor != right.mCursor || left.mEntries.size() != right.mEntries.size())
        return false;
    for (size_t i = 0; i < left.mEntries.size(); ++i)
    {
        const auto& a = left.mEntries[i];
        const auto& b = right.mEntries[i];
        if (b.references(original.mEntries[i].mReference) && a != b)
            return false; // Unrelated registrations retain their immutable identity.
        if (a.mScripts != b.mScripts || a.mReference != b.mReference || !a.mRegistration || !b.mRegistration
            || a.getScript() != b.getScript() || a.getCell() != b.getCell() || a.getContainer() != b.getContainer()
            || a.mRegistration->mReference != b.mRegistration->mReference)
            return false;
    }
    return true;
}

std::unique_ptr<MWWorld::LocalScripts::PreparedStorage> MWWorld::LocalScripts::prepareStorage(
    const List& relocated, const List& original, const std::vector<Ptr>& nodes, const std::vector<Ptr>& contexts) const
{
    if (relocated.mCursor > relocated.mEntries.size() || original.mEntries.size() != relocated.mEntries.size())
        throw std::invalid_argument("Local script storage preparation cursor out of range");
    auto storage = std::unique_ptr<PreparedStorage>(new PreparedStorage(this));
    storage->mNodes.reserve(relocated.mEntries.size());
    for (const auto& registration : relocated.mEntries)
    {
        if (registration.mScripts != this || !registration.mRegistration)
            throw std::invalid_argument("Local script storage preparation binding mismatch");
        Ptr item;
        // Only relocated registration identities may resolve to owned nodes.
        // An unchanged stale key can reuse an owned address; consider explicit,
        // lifetime-checked contexts only for those unaffected registrations.
        const auto& candidates = registration == original.mEntries[storage->mNodes.size()] ? contexts : nodes;
        for (const auto& node : candidates)
            if (registration.references(&node.getCellRef()))
            {
                item = node;
                item.mCell = registration.getCell();
                item.mContainerStore = registration.getContainer();
                break;
            }
        // The relocated witness supplies identities/order and already accounts
        // for stock remove/append cursor repair. Do not initialize locals again.
        storage->mEntries.emplace_back(item, registration.mRegistration);
        const auto* entry = &storage->mEntries.back();
        if (storage->mNodes.size() == relocated.mCursor)
            storage->mCursor = entry;
        storage->mNodes.push_back(entry);
    }
    return storage;
}

void MWWorld::LocalScripts::validateStorage(const PreparedStorage& storage, const List& relocated, const List& original,
    const std::vector<ConstPtr>& nodes, const std::vector<ConstPtr>& contexts) const
{
    if (storage.mService != this || storage.mEntries.size() != relocated.mEntries.size()
        || original.mEntries.size() != relocated.mEntries.size() || storage.mNodes.size() != relocated.mEntries.size()
        || relocated.mCursor > relocated.mEntries.size())
        throw std::invalid_argument("Local script prepared storage membership or service changed");
    const Entry* cursor = nullptr;
    size_t i = 0;
    for (const auto& entry : storage.mEntries)
    {
        const auto& registration = relocated.mEntries[i];
        ConstPtr expected;
        const bool contextBinding = registration == original.mEntries[i];
        for (const auto& node : contextBinding ? contexts : nodes)
            if (registration.references(&node.getCellRef()))
            {
                expected = node;
                expected.mCell = registration.getCell();
                expected.mContainerStore = registration.getContainer();
                break;
            }
        // Compare keys/Ptr fields only: either saved node or item may have been
        // destroyed by a stale write. Only current protected inventory nodes are read.
        if (&entry != storage.mNodes[i] || registration.mScripts != this
            || entry.mRegistration != registration.mRegistration || entry.mItem.mRef != expected.mRef
            || entry.mItem.mCell != expected.mCell || entry.mItem.mContainerStore != expected.mContainerStore
            || (contextBinding && entry.mItem.getReferenceLifetime() != expected.getReferenceLifetime()))
            throw std::invalid_argument("Local script prepared storage node or binding changed");
        if (i == relocated.mCursor)
            cursor = &entry;
        ++i;
    }
    if (storage.mCursor != cursor)
        throw std::invalid_argument("Local script prepared storage cursor changed");
}

void MWWorld::LocalScripts::validateContextBindings(const std::vector<ConstPtr>& contexts) const
{
    for (const auto& context : contexts)
    {
        if (!context.hasLiveReference())
            throw std::invalid_argument("Local script context lifetime changed");
        bool found = false;
        for (const auto& entry : mScripts)
        {
            if (!entry.references(&context.getCellRef()))
                continue;
            // The registration key alone is not a lifetime witness. Check the
            // captured Ptr before following it, including same-address reuse.
            const auto& item = entry.mItem;
            if (found || !item.hasLiveReference() || item.mRef != context.mRef
                || item.getReferenceLifetime() != context.getReferenceLifetime() || item.mCell != context.mCell
                || item.mContainerStore != context.mContainerStore || entry.getCell() != context.mCell
                || entry.getContainer() != context.mContainerStore
                || entry.getScript() != context.getClass().getScript(context)
                || entry.getScript() != context.getRefData().getLocals().getScriptId())
                throw std::invalid_argument("Local script context binding changed or inconsistent");
            found = true;
        }
    }
}

bool MWWorld::LocalScripts::isRunning(const ESM::RefId& scriptName, const Ptr& ptr) const
{
    return std::ranges::any_of(
        mScripts, [&](const Entry& entry) { return entry.mRegistration->mScript == scriptName && entry.mItem == ptr; });
}

void MWWorld::LocalScripts::validateInventoryBindings(const std::vector<ConstPtr>& nodes, CellStore* ownerCell) const
{
    for (const auto& node : nodes)
    {
        if (!node.hasLiveReference())
            throw std::invalid_argument("Local script inventory lifetime changed");
        // Registry inventory Ptrs have no cell hint. Stock scripts may use the
        // owning cell or nullptr (player); verify that hint before reusing the
        // exact context/lifetime/locals checks. Never follow the entry's saved Ptr.
        auto context = node;
        for (const auto& entry : mScripts)
            if (entry.references(&node.getCellRef()))
            {
                if (entry.getCell() && entry.getCell() != ownerCell)
                    throw std::invalid_argument("Local script inventory cell binding changed");
                context.mCell = entry.getCell();
            }
        validateContextBindings({ context });
    }
}
