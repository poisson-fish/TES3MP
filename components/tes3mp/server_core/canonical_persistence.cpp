#include <tes3mp/canonical_persistence.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

namespace
{
    using namespace TES3MP;

    class Writer
    {
    public:
        template <class T>
        void fixed(T value)
        {
            using U = std::make_unsigned_t<T>;
            const U bits = static_cast<U>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
                mBytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8))));
        }
        void bytes(std::span<const std::byte> values) { mBytes.insert(mBytes.end(), values.begin(), values.end()); }
        std::vector<std::byte> take() { return std::move(mBytes); }
        const std::vector<std::byte>& view() const noexcept { return mBytes; }

    private:
        std::vector<std::byte> mBytes;
    };

    class Reader
    {
    public:
        explicit Reader(std::span<const std::byte> bytes) noexcept
            : mBytes(bytes)
        {
        }

        template <class T>
        std::optional<T> fixed() noexcept
        {
            using U = std::make_unsigned_t<T>;
            if (remaining() < sizeof(T))
                return std::nullopt;
            U value = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
                value |= static_cast<U>(std::to_integer<std::uint8_t>(mBytes[mOffset++])) << (index * 8);
            return static_cast<T>(value);
        }

        std::optional<std::span<const std::byte>> bytes(std::size_t count) noexcept
        {
            if (count > remaining())
                return std::nullopt;
            auto result = mBytes.subspan(mOffset, count);
            mOffset += count;
            return result;
        }
        std::size_t remaining() const noexcept { return mBytes.size() - mOffset; }
        std::size_t offset() const noexcept { return mOffset; }

    private:
        std::span<const std::byte> mBytes;
        std::size_t mOffset = 0;
    };

    void writeCell(Writer& writer, const CellId& cell)
    {
        writer.fixed(static_cast<std::uint8_t>(cell.kind()));
        if (const auto* interior = cell.asInterior())
        {
            writer.fixed(interior->cellSpace().value());
            writer.fixed(std::int32_t{ 0 });
            writer.fixed(std::int32_t{ 0 });
        }
        else
        {
            const auto& exterior = *cell.asExterior();
            writer.fixed(exterior.worldspace().value());
            writer.fixed(exterior.gridX());
            writer.fixed(exterior.gridY());
        }
    }

    void writePlayer(Writer& writer, const CanonicalPlayerEntityState& player)
    {
        writer.fixed(player.playerId().value());
        writer.fixed(player.entityId().value());
        writer.fixed(player.appearanceId().value());
        writeCell(writer, player.transform().cell());
        const auto position = player.transform().position();
        writer.fixed(position.x());
        writer.fixed(position.y());
        writer.fixed(position.z());
        const auto orientation = player.transform().orientation();
        writer.fixed(orientation.x().value());
        writer.fixed(orientation.y().value());
        writer.fixed(orientation.z().value());
        const auto velocity = player.linearVelocity();
        writer.fixed(velocity.x());
        writer.fixed(velocity.y());
        writer.fixed(velocity.z());
        writer.fixed(player.entityRevision().value());
        writer.fixed(player.authorityEpoch().value());
        writer.fixed(player.lastSpatialChangeTick().value());
        writer.fixed(static_cast<std::uint8_t>(player.locomotionMode()));
    }

    std::optional<CanonicalPlayerEntityState> readPlayer(Reader& reader) noexcept
    {
        const auto playerRaw = reader.fixed<std::uint64_t>();
        const auto entityRaw = reader.fixed<std::uint64_t>();
        const auto appearanceRaw = reader.fixed<std::uint64_t>();
        const auto kind = reader.fixed<std::uint8_t>();
        const auto spaceRaw = reader.fixed<std::uint64_t>();
        const auto gridX = reader.fixed<std::int32_t>();
        const auto gridY = reader.fixed<std::int32_t>();
        const auto x = reader.fixed<std::int64_t>();
        const auto y = reader.fixed<std::int64_t>();
        const auto z = reader.fixed<std::int64_t>();
        const auto ox = reader.fixed<std::uint32_t>();
        const auto oy = reader.fixed<std::uint32_t>();
        const auto oz = reader.fixed<std::uint32_t>();
        const auto vx = reader.fixed<std::int64_t>();
        const auto vy = reader.fixed<std::int64_t>();
        const auto vz = reader.fixed<std::int64_t>();
        const auto revisionRaw = reader.fixed<std::uint64_t>();
        const auto epochRaw = reader.fixed<std::uint64_t>();
        const auto tickRaw = reader.fixed<std::uint64_t>();
        const auto locomotion = reader.fixed<std::uint8_t>();
        if (!playerRaw || !entityRaw || !appearanceRaw || !kind || !spaceRaw || !gridX || !gridY || !x || !y || !z
            || !ox || !oy || !oz || !vx || !vy || !vz || !revisionRaw || !epochRaw || !tickRaw || !locomotion
            || *kind > 1 || *locomotion > static_cast<std::uint8_t>(LocomotionMode::Jump))
            return std::nullopt;
        const auto player = PlayerId::fromValue(*playerRaw);
        const auto entity = EntityId::fromValue(*entityRaw);
        const auto appearance = AppearanceId::fromValue(*appearanceRaw);
        const auto space = CellSpaceId::fromValue(*spaceRaw);
        const auto revision = EntityRevision::fromValue(*revisionRaw);
        const auto epoch = AuthorityEpoch::fromValue(*epochRaw);
        const auto tick = ServerTick::fromValue(*tickRaw);
        if (!player || !entity || !appearance || !space || !revision || !epoch || !tick)
            return std::nullopt;
        const CellId cell = *kind == 0 ? CellId::interior(*space) : CellId::exterior(*space, *gridX, *gridY);
        return CanonicalPlayerEntityState(*player, *entity, *appearance,
            Transform(cell, Position3(*x, *y, *z),
                Orientation3(Turn32::fromValue(*ox), Turn32::fromValue(*oy), Turn32::fromValue(*oz))),
            LinearVelocity3(*vx, *vy, *vz), *revision, *epoch, *tick, static_cast<LocomotionMode>(*locomotion));
    }

    template <class T>
    void writeStrong(Writer& writer, T value)
    {
        writer.fixed(value.value());
    }

    template <class T>
    std::optional<T> readStrong(Reader& reader) noexcept
    {
        const auto value = reader.fixed<std::uint64_t>();
        return value ? T::fromValue(*value) : std::nullopt;
    }

    template <class T>
    void writeOptionalStrong(Writer& writer, const std::optional<T>& value)
    {
        writer.fixed(static_cast<std::uint8_t>(value.has_value()));
        if (value)
            writeStrong(writer, *value);
    }

    template <class T>
    bool readOptionalStrong(Reader& reader, std::optional<T>& result) noexcept
    {
        const auto present = reader.fixed<std::uint8_t>();
        if (!present || *present > 1)
            return false;
        if (!*present)
        {
            result.reset();
            return true;
        }
        auto value = readStrong<T>(reader);
        if (!value)
            return false;
        result = *value;
        return true;
    }

    void writeFloat(Writer& writer, float value)
    {
        writer.fixed(std::bit_cast<std::uint32_t>(value));
    }

    bool readFloat(Reader& reader, float& result) noexcept
    {
        const auto bits = reader.fixed<std::uint32_t>();
        if (!bits)
            return false;
        result = std::bit_cast<float>(*bits);
        return std::isfinite(result);
    }

    void writePosition(Writer& writer, Position3 value)
    {
        writer.fixed(value.x());
        writer.fixed(value.y());
        writer.fixed(value.z());
    }

    std::optional<Position3> readPosition(Reader& reader) noexcept
    {
        const auto x = reader.fixed<std::int64_t>();
        const auto y = reader.fixed<std::int64_t>();
        const auto z = reader.fixed<std::int64_t>();
        return x && y && z ? std::optional(Position3(*x, *y, *z)) : std::nullopt;
    }

    void writeItemStack(Writer& writer, const CanonicalItemStack& stack)
    {
        writeStrong(writer, stack.stackId);
        writeStrong(writer, stack.prototypeId);
        writer.fixed(stack.count);
        writer.fixed(stack.condition);
        writer.fixed(stack.enchantmentCharge);
        writeOptionalStrong(writer, stack.soulPrototype);
    }

    std::optional<CanonicalItemStack> readItemStack(Reader& reader) noexcept
    {
        auto stackId = readStrong<ItemStackId>(reader);
        auto prototypeId = readStrong<ItemPrototypeId>(reader);
        const auto count = reader.fixed<std::uint32_t>();
        const auto condition = reader.fixed<std::uint32_t>();
        const auto charge = reader.fixed<std::uint32_t>();
        std::optional<ActorPrototypeId> soul;
        if (!stackId || !prototypeId || !count || *count == 0 || *count > MaximumTransferCount || !condition || !charge
            || !readOptionalStrong(reader, soul))
            return std::nullopt;
        return CanonicalItemStack{ *stackId, *prototypeId, *count, *condition, *charge, soul };
    }

    void writeInventory(Writer& writer, const CanonicalDurableInventoryState& inventory)
    {
        writer.fixed(static_cast<std::uint32_t>(inventory.players.size()));
        for (const auto& player : inventory.players)
        {
            writeStrong(writer, player.player);
            writeStrong(writer, player.revision);
            writeStrong(writer, player.lastChangeTick);
            writer.fixed(static_cast<std::uint32_t>(player.stacks.size()));
            for (const auto& stack : player.stacks)
                writeItemStack(writer, stack);
            for (const auto& equipped : player.equipment)
                writeOptionalStrong(writer, equipped);
            writeOptionalStrong(writer, player.initializedCharacterProfile);
        }
        writer.fixed(static_cast<std::uint32_t>(inventory.containers.size()));
        for (const auto& container : inventory.containers)
        {
            writeStrong(writer, container.containerId);
            writeCell(writer, container.cell);
            writePosition(writer, container.position);
            writeStrong(writer, container.revision);
            writeStrong(writer, container.lastChangeTick);
            writer.fixed(container.capacityWeight);
            writer.fixed(static_cast<std::uint32_t>(container.stacks.size()));
            for (const auto& stack : container.stacks)
                writeItemStack(writer, stack);
        }
        writer.fixed(static_cast<std::uint32_t>(inventory.worldItems.size()));
        for (const auto& item : inventory.worldItems)
        {
            writeItemStack(writer, item.stack);
            writeCell(writer, item.cell);
            writePosition(writer, item.position);
            writeStrong(writer, item.revision);
            writeStrong(writer, item.lastChangeTick);
        }
        writeOptionalStrong(writer, inventory.nextItemStackId);
    }

    std::optional<CellId> readCell(Reader& reader) noexcept
    {
        const auto kind = reader.fixed<std::uint8_t>();
        const auto spaceRaw = reader.fixed<std::uint64_t>();
        const auto gridX = reader.fixed<std::int32_t>();
        const auto gridY = reader.fixed<std::int32_t>();
        const auto space = spaceRaw ? CellSpaceId::fromValue(*spaceRaw) : std::nullopt;
        if (!kind || *kind > 1 || !gridX || !gridY || !space)
            return std::nullopt;
        return *kind == 0 ? std::optional(CellId::interior(*space))
                          : std::optional(CellId::exterior(*space, *gridX, *gridY));
    }

    std::optional<CanonicalDurableInventoryState> readInventory(Reader& reader) noexcept
    {
        CanonicalDurableInventoryState result;
        const auto playerCount = reader.fixed<std::uint32_t>();
        if (!playerCount || *playerCount > MaximumInventoryPlayers)
            return std::nullopt;
        result.players.reserve(*playerCount);
        for (std::uint32_t index = 0; index < *playerCount; ++index)
        {
            auto playerId = readStrong<PlayerId>(reader);
            auto revision = readStrong<InventoryRevision>(reader);
            auto tick = readStrong<ServerTick>(reader);
            const auto stackCount = reader.fixed<std::uint32_t>();
            if (!playerId || !revision || !tick || !stackCount || *stackCount > MaximumPlayerInventoryStacks)
                return std::nullopt;
            std::vector<CanonicalItemStack> stacks;
            stacks.reserve(*stackCount);
            for (std::uint32_t stackIndex = 0; stackIndex < *stackCount; ++stackIndex)
            {
                auto stack = readItemStack(reader);
                if (!stack)
                    return std::nullopt;
                stacks.push_back(*stack);
            }
            std::array<std::optional<ItemStackId>, static_cast<std::size_t>(EquipmentSlot::Count)> equipment{};
            for (auto& equipped : equipment)
                if (!readOptionalStrong(reader, equipped))
                    return std::nullopt;
            std::optional<CharacterProfileRevision> profile;
            if (!readOptionalStrong(reader, profile))
                return std::nullopt;
            result.players.push_back({ *playerId, *revision, *tick, std::move(stacks), equipment, profile });
        }
        const auto containerCount = reader.fixed<std::uint32_t>();
        if (!containerCount || *containerCount > MaximumInventoryContainers)
            return std::nullopt;
        result.containers.reserve(*containerCount);
        for (std::uint32_t index = 0; index < *containerCount; ++index)
        {
            auto id = readStrong<ContainerId>(reader);
            auto cell = readCell(reader);
            auto position = readPosition(reader);
            auto revision = readStrong<ContainerRevision>(reader);
            auto tick = readStrong<ServerTick>(reader);
            const auto capacity = reader.fixed<std::uint32_t>();
            const auto stackCount = reader.fixed<std::uint32_t>();
            if (!id || !cell || !position || !revision || !tick || !capacity || !stackCount
                || *stackCount > MaximumContainerStacks)
                return std::nullopt;
            std::vector<CanonicalItemStack> stacks;
            stacks.reserve(*stackCount);
            for (std::uint32_t stackIndex = 0; stackIndex < *stackCount; ++stackIndex)
            {
                auto stack = readItemStack(reader);
                if (!stack)
                    return std::nullopt;
                stacks.push_back(*stack);
            }
            result.containers.push_back({ *id, *cell, *position, *revision, *tick, *capacity, std::move(stacks) });
        }
        const auto worldCount = reader.fixed<std::uint32_t>();
        if (!worldCount || *worldCount > MaximumWorldItemStacks)
            return std::nullopt;
        result.worldItems.reserve(*worldCount);
        for (std::uint32_t index = 0; index < *worldCount; ++index)
        {
            auto stack = readItemStack(reader);
            auto cell = readCell(reader);
            auto position = readPosition(reader);
            auto revision = readStrong<WorldItemRevision>(reader);
            auto tick = readStrong<ServerTick>(reader);
            if (!stack || !cell || !position || !revision || !tick)
                return std::nullopt;
            result.worldItems.push_back({ *stack, *cell, *position, *revision, *tick });
        }
        if (!readOptionalStrong(reader, result.nextItemStackId))
            return std::nullopt;
        return result;
    }

    void writeBool(Writer& writer, bool value)
    {
        writer.fixed(static_cast<std::uint8_t>(value));
    }

    bool readBool(Reader& reader, bool& value) noexcept
    {
        const auto raw = reader.fixed<std::uint8_t>();
        if (!raw || *raw > 1)
            return false;
        value = *raw != 0;
        return true;
    }

    void writeAttacker(Writer& writer, const OpenMwMeleeAttacker& value)
    {
        writeFloat(writer, value.agility);
        writeFloat(writer, value.luck);
        writeFloat(writer, value.strength);
        writeFloat(writer, value.fatigueTerm);
        writeFloat(writer, value.normalizedEncumbrance);
        writeFloat(writer, value.fortifyAttack);
        writeFloat(writer, value.blind);
        writeFloat(writer, value.weaponSkill);
        writeFloat(writer, value.handToHandSkill);
        writeFloat(writer, value.fatigue);
        writeFloat(writer, value.endurance);
        writeBool(writer, value.werewolf);
        writeBool(writer, value.godMode);
    }

    bool readAttacker(Reader& reader, OpenMwMeleeAttacker& value) noexcept
    {
        return readFloat(reader, value.agility) && readFloat(reader, value.luck) && readFloat(reader, value.strength)
            && readFloat(reader, value.fatigueTerm) && readFloat(reader, value.normalizedEncumbrance)
            && readFloat(reader, value.fortifyAttack) && readFloat(reader, value.blind)
            && readFloat(reader, value.weaponSkill) && readFloat(reader, value.handToHandSkill)
            && readFloat(reader, value.fatigue) && readFloat(reader, value.endurance)
            && readBool(reader, value.werewolf) && readBool(reader, value.godMode);
    }

    void writeVictim(Writer& writer, const OpenMwMeleeVictim& value)
    {
        writeFloat(writer, value.health);
        writeFloat(writer, value.fatigue);
        writeFloat(writer, value.evasion);
        writeFloat(writer, value.chameleon);
        writeFloat(writer, value.invisibility);
        writeFloat(writer, value.normalWeaponResistance);
        writeFloat(writer, value.normalWeaponWeakness);
        writeBool(writer, value.fatigueNonNegative);
        writeBool(writer, value.knockedDown);
        writeBool(writer, value.paralyzed);
        writeBool(writer, value.unaware);
        writeBool(writer, value.dead);
    }

    bool readVictim(Reader& reader, OpenMwMeleeVictim& value) noexcept
    {
        return readFloat(reader, value.health) && readFloat(reader, value.fatigue) && readFloat(reader, value.evasion)
            && readFloat(reader, value.chameleon) && readFloat(reader, value.invisibility)
            && readFloat(reader, value.normalWeaponResistance) && readFloat(reader, value.normalWeaponWeakness)
            && readBool(reader, value.fatigueNonNegative) && readBool(reader, value.knockedDown)
            && readBool(reader, value.paralyzed) && readBool(reader, value.unaware) && readBool(reader, value.dead);
    }

    void writeWeapon(Writer& writer, const OpenMwMeleeWeapon& value)
    {
        writeFloat(writer, value.chopMinimum);
        writeFloat(writer, value.chopMaximum);
        writeFloat(writer, value.slashMinimum);
        writeFloat(writer, value.slashMaximum);
        writeFloat(writer, value.thrustMinimum);
        writeFloat(writer, value.thrustMaximum);
        writeFloat(writer, value.weight);
        writeFloat(writer, value.normalizedCondition);
        writer.fixed(value.condition);
        writeBool(writer, value.hasCondition);
        writeBool(writer, value.normalWeapon);
    }

    bool readWeapon(Reader& reader, OpenMwMeleeWeapon& value) noexcept
    {
        return readFloat(reader, value.chopMinimum) && readFloat(reader, value.chopMaximum)
            && readFloat(reader, value.slashMinimum) && readFloat(reader, value.slashMaximum)
            && readFloat(reader, value.thrustMinimum) && readFloat(reader, value.thrustMaximum)
            && readFloat(reader, value.weight) && readFloat(reader, value.normalizedCondition) && ([&] {
                   const auto condition = reader.fixed<std::int32_t>();
                   if (!condition)
                       return false;
                   value.condition = *condition;
                   return true;
               })()
            && readBool(reader, value.hasCondition) && readBool(reader, value.normalWeapon);
    }

    void writeDefense(Writer& writer, const DirectMagicDefense& value)
    {
        writeFloat(writer, value.willpower);
        writeFloat(writer, value.destructionSkill);
        writeFloat(writer, value.fireResistance);
        writeFloat(writer, value.shockResistance);
        writeFloat(writer, value.frostResistance);
        writeFloat(writer, value.poisonResistance);
        writeFloat(writer, value.commonDiseaseResistance);
        writeFloat(writer, value.blightDiseaseResistance);
        writeFloat(writer, value.fireShield);
        writeFloat(writer, value.shockShield);
        writeFloat(writer, value.frostShield);
    }

    bool readDefense(Reader& reader, DirectMagicDefense& value) noexcept
    {
        return readFloat(reader, value.willpower) && readFloat(reader, value.destructionSkill)
            && readFloat(reader, value.fireResistance) && readFloat(reader, value.shockResistance)
            && readFloat(reader, value.frostResistance) && readFloat(reader, value.poisonResistance)
            && readFloat(reader, value.commonDiseaseResistance) && readFloat(reader, value.blightDiseaseResistance)
            && readFloat(reader, value.fireShield) && readFloat(reader, value.shockShield)
            && readFloat(reader, value.frostShield);
    }

    void writeCombat(Writer& writer, const CanonicalDurableCombatState& combat)
    {
        writer.fixed(static_cast<std::uint32_t>(combat.players.size()));
        for (const auto& player : combat.players)
        {
            writeStrong(writer, player.playerId);
            writeStrong(writer, player.revision);
            writeAttacker(writer, player.stats);
            for (float value : player.weaponSkills)
                writeFloat(writer, value);
            writeFloat(writer, player.blockSkill);
            writer.fixed(player.maximumEncumbranceWeightUnits);
            writeOptionalStrong(writer, player.lastAttackTick);
            writeOptionalStrong(writer, player.initializedCharacterProfile);
            writeVictim(writer, player.victim);
            writeVictim(writer, player.respawnVictim);
            writeOptionalStrong(writer, player.deathTick);
            writeFloat(writer, player.maximumHealth);
            writeFloat(writer, player.maximumFatigue);
            writeFloat(writer, player.magicka);
            writeFloat(writer, player.maximumMagicka);
            writeFloat(writer, player.healthRecoveryPerSecond);
            writeFloat(writer, player.magickaRecoveryPerSecond);
            for (const auto& rule : player.skillRules)
            {
                writer.fixed(static_cast<std::uint8_t>(rule.specialization));
                writeFloat(writer, rule.useGain);
            }
            for (const auto& progress : player.skillProgression)
            {
                writeFloat(writer, progress.progress);
                writeFloat(writer, progress.requirementFactor);
            }
            for (float value : player.armorSkills)
                writeFloat(writer, value);
            writeFloat(writer, player.securitySkill);
            writeDefense(writer, player.magicDefense);
            for (float value : player.magicSkills)
                writeFloat(writer, value);
            writeFloat(writer, player.enchantSkill);
            writer.fixed(static_cast<std::uint32_t>(player.knownSpells.size()));
            for (const auto spell : player.knownSpells)
                writeStrong(writer, spell);
            writeOptionalStrong(writer, player.lastMagicUseTick);
            writer.fixed(static_cast<std::uint32_t>(player.contractedDiseases.size()));
            for (const auto disease : player.contractedDiseases)
                writeStrong(writer, disease);
        }
        writer.fixed(static_cast<std::uint32_t>(combat.actors.size()));
        for (const auto& actor : combat.actors)
        {
            writeStrong(writer, actor.actorId);
            writeStrong(writer, actor.revision);
            writeVictim(writer, actor.stats);
            writeVictim(writer, actor.respawnStats);
            writeAttacker(writer, actor.attacker);
            writeBool(writer, actor.naturalWeapon.has_value());
            if (actor.naturalWeapon)
                writeWeapon(writer, *actor.naturalWeapon);
            writer.fixed(actor.attackReachQuanta);
            writeOptionalStrong(writer, actor.aggressionTarget);
            writeOptionalStrong(writer, actor.lastAttackTick);
            writeOptionalStrong(writer, actor.deathTick);
            writeFloat(writer, actor.maximumHealth);
            writeFloat(writer, actor.maximumFatigue);
            writeFloat(writer, actor.magicka);
            writeFloat(writer, actor.maximumMagicka);
            writeBool(writer, actor.creature);
            writeDefense(writer, actor.magicDefense);
        }
        for (const auto word : combat.randomWords)
            writer.fixed(word);
        writeOptionalStrong(writer, combat.lastSimulationTick);
    }

    std::optional<CanonicalDurableCombatState> readCombat(Reader& reader) noexcept
    {
        CanonicalDurableCombatState result;
        const auto playerCount = reader.fixed<std::uint32_t>();
        if (!playerCount || *playerCount > MaximumPlayerCombatants)
            return std::nullopt;
        result.players.reserve(*playerCount);
        for (std::uint32_t index = 0; index < *playerCount; ++index)
        {
            auto id = readStrong<PlayerId>(reader);
            auto revision = readStrong<CombatRevision>(reader);
            if (!id || !revision)
                return std::nullopt;
            CanonicalPlayerCombatState player{ *id };
            player.revision = *revision;
            if (!readAttacker(reader, player.stats))
                return std::nullopt;
            for (auto& value : player.weaponSkills)
                if (!readFloat(reader, value))
                    return std::nullopt;
            if (!readFloat(reader, player.blockSkill))
                return std::nullopt;
            const auto capacity = reader.fixed<std::uint64_t>();
            if (!capacity)
                return std::nullopt;
            player.maximumEncumbranceWeightUnits = *capacity;
            if (!readOptionalStrong(reader, player.lastAttackTick)
                || !readOptionalStrong(reader, player.initializedCharacterProfile) || !readVictim(reader, player.victim)
                || !readVictim(reader, player.respawnVictim) || !readOptionalStrong(reader, player.deathTick)
                || !readFloat(reader, player.maximumHealth) || !readFloat(reader, player.maximumFatigue)
                || !readFloat(reader, player.magicka) || !readFloat(reader, player.maximumMagicka)
                || !readFloat(reader, player.healthRecoveryPerSecond)
                || !readFloat(reader, player.magickaRecoveryPerSecond))
                return std::nullopt;
            for (auto& rule : player.skillRules)
            {
                const auto specialization = reader.fixed<std::uint8_t>();
                if (!specialization || *specialization > static_cast<std::uint8_t>(ClassSpecialization::Stealth)
                    || !readFloat(reader, rule.useGain))
                    return std::nullopt;
                rule.specialization = static_cast<ClassSpecialization>(*specialization);
            }
            for (auto& progress : player.skillProgression)
                if (!readFloat(reader, progress.progress) || !readFloat(reader, progress.requirementFactor))
                    return std::nullopt;
            for (auto& value : player.armorSkills)
                if (!readFloat(reader, value))
                    return std::nullopt;
            if (!readFloat(reader, player.securitySkill))
                return std::nullopt;
            if (!readDefense(reader, player.magicDefense))
                return std::nullopt;
            for (auto& value : player.magicSkills)
                if (!readFloat(reader, value))
                    return std::nullopt;
            if (!readFloat(reader, player.enchantSkill))
                return std::nullopt;
            const auto knownSpellCount = reader.fixed<std::uint32_t>();
            if (!knownSpellCount || *knownSpellCount > MaximumStartingSpells)
                return std::nullopt;
            player.knownSpells.reserve(*knownSpellCount);
            for (std::uint32_t spellIndex = 0; spellIndex < *knownSpellCount; ++spellIndex)
            {
                auto spell = readStrong<SpellRecordId>(reader);
                if (!spell)
                    return std::nullopt;
                player.knownSpells.push_back(*spell);
            }
            if (!readOptionalStrong(reader, player.lastMagicUseTick))
                return std::nullopt;
            const auto diseaseCount = reader.fixed<std::uint32_t>();
            if (!diseaseCount || *diseaseCount > MaximumContractedDiseasesPerPlayer)
                return std::nullopt;
            player.contractedDiseases.reserve(*diseaseCount);
            for (std::uint32_t diseaseIndex = 0; diseaseIndex < *diseaseCount; ++diseaseIndex)
            {
                auto disease = readStrong<SpellRecordId>(reader);
                if (!disease)
                    return std::nullopt;
                player.contractedDiseases.push_back(*disease);
            }
            result.players.push_back(std::move(player));
        }
        const auto actorCount = reader.fixed<std::uint32_t>();
        if (!actorCount || *actorCount > MaximumActorCombatants)
            return std::nullopt;
        result.actors.reserve(*actorCount);
        for (std::uint32_t index = 0; index < *actorCount; ++index)
        {
            auto id = readStrong<ActorId>(reader);
            auto revision = readStrong<CombatRevision>(reader);
            if (!id || !revision)
                return std::nullopt;
            CanonicalActorCombatState actor{ *id };
            actor.revision = *revision;
            if (!readVictim(reader, actor.stats) || !readVictim(reader, actor.respawnStats)
                || !readAttacker(reader, actor.attacker))
                return std::nullopt;
            bool hasWeapon = false;
            if (!readBool(reader, hasWeapon))
                return std::nullopt;
            if (hasWeapon)
            {
                OpenMwMeleeWeapon weapon;
                if (!readWeapon(reader, weapon))
                    return std::nullopt;
                actor.naturalWeapon = weapon;
            }
            const auto reach = reader.fixed<std::uint32_t>();
            if (!reach)
                return std::nullopt;
            actor.attackReachQuanta = *reach;
            if (!readOptionalStrong(reader, actor.aggressionTarget) || !readOptionalStrong(reader, actor.lastAttackTick)
                || !readOptionalStrong(reader, actor.deathTick) || !readFloat(reader, actor.maximumHealth)
                || !readFloat(reader, actor.maximumFatigue) || !readFloat(reader, actor.magicka)
                || !readFloat(reader, actor.maximumMagicka) || !readBool(reader, actor.creature)
                || !readDefense(reader, actor.magicDefense))
                return std::nullopt;
            result.actors.push_back(std::move(actor));
        }
        for (auto& word : result.randomWords)
        {
            const auto value = reader.fixed<std::uint64_t>();
            if (!value)
                return std::nullopt;
            word = *value;
        }
        if (std::ranges::all_of(result.randomWords, [](auto value) { return value == 0; })
            || !readOptionalStrong(reader, result.lastSimulationTick))
            return std::nullopt;
        const auto random = RandomStateV1::fromWords(
            result.randomWords[0], result.randomWords[1], result.randomWords[2], result.randomWords[3]);
        if (!random
            || !std::holds_alternative<CanonicalCombatWorld>(
                createCanonicalCombatWorld(result.players, result.actors, *random, result.lastSimulationTick)))
            return std::nullopt;
        return result;
    }

    void writeTransform(Writer& writer, const Transform& transform)
    {
        writeCell(writer, transform.cell());
        writePosition(writer, transform.position());
        const auto orientation = transform.orientation();
        writer.fixed(orientation.x().value());
        writer.fixed(orientation.y().value());
        writer.fixed(orientation.z().value());
    }

    std::optional<Transform> readTransform(Reader& reader) noexcept
    {
        auto cell = readCell(reader);
        auto position = readPosition(reader);
        const auto x = reader.fixed<std::uint32_t>();
        const auto y = reader.fixed<std::uint32_t>();
        const auto z = reader.fixed<std::uint32_t>();
        if (!cell || !position || !x || !y || !z)
            return std::nullopt;
        return Transform(
            *cell, *position, Orientation3(Turn32::fromValue(*x), Turn32::fromValue(*y), Turn32::fromValue(*z)));
    }

    void writeObjects(Writer& writer, const CanonicalDurableInteractiveObjectState& objects)
    {
        writer.fixed(static_cast<std::uint32_t>(objects.objects.size()));
        for (const auto& object : objects.objects)
        {
            writeStrong(writer, object.objectId());
            writeCell(writer, object.cell());
            writer.fixed(static_cast<std::uint8_t>(object.doorState()));
            writer.fixed(static_cast<std::uint8_t>(object.lockState()));
            writer.fixed(object.lockLevel());
            writeOptionalStrong(writer, object.keyId());
            writer.fixed(static_cast<std::uint8_t>(object.trapState()));
            writeOptionalStrong(writer, object.trapId());
            writeStrong(writer, object.revision());
            writeStrong(writer, object.lastChangeTick());
        }
    }

    std::optional<CanonicalDurableInteractiveObjectState> readObjects(Reader& reader) noexcept
    {
        const auto count = reader.fixed<std::uint32_t>();
        if (!count || *count > MaximumInteractiveObjectCatalogEntries)
            return std::nullopt;
        CanonicalDurableInteractiveObjectState result;
        result.objects.reserve(*count);
        for (std::uint32_t index = 0; index < *count; ++index)
        {
            auto id = readStrong<InteractiveObjectId>(reader);
            auto cell = readCell(reader);
            const auto door = reader.fixed<std::uint8_t>();
            const auto lock = reader.fixed<std::uint8_t>();
            const auto level = reader.fixed<std::uint32_t>();
            std::optional<KeyPrototypeId> key;
            if (!id || !cell || !door || *door > static_cast<std::uint8_t>(DoorState::Open) || !lock
                || *lock > static_cast<std::uint8_t>(LockState::Locked) || !level || !readOptionalStrong(reader, key))
                return std::nullopt;
            const auto trap = reader.fixed<std::uint8_t>();
            std::optional<TrapPrototypeId> trapId;
            if (!trap || *trap > static_cast<std::uint8_t>(TrapState::Armed) || !readOptionalStrong(reader, trapId))
                return std::nullopt;
            auto revision = readStrong<ObjectRevision>(reader);
            auto tick = readStrong<ServerTick>(reader);
            if (!revision || !tick)
                return std::nullopt;
            result.objects.emplace_back(*id, *cell, static_cast<DoorState>(*door), static_cast<LockState>(*lock),
                *level, key, static_cast<TrapState>(*trap), trapId, *revision, *tick);
        }
        if (!std::holds_alternative<CanonicalInteractiveObjectWorld>(
                createCanonicalInteractiveObjectWorld(result.objects)))
            return std::nullopt;
        return result;
    }

    void writeActors(Writer& writer, const CanonicalDurableActorState& actors)
    {
        writer.fixed(static_cast<std::uint32_t>(actors.actors.size()));
        for (const auto& actor : actors.actors)
        {
            writeStrong(writer, actor.actorId());
            writeStrong(writer, actor.entityId());
            writeStrong(writer, actor.prototypeId());
            writeTransform(writer, actor.root());
            const auto velocity = actor.velocity();
            writer.fixed(velocity.x());
            writer.fixed(velocity.y());
            writer.fixed(velocity.z());
            writeStrong(writer, actor.revision());
            writeStrong(writer, actor.authorityEpoch());
            writeStrong(writer, actor.lastChangeTick());
            writer.fixed(static_cast<std::uint8_t>(actor.activity()));
            writer.fixed(actor.waypointIndex());
        }
    }

    std::optional<CanonicalDurableActorState> readActors(Reader& reader) noexcept
    {
        const auto count = reader.fixed<std::uint32_t>();
        if (!count || *count > MaximumActorCatalogEntries)
            return std::nullopt;
        CanonicalDurableActorState result;
        result.actors.reserve(*count);
        for (std::uint32_t index = 0; index < *count; ++index)
        {
            auto actor = readStrong<ActorId>(reader);
            auto entity = readStrong<EntityId>(reader);
            auto prototype = readStrong<ActorPrototypeId>(reader);
            auto root = readTransform(reader);
            const auto vx = reader.fixed<std::int64_t>();
            const auto vy = reader.fixed<std::int64_t>();
            const auto vz = reader.fixed<std::int64_t>();
            auto revision = readStrong<EntityRevision>(reader);
            auto epoch = readStrong<AuthorityEpoch>(reader);
            auto tick = readStrong<ServerTick>(reader);
            const auto activity = reader.fixed<std::uint8_t>();
            const auto waypoint = reader.fixed<std::uint16_t>();
            if (!actor || !entity || !prototype || !root || !vx || !vy || !vz || !revision || !epoch || !tick
                || !activity || *activity > static_cast<std::uint8_t>(ActorActivity::Wander) || !waypoint)
                return std::nullopt;
            result.actors.emplace_back(*actor, *entity, *prototype, *root, LinearVelocity3(*vx, *vy, *vz), *revision,
                *epoch, *tick, static_cast<ActorActivity>(*activity), *waypoint);
        }
        if (!std::holds_alternative<CanonicalActorWorld>(createCanonicalActorWorld(result.actors)))
            return std::nullopt;
        return result;
    }

    void writeWorld(Writer& writer, const CanonicalWorldState& world)
    {
        const auto& time = world.time();
        writer.fixed(time.day);
        writer.fixed(time.month);
        writer.fixed(time.year);
        writer.fixed(time.millisecondsSinceMidnight);
        writer.fixed(time.timeScaleUnits);
        writer.fixed(time.subMillisecondRemainder);
        writeStrong(writer, time.revision);
        writeStrong(writer, time.lastChangeTick);
        writeStrong(writer, time.lastAdvanceTick);
        writer.fixed(static_cast<std::uint32_t>(world.globals().size()));
        for (const auto& global : world.globals())
        {
            writeStrong(writer, global.id);
            writer.fixed(static_cast<std::uint8_t>(global.type()));
            if (const auto* shortValue = std::get_if<std::int16_t>(&global.value))
                writer.fixed(*shortValue);
            else if (const auto* longValue = std::get_if<std::int32_t>(&global.value))
                writer.fixed(*longValue);
            else
                writeFloat(writer, std::get<float>(global.value));
            writeStrong(writer, global.revision);
            writeStrong(writer, global.lastChangeTick);
        }
        writeBool(writer, world.questJournalCatalog().has_value());
        if (!world.questJournalCatalog())
            return;
        const auto& catalog = *world.questJournalCatalog();
        writer.bytes(catalog.manifest().bytes());
        writer.fixed(static_cast<std::uint32_t>(catalog.quests().size()));
        for (const auto& quest : catalog.quests())
        {
            writeStrong(writer, quest.id);
            writeStrong(writer, quest.initialStage);
            writer.fixed(static_cast<std::uint32_t>(quest.stages.size()));
            for (const auto stage : quest.stages)
                writeStrong(writer, stage);
        }
        writer.fixed(static_cast<std::uint32_t>(catalog.journal().size()));
        for (const auto entry : catalog.journal())
        {
            writeStrong(writer, entry.id);
            writeStrong(writer, entry.quest);
            writeStrong(writer, entry.stage);
        }
        writer.fixed(static_cast<std::uint32_t>(world.questJournal().size()));
        for (const auto& player : world.questJournal())
        {
            writeStrong(writer, player.player);
            writer.fixed(static_cast<std::uint32_t>(player.quests.size()));
            for (const auto quest : player.quests)
            {
                writeStrong(writer, quest.id);
                writeStrong(writer, quest.stage);
                writeStrong(writer, quest.revision);
                writeStrong(writer, quest.lastChangeTick);
            }
            writer.fixed(static_cast<std::uint32_t>(player.journal.size()));
            for (const auto entry : player.journal)
            {
                writeStrong(writer, entry.id);
                writeStrong(writer, entry.revision);
                writeStrong(writer, entry.changeTick);
            }
            writeStrong(writer, player.journalRevision);
            writeStrong(writer, player.lastJournalChangeTick);
        }
        writeBool(writer, world.factionDialogueCatalog().has_value());
        if (!world.factionDialogueCatalog())
            return;
        const auto& factionCatalog = *world.factionDialogueCatalog();
        writer.bytes(factionCatalog.manifest().bytes());
        writer.fixed(static_cast<std::uint32_t>(factionCatalog.factions().size()));
        for (const auto& faction : factionCatalog.factions())
        {
            writeStrong(writer, faction.id);
            writer.fixed(static_cast<std::uint32_t>(faction.ranks.size()));
            for (const auto rank : faction.ranks)
                writeStrong(writer, rank);
        }
        writer.fixed(static_cast<std::uint32_t>(factionCatalog.dialogueChoices().size()));
        for (const auto choice : factionCatalog.dialogueChoices())
        {
            writeStrong(writer, choice.id);
            writeBool(writer, choice.requiredFaction.has_value());
            if (choice.requiredFaction)
                writeStrong(writer, *choice.requiredFaction);
            writeStrong(writer, choice.minimumRank);
            writer.fixed(choice.minimumReputation);
        }
        writer.fixed(static_cast<std::uint32_t>(world.factionStates().size()));
        for (const auto& player : world.factionStates())
        {
            writeStrong(writer, player.player);
            writer.fixed(static_cast<std::uint32_t>(player.factions.size()));
            for (const auto faction : player.factions)
            {
                writeStrong(writer, faction.id);
                writeBool(writer, faction.rank.has_value());
                if (faction.rank)
                    writeStrong(writer, *faction.rank);
                writeStrong(writer, faction.membershipRevision);
                writeStrong(writer, faction.lastMembershipChangeTick);
                writer.fixed(faction.reputation);
                writeStrong(writer, faction.reputationRevision);
                writeStrong(writer, faction.lastReputationChangeTick);
            }
        }
        writeBool(writer, world.weatherCatalog().has_value() && world.weather().has_value());
        if (!world.weatherCatalog() || !world.weather())
            return;
        const auto& weatherCatalog = *world.weatherCatalog();
        const auto& weather = *world.weather();
        writer.bytes(weatherCatalog.manifest().bytes());
        writer.fixed(static_cast<std::uint32_t>(weatherCatalog.weather().size()));
        for (const auto id : weatherCatalog.weather())
            writeStrong(writer, id);
        writer.fixed(static_cast<std::uint32_t>(weatherCatalog.regions().size()));
        for (const auto& region : weatherCatalog.regions())
        {
            writeStrong(writer, region.id);
            writeStrong(writer, region.initialWeather);
            writer.fixed(region.selectionIntervalTicks);
            writer.fixed(region.transitionDurationTicks);
            writer.fixed(static_cast<std::uint32_t>(region.eligibleWeather.size()));
            for (const auto id : region.eligibleWeather)
                writeStrong(writer, id);
        }
        for (const auto word : weather.randomState.words())
            writer.fixed(word);
        writeStrong(writer, weather.lastAdvanceTick);
        writer.fixed(static_cast<std::uint32_t>(weather.regions.size()));
        for (const auto region : weather.regions)
        {
            writeStrong(writer, region.region);
            writeStrong(writer, region.currentWeather);
            writeStrong(writer, region.targetWeather);
            writeStrong(writer, region.transitionStartTick);
            writeStrong(writer, region.transitionEndTick);
            writeStrong(writer, region.nextSelectionTick);
            writeStrong(writer, region.revision);
            writeStrong(writer, region.lastChangeTick);
        }
    }

    std::optional<CanonicalWorldState> readWorld(Reader& reader) noexcept
    {
        CanonicalWorldTimeState time;
        const auto day = reader.fixed<std::uint8_t>();
        const auto month = reader.fixed<std::uint8_t>();
        const auto year = reader.fixed<std::int32_t>();
        const auto milliseconds = reader.fixed<std::uint32_t>();
        const auto scale = reader.fixed<std::uint32_t>();
        const auto remainder = reader.fixed<std::uint16_t>();
        const auto revision = readStrong<WorldTimeRevision>(reader);
        const auto lastChange = readStrong<ServerTick>(reader);
        const auto lastAdvance = readStrong<ServerTick>(reader);
        const auto count = reader.fixed<std::uint32_t>();
        if (!day || !month || !year || !milliseconds || !scale || !remainder || !revision || !lastChange || !lastAdvance
            || !count || *count > MaximumGlobalVariables)
            return std::nullopt;
        time = { *day, *month, *year, *milliseconds, *scale, *remainder, *revision, *lastChange, *lastAdvance };
        std::vector<CanonicalGlobalVariableState> globals;
        globals.reserve(*count);
        for (std::uint32_t index = 0; index < *count; ++index)
        {
            const auto id = readStrong<GlobalVariableId>(reader);
            const auto type = reader.fixed<std::uint8_t>();
            if (!id || !type || *type > static_cast<std::uint8_t>(GlobalVariableType::Float))
                return std::nullopt;
            GlobalVariableValue value;
            if (*type == static_cast<std::uint8_t>(GlobalVariableType::Short))
            {
                const auto parsed = reader.fixed<std::int16_t>();
                if (!parsed)
                    return std::nullopt;
                value = *parsed;
            }
            else if (*type == static_cast<std::uint8_t>(GlobalVariableType::Long))
            {
                const auto parsed = reader.fixed<std::int32_t>();
                if (!parsed)
                    return std::nullopt;
                value = *parsed;
            }
            else
            {
                float parsed = 0.f;
                if (!readFloat(reader, parsed))
                    return std::nullopt;
                value = parsed;
            }
            const auto globalRevision = readStrong<GlobalVariableRevision>(reader);
            const auto globalTick = readStrong<ServerTick>(reader);
            if (!globalRevision || !globalTick)
                return std::nullopt;
            globals.push_back({ *id, value, *globalRevision, *globalTick });
        }
        bool hasQuestJournal = false;
        if (!readBool(reader, hasQuestJournal))
            return std::nullopt;
        if (!hasQuestJournal)
            return CanonicalWorldState::create(time, globals);
        const auto manifestBytes = reader.bytes(ContentManifestIdBytes);
        const auto questCount = reader.fixed<std::uint32_t>();
        if (!manifestBytes || !questCount || *questCount > MaximumQuestCatalogEntries)
            return std::nullopt;
        const auto manifest = ContentManifestId::fromBytes(*manifestBytes);
        if (!manifest)
            return std::nullopt;
        std::vector<QuestCatalogEntry> quests;
        quests.reserve(*questCount);
        std::size_t totalStages = 0;
        for (std::uint32_t index = 0; index < *questCount; ++index)
        {
            const auto id = readStrong<QuestId>(reader);
            const auto initialStage = readStrong<QuestStage>(reader);
            const auto stageCount = reader.fixed<std::uint32_t>();
            if (!id || !initialStage || !stageCount || *stageCount == 0 || *stageCount > MaximumQuestStagesPerQuest
                || *stageCount > MaximumQuestCatalogStages - totalStages)
                return std::nullopt;
            totalStages += *stageCount;
            std::vector<QuestStage> stages;
            stages.reserve(*stageCount);
            for (std::uint32_t stageIndex = 0; stageIndex < *stageCount; ++stageIndex)
            {
                auto stage = readStrong<QuestStage>(reader);
                if (!stage)
                    return std::nullopt;
                stages.push_back(*stage);
            }
            quests.push_back({ *id, *initialStage, std::move(stages) });
        }
        const auto journalCount = reader.fixed<std::uint32_t>();
        if (!journalCount || *journalCount > MaximumJournalCatalogEntries)
            return std::nullopt;
        std::vector<JournalCatalogEntry> journal;
        journal.reserve(*journalCount);
        for (std::uint32_t index = 0; index < *journalCount; ++index)
        {
            const auto id = readStrong<JournalEntryId>(reader);
            const auto quest = readStrong<QuestId>(reader);
            const auto stage = readStrong<QuestStage>(reader);
            if (!id || !quest || !stage)
                return std::nullopt;
            journal.push_back({ *id, *quest, *stage });
        }
        auto catalog = QuestJournalCatalog::create(*manifest, quests, journal);
        const auto playerCount = reader.fixed<std::uint32_t>();
        if (!catalog || !playerCount || *playerCount > MaximumPlayerQuestJournalStates)
            return std::nullopt;
        std::vector<CanonicalPlayerQuestJournalState> players;
        players.reserve(*playerCount);
        std::size_t totalQuestStates = 0;
        std::size_t totalJournalEntries = 0;
        for (std::uint32_t playerIndex = 0; playerIndex < *playerCount; ++playerIndex)
        {
            const auto player = readStrong<PlayerId>(reader);
            const auto playerQuestCount = reader.fixed<std::uint32_t>();
            if (!player || !playerQuestCount || *playerQuestCount > quests.size()
                || *playerQuestCount > MaximumCanonicalQuestStates - totalQuestStates)
                return std::nullopt;
            totalQuestStates += *playerQuestCount;
            CanonicalPlayerQuestJournalState state{ *player };
            state.quests.reserve(*playerQuestCount);
            for (std::uint32_t questIndex = 0; questIndex < *playerQuestCount; ++questIndex)
            {
                const auto id = readStrong<QuestId>(reader);
                const auto stage = readStrong<QuestStage>(reader);
                const auto questRevision = readStrong<QuestRevision>(reader);
                const auto tick = readStrong<ServerTick>(reader);
                if (!id || !stage || !questRevision || !tick)
                    return std::nullopt;
                state.quests.push_back({ *id, *stage, *questRevision, *tick });
            }
            const auto entryCount = reader.fixed<std::uint32_t>();
            if (!entryCount || *entryCount > journal.size()
                || *entryCount > MaximumCanonicalJournalEntries - totalJournalEntries)
                return std::nullopt;
            totalJournalEntries += *entryCount;
            state.journal.reserve(*entryCount);
            for (std::uint32_t entryIndex = 0; entryIndex < *entryCount; ++entryIndex)
            {
                const auto id = readStrong<JournalEntryId>(reader);
                const auto entryRevision = readStrong<JournalRevision>(reader);
                const auto tick = readStrong<ServerTick>(reader);
                if (!id || !entryRevision || !tick)
                    return std::nullopt;
                state.journal.push_back({ *id, *entryRevision, *tick });
            }
            const auto journalRevision = readStrong<JournalRevision>(reader);
            const auto tick = readStrong<ServerTick>(reader);
            if (!journalRevision || !tick)
                return std::nullopt;
            state.journalRevision = *journalRevision;
            state.lastJournalChangeTick = *tick;
            players.push_back(std::move(state));
        }
        bool hasFactionDialogue = false;
        if (!readBool(reader, hasFactionDialogue))
            return std::nullopt;
        if (!hasFactionDialogue)
            return CanonicalWorldState::create(time, globals, std::move(*catalog), players);
        const auto factionManifestBytes = reader.bytes(ContentManifestIdBytes);
        const auto factionCount = reader.fixed<std::uint32_t>();
        if (!factionManifestBytes || !factionCount || *factionCount > MaximumFactionCatalogEntries)
            return std::nullopt;
        const auto factionManifest = ContentManifestId::fromBytes(*factionManifestBytes);
        if (!factionManifest)
            return std::nullopt;
        std::vector<FactionCatalogEntry> factions;
        factions.reserve(*factionCount);
        std::size_t totalRanks = 0;
        for (std::uint32_t index = 0; index < *factionCount; ++index)
        {
            const auto id = readStrong<FactionId>(reader);
            const auto rankCount = reader.fixed<std::uint32_t>();
            if (!id || !rankCount || *rankCount == 0 || *rankCount > MaximumFactionRanksPerFaction
                || *rankCount > MaximumFactionCatalogRanks - totalRanks)
                return std::nullopt;
            totalRanks += *rankCount;
            std::vector<FactionRank> ranks;
            ranks.reserve(*rankCount);
            for (std::uint32_t rankIndex = 0; rankIndex < *rankCount; ++rankIndex)
            {
                const auto rank = readStrong<FactionRank>(reader);
                if (!rank)
                    return std::nullopt;
                ranks.push_back(*rank);
            }
            factions.push_back({ *id, std::move(ranks) });
        }
        const auto choiceCount = reader.fixed<std::uint32_t>();
        if (!choiceCount || *choiceCount > MaximumDialogueChoiceCatalogEntries)
            return std::nullopt;
        std::vector<DialogueChoiceCatalogEntry> choices;
        choices.reserve(*choiceCount);
        for (std::uint32_t index = 0; index < *choiceCount; ++index)
        {
            const auto id = readStrong<DialogueChoiceId>(reader);
            bool hasRequiredFaction = false;
            if (!id || !readBool(reader, hasRequiredFaction))
                return std::nullopt;
            std::optional<FactionId> requiredFaction;
            if (hasRequiredFaction)
            {
                requiredFaction = readStrong<FactionId>(reader);
                if (!requiredFaction)
                    return std::nullopt;
            }
            const auto minimumRank = readStrong<FactionRank>(reader);
            const auto minimumReputation = reader.fixed<std::int32_t>();
            if (!minimumRank || !minimumReputation)
                return std::nullopt;
            choices.push_back({ *id, requiredFaction, *minimumRank, *minimumReputation });
        }
        auto factionCatalog = FactionDialogueCatalog::create(*factionManifest, factions, choices);
        const auto factionPlayerCount = reader.fixed<std::uint32_t>();
        if (!factionCatalog || !factionPlayerCount || *factionPlayerCount > MaximumPlayerFactionStates)
            return std::nullopt;
        std::vector<CanonicalPlayerFactionState> playerFactions;
        playerFactions.reserve(*factionPlayerCount);
        std::size_t totalFactionStates = 0;
        for (std::uint32_t playerIndex = 0; playerIndex < *factionPlayerCount; ++playerIndex)
        {
            const auto player = readStrong<PlayerId>(reader);
            const auto stateCount = reader.fixed<std::uint32_t>();
            if (!player || !stateCount || *stateCount > factions.size()
                || *stateCount > MaximumCanonicalFactionStates - totalFactionStates)
                return std::nullopt;
            totalFactionStates += *stateCount;
            CanonicalPlayerFactionState state{ *player };
            state.factions.reserve(*stateCount);
            for (std::uint32_t stateIndex = 0; stateIndex < *stateCount; ++stateIndex)
            {
                const auto id = readStrong<FactionId>(reader);
                bool hasRank = false;
                if (!id || !readBool(reader, hasRank))
                    return std::nullopt;
                std::optional<FactionRank> rank;
                if (hasRank)
                {
                    rank = readStrong<FactionRank>(reader);
                    if (!rank)
                        return std::nullopt;
                }
                const auto membershipRevision = readStrong<FactionMembershipRevision>(reader);
                const auto membershipTick = readStrong<ServerTick>(reader);
                const auto reputation = reader.fixed<std::int32_t>();
                const auto reputationRevision = readStrong<FactionReputationRevision>(reader);
                const auto reputationTick = readStrong<ServerTick>(reader);
                if (!membershipRevision || !membershipTick || !reputation || !reputationRevision || !reputationTick)
                    return std::nullopt;
                state.factions.push_back({ *id, rank, *membershipRevision, *membershipTick, *reputation,
                    *reputationRevision, *reputationTick });
            }
            playerFactions.push_back(std::move(state));
        }
        bool hasWeather = false;
        if (!readBool(reader, hasWeather))
            return std::nullopt;
        if (!hasWeather)
            return CanonicalWorldState::create(
                time, globals, std::move(*catalog), std::move(*factionCatalog), players, playerFactions);
        const auto weatherManifestBytes = reader.bytes(ContentManifestIdBytes);
        const auto weatherCount = reader.fixed<std::uint32_t>();
        if (!weatherManifestBytes || !weatherCount || *weatherCount == 0 || *weatherCount > MaximumWeatherIdentities)
            return std::nullopt;
        const auto weatherManifest = ContentManifestId::fromBytes(*weatherManifestBytes);
        if (!weatherManifest)
            return std::nullopt;
        std::vector<WeatherId> weatherIds;
        weatherIds.reserve(*weatherCount);
        for (std::uint32_t index = 0; index < *weatherCount; ++index)
        {
            const auto id = readStrong<WeatherId>(reader);
            if (!id)
                return std::nullopt;
            weatherIds.push_back(*id);
        }
        const auto weatherRegionCount = reader.fixed<std::uint32_t>();
        if (!weatherRegionCount || *weatherRegionCount == 0 || *weatherRegionCount > MaximumWeatherRegions)
            return std::nullopt;
        std::vector<WeatherRegionCatalogEntry> weatherRegions;
        weatherRegions.reserve(*weatherRegionCount);
        std::size_t totalEligibility = 0;
        for (std::uint32_t index = 0; index < *weatherRegionCount; ++index)
        {
            const auto id = readStrong<WeatherRegionId>(reader);
            const auto initial = readStrong<WeatherId>(reader);
            const auto interval = reader.fixed<std::uint64_t>();
            const auto transition = reader.fixed<std::uint64_t>();
            const auto eligibleCount = reader.fixed<std::uint32_t>();
            if (!id || !initial || !interval || !transition || !eligibleCount || *eligibleCount == 0
                || *eligibleCount > MaximumWeatherEligibilityEntries - totalEligibility)
                return std::nullopt;
            totalEligibility += *eligibleCount;
            WeatherRegionCatalogEntry region{ *id, *initial, *interval, *transition, {} };
            region.eligibleWeather.reserve(*eligibleCount);
            for (std::uint32_t weatherIndex = 0; weatherIndex < *eligibleCount; ++weatherIndex)
            {
                const auto weather = readStrong<WeatherId>(reader);
                if (!weather)
                    return std::nullopt;
                region.eligibleWeather.push_back(*weather);
            }
            weatherRegions.push_back(std::move(region));
        }
        auto weatherCatalog = WeatherCatalog::create(*weatherManifest, weatherIds, weatherRegions);
        std::array<std::uint64_t, 4> randomWords{};
        for (auto& word : randomWords)
        {
            const auto value = reader.fixed<std::uint64_t>();
            if (!value)
                return std::nullopt;
            word = *value;
        }
        const auto random = RandomStateV1::fromWords(randomWords[0], randomWords[1], randomWords[2], randomWords[3]);
        const auto weatherLastAdvance = readStrong<ServerTick>(reader);
        const auto stateCount = reader.fixed<std::uint32_t>();
        if (!weatherCatalog || !random || !weatherLastAdvance || !stateCount || *stateCount != *weatherRegionCount)
            return std::nullopt;
        CanonicalWeatherState weatherState{ {}, *random, *weatherLastAdvance };
        weatherState.regions.reserve(*stateCount);
        for (std::uint32_t index = 0; index < *stateCount; ++index)
        {
            const auto region = readStrong<WeatherRegionId>(reader);
            const auto current = readStrong<WeatherId>(reader);
            const auto target = readStrong<WeatherId>(reader);
            const auto transitionStart = readStrong<ServerTick>(reader);
            const auto transitionEnd = readStrong<ServerTick>(reader);
            const auto nextSelection = readStrong<ServerTick>(reader);
            const auto weatherRevision = readStrong<WeatherRevision>(reader);
            const auto weatherLastChange = readStrong<ServerTick>(reader);
            if (!region || !current || !target || !transitionStart || !transitionEnd || !nextSelection
                || !weatherRevision || !weatherLastChange)
                return std::nullopt;
            weatherState.regions.push_back({ *region, *current, *target, *transitionStart, *transitionEnd,
                *nextSelection, *weatherRevision, *weatherLastChange });
        }
        return CanonicalWorldState::create(time, globals, std::move(*catalog), std::move(*factionCatalog),
            std::move(*weatherCatalog), players, playerFactions, std::move(weatherState));
    }

    void writeScriptValue(Writer& writer, const ScriptVariableValue& value)
    {
        writer.fixed(static_cast<std::uint8_t>(value.index()));
        if (const auto* boolean = std::get_if<bool>(&value))
            writer.fixed(static_cast<std::uint8_t>(*boolean));
        else if (const auto* integer = std::get_if<std::int64_t>(&value))
            writer.fixed(*integer);
        else if (const auto* number = std::get_if<double>(&value))
            writer.fixed(std::bit_cast<std::uint64_t>(*number));
        else
        {
            const auto& text = std::get<std::string>(value);
            writer.fixed(static_cast<std::uint32_t>(text.size()));
            writer.bytes({ reinterpret_cast<const std::byte*>(text.data()), text.size() });
        }
    }

    std::optional<ScriptVariableValue> readScriptValue(Reader& reader) noexcept
    {
        const auto type = reader.fixed<std::uint8_t>();
        if (!type || *type > static_cast<std::uint8_t>(ScriptVariableType::String))
            return std::nullopt;
        switch (static_cast<ScriptVariableType>(*type))
        {
            case ScriptVariableType::Boolean:
            {
                const auto value = reader.fixed<std::uint8_t>();
                return value && *value <= 1 ? std::optional<ScriptVariableValue>(*value != 0) : std::nullopt;
            }
            case ScriptVariableType::Integer:
            {
                const auto value = reader.fixed<std::int64_t>();
                return value ? std::optional<ScriptVariableValue>(*value) : std::nullopt;
            }
            case ScriptVariableType::Float:
            {
                const auto bits = reader.fixed<std::uint64_t>();
                if (!bits)
                    return std::nullopt;
                const double value = std::bit_cast<double>(*bits);
                return std::isfinite(value) ? std::optional<ScriptVariableValue>(value) : std::nullopt;
            }
            case ScriptVariableType::String:
            {
                const auto size = reader.fixed<std::uint32_t>();
                if (!size || *size > MaximumScriptStringBytes)
                    return std::nullopt;
                const auto bytes = reader.bytes(*size);
                return bytes ? std::optional<ScriptVariableValue>(
                                   std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()))
                             : std::nullopt;
            }
        }
        return std::nullopt;
    }

    void writeScriptState(Writer& writer, const CanonicalScriptState& state)
    {
        writer.fixed(static_cast<std::uint32_t>(state.variables().size()));
        for (const auto& variable : state.variables())
        {
            writer.fixed(variable.packageId);
            writer.fixed(variable.id.value());
            writeScriptValue(writer, variable.value);
            writer.fixed(variable.revision.value());
            writer.fixed(variable.lastChangeTick.value());
        }
    }

    std::optional<CanonicalScriptState> readScriptState(Reader& reader) noexcept
    {
        const auto count = reader.fixed<std::uint32_t>();
        if (!count || *count > MaximumScriptVariables)
            return std::nullopt;
        std::vector<CanonicalScriptVariableState> variables;
        std::vector<ServerScriptVariableCatalogEntry> catalogEntries;
        variables.reserve(*count);
        catalogEntries.reserve(*count);
        for (std::uint32_t index = 0; index < *count; ++index)
        {
            const auto packageId = reader.fixed<std::uint64_t>();
            const auto idRaw = reader.fixed<std::uint64_t>();
            auto value = readScriptValue(reader);
            const auto revisionRaw = reader.fixed<std::uint64_t>();
            const auto tickRaw = reader.fixed<std::uint64_t>();
            const auto id = idRaw ? ScriptVariableId::fromValue(*idRaw) : std::nullopt;
            const auto revision = revisionRaw ? ScriptStateRevision::fromValue(*revisionRaw) : std::nullopt;
            const auto tick = tickRaw ? ServerTick::fromValue(*tickRaw) : std::nullopt;
            if (!packageId || *packageId == 0 || !id || !value || !revision || !tick)
                return std::nullopt;
            catalogEntries.push_back({ *packageId, *id, *value });
            variables.push_back({ *packageId, *id, std::move(*value), *revision, *tick });
        }
        auto catalog = ServerScriptStateCatalog::create(catalogEntries);
        return catalog ? CanonicalScriptState::restore(*catalog, variables) : std::nullopt;
    }

    void writeDomains(Writer& writer, const std::optional<CanonicalDurableInventoryState>& inventory,
        const std::optional<CanonicalDurableCombatState>& combat,
        const std::optional<CanonicalDurableInteractiveObjectState>& objects,
        const std::optional<CanonicalDurableActorState>& actors, const std::optional<CanonicalWorldState>& world,
        const std::optional<CanonicalScriptState>& scriptState)
    {
        writeBool(writer, inventory.has_value());
        if (inventory)
            writeInventory(writer, *inventory);
        writeBool(writer, combat.has_value());
        if (combat)
            writeCombat(writer, *combat);
        writeBool(writer, objects.has_value());
        if (objects)
            writeObjects(writer, *objects);
        writeBool(writer, actors.has_value());
        if (actors)
            writeActors(writer, *actors);
        writeBool(writer, world.has_value());
        if (world)
            writeWorld(writer, *world);
        writeBool(writer, scriptState.has_value());
        if (scriptState)
            writeScriptState(writer, *scriptState);
    }

    bool readDomains(Reader& reader, std::optional<CanonicalDurableInventoryState>& inventory,
        std::optional<CanonicalDurableCombatState>& combat,
        std::optional<CanonicalDurableInteractiveObjectState>& objects,
        std::optional<CanonicalDurableActorState>& actors, std::optional<CanonicalWorldState>& world,
        std::optional<CanonicalScriptState>& scriptState) noexcept
    {
        bool present = false;
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readInventory(reader);
            if (!value)
                return false;
            inventory = std::move(*value);
        }
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readCombat(reader);
            if (!value)
                return false;
            combat = std::move(*value);
        }
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readObjects(reader);
            if (!value)
                return false;
            objects = std::move(*value);
        }
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readActors(reader);
            if (!value)
                return false;
            actors = std::move(*value);
        }
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readWorld(reader);
            if (!value)
                return false;
            world = std::move(*value);
        }
        if (!readBool(reader, present))
            return false;
        if (present)
        {
            auto value = readScriptState(reader);
            if (!value)
                return false;
            scriptState = std::move(*value);
        }
        return true;
    }

    void writeOrder(Writer& writer, DurableCommandOrder order)
    {
        writer.fixed(static_cast<std::uint8_t>(order.source));
        for (const auto field : order.fields)
            writer.fixed(field);
        writer.fixed(order.disposition);
    }

    std::optional<DurableCommandOrder> readOrder(Reader& reader) noexcept
    {
        DurableCommandOrder result;
        const auto source = reader.fixed<std::uint8_t>();
        if (!source || *source > static_cast<std::uint8_t>(DurableCommandSource::DialogueChoice))
            return std::nullopt;
        result.source = static_cast<DurableCommandSource>(*source);
        for (auto& field : result.fields)
        {
            const auto value = reader.fixed<std::uint64_t>();
            if (!value)
                return std::nullopt;
            field = *value;
        }
        const auto disposition = reader.fixed<std::uint8_t>();
        if (!disposition)
            return std::nullopt;
        result.disposition = *disposition;
        return result;
    }

    std::vector<std::byte> transactionMaterial(CanonicalStateVersion stateVersion, CanonicalRevision revision,
        ServerTick tick, CanonicalChecksum previous, CanonicalChecksum canonical,
        std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
        const std::optional<CanonicalDurableInventoryState>& inventory,
        const std::optional<CanonicalDurableCombatState>& combat,
        const std::optional<CanonicalDurableInteractiveObjectState>& objects,
        const std::optional<CanonicalDurableActorState>& actors, const std::optional<CanonicalWorldState>& world,
        const std::optional<CanonicalScriptState>& scriptState)
    {
        Writer writer;
        writer.fixed(stateVersion.value());
        writer.fixed(revision.value());
        writer.fixed(tick.value());
        writer.fixed(previous.value());
        writer.fixed(canonical.value());
        writer.fixed(static_cast<std::uint32_t>(players.size()));
        for (const auto& player : players)
            writePlayer(writer, player);
        writeDomains(writer, inventory, combat, objects, actors, world, scriptState);
        writer.fixed(static_cast<std::uint32_t>(commands.size()));
        for (const auto order : commands)
            writeOrder(writer, order);
        return writer.take();
    }

    bool validOrders(std::span<const DurableCommandOrder> commands) noexcept
    {
        bool scriptsStarted = false;
        std::optional<std::array<std::uint64_t, 9>> priorClient;
        std::optional<std::array<std::uint64_t, 9>> priorScript;
        for (const auto& command : commands)
        {
            if (command.source == DurableCommandSource::Client)
            {
                if (scriptsStarted
                    || command.disposition > static_cast<std::uint8_t>(CommandDisposition::WaitRestRejected)
                    || command.fields[1] == 0 || command.fields[2] == 0 || command.fields[3] == 0
                    || command.fields[4] == 0 || command.fields[5] == 0
                    || (priorClient && command.fields <= *priorClient))
                    return false;
                priorClient = command.fields;
            }
            else if (command.source == DurableCommandSource::Script)
            {
                scriptsStarted = true;
                if (command.disposition
                        > static_cast<std::uint8_t>(ServerScriptCommandDisposition::InvalidWorldMutation)
                    || command.fields[1] == 0 || command.fields[4] == 0 || command.fields[5] == 0
                    || command.fields[6] != ServerScriptApiVersion || (priorScript && command.fields <= *priorScript))
                    return false;
                priorScript = command.fields;
            }
            else if (command.source == DurableCommandSource::DialogueChoice)
            {
                if (commands.size() != 1 || command.disposition != 0 || command.fields[1] == 0 || command.fields[2] == 0
                    || std::ranges::any_of(std::span(command.fields).subspan(3), [](auto field) { return field != 0; }))
                    return false;
            }
            else
                return false;
        }
        return true;
    }

    bool durablePlayer(std::span<const CanonicalPlayerEntityState> players, PlayerId id) noexcept
    {
        return std::ranges::any_of(players, [id](const auto& player) { return player.playerId() == id; });
    }

    std::optional<CanonicalDurableInventoryState> snapshotInventory(
        const CanonicalInventoryWorld* world, std::span<const CanonicalPlayerEntityState> durablePlayers)
    {
        if (!world)
            return std::nullopt;
        CanonicalDurableInventoryState result;
        for (const auto& player : world->players())
            if (durablePlayer(durablePlayers, player.player))
                result.players.push_back(player);
        result.containers.assign(world->containers().begin(), world->containers().end());
        result.worldItems.assign(world->worldItems().begin(), world->worldItems().end());
        result.nextItemStackId = world->nextItemStackId();
        return result;
    }

    std::optional<CanonicalDurableCombatState> snapshotCombat(
        const CanonicalCombatWorld* world, std::span<const CanonicalPlayerEntityState> durablePlayers)
    {
        if (!world)
            return std::nullopt;
        CanonicalDurableCombatState result;
        for (const auto& player : world->players())
            if (durablePlayer(durablePlayers, player.playerId))
                result.players.push_back(player);
        result.actors.assign(world->actors().begin(), world->actors().end());
        for (auto& actor : result.actors)
            if (actor.aggressionTarget && !durablePlayer(durablePlayers, *actor.aggressionTarget))
                actor.aggressionTarget.reset();
        result.randomWords = world->randomState().words();
        result.lastSimulationTick = world->lastSimulationTick();
        return result;
    }

    std::optional<CanonicalDurableInteractiveObjectState> snapshotObjects(const CanonicalInteractiveObjectWorld* world)
    {
        if (!world)
            return std::nullopt;
        return CanonicalDurableInteractiveObjectState{ std::vector<CanonicalInteractiveObjectState>(
            world->objects().begin(), world->objects().end()) };
    }

    std::optional<CanonicalDurableActorState> snapshotActors(const CanonicalActorWorld* world)
    {
        if (!world)
            return std::nullopt;
        return CanonicalDurableActorState{ std::vector<CanonicalActorEntityState>(
            world->actors().begin(), world->actors().end()) };
    }

    std::optional<CanonicalWorldState> snapshotWorld(
        const CanonicalWorldState* world, std::span<const CanonicalPlayerEntityState> durablePlayers)
    {
        if (!world)
            return std::nullopt;
        if (!world->questJournalCatalog())
            return *world;
        std::vector<CanonicalPlayerQuestJournalState> players;
        for (const auto& player : world->questJournal())
            if (durablePlayer(durablePlayers, player.player))
                players.push_back(player);
        if (!world->factionDialogueCatalog())
            return CanonicalWorldState::create(world->time(), world->globals(), *world->questJournalCatalog(), players);
        std::vector<CanonicalPlayerFactionState> factionPlayers;
        for (const auto& player : world->factionStates())
            if (durablePlayer(durablePlayers, player.player))
                factionPlayers.push_back(player);
        if (world->weatherCatalog() && world->weather())
            return CanonicalWorldState::create(world->time(), world->globals(), *world->questJournalCatalog(),
                *world->factionDialogueCatalog(), *world->weatherCatalog(), players, factionPlayers, *world->weather());
        return CanonicalWorldState::create(world->time(), world->globals(), *world->questJournalCatalog(),
            *world->factionDialogueCatalog(), players, factionPlayers);
    }
}

