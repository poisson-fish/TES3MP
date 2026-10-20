#ifndef MWLUA_LUAEVENTS_H
#define MWLUA_LUAEVENTS_H

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <components/esm3/cellref.hpp> // defines RefNum that is used as a unique id

#include "../mwscript/executioncontext.hpp"

struct lua_State;

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace LuaUtil
{
    class UserdataSerializer;
}

namespace MWLua
{

    class GlobalScripts;
    class LocalScripts;
    class MenuScripts;

    class LuaEvents
    {
    public:
        static constexpr std::size_t MaximumEventDataBytes = 4096;

        explicit LuaEvents(GlobalScripts& globalScripts, MenuScripts& menuScripts)
            : mGlobalScripts(globalScripts)
            , mMenuScripts(&menuScripts)
        {
        }
        explicit LuaEvents(GlobalScripts& globalScripts)
            : mGlobalScripts(globalScripts)
        {
        }

        struct Global
        {
            std::string mEventName;
            std::string mEventData; // serialized Lua data
            std::optional<MWScript::ExecutionContext> mExecution = std::nullopt;
            friend bool operator==(const Global&, const Global&) = default;
        };
        struct Local
        {
            ESM::RefNum mDest;
            std::string mEventName;
            std::string mEventData; // serialized Lua data
            std::optional<MWScript::ExecutionContext> mExecution = std::nullopt;
            friend bool operator==(const Local&, const Local&) = default;
        };
        struct Snapshot
        {
            std::vector<Global> mNewGlobalEventBatch;
            std::vector<Local> mNewLocalEventBatch;
            std::vector<Global> mGlobalEventBatch;
            std::vector<Local> mLocalEventBatch;
            std::vector<Global> mMenuEvents;
            bool operator==(const Snapshot&) const = default;
        };
        using LocalResolver
            = std::function<LocalScripts*(const std::optional<MWScript::ExecutionContext>&, ESM::RefNum)>;
        using ExecutionValidator = std::function<MWScript::ExecutionContext()>;

        void setScopedResolver(
            LocalResolver localResolver, ExecutionValidator executionValidator = {}, bool strict = true)
        {
            mLocalResolver = std::move(localResolver);
            mExecutionValidator = std::move(executionValidator);
            mStrict = strict;
        }
        void setStrict(bool strict) { mStrict = strict; }
        bool isStrict() const { return mStrict; }
        Snapshot snapshot() const;
        void restore(Snapshot saved);

        void addGlobalEvent(Global event);
        void addMenuEvent(Global event);
        void addLocalEvent(Local event);

        void clear();
        void finalizeEventBatch();
        void callEventHandlers();
        void callMenuEventHandlers();

        void load(lua_State* lua, ESM::ESMReader& esm, const std::map<int, int>& contentFileMapping,
            const LuaUtil::UserdataSerializer* serializer);
        void save(ESM::ESMWriter& esm) const;

    private:
        GlobalScripts& mGlobalScripts;
        MenuScripts* mMenuScripts = nullptr;
        std::vector<Global> mNewGlobalEventBatch;
        std::vector<Local> mNewLocalEventBatch;
        std::vector<Global> mGlobalEventBatch;
        std::vector<Local> mLocalEventBatch;
        std::vector<Global> mMenuEvents;
        LocalResolver mLocalResolver;
        ExecutionValidator mExecutionValidator;
        bool mStrict = false;
    };

}

#endif // MWLUA_LUAEVENTS_H
