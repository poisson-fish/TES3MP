#ifndef OPENMW_MWWORLD_INVENTORYRECORDID_HPP
#define OPENMW_MWWORLD_INVENTORYRECORDID_HPP
#include "inventoryitem.hpp"
#include <components/esm3/loadcrea.hpp>
#include <map>

namespace MWWorld
{
    // Versioned record identity derived from the case-insensitive TES3 record
    // name. Both endpoints use the same loaded records; collisions fail startup.
    inline uint64_t inventoryRecordId(ESM::RefId id)
    {
        if (!id.is<ESM::StringRefId>()) throw std::invalid_argument("Native item requires a TES3 record ID");
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : id.getRefIdString())
        {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            hash = (hash ^ c) * 1099511628211ull;
        }
        return (hash & 0x3fffffffffffffffull) | 0x8000000000000000ull;
    }
    using InventoryRecordMap = std::map<uint64_t, ESM::RefId>;
    inline InventoryRecordMap inventoryRecords(const ESMStore& store)
    {
        InventoryRecordMap result;
        const auto add = [&]<class T>() {
            for (const auto& record : store.get<T>())
            {
                if (result.size() >= 262144 || !result.emplace(inventoryRecordId(record.mId), record.mId).second)
                    throw std::invalid_argument("Native inventory record budget or identity collision");
            }
        };
        add.operator()<ESM::Potion>(); add.operator()<ESM::Apparatus>(); add.operator()<ESM::Armor>();
        add.operator()<ESM::Book>(); add.operator()<ESM::Clothing>(); add.operator()<ESM::Ingredient>();
        add.operator()<ESM::Light>(); add.operator()<ESM::Lockpick>(); add.operator()<ESM::Miscellaneous>();
        add.operator()<ESM::Probe>(); add.operator()<ESM::Repair>(); add.operator()<ESM::Weapon>();
        return result;
    }
    inline InventoryRecordMap inventorySoulRecords(const ESMStore& store)
    {
        InventoryRecordMap result;
        for (const auto& record : store.get<ESM::Creature>())
            if (result.size() >= 262144 || !result.emplace(inventoryRecordId(record.mId), record.mId).second)
                throw std::invalid_argument("Native soul record budget or identity collision");
        return result;
    }
}
#endif
