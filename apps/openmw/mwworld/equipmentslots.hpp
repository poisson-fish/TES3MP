#ifndef OPENMW_MWWORLD_EQUIPMENTSLOTS_HPP
#define OPENMW_MWWORLD_EQUIPMENTSLOTS_HPP

#include "inventorystore.hpp"
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadweap.hpp>
#include "../mwmechanics/weapontype.hpp"

namespace MWWorld
{
    // Allocation-free stock slot rules, shared by Class and save preflight.
    struct EquipmentSlots
    {
        int mFirst = InventoryStore::Slot_NoSlot;
        int mSecond = InventoryStore::Slot_NoSlot;
        bool mStack = false;
        bool contains(int slot) const { return slot >= 0 && (slot == mFirst || slot == mSecond); }
        std::pair<std::vector<int>, bool> asClassSlots() const
        {
            std::vector<int> slots;
            if (mFirst >= 0) slots.push_back(mFirst);
            if (mSecond >= 0) slots.push_back(mSecond);
            return {std::move(slots), mStack};
        }
    };

    inline EquipmentSlots equipmentSlots(const ESM::Clothing& base)
    {
        using I = InventoryStore;
        switch (base.mData.mType)
        {
            case ESM::Clothing::Shirt: return {I::Slot_Shirt};
            case ESM::Clothing::Belt: return {I::Slot_Belt};
            case ESM::Clothing::Robe: return {I::Slot_Robe};
            case ESM::Clothing::Pants: return {I::Slot_Pants};
            case ESM::Clothing::Shoes: return {I::Slot_Boots};
            case ESM::Clothing::LGlove: return {I::Slot_LeftGauntlet};
            case ESM::Clothing::RGlove: return {I::Slot_RightGauntlet};
            case ESM::Clothing::Skirt: return {I::Slot_Skirt};
            case ESM::Clothing::Amulet: return {I::Slot_Amulet};
            case ESM::Clothing::Ring: return {I::Slot_LeftRing, I::Slot_RightRing};
            default: return {};
        }
    }
    inline EquipmentSlots equipmentSlots(const ESM::Armor& base)
    {
        using I = InventoryStore;
        switch (base.mData.mType)
        {
            case ESM::Armor::Helmet: return {I::Slot_Helmet};
            case ESM::Armor::Cuirass: return {I::Slot_Cuirass};
            case ESM::Armor::Greaves: return {I::Slot_Greaves};
            case ESM::Armor::LPauldron: return {I::Slot_LeftPauldron};
            case ESM::Armor::RPauldron: return {I::Slot_RightPauldron};
            case ESM::Armor::LGauntlet: case ESM::Armor::LBracer: return {I::Slot_LeftGauntlet};
            case ESM::Armor::RGauntlet: case ESM::Armor::RBracer: return {I::Slot_RightGauntlet};
            case ESM::Armor::Boots: return {I::Slot_Boots};
            case ESM::Armor::Shield: return {I::Slot_CarriedLeft};
            default: return {};
        }
    }
    inline EquipmentSlots equipmentSlots(const ESM::Weapon& base)
    {
        const auto type = MWMechanics::getWeaponType(base.mData.mType)->mWeaponClass;
        return {type == ESM::WeaponType::Ammo ? InventoryStore::Slot_Ammunition : InventoryStore::Slot_CarriedRight,
            InventoryStore::Slot_NoSlot, type == ESM::WeaponType::Ammo || type == ESM::WeaponType::Thrown};
    }
    template<class T> EquipmentSlots equipmentSlots(const T&) { return {}; }
}
#endif
