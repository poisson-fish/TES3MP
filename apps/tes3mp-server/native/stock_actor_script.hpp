#ifndef TES3MP_NATIVE_STOCK_ACTOR_SCRIPT_HPP
#define TES3MP_NATIVE_STOCK_ACTOR_SCRIPT_HPP

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    // The stock summoned bonewalker bootstraps Brown Rot on its first cell
    // change. Admit that exact script through the existing condition writer;
    // a mod override or any other script still requires the script runtime.
    inline ESM::RefId stockActorSpawnDisease(ESM::RefId script, const MWWorld::ESMStore& store)
    {
        if (script != ESM::RefId::stringRefId("diseaseBrownRot")) return {};
        const auto* source = store.get<ESM::Script>().search(script);
        if (!source || source->mScriptText.size() > 1024) return {};
        std::string compact;
        bool quoted = false;
        for (unsigned char c : source->mScriptText)
        {
            if (!quoted && (c == '(' || c == ')' || c == '='))
            {
                if (!compact.empty() && compact.back() != ' ') compact += ' ';
                compact += char(c);
                compact += ' ';
                continue;
            }
            if (c == '"') quoted = !quoted;
            if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
            {
                if (!compact.empty() && compact.back() != ' ') compact += ' ';
            }
            else compact += char(c);
        }
        if (!compact.empty() && compact.back() == ' ') compact.pop_back();
        Misc::StringUtils::lowerCaseInPlace(compact);
        if (compact != "begin diseasebrownrot dontsaveobject if ( cellchanged = = 0 ) return endif if ( getcommondisease = = 0 ) addspell \"brown rot\" endif end")
            return {};
        return ESM::RefId::stringRefId("brown rot");
    }
}
#endif
