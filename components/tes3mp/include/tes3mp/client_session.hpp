#ifndef TES3MP_CLIENT_SESSION_HPP
#define TES3MP_CLIENT_SESSION_HPP

#include "actor_replication.hpp"
#include "interactive_object_replication.hpp"
#include "inventory_replication.hpp"
#include "monotonic_clock.hpp"
#include "protocol_exchange.hpp"
#include "protocol_handshake.hpp"
#include "session_types.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <variant>

namespace TES3MP
{
    enum class ClientSessionState : std::uint8_t
    {
        AwaitingEncryptedTransport,
        AwaitingServerHello,
        AwaitingAuthenticationInput,
        AwaitingAuthenticationResult,
        Established,
        Rejected,
        TimedOut,
        Cancelled,
        Closed,
    };

    enum class ClientSessionEventKind : std::uint8_t
    {
        EncryptedTransportReady,
        ServerHelloReceived,
        SessionRejectedReceived,
        AuthenticationSubmitted,
        AuthenticationAccepted,
        AuthenticationRejected,
        CheckTimeout,
        Cancel,
        Close,
    };

    struct ClientEncryptedTransportReady
    {
    };

    struct ClientServerHelloReceived
    {
        ServerHello hello;
    };

    struct ClientSessionRejectedReceived
    {
        SessionRejected rejection;
    };

    struct ClientAuthenticationSubmitted
    {
    };

    struct ClientAuthenticationAccepted
    {
    };

    struct ClientAuthenticationRejected
    {
        AuthenticationRejectionReason reason;
    };

    struct ClientCheckTimeout
    {
    };

    struct ClientCancel
    {
    };

    struct ClientClose
    {
    };

    using ClientSessionEvent = std::variant<ClientEncryptedTransportReady, ClientServerHelloReceived,
        ClientSessionRejectedReceived, ClientAuthenticationSubmitted, ClientAuthenticationAccepted,
        ClientAuthenticationRejected, ClientCheckTimeout, ClientCancel, ClientClose>;

    enum class ClientSessionAction : std::uint8_t
    {
        None,
        SendClientHello,
        AuthenticationInputReady,
        AuthenticationSubmitted,
        SessionEstablished,
        SessionRejected,
        SessionTimedOut,
        SessionCancelled,
        SessionClosed,
    };

    struct ClientSessionTransition
    {
        ClientSessionAction action = ClientSessionAction::None;
        std::optional<SessionTransitionError> error;

        bool accepted() const noexcept { return !error.has_value(); }
    };

    enum class ClientSessionBindingResult : std::uint8_t
    {
        Bound,
        NotEstablished,
        AlreadyBound,
    };

    enum class LatestWinsSnapshotReceiveResult : std::uint8_t
    {
        Applied,
        IdenticalDuplicate,
        NotEstablished,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleTick,
        RegressingAcknowledgement,
        ContradictorySameTick,
        TargetBindingMissing,
        TargetBindingMismatch,
    };

    enum class ReliableObservationReceiveResult : std::uint8_t
    {
        Applied,
        IdenticalDuplicate,
        NotEstablished,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleTick,
        ContradictorySameTick,
        ContradictoryChange,
        BaselineMissing,
    };

    using ObservedPlayer = InterestMember;

    enum class ReliableInterestBaselineReceiveResult : std::uint8_t
    {
        Applied,
        IdenticalDuplicate,
        NotEstablished,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleRevision,
        ContradictorySameRevision,
    };

    enum class ActorReplicationReceiveResult : std::uint8_t
    {
        Applied,
        IdenticalDuplicate,
        NotEstablished,
        CapabilityNotNegotiated,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleTick,
        ContradictorySameTick,
    };

    enum class InteractiveObjectReplicationReceiveResult : std::uint8_t
    {
        Applied,
        IdenticalDuplicate,
        NotEstablished,
        CapabilityNotNegotiated,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleTick,
        ContradictorySameTick,
    };

    enum class InventoryReplicationReceiveResult : std::uint8_t
    {
        Applied,
        ChunkAccepted,
        IdenticalDuplicate,
        NotEstablished,
        CapabilityNotNegotiated,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
        StaleTick,
        ContradictorySameTick,
        InvalidChunkSequence,
    };

