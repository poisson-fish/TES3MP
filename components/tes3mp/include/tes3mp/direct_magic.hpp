#ifndef TES3MP_DIRECT_MAGIC_HPP
#define TES3MP_DIRECT_MAGIC_HPP

#include "actor_catalog.hpp"
#include "deterministic_random.hpp"
#include "interactive_object_catalog.hpp"
#include "inventory_world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumDirectMagicEffectsPerSource = 8;
    inline constexpr std::size_t MaximumDirectMagicSpells = 4096;
    inline constexpr std::size_t MaximumDirectMagicDiseasesPerActor = 16;
    inline constexpr std::size_t MaximumContractedDiseasesPerPlayer = 64;
    inline constexpr std::size_t MaximumDirectMagicTraps = MaximumInteractiveObjectCatalogEntries;

    enum class DirectMagicTarget : std::uint8_t
    {
        Self = 0,
        Other = 1,
    };

    enum class DirectMagicEffectKind : std::uint8_t
    {
        FireDamage = 0,
        ShockDamage = 1,
        FrostDamage = 2,
        PoisonDamage = 3,
        DamageHealth = 4,
        DamageFatigue = 5,
        DamageMagicka = 6,
        RestoreHealth = 7,
        RestoreFatigue = 8,
        RestoreMagicka = 9,
    };

    enum class DirectMagicSchool : std::uint8_t
    {
        Alteration = 0,
        Conjuration = 1,
        Destruction = 2,
        Illusion = 3,
        Mysticism = 4,
        Restoration = 5,
        Count = 6,
    };

    enum class DirectMagicEnchantmentKind : std::uint8_t
    {
        OnStrike = 0,
        ConstantEffect = 1,
        WhenUsed = 2,
    };

    enum class DirectDiseaseKind : std::uint8_t
    {
        Common = 0,
        Blight = 1,
    };

    struct DirectMagicEffectProfile
    {
        DirectMagicTarget target = DirectMagicTarget::Other;
        DirectMagicEffectKind kind = DirectMagicEffectKind::DamageHealth;
        float minimumMagnitude = 0.f;
        float maximumMagnitude = 0.f;

        friend constexpr bool operator==(DirectMagicEffectProfile, DirectMagicEffectProfile) noexcept = default;
    };

    struct DirectMagicDefense
    {
        float willpower = 0.f;
        float destructionSkill = 0.f;
        float fireResistance = 0.f;
        float shockResistance = 0.f;
        float frostResistance = 0.f;
        float poisonResistance = 0.f;
        float commonDiseaseResistance = 0.f;
        float blightDiseaseResistance = 0.f;
        float fireShield = 0.f;
        float shockShield = 0.f;
        float frostShield = 0.f;

        friend constexpr bool operator==(DirectMagicDefense, DirectMagicDefense) noexcept = default;
    };

    struct DirectMagicSettings
    {
        float elementalShieldMultiplier = 0.f;
        float diseaseTransferChance = 0.f;

        friend constexpr bool operator==(DirectMagicSettings, DirectMagicSettings) noexcept = default;
    };

    struct DirectEnchantmentProfile
    {
        ItemPrototypeId prototypeId;
        DirectMagicEnchantmentKind kind = DirectMagicEnchantmentKind::OnStrike;
        std::uint32_t chargeCost = 0;
        std::vector<DirectMagicEffectProfile> effects;

        friend bool operator==(const DirectEnchantmentProfile&, const DirectEnchantmentProfile&) noexcept = default;
    };

    struct DirectSpellProfile
    {
        SpellRecordId spellId;
        DirectMagicSchool school = DirectMagicSchool::Alteration;
        std::uint32_t magickaCost = 0;
        float effectDifficulty = 0.f;
        bool alwaysSucceeds = false;
        std::vector<DirectMagicEffectProfile> effects;

        friend bool operator==(const DirectSpellProfile&, const DirectSpellProfile&) noexcept = default;
    };

    struct DirectEquipmentMagicProfile
    {
        ItemPrototypeId prototypeId;
        DirectMagicDefense defense;

        friend constexpr bool operator==(DirectEquipmentMagicProfile, DirectEquipmentMagicProfile) noexcept = default;
    };

    struct DirectDiseaseProfile
    {
        SpellRecordId spellId;
        DirectDiseaseKind kind = DirectDiseaseKind::Common;
        std::vector<DirectMagicEffectProfile> effects;

        friend bool operator==(const DirectDiseaseProfile&, const DirectDiseaseProfile&) noexcept = default;
    };

    struct DirectActorMagicProfile
    {
        ActorId actorId;
        DirectMagicDefense defense;
        std::vector<DirectDiseaseProfile> diseases;

        friend bool operator==(const DirectActorMagicProfile&, const DirectActorMagicProfile&) noexcept = default;
    };

    struct DirectTrapMagicProfile
    {
        TrapPrototypeId trapId;
        std::vector<DirectMagicEffectProfile> effects;

        friend bool operator==(const DirectTrapMagicProfile&, const DirectTrapMagicProfile&) noexcept = default;
    };

    class DirectMagicCatalog
    {
    public:
        static std::optional<DirectMagicCatalog> create(ContentManifestId manifest, const ItemPrototypeCatalog& items,
            DirectMagicSettings settings, std::span<const DirectEnchantmentProfile> enchantments,
            std::span<const DirectEquipmentMagicProfile> equipment, std::span<const DirectActorMagicProfile> actors,
            std::span<const DirectTrapMagicProfile> traps = {},
            std::span<const DirectSpellProfile> spells = {}) noexcept;

        constexpr ContentManifestId contentManifestId() const noexcept { return mManifest; }
        constexpr DirectMagicSettings settings() const noexcept { return mSettings; }
        const DirectEnchantmentProfile* findEnchantment(ItemPrototypeId id) const noexcept;
        const DirectEquipmentMagicProfile* findEquipment(ItemPrototypeId id) const noexcept;
        const DirectActorMagicProfile* findActor(ActorId id) const noexcept;
        const DirectTrapMagicProfile* findTrap(TrapPrototypeId id) const noexcept;
        const DirectSpellProfile* findSpell(SpellRecordId id) const noexcept;
        std::span<const DirectEnchantmentProfile> enchantments() const noexcept { return mEnchantments; }
        std::span<const DirectEquipmentMagicProfile> equipment() const noexcept { return mEquipment; }
        std::span<const DirectActorMagicProfile> actors() const noexcept { return mActors; }
        std::span<const DirectTrapMagicProfile> traps() const noexcept { return mTraps; }
        std::span<const DirectSpellProfile> spells() const noexcept { return mSpells; }

        friend bool operator==(const DirectMagicCatalog&, const DirectMagicCatalog&) noexcept = default;

    private:
        DirectMagicCatalog(ContentManifestId manifest, DirectMagicSettings settings,
            std::vector<DirectEnchantmentProfile> enchantments, std::vector<DirectEquipmentMagicProfile> equipment,
            std::vector<DirectActorMagicProfile> actors, std::vector<DirectTrapMagicProfile> traps,
            std::vector<DirectSpellProfile> spells) noexcept
            : mManifest(manifest)
            , mSettings(settings)
            , mEnchantments(std::move(enchantments))
            , mEquipment(std::move(equipment))
            , mActors(std::move(actors))
            , mTraps(std::move(traps))
            , mSpells(std::move(spells))
        {
        }

        ContentManifestId mManifest;
        DirectMagicSettings mSettings;
        std::vector<DirectEnchantmentProfile> mEnchantments;
        std::vector<DirectEquipmentMagicProfile> mEquipment;
        std::vector<DirectActorMagicProfile> mActors;
        std::vector<DirectTrapMagicProfile> mTraps;
        std::vector<DirectSpellProfile> mSpells;
    };

    struct DirectMagicResolution
    {
        float healthDamage = 0.f;
        float fatigueDamage = 0.f;
        float magickaDamage = 0.f;
        float healthRestore = 0.f;
        float fatigueRestore = 0.f;
        float magickaRestore = 0.f;

        friend constexpr bool operator==(DirectMagicResolution, DirectMagicResolution) noexcept = default;
    };

    bool validDirectMagicDefense(const DirectMagicDefense& value) noexcept;
    DirectMagicDefense combinedDirectMagicDefense(const DirectMagicDefense& base,
        const CanonicalPlayerInventoryState* inventory, const DirectMagicCatalog& catalog) noexcept;
    std::optional<DirectMagicResolution> resolveDirectMagicEffects(std::span<const DirectMagicEffectProfile> effects,
        DirectMagicTarget target, const DirectMagicDefense& defense, Xoshiro256StarStar& random) noexcept;
    std::optional<float> resolveElementalShieldDamage(const DirectMagicDefense& shieldOwner,
        const DirectMagicDefense& attackerDefense, float attackerLuck, float attackerFatigue,
        float attackerMaximumFatigue, DirectMagicSettings settings, Xoshiro256StarStar& random) noexcept;
    std::optional<bool> resolveDiseaseTransfer(DirectDiseaseKind kind, const DirectMagicDefense& target,
        DirectMagicSettings settings, Xoshiro256StarStar& random) noexcept;
    bool directMagicCoversInteractiveObjectTraps(
        const DirectMagicCatalog& magic, const InteractiveObjectCatalog& objects) noexcept;
}

#endif
