#include "mwscriptbindings.hpp"

#include <components/compiler/locals.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/util.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwscript/globalscripts.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/worldimp.hpp"

#include "context.hpp"
#include "object.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace MWLua
{
    struct MWScriptRef
    {
        ESM::RefId mId;
        sol::optional<GObject> mObj;
        Context mContext{};

        MWScript::Locals& getLocals()
        {
            if (mObj)
                return mObj->ptr().getRefData().getLocals();
            else
                return MWBase::Environment::get().getScriptManager()->getGlobalScripts().getLocals(mId);
        }
        bool isRunning() const
        {
            if (mObj.has_value()) // local script
            {
                MWWorld::LocalScripts& localScripts = MWBase::Environment::get().getWorld()->getLocalScripts();
                return localScripts.isRunning(mId, mObj->ptr());
            }

            return MWBase::Environment::get().getScriptManager()->getGlobalScripts().isRunning(mId);
        }
    };
    struct MWScriptVariables
    {
        MWScriptRef mRef;
    };
    struct GlobalVariables
    {
        Context mContext{};
        const MWWorld::Store<ESM::Global>* mStore = nullptr;
    };
}

namespace sol
{
    template <>
    struct is_automagical<MWLua::MWScriptRef> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::MWScriptVariables> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::GlobalVariables> : std::false_type
    {
    };
    template <>
    struct is_automagical<ESM::Global> : std::false_type
    {
    };
}

namespace MWLua
{
    namespace
    {
        void checkScopedName(std::string_view name)
        {
            if (name.empty() || name.size() > MWScript::ScriptState::MaximumNameBytes
                || name.find('\0') != std::string_view::npos)
                throw std::invalid_argument("Scoped Lua MWScript name exceeds bounds");
        }

        bool matchesScopedPlayer(const GObject& player, MWScript::InterpreterContext& scoped)
        {
            const MWWorld::Ptr expected = scoped.getPlayer();
            if (!player.ptrOrEmpty().isEmpty())
                return player.ptr() == expected;
            const ESM::RefNum expectedId = expected.getCellRef().getRefNum();
            return expectedId.isSet() && player.id() == expectedId;
        }

        sol::optional<float> getScopedGlobalValue(MWScript::InterpreterContext& scoped, std::string_view globalId)
        {
            checkScopedName(globalId);
            const char varType = scoped.getGlobalType(globalId);
            if (varType == 'f')
                return scoped.getGlobalFloat(globalId);
            if (varType == 's')
                return static_cast<float>(scoped.getGlobalShort(globalId));
            if (varType == 'l')
                return static_cast<float>(scoped.getGlobalLong(globalId));
            return sol::nullopt;
        }

        void setScopedGlobalValue(MWScript::InterpreterContext& scoped, std::string_view globalId, float val)
        {
            checkScopedName(globalId);
            if (!std::isfinite(val))
                throw std::invalid_argument("Nonfinite scoped global variable");
            const char varType = scoped.getGlobalType(globalId);
            if (varType == 'f')
                scoped.setGlobalFloat(globalId, val);
            else if (varType == 's')
            {
                if (double(val) < std::numeric_limits<int16_t>::min()
                    || double(val) > std::numeric_limits<int16_t>::max())
                    throw std::invalid_argument("Scoped global short exceeds range");
                scoped.setGlobalShort(globalId, static_cast<int>(val));
            }
            else if (varType == 'l')
            {
                if (double(val) < std::numeric_limits<int32_t>::min()
                    || double(val) > std::numeric_limits<int32_t>::max())
                    throw std::invalid_argument("Scoped global long exceeds range");
                scoped.setGlobalLong(globalId, static_cast<int>(val));
            }
            else
                throw std::runtime_error("No variable \"" + std::string(globalId) + "\" in GlobalStore");
        }
    }

    float getGlobalVariableValue(const std::string_view globalId)
    {
        char varType = MWBase::Environment::get().getWorld()->getGlobalVariableType(globalId);
        if (varType == 'f')
        {
            return MWBase::Environment::get().getWorld()->getGlobalFloat(globalId);
        }
        else if (varType == 's' || varType == 'l')
        {
            return static_cast<float>(MWBase::Environment::get().getWorld()->getGlobalInt(globalId));
        }
        return 0;
    }

