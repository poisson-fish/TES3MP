#ifndef TES3MP_ITEM_CATALOG_HPP
#define TES3MP_ITEM_CATALOG_HPP

#include "content_identity.hpp"
#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumItemPrototypes = 65536;

    enum class ItemCategory : std::uint8_t
    {
        Potion = 0,
        Apparatus = 1,
        Armor = 2,
        Book = 3,
        Clothing = 4,
        Ingredient = 5,
        Light = 6,
        Lockpick = 7,
        Miscellaneous = 8,
        Probe = 9,
        Repair = 10,
        Weapon = 11,
    };

    enum class EquipmentSlot : std::uint8_t
    {
        Helmet = 0,
        Cuirass = 1,
        Greaves = 2,
        LeftPauldron = 3,
        RightPauldron = 4,
        LeftGauntlet = 5,
        RightGauntlet = 6,
        Boots = 7,
        Shirt = 8,
        Pants = 9,
        Skirt = 10,
        Robe = 11,
        LeftRing = 12,
        RightRing = 13,
        Amulet = 14,
        Belt = 15,
        CarriedRight = 16,
        CarriedLeft = 17,
        Ammunition = 18,
        Count = 19,
    };

    inline constexpr std::uint32_t slotToMask(EquipmentSlot slot) noexcept
    {
        const auto val = static_cast<std::uint8_t>(slot);
        if (val >= static_cast<std::uint8_t>(EquipmentSlot::Count))
            return 0;
        return 1u << val;
    }

    struct ItemPrototypeDeclaration
    {
        ItemPrototypeId id;
        ItemCategory category = ItemCategory::Miscellaneous;
        std::uint32_t weightUnits = 0;
        std::uint32_t value = 0;
        std::uint32_t maxCondition = 0;
        std::uint32_t maxEnchantmentCharge = 0;
        std::uint32_t slotMask = 0;
        bool stackable = true;
        std::optional<KeyPrototypeId> keyId = std::nullopt;

        friend bool operator==(const ItemPrototypeDeclaration&, const ItemPrototypeDeclaration&) noexcept = default;
    };

    class ItemPrototypeCatalog
    {
    public:
        static std::optional<ItemPrototypeCatalog> create(
            const ContentManifest& manifest, std::span<const ItemPrototypeDeclaration> declarations) noexcept;

        constexpr ContentManifestId contentManifestId() const noexcept { return mContentManifestId; }
        std::span<const ItemPrototypeDeclaration> declarations() const noexcept { return mDeclarations; }
        const ItemPrototypeDeclaration* find(ItemPrototypeId id) const noexcept;

        friend bool operator==(const ItemPrototypeCatalog&, const ItemPrototypeCatalog&) noexcept = default;

    private:
        ItemPrototypeCatalog(
            ContentManifestId contentManifestId, std::vector<ItemPrototypeDeclaration> declarations) noexcept
            : mContentManifestId(contentManifestId)
            , mDeclarations(std::move(declarations))
        {
        }

        ContentManifestId mContentManifestId;
        std::vector<ItemPrototypeDeclaration> mDeclarations;
    };
}

#endif
