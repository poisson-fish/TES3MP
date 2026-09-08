#include <tes3mp/item_catalog.hpp>

#include <algorithm>

namespace TES3MP
{
    std::optional<ItemPrototypeCatalog> ItemPrototypeCatalog::create(
        const ContentManifest& manifest, std::span<const ItemPrototypeDeclaration> declarations) noexcept
    try
    {
        if (declarations.size() > MaximumItemPrototypes)
            return std::nullopt;

        std::vector<ItemPrototypeDeclaration> sorted(declarations.begin(), declarations.end());
        std::ranges::sort(sorted, {}, &ItemPrototypeDeclaration::id);

        constexpr std::uint32_t validSlotMask = (1u << static_cast<std::uint8_t>(EquipmentSlot::Count)) - 1u;

        for (std::size_t index = 0; index < sorted.size(); ++index)
        {
            const auto& decl = sorted[index];

            if (decl.id.value() == 0)
                return std::nullopt;

            if (index != 0 && sorted[index - 1].id == decl.id)
                return std::nullopt;

            if (decl.category < ItemCategory::Potion || decl.category > ItemCategory::Weapon)
                return std::nullopt;

            if ((decl.slotMask & ~validSlotMask) != 0)
                return std::nullopt;

            if (decl.keyId.has_value() && decl.keyId->value() == 0)
                return std::nullopt;
        }

        return ItemPrototypeCatalog(manifest.id(), std::move(sorted));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const ItemPrototypeDeclaration* ItemPrototypeCatalog::find(ItemPrototypeId id) const noexcept
    {
        const auto it = std::ranges::lower_bound(mDeclarations, id, {}, &ItemPrototypeDeclaration::id);
        if (it != mDeclarations.end() && it->id == id)
            return &(*it);
        return nullptr;
    }
}
