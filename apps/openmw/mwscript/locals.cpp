#include "locals.hpp"
#include "globalscripts.hpp"

#include <stdexcept>

#include <components/compiler/locals.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/locals.hpp>
#include <components/esm3/variant.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/scriptmanager.hpp"

#include "../mwworld/esmstore.hpp"

namespace MWScript
{
    void Locals::ensure(const ESM::RefId& scriptName)
    {
        if (!mInitialised)
        {
            const ESM::Script* script = MWBase::Environment::get().getESMStore()->get<ESM::Script>().find(scriptName);

            configure(*script);
        }
    }

    Locals::Locals()
        : mInitialised(false)
    {
    }

    bool Locals::configure(const ESM::Script& script)
    {
        if (mInitialised)
            return false;
        return configure(script, *MWBase::Environment::get().getScriptManager());
    }

    bool Locals::configure(const ESM::Script& script, MWBase::ScriptManager& scripts)
    {
        if (mInitialised)
            return false;

        const GlobalScriptDesc* global = scripts.getGlobalScripts().getScriptIfPresent(script.mId);
        if (global)
        {
            mShorts = global->mLocals.mShorts;
            mLongs = global->mLocals.mLongs;
            mFloats = global->mLocals.mFloats;
        }
        else
        {
            const Compiler::Locals& locals = scripts.getLocals(script.mId);

            mShorts.clear();
            mShorts.resize(locals.get('s').size(), 0);
            mLongs.clear();
            mLongs.resize(locals.get('l').size(), 0);
            mFloats.clear();
            mFloats.resize(locals.get('f').size(), 0);
        }

        mScriptId = script.mId;
        mInitialised = true;
        return true;
    }

    bool Locals::isEmpty() const
    {
        return (mShorts.empty() && mLongs.empty() && mFloats.empty());
    }

    bool Locals::hasVar(const ESM::RefId& script, std::string_view var)
    {
        ensure(script);

        const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script);
        int index = locals.getIndex(var);
        return (index != -1);
    }

    double Locals::getVarAsDouble(const ESM::RefId& script, std::string_view var)
    {
        ensure(script);

        const Compiler::Locals& locals = MWBase::Environment::get().getScriptManager()->getLocals(script);
        int index = locals.getIndex(var);
        if (index == -1)
            return 0;
        switch (locals.getType(var))
        {
            case 's':
                return mShorts.at(index);

            case 'l':
                return mLongs.at(index);

            case 'f':
                return mFloats.at(index);
            default:
                return 0;
        }
    }

    bool Locals::setVar(const ESM::RefId& script, std::string_view var, double val)
    {
        ensure(script);
        return setVar(MWBase::Environment::get().getScriptManager()->getLocals(script), var, val);
    }

    bool Locals::setVar(const ESM::Script& script, std::string_view var, double val, MWBase::ScriptManager& scripts)
    {
        configure(script, scripts);
        return setVar(scripts.getLocals(script.mId), var, val);
    }

    bool Locals::setVar(const Compiler::Locals& locals, std::string_view var, double val)
    {
        int index = locals.getIndex(var);
        if (index == -1)
            return false;
        switch (locals.getType(var))
        {
            case 's':
                mShorts.at(index) = static_cast<Interpreter::Type_Short>(val);
                break;

            case 'l':
                mLongs.at(index) = static_cast<Interpreter::Type_Integer>(val);
                break;

            case 'f':
                mFloats.at(index) = static_cast<Interpreter::Type_Float>(val);
                break;
        }
        return true;
    }

    std::size_t Locals::getSize(const ESM::RefId& script)
    {
        ensure(script);
        return mShorts.size() + mLongs.size() + mFloats.size();
    }

    bool Locals::write(ESM::Locals& locals, const ESM::RefId& script) const
    {
        if (!mInitialised)
            return false;

        return write(locals, MWBase::Environment::get().getScriptManager()->getLocals(script));
    }

    bool Locals::write(ESM::Locals& locals, const Compiler::Locals& declarations) const
    {
        if (!mInitialised)
            return false;

        // Check the entire shape before allocating or publishing any values.
        // Extra values must not be silently lost; missing ones must not leave a
        // partially appended save. Names must identify one unambiguous variable.
        if (declarations.get('s').size() != mShorts.size() || declarations.get('l').size() != mLongs.size()
            || declarations.get('f').size() != mFloats.size())
            throw std::invalid_argument("Local declaration/value shape mismatch");
        for (char type : { 's', 'l', 'f' })
        {
            const auto& names = declarations.get(type);
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i].empty() || declarations.getType(names[i]) != type
                    || declarations.getIndex(names[i]) != static_cast<int>(i))
                    throw std::invalid_argument("Invalid local declaration name");
        }

        auto variables = locals.mVariables;
        for (char type : { 's', 'l', 'f' })
        {
            const auto& names = declarations.get(type);
            for (size_t i = 0; i < names.size(); ++i)
            {
                ESM::Variant value;
                switch (type)
                {
                    case 's':
                        value.setType(ESM::VT_Int);
                        value.setInteger(mShorts[i]);
                        break;
                    case 'l':
                        value.setType(ESM::VT_Int);
                        value.setInteger(mLongs[i]);
                        break;
                    case 'f':
                        value.setType(ESM::VT_Float);
                        value.setFloat(mFloats[i]);
                        break;
                }
                variables.emplace_back(names[i], value);
            }
        }
        locals.mVariables.swap(variables);
        return true;
    }

    void Locals::read(const ESM::Locals& locals, const ESM::RefId& script)
    {
        ensure(script);

        const Compiler::Locals& declarations = MWBase::Environment::get().getScriptManager()->getLocals(script);

        int index = 0, numshorts = 0, numlongs = 0;
        for (const auto& [_, value] : locals.mVariables)
        {
            ESM::VarType type = value.getType();
            if (type == ESM::VT_Short)
                ++numshorts;
            else if (type == ESM::VT_Int)
                ++numlongs;
        }

        for (const auto& [name, value] : locals.mVariables)
        {
            if (name.empty())
            {
                // no variable names available (this will happen for legacy, i.e. ESS-imported savegames only)
                try
                {
                    if (index >= numshorts + numlongs)
                        mFloats.at(index - (numshorts + numlongs)) = value.getFloat();
                    else if (index >= numshorts)
                        mLongs.at(index - numshorts) = value.getInteger();
                    else
                        mShorts.at(index) = static_cast<Interpreter::Type_Short>(value.getInteger());
                }
                catch (std::exception& e)
                {
                    Log(Debug::Error) << "Failed to read local variable state for script '" << script
                                      << "' (legacy format): " << e.what() << "\nNum shorts: " << numshorts << " / "
                                      << mShorts.size() << " Num longs: " << numlongs << " / " << mLongs.size();
                }
            }
            else
            {
                char type = declarations.getType(name);
                int index2 = declarations.getIndex(name);

                // silently ignore locals that don't exist anymore
                if (type == ' ' || index2 == -1)
                    continue;

                try
                {
                    switch (type)
                    {
                        case 's':
                            mShorts.at(index2) = static_cast<Interpreter::Type_Short>(value.getInteger());
                            break;
                        case 'l':
                            mLongs.at(index2) = value.getInteger();
                            break;
                        case 'f':
                            mFloats.at(index2) = value.getFloat();
                            break;
                    }
                }
                catch (...)
                {
                    // ignore type changes
                    /// \todo write to log
                }
            }
        }
    }
}
