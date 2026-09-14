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
            mScripts.push_back({ registered, std::make_shared<const ScriptRegistration>(
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
    return std::find_if(mScripts.begin(), mScripts.end(),
        [&](const Entry& entry) { return entry.mRegistration->mReference == ref; });
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

bool MWWorld::LocalScripts::isRunning(const ESM::RefId& scriptName, const Ptr& ptr) const
{
    return std::ranges::any_of(mScripts,
        [&](const Entry& entry) { return entry.mRegistration->mScript == scriptName && entry.mItem == ptr; });
}
