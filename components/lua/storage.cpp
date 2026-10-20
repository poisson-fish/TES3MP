#include "storage.hpp"

#include <filesystem>
#include <fstream>

#include <components/debug/debuglog.hpp>

#include "luastate.hpp"

namespace sol
{
    template <>
    struct is_automagical<LuaUtil::LuaStorage::SectionView> : std::false_type
    {
    };
}

namespace LuaUtil
{
    LuaStorage::Value LuaStorage::Section::sEmpty;

    void LuaStorage::registerLifeTime(LuaUtil::LuaView& view, sol::table& res)
    {
        res["LIFE_TIME"] = LuaUtil::makeStrictReadOnly(tableFromPairs<std::string_view, Section::LifeTime>(view.sol(),
            {
                { "Persistent", Section::LifeTime::Persistent },
                { "GameSession", Section::LifeTime::GameSession },
                { "Temporary", Section::LifeTime::Temporary },
            }));
    }

    sol::object LuaStorage::Value::getCopy(lua_State* state) const
    {
        return deserialize(state, mSerializedValue);
    }

    sol::object LuaStorage::Value::getReadOnly(lua_State* state) const
    {
        if (mReadOnlyValue == sol::nil && !mSerializedValue.empty())
            mReadOnlyValue = sol::main_object(deserialize(state, mSerializedValue, nullptr, true));
        return mReadOnlyValue;
    }

    const LuaStorage::Value& LuaStorage::Section::get(std::string_view key) const
    {
        checkIfActive();
        auto it = mValues.find(key);
        if (it != mValues.end())
            return it->second;
        else
            return sEmpty;
    }