namespace TES3MP
{
    std::optional<ServerConfigurationId> ServerConfigurationId::fromBytes(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() != 32 || std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{}; }))
            return std::nullopt;
        std::array<std::byte, 32> result{};
        std::ranges::copy(bytes, result.begin());
        return ServerConfigurationId(result);
    }

    std::optional<CanonicalPersistenceIdentity> CanonicalPersistenceIdentity::create(ContentManifestId content,
        ServerConfigurationId configuration, std::span<const ServerScriptPackage> scripts,
        std::span<const PersistenceSeed> seeds) noexcept
    {
        auto empty = ServerScriptStateCatalog::create({});
        return empty ? create(content, configuration, scripts, *empty, seeds) : std::nullopt;
    }

    std::optional<CanonicalPersistenceIdentity> CanonicalPersistenceIdentity::create(ContentManifestId content,
        ServerConfigurationId configuration, std::span<const ServerScriptPackage> scripts,
        const ServerScriptStateCatalog& scriptStateCatalog, std::span<const PersistenceSeed> seeds) noexcept
    try
    {
        if (scripts.size() > MaximumPersistenceScriptPackages || seeds.size() > MaximumPersistenceSeeds)
            return std::nullopt;
        const auto scriptKey = [](ServerScriptPackage package) {
            return std::tuple(package.loadOrder(), package.packageId(), package.packageVersion(), package.apiVersion());
        };
        for (std::size_t index = 0; index < scripts.size(); ++index)
            if (scripts[index].apiVersion() != ServerScriptApiVersion
                || (index != 0 && scriptKey(scripts[index - 1]) >= scriptKey(scripts[index]))
                || std::ranges::any_of(scripts.first(index),
                    [&](const auto package) { return package.packageId() == scripts[index].packageId(); }))
                return std::nullopt;
        for (std::size_t index = 0; index < seeds.size(); ++index)
            if (seeds[index].domain == 0
                || std::ranges::all_of(seeds[index].words, [](auto value) { return value == 0; })
                || (index != 0 && seeds[index - 1].domain >= seeds[index].domain))
                return std::nullopt;
        if (std::ranges::any_of(scriptStateCatalog.entries(), [&](const auto& entry) {
                return std::ranges::none_of(
                    scripts, [&](const auto package) { return package.packageId() == entry.packageId; });
            }))
            return std::nullopt;
        return CanonicalPersistenceIdentity(content, configuration,
            std::vector<ServerScriptPackage>(scripts.begin(), scripts.end()), scriptStateCatalog,
            std::vector<PersistenceSeed>(seeds.begin(), seeds.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    CanonicalChecksum canonicalDurableStateChecksumV1(CanonicalStateVersion stateVersion, ServerTick checkpointTick,
        std::span<const CanonicalPlayerEntityState> players,
        const std::optional<CanonicalDurableInventoryState>& inventory,
        const std::optional<CanonicalDurableCombatState>& combat,
        const std::optional<CanonicalDurableInteractiveObjectState>& objects,
        const std::optional<CanonicalDurableActorState>& actors, const std::optional<CanonicalWorldState>& world,
        const std::optional<CanonicalScriptState>& scriptState) noexcept
    try
    {
        Writer writer;
        writer.fixed(stateVersion.value());
        writer.fixed(checkpointTick.value());
        writer.fixed(static_cast<std::uint32_t>(players.size()));
        for (const auto& player : players)
            writePlayer(writer, player);
        writeDomains(writer, inventory, combat, objects, actors, world, scriptState);
        const auto& bytes = writer.view();
        return crc64Ecma182({ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() });
    }
    catch (...)
    {
        return CanonicalChecksum(0);
    }

    std::optional<CanonicalDurableTick> CanonicalDurableTick::create(CanonicalStateVersion stateVersion,
        CanonicalRevision canonicalRevision, ServerTick checkpointTick,
        std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
        CanonicalChecksum previousTransactionChecksum, const CanonicalInventoryWorld* inventory,
        const CanonicalCombatWorld* combat, const CanonicalInteractiveObjectWorld* objects,
        const CanonicalActorWorld* actors, const CanonicalWorldState* world,
        const CanonicalScriptState* scriptState) noexcept
    {
        return create(stateVersion, canonicalRevision, checkpointTick, players, commands, previousTransactionChecksum,
            snapshotInventory(inventory, players), snapshotCombat(combat, players), snapshotObjects(objects),
            snapshotActors(actors), snapshotWorld(world, players),
            scriptState ? std::optional<CanonicalScriptState>(*scriptState) : std::nullopt);
    }

    std::optional<CanonicalDurableTick> CanonicalDurableTick::create(CanonicalStateVersion stateVersion,
        CanonicalRevision canonicalRevision, ServerTick checkpointTick,
        std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
        CanonicalChecksum previousTransactionChecksum, std::optional<CanonicalDurableInventoryState> inventory,
        std::optional<CanonicalDurableCombatState> combat,
        std::optional<CanonicalDurableInteractiveObjectState> objects, std::optional<CanonicalDurableActorState> actors,
        std::optional<CanonicalWorldState> world, std::optional<CanonicalScriptState> scriptState) noexcept
    try
    {
        if (players.size() > MaximumCanonicalPlayerEntities || commands.size() > MaximumPersistenceCommandsPerTick
            || !validOrders(commands)
            || std::ranges::any_of(players,
                [checkpointTick](const auto& player) { return player.lastSpatialChangeTick() > checkpointTick; })
            || std::ranges::any_of(commands,
                [checkpointTick](const auto& command) { return command.fields[0] != checkpointTick.value(); }))
            return std::nullopt;
        auto candidate = createCanonicalServerState(players, {});
        if (!std::holds_alternative<CanonicalServerState>(candidate))
            return std::nullopt;
        if (inventory)
        {
            if (inventory->players.size() > MaximumInventoryPlayers
                || inventory->containers.size() > MaximumInventoryContainers
                || inventory->worldItems.size() > MaximumWorldItemStacks
                || std::ranges::any_of(
                    inventory->players, [&](const auto& player) { return !durablePlayer(players, player.player); }))
                return std::nullopt;
        }
        if (combat)
        {
            const auto random = RandomStateV1::fromWords(
                combat->randomWords[0], combat->randomWords[1], combat->randomWords[2], combat->randomWords[3]);
            if (!random
                || std::ranges::any_of(
                    combat->players, [&](const auto& player) { return !durablePlayer(players, player.playerId); })
                || !std::holds_alternative<CanonicalCombatWorld>(
                    createCanonicalCombatWorld(combat->players, combat->actors, *random, combat->lastSimulationTick)))
                return std::nullopt;
        }
        if (objects)
        {
            if (std::ranges::any_of(objects->objects,
                    [checkpointTick](const auto& object) {
                        return object.lastChangeTick() > checkpointTick
                            || static_cast<std::uint8_t>(object.doorState())
                            > static_cast<std::uint8_t>(DoorState::Open)
                            || static_cast<std::uint8_t>(object.lockState())
                            > static_cast<std::uint8_t>(LockState::Locked)
                            || static_cast<std::uint8_t>(object.trapState())
                            > static_cast<std::uint8_t>(TrapState::Armed);
                    })
                || !std::holds_alternative<CanonicalInteractiveObjectWorld>(
                    createCanonicalInteractiveObjectWorld(objects->objects)))
                return std::nullopt;
        }
        if (actors)
        {
            const auto actorWorld = createCanonicalActorWorld(actors->actors);
            const auto* restoredActors = std::get_if<CanonicalActorWorld>(&actorWorld);
            const auto* restoredPlayers = std::get_if<CanonicalServerState>(&candidate);
            if (std::ranges::any_of(actors->actors,
                    [checkpointTick](const auto& actor) {
                        return actor.lastChangeTick() > checkpointTick
                            || static_cast<std::uint8_t>(actor.activity())
                            > static_cast<std::uint8_t>(ActorActivity::Wander);
                    })
                || !restoredActors || !restoredPlayers
                || !actorAndPlayerEntityIdsAreDisjoint(*restoredActors, *restoredPlayers))
                return std::nullopt;
        }
        if (combat && actors
            && (combat->actors.size() != actors->actors.size()
                || !std::equal(combat->actors.begin(), combat->actors.end(), actors->actors.begin(),
                    [](const auto& combatActor, const auto& actor) { return combatActor.actorId == actor.actorId(); })))
            return std::nullopt;
        if (combat && std::ranges::any_of(combat->actors, [&](const auto& actor) {
                return actor.aggressionTarget && !durablePlayer(players, *actor.aggressionTarget);
            }))
            return std::nullopt;
        if (world
            && (world->time().lastAdvanceTick > checkpointTick || world->time().lastChangeTick > checkpointTick
                || (world->weather()
                    && (world->weather()->lastAdvanceTick != world->time().lastAdvanceTick
                        || world->weather()->lastAdvanceTick > checkpointTick))
                || std::ranges::any_of(world->globals(),
                    [checkpointTick](const auto& global) { return global.lastChangeTick > checkpointTick; })
                || std::ranges::any_of(world->questJournal(),
                    [&](const auto& player) {
                        return !durablePlayer(players, player.player) || player.lastJournalChangeTick > checkpointTick
                            || std::ranges::any_of(player.quests,
                                [checkpointTick](const auto& quest) { return quest.lastChangeTick > checkpointTick; })
                            || std::ranges::any_of(player.journal,
                                [checkpointTick](const auto& entry) { return entry.changeTick > checkpointTick; });
                    })
                || std::ranges::any_of(world->factionStates(), [&](const auto& player) {
                       return !durablePlayer(players, player.player)
                           || std::ranges::any_of(player.factions, [checkpointTick](const auto& faction) {
                                  return faction.lastMembershipChangeTick > checkpointTick
                                      || faction.lastReputationChangeTick > checkpointTick;
                              });
                   })))
            return std::nullopt;
        if (scriptState && std::ranges::any_of(scriptState->variables(), [checkpointTick](const auto& variable) {
                return variable.lastChangeTick > checkpointTick;
            }))
            return std::nullopt;
        const auto canonical = canonicalDurableStateChecksumV1(
            stateVersion, checkpointTick, players, inventory, combat, objects, actors, world, scriptState);
        auto material
            = transactionMaterial(stateVersion, canonicalRevision, checkpointTick, previousTransactionChecksum,
                canonical, players, commands, inventory, combat, objects, actors, world, scriptState);
        if (material.size() + sizeof(std::uint64_t) > MaximumPersistenceRecordBytes)
            return std::nullopt;
        const auto transaction
            = crc64Ecma182({ reinterpret_cast<const std::uint8_t*>(material.data()), material.size() });
        return CanonicalDurableTick(stateVersion, canonicalRevision, checkpointTick, previousTransactionChecksum,
            canonical, transaction, std::vector<CanonicalPlayerEntityState>(players.begin(), players.end()),
            std::vector<DurableCommandOrder>(commands.begin(), commands.end()), std::move(inventory), std::move(combat),
            std::move(objects), std::move(actors), std::move(world), std::move(scriptState));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalDurablePrefix> CanonicalDurablePrefix::create(
        CanonicalPersistenceIdentity identity, std::vector<CanonicalDurableTick> transactions) noexcept
    try
    {
        if (transactions.size() > MaximumPersistenceTransactions)
            return std::nullopt;
        CanonicalChecksum prior(0);
        std::optional<ServerTick> priorTick;
        CanonicalStateVersion priorVersion = CanonicalStateVersion::initial();
        CanonicalRevision priorRevision = CanonicalRevision::initial();
        for (const auto& transaction : transactions)
        {
            if (transaction.previousTransactionChecksum() != prior
                || (priorTick && transaction.checkpointTick() < *priorTick) || transaction.stateVersion() < priorVersion
                || transaction.canonicalRevision() < priorRevision)
                return std::nullopt;
            auto rebuilt = CanonicalDurableTick::create(transaction.stateVersion(), transaction.canonicalRevision(),
                transaction.checkpointTick(), transaction.players(), transaction.commands(), prior,
                transaction.inventory(), transaction.combat(), transaction.objects(), transaction.actors(),
                transaction.world(), transaction.scriptState());
            if (!rebuilt || rebuilt->transactionChecksum() != transaction.transactionChecksum()
                || rebuilt->canonicalChecksum() != transaction.canonicalChecksum())
                return std::nullopt;
            prior = transaction.transactionChecksum();
            priorTick = transaction.checkpointTick();
            priorVersion = transaction.stateVersion();
            priorRevision = transaction.canonicalRevision();
        }
        return CanonicalDurablePrefix(std::move(identity), std::move(transactions));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::vector<std::byte> encodeCanonicalDurablePrefixV2(const CanonicalDurablePrefix& prefix)
    {
        Writer writer;
        constexpr std::array magic{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'P' }, std::byte{ 'E' },
            std::byte{ 'R' }, std::byte{ 'S' }, std::byte{ 'I' }, std::byte{ 'S' } };
        writer.bytes(magic);
        writer.fixed(CanonicalPersistenceFormatVersion);
        writer.bytes(prefix.identity().contentManifest().bytes());
        writer.bytes(prefix.identity().serverConfiguration().bytes());
        writer.fixed(ServerScriptApiVersion);
        writer.fixed(static_cast<std::uint32_t>(prefix.identity().scripts().size()));
        for (const auto package : prefix.identity().scripts())
        {
            writer.fixed(package.packageId());
            writer.fixed(package.packageVersion());
            writer.fixed(package.loadOrder());
            writer.fixed(package.apiVersion());
        }
        writer.fixed(static_cast<std::uint32_t>(prefix.identity().scriptStateCatalog().entries().size()));
        for (const auto& entry : prefix.identity().scriptStateCatalog().entries())
        {
            writer.fixed(entry.packageId);
            writer.fixed(entry.id.value());
            writeScriptValue(writer, entry.initialValue);
        }
        writer.fixed(static_cast<std::uint32_t>(prefix.identity().seeds().size()));
        for (const auto& seed : prefix.identity().seeds())
        {
            writer.fixed(seed.domain);
            for (const auto word : seed.words)
                writer.fixed(word);
        }
        writer.fixed(static_cast<std::uint32_t>(prefix.transactions().size()));
        for (const auto& transaction : prefix.transactions())
        {
            auto material = transactionMaterial(transaction.stateVersion(), transaction.canonicalRevision(),
                transaction.checkpointTick(), transaction.previousTransactionChecksum(),
                transaction.canonicalChecksum(), transaction.players(), transaction.commands(), transaction.inventory(),
                transaction.combat(), transaction.objects(), transaction.actors(), transaction.world(),
                transaction.scriptState());
            writer.fixed(static_cast<std::uint32_t>(material.size() + sizeof(std::uint64_t)));
            writer.fixed(transaction.transactionChecksum().value());
            writer.bytes(material);
        }
        const auto checksum
            = crc64Ecma182({ reinterpret_cast<const std::uint8_t*>(writer.view().data()), writer.view().size() });
        writer.fixed(checksum.value());
        return writer.take();
    }

    CanonicalPersistenceDecodeResult decodeCanonicalDurablePrefix(
        std::span<const std::byte> bytes, const CanonicalPersistenceIdentity& expected) noexcept
    try
    {
        if (bytes.size() > MaximumPersistenceFileBytes)
            return CanonicalPersistenceDecodeError::TooLarge;
        if (bytes.size() < 8 + 2 + 32 + 32 + 4 + 4 + 4 + 4 + 8)
            return CanonicalPersistenceDecodeError::Truncated;
        const auto storedFileChecksumBytes = bytes.last(sizeof(std::uint64_t));
        Reader checksumReader(storedFileChecksumBytes);
        const auto storedFileChecksum = checksumReader.fixed<std::uint64_t>();
        const auto calculatedFileChecksum = crc64Ecma182(
            { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() - sizeof(std::uint64_t) });
        if (!storedFileChecksum || *storedFileChecksum != calculatedFileChecksum.value())
            return CanonicalPersistenceDecodeError::Corrupted;
        Reader reader(bytes.first(bytes.size() - sizeof(std::uint64_t)));
        constexpr std::array magic{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'P' }, std::byte{ 'E' },
            std::byte{ 'R' }, std::byte{ 'S' }, std::byte{ 'I' }, std::byte{ 'S' } };
        const auto foundMagic = reader.bytes(magic.size());
        if (!foundMagic || !std::ranges::equal(*foundMagic, magic))
            return CanonicalPersistenceDecodeError::Malformed;
        const auto version = reader.fixed<std::uint16_t>();
        if (!version)
            return CanonicalPersistenceDecodeError::Truncated;
        if (*version != CanonicalPersistenceFormatVersion)
            return CanonicalPersistenceDecodeError::UnsupportedVersion;
        const auto manifestBytes = reader.bytes(ContentManifestIdBytes);
        const auto configuration = reader.bytes(32);
        const auto api = reader.fixed<std::uint32_t>();
        const auto scriptCount = reader.fixed<std::uint32_t>();
        if (!manifestBytes || !configuration || !api || !scriptCount || *scriptCount > MaximumPersistenceScriptPackages)
            return CanonicalPersistenceDecodeError::Malformed;
        const auto manifest = ContentManifestId::fromBytes(*manifestBytes);
        const auto configurationId = configuration ? ServerConfigurationId::fromBytes(*configuration) : std::nullopt;
        std::vector<ServerScriptPackage> scripts;
        scripts.reserve(*scriptCount);
        for (std::uint32_t index = 0; index < *scriptCount; ++index)
        {
            const auto id = reader.fixed<std::uint64_t>();
            const auto packageVersion = reader.fixed<std::uint32_t>();
            const auto loadOrder = reader.fixed<std::uint32_t>();
            const auto packageApi = reader.fixed<std::uint32_t>();
            if (!id || !packageVersion || !loadOrder || !packageApi)
                return CanonicalPersistenceDecodeError::Truncated;
            auto package = ServerScriptPackage::create(*id, *packageVersion, *loadOrder, *packageApi);
            if (!package)
                return CanonicalPersistenceDecodeError::Malformed;
            scripts.push_back(*package);
        }
        const auto catalogCount = reader.fixed<std::uint32_t>();
        if (!catalogCount || *catalogCount > MaximumScriptVariables)
            return CanonicalPersistenceDecodeError::Malformed;
        std::vector<ServerScriptVariableCatalogEntry> catalogEntries;
        catalogEntries.reserve(*catalogCount);
        for (std::uint32_t index = 0; index < *catalogCount; ++index)
        {
            const auto packageId = reader.fixed<std::uint64_t>();
            const auto idRaw = reader.fixed<std::uint64_t>();
            auto value = readScriptValue(reader);
            const auto id = idRaw ? ScriptVariableId::fromValue(*idRaw) : std::nullopt;
            if (!packageId || *packageId == 0 || !id || !value)
                return CanonicalPersistenceDecodeError::Malformed;
            catalogEntries.push_back({ *packageId, *id, std::move(*value) });
        }
        auto scriptStateCatalog = ServerScriptStateCatalog::create(catalogEntries);
        if (!scriptStateCatalog)
            return CanonicalPersistenceDecodeError::Malformed;
        const auto seedCount = reader.fixed<std::uint32_t>();
        if (!seedCount || *seedCount > MaximumPersistenceSeeds)
            return CanonicalPersistenceDecodeError::Malformed;
        std::vector<PersistenceSeed> seeds;
        seeds.reserve(*seedCount);
        for (std::uint32_t index = 0; index < *seedCount; ++index)
        {
            PersistenceSeed seed;
            const auto domain = reader.fixed<std::uint64_t>();
            if (!domain)
                return CanonicalPersistenceDecodeError::Truncated;
            seed.domain = *domain;
            for (auto& word : seed.words)
            {
                const auto value = reader.fixed<std::uint64_t>();
                if (!value)
                    return CanonicalPersistenceDecodeError::Truncated;
                word = *value;
            }
            seeds.push_back(seed);
        }
        if (!manifest || !configurationId || *api != ServerScriptApiVersion)
            return CanonicalPersistenceDecodeError::Malformed;
        auto identity
            = CanonicalPersistenceIdentity::create(*manifest, *configurationId, scripts, *scriptStateCatalog, seeds);
        if (!identity)
            return CanonicalPersistenceDecodeError::Malformed;
        if (*identity != expected)
            return CanonicalPersistenceDecodeError::IdentityMismatch;
        const auto transactionCount = reader.fixed<std::uint32_t>();
        if (!transactionCount || *transactionCount > MaximumPersistenceTransactions)
            return CanonicalPersistenceDecodeError::Malformed;
        std::vector<CanonicalDurableTick> transactions;
        transactions.reserve(*transactionCount);
        for (std::uint32_t index = 0; index < *transactionCount; ++index)
        {
            const auto recordLength = reader.fixed<std::uint32_t>();
            if (!recordLength || *recordLength < sizeof(std::uint64_t) || *recordLength > MaximumPersistenceRecordBytes)
                return CanonicalPersistenceDecodeError::Malformed;
            const auto record = reader.bytes(*recordLength);
            if (!record)
                return CanonicalPersistenceDecodeError::Truncated;
            Reader recordReader(*record);
            const auto storedChecksum = recordReader.fixed<std::uint64_t>();
            const auto stateVersionRaw = recordReader.fixed<std::uint64_t>();
            const auto revisionRaw = recordReader.fixed<std::uint64_t>();
            const auto tickRaw = recordReader.fixed<std::uint64_t>();
            const auto previousRaw = recordReader.fixed<std::uint64_t>();
            const auto canonicalRaw = recordReader.fixed<std::uint64_t>();
            const auto playerCount = recordReader.fixed<std::uint32_t>();
            if (!storedChecksum || !stateVersionRaw || !revisionRaw || !tickRaw || !previousRaw || !canonicalRaw
                || !playerCount || *playerCount > MaximumCanonicalPlayerEntities)
                return CanonicalPersistenceDecodeError::Malformed;
            std::vector<CanonicalPlayerEntityState> players;
            players.reserve(*playerCount);
            for (std::uint32_t playerIndex = 0; playerIndex < *playerCount; ++playerIndex)
            {
                auto player = readPlayer(recordReader);
                if (!player)
                    return CanonicalPersistenceDecodeError::Malformed;
                players.push_back(*player);
            }
            std::optional<CanonicalDurableInventoryState> inventory;
            std::optional<CanonicalDurableCombatState> combat;
            std::optional<CanonicalDurableInteractiveObjectState> objects;
            std::optional<CanonicalDurableActorState> actors;
            std::optional<CanonicalWorldState> world;
            std::optional<CanonicalScriptState> scriptState;
            if (!readDomains(recordReader, inventory, combat, objects, actors, world, scriptState))
                return CanonicalPersistenceDecodeError::Malformed;
            const auto commandCount = recordReader.fixed<std::uint32_t>();
            if (!commandCount || *commandCount > MaximumPersistenceCommandsPerTick)
                return CanonicalPersistenceDecodeError::Malformed;
            std::vector<DurableCommandOrder> commands;
            commands.reserve(*commandCount);
            for (std::uint32_t commandIndex = 0; commandIndex < *commandCount; ++commandIndex)
            {
                auto command = readOrder(recordReader);
                if (!command)
                    return CanonicalPersistenceDecodeError::Malformed;
                commands.push_back(*command);
            }
            const auto stateVersion = CanonicalStateVersion::fromValue(*stateVersionRaw);
            const auto revision = CanonicalRevision::fromValue(*revisionRaw);
            const auto tick = ServerTick::fromValue(*tickRaw);
            if (!stateVersion || !revision || !tick || recordReader.remaining() != 0)
                return CanonicalPersistenceDecodeError::Malformed;
            if ((!scriptState && !identity->scriptStateCatalog().entries().empty())
                || (scriptState
                    && !CanonicalScriptState::restore(identity->scriptStateCatalog(), scriptState->variables())))
                return CanonicalPersistenceDecodeError::Malformed;
            auto transaction = CanonicalDurableTick::create(*stateVersion, *revision, *tick, players, commands,
                CanonicalChecksum(*previousRaw), std::move(inventory), std::move(combat), std::move(objects),
                std::move(actors), std::move(world), std::move(scriptState));
            if (!transaction || transaction->canonicalChecksum().value() != *canonicalRaw
                || transaction->transactionChecksum().value() != *storedChecksum)
                return CanonicalPersistenceDecodeError::Corrupted;
            transactions.push_back(std::move(*transaction));
        }
        if (reader.remaining() != 0)
            return CanonicalPersistenceDecodeError::Malformed;
        auto prefix = CanonicalDurablePrefix::create(std::move(*identity), std::move(transactions));
        return prefix ? CanonicalPersistenceDecodeResult(std::move(*prefix))
                      : CanonicalPersistenceDecodeResult(CanonicalPersistenceDecodeError::Corrupted);
    }
    catch (...)
    {
        return CanonicalPersistenceDecodeError::Malformed;
    }

    bool replayCanonicalDurablePrefix(const CanonicalDurablePrefix& prefix, CanonicalReplayStep step) noexcept
    try
    {
        if (!step)
            return false;
        const auto* checkpoint = prefix.checkpoint();
        if (!checkpoint)
            return true;
        auto currentResult = createCanonicalServerState(checkpoint->players(), {});
        auto* checkpointPlayers = std::get_if<CanonicalServerState>(&currentResult);
        if (!checkpointPlayers
            || canonicalDurableStateChecksumV1(checkpoint->stateVersion(), checkpoint->checkpointTick(),
                   checkpoint->players(), checkpoint->inventory(), checkpoint->combat(), checkpoint->objects(),
                   checkpoint->actors(), checkpoint->world(), checkpoint->scriptState())
                != checkpoint->canonicalChecksum())
            return false;
        CanonicalReplayState state{ std::move(*checkpointPlayers), checkpoint->inventory(), checkpoint->combat(),
            checkpoint->objects(), checkpoint->actors(), checkpoint->world(), checkpoint->scriptState() };
        for (const auto& transaction : prefix.journal())
        {
            auto replayed = step(state, transaction.commands(), transaction.checkpointTick());
            auto* next = std::get_if<CanonicalReplayState>(&replayed);
            if (!next || !next->players.activeSessions().empty()
                || canonicalDurableStateChecksumV1(transaction.stateVersion(), transaction.checkpointTick(),
                       next->players.players(), next->inventory, next->combat, next->objects, next->actors, next->world,
                       next->scriptState)
                    != transaction.canonicalChecksum())
                return false;
            state = std::move(*next);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
