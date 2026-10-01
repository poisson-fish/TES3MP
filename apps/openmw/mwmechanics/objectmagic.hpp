#ifndef OPENMW_MWMECHANICS_OBJECTMAGIC_HPP
#define OPENMW_MWMECHANICS_OBJECTMAGIC_HPP
#include <components/esm/refid.hpp>
#include <limits>
#include "../mwworld/containerstore.hpp"
#include "../mwworld/class.hpp"
#include <components/esm3/loadmisc.hpp>
namespace MWMechanics
{
    inline bool lockRaisesLevel(int level, int magnitude) { return level < magnitude; }
    inline bool openReachesLevel(int level, int magnitude) { return level <= magnitude; }

    inline float activationReach(float base, float telekinesis, bool allowed)
    { return base + (allowed ? telekinesis * 22.f : 0.f); }

    // Preserve stock inventory order for equal-capacity gems.
    template<class Iterator, class Capacity, class Empty>
    Iterator smallestSoulGem(Iterator first, Iterator last, int soulValue, Capacity capacity, Empty empty)
    {
        auto selected = last;
        float best = std::numeric_limits<float>::max();
        if (soulValue <= 0) return selected;
        for (auto it = first; it != last; ++it)
        {
            const float value = capacity(*it);
            if (empty(*it) && value >= soulValue && value < best)
            { selected = it; best = value; }
        }
        return selected;
    }
    inline MWWorld::ContainerStoreIterator soulGem(MWWorld::ContainerStore& store, int value, float multiplier)
    {
        return smallestSoulGem(store.begin(MWWorld::ContainerStore::Type_Miscellaneous), store.end(), value,
            [multiplier](const auto& item) {
                return item.getClass().isSoulGem(item)
                    ? item.template get<ESM::Miscellaneous>()->mBase->mData.mValue * multiplier : -1.f;
            }, [](const auto& item) { return item.getCellRef().getSoul().empty(); });
    }
    inline bool captureSoul(MWWorld::ContainerStore& store, ESM::RefId soul, int value,
        float multiplier, const MWWorld::ContainerStoreStackContext* context = nullptr)
    {
        auto gem = soulGem(store, value, multiplier);
        if (gem == store.end()) return false;
        if (context) store.unstack(*gem, 1, *context);
        else store.unstack(*gem);
        gem->getCellRef().setSoul(soul);
        if (context) store.restack(*gem, *context);
        else store.restack(*gem);
        return true;
    }
}
#endif
