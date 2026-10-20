#include "luaevents.hpp"

#include <components/debug/debuglog.hpp>

#include <components/esm/luascripts.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <components/lua/serialization.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/worldmodel.hpp"

#include "globalscripts.hpp"
#include "localscripts.hpp"
#include "menuscripts.hpp"

namespace MWLua
{
    namespace
    {
        void validateEvent(std::string_view name, std::string_view data)
        {
            if (name.empty() || name.size() > MWScript::ExecutionContext::MaximumReferenceNameBytes
                || name.find('\0') != std::string_view::npos || data.size() > LuaEvents::MaximumEventDataBytes)
                throw std::invalid_argument("Lua event name or payload exceeds bounds");
        }
    }

    LuaEvents::Snapshot LuaEvents::snapshot() const
    {
        return { mNewGlobalEventBatch, mNewLocalEventBatch, mGlobalEventBatch, mLocalEventBatch, mMenuEvents };
    }

    void LuaEvents::restore(Snapshot saved)
    {
        mNewGlobalEventBatch = std::move(saved.mNewGlobalEventBatch);
        mNewLocalEventBatch = std::move(saved.mNewLocalEventBatch);
        mGlobalEventBatch = std::move(saved.mGlobalEventBatch);
        mLocalEventBatch = std::move(saved.mLocalEventBatch);
        mMenuEvents = std::move(saved.mMenuEvents);
    }

    void LuaEvents::addGlobalEvent(Global event)
    {
        if (mStrict)
        {
            validateEvent(event.mEventName, event.mEventData);
            if (mExecutionValidator)
                event.mExecution = mExecutionValidator();
            else if (event.mExecution)
                event.mExecution->validate();
        }
        mNewGlobalEventBatch.push_back(std::move(event));
    }

    void LuaEvents::addMenuEvent(Global event)
    {
        if (mStrict)
            throw std::runtime_error("Menu Lua events are unavailable in scoped transactions");
        mMenuEvents.push_back(std::move(event));
    }

    void LuaEvents::addLocalEvent(Local event)
    {
        if (mStrict)
        {
            validateEvent(event.mEventName, event.mEventData);
            if (!event.mDest.isSet())
                throw std::invalid_argument("Local Lua event lacks target identity");
            if (mExecutionValidator)
                event.mExecution = mExecutionValidator();
            else if (event.mExecution)
                event.mExecution->validate();
            if (mLocalResolver && !mLocalResolver(event.mExecution, event.mDest))
                throw std::runtime_error("Local Lua event target absent from scope");
        }
        mNewLocalEventBatch.push_back(std::move(event));
    }

    void LuaEvents::clear()
    {
        mGlobalEventBatch.clear();
        mLocalEventBatch.clear();
        mNewGlobalEventBatch.clear();
        mNewLocalEventBatch.clear();
        mMenuEvents.clear();
    }

    void LuaEvents::finalizeEventBatch()
    {
        mNewGlobalEventBatch.swap(mGlobalEventBatch);
        mNewLocalEventBatch.swap(mLocalEventBatch);
        mNewGlobalEventBatch.clear();
        mNewLocalEventBatch.clear();
    }

