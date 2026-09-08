#include <tes3mp/client_session.hpp>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace
{
    constexpr std::uint32_t PlayerInventoryChunkLimit
        = (TES3MP::MaximumPlayerInventoryStacks + TES3MP::MaximumInventoryBaselineChunkStacks - 1)
        / TES3MP::MaximumInventoryBaselineChunkStacks;
    constexpr std::uint32_t ContainerInventoryChunkLimit
        = (TES3MP::MaximumContainerStacks + TES3MP::MaximumInventoryBaselineChunkStacks - 1)
        / TES3MP::MaximumInventoryBaselineChunkStacks;
    constexpr std::uint32_t GroundItemChunkLimit
        = (TES3MP::MaximumWorldItemStacks + TES3MP::MaximumGroundItemBaselineChunkItems - 1)
        / TES3MP::MaximumGroundItemBaselineChunkItems;

    TES3MP::ClientSessionEventKind eventKind(const TES3MP::ClientSessionEvent& event) noexcept
    {
        return std::visit(
            [](const auto& value) {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, TES3MP::ClientEncryptedTransportReady>)
                    return TES3MP::ClientSessionEventKind::EncryptedTransportReady;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientServerHelloReceived>)
                    return TES3MP::ClientSessionEventKind::ServerHelloReceived;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientSessionRejectedReceived>)
                    return TES3MP::ClientSessionEventKind::SessionRejectedReceived;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationSubmitted>)
                    return TES3MP::ClientSessionEventKind::AuthenticationSubmitted;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationAccepted>)
                    return TES3MP::ClientSessionEventKind::AuthenticationAccepted;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationRejected>)
                    return TES3MP::ClientSessionEventKind::AuthenticationRejected;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientCheckTimeout>)
                    return TES3MP::ClientSessionEventKind::CheckTimeout;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientCancel>)
                    return TES3MP::ClientSessionEventKind::Cancel;
                else
                    return TES3MP::ClientSessionEventKind::Close;
            },
            event);
    }
}

