#ifndef OPENMW_MWWORLD_INVENTORYITEM_HPP
#define OPENMW_MWWORLD_INVENTORYITEM_HPP

#include "containerstore.hpp"
#include "equipmentslots.hpp"
#include "esmstore.hpp"

namespace MWWorld
{
    // Shared dispatch for the TES3 types accepted by ContainerStore. No manual
    // gameplay catalog and no network types enter the engine.
    template<class F> decltype(auto) visitInventoryRecord(const ESMStore& store, ESM::RefId id, F&& f)
    {
        switch (store.find(id))
        {
#define INVENTORY_CASE(T) case ESM::T::sRecordId: return f(*store.get<ESM::T>().find(id));
            INVENTORY_CASE(Potion) INVENTORY_CASE(Apparatus) INVENTORY_CASE(Armor)
            INVENTORY_CASE(Book) INVENTORY_CASE(Clothing) INVENTORY_CASE(Ingredient)
            INVENTORY_CASE(Light) INVENTORY_CASE(Lockpick) INVENTORY_CASE(Miscellaneous)
            INVENTORY_CASE(Probe) INVENTORY_CASE(Repair) INVENTORY_CASE(Weapon)
#undef INVENTORY_CASE
            default: throw std::invalid_argument("Record is not a supported inventory item");
        }
    }
    struct InventoryItemRecord
    {
        unsigned int mType;
        const void* mBase;
        ESM::RefId mScript, mEnchant;
        EquipmentSlots mSlots;
    };
    inline InventoryItemRecord inventoryItemRecord(const ESMStore& store, ESM::RefId id)
    {
        return visitInventoryRecord(store, id, [](const auto& base) {
            using T = std::remove_cvref_t<decltype(base)>;
            InventoryItemRecord result{T::sRecordId, &base, base.mScript, {}, equipmentSlots(base)};
            if constexpr (requires { base.mEnchant; }) result.mEnchant = base.mEnchant;
            return result;
        });
    }

    inline void validateEquipmentItemSlots(const InventoryItemRecord& record, ESM::RefNum identity, int64_t count,
        const std::array<ESM::RefNum, InventoryStore::Slots>& slots, bool npcStats)
    {
        bool found = false;
        for (int slot = 0; slot < InventoryStore::Slots; ++slot)
            if (identity == slots[slot])
            {
                if (found || !record.mSlots.contains(slot) || count == 0 || (!record.mSlots.mStack && std::abs(count) != 1)
                    || ((!record.mEnchant.empty() || !record.mScript.empty())
                        && (slot != InventoryStore::Slot_Shirt || !npcStats)))
                    throw std::invalid_argument("Invalid equipment slot, count, duplicate item or unavailable effects");
                found = true;
            }
    }
}
#endif