    void LuaEvents::callEventHandlers()
    {
        std::vector<Global> globalBatch;
        std::vector<Local> localBatch;
        globalBatch.swap(mGlobalEventBatch);
        localBatch.swap(mLocalEventBatch);
        for (const Global& e : globalBatch)
        {
            if (mStrict)
            {
                validateEvent(e.mEventName, e.mEventData);
                if (mExecutionValidator)
                {
                    const auto active = mExecutionValidator();
                    if (e.mExecution && *e.mExecution != active)
                        throw std::invalid_argument("Global Lua event execution context mismatch");
                }
            }
            mGlobalScripts.receiveEvent(e.mEventName, e.mEventData);
        }
        for (const Local& e : localBatch)
        {
            if (mStrict)
            {
                validateEvent(e.mEventName, e.mEventData);
                if (mExecutionValidator)
                {
                    const auto active = mExecutionValidator();
                    if (e.mExecution && *e.mExecution != active)
                        throw std::invalid_argument("Local Lua event execution context mismatch");
                }
            }
            LocalScripts* scripts = nullptr;
            if (mLocalResolver)
            {
                scripts = mLocalResolver(e.mExecution, e.mDest);
                if (!scripts && mStrict)
                    throw std::runtime_error("Local Lua event target absent from scope");
            }
            else
            {
                MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(e.mDest);
                scripts = ptr.isEmpty() ? nullptr : ptr.getRefData().getLuaScripts();
            }
            if (scripts)
                scripts->receiveEvent(e.mEventName, e.mEventData);
            else
                Log(Debug::Debug) << "Ignored event " << e.mEventName << " to L" << e.mDest.toString()
                                  << ". Object not found or has no attached scripts";
        }
    }

    void LuaEvents::callMenuEventHandlers()
    {
        if (!mMenuScripts)
        {
            mMenuEvents.clear();
            return;
        }
        for (const Global& e : mMenuEvents)
            mMenuScripts->receiveEvent(e.mEventName, e.mEventData);
        mMenuEvents.clear();
    }

    template <typename Event>
    static void saveEvent(ESM::ESMWriter& esm, ESM::RefNum dest, const Event& event)
    {
        esm.writeHNString("LUAE", event.mEventName);
        esm.writeFormId(dest, true);
        if (!event.mEventData.empty())
            saveLuaBinaryData(esm, event.mEventData);
    }

    void LuaEvents::load(lua_State* lua, ESM::ESMReader& esm, const std::map<int, int>& contentFileMapping,
        const LuaUtil::UserdataSerializer* serializer)
    {
        std::vector<Global> stagedGlobal;
        std::vector<Local> stagedLocal;
        std::optional<MWScript::ExecutionContext> execution;
        if (mStrict && mExecutionValidator)
            execution = mExecutionValidator();
        while (esm.isNextSub("LUAE"))
        {
            std::string name = esm.getHString();
            ESM::RefNum dest = esm.getFormId(true);
            std::string data = loadLuaBinaryData(esm);
            if (mStrict)
                validateEvent(name, data);
            if (!data.empty())
            {
                try
                {
                    data = LuaUtil::serialize(LuaUtil::deserialize(lua, data, serializer), serializer);
                }
                catch (std::exception& e)
                {
                    if (mStrict)
                        throw;
                    Log(Debug::Error) << "loadEvent: invalid event data: " << e.what();
                }
            }
            if (dest.isSet())
            {
                auto it = contentFileMapping.find(dest.mContentFile);
                if (it != contentFileMapping.end())
                    dest.mContentFile = it->second;
                if (mStrict && mLocalResolver && !mLocalResolver(execution, dest))
                    throw std::runtime_error("Saved local Lua event target absent from scope");
                stagedLocal.push_back({ dest, std::move(name), std::move(data), execution });
            }
            else
                stagedGlobal.push_back({ std::move(name), std::move(data), execution });
        }
        clear();
        mGlobalEventBatch = std::move(stagedGlobal);
        mLocalEventBatch = std::move(stagedLocal);
    }

    void LuaEvents::save(ESM::ESMWriter& esm) const
    {
        // Used as a marker of a global event.
        constexpr ESM::RefNum globalId;

        for (const Global& e : mGlobalEventBatch)
            saveEvent(esm, globalId, e);
        for (const Global& e : mNewGlobalEventBatch)
            saveEvent(esm, globalId, e);
        for (const Local& e : mLocalEventBatch)
            saveEvent(esm, e.mDest, e);
        for (const Local& e : mNewLocalEventBatch)
            saveEvent(esm, e.mDest, e);
    }

}
