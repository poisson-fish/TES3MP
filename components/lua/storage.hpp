#ifndef COMPONENTS_LUA_STORAGE_H
#define COMPONENTS_LUA_STORAGE_H

#include <map>
#include <sol/sol.hpp>
#include <stdexcept>

#include "asyncpackage.hpp"
#include "serialization.hpp"

namespace LuaUtil
{
    class LuaView;

    class LuaStorage
    {
    public:
        static void initLuaBindings(LuaUtil::LuaView& view);
        static sol::table initGlobalPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage);
        static sol::table initLocalPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage);
        static sol::table initPlayerPackage(
            LuaUtil::LuaView& view, LuaStorage* globalStorage, LuaStorage* playerStorage);
        static sol::table initMenuPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage, LuaStorage* playerStorage);
        static sol::table initLoadPackage(LuaUtil::LuaView& view, LuaStorage* playerStorage);

        explicit LuaStorage() {}

        void clearTemporaryAndRemoveCallbacks();
        void load(lua_State* state, const std::filesystem::path& path);
        void save(lua_State* state, const std::filesystem::path& path) const;
        void load(lua_State* state, std::string_view binaryData);
        std::string save(lua_State* state) const;

        struct SectionSnapshot
        {
            std::map<std::string, std::string, std::less<>> mValues;
            std::vector<Callback> mCallbacks;
            std::vector<Callback> mMenuScriptsCallbacks;
            int mLifeTime = 0;
            bool operator==(const SectionSnapshot& other) const
            {
                return mValues == other.mValues && mLifeTime == other.mLifeTime
                    && mCallbacks.size() == other.mCallbacks.size()
                    && mMenuScriptsCallbacks.size() == other.mMenuScriptsCallbacks.size();
            }
        };
        using Snapshot = std::map<std::string, SectionSnapshot, std::less<>>;
        Snapshot snapshot() const;
        void restore(const Snapshot& saved);

        sol::object getSection(
            lua_State* state, std::string_view sectionName, bool readOnly, bool forMenuScripts = false);
        sol::object getMutableSection(lua_State* state, std::string_view sectionName, bool forMenuScripts = false)
        {
            return getSection(state, sectionName, false, forMenuScripts);
        }
        sol::object getReadOnlySection(lua_State* state, std::string_view sectionName)
        {
            return getSection(state, sectionName, true);
        }
        sol::table getAllSections(lua_State* state, bool readOnly = false);

        void setSingleValue(std::string_view section, std::string_view key, const sol::object& value)
        {
            validateAccess(false);
            getSection(section)->set(key, value);
        }

        void setSectionValues(std::string_view section, const sol::optional<sol::table>& values)
        {
            validateAccess(false);
            getSection(section)->setAll(values);
        }

        class Listener
        {
        public:
            virtual ~Listener() = default;
            virtual void valueChanged(
                std::string_view section, std::string_view key, const sol::object& value) const = 0;
            virtual void sectionReplaced(std::string_view section, const sol::optional<sol::table>& values) const = 0;
        };
        void setListener(const Listener* listener) { mListener = listener; }
        void setActive(bool active) { mActive = active; }
        void setStrict(bool strict) { mStrict = strict; }
        bool isStrict() const { return mStrict; }
        void setValidator(std::function<void(bool readOnly)> validator, bool strict = true)
        {
            mValidator = std::move(validator);
            mStrict = strict;
        }

    private:
        class Value
        {
        public:
            Value() {}
            explicit Value(std::string serialized)
                : mSerializedValue(std::move(serialized))
            {
            }
            Value(const sol::object& value)
                : mSerializedValue(serialize(value))
            {
            }
            sol::object getCopy(lua_State* state) const;
            sol::object getReadOnly(lua_State* state) const;
            const std::string& serialized() const { return mSerializedValue; }

        private:
            std::string mSerializedValue;
            mutable sol::main_object mReadOnlyValue = sol::nil;
        };

        struct Section
        {
            enum LifeTime
            {
                Persistent,
                GameSession,
                Temporary
            };

            explicit Section(LuaStorage* storage, std::string name)
                : mStorage(storage)
                , mSectionName(std::move(name))
            {
            }
            const Value& get(std::string_view key) const;
            void set(std::string_view key, const sol::object& value);
            void setAll(const sol::optional<sol::table>& values);
            sol::table asTable(lua_State* state);
            void runCallbacks(sol::optional<std::string_view> changedKey);
            void throwIfCallbackRecursionIsTooDeep();

            LuaStorage* mStorage;
            std::string mSectionName;
            std::map<std::string, Value, std::less<>> mValues;
            std::vector<Callback> mCallbacks;
            std::vector<Callback> mMenuScriptsCallbacks; // menu callbacks are in a separate vector because we don't
                                                         // remove them in clear()
            LifeTime mLifeTime = Persistent;
            static Value sEmpty;

            void checkIfActive(bool readOnly = true) const { mStorage->validateAccess(readOnly); }
        };
        struct SectionView
        {
            std::shared_ptr<Section> mSection;
            bool mReadOnly;
            bool mForMenuScripts = false;
        };

        const std::shared_ptr<Section>& getSection(std::string_view sectionName);

        std::map<std::string_view, std::shared_ptr<Section>> mData;
        const Listener* mListener = nullptr;
        std::set<const Section*> mRunningCallbacks;
        bool mActive = false;
        bool mStrict = false;
        std::function<void(bool readOnly)> mValidator;
        void checkIfActive() const
        {
            if (!mActive)
                throw std::logic_error("Trying to access inactive storage");
        }
        void validateAccess(bool readOnly) const
        {
            checkIfActive();
            if (mValidator)
                mValidator(readOnly);
        }
        static void registerLifeTime(LuaUtil::LuaView& view, sol::table& res);
    };

}

#endif // COMPONENTS_LUA_STORAGE_H
