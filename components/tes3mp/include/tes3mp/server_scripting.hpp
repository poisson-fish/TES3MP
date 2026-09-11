#ifndef TES3MP_SERVER_SCRIPTING_HPP
#define TES3MP_SERVER_SCRIPTING_HPP

#include "canonical_sinks.hpp"
#include "world_state.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::uint32_t ServerScriptApiVersion = 2;
    inline constexpr std::size_t MaximumServerScriptCallbacks = 64;
    inline constexpr std::size_t MaximumServerScriptEventsPerPublication = 4096;
    inline constexpr std::size_t MaximumServerScriptCommandsPerCallback = 16;
    inline constexpr std::size_t MaximumServerScriptCommandsPerPublication = 256;
    inline constexpr std::size_t MaximumPendingServerScriptCommands = 4096;
    inline constexpr std::size_t MaximumServerScriptCommandsPerTick = 256;

    enum class ServerScriptEventKind : std::uint8_t
    {
        CommandFinalized,
        SessionJoined,
        SpatialStateChanged,
        SessionLifecycle,
    };

    class ServerScriptEvent
    {
    public:
        constexpr ServerScriptEventKind kind() const noexcept { return mKind; }
        constexpr CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        constexpr ServerTick commitTick() const noexcept { return mCommitTick; }
        constexpr std::optional<WriterAdmissionStamp> writerStamp() const noexcept { return mWriterStamp; }
        constexpr std::optional<SessionId> sessionId() const noexcept { return mSessionId; }
        constexpr std::optional<SessionGeneration> sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr std::optional<PlayerId> playerId() const noexcept { return mPlayerId; }
        constexpr std::optional<CommandSequence> commandSequence() const noexcept { return mCommandSequence; }
        constexpr std::optional<CommandId> commandId() const noexcept { return mCommandId; }
        constexpr std::optional<CommandDisposition> commandDisposition() const noexcept { return mCommandDisposition; }
        constexpr const std::optional<ObjectInteractionOutcome>& objectInteractionOutcome() const noexcept
        {
            return mObjectInteractionOutcome;
        }
        constexpr const std::optional<CanonicalPlayerEntityState>& playerState() const noexcept { return mPlayerState; }
        constexpr std::optional<CanonicalSessionLifecycleKind> lifecycleKind() const noexcept { return mLifecycleKind; }

        friend bool operator==(const ServerScriptEvent&, const ServerScriptEvent&) noexcept = default;

    private:
        friend class DeterministicServerScriptRuntime;

        ServerScriptEventKind mKind = ServerScriptEventKind::CommandFinalized;
        CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
        ServerTick mCommitTick = ServerTick::initial();
        std::optional<WriterAdmissionStamp> mWriterStamp;
        std::optional<SessionId> mSessionId;
        std::optional<SessionGeneration> mSessionGeneration;
        std::optional<PlayerId> mPlayerId;
        std::optional<CommandSequence> mCommandSequence;
        std::optional<CommandId> mCommandId;
        std::optional<CommandDisposition> mCommandDisposition;
        std::optional<ObjectInteractionOutcome> mObjectInteractionOutcome;
        std::optional<CanonicalPlayerEntityState> mPlayerState;
        std::optional<CanonicalSessionLifecycleKind> mLifecycleKind;
    };

    class ServerScriptPackage
    {
    public:
        static std::optional<ServerScriptPackage> create(std::uint64_t packageId, std::uint32_t packageVersion,
            std::uint32_t loadOrder, std::uint32_t apiVersion = ServerScriptApiVersion) noexcept;

        constexpr std::uint64_t packageId() const noexcept { return mPackageId; }
        constexpr std::uint32_t packageVersion() const noexcept { return mPackageVersion; }
        constexpr std::uint32_t loadOrder() const noexcept { return mLoadOrder; }
        constexpr std::uint32_t apiVersion() const noexcept { return mApiVersion; }

        friend constexpr bool operator==(ServerScriptPackage, ServerScriptPackage) noexcept = default;
        friend constexpr auto operator<=>(ServerScriptPackage, ServerScriptPackage) noexcept = default;

    private:
        constexpr ServerScriptPackage(std::uint64_t packageId, std::uint32_t packageVersion, std::uint32_t loadOrder,
            std::uint32_t apiVersion) noexcept
            : mPackageId(packageId)
            , mPackageVersion(packageVersion)
            , mLoadOrder(loadOrder)
            , mApiVersion(apiVersion)
        {
        }

        std::uint64_t mPackageId;
        std::uint32_t mPackageVersion;
        std::uint32_t mLoadOrder;
        std::uint32_t mApiVersion;
    };

    class ServerScriptCallbackInput
    {
    public:
        constexpr std::uint32_t apiVersion() const noexcept { return ServerScriptApiVersion; }
        constexpr std::uint64_t publicationOrdinal() const noexcept { return mPublicationOrdinal; }
        constexpr std::uint32_t eventOrdinal() const noexcept { return mEventOrdinal; }
        constexpr const ServerScriptEvent& event() const noexcept { return mEvent; }

    private:
        friend class DeterministicServerScriptRuntime;
        ServerScriptCallbackInput(
            std::uint64_t publicationOrdinal, std::uint32_t eventOrdinal, ServerScriptEvent event) noexcept
            : mPublicationOrdinal(publicationOrdinal)
            , mEventOrdinal(eventOrdinal)
            , mEvent(std::move(event))
        {
        }

        std::uint64_t mPublicationOrdinal;
        std::uint32_t mEventOrdinal;
        ServerScriptEvent mEvent;
    };

    class ServerScriptPlayerSafePointCommand
    {
    public:
        constexpr ServerScriptPlayerSafePointCommand(
            PlayerId player, EntityPrecondition precondition, Transform destination) noexcept
            : mPlayer(player)
            , mPrecondition(precondition)
            , mDestination(destination)
        {
        }

        constexpr PlayerId player() const noexcept { return mPlayer; }
        constexpr EntityPrecondition precondition() const noexcept { return mPrecondition; }
        constexpr const Transform& destination() const noexcept { return mDestination; }

        friend constexpr bool operator==(
            const ServerScriptPlayerSafePointCommand&, const ServerScriptPlayerSafePointCommand&) noexcept = default;

    private:
        PlayerId mPlayer;
        EntityPrecondition mPrecondition;
        Transform mDestination;
    };

    class ServerScriptSetGlobalCommand
    {
    public:
        constexpr ServerScriptSetGlobalCommand(GlobalVariableId id, GlobalVariableRevision expectedRevision,
            GlobalVariableValue value) noexcept
            : mId(id)
            , mExpectedRevision(expectedRevision)
            , mValue(std::move(value))
        {
        }
        constexpr GlobalVariableId id() const noexcept { return mId; }
        constexpr GlobalVariableRevision expectedRevision() const noexcept { return mExpectedRevision; }
        constexpr const GlobalVariableValue& value() const noexcept { return mValue; }
        friend constexpr bool operator==(const ServerScriptSetGlobalCommand&,
            const ServerScriptSetGlobalCommand&) noexcept = default;

    private:
        GlobalVariableId mId;
        GlobalVariableRevision mExpectedRevision;
        GlobalVariableValue mValue;
    };

    class ServerScriptSetWorldTimeCommand
    {
    public:
        constexpr ServerScriptSetWorldTimeCommand(WorldTimeRevision expectedRevision,
            CanonicalWorldTimeState replacement) noexcept
            : mExpectedRevision(expectedRevision)
            , mReplacement(replacement)
        {
        }
        constexpr WorldTimeRevision expectedRevision() const noexcept { return mExpectedRevision; }
        constexpr const CanonicalWorldTimeState& replacement() const noexcept { return mReplacement; }
        friend constexpr bool operator==(const ServerScriptSetWorldTimeCommand&,
            const ServerScriptSetWorldTimeCommand&) noexcept = default;

    private:
        WorldTimeRevision mExpectedRevision;
        CanonicalWorldTimeState mReplacement;
    };

    using ServerScriptCommandPayload
        = std::variant<ServerScriptPlayerSafePointCommand, ServerScriptSetGlobalCommand, ServerScriptSetWorldTimeCommand>;

    class ServerScriptCommandOrder
    {
    public:
        constexpr ServerTick eligibleTick() const noexcept { return mEligibleTick; }
        constexpr std::uint64_t publicationOrdinal() const noexcept { return mPublicationOrdinal; }
        constexpr std::uint32_t eventOrdinal() const noexcept { return mEventOrdinal; }
        constexpr std::uint32_t packageLoadOrder() const noexcept { return mPackageLoadOrder; }
        constexpr std::uint64_t packageId() const noexcept { return mPackageId; }
        constexpr std::uint32_t packageVersion() const noexcept { return mPackageVersion; }
        constexpr std::uint32_t apiVersion() const noexcept { return mApiVersion; }
        constexpr std::uint32_t callbackOrder() const noexcept { return mCallbackOrder; }
        constexpr std::uint32_t commandOrdinal() const noexcept { return mCommandOrdinal; }

        friend constexpr bool operator==(ServerScriptCommandOrder, ServerScriptCommandOrder) noexcept = default;
        friend constexpr auto operator<=>(ServerScriptCommandOrder, ServerScriptCommandOrder) noexcept = default;

    private:
        friend class ServerScriptCommandEmitter;
        constexpr ServerScriptCommandOrder(ServerTick eligibleTick, std::uint64_t publicationOrdinal,
            std::uint32_t eventOrdinal, std::uint32_t packageLoadOrder, std::uint64_t packageId,
            std::uint32_t packageVersion, std::uint32_t apiVersion, std::uint32_t callbackOrder,
            std::uint32_t commandOrdinal) noexcept
            : mEligibleTick(eligibleTick)
            , mPublicationOrdinal(publicationOrdinal)
            , mEventOrdinal(eventOrdinal)
            , mPackageLoadOrder(packageLoadOrder)
            , mPackageId(packageId)
            , mPackageVersion(packageVersion)
            , mApiVersion(apiVersion)
            , mCallbackOrder(callbackOrder)
            , mCommandOrdinal(commandOrdinal)
        {
        }

        ServerTick mEligibleTick;
        std::uint64_t mPublicationOrdinal;
        std::uint32_t mEventOrdinal;
        std::uint32_t mPackageLoadOrder;
        std::uint64_t mPackageId;
        std::uint32_t mPackageVersion;
        std::uint32_t mApiVersion;
        std::uint32_t mCallbackOrder;
        std::uint32_t mCommandOrdinal;
    };

    class QueuedServerScriptCommand
    {
    public:
        constexpr ServerScriptCommandOrder order() const noexcept { return mOrder; }
        constexpr const ServerScriptCommandPayload& payload() const noexcept { return mPayload; }

        friend constexpr bool operator==(const QueuedServerScriptCommand&, const QueuedServerScriptCommand&) noexcept
            = default;

    private:
        friend class ServerScriptCommandEmitter;
        QueuedServerScriptCommand(ServerScriptCommandOrder order, ServerScriptCommandPayload payload) noexcept
            : mOrder(order)
            , mPayload(std::move(payload))
        {
        }

        ServerScriptCommandOrder mOrder;
        ServerScriptCommandPayload mPayload;
    };

    enum class ServerScriptEmitResult : std::uint8_t
    {
        Accepted,
        PerCallbackLimit,
        PerPublicationLimit,
    };

    class ServerScriptCommandEmitter
    {
    public:
        ServerScriptEmitResult enqueue(ServerScriptPlayerSafePointCommand command) noexcept;
        ServerScriptEmitResult enqueue(ServerScriptSetGlobalCommand command) noexcept;
        ServerScriptEmitResult enqueue(ServerScriptSetWorldTimeCommand command) noexcept;

    private:
        friend class DeterministicServerScriptRuntime;
        ServerScriptCommandEmitter(std::vector<QueuedServerScriptCommand>& commands, ServerTick eligibleTick,
            std::uint64_t publicationOrdinal, std::uint32_t eventOrdinal, ServerScriptPackage package,
            std::uint32_t callbackOrder) noexcept
            : mCommands(commands)
            , mEligibleTick(eligibleTick)
            , mPublicationOrdinal(publicationOrdinal)
            , mEventOrdinal(eventOrdinal)
            , mPackage(package)
            , mCallbackOrder(callbackOrder)
        {
        }

        std::vector<QueuedServerScriptCommand>& mCommands;
        ServerTick mEligibleTick;
        std::uint64_t mPublicationOrdinal;
        std::uint32_t mEventOrdinal;
        ServerScriptPackage mPackage;
        std::uint32_t mCallbackOrder;
        std::uint32_t mEmitted = 0;
        ServerScriptEmitResult mResult = ServerScriptEmitResult::Accepted;

        ServerScriptEmitResult enqueuePayload(ServerScriptCommandPayload command) noexcept;
    };

    enum class ServerScriptCallbackResult : std::uint8_t
    {
        Accepted,
        Failed,
    };

    class ServerScriptCallback
    {
    public:
        virtual ~ServerScriptCallback() = default;
        virtual ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept = 0;
    };

    enum class ServerScriptRegistrationResult : std::uint8_t
    {
        Accepted,
        RuntimeStarted,
        CallbackLimit,
        DuplicateRegistration,
        PackageConflict,
    };

    enum class ServerScriptPumpError : std::uint8_t
    {
        None,
        TickNotStrictlyIncreasing,
        MissedEligibleTick,
        RuntimeTerminated,
    };

    class ServerScriptPumpResult
    {
    public:
        constexpr ServerScriptPumpError error() const noexcept { return mError; }
        constexpr std::span<const QueuedServerScriptCommand> commands() const noexcept { return mCommands; }
        constexpr explicit operator bool() const noexcept { return mError == ServerScriptPumpError::None; }

    private:
        friend class DeterministicServerScriptRuntime;
        ServerScriptPumpError mError = ServerScriptPumpError::None;
        std::vector<QueuedServerScriptCommand> mCommands;
    };

    class DeterministicServerScriptRuntime final : public CanonicalScriptSink
    {
    public:
        ServerScriptRegistrationResult registerCallback(ServerScriptPackage package, std::uint32_t callbackOrder,
            ServerScriptEventKind eventKind, ServerScriptCallback& callback) noexcept;

        CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept override;
        ServerScriptPumpResult pump(ServerTick tick) noexcept;
        constexpr std::size_t pendingCommandCount() const noexcept { return mPending.size(); }
        constexpr std::size_t callbackCount() const noexcept { return mCallbacks.size(); }
        constexpr bool healthy() const noexcept { return !mTerminated; }

    private:
        struct Registration
        {
            ServerScriptPackage package;
            std::uint32_t callbackOrder;
            ServerScriptEventKind eventKind;
            ServerScriptCallback* callback;

            friend constexpr bool operator==(const Registration&, const Registration&) noexcept = default;
        };

        std::vector<Registration> mCallbacks;
        std::vector<QueuedServerScriptCommand> mPending;
        std::optional<ServerTick> mLastPumpedTick;
        std::uint64_t mNextPublicationOrdinal = 1;
        bool mStarted = false;
        bool mTerminated = false;

        CanonicalSinkDeliveryResult terminate(CanonicalSinkDeliveryResult result) noexcept;
    };

    enum class ServerScriptCommandDisposition : std::uint8_t
    {
        Applied,
        UnknownPlayer,
        InactivePlayer,
        EntityBindingMismatch,
        EntityRevisionMismatch,
        AuthorityEpochMismatch,
        UnknownCell,
        SpatialTickRegression,
        EntityRevisionExhausted,
        UnknownGlobal,
        GlobalTypeMismatch,
        GlobalRevisionMismatch,
        WorldTimeRevisionMismatch,
        InvalidWorldMutation,
    };

    class ServerScriptCommandDispositionRecord
    {
    public:
        constexpr ServerScriptCommandDispositionRecord(
            ServerScriptCommandOrder order, ServerScriptCommandDisposition disposition) noexcept
            : mOrder(order)
            , mDisposition(disposition)
        {
        }

        constexpr ServerScriptCommandOrder order() const noexcept { return mOrder; }
        constexpr ServerScriptCommandDisposition disposition() const noexcept { return mDisposition; }
        friend constexpr bool operator==(
            ServerScriptCommandDispositionRecord, ServerScriptCommandDispositionRecord) noexcept = default;

    private:
        ServerScriptCommandOrder mOrder;
        ServerScriptCommandDisposition mDisposition;
    };
}

#endif
