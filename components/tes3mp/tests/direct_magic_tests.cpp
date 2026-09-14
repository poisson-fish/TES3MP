#include <tes3mp/content_identity.hpp>
#include <tes3mp/direct_magic.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace TES3MP;

namespace
{
    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "direct_magic_tests assertion failed at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    ItemPrototypeCatalog items()
    {
        const std::array declarations{
            ItemPrototypeDeclaration{ id<ItemPrototypeId>(1), ItemCategory::Weapon, 10, 5, 100, 20,
                slotToMask(EquipmentSlot::CarriedRight), false, std::nullopt },
            ItemPrototypeDeclaration{ id<ItemPrototypeId>(2), ItemCategory::Armor, 10, 5, 100, 0,
                slotToMask(EquipmentSlot::Cuirass), false, std::nullopt },
        };
        return *ItemPrototypeCatalog::create(testContentManifest(), declarations);
    }

    void catalog_is_bounded_sorted_and_cross_checked()
    {
        const auto itemCatalog = items();
        const std::array enchantments{ DirectEnchantmentProfile{ id<ItemPrototypeId>(1),
            DirectMagicEnchantmentKind::OnStrike, 5,
            { { DirectMagicTarget::Other, DirectMagicEffectKind::FireDamage, 2.f, 4.f } } } };
        DirectMagicDefense armorDefense;
        armorDefense.fireResistance = 25.f;
        const std::array equipment{ DirectEquipmentMagicProfile{ id<ItemPrototypeId>(2), armorDefense } };
        DirectMagicDefense actorDefense;
        actorDefense.willpower = 30.f;
        const std::array actors{ DirectActorMagicProfile{ id<ActorId>(3), actorDefense,
            { { id<SpellRecordId>(4), DirectDiseaseKind::Common,
                { { DirectMagicTarget::Other, DirectMagicEffectKind::DamageHealth, 1.f, 1.f } } } } } };
        const std::array traps{ DirectTrapMagicProfile{ id<TrapPrototypeId>(5),
            { { DirectMagicTarget::Other, DirectMagicEffectKind::FireDamage, 6.f, 6.f } } } };
        const auto catalog = DirectMagicCatalog::create(testContentManifestId(), itemCatalog,
            { .elementalShieldMultiplier = 0.1f, .diseaseTransferChance = 10.f }, enchantments, equipment, actors,
            traps);
        assert(catalog);
        assert(catalog->findEnchantment(id<ItemPrototypeId>(1)) != nullptr);
        assert(catalog->findEquipment(id<ItemPrototypeId>(2))->defense.fireResistance == 25.f);
        assert(catalog->findActor(id<ActorId>(3))->diseases[0].spellId == id<SpellRecordId>(4));
        assert(catalog->findTrap(id<TrapPrototypeId>(5))->effects[0].minimumMagnitude == 6.f);

        const auto zero = Turn32::fromValue(0);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const auto transform = Transform(cell, Position3(0, 0, 0), Orientation3(zero, zero, zero));
        const std::array objectEntries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, cell, transform, std::nullopt, {},
            ObjectTrapDeclaration{ true, id<TrapPrototypeId>(5), 10 } } };
        const auto objects = InteractiveObjectCatalog::create(testContentManifest(), objectEntries);
        assert(objects && directMagicCoversInteractiveObjectTraps(*catalog, *objects));

        auto invalidCharge = enchantments;
        invalidCharge[0].chargeCost = 21;
        assert(!DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, invalidCharge, {}, {}));
        auto invalidDisease = actors;
        invalidDisease[0].diseases[0].effects[0].target = DirectMagicTarget::Self;
        assert(!DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, {}, {}, invalidDisease));
        auto invalidEquipment = equipment;
        invalidEquipment[0].defense.willpower = 1.f;
        assert(!DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, {}, invalidEquipment, {}));
        auto invalidTrap = traps;
        invalidTrap[0].effects[0].target = DirectMagicTarget::Self;
        assert(!DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, {}, {}, {}, invalidTrap));

        const std::array timedSpells{ DirectSpellProfile{ id<SpellRecordId>(8), DirectMagicSchool::Destruction, 10,
            5.f, true,
            { { DirectMagicTarget::Other, DirectMagicEffectKind::FireDamage, 2.f, 4.f, 126, 10240,
                DirectMagicStacking::Refresh } } } };
        assert(DirectMagicCatalog::create(
            testContentManifestId(), itemCatalog, {}, {}, {}, {}, {}, timedSpells));
        auto invalidDuration = timedSpells;
        invalidDuration[0].effects[0].durationTicks = MaximumDirectMagicDurationTicks + 1;
        assert(!DirectMagicCatalog::create(
            testContentManifestId(), itemCatalog, {}, {}, {}, {}, {}, invalidDuration));
        auto invalidDispel = timedSpells;
        invalidDispel[0].effects[0].kind = DirectMagicEffectKind::Dispel;
        assert(!DirectMagicCatalog::create(
            testContentManifestId(), itemCatalog, {}, {}, {}, {}, {}, invalidDispel));
        auto invalidStrike = enchantments;
        invalidStrike[0].effects[0].durationTicks = 1;
        assert(!DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, invalidStrike, {}, {}));
    }

    void direct_effects_shields_and_disease_use_server_randomness()
    {
        const auto key = *RandomStreamKey::fromValues(1, 2);
        auto random = Xoshiro256StarStar::fromWorldSeed(3, key);
        DirectMagicDefense target;
        target.fireResistance = 50.f;
        const std::array effects{
            DirectMagicEffectProfile{ DirectMagicTarget::Other, DirectMagicEffectKind::FireDamage, 10.f, 10.f },
            DirectMagicEffectProfile{ DirectMagicTarget::Other, DirectMagicEffectKind::DamageFatigue, 3.f, 3.f },
            DirectMagicEffectProfile{ DirectMagicTarget::Self, DirectMagicEffectKind::DamageHealth, 100.f, 100.f },
        };
        const auto resolved = resolveDirectMagicEffects(effects, DirectMagicTarget::Other, target, random);
        assert(resolved && resolved->healthDamage == 5.f && resolved->fatigueDamage == 3.f);

        DirectMagicDefense shield;
        shield.fireShield = 20.f;
        DirectMagicDefense attacker;
        const auto shieldDamage = resolveElementalShieldDamage(
            shield, attacker, 0.f, 0.f, 10.f, { .elementalShieldMultiplier = 0.1f }, random);
        assert(shieldDamage && std::abs(*shieldDamage - 2.f) < 0.0001f);

        const auto contracted
            = resolveDiseaseTransfer(DirectDiseaseKind::Common, attacker, { .diseaseTransferChance = 100.f }, random);
        assert(contracted && *contracted);
        attacker.commonDiseaseResistance = 100.f;
        const auto resisted
            = resolveDiseaseTransfer(DirectDiseaseKind::Common, attacker, { .diseaseTransferChance = 100.f }, random);
        assert(resisted && !*resisted);
    }

    void equipped_passive_defenses_are_composed_once()
    {
        const auto itemCatalog = items();
        DirectMagicDefense armorDefense;
        armorDefense.fireResistance = 25.f;
        armorDefense.fireShield = 10.f;
        const std::array equipment{ DirectEquipmentMagicProfile{ id<ItemPrototypeId>(2), armorDefense } };
        const auto catalog = *DirectMagicCatalog::create(testContentManifestId(), itemCatalog, {}, {}, equipment, {});
        CanonicalPlayerInventoryState player{ .player = id<PlayerId>(1),
            .stacks = { { id<ItemStackId>(5), id<ItemPrototypeId>(2), 1, 100, 0, std::nullopt } } };
        player.equipment[static_cast<std::size_t>(EquipmentSlot::Cuirass)] = id<ItemStackId>(5);
        const auto inventory
            = *CanonicalInventoryWorld::create(testContentManifest(), itemCatalog, std::array{ player }, {});
        DirectMagicDefense base;
        base.fireResistance = 5.f;
        const auto combined = combinedDirectMagicDefense(base, inventory.findPlayer(id<PlayerId>(1)), catalog);
        assert(combined.fireResistance == 30.f && combined.fireShield == 10.f);
        assert(base.fireResistance == 5.f);
    }
}

int main()
{
    catalog_is_bounded_sorted_and_cross_checked();
    direct_effects_shields_and_disease_use_server_randomness();
    equipped_passive_defenses_are_composed_once();
}