    void LuaStorage::Section::runCallbacks(sol::optional<std::string_view> changedKey)
    {
        struct Guard
        {
            LuaStorage* storage;
            const Section* section;
            ~Guard() { storage->mRunningCallbacks.erase(section); }
        } guard{ mStorage, this };
        mStorage->mRunningCallbacks.insert(this);
        auto run = [&](std::vector<Callback>& callbacks) {
            callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                 [&](const Callback& callback) {
                                     bool valid = callback.isValid();
                                     if (valid)
                                     {
                                         if (mStorage->mStrict)
                                             callback.call(mSectionName, changedKey);
                                         else
                                             callback.tryCall(mSectionName, changedKey);
                                     }
                                     return !valid;
                                 }),
                callbacks.end());
        };
        run(mCallbacks);
        run(mMenuScriptsCallbacks);
    }

    void LuaStorage::Section::throwIfCallbackRecursionIsTooDeep()
    {
        if (mStorage->mRunningCallbacks.count(this) > 0)
            throw std::runtime_error(
                "Storage handler shouldn't change the storage section it handles (leads to an infinite recursion)");
        if (mStorage->mRunningCallbacks.size() > 10)
            throw std::runtime_error(
                "Too many subscribe callbacks triggering in a chain, likely an infinite recursion");
    }

    void LuaStorage::Section::set(std::string_view key, const sol::object& value)
    {
        checkIfActive(false);
        if (mStorage->mStrict
            && (mSectionName.empty() || mSectionName.size() > 128 || mSectionName.find('\0') != std::string::npos
                || key.empty() || key.size() > 128 || key.find('\0') != std::string_view::npos))
            throw std::invalid_argument("Storage section/key name exceeds bounds");
        throwIfCallbackRecursionIsTooDeep();
        auto previous = mValues;
        if (value != sol::nil)
            mValues[std::string(key)] = Value(value);
        else
        {
            auto it = mValues.find(key);
            if (it != mValues.end())
                mValues.erase(it);
        }
        try
        {
            if (mStorage->mListener)
                mStorage->mListener->valueChanged(mSectionName, key, value);
            runCallbacks(key);
        }
        catch (...)
        {
            if (mStorage->mStrict)
                mValues.swap(previous);
            throw;
        }
    }

    void LuaStorage::Section::setAll(const sol::optional<sol::table>& values)
    {
        checkIfActive(false);
        if (mStorage->mStrict
            && (mSectionName.empty() || mSectionName.size() > 128 || mSectionName.find('\0') != std::string::npos))
            throw std::invalid_argument("Storage section name exceeds bounds");
        throwIfCallbackRecursionIsTooDeep();
        std::map<std::string, Value, std::less<>> staged;
        if (values)
        {
            for (const auto& [k, v] : *values)
            {
                std::string key = cast<std::string>(k);
                if (mStorage->mStrict && (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos))
                    throw std::invalid_argument("Storage key name exceeds bounds");
                staged[std::move(key)] = Value(v);
            }
        }
        mValues.swap(staged);
        try
        {
            if (mStorage->mListener)
                mStorage->mListener->sectionReplaced(mSectionName, values);
            runCallbacks(sol::nullopt);
        }
        catch (...)
        {
            if (mStorage->mStrict)
                mValues.swap(staged);
            throw;
        }
    }

    sol::table LuaStorage::Section::asTable(lua_State* state)
    {
        checkIfActive();
        sol::table res(state, sol::create);
        for (const auto& [k, v] : mValues)
            res[k] = v.getCopy(state);
        return res;
    }

    void LuaStorage::initLuaBindings(LuaUtil::LuaView& view)
    {
        sol::usertype<SectionView> sview = view.sol().new_usertype<SectionView>("Section");
        sview["get"] = [](sol::this_state s, const SectionView& section, std::string_view key) {
            return section.mSection->get(key).getReadOnly(s);
        };
        sview["getCopy"] = [](sol::this_state s, const SectionView& section, std::string_view key) {
            return section.mSection->get(key).getCopy(s);
        };
        sview["asTable"]
            = [](sol::this_state lua, const SectionView& section) { return section.mSection->asTable(lua); };
        sview["subscribe"] = [](const SectionView& section, const sol::table& callback) {
            section.mSection->checkIfActive(true);
            Callback cb = Callback::fromLua(callback);
            if (section.mSection->mStorage->mStrict)
            {
                sol::optional<ScriptId> scriptId = cb.mHiddenData[ScriptsContainer::sScriptIdKey];
                if (scriptId && scriptId->mContainer)
                    scriptId->mContainer->validateContext();
            }
            std::vector<Callback>& callbacks
                = section.mForMenuScripts ? section.mSection->mMenuScriptsCallbacks : section.mSection->mCallbacks;
            callbacks.erase(
                std::remove_if(callbacks.begin(), callbacks.end(), [&](const Callback& c) { return !c.isValid(); }),
                callbacks.end());
            callbacks.push_back(std::move(cb));
        };
        sview["reset"] = [](const SectionView& section, const sol::optional<sol::table>& newValues) {
            if (section.mReadOnly)
                throw std::runtime_error("Access to storage is read only");
            section.mSection->setAll(newValues);
        };
        sview["removeOnExit"] = [](const SectionView& section) {
            if (section.mReadOnly)
                throw std::runtime_error("Access to storage is read only");
            section.mSection->checkIfActive(false);
            section.mSection->mLifeTime = Section::Temporary;
        };
        sview["setLifeTime"] = [](const SectionView& section, Section::LifeTime lifeTime) {
            if (section.mReadOnly)
                throw std::runtime_error("Access to storage is read only");
            section.mSection->checkIfActive(false);
            section.mSection->mLifeTime = lifeTime;
        };
        sview["set"] = [](const SectionView& section, std::string_view key, const sol::object& value) {
            if (section.mReadOnly)
                throw std::runtime_error("Access to storage is read only");
            section.mSection->set(key, value);
        };
    }

    sol::table LuaStorage::initGlobalPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage)
    {
        sol::table res(view.sol(), sol::create);
        registerLifeTime(view, res);

        res["globalSection"] = [globalStorage](sol::this_state lua, std::string_view section) {
            return globalStorage->getMutableSection(lua, section);
        };
        res["allGlobalSections"] = [globalStorage](sol::this_state lua) { return globalStorage->getAllSections(lua); };
        return LuaUtil::makeReadOnly(res);
    }

    sol::table LuaStorage::initLocalPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage)
    {
        sol::table res(view.sol(), sol::create);
        registerLifeTime(view, res);

        res["globalSection"] = [globalStorage](sol::this_state lua, std::string_view section) {
            return globalStorage->getReadOnlySection(lua, section);
        };
        return LuaUtil::makeReadOnly(res);
    }

    sol::table LuaStorage::initPlayerPackage(
        LuaUtil::LuaView& view, LuaStorage* globalStorage, LuaStorage* playerStorage)
    {
        sol::table res(view.sol(), sol::create);
        registerLifeTime(view, res);

        res["globalSection"] = [globalStorage](sol::this_state lua, std::string_view section) {
            return globalStorage->getReadOnlySection(lua, section);
        };
        res["playerSection"] = [playerStorage](sol::this_state lua, std::string_view section) {
            return playerStorage->getMutableSection(lua, section);
        };
        res["allPlayerSections"] = [playerStorage](sol::this_state lua) { return playerStorage->getAllSections(lua); };
        return LuaUtil::makeReadOnly(res);
    }

    sol::table LuaStorage::initMenuPackage(LuaUtil::LuaView& view, LuaStorage* globalStorage, LuaStorage* playerStorage)
    {
        sol::table res(view.sol(), sol::create);
        registerLifeTime(view, res);

        res["playerSection"] = [playerStorage](sol::this_state lua, std::string_view section) {
            return playerStorage->getMutableSection(lua, section, /*forMenuScripts=*/true);
        };
        res["globalSection"] = [globalStorage](sol::this_state lua, std::string_view section) {
            return globalStorage->getReadOnlySection(lua, section);
        };
        res["allPlayerSections"] = [playerStorage](sol::this_state lua) { return playerStorage->getAllSections(lua); };
        return LuaUtil::makeReadOnly(res);
    }

    sol::table LuaStorage::initLoadPackage(LuaUtil::LuaView& view, LuaStorage* playerStorage)
    {
        sol::table res(view.sol(), sol::create);
        registerLifeTime(view, res);

        res["playerSection"] = [playerStorage](sol::this_state lua, std::string_view section) {
            return playerStorage->getReadOnlySection(lua, section);
        };
        return LuaUtil::makeReadOnly(res);
    }

    void LuaStorage::clearTemporaryAndRemoveCallbacks()
    {
        auto it = mData.begin();
        while (it != mData.end())
        {
            it->second->mCallbacks.clear();
            // Note that we don't clear menu callbacks for permanent sections
            // because starting/loading a game doesn't reset menu scripts.
            if (it->second->mLifeTime == Section::Temporary)
            {
                it->second->mMenuScriptsCallbacks.clear();
                it->second->mValues.clear();
                it = mData.erase(it);
            }
            else
                ++it;
        }
    }

    void LuaStorage::load(lua_State* state, const std::filesystem::path& path)
    {
        assert(mData.empty()); // Shouldn't be used before loading
        try
        {
            std::uintmax_t fileSize = std::filesystem::file_size(path);
            Log(Debug::Info) << "Loading Lua storage \"" << path << "\" (" << fileSize << " bytes)";
            if (fileSize == 0)
                throw std::runtime_error("Storage file has zero length");

            std::ifstream fin(path, std::fstream::binary);
            std::string serializedData((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
            sol::table data = deserialize(state, serializedData);
            for (const auto& [sectionName, sectionTable] : data)
            {
                const std::shared_ptr<Section>& section = getSection(cast<std::string_view>(sectionName));
                for (const auto& [key, value] : cast<sol::table>(sectionTable))
                    section->set(cast<std::string_view>(key), value);
            }
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Cannot read \"" << path << "\": " << e.what();
        }
    }

    std::string LuaStorage::save(lua_State* state) const
    {
        validateAccess(true);
        sol::table data(state, sol::create);
        for (const auto& [sectionName, section] : mData)
        {
            if (section->mLifeTime == Section::Persistent && !section->mValues.empty())
                data[sectionName] = section->asTable(state);
        }
        return serialize(data);
    }

    void LuaStorage::load(lua_State* state, std::string_view binaryData)
    {
        validateAccess(false);
        if (binaryData.empty())
            throw std::runtime_error("Storage data has zero length");
        sol::table data = cast<sol::table>(deserialize(state, binaryData));
        Snapshot staged = snapshot();
        for (auto& [_, sec] : staged)
            sec.mValues.clear();
        for (const auto& [sectionNameObj, sectionTableObj] : data)
        {
            std::string_view sectionName = cast<std::string_view>(sectionNameObj);
            if (mStrict
                && (sectionName.empty() || sectionName.size() > 128
                    || sectionName.find('\0') != std::string_view::npos))
                throw std::invalid_argument("Storage section name exceeds bounds");
            sol::table sectionTable = cast<sol::table>(sectionTableObj);
            auto& secSnap = staged[std::string(sectionName)];
            for (const auto& [keyObj, valueObj] : sectionTable)
            {
                std::string_view key = cast<std::string_view>(keyObj);
                if (mStrict && (key.empty() || key.size() > 128 || key.find('\0') != std::string_view::npos))
                    throw std::invalid_argument("Storage key name exceeds bounds");
                secSnap.mValues[std::string(key)] = serialize(valueObj);
            }
        }
        restore(staged);
    }

    LuaStorage::Snapshot LuaStorage::snapshot() const
    {
        Snapshot result;
        for (const auto& [sectionName, section] : mData)
        {
            SectionSnapshot entry;
            entry.mLifeTime = static_cast<int>(section->mLifeTime);
            for (const Callback& c : section->mCallbacks)
                if (c.isValid())
                    entry.mCallbacks.push_back(c);
            for (const Callback& c : section->mMenuScriptsCallbacks)
                if (c.isValid())
                    entry.mMenuScriptsCallbacks.push_back(c);
            for (const auto& [k, v] : section->mValues)
                entry.mValues.emplace(k, v.serialized());
            result.emplace(sectionName, std::move(entry));
        }
        return result;
    }

    void LuaStorage::restore(const Snapshot& saved)
    {
        mRunningCallbacks.clear();
        for (auto it = mData.begin(); it != mData.end();)
        {
            if (!saved.contains(it->first))
            {
                it->second->mValues.clear();
                it->second->mCallbacks.clear();
                it->second->mMenuScriptsCallbacks.clear();
                it->second->mLifeTime = Section::Persistent;
                it = mData.erase(it);
            }
            else
                ++it;
        }
        auto restoreCallbacks = [](std::vector<Callback>& target, const std::vector<Callback>& snap) {
            const bool snapHasValid
                = std::any_of(snap.begin(), snap.end(), [](const Callback& c) { return c.isValid(); });
            if (snapHasValid || snap.empty())
            {
                target.clear();
                for (const Callback& c : snap)
                    if (c.isValid())
                        target.push_back(c);
            }
            else
            {
                target.erase(
                    std::remove_if(target.begin(), target.end(), [](const Callback& c) { return !c.isValid(); }),
                    target.end());
            }
        };
        for (const auto& [sectionName, secSnap] : saved)
        {
            auto it = mData.find(sectionName);
            if (it == mData.end())
            {
                auto section = std::make_shared<Section>(this, sectionName);
                std::string_view view = section->mSectionName;
                it = mData.emplace(view, std::move(section)).first;
            }
            Section& section = *it->second;
            section.mLifeTime = static_cast<Section::LifeTime>(secSnap.mLifeTime);
            restoreCallbacks(section.mCallbacks, secSnap.mCallbacks);
            restoreCallbacks(section.mMenuScriptsCallbacks, secSnap.mMenuScriptsCallbacks);
            section.mValues.clear();
            for (const auto& [k, serialized] : secSnap.mValues)
                section.mValues.emplace(k, Value(serialized));
        }
    }

    void LuaStorage::save(lua_State* state, const std::filesystem::path& path) const
    {
        std::string serializedData = save(state);
        Log(Debug::Info) << "Saving Lua storage \"" << path << "\" (" << serializedData.size() << " bytes)";
        std::ofstream fout(path, std::fstream::binary);
        fout.write(serializedData.data(), serializedData.size());
        fout.close();
    }

    const std::shared_ptr<LuaStorage::Section>& LuaStorage::getSection(std::string_view sectionName)
    {
        checkIfActive();
        if (mStrict
            && (sectionName.empty() || sectionName.size() > 128 || sectionName.find('\0') != std::string_view::npos))
            throw std::invalid_argument("Storage section name exceeds bounds");
        auto it = mData.find(sectionName);
        if (it != mData.end())
            return it->second;
        auto section = std::make_shared<Section>(this, std::string(sectionName));
        sectionName = section->mSectionName;
        auto [newIt, _] = mData.emplace(sectionName, std::move(section));
        return newIt->second;
    }

    sol::object LuaStorage::getSection(
        lua_State* state, std::string_view sectionName, bool readOnly, bool forMenuScripts)
    {
        validateAccess(readOnly);
        const std::shared_ptr<Section>& section = getSection(sectionName);
        return sol::make_object<SectionView>(state, SectionView{ section, readOnly, forMenuScripts });
    }

    sol::table LuaStorage::getAllSections(lua_State* state, bool readOnly)
    {
        validateAccess(readOnly);
        sol::table res(state, sol::create);
        for (const auto& [sectionName, _] : mData)
            res[sectionName] = getSection(state, sectionName, readOnly);
        return res;
    }

}
