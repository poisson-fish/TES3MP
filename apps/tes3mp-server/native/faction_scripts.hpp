#ifndef TES3MP_NATIVE_FACTION_SCRIPTS_HPP
#define TES3MP_NATIVE_FACTION_SCRIPTS_HPP

#include <components/esm/refid.hpp>

#include "actor_campaign.hpp"

namespace MWWorld { class ESMStore; class Ptr; }
namespace TES3MP::Native
{
    // Runs the supported stock faction instructions on detached player stats.
    // Unsupported instructions and unbounded scripts fail before actor-tick preparation.
    ActorCampaignCombat::PlayerAi runFactionScript(const MWWorld::ESMStore& content,
        const ESM::RefId& script, const MWWorld::Ptr& actor, const MWWorld::Ptr& player,
        const ActorCampaignCombat::PlayerAi& before);
}

#endif