    using ClientSessionCreateResult
        = std::variant<std::unique_ptr<class ClientSessionStateMachine>, SessionTransitionError>;

    class ClientSessionStateMachine
    {
    public:
        static ClientSessionCreateResult create(
            MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation);

        ClientSessionStateMachine(const ClientSessionStateMachine&) = delete;
        ClientSessionStateMachine& operator=(const ClientSessionStateMachine&) = delete;
        ClientSessionStateMachine(ClientSessionStateMachine&&) = delete;
        ClientSessionStateMachine& operator=(ClientSessionStateMachine&&) = delete;

        ClientSessionTransition handle(ClientSessionEvent event) noexcept;
        ClientSessionBindingResult bindEstablishedSession(SessionId sessionId) noexcept;
        LatestWinsSnapshotReceiveResult receiveLatestWinsSnapshot(LatestWinsSnapshot snapshot);
        ReliableObservationReceiveResult receiveReliableObservationBatch(ReliableObservationBatch batch);
        ReliableInterestBaselineReceiveResult receiveReliableInterestBaseline(ReliableInterestBaseline baseline);
        ActorReplicationReceiveResult receiveLatestWinsActorSnapshot(LatestWinsActorSnapshot snapshot);
        ActorReplicationReceiveResult receiveReliableActorInterestBaseline(ReliableActorInterestBaseline baseline);
        InteractiveObjectReplicationReceiveResult receiveReliableInteractiveObjectInterestBaseline(
            ReliableInteractiveObjectInterestBaseline baseline);
        InventoryReplicationReceiveResult receiveReliablePlayerInventoryBaseline(
            ReliablePlayerInventoryBaseline baseline);
        InventoryReplicationReceiveResult receiveReliableContainerInventoryBaseline(
            ReliableContainerInventoryBaseline baseline);
        InventoryReplicationReceiveResult receiveReliableGroundItemBaseline(ReliableGroundItemBaseline baseline);
        InventoryReplicationReceiveResult receiveLatestWinsEquipmentSnapshot(LatestWinsEquipmentSnapshot snapshot);

        ClientSessionState state() const noexcept { return mState; }
        SessionGeneration generation() const noexcept { return mGeneration; }
        std::optional<MonotonicInstant> deadline() const noexcept { return mDeadline; }
        const std::optional<ServerHello>& negotiatedHello() const noexcept { return mNegotiatedHello; }
        const std::optional<SessionRejected>& protocolRejection() const noexcept { return mProtocolRejection; }
        std::optional<AuthenticationRejectionReason> authenticationRejection() const noexcept
        {
            return mAuthenticationRejection;
        }
        std::optional<SessionId> sessionId() const noexcept { return mSessionId; }
        std::optional<PlayerId> targetPlayerId() const noexcept { return mTargetPlayerId; }
        std::optional<EntityId> targetEntityId() const noexcept { return mTargetEntityId; }
        const std::optional<LatestWinsSnapshot>& confirmedSnapshot() const noexcept { return mConfirmedSnapshot; }
        const std::optional<ReliableObservationBatch>& confirmedObservationBatch() const noexcept
        {
            return mConfirmedObservationBatch;
        }
        std::span<const ObservedPlayer> observedPlayers() const noexcept { return mObservedPlayers; }
        const std::optional<ReliableInterestBaseline>& confirmedInterestBaseline() const noexcept
        {
            return mConfirmedInterestBaseline;
        }
        bool interestBaselineComplete() const noexcept;
        std::span<const ActorInterestMember> observedActors() const noexcept { return mObservedActors; }
        const std::optional<LatestWinsActorSnapshot>& confirmedActorSnapshot() const noexcept
        {
            return mConfirmedActorSnapshot;
        }
        const std::optional<ReliableActorInterestBaseline>& confirmedActorInterestBaseline() const noexcept
        {
            return mConfirmedActorInterestBaseline;
        }
        bool actorInterestBaselineComplete() const noexcept;
        std::span<const InteractiveObjectInterestMember> observedInteractiveObjects() const noexcept
        {
            return mObservedInteractiveObjects;
        }
        const std::optional<ReliableInteractiveObjectInterestBaseline>&
        confirmedInteractiveObjectInterestBaseline() const noexcept
        {
            return mConfirmedInteractiveObjectInterestBaseline;
        }
        bool interactiveObjectInterestBaselineComplete() const noexcept;
        const std::optional<ReliablePlayerInventoryBaseline>& confirmedPlayerInventoryBaseline() const noexcept
        {
            return mConfirmedPlayerInventoryBaseline;
        }
        std::span<const ReliableContainerInventoryBaseline> confirmedContainerInventoryBaselines() const noexcept
        {
            return mConfirmedContainerInventoryBaselines;
        }
        const std::optional<ReliableGroundItemBaseline>& confirmedGroundItemBaseline() const noexcept
        {
            return mConfirmedGroundItemBaseline;
        }
        const std::optional<LatestWinsEquipmentSnapshot>& confirmedEquipmentSnapshot() const noexcept
        {
            return mConfirmedEquipmentSnapshot;
        }
        bool inventoryReplicationComplete() const noexcept;

