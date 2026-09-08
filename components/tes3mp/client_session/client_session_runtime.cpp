#include <tes3mp/client_session_runtime.hpp>

#include <algorithm>
#include <array>
#include <ranges>

namespace TES3MP
{
    namespace
    {
        bool negotiated(const ClientSessionStateMachine& session, CapabilityId capability) noexcept
        {
            const auto& hello = session.negotiatedHello();
            return hello
                && std::binary_search(
                    hello->negotiatedCapabilities().begin(), hello->negotiatedCapabilities().end(), capability);
        }
    }

    ClientRuntimeCreateResult ClientSessionRuntime::create(TransportRuntime& transport, MonotonicClock& clock,
        SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, OutboundQueuePolicy outboundPolicy)
    {
        auto created = HeadlessClientSession::create(transport, clock, timeoutPolicy, generation);
        if (auto* failure = std::get_if<SessionTransitionError>(&created))
            return *failure;
        return std::unique_ptr<ClientSessionRuntime>(new ClientSessionRuntime(
            transport, clock, std::get<std::unique_ptr<HeadlessClientSession>>(std::move(created)), outboundPolicy));
    }

    ClientSessionRuntime::ClientSessionRuntime(TransportRuntime& transport, MonotonicClock& clock,
        std::unique_ptr<HeadlessClientSession> session, OutboundQueuePolicy outboundPolicy) noexcept
        : mTransport(transport)
        , mClock(clock)
        , mSession(std::move(session))
        , mOutbound(outboundPolicy)
    {
    }

    HeadlessClientResult ClientSessionRuntime::connect(const ConnectionEndpoint& endpoint) noexcept
    {
        return mSession->connect(endpoint);
    }

    HeadlessClientResult ClientSessionRuntime::start(
        const ConnectionEndpoint& endpoint, ClientHello hello, AuthenticationRequest authentication) noexcept
    {
        mMayAcceptPlayerCredential = authentication.kind() == AuthenticationCredentialKind::JoinPassword
            && !authentication.hasPlayerCredential();
        mClientHello.emplace(std::move(hello));
        mAuthentication.emplace(std::move(authentication));
        const auto result = connect(endpoint);
        if (result != HeadlessClientResult::Accepted)
        {
            mClientHello.reset();
            mMayAcceptPlayerCredential = false;
            if (mAuthentication->kind() != AuthenticationCredentialKind::ResumeToken)
                mAuthentication.reset();
        }
        return result;
    }