    void setGlobalVariableValue(const std::string_view globalId, float value)
    {
        char varType = MWBase::Environment::get().getWorld()->getGlobalVariableType(globalId);
        if (varType == 'f')
        {
            MWBase::Environment::get().getWorld()->setGlobalFloat(globalId, value);
        }
        else if (varType == 's' || varType == 'l')
        {
            MWBase::Environment::get().getWorld()->setGlobalInt(globalId, static_cast<int>(value));
        }
    }

    sol::table initMWScriptBindings(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);

        api["getGlobalScript"]
            = [context](std::string_view recordId, sol::optional<GObject> player) -> sol::optional<MWScriptRef> {
            if (context.isScoped())
            {
                return context.withScoped([&](MWScript::InterpreterContext& scoped) -> sol::optional<MWScriptRef> {
                    if (player.has_value() && !matchesScopedPlayer(*player, scoped))
                        throw std::runtime_error("Second argument must either be a player or be missing");
                    const ESM::RefId scriptId = scoped.getMemberId(recordId);
                    (void)scoped.getScopedScript(scriptId);
                    return MWScriptRef{ scriptId, sol::nullopt, context };
                });
            }
            if (player.has_value() && player->ptr() != MWBase::Environment::get().getWorld()->getPlayerPtr())
                throw std::runtime_error("Second argument must either be a player or be missing");
            auto scriptId = ESM::RefId::deserializeText(recordId);
            if (MWBase::Environment::get().getScriptManager()->getGlobalScripts().getScriptIfPresent(scriptId))
                return MWScriptRef{ scriptId, sol::nullopt, context };
            else
                return sol::nullopt;
        };
        api["getLocalScript"]
            = [context](const GObject& obj, sol::optional<GObject> player) -> sol::optional<MWScriptRef> {
            if (context.isScoped())
            {
                return context.withScoped([&](MWScript::InterpreterContext& scoped) -> sol::optional<MWScriptRef> {
                    if (player.has_value() && !matchesScopedPlayer(*player, scoped))
                        throw std::runtime_error("Second argument must either be a player or be missing");
                    (void)obj.ptr();
                    throw std::runtime_error("Reference script state is not staged");
                });
            }
            if (player.has_value() && player->ptr() != MWBase::Environment::get().getWorld()->getPlayerPtr())
                throw std::runtime_error("Second argument must either be a player or be missing");
            auto scriptId = obj.ptr().getRefData().getLocals().getScriptId();
            if (scriptId.empty())
                return sol::nullopt;
            return MWScriptRef{ scriptId, obj, context };
        };

        // In multiplayer it will be possible to have several instances (per player) of a single script,
        // so we will likely add functions returning Lua table of scripts.
        // api["getGlobalScripts"] = [](std::string_view recordId) -> list of scripts
        // api["getLocalScripts"] = [](const GObject& obj) -> list of scripts

