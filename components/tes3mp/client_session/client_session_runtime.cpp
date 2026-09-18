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
        mCharacterLifecycle = CharacterLifecycle::NewCharacter;
        mCharacterProfileRevision = CharacterProfileRevision::initial();
        mCharacterProfile.reset();
        mPendingCharacterProfile.reset();
        mLastCharacterCommandSequence.reset();
        mAuthenticationRejection.reset();
        mProtocolRejection.reset();
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
                mProtocolRejection.emplace(*rejected);
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
                mAuthenticationRejection = reason;
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
                mCharacterLifecycle = accepted->characterLifecycle();
                mCharacterProfileRevision = accepted->profileRevision();
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
            else if (auto* profile = std::get_if<ReliableCharacterProfile>(&message))
            {
                const auto sessionId = mSession->stateMachine().sessionId();
                const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
                if (profile->targetSessionGeneration != mSession->stateMachine().generation()
                    || (sessionId && profile->targetSessionId != *sessionId)
                    || profile->profile.revision() < mCharacterProfileRevision)
                    return reject();
                if (!sessionId || !snapshot)
                {
                    if (mPendingCharacterProfile)
                        return reject();
                    mPendingCharacterProfile = std::move(*profile);
                    continue;
                }
                if (profile->playerId != snapshot->header().targetPlayerId())
                    return reject();
                mCharacterLifecycle = profile->profile.lifecycle();
                mCharacterProfileRevision = profile->profile.revision();
                mCharacterProfile = std::move(*profile);
                result.characterProfileApplied = true;
            }
            else if (auto* dialogue = std::get_if<ReliableDialogueChoiceResult>(&message))
            {
                const auto sessionId = mSession->stateMachine().sessionId();
                if (!sessionId || dialogue->targetSessionId != *sessionId
                    || dialogue->targetSessionGeneration != mSession->stateMachine().generation()
                    || !negotiated(mSession->stateMachine(), dialogueChoiceCapability()))
                    return reject();
                result.dialogueChoiceResults.push_back(std::move(*dialogue));
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
                if (mPendingCharacterProfile)
                {
                    const auto sessionId = mSession->stateMachine().sessionId();
                    const auto& confirmed = mSession->stateMachine().confirmedSnapshot();
                    if (!sessionId || !confirmed || mPendingCharacterProfile->targetSessionId != *sessionId
                        || mPendingCharacterProfile->targetSessionGeneration != mSession->stateMachine().generation()
                        || mPendingCharacterProfile->playerId != confirmed->header().targetPlayerId()
                        || mPendingCharacterProfile->profile.revision() < mCharacterProfileRevision)
                        return reject();
                    mCharacterLifecycle = mPendingCharacterProfile->profile.lifecycle();
                    mCharacterProfileRevision = mPendingCharacterProfile->profile.revision();
                    mCharacterProfile = std::move(mPendingCharacterProfile);
                    mPendingCharacterProfile.reset();
                    result.characterProfileApplied = true;
                }
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
            else if (auto* objectBaseline = std::get_if<ReliableInteractiveObjectInterestBaseline>(&message))
            {
                const auto wasComplete = mSession->stateMachine().interactiveObjectInterestBaselineComplete();
                if (!mSession->stateMachine().sessionId())
                {
                    if (mSession->bindEstablishedSession(objectBaseline->targetSessionId())
                        != ClientSessionBindingResult::Bound)
                        return reject();
                }
                const auto applied
                    = mSession->receiveReliableInteractiveObjectInterestBaseline(std::move(*objectBaseline));
                if (applied != InteractiveObjectReplicationReceiveResult::Applied
                    && applied != InteractiveObjectReplicationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.interactiveObjectBaselineApplied = result.interactiveObjectBaselineApplied
                    || applied == InteractiveObjectReplicationReceiveResult::Applied;
                if (mResyncPending)
                    mResyncObjectBaselineObserved = true;
                result.interactiveObjectBaselineCompleted = result.interactiveObjectBaselineCompleted
                    || ((!wasComplete || mResyncPending)
                        && mSession->stateMachine().interactiveObjectInterestBaselineComplete());
            }
            else if (auto* playerInventory = std::get_if<ReliablePlayerInventoryBaseline>(&message))
            {
                const bool wasComplete = mSession->stateMachine().inventoryReplicationComplete();
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(playerInventory->header.targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied = mSession->receiveReliablePlayerInventoryBaseline(std::move(*playerInventory));
                if (applied != InventoryReplicationReceiveResult::Applied
                    && applied != InventoryReplicationReceiveResult::ChunkAccepted
                    && applied != InventoryReplicationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.playerInventoryApplied
                    = result.playerInventoryApplied || applied == InventoryReplicationReceiveResult::Applied;
                if (mResyncPending && applied != InventoryReplicationReceiveResult::ChunkAccepted)
                {
                    mResyncPlayerInventoryObserved = true;
                    mResyncInventoryObserved = mResyncGroundItemsObserved && mResyncEquipmentObserved;
                }
                result.inventoryReplicationCompleted = result.inventoryReplicationCompleted
                    || ((!wasComplete || (mResyncPending && mResyncInventoryObserved))
                        && mSession->stateMachine().inventoryReplicationComplete());
            }
            else if (auto* containerInventory = std::get_if<ReliableContainerInventoryBaseline>(&message))
            {
                const bool wasComplete = mSession->stateMachine().inventoryReplicationComplete();
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(containerInventory->header.targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied
                    = mSession->receiveReliableContainerInventoryBaseline(std::move(*containerInventory));
                if (applied != InventoryReplicationReceiveResult::Applied
                    && applied != InventoryReplicationReceiveResult::ChunkAccepted
                    && applied != InventoryReplicationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.containerInventoryApplied
                    = result.containerInventoryApplied || applied == InventoryReplicationReceiveResult::Applied;
                result.inventoryReplicationCompleted = result.inventoryReplicationCompleted
                    || (!wasComplete && mSession->stateMachine().inventoryReplicationComplete());
            }
            else if (auto* groundItems = std::get_if<ReliableGroundItemBaseline>(&message))
            {
                const bool wasComplete = mSession->stateMachine().inventoryReplicationComplete();
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(groundItems->header.targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied = mSession->receiveReliableGroundItemBaseline(std::move(*groundItems));
                if (applied != InventoryReplicationReceiveResult::Applied
                    && applied != InventoryReplicationReceiveResult::ChunkAccepted
                    && applied != InventoryReplicationReceiveResult::IdenticalDuplicate)
                    return reject();
                result.groundItemsApplied
                    = result.groundItemsApplied || applied == InventoryReplicationReceiveResult::Applied;
                if (mResyncPending && applied != InventoryReplicationReceiveResult::ChunkAccepted)
                {
                    mResyncGroundItemsObserved = true;
                    mResyncInventoryObserved = mResyncPlayerInventoryObserved && mResyncEquipmentObserved;
                }
                result.inventoryReplicationCompleted = result.inventoryReplicationCompleted
                    || ((!wasComplete || (mResyncPending && mResyncInventoryObserved))
                        && mSession->stateMachine().inventoryReplicationComplete());
            }
            else if (auto* equipment = std::get_if<LatestWinsEquipmentSnapshot>(&message))
            {
                const bool wasComplete = mSession->stateMachine().inventoryReplicationComplete();
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(equipment->targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied = mSession->receiveLatestWinsEquipmentSnapshot(std::move(*equipment));
                if (applied != InventoryReplicationReceiveResult::Applied
                    && applied != InventoryReplicationReceiveResult::IdenticalDuplicate
                    && applied != InventoryReplicationReceiveResult::StaleTick)
                    return reject();
                result.equipmentSnapshotApplied
                    = result.equipmentSnapshotApplied || applied == InventoryReplicationReceiveResult::Applied;
                if (mResyncPending && applied != InventoryReplicationReceiveResult::StaleTick)
                {
                    mResyncEquipmentObserved = true;
                    mResyncInventoryObserved = mResyncPlayerInventoryObserved && mResyncGroundItemsObserved;
                }
                result.inventoryReplicationCompleted = result.inventoryReplicationCompleted
                    || ((!wasComplete || (mResyncPending && mResyncInventoryObserved))
                        && mSession->stateMachine().inventoryReplicationComplete());
            }
            else if (auto* weather = std::get_if<ReliableWeatherState>(&message))
            {
                const bool wasComplete = mSession->stateMachine().weatherBaselineComplete();
                const bool completeBaseline = weather->header().completeBaseline;
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(weather->header().targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied = mSession->receiveReliableWeatherState(std::move(*weather));
                if (applied != WeatherReplicationReceiveResult::Applied
                    && applied != WeatherReplicationReceiveResult::ChunkAccepted
                    && applied != WeatherReplicationReceiveResult::IdenticalDuplicate
                    && applied != WeatherReplicationReceiveResult::StaleRevision)
                    return reject();
                result.weatherStateApplied
                    = result.weatherStateApplied || applied == WeatherReplicationReceiveResult::Applied;
                const bool completedBaseline = completeBaseline
                    && (applied == WeatherReplicationReceiveResult::Applied
                        || applied == WeatherReplicationReceiveResult::IdenticalDuplicate);
                if (mResyncPending && completedBaseline)
                    mResyncWeatherObserved = true;
                result.weatherBaselineCompleted = result.weatherBaselineCompleted
                    || ((!wasComplete || mResyncPending) && completedBaseline
                        && mSession->stateMachine().weatherBaselineComplete());
            }
            else if (auto* worldTime = std::get_if<ReliableWorldTimeState>(&message))
            {
                const bool wasComplete = mSession->stateMachine().worldTimeBaselineComplete();
                const bool completeBaseline = worldTime->completeBaseline;
                if (!mSession->stateMachine().sessionId()
                    && mSession->bindEstablishedSession(worldTime->targetSessionId)
                        != ClientSessionBindingResult::Bound)
                    return reject();
                const auto applied = mSession->receiveReliableWorldTimeState(std::move(*worldTime));
                if (applied != WorldTimeReplicationReceiveResult::Applied
                    && applied != WorldTimeReplicationReceiveResult::IdenticalDuplicate
                    && applied != WorldTimeReplicationReceiveResult::StaleRevision)
                    return reject();
                result.worldTimeStateApplied
                    = result.worldTimeStateApplied || applied == WorldTimeReplicationReceiveResult::Applied;
                if (mResyncPending && completeBaseline)
                    mResyncWorldTimeObserved = true;
                result.worldTimeBaselineCompleted = result.worldTimeBaselineCompleted
                    || ((!wasComplete || mResyncPending) && completeBaseline
                        && mSession->stateMachine().worldTimeBaselineComplete());
            }
            else if (auto* combat = std::get_if<LatestWinsCombatSnapshot>(&message))
            {
                const auto sessionId = mSession->stateMachine().sessionId();
                if (!sessionId || !negotiated(mSession->stateMachine(), combatReplicationCapability())
                    || combat->targetSessionId() != *sessionId
                    || combat->targetSessionGeneration() != mSession->stateMachine().generation())
                    return reject();
                if (mCombatSnapshot && combat->serverTick() < mCombatSnapshot->serverTick())
                    continue;
                if (mCombatSnapshot && combat->serverTick() == mCombatSnapshot->serverTick()
                    && *combat != *mCombatSnapshot)
                    return reject();
                result.combatSnapshotApplied = !mCombatSnapshot || *combat != *mCombatSnapshot;
                mCombatSnapshot = std::move(*combat);
                if (mResyncPending)
                    mResyncCombatObserved = true;
            }
            else if (auto* events = std::get_if<ReliableCombatEventBatch>(&message))
            {
                const auto sessionId = mSession->stateMachine().sessionId();
                if (!sessionId || !negotiated(mSession->stateMachine(), combatReplicationCapability())
                    || events->targetSessionId() != *sessionId
                    || events->targetSessionGeneration() != mSession->stateMachine().generation())
                    return reject();
                result.combatEvents.emplace_back(std::move(*events));
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
                || (mResyncActorBaselineObserved && mSession->stateMachine().actorInterestBaselineComplete()))
            && (!negotiated(mSession->stateMachine(), interactiveObjectReplicationCapability())
                || (mResyncObjectBaselineObserved
                    && mSession->stateMachine().interactiveObjectInterestBaselineComplete()))
            && (!negotiated(mSession->stateMachine(), inventoryReplicationCapability())
                || (mResyncInventoryObserved && mSession->stateMachine().inventoryReplicationComplete()))
            && (!negotiated(mSession->stateMachine(), combatReplicationCapability()) || mResyncCombatObserved)
            && (!negotiated(mSession->stateMachine(), weatherReplicationCapability())
                || (mResyncWeatherObserved && mSession->stateMachine().weatherBaselineComplete()))
            && (!negotiated(mSession->stateMachine(), worldTimeReplicationCapability())
                || (mResyncWorldTimeObserved && mSession->stateMachine().worldTimeBaselineComplete())))
        {
            mResyncPending = false;
            mResyncPlayerBaselineObserved = false;
            mResyncActorBaselineObserved = false;
            mResyncObjectBaselineObserved = false;
            mResyncInventoryObserved = false;
            mResyncPlayerInventoryObserved = false;
            mResyncGroundItemsObserved = false;
            mResyncEquipmentObserved = false;
            mResyncCombatObserved = false;
            mResyncWeatherObserved = false;
            mResyncWorldTimeObserved = false;
        }
        if (drained.action == ClientSessionAction::SessionClosed
            || mSession->stateMachine().state() == ClientSessionState::Closed)
        {
            fail(ClientRuntimeResult::TransportFailed);
            return { ClientRuntimeResult::TransportFailed, ClientSessionAction::SessionClosed, result.transportEvents };
        }
        if (drained.action == ClientSessionAction::SessionTimedOut
            || mSession->stateMachine().state() == ClientSessionState::TimedOut)
        {
            fail(ClientRuntimeResult::TransportFailed);
            return { ClientRuntimeResult::TransportFailed, ClientSessionAction::SessionTimedOut,
                result.transportEvents };
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

    ClientRuntimeQueueResult ClientSessionRuntime::queueInteractObject(InteractiveObjectId objectId, CellId targetCell,
        Position3 interactionOrigin, ObjectRevision expectedRevision, ObjectInteractionKind kind,
        std::optional<KeyPrototypeId> requestedKey, std::optional<ItemStackId> requestedTool,
        std::optional<InventoryRevision> expectedInventoryRevision,
        std::optional<CombatRevision> expectedCombatRevision)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!snapshot || !sessionId || !mSession->stateMachine().interestBaselineComplete()
            || (!negotiated(mSession->stateMachine(), interactiveObjectReplicationCapability())
                && !negotiated(mSession->stateMachine(), nativeDoorCapability())))
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : snapshot->header().acknowledgedCommandSequence()
            ? snapshot->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        ClientInteractObjectCommand command{ *sessionId, snapshot->header().targetSessionGeneration(), *sequence,
            *commandId, snapshot->header().canonicalRevision(), objectId, targetCell, interactionOrigin,
            expectedRevision, kind, requestedKey, requestedTool, expectedInventoryRevision, expectedCombatRevision };
        const auto encoded = encodeClientInteractObjectCommand(command);
        const auto queued = queue(MessageClass::ReliableOperation, MessageKind::ClientInteractObjectCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueInventoryTransaction(InventoryTransactionKind kind,
        ItemPrototypeId prototypeId, std::optional<ItemStackId> stackId, std::uint32_t count,
        InventoryRevision expectedInventoryRevision, Position3 interactionOrigin,
        std::optional<ContainerId> containerId, std::optional<EquipmentSlot> slot,
        std::optional<ContainerRevision> expectedContainerRevision,
        std::optional<WorldItemRevision> expectedWorldItemRevision, std::optional<DropPlacementView> placement)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!snapshot || !sessionId || !mSession->stateMachine().inventoryReplicationComplete()
            || !negotiated(mSession->stateMachine(), inventoryReplicationCapability()))
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : snapshot->header().acknowledgedCommandSequence()
            ? snapshot->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const ClientInventoryTransactionCommand command{ *sessionId, snapshot->header().targetSessionGeneration(),
            *sequence, *commandId, snapshot->header().canonicalRevision(), kind, containerId, prototypeId, stackId,
            count, slot, expectedInventoryRevision, expectedContainerRevision, expectedWorldItemRevision,
            interactionOrigin, std::move(placement) };
        const auto encoded = encodeClientInventoryTransactionCommand(command);
        if (encoded.empty())
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto queued
            = queue(MessageClass::ReliableOperation, MessageKind::ClientInventoryTransactionCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueMeleeAttack(std::optional<ActorId> target,
        ServerTick sourceTick, CombatRevision expectedAttackerRevision, CombatRevision expectedTargetRevision,
        MeleeAttackType attackType, float attackStrength)
    {
        const auto& spatial = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!spatial || !mCombatSnapshot || !sessionId
            || !negotiated(mSession->stateMachine(), combatReplicationCapability()))
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : spatial->header().acknowledgedCommandSequence()
            ? spatial->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const ClientMeleeAttackCommand command{ *sessionId, spatial->header().targetSessionGeneration(), *sequence,
            *commandId, spatial->header().canonicalRevision(), target, sourceTick, expectedAttackerRevision,
            expectedTargetRevision, attackType, attackStrength };
        const auto encoded = encodeClientMeleeAttackCommand(command);
        const auto queued = queue(MessageClass::ReliableOperation, MessageKind::ClientMeleeAttackCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueMagicUse(MagicUseSourceKind sourceKind, std::uint64_t sourceId,
        MagicUseTargetKind targetKind, std::uint64_t targetId, ServerTick sourceTick,
        CombatRevision expectedCasterRevision, CombatRevision expectedTargetRevision,
        InventoryRevision expectedInventoryRevision)
    {
        const auto& spatial = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!spatial || !mCombatSnapshot || !sessionId
            || !negotiated(mSession->stateMachine(), authoritativeInstantMagicCapability())
            || !negotiated(mSession->stateMachine(), authoritativeTimedAreaMagicCapability()))
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        const auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : spatial->header().acknowledgedCommandSequence()
            ? spatial->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId || sourceId == 0 || ((targetKind == MagicUseTargetKind::Self) != (targetId == 0)))
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const ClientMagicUseCommand command{ *sessionId, spatial->header().targetSessionGeneration(), *sequence,
            *commandId, spatial->header().canonicalRevision(), sourceKind, sourceId, targetKind, targetId, sourceTick,
            expectedCasterRevision, expectedTargetRevision, expectedInventoryRevision };
        const auto encoded = encodeClientMagicUseCommand(command);
        const auto queued = queue(MessageClass::ReliableOperation, MessageKind::ClientMagicUseCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueCharacterCreation(
        CharacterCreationChoice choice, CharacterProfileRevision expectedRevision)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!snapshot || !sessionId || mCharacterLifecycle == CharacterLifecycle::EstablishedCharacter)
            return { ClientRuntimeResult::NotConnected, std::nullopt };
        auto sequence = mLastCharacterCommandSequence ? mLastCharacterCommandSequence->next()
                                                      : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        auto commandId = CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        ClientCharacterCreationCommand command{ *sessionId, snapshot->header().targetSessionGeneration(), *sequence,
            *commandId, snapshot->header().canonicalRevision(), { expectedRevision, std::move(choice) } };
        const auto encoded = encodeClientCharacterCreationCommand(command);
        if (encoded.empty())
            return { ClientRuntimeResult::EncodeRejected, std::nullopt };
        const auto queued
            = queue(MessageClass::ReliableOperation, MessageKind::ClientCharacterCreationCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastCharacterCommandSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueDialogueChoice(
        DialogueChoiceId choice, std::optional<CommandId> retainedCommandId)
    {
        const auto& snapshot = mSession->stateMachine().confirmedSnapshot();
        const auto sessionId = mSession->stateMachine().sessionId();
        if (!snapshot || !sessionId || !mSession->stateMachine().interestBaselineComplete()
            || !negotiated(mSession->stateMachine(), dialogueChoiceCapability()))
            return { ClientRuntimeResult::NotConnected, std::nullopt, std::nullopt };
        const auto sequence = mLastQueuedSequence ? mLastQueuedSequence->next()
            : snapshot->header().acknowledgedCommandSequence()
            ? snapshot->header().acknowledgedCommandSequence()->next()
            : std::optional<CommandSequence>(CommandSequence::initial());
        if (!sequence)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt, std::nullopt };
        const auto commandId = retainedCommandId ? retainedCommandId : CommandId::fromValue(sequence->value());
        if (!commandId)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt, std::nullopt };
        const ClientDialogueChoiceCommand command{ *sessionId, snapshot->header().targetSessionGeneration(), *sequence,
            *commandId, snapshot->header().canonicalRevision(), choice };
        const auto encoded = encodeClientDialogueChoiceCommand(command);
        if (encoded.empty())
            return { ClientRuntimeResult::EncodeRejected, std::nullopt, std::nullopt };
        const auto queued = queue(MessageClass::ReliableOperation, MessageKind::ClientDialogueChoiceCommand, encoded);
        if (queued == ClientRuntimeResult::Accepted)
            mLastQueuedSequence = *sequence;
        return { queued, queued == ClientRuntimeResult::Accepted ? sequence : std::nullopt,
            queued == ClientRuntimeResult::Accepted ? commandId : std::nullopt };
    }

    ClientRuntimeQueueResult ClientSessionRuntime::queueWaitRest(std::uint8_t hours, WaitRestMode mode)
    {
        const auto& hello = mSession->stateMachine().negotiatedHello();
        if (!hello || hello->selectedVersion().major != 1 || hello->selectedVersion().minor < 8
            || !mSession->stateMachine().worldTimeBaselineComplete() || !mCombatSnapshot
            || !negotiated(mSession->stateMachine(), authoritativeWaitRestCapability()))
            return { ClientRuntimeResult::NotConnected, std::nullopt, std::nullopt };
        auto created = WaitRestRequest::create(hours, mode);
        auto* request = std::get_if<WaitRestRequest>(&created);
        if (!request)
            return { ClientRuntimeResult::EncodeRejected, std::nullopt, std::nullopt };
        return queueReliable(ReliableOperationBody(*request));
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
            mResyncObjectBaselineObserved = false;
            mResyncInventoryObserved = false;
            mResyncPlayerInventoryObserved = false;
            mResyncGroundItemsObserved = false;
            mResyncEquipmentObserved = false;
            mResyncCombatObserved = false;
            mResyncWeatherObserved = false;
            mResyncWorldTimeObserved = false;
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
        const auto priorConnection = mSession->connection();
        std::array<TransportMessage, MaximumInboundMessagesPerDrain> messages{};
        std::size_t receivedMessageCount = 0;
        bool priorReceiveFailed = false;

        if (priorConnection)
        {
            const auto received = mTransport.receive(*priorConnection, messages);
            if (received.result == TransportResult::Accepted && received.messages <= messages.size())
                receivedMessageCount = received.messages;
            else if (received.result != TransportResult::AlreadyFinalized)
                priorReceiveFailed = true;
        }

        const auto lifecycle = mSession->pump();

        if (priorReceiveFailed)
            return fail(ClientRuntimeResult::TransportFailed);

        const auto currentConnection = mSession->connection();
        if (receivedMessageCount == 0 && currentConnection && currentConnection != priorConnection)
        {
            const auto received = mTransport.receive(*currentConnection, messages);
            if (received.result != TransportResult::Accepted || received.messages > messages.size())
                return fail(ClientRuntimeResult::TransportFailed);
            receivedMessageCount = received.messages;
        }

        if (lifecycle.result != HeadlessClientResult::Accepted && receivedMessageCount == 0)
        {
            auto failed = fail(ClientRuntimeResult::TransportFailed);
            if (lifecycle.action == ClientSessionAction::SessionTimedOut)
                failed.action = ClientSessionAction::SessionTimedOut;
            return failed;
        }

        ClientRuntimeDrainResult result{ ClientRuntimeResult::Accepted, lifecycle.action, lifecycle.transportEvents };
        result.messages.reserve(receivedMessageCount);
        for (std::size_t index = 0; index < receivedMessageCount; ++index)
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
                case MessageKind::ReliableInteractiveObjectInterestBaseline:
                {
                    auto value = decodeReliableInteractiveObjectInterestBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliableInteractiveObjectInterestBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliablePlayerInventoryBaseline:
                {
                    auto value = decodeReliablePlayerInventoryBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliablePlayerInventoryBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableContainerInventoryBaseline:
                {
                    auto value = decodeReliableContainerInventoryBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliableContainerInventoryBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableGroundItemBaseline:
                {
                    auto value = decodeReliableGroundItemBaseline(frame->payload());
                    if (auto* typed = std::get_if<ReliableGroundItemBaseline>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::LatestWinsEquipmentSnapshot:
                {
                    auto value = decodeLatestWinsEquipmentSnapshot(frame->payload());
                    if (auto* typed = std::get_if<LatestWinsEquipmentSnapshot>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::LatestWinsCombatSnapshot:
                {
                    auto value = decodeLatestWinsCombatSnapshot(frame->payload());
                    if (auto* typed = std::get_if<LatestWinsCombatSnapshot>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableCombatEventBatch:
                {
                    auto value = decodeReliableCombatEventBatch(frame->payload());
                    if (auto* typed = std::get_if<ReliableCombatEventBatch>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableCharacterProfile:
                {
                    auto value = decodeReliableCharacterProfile(frame->payload());
                    if (auto* typed = std::get_if<ReliableCharacterProfile>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableDialogueChoiceResult:
                {
                    auto value = decodeReliableDialogueChoiceResult(frame->payload());
                    if (auto* typed = std::get_if<ReliableDialogueChoiceResult>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableWeatherState:
                {
                    auto value = decodeReliableWeatherState(frame->payload());
                    if (auto* typed = std::get_if<ReliableWeatherState>(&value))
                        result.messages.emplace_back(std::move(*typed));
                    else
                        return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableWorldTimeState:
                {
                    auto value = decodeReliableWorldTimeState(frame->payload());
                    if (auto* typed = std::get_if<ReliableWorldTimeState>(&value))
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

    HeadlessClientResult ClientSessionRuntime::close(TransportCloseMode mode) noexcept
    {
        mMayAcceptPlayerCredential = false;
        mOutbound.clear();
        mLocomotionHistory.clear();
        mPendingCharacterProfile.reset();
        return mSession->close(mode);
    }

    std::optional<PlayerCredential> ClientSessionRuntime::takePlayerCredential() noexcept
    {
        auto result = std::move(mPlayerCredential);
        mPlayerCredential.reset();
        return result;
    }

    std::optional<AuthenticationRejectionReason> ClientSessionRuntime::authenticationRejection() const noexcept
    {
        if (mAuthenticationRejection)
            return mAuthenticationRejection;
        return mSession ? mSession->stateMachine().authenticationRejection() : std::nullopt;
    }

    const std::optional<SessionRejected>& ClientSessionRuntime::protocolRejection() const noexcept
    {
        if (mProtocolRejection)
            return mProtocolRejection;
        static const std::optional<SessionRejected> none;
        return mSession ? mSession->stateMachine().protocolRejection() : none;
    }
}