    ClientRuntimeAdvanceResult ClientSessionRuntime::advance()
    {
        const auto reject = [this] {
            fail(ClientRuntimeResult::ProtocolRejected);
            return ClientRuntimeAdvanceResult{ ClientRuntimeResult::ProtocolRejected,
                ClientSessionAction::SessionClosed };
        };
        auto drained = drainInbound();
        ClientRuntimeAdvanceResult result{ drained.result, drained.action, drained.transportEvents };
        if (drained.result != ClientRuntimeResult::Accepted)
            return result;

        if (drained.action == ClientSessionAction::SendClientHello)
        {
            if (!mClientHello
                || queue(MessageClass::SessionControl, MessageKind::ClientHello, encodeClientHello(*mClientHello))
                    != ClientRuntimeResult::Accepted)
            {
                fail(ClientRuntimeResult::QueueRejected);
                return { ClientRuntimeResult::QueueRejected, ClientSessionAction::SessionClosed };
            }
        }

        for (auto& message : drained.messages)
        {
            if (auto* hello = std::get_if<ServerHello>(&message))
            {
                const auto transition = mSession->handle(ClientServerHelloReceived{ std::move(*hello) });
                if (!transition.accepted() || transition.action != ClientSessionAction::AuthenticationInputReady
                    || !mAuthentication
                    || queue(MessageClass::SessionControl, MessageKind::AuthenticationRequest,
                           encodeAuthenticationRequest(*mAuthentication))
                        != ClientRuntimeResult::Accepted
                    || !mSession->handle(ClientAuthenticationSubmitted{}).accepted())
                    return reject();
                mAuthentication.reset();
            }
            else if (auto* rejected = std::get_if<SessionRejected>(&message))
            {
                mSession->handle(ClientSessionRejectedReceived{ std::move(*rejected) });
                mMayAcceptPlayerCredential = false;
                mOutbound.clear();
                mSession->close();
                return { ClientRuntimeResult::ProtocolRejected, ClientSessionAction::SessionRejected };
            }
            else if (auto* authenticationRejected = std::get_if<AuthenticationRejectedMessage>(&message))
            {
                const auto reason = authenticationRejected->reason == AuthenticationPublicRejection::Denied
                    ? AuthenticationRejectionReason::Denied
                    : AuthenticationRejectionReason::ProviderUnavailable;
                mSession->handle(ClientAuthenticationRejected{ reason });
                mMayAcceptPlayerCredential = false;
                mOutbound.clear();
                mSession->close();
                return { ClientRuntimeResult::ProtocolRejected, ClientSessionAction::SessionRejected };
            }
            else if (auto* accepted = std::get_if<AuthenticationAcceptedMessage>(&message))
            {
                const auto transition = mSession->handle(ClientAuthenticationAccepted{});
                if (!transition.accepted() || transition.action != ClientSessionAction::SessionEstablished)
                    return reject();
                mResumeLifetimeMilliseconds = accepted->lifetimeMilliseconds();
                mResumeToken.emplace(accepted->takeToken());
                if (auto playerCredential = accepted->takePlayerCredential())
                {
                    if (!mMayAcceptPlayerCredential)
                        return reject();
                    mPlayerCredential = std::move(playerCredential);
                }
                mMayAcceptPlayerCredential = false;
                result.authenticationAccepted = true;
            }
            else if (auto* snapshot = std::get_if<LatestWinsSnapshot>(&message))
            {
                const auto wasComplete = mSession->stateMachine().interestBaselineComplete();
                if (!mSession->stateMachine().sessionId())
                {
                    if (mSession->bindEstablishedSession(snapshot->header().targetSessionId())
                        != ClientSessionBindingResult::Bound)
                        return reject();
                }
                const auto applied = mSession->receiveLatestWinsSnapshot(std::move(*snapshot));
                if (applied != LatestWinsSnapshotReceiveResult::Applied
                    && applied != LatestWinsSnapshotReceiveResult::IdenticalDuplicate
                    && applied != LatestWinsSnapshotReceiveResult::StaleTick)
                    return reject();
                result.snapshotApplied = result.snapshotApplied || applied == LatestWinsSnapshotReceiveResult::Applied;
                for (auto& pending : mPendingObservations)
                {
                    const auto observed = mSession->receiveReliableObservationBatch(std::move(pending));
                    if (observed != ReliableObservationReceiveResult::Applied
                        && observed != ReliableObservationReceiveResult::IdenticalDuplicate)
                        return reject();
                    result.observationApplied = true;
                }
                mPendingObservations.clear();
                if (!wasComplete && mSession->stateMachine().interestBaselineComplete())
                    result.baselineCompleted = true;
                if (mResyncPending && mResyncPlayerBaselineObserved
                    && mSession->stateMachine().interestBaselineComplete())
                    result.baselineCompleted = true;
            }
            else if (auto* observation = std::get_if<ReliableObservationBatch>(&message))
            {
                if (!mSession->stateMachine().sessionId())
                {
                    if (mPendingObservations.size() >= MaximumInboundMessagesPerDrain)
                        return reject();
                    mPendingObservations.emplace_back(std::move(*observation));
                    continue;
                }
                const auto applied = mSession->receiveReliableObservationBatch(std::move(*observation));
                if (applied != ReliableObservationReceiveResult::Applied
                    && applied != ReliableObservationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.observationApplied
                    = result.observationApplied || applied == ReliableObservationReceiveResult::Applied;
            }
            else if (auto* baseline = std::get_if<ReliableInterestBaseline>(&message))
            {
                const auto wasComplete = mSession->stateMachine().interestBaselineComplete();
                if (!mSession->stateMachine().sessionId())
                {
                    if (mSession->bindEstablishedSession(baseline->targetSessionId())
                        != ClientSessionBindingResult::Bound)
                        return reject();
                }
                const auto applied = mSession->receiveReliableInterestBaseline(std::move(*baseline));
                if (applied != ReliableInterestBaselineReceiveResult::Applied
                    && applied != ReliableInterestBaselineReceiveResult::IdenticalDuplicate)
                    return reject();
                result.baselineApplied
                    = result.baselineApplied || applied == ReliableInterestBaselineReceiveResult::Applied;
                if (mResyncPending)
                    mResyncPlayerBaselineObserved = true;
                if (mSession->stateMachine().interestBaselineComplete())
                {
                    result.baselineCompleted = result.baselineCompleted || !wasComplete || mResyncPending;
                }
            }
            else if (auto* actorSnapshot = std::get_if<LatestWinsActorSnapshot>(&message))
            {
                const auto wasComplete = mSession->stateMachine().actorInterestBaselineComplete();
                if (!mSession->stateMachine().sessionId())
                {
                    if (mSession->bindEstablishedSession(actorSnapshot->targetSessionId())
                        != ClientSessionBindingResult::Bound)
                        return reject();
                }
                const auto applied = mSession->receiveLatestWinsActorSnapshot(std::move(*actorSnapshot));
                if (applied != ActorReplicationReceiveResult::Applied
                    && applied != ActorReplicationReceiveResult::IdenticalDuplicate
                    && applied != ActorReplicationReceiveResult::StaleTick)
                    return reject();
                result.actorSnapshotApplied
                    = result.actorSnapshotApplied || applied == ActorReplicationReceiveResult::Applied;
                result.actorBaselineCompleted = result.actorBaselineCompleted
                    || (!wasComplete && mSession->stateMachine().actorInterestBaselineComplete());
                if (mResyncPending && mResyncActorBaselineObserved
                    && mSession->stateMachine().actorInterestBaselineComplete())
                    result.actorBaselineCompleted = true;
            }
            else if (auto* actorBaseline = std::get_if<ReliableActorInterestBaseline>(&message))
            {
                const auto wasComplete = mSession->stateMachine().actorInterestBaselineComplete();
                if (!mSession->stateMachine().sessionId())
                {
                    if (mSession->bindEstablishedSession(actorBaseline->targetSessionId())
                        != ClientSessionBindingResult::Bound)
                        return reject();
                }
                const auto applied = mSession->receiveReliableActorInterestBaseline(std::move(*actorBaseline));
                if (applied != ActorReplicationReceiveResult::Applied
                    && applied != ActorReplicationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.actorBaselineApplied
                    = result.actorBaselineApplied || applied == ActorReplicationReceiveResult::Applied;
                if (mResyncPending)
                    mResyncActorBaselineObserved = true;
                result.actorBaselineCompleted = result.actorBaselineCompleted
                    || ((!wasComplete || mResyncPending) && mSession->stateMachine().actorInterestBaselineComplete());
            }
            else if (auto* pose = std::get_if<ServerVrPoseSnapshot>(&message))
            {
                const auto sessionId = mSession->stateMachine().sessionId();
                if (mSession->stateMachine().state() != ClientSessionState::Established || !sessionId
                    || !negotiated(mSession->stateMachine(), vrPoseCapability())
                    || pose->targetSessionId() != *sessionId
                    || pose->targetSessionGeneration() != mSession->stateMachine().generation())
                    return reject();
                result.poseSnapshots.emplace_back(std::move(*pose));
            }
        }
        if (mResyncPending && mResyncPlayerBaselineObserved && mSession->stateMachine().interestBaselineComplete()
            && (!negotiated(mSession->stateMachine(), actorReplicationCapability())
                || (mResyncActorBaselineObserved && mSession->stateMachine().actorInterestBaselineComplete())))
        {
            mResyncPending = false;
            mResyncPlayerBaselineObserved = false;
            mResyncActorBaselineObserved = false;
        }
        return result;
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueMotionIntent(PlayerMotionIntent intent)
    {
        return queueReliable(ReliableOperationBody(std::move(intent)));
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueLocomotionIntent(LocomotionIntent intent)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto& negotiatedHello = mSession->stateMachine().negotiatedHello();
        if (!snapshot || !negotiatedHello || negotiatedHello->selectedVersion().major != 1
            || negotiatedHello->selectedVersion().minor < 3
            || mLocomotionHistory.size() == MaximumRetainedLocomotionInputs)
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        const auto self = std::ranges::find_if(snapshot->view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot->header().targetPlayerId()
                && entry.entityId() == snapshot->header().targetEntityId();
        });
        if (self == snapshot->view().entries().end())
            return { ClientRuntimeResult::ProtocolRejected, std::nullopt };

        std::uint64_t tickValue = self->serverTick().value();
        if (mLastLocomotionInputTick && tickValue <= mLastLocomotionInputTick->value())
            tickValue = mLastLocomotionInputTick->value() + 1;
        const auto inputTick = LocomotionInputTick::fromValue(tickValue);
        const auto inputSequence = mLastLocomotionInputSequence
            ? mLastLocomotionInputSequence->next()
            : std::optional<LocomotionInputSequence>(LocomotionInputSequence::initial());
        if (!inputTick || !inputSequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };

        const PlayerLocomotionInput input(*inputTick, *inputSequence, intent);
        auto queued = queueReliable(ReliableOperationBody(input));
        if (queued.result == ClientRuntimeResult::Accepted
            && (!queued.sequence || !mLocomotionHistory.retain(*queued.sequence, input)))
            return { ClientRuntimeResult::QueueRejected, std::nullopt };
        if (queued.result == ClientRuntimeResult::Accepted)
        {
            mLastLocomotionInputTick = *inputTick;
            mLastLocomotionInputSequence = *inputSequence;
        }
        return queued;
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueCellTransition(CellTransition transition)
    {
        auto queued = queueReliable(ReliableOperationBody(std::move(transition)));
        if (queued.result == ClientRuntimeResult::Accepted)
            mLocomotionHistory.clear();
        return queued;
    }

    std::optional<LocalLocomotionReconciliation> ClientSessionRuntime::reconcileLocalPresentation(
        bool hardDiscontinuity) noexcept
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        if (!snapshot)
            return std::nullopt;
        const auto self = std::ranges::find_if(snapshot->view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot->header().targetPlayerId()
                && entry.entityId() == snapshot->header().targetEntityId();
        });
        if (self == snapshot->view().entries().end())
            return std::nullopt;
        return mLocomotionHistory.reconcile(*self, snapshot->header().acknowledgedCommandSequence(), hardDiscontinuity);
    }

    ClientRuntimeResult ClientSessionRuntime::queuePoseSample(const ClientVrPoseSample& sample)
    {
        const auto sessionId = mSession->stateMachine().sessionId();
        if (mSession->stateMachine().state() != ClientSessionState::Established || !sessionId)
            return ClientRuntimeResult::NotConnected;
        if (!negotiated(mSession->stateMachine(), vrPoseCapability()) || sample.sourceSessionId() != *sessionId
            || sample.sourceSessionGeneration() != mSession->stateMachine().generation())
            return ClientRuntimeResult::ProtocolRejected;
        return queue(
            MessageClass::PresentationSample, MessageKind::ClientVrPoseSample, encodeClientVrPoseSample(sample));
    }

    ClientRuntimeResult ClientSessionRuntime::requestResync(ResyncReason reason)
    {
        const auto sessionId = mSession->stateMachine().sessionId();
        if (mSession->stateMachine().state() != ClientSessionState::Established || !sessionId)
            return ClientRuntimeResult::NotConnected;
        if (mResyncPending)
            return ClientRuntimeResult::Accepted;
        const auto version = mSession->stateMachine().confirmedInterestBaseline()
            ? mSession->stateMachine().confirmedInterestBaseline()->canonicalStateVersion()
            : CanonicalStateVersion::initial();
        auto created = SessionResyncRequest::create(*sessionId, mSession->stateMachine().generation(), reason, version);
        auto* request = std::get_if<SessionResyncRequest>(&created);
        if (!request)
            return ClientRuntimeResult::EncodeRejected;
        const auto result = queue(
            MessageClass::SessionControl, MessageKind::SessionResyncRequest, encodeSessionResyncRequest(*request));
        if (result == ClientRuntimeResult::Accepted)
        {
            mResyncPending = true;
            mResyncPlayerBaselineObserved = false;
            mResyncActorBaselineObserved = false;
        }
        return result;
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueReliable(ReliableOperationBody body)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!snapshot || !sessionId || !mSession->stateMachine().interestBaselineComplete())
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        const auto self = std::ranges::find_if(snapshot->view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot->header().targetPlayerId()
                && entry.entityId() == snapshot->header().targetEntityId();
        });
        if (self == snapshot->view().entries().end())
            return { ClientRuntimeResult::ProtocolRejected, std::nullopt };
        auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : snapshot->header().acknowledgedCommandSequence()
            ? snapshot->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        ClientCommandHeader header(*sessionId, snapshot->header().targetSessionGeneration(), *sequence, *commandId,
            snapshot->header().canonicalRevision());
        ReliableOperationHeader reliable(
            header, EntityPrecondition(self->entityId(), self->entityRevision(), self->authorityEpoch()));
        auto operation = std::visit(
            [&](auto&& value) { return ReliableOperation::create(reliable, std::forward<decltype(value)>(value)); },
            std::move(body));
        auto* value = std::get_if<ReliableOperation>(&operation);
        if (!value)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto queued
            = queue(MessageClass::ReliableOperation, MessageKind::ReliableOperation, encodeReliableOperation(*value));
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    std::optional<ResumeToken> ClientSessionRuntime::takeResumeToken() noexcept
    {
        auto result = std::move(mResumeToken);
        mResumeToken.reset();
        return result;
    }

