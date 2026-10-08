#include "disposition.hpp"
#include <cstdint>

#include "airating.hpp"
#include "npcstats.hpp"
#include "../mwworld/store.hpp"
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadnpc.hpp>

namespace MWMechanics
{
    int derivedDisposition(const ESM::NPC& speaker, const NpcStats& speakerStats,
        const ESM::NPC& player, const NpcStats& playerStats, const MWWorld::Store<ESM::GameSetting>& gmst,
        const std::function<int(const ESM::RefId&, const ESM::RefId&)>& factionReaction, bool clamp)
    {
        float x = static_cast<float>(int64_t(speakerStats.getBaseDisposition()) + speakerStats.getCrimeDispositionModifier());

        const float fDispRaceMod = gmst.find("fDispRaceMod")->mValue.getFloat();
        if (speaker.mRace == player.mRace)
            x += fDispRaceMod;

        const float fDispPersonalityMult = gmst.find("fDispPersonalityMult")->mValue.getFloat();
        const float fDispPersonalityBase = gmst.find("fDispPersonalityBase")->mValue.getFloat();
        x += fDispPersonalityMult
            * (playerStats.getAttribute(ESM::Attribute::Personality).getModified() - fDispPersonalityBase);

        float reaction = 0;
        int rank = 0;
        const ESM::RefId& npcFaction = speaker.mFaction;

        if (playerStats.getFactionRanks().find(npcFaction) != playerStats.getFactionRanks().end())
        {
            if (!playerStats.getExpelled(npcFaction))
            {
                // faction reaction towards itself. yes, that exists
                reaction = static_cast<float>(
                    factionReaction(npcFaction, npcFaction));

                rank = playerStats.getFactionRanks().find(npcFaction)->second;
            }
        }
        else if (!npcFaction.empty())
        {
            auto playerFactionIt = playerStats.getFactionRanks().begin();
            for (; playerFactionIt != playerStats.getFactionRanks().end(); ++playerFactionIt)
            {
                const ESM::RefId& itFaction = playerFactionIt->first;

                // Ignore the faction, if a player was expelled from it.
                if (playerStats.getExpelled(itFaction))
                    continue;

                int itReaction
                    = factionReaction(npcFaction, itFaction);
                if (playerFactionIt == playerStats.getFactionRanks().begin() || itReaction < reaction)
                {
                    reaction = static_cast<float>(itReaction);
                    rank = playerFactionIt->second;
                }
            }
        }
        else
        {
            reaction = 0;
            rank = 0;
        }

        const float fDispFactionRankMult = gmst.find("fDispFactionRankMult")->mValue.getFloat();
        const float fDispFactionRankBase = gmst.find("fDispFactionRankBase")->mValue.getFloat();
        const float fDispFactionMod = gmst.find("fDispFactionMod")->mValue.getFloat();
        x += factionDisposition(rank, reaction, fDispFactionRankMult, fDispFactionRankBase, fDispFactionMod);

        const float fDispCrimeMod = gmst.find("fDispCrimeMod")->mValue.getFloat();
        const float fDispDiseaseMod = gmst.find("fDispDiseaseMod")->mValue.getFloat();
        x += crimeDisposition(0.f, playerStats.getBounty(), fDispCrimeMod);
        if (playerStats.hasCommonDisease() || playerStats.hasBlightDisease())
            x += fDispDiseaseMod;

        const float fDispWeaponDrawn = gmst.find("fDispWeaponDrawn")->mValue.getFloat();
        x += weaponDrawnDisposition(playerStats.getDrawState() == MWMechanics::DrawState::Weapon,
            fDispWeaponDrawn);

        const float charm = speakerStats.getMagicEffects().getOrDefault(ESM::MagicEffect::Charm).getMagnitude();
        return dispositionWithCharm(x, charm, clamp);
    }
}