        sol::usertype<MWScriptRef> mwscript = lua.new_usertype<MWScriptRef>("MWScript");
        sol::usertype<MWScriptVariables> mwscriptVars = lua.new_usertype<MWScriptVariables>("MWScriptVariables");
        mwscript[sol::meta_function::to_string]
            = [](const MWScriptRef& s) { return std::string("MWScript{") + s.mId.toDebugString() + "}"; };
        mwscript["isRunning"] = sol::property(
            [](const MWScriptRef& s) {
                if (s.mContext.isScoped())
                    return s.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                        return scoped.scriptRunning(s.mId.getRefIdString());
                    });
                return s.isRunning();
            },
            [](MWScriptRef& s, bool running) {
                if (!s.mContext.isScoped())
                    throw std::runtime_error("MWScript.isRunning is read-only without scoped transaction");
                s.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                    if (running)
                        scoped.startScript(s.mId.getRefIdString(), {});
                    else
                        scoped.stopScript(s.mId.getRefIdString());
                });
            });
        mwscript["recordId"] = sol::readonly_property([](const MWScriptRef& s) { return s.mId.serializeText(); });
        mwscript["variables"] = sol::readonly_property([](const MWScriptRef& s) { return MWScriptVariables{ s }; });
        mwscript["object"] = sol::readonly_property([](const MWScriptRef& s) -> sol::optional<GObject> {
            if (s.mContext.isScoped())
            {
                return s.mContext.withScoped([&](MWScript::InterpreterContext& scoped) -> sol::optional<GObject> {
                    (void)scoped.getScopedScript(s.mId);
                    return s.mObj;
                });
            }
            if (s.mObj)
                return s.mObj;
            const MWScript::GlobalScriptDesc* script
                = MWBase::Environment::get().getScriptManager()->getGlobalScripts().getScriptIfPresent(s.mId);
            if (!script)
                throw std::runtime_error("Invalid MWScriptRef");
            const MWWorld::Ptr* ptr = script->getPtrIfPresent();
            if (ptr && !ptr->isEmpty())
                return GObject(*ptr);
            else
                return sol::nullopt;
        });
        mwscript["player"] = sol::readonly_property([](const MWScriptRef& s) {
            if (s.mContext.isScoped())
                return s.mContext.withScoped(
                    [&](MWScript::InterpreterContext& scoped) { return GObject(scoped.getPlayer()); });
            return GObject(MWBase::Environment::get().getWorld()->getPlayerPtr());
        });
        mwscriptVars[sol::meta_function::length] = [](MWScriptVariables& s) {
            if (s.mRef.mContext.isScoped())
            {
                return s.mRef.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                    const auto binding = scoped.getScopedScript(s.mRef.mId);
                    const auto& locals = binding.instance.mLocals;
                    return locals.mShorts.size() + locals.mLongs.size() + locals.mFloats.size();
                });
            }
            return s.mRef.getLocals().getSize(s.mRef.mId);
        };
        mwscriptVars[sol::meta_function::index] = sol::overload(
            [](MWScriptVariables& s, std::string_view var) -> sol::optional<double> {
                if (s.mRef.mContext.isScoped())
                {
                    return s.mRef.mContext.withScoped(
                        [&](MWScript::InterpreterContext& scoped) -> sol::optional<double> {
                            checkScopedName(var);
                            const auto binding = scoped.getScopedScript(s.mRef.mId);
                            const std::string lower = Misc::StringUtils::lowerCase(var);
                            const char type = binding.declarations.getType(lower);
                            if (type == 's')
                                return scoped.getMemberShort(s.mRef.mId, lower, true);
                            if (type == 'l')
                                return scoped.getMemberLong(s.mRef.mId, lower, true);
                            if (type == 'f')
                                return scoped.getMemberFloat(s.mRef.mId, lower, true);
                            return sol::nullopt;
                        });
                }
                if (s.mRef.getLocals().hasVar(s.mRef.mId, var))
                    return s.mRef.getLocals().getVarAsDouble(s.mRef.mId, Misc::StringUtils::lowerCase(var));
                else
                    return sol::nullopt;
            },
            [](MWScriptVariables& s, std::size_t index) -> sol::optional<double> {
                if (s.mRef.mContext.isScoped())
                {
                    return s.mRef.mContext.withScoped(
                        [&](MWScript::InterpreterContext& scoped) -> sol::optional<double> {
                            const auto binding = scoped.getScopedScript(s.mRef.mId);
                            const auto& locals = binding.instance.mLocals;
                            const std::size_t total
                                = locals.mShorts.size() + locals.mLongs.size() + locals.mFloats.size();
                            if (index < 1 || index > total)
                                return sol::nullopt;
                            if (index <= locals.mShorts.size())
                                return locals.mShorts[index - 1];
                            index -= locals.mShorts.size();
                            if (index <= locals.mLongs.size())
                                return locals.mLongs[index - 1];
                            index -= locals.mLongs.size();
                            return locals.mFloats[index - 1];
                        });
                }
                auto& locals = s.mRef.getLocals();
                if (index < 1 || locals.getSize(s.mRef.mId) < index)
                    return sol::nullopt;
                if (index <= locals.mShorts.size())
                    return locals.mShorts[index - 1];
                index -= locals.mShorts.size();
                if (index <= locals.mLongs.size())
                    return locals.mLongs[index - 1];
                index -= locals.mLongs.size();
                if (index <= locals.mFloats.size())
                    return locals.mFloats[index - 1];
                return sol::nullopt;
            });
        mwscriptVars[sol::meta_function::new_index] = sol::overload(
            [](MWScriptVariables& s, std::string_view var, double val) {
                if (s.mRef.mContext.isScoped())
                {
                    s.mRef.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                        checkScopedName(var);
                        if (!std::isfinite(val))
                            throw std::invalid_argument("Nonfinite scoped script member");
                        const auto binding = scoped.getScopedScript(s.mRef.mId);
                        const std::string lower = Misc::StringUtils::lowerCase(var);
                        const char type = binding.declarations.getType(lower);
                        if (type == 's')
                            scoped.setMemberShort(s.mRef.mId, lower, static_cast<int>(val), true);
                        else if (type == 'l')
                            scoped.setMemberLong(s.mRef.mId, lower, static_cast<int>(val), true);
                        else if (type == 'f')
                            scoped.setMemberFloat(s.mRef.mId, lower, static_cast<float>(val), true);
                        else
                            throw std::runtime_error(
                                "No variable \"" + std::string(var) + "\" in mwscript " + s.mRef.mId.toDebugString());
                    });
                    return;
                }
                MWScript::Locals& locals = s.mRef.getLocals();
                if (!locals.setVar(s.mRef.mId, Misc::StringUtils::lowerCase(var), val))
                    throw std::runtime_error(
                        "No variable \"" + std::string(var) + "\" in mwscript " + s.mRef.mId.toDebugString());
            },
            [](MWScriptVariables& s, std::size_t index, double val) {
                if (s.mRef.mContext.isScoped())
                {
                    s.mRef.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                        if (!std::isfinite(val))
                            throw std::invalid_argument("Nonfinite scoped script member");
                        auto binding = scoped.getScopedScript(s.mRef.mId);
                        auto& locals = binding.instance.mLocals;
                        const std::size_t total = locals.mShorts.size() + locals.mLongs.size() + locals.mFloats.size();
                        if (index < 1 || index > total)
                            throw std::runtime_error("Index out of range in mwscript " + s.mRef.mId.toDebugString());
                        if (index <= locals.mShorts.size())
                        {
                            scoped.setMemberShort(
                                s.mRef.mId, binding.declarations.get('s')[index - 1], static_cast<int>(val), true);
                            return;
                        }
                        index -= locals.mShorts.size();
                        if (index <= locals.mLongs.size())
                        {
                            scoped.setMemberLong(
                                s.mRef.mId, binding.declarations.get('l')[index - 1], static_cast<int>(val), true);
                            return;
                        }
                        index -= locals.mLongs.size();
                        scoped.setMemberFloat(
                            s.mRef.mId, binding.declarations.get('f')[index - 1], static_cast<float>(val), true);
                    });
                    return;
                }
                auto& locals = s.mRef.getLocals();
                if (index < 1 || locals.getSize(s.mRef.mId) < index)
                    throw std::runtime_error("Index out of range in mwscript " + s.mRef.mId.toDebugString());
                if (index <= locals.mShorts.size())
                {
                    locals.mShorts[index - 1] = static_cast<Interpreter::Type_Short>(val);
                    return;
                }
                index -= locals.mShorts.size();
                if (index <= locals.mLongs.size())
                {
                    locals.mLongs[index - 1] = static_cast<Interpreter::Type_Integer>(val);
                    return;
                }
                index -= locals.mLongs.size();
                if (index <= locals.mFloats.size())
                    locals.mFloats[index - 1] = static_cast<Interpreter::Type_Float>(val);
            });
        mwscriptVars[sol::meta_function::pairs] = [](MWScriptVariables& s) {
            std::size_t index = 0;
            return sol::as_function(
                [s, index](sol::this_state) mutable -> sol::optional<std::tuple<std::string_view, double>> {
                    if (s.mRef.mContext.isScoped())
                    {
                        return s.mRef.mContext.withScoped(
                            [&](MWScript::InterpreterContext& scoped)
                                -> sol::optional<std::tuple<std::string_view, double>> {
                                const auto binding = scoped.getScopedScript(s.mRef.mId);
                                const auto& compilerLocals = binding.declarations;
                                const auto& locals = binding.instance.mLocals;
                                const std::size_t size
                                    = locals.mShorts.size() + locals.mLongs.size() + locals.mFloats.size();
                                if (index >= size)
                                    return sol::nullopt;
                                auto i = index++;
                                if (i < locals.mShorts.size())
                                    return std::make_tuple<std::string_view, double>(
                                        compilerLocals.get('s')[i], locals.mShorts[i]);
                                i -= locals.mShorts.size();
                                if (i < locals.mLongs.size())
                                    return std::make_tuple<std::string_view, double>(
                                        compilerLocals.get('l')[i], locals.mLongs[i]);
                                i -= locals.mLongs.size();
                                if (i < locals.mFloats.size())
                                    return std::make_tuple<std::string_view, double>(
                                        compilerLocals.get('f')[i], locals.mFloats[i]);
                                return sol::nullopt;
                            });
                    }
                    const auto& compilerLocals = MWBase::Environment::get().getScriptManager()->getLocals(s.mRef.mId);
                    auto& locals = s.mRef.getLocals();
                    std::size_t size = locals.getSize(s.mRef.mId);
                    if (index >= size)
                        return sol::nullopt;
                    auto i = index++;
                    if (i < locals.mShorts.size())
                        return std::make_tuple<std::string_view, double>(compilerLocals.get('s')[i], locals.mShorts[i]);
                    i -= locals.mShorts.size();
                    if (i < locals.mLongs.size())
                        return std::make_tuple<std::string_view, double>(compilerLocals.get('l')[i], locals.mLongs[i]);
                    i -= locals.mLongs.size();
                    if (i < locals.mFloats.size())
                        return std::make_tuple<std::string_view, double>(compilerLocals.get('f')[i], locals.mFloats[i]);
                    return sol::nullopt;
                });
        };

        using GlobalStore = MWWorld::Store<ESM::Global>;
        sol::usertype<GlobalVariables> globalStoreT = lua.new_usertype<GlobalVariables>("ESM3_GlobalStore");
        const GlobalStore* globalStore
            = (!context.isScoped() && MWBase::Environment::tryGet() && MWBase::Environment::tryGet()->getWorld())
            ? &MWBase::Environment::tryGet()->getWorld()->getStore().get<ESM::Global>()
            : nullptr;
        globalStoreT[sol::meta_function::to_string] = [](const GlobalVariables& store) {
            if (store.mContext.isScoped())
            {
                const auto count = store.mContext.withScoped(
                    [](MWScript::InterpreterContext& scoped) { return scoped.getGlobals().size(); });
                return "ESM3_GlobalStore{" + std::to_string(count) + " globals}";
            }
            return "ESM3_GlobalStore{" + std::to_string(store.mStore->getSize()) + " globals}";
        };
        globalStoreT[sol::meta_function::length] = [](const GlobalVariables& store) {
            if (store.mContext.isScoped())
                return store.mContext.withScoped(
                    [](MWScript::InterpreterContext& scoped) { return scoped.getGlobals().size(); });
            return store.mStore->getSize();
        };
        globalStoreT[sol::meta_function::index] = sol::overload(
            [](const GlobalVariables& store, std::string_view globalId) -> sol::optional<float> {
                if (store.mContext.isScoped())
                    return store.mContext.withScoped(
                        [&](MWScript::InterpreterContext& scoped) { return getScopedGlobalValue(scoped, globalId); });
                auto g = store.mStore->search(ESM::RefId::deserializeText(globalId));
                if (g == nullptr)
                    return sol::nullopt;
                return getGlobalVariableValue(globalId);
            },
            [](const GlobalVariables& store, size_t index) -> sol::optional<float> {
                if (store.mContext.isScoped())
                {
                    return store.mContext.withScoped(
                        [&](MWScript::InterpreterContext& scoped) -> sol::optional<float> {
                            const auto names = scoped.getGlobals();
                            if (index < 1 || index > names.size())
                                return sol::nullopt;
                            return getScopedGlobalValue(scoped, names[index - 1]);
                        });
                }
                if (index < 1 || store.mStore->getSize() < index)
                    return sol::nullopt;
                auto g = store.mStore->at(LuaUtil::fromLuaIndex(index));
                if (g == nullptr)
                    return sol::nullopt;
                std::string globalId = g->mId.serializeText();
                return getGlobalVariableValue(globalId);
            });
        globalStoreT[sol::meta_function::new_index] = sol::overload(
            [](const GlobalVariables& store, std::string_view globalId, float val) -> void {
                if (store.mContext.isScoped())
                {
                    store.mContext.withScoped(
                        [&](MWScript::InterpreterContext& scoped) { setScopedGlobalValue(scoped, globalId, val); });
                    return;
                }
                auto g = store.mStore->search(ESM::RefId::deserializeText(globalId));
                if (g == nullptr)
                    throw std::runtime_error("No variable \"" + std::string(globalId) + "\" in GlobalStore");
                setGlobalVariableValue(globalId, val);
            },
            [](const GlobalVariables& store, size_t index, float val) {
                if (store.mContext.isScoped())
                {
                    store.mContext.withScoped([&](MWScript::InterpreterContext& scoped) {
                        const auto names = scoped.getGlobals();
                        if (index < 1 || index > names.size())
                            throw std::runtime_error("Index out of range in GlobalStore");
                        setScopedGlobalValue(scoped, names[index - 1], val);
                    });
                    return;
                }
                if (index < 1 || store.mStore->getSize() < index)
                    return;
                auto g = store.mStore->at(LuaUtil::fromLuaIndex(index));
                if (g == nullptr)
                    return;
                std::string globalId = g->mId.serializeText();
                setGlobalVariableValue(globalId, val);
            });
        globalStoreT[sol::meta_function::pairs] = [](const GlobalVariables& store) {
            size_t index = 0;
            return sol::as_function(
                [index, store](sol::this_state ts) mutable -> sol::optional<std::tuple<std::string, float>> {
                    if (store.mContext.isScoped())
                    {
                        return store.mContext.withScoped(
                            [&](MWScript::InterpreterContext& scoped)
                                -> sol::optional<std::tuple<std::string, float>> {
                                const auto names = scoped.getGlobals();
                                if (index >= names.size())
                                    return sol::nullopt;
                                std::string globalId = names[index++];
                                auto val = getScopedGlobalValue(scoped, globalId);
                                if (!val)
                                    return sol::nullopt;
                                return std::make_tuple(std::move(globalId), *val);
                            });
                    }
                    if (index >= store.mStore->getSize())
                        return sol::nullopt;

                    const ESM::Global* global = store.mStore->at(index++);
                    if (!global)
                        return sol::nullopt;

                    std::string globalId = global->mId.serializeText();
                    float value = getGlobalVariableValue(globalId);

                    return std::make_tuple(globalId, value);
                });
        };
        globalStoreT[sol::meta_function::ipairs] = lua["ipairsForArray"].template get<sol::function>();
        api["getGlobalVariables"] = [context, globalStore](sol::optional<GObject> player) {
            if (context.isScoped())
            {
                return context.withScoped([&](MWScript::InterpreterContext& scoped) {
                    if (player.has_value() && !matchesScopedPlayer(*player, scoped))
                        throw std::runtime_error("First argument must either be a player or be missing");
                    (void)scoped.getGlobals();
                    return GlobalVariables{ context, nullptr };
                });
            }
            if (player.has_value() && player->ptr() != MWBase::Environment::get().getWorld()->getPlayerPtr())
                throw std::runtime_error("First argument must either be a player or be missing");

            return GlobalVariables{ context, globalStore };
        };
        return LuaUtil::makeReadOnly(api);
    }
}
