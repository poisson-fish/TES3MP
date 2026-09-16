#ifndef OPENMW_MWSCRIPT_ITEMLOCALS_H
#define OPENMW_MWSCRIPT_ITEMLOCALS_H

#include "../mwworld/ptr.hpp"

#include <functional>

namespace Compiler
{
    class Locals;
}

namespace MWScript
{
    // The provider configures/validates the item's locals and returns their
    // declarations. Detached callers supply owned locals, never a live script runner.
    struct ItemLocalsContext
    {
        MWWorld::Ptr mActor, mPlayer;
        std::function<const Compiler::Locals&(const MWWorld::Ptr&, ESM::RefId)> mDeclarations;
    };

    ItemLocalsContext stockItemLocalsContext(const MWWorld::Ptr& actor, const MWWorld::Ptr& player);
    // False means PCSkipEquip handled the use: OnPCEquip becomes 1, no action runs.
    bool beginItemUse(const MWWorld::Ptr& item, const ItemLocalsContext& context);
    void finishItemUse(const MWWorld::Ptr& item, bool willEquip, const ItemLocalsContext& context);
    void unequipItemLocals(const MWWorld::Ptr& item, const ItemLocalsContext& context);
}
#endif
