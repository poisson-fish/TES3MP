#ifndef OPENMW_MWMECHANICS_BOUNDEQUIPMENT_HPP
#define OPENMW_MWMECHANICS_BOUNDEQUIPMENT_HPP

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include <components/esm3/loadmgef.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <span>
#include <string_view>

namespace MWMechanics
{
    // Effect-owned identities, not record-owned inventory. A record can also be
    // carried ordinarily, or be produced by several overlapping spell sources.
    struct BoundEquipmentItem
    {
        ESM::RefNum item, previous;
        ESM::RefId previousRecord;
        int slot = -1;
        bool operator==(const BoundEquipmentItem&) const = default;
    };

    inline std::span<const std::string_view> boundEquipmentSettings(ESM::RefId effect)
    {
        static constexpr std::array<std::string_view, 2> gloves{"sMagicBoundRightGauntletID", "sMagicBoundLeftGauntletID"};
        // Keep the stock GMST lookup, including mods which replace bound records.
        static const std::array effects{ESM::MagicEffect::BoundBattleAxe, ESM::MagicEffect::BoundBoots,
            ESM::MagicEffect::BoundCuirass, ESM::MagicEffect::BoundDagger, ESM::MagicEffect::BoundHelm,
            ESM::MagicEffect::BoundLongbow, ESM::MagicEffect::BoundLongsword, ESM::MagicEffect::BoundMace,
            ESM::MagicEffect::BoundShield, ESM::MagicEffect::BoundSpear};
        static constexpr std::array<std::string_view, 10> settings{"sMagicBoundBattleAxeID", "sMagicBoundBootsID",
            "sMagicBoundCuirassID", "sMagicBoundDaggerID", "sMagicBoundHelmID", "sMagicBoundLongbowID",
            "sMagicBoundLongswordID", "sMagicBoundMaceID", "sMagicBoundShieldID", "sMagicBoundSpearID"};
        if (effect == ESM::MagicEffect::BoundGloves) return gloves;
        const auto found = std::find(effects.begin(), effects.end(), effect);
        return found == effects.end() ? std::span<const std::string_view>{}
            : std::span<const std::string_view>{&settings[found - effects.begin()], 1};
    }

    inline bool equipmentMagicEffect(ESM::RefId effect)
    { return effect == ESM::MagicEffect::ExtraSpell || !boundEquipmentSettings(effect).empty(); }

    struct BoundEquipmentContext
    {
        MWWorld::InventoryStore& inventory;
        bool player, dead, werewolf;
        std::function<MWWorld::Ptr(ESM::RefId)> add;
        std::function<void(const MWWorld::Ptr&)> equip, remove;
        std::function<MWWorld::Ptr(const BoundEquipmentItem&)> replacement;
        std::function<void()> autoEquip, drawWeapon;
    };

    inline bool addBoundEquipment(ESM::RefId record, BoundEquipmentItem& state,
        const BoundEquipmentContext& context)
    {
        auto& store = context.inventory;
        const auto item = context.add(record);
        state.item = item.getCellRef().getRefNum();
        const auto slots = item.getClass().getEquipmentSlots(item).first;
        state.slot = slots.empty() ? -1 : slots.front();
        const auto previous = state.slot >= 0 ? store.getSlot(state.slot) : store.end();
        if (previous != store.end())
        {
            state.previous = previous->getCellRef().getRefNum();
            state.previousRecord = previous->getCellRef().getRefId();
        }
        context.equip(item);
        const auto current = state.slot >= 0 ? store.getSlot(state.slot) : store.end();
        const bool equipped = current != store.end() && *current == item;
        if (equipped && context.player && state.slot == MWWorld::InventoryStore::Slot_CarriedRight)
            context.drawWeapon();
        return equipped;
    }

    inline void removeBoundEquipment(const BoundEquipmentItem& state, const BoundEquipmentContext& context)
    {
        auto& store = context.inventory;
        const auto item = std::find_if(store.begin(), store.end(), [&](const auto& value) {
            return value.getCellRef().getRefNum() == state.item;
        });
        if (item == store.end()) return;
        const auto current = state.slot >= 0 ? store.getSlot(state.slot) : store.end();
        const bool equipped = current != store.end() && *current == *item;
        context.remove(*item);
        // Manual changes win. Dead bodies and transformed actors cannot restore.
        if (!equipped || context.dead || context.werewolf) return;
        if (!context.player) { context.autoEquip(); return; }
        if (state.previousRecord.empty()) return;
        const auto previous = context.replacement(state);
        if (!previous.isEmpty()) context.equip(previous);
    }

    // Stock ExtraSpell removes armor/clothing from non-player inventories. It
    // retains weapons, ammunition, pants, and a non-armor carried-left item.
    template<class Unequip>
    void applyExtraSpell(MWWorld::InventoryStore& store, bool player, Unequip&& unequip)
    {
        if (player) return;
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            if (slot == MWWorld::InventoryStore::Slot_Ammunition
                || slot == MWWorld::InventoryStore::Slot_CarriedRight
                || slot == MWWorld::InventoryStore::Slot_Pants) continue;
            const auto item = store.getSlot(slot);
            if (slot == MWWorld::InventoryStore::Slot_CarriedLeft
                && (item == store.end() || item.getType() != MWWorld::ContainerStore::Type_Armor)) continue;
            unequip(slot);
        }
    }
}
#endif