    std::optional<ResumeToken> ClientSessionRuntime::takeUnsubmittedResumeToken() noexcept
    {
        if (!mAuthentication || mAuthentication->kind() != AuthenticationCredentialKind::ResumeToken)
            return std::nullopt;
        auto result = mAuthentication->takeResumeToken();
        mAuthentication.reset();
        return result;
    }

    ClientRuntimeDrainResult ClientSessionRuntime::fail(ClientRuntimeResult result) noexcept
    {
        mMayAcceptPlayerCredential = false;
        mOutbound.clear();
        mLocomotionHistory.clear();
        mSession->close();
        return { result, ClientSessionAction::SessionClosed };
    }

    ClientRuntimeDrainResult ClientSessionRuntime::drainInbound()
    {
        const auto lifecycle = mSession->pump();
        if (lifecycle.result != HeadlessClientResult::Accepted)
            return fail(ClientRuntimeResult::TransportFailed);
        ClientRuntimeDrainResult result{ ClientRuntimeResult::Accepted, lifecycle.action, lifecycle.transportEvents };
        const auto connection = mSession->connection();
        if (!connection)
            return result;

        std::array<TransportMessage, MaximumInboundMessagesPerDrain> messages{};
        const auto received = mTransport.receive(*connection, messages);
        if (received.result != TransportResult::Accepted || received.messages > messages.size())
            return fail(ClientRuntimeResult::TransportFailed);
        result.messages.reserve(received.messages);
        for (std::size_t index = 0; index < received.messages; ++index)
        {
            auto decoded = decodeProtocolFrame(messages[index].bytes);
            auto* frame = std::get_if<DecodedFrame>(&decoded);
            if (!frame || !isMessageClassAllowedOnTransportChannel(frame->messageClass(), messages[index].channel))
                return fail(ClientRuntimeResult::ProtocolRejected);

            switch (frame->messageKind())
            {
                case MessageKind::ServerHello:
                {
                    auto value = decodeServerHello(frame->payload());
                    if (auto* typed = std::get_if<ServerHello>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::SessionRejected:
                {
                    auto value = decodeSessionRejected(frame->payload());
                    if (auto* typed = std::get_if<SessionRejected>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::AuthenticationAccepted:
                {
                    auto value = decodeAuthenticationAccepted(frame->payload());
                    if (auto* typed = std::get_if<AuthenticationAcceptedMessage>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::AuthenticationRejected:
                {
                    auto value = decodeAuthenticationRejected(frame->payload());
                    if (auto* typed = std::get_if<AuthenticationRejectedMessage>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::LatestWinsSnapshot:
                {
                    auto value = decodeLatestWinsSnapshot(frame->payload());
                    if (auto* typed = std::get_if<LatestWinsSnapshot>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableObservationBatch:
                {
                    auto value = decodeReliableObservationBatch(frame->payload());
                    if (auto* typed = std::get_if<ReliableObservationBatch>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableInterestBaseline:
                {
                    auto value = decodeReliableInterestBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliableInterestBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::LatestWinsActorSnapshot:
                {
                    auto value = decodeLatestWinsActorSnapshot(frame->payload());
                    if (auto* typed = std::get_if<LatestWinsActorSnapshot>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableActorInterestBaseline:
                {
                    auto value = decodeReliableActorInterestBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliableActorInterestBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ServerVrPoseSnapshot:
                {
                    auto value = decodeServerVrPoseSnapshot(frame->payload());
                    if (auto* typed = std::get_if<ServerVrPoseSnapshot>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                default:
                    return fail(ClientRuntimeResult::ProtocolRejected);
            }
        }
        return result;
    }

    ClientRuntimeResult ClientSessionRuntime::queue(
        MessageClass messageClass, MessageKind kind, std::span<const std::byte> payload)
    {
        const auto channel = transportChannelFor(messageClass);
        auto encoded = encodeProtocolFrame(messageClass, kind, payload);
        auto* bytes = std::get_if<std::vector<std::byte>>(&encoded);
        if (!channel || !bytes)
            return ClientRuntimeResult::EncodeRejected;
        const auto queued = mOutbound.enqueue(*channel, *bytes);
        return queued == TransportResult::Accepted ? ClientRuntimeResult::Accepted : ClientRuntimeResult::QueueRejected;
    }

    ClientRuntimeResult ClientSessionRuntime::flushOutbound() noexcept
    {
        const auto connection = mSession->connection();
        if (!connection)
            return mOutbound.reliableMessages() == 0 && !mOutbound.hasLatest() ? ClientRuntimeResult::Accepted
                                                                               : ClientRuntimeResult::NotConnected;
        const auto nowMilliseconds = mClock.now().nanoseconds() / 1'000'000;
        const auto result = mOutbound.pump(mTransport, *connection, nowMilliseconds);
        if (result == OutboundPumpResult::Progress || result == OutboundPumpResult::Idle
            || result == OutboundPumpResult::Blocked)
            return ClientRuntimeResult::Accepted;
        mOutbound.clear();
        mSession->close();
        return ClientRuntimeResult::TransportFailed;
    }

    HeadlessClientResult ClientSessionRuntime::close() noexcept
    {
        mMayAcceptPlayerCredential = false;
        mOutbound.clear();
        mLocomotionHistory.clear();
        return mSession->close();
    }

    std::optional<PlayerCredential> ClientSessionRuntime::takePlayerCredential() noexcept
    {
        auto result = std::move(mPlayerCredential);
        mPlayerCredential.reset();
        return result;
    }
}