    private:
        ClientSessionStateMachine(MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy,
            SessionGeneration generation, MonotonicInstant deadline) noexcept;

        ClientSessionTransition illegal(ClientSessionEventKind event) const noexcept;
        ClientSessionTransition deadlineOverflow(ClientSessionEventKind event, SessionStage stage) const noexcept;
        bool prepareDeadline(SessionStage stage, MonotonicInstant& result) const noexcept;

        MonotonicClock& mClock;
        SessionTimeoutPolicy mTimeoutPolicy;
        SessionGeneration mGeneration;
        ClientSessionState mState = ClientSessionState::AwaitingEncryptedTransport;
        std::optional<MonotonicInstant> mDeadline;
        std::optional<ServerHello> mNegotiatedHello;
        std::optional<SessionRejected> mProtocolRejection;
        std::optional<AuthenticationRejectionReason> mAuthenticationRejection;
        std::optional<SessionId> mSessionId;
        std::optional<PlayerId> mTargetPlayerId;
        std::optional<EntityId> mTargetEntityId;
        std::optional<LatestWinsSnapshot> mConfirmedSnapshot;
        std::optional<ReliableObservationBatch> mConfirmedObservationBatch;
        std::optional<ReliableInterestBaseline> mConfirmedInterestBaseline;
        std::vector<ObservedPlayer> mObservedPlayers;
        std::optional<LatestWinsActorSnapshot> mConfirmedActorSnapshot;
        std::optional<ReliableActorInterestBaseline> mConfirmedActorInterestBaseline;
        std::vector<ActorInterestMember> mObservedActors;
        std::optional<ReliableInteractiveObjectInterestBaseline> mConfirmedInteractiveObjectInterestBaseline;
        std::vector<InteractiveObjectInterestMember> mObservedInteractiveObjects;
        struct PlayerInventoryChunks
        {
            InventoryBaselineHeader header;
            PlayerId player;
            InventoryRevision revision;
            std::vector<std::optional<ReliablePlayerInventoryBaseline>> chunks;
        };
        struct ContainerInventoryChunks
        {
            InventoryBaselineHeader header;
            ContainerId container;
            CellId cell;
            Position3 position;
            ContainerRevision revision;
            std::uint32_t capacityWeight = 0;
            std::vector<std::optional<ReliableContainerInventoryBaseline>> chunks;
        };
        struct GroundItemChunks
        {
            InventoryBaselineHeader header;
            CellId cell;
            std::vector<std::optional<ReliableGroundItemBaseline>> chunks;
        };
        std::optional<PlayerInventoryChunks> mPendingPlayerInventory;
        std::map<ContainerId, ContainerInventoryChunks> mPendingContainerInventories;
        std::optional<GroundItemChunks> mPendingGroundItems;
        std::optional<ReliablePlayerInventoryBaseline> mConfirmedPlayerInventoryBaseline;
        std::vector<ReliableContainerInventoryBaseline> mConfirmedContainerInventoryBaselines;
        std::optional<ReliableGroundItemBaseline> mConfirmedGroundItemBaseline;
        std::optional<LatestWinsEquipmentSnapshot> mConfirmedEquipmentSnapshot;
    };
}

#endif
