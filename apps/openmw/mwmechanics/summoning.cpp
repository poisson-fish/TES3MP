#include "summoning.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/misc/resourcehelpers.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwrender/animation.hpp"

#include "aifollow.hpp"
#include "creaturestats.hpp"

namespace MWMechanics
{

    ESM::RefId getSummonedCreature(ESM::RefId effectId, const MWWorld::ESMStore& store)
    {
        return selectSummonedCreature(effectId, [&](std::string_view setting) {
            return store.get<ESM::GameSetting>().find(setting)->mValue.getString();
        });
    }

    ESM::RefId getSummonedCreature(ESM::RefId effectId)
    {
        return getSummonedCreature(effectId, *MWBase::Environment::get().getESMStore());
    }

    ESM::RefNum summonCreature(ESM::RefId effectId, const MWWorld::Ptr& summoner)
    {
        auto world = MWBase::Environment::get().getWorld();
        const auto record = getSummonedCreature(effectId, world->getStore());
        MWWorld::Ptr placed;
        return createSummon<ESM::RefNum>(record, [&](ESM::RefId selected, ESM::RefNum& identity) {
            MWWorld::ManualRef ref(world->getStore(), selected, 1);
            placed = world->safePlaceObject(ref.getPtr(), summoner, summoner.getCell(), 0, 120.f);
            MWBase::Environment::get().getWorldModel()->registerPtr(placed);
            identity = placed.getCellRef().getRefNum();
        }, [&](ESM::RefNum) {
            AiFollow package(summoner);
            placed.getClass().getCreatureStats(placed).getAiSequence().stack(package, placed);
            MWRender::Animation* anim = world->getAnimation(placed);
            if (anim)
            {
                const ESM::Static* fx = world->getStore().get<ESM::Static>().search(
                    ESM::RefId::stringRefId("VFX_Summon_Start"));
                if (fx)
                    anim->addEffect(Misc::ResourceHelpers::correctMeshPath(
                        VFS::Path::Normalized(fx->mModel)).value(), "", false);
            }
        }, [&](ESM::RefNum identity) {
            summoner.getClass().getCreatureStats(summoner).getSummonedCreatureMap().emplace(effectId, identity);
        }, [](const std::exception& error) {
            Log(Debug::Error) << "Failed to spawn summoned creature: " << error.what();
        });
    }

    void updateSummons(const MWWorld::Ptr& summoner, bool cleanup)
    {
        MWMechanics::CreatureStats& creatureStats = summoner.getClass().getCreatureStats(summoner);
        auto& creatureMap = creatureStats.getSummonedCreatureMap();

        if (!cleanup)
            return;

        for (auto it = creatureMap.begin(); it != creatureMap.end();)
        {
            if (!it->second.isSet())
            {
                // Keep the spell effect active if we failed to spawn anything
                it++;
                continue;
            }
            MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(it->second);
            if (!ptr.isEmpty() && ptr.getClass().getCreatureStats(ptr).isDead()
                && ptr.getClass().getCreatureStats(ptr).isDeathAnimationFinished())
            {
                // Purge the magic effect so a new creature can be summoned if desired
                auto summon = *it;
                creatureMap.erase(it++);
                purgeSummonEffect(summoner, summon);
            }
            else
                ++it;
        }
    }

    void purgeSummonEffect(const MWWorld::Ptr& summoner, const std::pair<ESM::RefId, ESM::RefNum>& summon)
    {
        auto& creatureStats = summoner.getClass().getCreatureStats(summoner);
        creatureStats.getActiveSpells().purge(
            [summon](const auto& spell, const auto& effect) {
                return effect.mEffectId == summon.first && effect.getActor() == summon.second;
            },
            summoner);

        MWBase::Environment::get().getMechanicsManager()->cleanupSummonedCreature(summon.second);
    }
}
