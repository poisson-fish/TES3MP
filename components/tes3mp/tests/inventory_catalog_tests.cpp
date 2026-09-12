#include <tes3mp/content_identity.hpp>
#include <tes3mp/item_catalog.hpp>

#include <cassert>
#include <array>
#include <cstdlib>
#include <iostream>

using namespace TES3MP;

namespace
{
    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "inventory_catalog_tests assertion failed at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    void testValidCatalogCreationAndLookup()
    {
        const auto manifest = testContentManifest();

        const std::vector<ItemPrototypeDeclaration> decls = {
            ItemPrototypeDeclaration{
                .id = *ItemPrototypeId::fromValue(1),
                .category = ItemCategory::Miscellaneous,
                .weightUnits = 1,
                .value = 1,
                .slotMask = 0,
                .stackable = true,
                .keyId = std::nullopt,
            },
            ItemPrototypeDeclaration{
                .id = *ItemPrototypeId::fromValue(2),
                .category = ItemCategory::Armor,
                .weightUnits = 50,
                .value = 100,
                .maxCondition = 200,
                .slotMask = slotToMask(EquipmentSlot::Cuirass),
                .stackable = false,
                .keyId = std::nullopt,
            },
            ItemPrototypeDeclaration{
                .id = *ItemPrototypeId::fromValue(3),
                .category = ItemCategory::Miscellaneous,
                .weightUnits = 2,
                .value = 5,
                .slotMask = 0,
                .stackable = false,
                .keyId = *KeyPrototypeId::fromValue(99),
            },
            ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(4),
                .category = ItemCategory::Lockpick,
                .weightUnits = 1,
                .value = 5,
                .maxCondition = 10,
                .stackable = false,
                .toolQuality = 1.25f },
            ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(5),
                .category = ItemCategory::Probe,
                .weightUnits = 1,
                .value = 5,
                .maxCondition = 10,
                .stackable = false,
                .toolQuality = 0.75f },
        };

        const auto catalog = ItemPrototypeCatalog::create(manifest, decls);
        assert(catalog.has_value());
        assert(catalog->contentManifestId() == manifest.id());
        assert(catalog->declarations().size() == 5);

        const auto* item1 = catalog->find(*ItemPrototypeId::fromValue(1));
        assert(item1 != nullptr);
        assert(item1->category == ItemCategory::Miscellaneous);
        assert(item1->stackable);

        const auto* item2 = catalog->find(*ItemPrototypeId::fromValue(2));
        assert(item2 != nullptr);
        assert(item2->category == ItemCategory::Armor);
        assert(!item2->stackable);
        assert(item2->slotMask == slotToMask(EquipmentSlot::Cuirass));

        const auto* item3 = catalog->find(*ItemPrototypeId::fromValue(3));
        assert(item3 != nullptr);
        assert(item3->keyId.has_value());
        assert(*item3->keyId == *KeyPrototypeId::fromValue(99));

        assert(catalog->find(*ItemPrototypeId::fromValue(999)) == nullptr);
    }

    void testDuplicateItemIdsRejected()
    {
        const auto manifest = testContentManifest();
        const std::vector<ItemPrototypeDeclaration> decls = {
            ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(1) },
            ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(1) },
        };
        const auto catalog = ItemPrototypeCatalog::create(manifest, decls);
        assert(!catalog.has_value());
    }

    void testZeroItemIdRejected()
    {
        assert(!ItemPrototypeId::fromValue(0).has_value());
    }

    void testInvalidSlotMaskRejected()
    {
        const auto manifest = testContentManifest();
        const std::vector<ItemPrototypeDeclaration> decls = {
            ItemPrototypeDeclaration{
                .id = *ItemPrototypeId::fromValue(1),
                .slotMask = 1u << 31, // Invalid slot mask beyond 19 slots
            },
        };
        const auto catalog = ItemPrototypeCatalog::create(manifest, decls);
        assert(!catalog.has_value());
    }

    void testInvalidCategoryRejected()
    {
        const auto manifest = testContentManifest();
        const std::vector<ItemPrototypeDeclaration> declarations = {
            ItemPrototypeDeclaration{
                .id = *ItemPrototypeId::fromValue(1),
                .category = static_cast<ItemCategory>(255),
            },
        };
        assert(!ItemPrototypeCatalog::create(manifest, declarations));
    }

    void testSecurityToolQualityIsManifestBound()
    {
        const auto manifest = testContentManifest();
        const std::array missingQuality{ ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(1),
            .category = ItemCategory::Lockpick,
            .maxCondition = 10 } };
        const std::array qualityOnWeapon{ ItemPrototypeDeclaration{ .id = *ItemPrototypeId::fromValue(2),
            .category = ItemCategory::Weapon,
            .maxCondition = 10,
            .toolQuality = 1.f } };
        assert(!ItemPrototypeCatalog::create(manifest, missingQuality));
        assert(!ItemPrototypeCatalog::create(manifest, qualityOnWeapon));
    }
}

int main()
{
    testValidCatalogCreationAndLookup();
    testDuplicateItemIdsRejected();
    testZeroItemIdRejected();
    testInvalidSlotMaskRejected();
    testInvalidCategoryRejected();
    testSecurityToolQualityIsManifestBound();

    std::cout << "All inventory catalog tests passed." << std::endl;
    return 0;
}
