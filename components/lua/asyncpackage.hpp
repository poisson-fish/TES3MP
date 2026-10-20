#ifndef COMPONENTS_LUA_ASYNCPACKAGE_H
#define COMPONENTS_LUA_ASYNCPACKAGE_H

#include "scriptscontainer.hpp"

namespace LuaUtil
{
    struct AsyncPackageId
    {
        ScriptsContainer* mContainer;
        int mScriptId;
        sol::table mHiddenData;
    };
    sol::function getAsyncPackageInitializer(
        lua_State* state, std::function<double()> simulationTimeFn, std::function<double()> gameTimeFn);

    // Wrapper for a Lua function.
    // Holds information about the script the function belongs to.
    // Needed to prevent callback calls if the script was removed.
    struct Callback
    {
        sol::main_protected_function mFunc;
        sol::table mHiddenData; // same object as Script::mHiddenData in ScriptsContainer

        static bool isLuaCallback(const sol::object&);
        static Callback fromLua(const sol::table&);
        static sol::table makeMetatable(lua_State* state);
        static sol::table make(const AsyncPackageId& asyncId, sol::main_protected_function fn, sol::table metatable);

        bool isValid() const { return mHiddenData[ScriptsContainer::sScriptIdKey] != sol::nil; }
        bool operator==(const Callback&) const = default;

        template <typename... Args>
        sol::object call(Args&&... args) const
        {
            sol::optional<ScriptId> scriptId = mHiddenData[ScriptsContainer::sScriptIdKey];
            if (scriptId.has_value())
            {
                if (scriptId->mContainer)
                    scriptId->mContainer->validateContext();
                return LuaUtil::call(scriptId.value(), mFunc, std::forward<Args>(args)...);
            }
            else
                Log(Debug::Debug) << "Ignored callback to the removed script "
                                  << mHiddenData.get<std::string>(ScriptsContainer::sScriptDebugNameKey);
            return sol::nil;
        }

        template <typename... Args>
        void tryCall(Args&&... args) const
        {
            try
            {
                this->call(std::forward<Args>(args)...);
            }
            catch (std::exception& e)
            {
                sol::optional<ScriptId> scriptId = mHiddenData[ScriptsContainer::sScriptIdKey];
                if (scriptId.has_value() && scriptId->mContainer && scriptId->mContainer->isStrict())
                    throw;
                Log(Debug::Error) << "Error in callback: " << e.what();
            }
        }
    };
}

#endif // COMPONENTS_LUA_ASYNCPACKAGE_H
