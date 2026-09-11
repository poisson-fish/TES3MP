#ifndef TES3MP_CANONICAL_PERSISTENCE_HPP
#define TES3MP_CANONICAL_PERSISTENCE_HPP

#include "actor_simulation.hpp"
#include "canonical_publication.hpp"
#include "combat_world.hpp"
#include "interactive_object_world.hpp"
#include "inventory_world.hpp"
#include "server_scripting.hpp"
#include "world_state.hpp"

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
    inline constexpr std::uint16_t CanonicalPersistenceFormatVersion = 2;
    inline constexpr std::size_t MaximumPersistenceScriptPackages = 64;
    inline constexpr std::size_t MaximumPersistenceSeeds = 64;
    inline constexpr std::size_t MaximumPersistenceCommandsPerTick
        = MaximumServerCommandsPerTick + MaximumServerScriptCommandsPerTick;
    inline constexpr std::size_t MaximumPersistenceJournalTransactions = 32;
    inline constexpr std::size_t MaximumPersistenceTransactions = MaximumPersistenceJournalTransactions + 1;
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

    struct CanonicalDurableInventoryState
    {
        std::vector<CanonicalPlayerInventoryState> players;
        std::vector<CanonicalContainerInventoryState> containers;
        std::vector<CanonicalWorldItemState> worldItems;
        std::optional<ItemStackId> nextItemStackId;

        friend bool operator==(const CanonicalDurableInventoryState&, const CanonicalDurableInventoryState&) noexcept
            = default;
    };

    struct CanonicalDurableCombatState
    {
        std::vector<CanonicalPlayerCombatState> players;
        std::vector<CanonicalActorCombatState> actors;
        std::array<std::uint64_t, 4> randomWords{};
        std::optional<ServerTick> lastSimulationTick;

        friend bool operator==(const CanonicalDurableCombatState&, const CanonicalDurableCombatState&) noexcept
            = default;
    };

    struct CanonicalDurableInteractiveObjectState
    {
        std::vector<CanonicalInteractiveObjectState> objects;

        friend bool operator==(const CanonicalDurableInteractiveObjectState&,
            const CanonicalDurableInteractiveObjectState&) noexcept = default;
    };

    struct CanonicalDurableActorState
    {
        std::vector<CanonicalActorEntityState> actors;

        friend bool operator==(const CanonicalDurableActorState&, const CanonicalDurableActorState&) noexcept = default;
    };

    class CanonicalDurableTick
    {
    public:
        static std::optional<CanonicalDurableTick> create(CanonicalStateVersion stateVersion,
            CanonicalRevision canonicalRevision, ServerTick checkpointTick,
            std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
            CanonicalChecksum previousTransactionChecksum = CanonicalChecksum(0),
            const CanonicalInventoryWorld* inventory = nullptr, const CanonicalCombatWorld* combat = nullptr,
            const CanonicalInteractiveObjectWorld* objects = nullptr,
            const CanonicalActorWorld* actors = nullptr, const CanonicalWorldState* world = nullptr) noexcept;
        static std::optional<CanonicalDurableTick> create(CanonicalStateVersion stateVersion,
            CanonicalRevision canonicalRevision, ServerTick checkpointTick,
            std::span<const CanonicalPlayerEntityState> players, std::span<const DurableCommandOrder> commands,
            CanonicalChecksum previousTransactionChecksum, std::optional<CanonicalDurableInventoryState> inventory,
            std::optional<CanonicalDurableCombatState> combat,
            std::optional<CanonicalDurableInteractiveObjectState> objects = std::nullopt,
            std::optional<CanonicalDurableActorState> actors = std::nullopt,
            std::optional<CanonicalWorldState> world = std::nullopt) noexcept;

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
        const std::optional<CanonicalDurableInventoryState>& inventory() const noexcept { return mInventory; }
        const std::optional<CanonicalDurableCombatState>& combat() const noexcept { return mCombat; }
        const std::optional<CanonicalDurableInteractiveObjectState>& objects() const noexcept { return mObjects; }
        const std::optional<CanonicalDurableActorState>& actors() const noexcept { return mActors; }
        const std::optional<CanonicalWorldState>& world() const noexcept { return mWorld; }
        friend bool operator==(const CanonicalDurableTick&, const CanonicalDurableTick&) noexcept = default;

    private:
        CanonicalDurableTick(CanonicalStateVersion stateVersion, CanonicalRevision canonicalRevision,
            ServerTick checkpointTick, CanonicalChecksum previousTransactionChecksum,
            CanonicalChecksum canonicalChecksum, CanonicalChecksum transactionChecksum,
            std::vector<CanonicalPlayerEntityState> players, std::vector<DurableCommandOrder> commands,
            std::optional<CanonicalDurableInventoryState> inventory, std::optional<CanonicalDurableCombatState> combat,
            std::optional<CanonicalDurableInteractiveObjectState> objects,
            std::optional<CanonicalDurableActorState> actors, std::optional<CanonicalWorldState> world) noexcept
            : mStateVersion(stateVersion)
            , mCanonicalRevision(canonicalRevision)
            , mCheckpointTick(checkpointTick)
            , mPreviousTransactionChecksum(previousTransactionChecksum)
            , mCanonicalChecksum(canonicalChecksum)
            , mTransactionChecksum(transactionChecksum)
            , mPlayers(std::move(players))
            , mCommands(std::move(commands))
            , mInventory(std::move(inventory))
            , mCombat(std::move(combat))
            , mObjects(std::move(objects))
            , mActors(std::move(actors))
            , mWorld(std::move(world))
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
        std::optional<CanonicalDurableInventoryState> mInventory;
        std::optional<CanonicalDurableCombatState> mCombat;
        std::optional<CanonicalDurableInteractiveObjectState> mObjects;
        std::optional<CanonicalDurableActorState> mActors;
        std::optional<CanonicalWorldState> mWorld;
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
        const CanonicalDurableTick* checkpoint() const noexcept
        {
            return mTransactions.empty() ? nullptr : &mTransactions.front();
        }
        std::span<const CanonicalDurableTick> journal() const noexcept
        {
            return mTransactions.empty() ? std::span<const CanonicalDurableTick>{}
                                         : std::span<const CanonicalDurableTick>(mTransactions).subspan(1);
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

    std::vector<std::byte> encodeCanonicalDurablePrefixV2(const CanonicalDurablePrefix& prefix);
    CanonicalPersistenceDecodeResult decodeCanonicalDurablePrefix(
        std::span<const std::byte> bytes, const CanonicalPersistenceIdentity& expected) noexcept;

    CanonicalChecksum canonicalDurableStateChecksumV1(CanonicalStateVersion stateVersion, ServerTick checkpointTick,
        std::span<const CanonicalPlayerEntityState> players,
        const std::optional<CanonicalDurableInventoryState>& inventory,
        const std::optional<CanonicalDurableCombatState>& combat,
        const std::optional<CanonicalDurableInteractiveObjectState>& objects = std::nullopt,
        const std::optional<CanonicalDurableActorState>& actors = std::nullopt,
        const std::optional<CanonicalWorldState>& world = std::nullopt) noexcept;

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
            CanonicalRevision canonicalRevision, std::span<const DurableCommandOrder> commands,
            const CanonicalInventoryWorld* inventory = nullptr, const CanonicalCombatWorld* combat = nullptr,
            const CanonicalInteractiveObjectWorld* objects = nullptr,
            const CanonicalActorWorld* actors = nullptr, const CanonicalWorldState* world = nullptr) noexcept = 0;
    };

    struct CanonicalReplayState
    {
        CanonicalServerState players;
        std::optional<CanonicalDurableInventoryState> inventory;
        std::optional<CanonicalDurableCombatState> combat;
        std::optional<CanonicalDurableInteractiveObjectState> objects;
        std::optional<CanonicalDurableActorState> actors;
        std::optional<CanonicalWorldState> world;

        friend bool operator==(const CanonicalReplayState&, const CanonicalReplayState&) noexcept = default;
    };

    using CanonicalReplayStep = std::variant<CanonicalReplayState, CanonicalChecksum> (*)(
        const CanonicalReplayState&, std::span<const DurableCommandOrder>, ServerTick);
    bool replayCanonicalDurablePrefix(const CanonicalDurablePrefix& prefix, CanonicalReplayStep step) noexcept;
}

#endif
