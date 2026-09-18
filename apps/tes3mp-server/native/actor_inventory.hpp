#ifndef TES3MP_NATIVE_ACTOR_INVENTORY_HPP
#define TES3MP_NATIVE_ACTOR_INVENTORY_HPP

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>

namespace TES3MP::Native
{
    // This bootstrap has no dynamic death/AI writer. Only content-defined
    // corpses are lootable; neither client state nor a command can declare death.
    inline bool initialCorpse(const MWWorld::ConstPtr& ptr)
    {
        if (ptr.getType() == ESM::NPC::sRecordId)
        {
            const auto& npc = *ptr.get<ESM::NPC>()->mBase;
            return npc.mNpdtType != ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS && npc.mNpdt.mHealth < 1;
        }
        return ptr.getType() == ESM::Creature::sRecordId && ptr.get<ESM::Creature>()->mBase->mData.mHealth < 1;
    }
    inline bool actorInventory(const MWWorld::ConstPtr& ptr)
    {
        return ptr.getType() == ESM::NPC::sRecordId || ptr.getType() == ESM::Creature::sRecordId;
    }
}
#endif
