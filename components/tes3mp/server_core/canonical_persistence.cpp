#include <tes3mp/canonical_persistence.hpp>

#include <algorithm>
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
        if (!source || *source > static_cast<std::uint8_t>(DurableCommandSource::Script))
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
        std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands)
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
                    || command.disposition > static_cast<std::uint8_t>(CommandDisposition::CombatRejected)
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
                        > static_cast<std::uint8_t>(ServerScriptCommandDisposition::EntityRevisionExhausted)
                    || command.fields[1] == 0 || command.fields[4] == 0 || command.fields[5] == 0
                    || command.fields[6] != ServerScriptApiVersion || (priorScript && command.fields <= *priorScript))
                    return false;
                priorScript = command.fields;
            }
            else
                return false;
        }
        return true;
    }

    CanonicalChecksum rootChecksum(
        CanonicalStateVersion version, ServerTick tick, std::span<const CanonicalPlayerEntityState> players) noexcept
    {
        auto state = createCanonicalServerState(players, {});
        const auto* value = std::get_if<CanonicalServerState>(&state);
        return value ? canonicalStateChecksumV2(version, tick, *value) : CanonicalChecksum(0);
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
    try
    {
        if (scripts.size() > MaximumPersistenceScriptPackages || seeds.size() > MaximumPersistenceSeeds)
            return std::nullopt;
        const auto scriptKey = [](ServerScriptPackage package) {
            return std::tuple(package.loadOrder(), package.packageId(), package.packageVersion(), package.apiVersion());
        };
        for (std::size_t index = 0; index < scripts.size(); ++index)
            if (scripts[index].apiVersion() != ServerScriptApiVersion
                || (index != 0 && scriptKey(scripts[index - 1]) >= scriptKey(scripts[index])))
                return std::nullopt;
        for (std::size_t index = 0; index < seeds.size(); ++index)
            if (seeds[index].domain == 0
                || std::ranges::all_of(seeds[index].words, [](auto value) { return value == 0; })
                || (index != 0 && seeds[index - 1].domain >= seeds[index].domain))
                return std::nullopt;
        return CanonicalPersistenceIdentity(content, configuration,
            std::vector<ServerScriptPackage>(scripts.begin(), scripts.end()),
            std::vector<PersistenceSeed>(seeds.begin(), seeds.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalDurableTick> CanonicalDurableTick::create(CanonicalStateVersion stateVersion,
        CanonicalRevision canonicalRevision, ServerTick checkpointTick,
        std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
        CanonicalChecksum previousTransactionChecksum) noexcept
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
        const auto canonical = rootChecksum(stateVersion, checkpointTick, players);
        auto material = transactionMaterial(
            stateVersion, canonicalRevision, checkpointTick, previousTransactionChecksum, canonical, players, commands);
        const auto transaction
            = crc64Ecma182({ reinterpret_cast<const std::uint8_t*>(material.data()), material.size() });
        return CanonicalDurableTick(stateVersion, canonicalRevision, checkpointTick, previousTransactionChecksum,
            canonical, transaction, std::vector<CanonicalPlayerEntityState>(players.begin(), players.end()),
            std::vector<DurableCommandOrder>(commands.begin(), commands.end()));
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
                transaction.checkpointTick(), transaction.players(), transaction.commands(), prior);
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

    std::vector<std::byte> encodeCanonicalDurablePrefixV1(const CanonicalDurablePrefix& prefix)
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
                transaction.canonicalChecksum(), transaction.players(), transaction.commands());
            writer.fixed(static_cast<std::uint32_t>(material.size() + sizeof(std::uint64_t)));
            writer.fixed(transaction.transactionChecksum().value());
            writer.bytes(material);
        }
        const auto checksum
            = crc64Ecma182({ reinterpret_cast<const std::uint8_t*>(writer.view().data()), writer.view().size() });
        writer.fixed(checksum.value());
        return writer.take();
    }

    CanonicalPersistenceDecodeResult decodeCanonicalDurablePrefixV1(
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
        auto identity = CanonicalPersistenceIdentity::create(*manifest, *configurationId, scripts, seeds);
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
            auto transaction = CanonicalDurableTick::create(
                *stateVersion, *revision, *tick, players, commands, CanonicalChecksum(*previousRaw));
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
        auto currentResult = createCanonicalServerState({}, {});
        auto* current = std::get_if<CanonicalServerState>(&currentResult);
        if (!current)
            return false;
        CanonicalServerState state = std::move(*current);
        for (const auto& transaction : prefix.transactions())
        {
            auto replayed = step(state, transaction.commands(), transaction.checkpointTick());
            auto* next = std::get_if<CanonicalServerState>(&replayed);
            if (!next || !next->activeSessions().empty()
                || canonicalStateChecksumV2(transaction.stateVersion(), transaction.checkpointTick(), *next)
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
