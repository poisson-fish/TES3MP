#include "itemlocals.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadrepa.hpp>

namespace MWScript
{
    ItemLocalsContext stockItemLocalsContext(const MWWorld::Ptr& actor, const MWWorld::Ptr& player)
    {
        return { actor, player, [](const MWWorld::Ptr& item, ESM::RefId script) -> const Compiler::Locals& {
            auto& scripts = *MWBase::Environment::get().getScriptManager();
            item.getRefData().getLocals().configure(
                *MWBase::Environment::get().getESMStore()->get<ESM::Script>().find(script), scripts);
            return scripts.getLocals(script);
        } };
    }

    bool beginItemUse(const MWWorld::Ptr& item, const ItemLocalsContext& context)
    {
        const auto script = item.getClass().getScript(item);
        if (context.mActor != context.mPlayer || script.empty())
            return true;
        const auto& declarations = context.mDeclarations(item, script);
        auto& locals = item.getRefData().getLocals();
        const bool skip = static_cast<int>(locals.getVarAsDouble(declarations, "pcskipequip")) == 1;
        locals.setVar(declarations, "onpcequip", skip ? 1 : 0);
        return !skip;
    }

    void finishItemUse(const MWWorld::Ptr& item, bool willEquip, const ItemLocalsContext& context)
    {
        const auto script = item.getClass().getScript(item);
        if (context.mActor != context.mPlayer || script.empty() || !willEquip)
            return;
        const auto& declarations = context.mDeclarations(item, script);
        const auto type = item.getType();
        if (type == ESM::Book::sRecordId)
            item.getRefData().getLocals().setVar(declarations, "pcskipequip", 1);
        else if (type != ESM::Ingredient::sRecordId && type != ESM::Repair::sRecordId)
            item.getRefData().getLocals().setVar(declarations, "onpcequip", 1);
    }

    void unequipItemLocals(const MWWorld::Ptr& item, const ItemLocalsContext& context)
    {
        const auto script = item.getClass().getScript(item);
        if (context.mActor == context.mPlayer && !script.empty())
            item.getRefData().getLocals().setVar(context.mDeclarations(item, script), "onpcequip", 0);
    }
}
