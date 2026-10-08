#include "defines.hpp"

#include <algorithm>
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>

namespace
{

    bool check(std::string_view str, std::string_view escword, size_t& i, size_t& start)
    {
        bool found = Misc::StringUtils::ciStartsWith(str, escword);
        if (found)
        {
            i += escword.length();
            start = i + 1;
        }
        return found;
    }

    std::vector<std::string> globals;
    const std::initializer_list<std::tuple<std::string_view, std::string_view>> sActionBindings{
        { "actionslideright", "#{sRight}" },
        { "actionreadymagic", "#{sReady_Magic}" },
        { "actionprevweapon", "#{sPrevWeapon}" },
        { "actionnextweapon", "#{sNextWeapon}" },
        { "actiontogglerun", "#{sAuto_Run}" },
        { "actionslideleft", "#{sLeft}" },
        { "actionreadyitem", "#{sReady_Weapon}" },
        { "actionprevspell", "#{sPrevSpell}" },
        { "actionnextspell", "#{sNextSpell}" },
        { "actionrestmenu", "#{sRestKey}" },
        { "actionmenumode", "#{sInventory}" },
        { "actionactivate", "#{sActivate}" },
        { "actionjournal", "#{sJournal}" },
        { "actionforward", "#{sForward}" },
        { "actioncrouch", "#{sCrouch_Sneak}" },
        { "actionjump", "#{sJump}" },
        { "actionback", "#{sBack}" },
        { "actionuse", "#{sUse}" },
        { "actionrun", "#{sRun}" },
    };
    using ContextMethod = std::string_view (Interpreter::Context::*)() const;
    const std::initializer_list<std::tuple<std::string_view, std::pair<ContextMethod, ContextMethod>>> sContextMethods{
        { "nextpcrank", { &Interpreter::Context::getPCNextRank, nullptr } },
        { "pcnextrank", { &Interpreter::Context::getPCNextRank, nullptr } },
        { "faction", { &Interpreter::Context::getNPCFaction, nullptr } },
        { "pcclass", { &Interpreter::Context::getPCClass, &Interpreter::Context::getPCClass } },
        { "pcname", { &Interpreter::Context::getPCName, &Interpreter::Context::getPCName } },
        { "pcrace", { &Interpreter::Context::getPCRace, &Interpreter::Context::getPCRace } },
        { "pcrank", { &Interpreter::Context::getPCRank, nullptr } },
        { "class", { &Interpreter::Context::getNPCClass, &Interpreter::Context::getPCClass } },
        { "cell", { &Interpreter::Context::getCurrentCellName, &Interpreter::Context::getCurrentCellName } },
        { "race", { &Interpreter::Context::getNPCRace, &Interpreter::Context::getPCRace } },
        { "rank", { &Interpreter::Context::getNPCRank, nullptr } },
        { "name", { &Interpreter::Context::getActorName, &Interpreter::Context::getPCName } },
    };

    bool longerStr(std::string_view a, std::string_view b)
    {
        return a.length() > b.length();
    }

    void append(std::string& output, std::string_view value, size_t maximumBytes)
    {
        if (value.size() > maximumBytes - output.size())
            throw std::invalid_argument("Dialogue substitution output exceeds bounds");
        if (maximumBytes != std::numeric_limits<size_t>::max() && value.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Invalid dialogue substitution text");
        output.append(value);
    }

    template<class T> void appendNumber(std::string& output, T value, size_t maximumBytes)
    {
        std::ostringstream formatted;
        formatted << value;
        append(output, formatted.str(), maximumBytes);
    }

    bool findReplacement(std::string_view temp, size_t& i, size_t& start, Interpreter::Context& context,
        std::string& retval, bool dialogue, size_t maximumBytes)
    {
        for (const auto& [name, binding] : sActionBindings)
        {
            if (check(temp, name, i, start))
            {
                append(retval, context.getActionBinding(binding), maximumBytes);
                return true;
            }
        }
        if (check(temp, "pccrimelevel", i, start))
        {
            appendNumber(retval, context.getPCBounty(), maximumBytes);
            return true;
        }
        for (const auto& [name, methods] : sContextMethods)
        {
            const auto& method = dialogue ? methods.first : methods.second;
            if (check(temp, name, i, start))
            {
                if (method) // Not all variables are available outside of dialogue
                    append(retval, (context.*method)(), maximumBytes);
                return true;
            }
        }
        return false;
    }

    std::string fixDefinesReal(std::string_view text, bool dialogue, Interpreter::Context& context,
        size_t maximumBytes = std::numeric_limits<size_t>::max())
    {
        const bool transactional = maximumBytes != std::numeric_limits<size_t>::max();
        if (text.size() > maximumBytes)
            throw std::invalid_argument("Dialogue substitution input exceeds bounds");
        std::vector<std::string> scopedGlobals;
        auto& names = transactional ? scopedGlobals : globals;
        bool loadedGlobals = false;
        size_t start = 0;
        std::string retval;
        for (size_t i = 0; i < text.length(); ++i)
        {
            char eschar = text[i];
            if (eschar == '%' || eschar == '^')
            {
                append(retval, text.substr(start, i - start), maximumBytes);
                std::string_view temp = text.substr(i + 1);

                bool found = false;
                try
                {
                    found = findReplacement(temp, i, start, context, retval, dialogue, maximumBytes);
                    /* Not a builtin, try global variables */
                    if (!found)
                    {
                        /* if list of globals is empty, grab it and sort it by descending string length */
                        if (names.empty() && !loadedGlobals)
                        {
                            names = context.getGlobals();
                            sort(names.begin(), names.end(), longerStr);
                        }
                        loadedGlobals = true;

                        for (const std::string& global : names)
                        {
                            found = check(temp, global, i, start);
                            if (found)
                            {
                                char type = context.getGlobalType(global);

                                switch (type)
                                {
                                    case 's':
                                        appendNumber(retval, context.getGlobalShort(global), maximumBytes);
                                        break;
                                    case 'l':
                                        appendNumber(retval, context.getGlobalLong(global), maximumBytes);
                                        break;
                                    case 'f':
                                        appendNumber(retval, context.getGlobalFloat(global), maximumBytes);
                                        break;
                                }
                                break;
                            }
                        }
                    }
                }
                catch (std::exception& e)
                {
                    if (transactional)
                        throw;
                    Log(Debug::Error) << "Error: Failed to replace escape character, with the following error: "
                                      << e.what();
                    Log(Debug::Error) << "Full text below:\n" << text;
                }

                // Not found, or error
                if (!found)
                {
                    /* leave unmodified */
                    i += 1;
                    start = i;
                    append(retval, std::string_view(&eschar, 1), maximumBytes);
                }
            }
        }
        append(retval, text.substr(start, text.length() - start), maximumBytes);
        return retval;
    }

}

namespace Interpreter
{
    std::string fixDefinesDialog(std::string_view text, Context& context)
    {
        return fixDefinesReal(text, true, context);
    }

    std::string fixDefinesDialog(std::string_view text, Context& context, size_t maximumBytes)
    {
        return fixDefinesReal(text, true, context, maximumBytes);
    }

    std::string fixDefinesMsgBox(std::string_view text, Context& context)
    {
        return fixDefinesReal(text, false, context);
    }

    std::string fixDefinesBook(std::string_view text, Context& context)
    {
        return fixDefinesReal(text, false, context);
    }
}
