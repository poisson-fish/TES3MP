#ifndef TES3MP_CANONICAL_PERSISTENCE_HPP
#define TES3MP_CANONICAL_PERSISTENCE_HPP

#include "canonical_publication.hpp"
#include "server_scripting.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::uint16_t CanonicalPersistenceFormatVersion = 1;
    inline constexpr std::size_t MaximumPersistenceScriptPackages = 64;
    inline constexpr std::size_t MaximumPersistenceSeeds = 64;
    inline constexpr std::size_t MaximumPersistenceCommandsPerTick
        = MaximumServerCommandsPerTick + MaximumServerScriptCommandsPerTick;
    inline constexpr std::size_t MaximumPersistenceTransactions = 1'000'000;
    inline constexpr std::size_t MaximumPersistenceRecordBytes = 2 * 1024 * 1024;
    inline constexpr std::size_t MaximumPersistenceFileBytes = 256 * 1024 * 1024;

    class ServerConfigurationId
    {
    public:
        static std::optional<ServerConfigurationId> fromBytes(std::span<const std::byte> bytes) noexcept;

        constexpr std::span<const std::byte, 32> bytes() const noexcept { return mBytes; }
        friend constexpr bool operator==(ServerConfigurationId, ServerConfigurationId) noexcept = default;

    private:
        constexpr explicit ServerConfigurationId(std::array<std::byte, 32> bytes) noexcept
            : mBytes(bytes)
        {
        }
        std::array<std::byte, 32> mBytes{};
    };

    struct PersistenceSeed
    {
        std::uint64_t domain = 0;
        std::array<std::uint64_t, 4> words{};
        friend constexpr bool operator==(PersistenceSeed, PersistenceSeed) noexcept = default;
    };

    class CanonicalPersistenceIdentity
    {
    public:
        static std::optional<CanonicalPersistenceIdentity> create(ContentManifestId content,
            ServerConfigurationId configuration, std::span<const ServerScriptPackage> scripts,
            std::span<const PersistenceSeed> seeds) noexcept;

        constexpr ContentManifestId contentManifest() const noexcept { return mContent; }
        constexpr ServerConfigurationId serverConfiguration() const noexcept { return mConfiguration; }
        std::span<const ServerScriptPackage> scripts() const noexcept { return mScripts; }
        std::span<const PersistenceSeed> seeds() const noexcept { return mSeeds; }
        friend bool operator==(const CanonicalPersistenceIdentity&, const CanonicalPersistenceIdentity&) noexcept
            = default;

    private:
        CanonicalPersistenceIdentity(ContentManifestId content, ServerConfigurationId configuration,
            std::vector<ServerScriptPackage> scripts, std::vector<PersistenceSeed> seeds) noexcept
            : mContent(content)
            , mConfiguration(configuration)
            , mScripts(std::move(scripts))
            , mSeeds(std::move(seeds))
        {
        }

        ContentManifestId mContent;
        ServerConfigurationId mConfiguration;
        std::vector<ServerScriptPackage> mScripts;
        std::vector<PersistenceSeed> mSeeds;
    };

    enum class DurableCommandSource : std::uint8_t
    {
        Client,
        Script,
    };

    // The nine fields are a lossless normalized ordering key. Client commands use
    // tick, ingress ordinal, session, generation, sequence, and command id.
    // Script commands use the complete ServerScriptCommandOrder tuple.
    struct DurableCommandOrder
    {
        DurableCommandSource source = DurableCommandSource::Client;
        std::array<std::uint64_t, 9> fields{};
        std::uint8_t disposition = 0;
        friend constexpr bool operator==(DurableCommandOrder, DurableCommandOrder) noexcept = default;
    };

    class CanonicalDurableTick
    {
    public:
        static std::optional<CanonicalDurableTick> create(CanonicalStateVersion stateVersion,
            CanonicalRevision canonicalRevision, ServerTick checkpointTick,
            std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
            CanonicalChecksum previousTransactionChecksum = CanonicalChecksum(0)) noexcept;

        constexpr CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        constexpr ServerTick checkpointTick() const noexcept { return mCheckpointTick; }
        constexpr CanonicalChecksum previousTransactionChecksum() const noexcept
        {
            return mPreviousTransactionChecksum;
        }
        constexpr CanonicalChecksum canonicalChecksum() const noexcept { return mCanonicalChecksum; }
        constexpr CanonicalChecksum transactionChecksum() const noexcept { return mTransactionChecksum; }
        std::span<const CanonicalPlayerEntityState> players() const noexcept { return mPlayers; }
        std::span<const DurableCommandOrder> commands() const noexcept { return mCommands; }
        friend bool operator==(const CanonicalDurableTick&, const CanonicalDurableTick&) noexcept = default;

    private:
        CanonicalDurableTick(CanonicalStateVersion stateVersion, CanonicalRevision canonicalRevision,
            ServerTick checkpointTick, CanonicalChecksum previousTransactionChecksum,
            CanonicalChecksum canonicalChecksum, CanonicalChecksum transactionChecksum,
            std::vector<CanonicalPlayerEntityState> players, std::vector<DurableCommandOrder> commands) noexcept
            : mStateVersion(stateVersion)
            , mCanonicalRevision(canonicalRevision)
            , mCheckpointTick(checkpointTick)
            , mPreviousTransactionChecksum(previousTransactionChecksum)
            , mCanonicalChecksum(canonicalChecksum)
            , mTransactionChecksum(transactionChecksum)
            , mPlayers(std::move(players))
            , mCommands(std::move(commands))
        {
        }

        CanonicalStateVersion mStateVersion;
        CanonicalRevision mCanonicalRevision;
        ServerTick mCheckpointTick;
        CanonicalChecksum mPreviousTransactionChecksum;
        CanonicalChecksum mCanonicalChecksum;
        CanonicalChecksum mTransactionChecksum;
        std::vector<CanonicalPlayerEntityState> mPlayers;
        std::vector<DurableCommandOrder> mCommands;
    };

    class CanonicalDurablePrefix
    {
    public:
        static std::optional<CanonicalDurablePrefix> create(
            CanonicalPersistenceIdentity identity, std::vector<CanonicalDurableTick> transactions) noexcept;

        const CanonicalPersistenceIdentity& identity() const noexcept { return mIdentity; }
        std::span<const CanonicalDurableTick> transactions() const noexcept { return mTransactions; }
        const CanonicalDurableTick* latest() const noexcept
        {
            return mTransactions.empty() ? nullptr : &mTransactions.back();
        }
        friend bool operator==(const CanonicalDurablePrefix&, const CanonicalDurablePrefix&) noexcept = default;

    private:
        CanonicalDurablePrefix(
            CanonicalPersistenceIdentity identity, std::vector<CanonicalDurableTick> transactions) noexcept
            : mIdentity(std::move(identity))
            , mTransactions(std::move(transactions))
        {
        }
        CanonicalPersistenceIdentity mIdentity;
        std::vector<CanonicalDurableTick> mTransactions;
    };

    enum class CanonicalPersistenceDecodeError : std::uint8_t
    {
        Truncated,
        TooLarge,
        UnsupportedVersion,
        Corrupted,
        Malformed,
        IdentityMismatch,
    };

    using CanonicalPersistenceDecodeResult = std::variant<CanonicalDurablePrefix, CanonicalPersistenceDecodeError>;

    std::vector<std::byte> encodeCanonicalDurablePrefixV1(const CanonicalDurablePrefix& prefix);
    CanonicalPersistenceDecodeResult decodeCanonicalDurablePrefixV1(
        std::span<const std::byte> bytes, const CanonicalPersistenceIdentity& expected) noexcept;

    enum class CanonicalDurabilityResult : std::uint8_t
    {
        NotConfigured,
        Committed,
        Rejected,
        Failed,
    };

    class CanonicalDurabilityPort
    {
    public:
        virtual ~CanonicalDurabilityPort() = default;
        // Committed is the durability acknowledgement point. The reducer has
        // not installed or published the candidate when this call begins.
        virtual CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision canonicalRevision, std::span<const DurableCommandOrder> commands) noexcept = 0;
    };

    using CanonicalReplayStep = std::variant<CanonicalServerState, CanonicalChecksum> (*)(
        const CanonicalServerState&, std::span<const DurableCommandOrder>, ServerTick);
    bool replayCanonicalDurablePrefix(const CanonicalDurablePrefix& prefix, CanonicalReplayStep step) noexcept;
}

#endif