namespace TES3MP
{
    ClientSessionCreateResult ClientSessionStateMachine::create(
        MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation)
    {
        const auto deadline
            = sessionDeadline(clock.now(), timeoutPolicy.duration(SessionStage::TransportAndNegotiation));
        if (!deadline)
        {
            return SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow,
                static_cast<std::uint16_t>(ClientSessionState::AwaitingEncryptedTransport), 0,
                SessionStage::TransportAndNegotiation };
        }
        return std::unique_ptr<ClientSessionStateMachine>(
            new ClientSessionStateMachine(clock, timeoutPolicy, generation, *deadline));
    }

    ClientSessionStateMachine::ClientSessionStateMachine(MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy,
        SessionGeneration generation, MonotonicInstant deadline) noexcept
        : mClock(clock)
        , mTimeoutPolicy(timeoutPolicy)
        , mGeneration(generation)
        , mDeadline(deadline)
    {
    }

    ClientSessionTransition ClientSessionStateMachine::illegal(ClientSessionEventKind event) const noexcept
    {
        const SessionStage stage = mState == ClientSessionState::AwaitingAuthenticationInput
            ? SessionStage::AuthenticationInput
            : (mState == ClientSessionState::AwaitingAuthenticationResult ? SessionStage::AuthenticationProvider
                                                                          : SessionStage::TransportAndNegotiation);
        return { ClientSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::IllegalTransition, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event), stage } };
    }

    ClientSessionTransition ClientSessionStateMachine::deadlineOverflow(
        ClientSessionEventKind event, SessionStage stage) const noexcept
    {
        return { ClientSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event), stage } };
    }

    bool ClientSessionStateMachine::prepareDeadline(SessionStage stage, MonotonicInstant& result) const noexcept
    {
        const auto deadline = sessionDeadline(mClock.now(), mTimeoutPolicy.duration(stage));
        if (!deadline)
            return false;
        result = *deadline;
        return true;
    }

    ClientSessionTransition ClientSessionStateMachine::handle(ClientSessionEvent event) noexcept
    {
        const auto kind = eventKind(event);

        if (std::holds_alternative<ClientClose>(event))
        {
            if (mState == ClientSessionState::Closed)
                return {};
            mState = ClientSessionState::Closed;
            mDeadline.reset();
            return { ClientSessionAction::SessionClosed, std::nullopt };
        }

        if (std::holds_alternative<ClientCancel>(event))
        {
            if (mState == ClientSessionState::Cancelled || mState == ClientSessionState::Rejected
                || mState == ClientSessionState::TimedOut || mState == ClientSessionState::Closed)
                return {};
            mState = ClientSessionState::Cancelled;
            mDeadline.reset();
            return { ClientSessionAction::SessionCancelled, std::nullopt };
        }

        if (std::holds_alternative<ClientCheckTimeout>(event))
        {
            if (!mDeadline || mState == ClientSessionState::Established || mState == ClientSessionState::Rejected
                || mState == ClientSessionState::TimedOut || mState == ClientSessionState::Cancelled
                || mState == ClientSessionState::Closed)
                return {};
            if (mClock.now() < *mDeadline)
                return {};
            mState = ClientSessionState::TimedOut;
            mDeadline.reset();
            return { ClientSessionAction::SessionTimedOut, std::nullopt };
        }

        if (std::holds_alternative<ClientEncryptedTransportReady>(event))
        {
            if (mState != ClientSessionState::AwaitingEncryptedTransport)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::TransportAndNegotiation, deadline))
                return deadlineOverflow(kind, SessionStage::TransportAndNegotiation);
            mState = ClientSessionState::AwaitingServerHello;
            mDeadline = deadline;
            return { ClientSessionAction::SendClientHello, std::nullopt };
        }

        if (auto* hello = std::get_if<ClientServerHelloReceived>(&event))
        {
            if (mState != ClientSessionState::AwaitingServerHello)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationInput, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationInput);
            mNegotiatedHello = std::move(hello->hello);
            mState = ClientSessionState::AwaitingAuthenticationInput;
            mDeadline = deadline;
            return { ClientSessionAction::AuthenticationInputReady, std::nullopt };
        }

        if (auto* rejection = std::get_if<ClientSessionRejectedReceived>(&event))
        {
            if (mState != ClientSessionState::AwaitingServerHello)
                return illegal(kind);
            mProtocolRejection = std::move(rejection->rejection);
            mState = ClientSessionState::Rejected;
            mDeadline.reset();
            return { ClientSessionAction::SessionRejected, std::nullopt };
        }

        if (std::holds_alternative<ClientAuthenticationSubmitted>(event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationInput)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationProvider, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationProvider);
            mState = ClientSessionState::AwaitingAuthenticationResult;
            mDeadline = deadline;
            return { ClientSessionAction::AuthenticationSubmitted, std::nullopt };
        }

        if (std::holds_alternative<ClientAuthenticationAccepted>(event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationResult)
                return illegal(kind);
            mState = ClientSessionState::Established;
            mDeadline.reset();
            return { ClientSessionAction::SessionEstablished, std::nullopt };
        }

        if (const auto* rejected = std::get_if<ClientAuthenticationRejected>(&event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationResult)
                return illegal(kind);
            mAuthenticationRejection = rejected->reason;
            mState = ClientSessionState::Rejected;
            mDeadline.reset();
            return { ClientSessionAction::SessionRejected, std::nullopt };
        }

        return illegal(kind);
    }

    ClientSessionBindingResult ClientSessionStateMachine::bindEstablishedSession(SessionId sessionId) noexcept
    {
        if (mState != ClientSessionState::Established)
            return ClientSessionBindingResult::NotEstablished;
        if (mSessionId)
            return ClientSessionBindingResult::AlreadyBound;
        mSessionId = sessionId;
        return ClientSessionBindingResult::Bound;
    }

    LatestWinsSnapshotReceiveResult ClientSessionStateMachine::receiveLatestWinsSnapshot(LatestWinsSnapshot snapshot)
    {
        if (mState != ClientSessionState::Established)
            return LatestWinsSnapshotReceiveResult::NotEstablished;
        if (!mSessionId)
            return LatestWinsSnapshotReceiveResult::SessionNotBound;
        if (snapshot.header().targetSessionId() != *mSessionId)
            return LatestWinsSnapshotReceiveResult::SessionMismatch;
        if (snapshot.header().targetSessionGeneration() != mGeneration)
            return LatestWinsSnapshotReceiveResult::GenerationMismatch;
        const auto target = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot.header().targetPlayerId()
                && entry.entityId() == snapshot.header().targetEntityId();
        });
        if (target == snapshot.view().entries().end())
            return LatestWinsSnapshotReceiveResult::TargetBindingMissing;
        if ((mTargetPlayerId && *mTargetPlayerId != snapshot.header().targetPlayerId())
            || (mTargetEntityId && *mTargetEntityId != snapshot.header().targetEntityId()))
            return LatestWinsSnapshotReceiveResult::TargetBindingMismatch;
        if (mConfirmedPlayerInventoryBaseline
            && mConfirmedPlayerInventoryBaseline->player != snapshot.header().targetPlayerId())
            return LatestWinsSnapshotReceiveResult::TargetBindingMismatch;

        if (mConfirmedSnapshot)
        {
            const auto currentTick = mConfirmedSnapshot->header().canonicalRevision();
            const auto incomingTick = snapshot.header().canonicalRevision();
            if (incomingTick < currentTick)
                return LatestWinsSnapshotReceiveResult::StaleTick;
            if (incomingTick == currentTick)
            {
                return snapshot == *mConfirmedSnapshot ? LatestWinsSnapshotReceiveResult::IdenticalDuplicate
                                                       : LatestWinsSnapshotReceiveResult::ContradictorySameTick;
            }

            const auto& currentAcknowledgement = mConfirmedSnapshot->header().acknowledgedCommandSequence();
            const auto& incomingAcknowledgement = snapshot.header().acknowledgedCommandSequence();
            if (currentAcknowledgement
                && (!incomingAcknowledgement || *incomingAcknowledgement < *currentAcknowledgement))
            {
                return LatestWinsSnapshotReceiveResult::RegressingAcknowledgement;
            }
        }

        mTargetPlayerId = snapshot.header().targetPlayerId();
        mTargetEntityId = snapshot.header().targetEntityId();
        mConfirmedSnapshot = std::move(snapshot);
        return LatestWinsSnapshotReceiveResult::Applied;
    }

    ReliableObservationReceiveResult ClientSessionStateMachine::receiveReliableObservationBatch(
        ReliableObservationBatch batch)
    {
        if (mState != ClientSessionState::Established)
            return ReliableObservationReceiveResult::NotEstablished;
        if (!mSessionId)
            return ReliableObservationReceiveResult::SessionNotBound;
        if (batch.targetSessionId() != *mSessionId)
            return ReliableObservationReceiveResult::SessionMismatch;
        if (batch.targetSessionGeneration() != mGeneration)
            return ReliableObservationReceiveResult::GenerationMismatch;
        if (!mConfirmedInterestBaseline)
            return ReliableObservationReceiveResult::BaselineMissing;

        if (mConfirmedObservationBatch)
        {
            if (batch.canonicalRevision() < mConfirmedObservationBatch->canonicalRevision())
                return ReliableObservationReceiveResult::StaleTick;
            if (batch.canonicalRevision() == mConfirmedObservationBatch->canonicalRevision())
                return batch == *mConfirmedObservationBatch ? ReliableObservationReceiveResult::IdenticalDuplicate
                                                            : ReliableObservationReceiveResult::ContradictorySameTick;
        }

        auto next = mObservedPlayers;
        for (const auto& change : batch.changes())
        {
            const auto found = std::ranges::lower_bound(next, change.playerId, {}, &ObservedPlayer::playerId);
            if (change.kind == ObservationChangeKind::Enter)
            {
                if (found != next.end() && found->playerId == change.playerId)
                    return ReliableObservationReceiveResult::ContradictoryChange;
                next.insert(found, ObservedPlayer{ change.playerId, change.entityId });
            }
            else
            {
                if (found == next.end() || found->playerId != change.playerId || found->entityId != change.entityId)
                    return ReliableObservationReceiveResult::ContradictoryChange;
                next.erase(found);
            }
        }
        mObservedPlayers = std::move(next);
        mConfirmedObservationBatch = std::move(batch);
        return ReliableObservationReceiveResult::Applied;
    }

    ReliableInterestBaselineReceiveResult ClientSessionStateMachine::receiveReliableInterestBaseline(
        ReliableInterestBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return ReliableInterestBaselineReceiveResult::NotEstablished;
        if (!mSessionId)
            return ReliableInterestBaselineReceiveResult::SessionNotBound;
        if (baseline.targetSessionId() != *mSessionId)
            return ReliableInterestBaselineReceiveResult::SessionMismatch;
        if (baseline.targetSessionGeneration() != mGeneration)
            return ReliableInterestBaselineReceiveResult::GenerationMismatch;
        if (mConfirmedInterestBaseline)
        {
            if (baseline.canonicalRevision() < mConfirmedInterestBaseline->canonicalRevision())
                return ReliableInterestBaselineReceiveResult::StaleRevision;
            if (baseline.canonicalRevision() == mConfirmedInterestBaseline->canonicalRevision())
                return baseline == *mConfirmedInterestBaseline
                    ? ReliableInterestBaselineReceiveResult::IdenticalDuplicate
                    : ReliableInterestBaselineReceiveResult::ContradictorySameRevision;
        }
        mObservedPlayers.assign(baseline.members().begin(), baseline.members().end());
        mConfirmedObservationBatch.reset();
        mConfirmedInterestBaseline = std::move(baseline);
        return ReliableInterestBaselineReceiveResult::Applied;
    }

    bool ClientSessionStateMachine::interestBaselineComplete() const noexcept
    {
        return mConfirmedInterestBaseline && mConfirmedSnapshot
            && mConfirmedSnapshot->header().canonicalRevision() >= mConfirmedInterestBaseline->canonicalRevision();
    }

    ActorReplicationReceiveResult ClientSessionStateMachine::receiveLatestWinsActorSnapshot(
        LatestWinsActorSnapshot snapshot)
    {
        if (mState != ClientSessionState::Established)
            return ActorReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(mNegotiatedHello->negotiatedCapabilities(), actorReplicationCapability()))
            return ActorReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return ActorReplicationReceiveResult::SessionNotBound;
        if (snapshot.targetSessionId() != *mSessionId)
            return ActorReplicationReceiveResult::SessionMismatch;
        if (snapshot.targetSessionGeneration() != mGeneration)
            return ActorReplicationReceiveResult::GenerationMismatch;
        if (mConfirmedActorSnapshot)
        {
            if (snapshot.serverTick() < mConfirmedActorSnapshot->serverTick())
                return ActorReplicationReceiveResult::StaleTick;
            if (snapshot.serverTick() == mConfirmedActorSnapshot->serverTick())
                return snapshot == *mConfirmedActorSnapshot ? ActorReplicationReceiveResult::IdenticalDuplicate
                                                            : ActorReplicationReceiveResult::ContradictorySameTick;
        }
        mConfirmedActorSnapshot = std::move(snapshot);
        return ActorReplicationReceiveResult::Applied;
    }

    ActorReplicationReceiveResult ClientSessionStateMachine::receiveReliableActorInterestBaseline(
        ReliableActorInterestBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return ActorReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(mNegotiatedHello->negotiatedCapabilities(), actorReplicationCapability()))
            return ActorReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return ActorReplicationReceiveResult::SessionNotBound;
        if (baseline.targetSessionId() != *mSessionId)
            return ActorReplicationReceiveResult::SessionMismatch;
        if (baseline.targetSessionGeneration() != mGeneration)
            return ActorReplicationReceiveResult::GenerationMismatch;
        if (mConfirmedActorInterestBaseline)
        {
            if (baseline.canonicalRevision() < mConfirmedActorInterestBaseline->canonicalRevision())
                return ActorReplicationReceiveResult::StaleTick;
            if (baseline.canonicalRevision() == mConfirmedActorInterestBaseline->canonicalRevision())
                return baseline.members().size() == mConfirmedActorInterestBaseline->members().size()
                        && std::ranges::equal(baseline.members(), mConfirmedActorInterestBaseline->members())
                    ? ActorReplicationReceiveResult::IdenticalDuplicate
                    : ActorReplicationReceiveResult::ContradictorySameTick;
            if (baseline.serverTick() < mConfirmedActorInterestBaseline->serverTick())
                return ActorReplicationReceiveResult::StaleTick;
        }
        mObservedActors.assign(baseline.members().begin(), baseline.members().end());
        mConfirmedActorInterestBaseline = std::move(baseline);
        return ActorReplicationReceiveResult::Applied;
    }

    bool ClientSessionStateMachine::actorInterestBaselineComplete() const noexcept
    {
        return mConfirmedActorInterestBaseline && mConfirmedActorSnapshot
            && mConfirmedActorSnapshot->canonicalRevision() >= mConfirmedActorInterestBaseline->canonicalRevision();
    }

    InteractiveObjectReplicationReceiveResult
    ClientSessionStateMachine::receiveReliableInteractiveObjectInterestBaseline(
        ReliableInteractiveObjectInterestBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return InteractiveObjectReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(
                mNegotiatedHello->negotiatedCapabilities(), interactiveObjectReplicationCapability()))
            return InteractiveObjectReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return InteractiveObjectReplicationReceiveResult::SessionNotBound;
        if (baseline.targetSessionId() != *mSessionId)
            return InteractiveObjectReplicationReceiveResult::SessionMismatch;
        if (baseline.targetSessionGeneration() != mGeneration)
            return InteractiveObjectReplicationReceiveResult::GenerationMismatch;
        if (mConfirmedInteractiveObjectInterestBaseline)
        {
            if (baseline.canonicalRevision() < mConfirmedInteractiveObjectInterestBaseline->canonicalRevision())
                return InteractiveObjectReplicationReceiveResult::StaleTick;
            if (baseline.canonicalRevision() == mConfirmedInteractiveObjectInterestBaseline->canonicalRevision())
                return baseline.members().size() == mConfirmedInteractiveObjectInterestBaseline->members().size()
                        && std::ranges::equal(
                            baseline.members(), mConfirmedInteractiveObjectInterestBaseline->members())
                    ? InteractiveObjectReplicationReceiveResult::IdenticalDuplicate
                    : InteractiveObjectReplicationReceiveResult::ContradictorySameTick;
            if (baseline.serverTick() < mConfirmedInteractiveObjectInterestBaseline->serverTick())
                return InteractiveObjectReplicationReceiveResult::StaleTick;
            for (const auto& member : baseline.members())
            {
                const auto previous = std::ranges::lower_bound(mConfirmedInteractiveObjectInterestBaseline->members(),
                    member.objectId, {}, &InteractiveObjectInterestMember::objectId);
                if (previous == mConfirmedInteractiveObjectInterestBaseline->members().end()
                    || previous->objectId != member.objectId)
                    continue;
                if (member.revision < previous->revision)
                    return InteractiveObjectReplicationReceiveResult::StaleTick;
                if (member.revision == previous->revision && member != *previous)
                    return InteractiveObjectReplicationReceiveResult::ContradictorySameTick;
            }
        }
        mObservedInteractiveObjects.assign(baseline.members().begin(), baseline.members().end());
        mConfirmedInteractiveObjectInterestBaseline = std::move(baseline);
        return InteractiveObjectReplicationReceiveResult::Applied;
    }

    bool ClientSessionStateMachine::interactiveObjectInterestBaselineComplete() const noexcept
    {
        return mConfirmedInteractiveObjectInterestBaseline && mConfirmedSnapshot
            && mConfirmedSnapshot->header().canonicalRevision()
            >= mConfirmedInteractiveObjectInterestBaseline->canonicalRevision();
    }

    InventoryReplicationReceiveResult ClientSessionStateMachine::receiveReliablePlayerInventoryBaseline(
        ReliablePlayerInventoryBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return InventoryReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(
                mNegotiatedHello->negotiatedCapabilities(), inventoryReplicationCapability()))
            return InventoryReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return InventoryReplicationReceiveResult::SessionNotBound;
        if (baseline.header.targetSessionId != *mSessionId)
            return InventoryReplicationReceiveResult::SessionMismatch;
        if (baseline.header.targetSessionGeneration != mGeneration)
            return InventoryReplicationReceiveResult::GenerationMismatch;
        if (baseline.header.chunkCount == 0 || baseline.header.chunkCount > PlayerInventoryChunkLimit
            || baseline.header.chunkIndex >= baseline.header.chunkCount)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        if (mTargetPlayerId && baseline.player != *mTargetPlayerId)
            return InventoryReplicationReceiveResult::SessionMismatch;
        if (mConfirmedPlayerInventoryBaseline
            && baseline.header.canonicalRevision < mConfirmedPlayerInventoryBaseline->header.canonicalRevision)
            return InventoryReplicationReceiveResult::StaleTick;

        const auto sameSeries = [&](const PlayerInventoryChunks& chunks) {
            return chunks.header.targetSessionId == baseline.header.targetSessionId
                && chunks.header.targetSessionGeneration == baseline.header.targetSessionGeneration
                && chunks.header.serverTick == baseline.header.serverTick
                && chunks.header.canonicalRevision == baseline.header.canonicalRevision
                && chunks.header.chunkCount == baseline.header.chunkCount && chunks.player == baseline.player
                && chunks.revision == baseline.revision;
        };
        if (!mPendingPlayerInventory || !sameSeries(*mPendingPlayerInventory))
        {
            if (mPendingPlayerInventory
                && baseline.header.canonicalRevision <= mPendingPlayerInventory->header.canonicalRevision)
                return InventoryReplicationReceiveResult::ContradictorySameTick;
            mPendingPlayerInventory = PlayerInventoryChunks{ baseline.header, baseline.player, baseline.revision,
                std::vector<std::optional<ReliablePlayerInventoryBaseline>>(baseline.header.chunkCount) };
        }
        auto& slot = mPendingPlayerInventory->chunks[baseline.header.chunkIndex];
        if (slot)
            return *slot == baseline ? InventoryReplicationReceiveResult::IdenticalDuplicate
                                     : InventoryReplicationReceiveResult::ContradictorySameTick;
        slot = std::move(baseline);
        if (!std::ranges::all_of(mPendingPlayerInventory->chunks, [](const auto& value) { return value.has_value(); }))
            return InventoryReplicationReceiveResult::ChunkAccepted;

        std::vector<CanonicalItemStack> stacks;
        std::vector<EquipmentBinding> equipment;
        for (const auto& chunk : mPendingPlayerInventory->chunks)
        {
            stacks.insert(stacks.end(), chunk->stacks.begin(), chunk->stacks.end());
            equipment.insert(equipment.end(), chunk->equipment.begin(), chunk->equipment.end());
        }
        auto header = mPendingPlayerInventory->header;
        header.chunkIndex = 0;
        header.chunkCount = 1;
        auto created = ReliablePlayerInventoryBaseline::create(
            header, mPendingPlayerInventory->player, mPendingPlayerInventory->revision, stacks, equipment);
        auto* complete = std::get_if<ReliablePlayerInventoryBaseline>(&created);
        if (!complete)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        if (mConfirmedPlayerInventoryBaseline
            && complete->header.canonicalRevision == mConfirmedPlayerInventoryBaseline->header.canonicalRevision)
        {
            const bool identical = *complete == *mConfirmedPlayerInventoryBaseline;
            mPendingPlayerInventory.reset();
            return identical ? InventoryReplicationReceiveResult::IdenticalDuplicate
                             : InventoryReplicationReceiveResult::ContradictorySameTick;
        }
        mConfirmedPlayerInventoryBaseline = std::move(*complete);
        mPendingPlayerInventory.reset();
        return InventoryReplicationReceiveResult::Applied;
    }

    InventoryReplicationReceiveResult ClientSessionStateMachine::receiveReliableContainerInventoryBaseline(
        ReliableContainerInventoryBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return InventoryReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(
                mNegotiatedHello->negotiatedCapabilities(), inventoryReplicationCapability()))
            return InventoryReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return InventoryReplicationReceiveResult::SessionNotBound;
        if (baseline.header.targetSessionId != *mSessionId)
            return InventoryReplicationReceiveResult::SessionMismatch;
        if (baseline.header.targetSessionGeneration != mGeneration)
            return InventoryReplicationReceiveResult::GenerationMismatch;
        if (baseline.header.chunkCount == 0 || baseline.header.chunkCount > ContainerInventoryChunkLimit
            || baseline.header.chunkIndex >= baseline.header.chunkCount)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        const auto confirmed = std::ranges::lower_bound(mConfirmedContainerInventoryBaselines, baseline.container, {},
            &ReliableContainerInventoryBaseline::container);
        if (confirmed != mConfirmedContainerInventoryBaselines.end() && confirmed->container == baseline.container
            && baseline.header.canonicalRevision < confirmed->header.canonicalRevision)
            return InventoryReplicationReceiveResult::StaleTick;

        auto pending = mPendingContainerInventories.find(baseline.container);
        const auto sameSeries = [&](const ContainerInventoryChunks& chunks) {
            return chunks.header.targetSessionId == baseline.header.targetSessionId
                && chunks.header.targetSessionGeneration == baseline.header.targetSessionGeneration
                && chunks.header.serverTick == baseline.header.serverTick
                && chunks.header.canonicalRevision == baseline.header.canonicalRevision
                && chunks.header.chunkCount == baseline.header.chunkCount && chunks.container == baseline.container
                && chunks.cell == baseline.cell && chunks.position == baseline.position
                && chunks.revision == baseline.revision && chunks.capacityWeight == baseline.capacityWeight;
        };
        if (pending == mPendingContainerInventories.end() || !sameSeries(pending->second))
        {
            if (pending != mPendingContainerInventories.end()
                && baseline.header.canonicalRevision <= pending->second.header.canonicalRevision)
                return InventoryReplicationReceiveResult::ContradictorySameTick;
            if (pending == mPendingContainerInventories.end()
                && mPendingContainerInventories.size() >= MaximumInventoryContainers)
                return InventoryReplicationReceiveResult::InvalidChunkSequence;
            auto [inserted, unused] = mPendingContainerInventories.insert_or_assign(baseline.container,
                ContainerInventoryChunks{ baseline.header, baseline.container, baseline.cell, baseline.position,
                    baseline.revision, baseline.capacityWeight,
                    std::vector<std::optional<ReliableContainerInventoryBaseline>>(baseline.header.chunkCount) });
            (void)unused;
            pending = inserted;
        }
        auto& slot = pending->second.chunks[baseline.header.chunkIndex];
        if (slot)
            return *slot == baseline ? InventoryReplicationReceiveResult::IdenticalDuplicate
                                     : InventoryReplicationReceiveResult::ContradictorySameTick;
        slot = std::move(baseline);
        if (!std::ranges::all_of(pending->second.chunks, [](const auto& value) { return value.has_value(); }))
            return InventoryReplicationReceiveResult::ChunkAccepted;
        std::vector<CanonicalItemStack> stacks;
        for (const auto& chunk : pending->second.chunks)
            stacks.insert(stacks.end(), chunk->stacks.begin(), chunk->stacks.end());
        auto header = pending->second.header;
        header.chunkIndex = 0;
        header.chunkCount = 1;
        auto created
            = ReliableContainerInventoryBaseline::create(header, pending->second.container, pending->second.cell,
                pending->second.position, pending->second.revision, pending->second.capacityWeight, stacks);
        auto* complete = std::get_if<ReliableContainerInventoryBaseline>(&created);
        if (!complete)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        const auto current = std::ranges::lower_bound(mConfirmedContainerInventoryBaselines, complete->container, {},
            &ReliableContainerInventoryBaseline::container);
        if (current != mConfirmedContainerInventoryBaselines.end() && current->container == complete->container)
        {
            if (current->header.canonicalRevision == complete->header.canonicalRevision)
            {
                const bool identical = *current == *complete;
                mPendingContainerInventories.erase(pending);
                return identical ? InventoryReplicationReceiveResult::IdenticalDuplicate
                                 : InventoryReplicationReceiveResult::ContradictorySameTick;
            }
            *current = std::move(*complete);
        }
        else
            mConfirmedContainerInventoryBaselines.insert(current, std::move(*complete));
        mPendingContainerInventories.erase(pending);
        return InventoryReplicationReceiveResult::Applied;
    }

    InventoryReplicationReceiveResult ClientSessionStateMachine::receiveReliableGroundItemBaseline(
        ReliableGroundItemBaseline baseline)
    {
        if (mState != ClientSessionState::Established)
            return InventoryReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(
                mNegotiatedHello->negotiatedCapabilities(), inventoryReplicationCapability()))
            return InventoryReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return InventoryReplicationReceiveResult::SessionNotBound;
        if (baseline.header.targetSessionId != *mSessionId)
            return InventoryReplicationReceiveResult::SessionMismatch;
        if (baseline.header.targetSessionGeneration != mGeneration)
            return InventoryReplicationReceiveResult::GenerationMismatch;
        if (baseline.header.chunkCount == 0 || baseline.header.chunkCount > GroundItemChunkLimit
            || baseline.header.chunkIndex >= baseline.header.chunkCount)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        if (mConfirmedGroundItemBaseline
            && baseline.header.canonicalRevision < mConfirmedGroundItemBaseline->header.canonicalRevision)
            return InventoryReplicationReceiveResult::StaleTick;
        const auto sameSeries = [&](const GroundItemChunks& chunks) {
            return chunks.header.targetSessionId == baseline.header.targetSessionId
                && chunks.header.targetSessionGeneration == baseline.header.targetSessionGeneration
                && chunks.header.serverTick == baseline.header.serverTick
                && chunks.header.canonicalRevision == baseline.header.canonicalRevision
                && chunks.header.chunkCount == baseline.header.chunkCount && chunks.cell == baseline.cell;
        };
        if (!mPendingGroundItems || !sameSeries(*mPendingGroundItems))
        {
            if (mPendingGroundItems
                && baseline.header.canonicalRevision <= mPendingGroundItems->header.canonicalRevision)
                return InventoryReplicationReceiveResult::ContradictorySameTick;
            mPendingGroundItems = GroundItemChunks{ baseline.header, baseline.cell,
                std::vector<std::optional<ReliableGroundItemBaseline>>(baseline.header.chunkCount) };
        }
        auto& slot = mPendingGroundItems->chunks[baseline.header.chunkIndex];
        if (slot)
            return *slot == baseline ? InventoryReplicationReceiveResult::IdenticalDuplicate
                                     : InventoryReplicationReceiveResult::ContradictorySameTick;
        slot = std::move(baseline);
        if (!std::ranges::all_of(mPendingGroundItems->chunks, [](const auto& value) { return value.has_value(); }))
            return InventoryReplicationReceiveResult::ChunkAccepted;
        std::vector<GroundItemInterestMember> items;
        for (const auto& chunk : mPendingGroundItems->chunks)
            items.insert(items.end(), chunk->items.begin(), chunk->items.end());
        auto header = mPendingGroundItems->header;
        header.chunkIndex = 0;
        header.chunkCount = 1;
        auto created = ReliableGroundItemBaseline::create(header, mPendingGroundItems->cell, items);
        auto* complete = std::get_if<ReliableGroundItemBaseline>(&created);
        if (!complete)
            return InventoryReplicationReceiveResult::InvalidChunkSequence;
        if (mConfirmedGroundItemBaseline
            && complete->header.canonicalRevision == mConfirmedGroundItemBaseline->header.canonicalRevision)
        {
            const bool identical = *complete == *mConfirmedGroundItemBaseline;
            mPendingGroundItems.reset();
            return identical ? InventoryReplicationReceiveResult::IdenticalDuplicate
                             : InventoryReplicationReceiveResult::ContradictorySameTick;
        }
        const CellId completedCell = complete->cell;
        const CanonicalRevision completedRevision = complete->header.canonicalRevision;
        std::erase_if(mConfirmedContainerInventoryBaselines, [&](const auto& container) {
            return container.cell != completedCell || container.header.canonicalRevision != completedRevision;
        });
        std::erase_if(mPendingContainerInventories, [&](const auto& container) {
            return container.second.cell != completedCell
                || container.second.header.canonicalRevision != completedRevision;
        });
        mConfirmedGroundItemBaseline = std::move(*complete);
        mPendingGroundItems.reset();
        return InventoryReplicationReceiveResult::Applied;
    }

    InventoryReplicationReceiveResult ClientSessionStateMachine::receiveLatestWinsEquipmentSnapshot(
        LatestWinsEquipmentSnapshot snapshot)
    {
        if (mState != ClientSessionState::Established)
            return InventoryReplicationReceiveResult::NotEstablished;
        if (!mNegotiatedHello
            || !std::ranges::binary_search(
                mNegotiatedHello->negotiatedCapabilities(), inventoryReplicationCapability()))
            return InventoryReplicationReceiveResult::CapabilityNotNegotiated;
        if (!mSessionId)
            return InventoryReplicationReceiveResult::SessionNotBound;
        if (snapshot.targetSessionId != *mSessionId)
            return InventoryReplicationReceiveResult::SessionMismatch;
        if (snapshot.targetSessionGeneration != mGeneration)
            return InventoryReplicationReceiveResult::GenerationMismatch;
        if (mConfirmedEquipmentSnapshot)
        {
            if (snapshot.serverTick < mConfirmedEquipmentSnapshot->serverTick)
                return InventoryReplicationReceiveResult::StaleTick;
            if (snapshot.serverTick == mConfirmedEquipmentSnapshot->serverTick)
                return snapshot == *mConfirmedEquipmentSnapshot
                    ? InventoryReplicationReceiveResult::IdenticalDuplicate
                    : InventoryReplicationReceiveResult::ContradictorySameTick;
        }
        mConfirmedEquipmentSnapshot = std::move(snapshot);
        return InventoryReplicationReceiveResult::Applied;
    }

    bool ClientSessionStateMachine::inventoryReplicationComplete() const noexcept
    {
        if (!mConfirmedSnapshot || !mConfirmedPlayerInventoryBaseline || !mConfirmedGroundItemBaseline
            || !mConfirmedEquipmentSnapshot)
            return false;
        const auto revision = mConfirmedPlayerInventoryBaseline->header.canonicalRevision;
        return mConfirmedGroundItemBaseline->header.canonicalRevision == revision
            && mConfirmedEquipmentSnapshot->canonicalRevision >= revision
            && mConfirmedSnapshot->header().canonicalRevision() >= revision;
    }
}
