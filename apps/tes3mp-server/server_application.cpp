#include "server_application.hpp"
#include "actor_interest_projection.hpp"
#include "combat_interest_projection.hpp"
#include "interactive_object_interest_projection.hpp"
#include "interest_projection.hpp"
#include "inventory_interest_projection.hpp"
#include "tes3mp/canonical_resync.hpp"

#include <algorithm>
#include <array>
#include <ranges>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::uint64_t RejectionDrainNanoseconds = 250'000'000;
        constexpr std::size_t MaximumRetainedDialogueChoiceResults
            = MaximumCanonicalActiveSessions * MaximumFinalizedCommandHistory;

        bool supportsPose(const ServerSessionStateMachine& session) noexcept
        {
            const auto& hello = session.negotiatedHello();
            return hello
                && std::binary_search(
                    hello->negotiatedCapabilities().begin(), hello->negotiatedCapabilities().end(), vrPoseCapability());
        }

        CombatSimulationPolicy combatSimulationPolicy(std::uint64_t tickMilliseconds, std::int16_t difficulty) noexcept
        {
            const auto ticksFor = [tickMilliseconds](std::uint64_t milliseconds) {
                return std::max<std::uint64_t>(1, (milliseconds + tickMilliseconds - 1) / tickMilliseconds);
            };
            return { ticksFor(1000), ticksFor(30000), static_cast<float>(tickMilliseconds) / 1000.f, difficulty };
        }
    }

    ServerApplication::ServerApplication(TransportRuntime& transport, const ServerConfig& config) noexcept
        : mTransport(transport)
        , mConfig(config)
    {
    }

    ServerApplication::ServerApplication(
        TransportRuntime& transport, const ServerConfig& config, ServerApplicationWiring wiring) noexcept
        : mTransport(transport)
        , mConfig(config)
        , mWiring(wiring)
    {
    }

    ServerApplication::~ServerApplication()
    {
        stop();
    }

    bool ServerApplication::supportsActors(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* session = mWiring->sessions.session(connection);
        if (!session || session->state() != ServerSessionState::Established || !session->sessionId())
            return false;
        const auto& hello = session->negotiatedHello();
        return hello && std::ranges::binary_search(hello->negotiatedCapabilities(), actorReplicationCapability());
    }

    bool ServerApplication::supportsInteractiveObjects(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* session = mWiring->sessions.session(connection);
        if (!session || session->state() != ServerSessionState::Established || !session->sessionId())
            return false;
        const auto& hello = session->negotiatedHello();
        return hello
            && std::ranges::binary_search(hello->negotiatedCapabilities(), interactiveObjectReplicationCapability());
    }

    bool ServerApplication::supportsInventory(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* session = mWiring->sessions.session(connection);
        if (!session || session->state() != ServerSessionState::Established || !session->sessionId())
            return false;
        const auto& hello = session->negotiatedHello();
        return hello && std::ranges::binary_search(hello->negotiatedCapabilities(), inventoryReplicationCapability());
    }

    bool ServerApplication::supportsCombat(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* session = mWiring->sessions.session(connection);
        if (!session || session->state() != ServerSessionState::Established || !session->sessionId())
            return false;
        const auto& hello = session->negotiatedHello();
        return hello && std::ranges::binary_search(hello->negotiatedCapabilities(), combatReplicationCapability());
    }

    bool ServerApplication::supportsCharacterCreation(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* state = mWiring->sessions.session(connection);
        if (!state)
            return false;
        const auto& hello = state->negotiatedHello();
        return hello && std::ranges::binary_search(hello->negotiatedCapabilities(), characterCreationCapability());
    }

    bool ServerApplication::supportsDialogueChoices(TransportConnectionId connection) const noexcept
    {
        if (!mWiring)
            return false;
        const auto* session = mWiring->sessions.session(connection);
        if (!session || session->state() != ServerSessionState::Established || !session->sessionId())
            return false;
        const auto& hello = session->negotiatedHello();
        return hello && std::ranges::binary_search(hello->negotiatedCapabilities(), dialogueChoiceCapability());
    }

    bool ServerApplication::start() noexcept
    {
        if (mRunning || mListener)
        {
            mFailure = "server already started";
            return false;
        }
        const auto admitted = mTransport.startListener(mConfig.endpoint);
        if (admitted.result != TransportResult::Accepted || !admitted.id)
        {
            mFailure = "listener start rejected";
            mTransport.shutdown();
            return false;
        }
        mListener = admitted.id;
        mRunning = true;
        mFailure = {};
        return true;
    }

    bool ServerApplication::pump() noexcept
    {
        return pump(ServerTick::initial());
    }

    bool ServerApplication::failConnection(TransportConnectionId connection, std::string_view failure) noexcept
    {
        mRejectedCloseDeadlines.erase(connection);
        if (mWiring)
        {
            if (auto* session = mWiring->sessions.session(connection); session && session->sessionId())
                mLatestPoses.erase(*session->sessionId());
            (void)mWiring->sessions.close(connection);
        }
        (void)mTransport.close(connection, TransportCloseMode::Abort);
        mFailure = failure;
        return true;
    }

    bool ServerApplication::disconnectConnection(TransportConnectionId connection, ServerTick tick) noexcept
    {
        return disconnectConnections(std::span<const TransportConnectionId>(&connection, 1), tick);
    }

    bool ServerApplication::disconnectConnections(
        std::span<const TransportConnectionId> connections, ServerTick tick) noexcept
    {
        std::vector<SessionId> sessionIds;
        std::vector<TransportConnectionId> knownConnections;
        sessionIds.reserve(connections.size());
        knownConnections.reserve(connections.size());
        for (const auto connection : connections)
        {
            auto* session = mWiring->sessions.session(connection);
            if (!session)
                continue;
            knownConnections.push_back(connection);
            if (session->sessionId())
                sessionIds.push_back(*session->sessionId());
        }
        if (knownConnections.empty())
            return true;
        if (sessionIds.empty())
        {
            for (const auto connection : knownConnections)
                if (mWiring->sessions.close(connection) != ConnectionSessionResult::Accepted)
                    return false;
            return true;
        }
        const auto before = mWiring->reducer.state();
        auto prepared = mWiring->lifecycle.prepareDisconnectBatch(sessionIds, mWiring->clock.now(), tick);
        auto* lifecycle = std::get_if<ServerLifecycleBatchPreparation>(&prepared);
        if (!lifecycle)
            return false;
        const auto cancel = [this, id = lifecycle->id]() noexcept { (void)mWiring->lifecycle.cancel(id); };
        const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
        const auto revision = mWiring->lifecycle.candidateRevision(lifecycle->id);
        auto projected
            = candidate && revision ? projectInterestChanges(before, *candidate, tick, *revision) : std::nullopt;
        if (!projected)
        {
            cancel();
            return false;
        }
        std::vector<std::pair<TransportConnectionId, InterestDelivery>> routed;
        routed.reserve(projected->size());
        for (auto& delivery : *projected)
        {
            auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
            if (!target || std::ranges::find(knownConnections, *target) != knownConnections.end())
            {
                cancel();
                return false;
            }
            routed.emplace_back(*target, std::move(delivery));
        }
        std::vector<PlayerId> players;
        players.reserve(lifecycle->entries.size());
        for (const auto& entry : lifecycle->entries)
            players.push_back(entry.player);
        if (!mWiring->joins.persistPlayers(players))
        {
            cancel();
            return false;
        }
        if (!admitInterestChangesAtomically(mWiring->queues, routed))
        {
            cancel();
            return false;
        }
        if (!mWiring->lifecycle.commit(lifecycle->id))
            return false;
        for (const auto connection : knownConnections)
            if (mWiring->sessions.close(connection) != ConnectionSessionResult::Accepted)
                return false;
        for (const auto session : sessionIds)
            mLatestPoses.erase(session);
        return true;
    }

    bool ServerApplication::relayPose(TransportConnectionId connection, const TransportMessage& message) noexcept
    try
    {
        if (!mWiring || message.channel != TransportChannel::PresentationLatest)
            return false;
        auto* sourceState = mWiring->sessions.session(connection);
        if (!sourceState || sourceState->state() != ServerSessionState::Established || !sourceState->sessionId()
            || !supportsPose(*sourceState))
            return false;
        auto decodedFrame = decodeProtocolFrame(message.bytes);
        auto* frame = std::get_if<DecodedFrame>(&decodedFrame);
        if (!frame || frame->messageClass() != MessageClass::PresentationSample
            || frame->messageKind() != MessageKind::ClientVrPoseSample)
            return false;
        auto decodedPose = decodeClientVrPoseSample(frame->payload());
        auto* pose = std::get_if<ClientVrPoseSample>(&decodedPose);
        if (!pose || pose->sourceSessionId() != *sourceState->sessionId()
            || pose->sourceSessionGeneration() != sourceState->generation())
            return false;

        const auto& canonical = mWiring->reducer.state();
        const auto* sourceSession = canonical.findActiveSession(*sourceState->sessionId());
        const auto* sourcePlayer = sourceSession ? canonical.findPlayer(sourceSession->playerId()) : nullptr;
        if (!sourceSession || !sourcePlayer || sourceSession->sessionGeneration() != pose->sourceSessionGeneration()
            || sourceSession->entityId() != pose->rootEntityId() || sourcePlayer->entityId() != pose->rootEntityId()
            || sourcePlayer->authorityEpoch() != pose->rootAuthorityEpoch())
            return false;

        const auto retained = mLatestPoses.find(pose->sourceSessionId());
        if (retained != mLatestPoses.end())
        {
            const auto recency
                = classifyPoseSample(retained->second.sample, retained->second.payload, *pose, frame->payload());
            if (recency == PoseSampleRecency::Duplicate || recency == PoseSampleRecency::Stale)
                return true;
            if (recency == PoseSampleRecency::ConflictingDuplicate)
                return false;
        }

        std::vector<std::vector<std::byte>> ownedFrames;
        std::vector<OutboundQueueSet::AtomicMessage> messages;
        ownedFrames.reserve(mWiring->sessions.size());
        messages.reserve(mWiring->sessions.size());
        for (const auto targetConnection : mWiring->sessions.connections())
        {
            if (targetConnection == connection)
                continue;
            auto* targetState = mWiring->sessions.session(targetConnection);
            if (!targetState || targetState->state() != ServerSessionState::Established || !targetState->sessionId()
                || !supportsPose(*targetState))
                continue;
            if (!sharesInterest(canonical, *targetState->sessionId(), *sourceState->sessionId()))
                continue;
            ServerVrPoseSnapshot snapshot(*targetState->sessionId(), targetState->generation(),
                sourcePlayer->playerId(), pose->sourceSessionId(), pose->sourceSessionGeneration(),
                pose->rootEntityId(), pose->rootAuthorityEpoch(), pose->sampleSequence(), pose->head(),
                pose->leftHand(), pose->rightHand());
            auto encoded = encodeProtocolFrame(MessageClass::PresentationSample, MessageKind::ServerVrPoseSnapshot,
                encodeServerVrPoseSnapshot(snapshot));
            auto* bytes = std::get_if<std::vector<std::byte>>(&encoded);
            if (!bytes)
                return false;
            ownedFrames.emplace_back(std::move(*bytes));
            messages.push_back({ targetConnection, TransportChannel::PresentationLatest, ownedFrames.back() });
        }
        if (!messages.empty() && mWiring->queues.enqueueMessagesAtomically(messages) != TransportResult::Accepted)
            return false;
        mLatestPoses.insert_or_assign(pose->sourceSessionId(),
            RetainedPose{ std::move(*pose), std::vector<std::byte>(frame->payload().begin(), frame->payload().end()) });
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool ServerApplication::resumeConnection(TransportConnectionId connection, ServerTick tick) noexcept
    {
        auto* session = mWiring->sessions.session(connection);
        if (!session || !session->principal() || !session->sessionId() || !session->preparedResumeId())
            return false;
        const auto before = mWiring->reducer.state();
        auto prepared = mWiring->lifecycle.prepareResume(
            *session->principal(), *session->sessionId(), mWiring->clock.now(), tick);
        auto* lifecycle = std::get_if<ServerLifecyclePreparation>(&prepared);
        if (!lifecycle || lifecycle->generation != session->generation())
        {
            if (lifecycle)
                (void)mWiring->lifecycle.cancel(lifecycle->id);
            (void)session->cancelPreparedResume();
            return false;
        }
        const auto cancel = [&]() noexcept {
            (void)mWiring->lifecycle.cancel(lifecycle->id);
            (void)session->cancelPreparedResume();
        };
        const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
        const auto revision = mWiring->lifecycle.candidateRevision(lifecycle->id);
        const auto stateVersion = mWiring->lifecycle.candidateStateVersion(lifecycle->id);
        auto baseline = candidate && revision && stateVersion
            ? projectInterestBaseline(*candidate, *session->sessionId(), tick, *revision, *stateVersion)
            : std::nullopt;
        auto observations
            = candidate && revision ? projectInterestChanges(before, *candidate, tick, *revision) : std::nullopt;
        auto accepted = session->takeAuthenticationAccepted();
        const auto* resumedSession = candidate ? candidate->findActiveSession(*session->sessionId()) : nullptr;
        const auto* resumedProfile
            = resumedSession ? mWiring->joins.characterProfile(resumedSession->playerId()) : nullptr;
        if (accepted && resumedProfile)
        {
            auto withCharacterState
                = AuthenticationAcceptedMessage::create(accepted->takeToken(), accepted->lifetimeMilliseconds(),
                    std::nullopt, resumedProfile->lifecycle(), resumedProfile->revision());
            if (!withCharacterState)
            {
                cancel();
                return false;
            }
            accepted = std::move(*withCharacterState);
        }
        auto actorBaseline = candidate && mWiring->actors && supportsActors(connection)
            ? projectActorInterestBaseline(*candidate, *mWiring->actors, *session->sessionId(), tick, *revision)
            : std::optional<ActorInterestBaselineDelivery>{};
        auto objectBaseline = candidate && mWiring->interactiveObjects && supportsInteractiveObjects(connection)
            ? projectInteractiveObjectInterestBaseline(
                  *candidate, *mWiring->interactiveObjects, *session->sessionId(), tick, *revision)
            : std::optional<InteractiveObjectInterestBaselineDelivery>{};
        auto inventoryBaseline = candidate && mWiring->inventory && supportsInventory(connection)
            ? projectInventoryInterestBaseline(*candidate, *mWiring->inventory, *session->sessionId(), tick, *revision)
            : std::optional<InventoryInterestDelivery>{};
        auto combatSnapshot = candidate && revision && mWiring->combat && mWiring->actors && supportsCombat(connection)
            ? projectCombatSnapshot(
                  *candidate, *mWiring->actors, *mWiring->combat, *session->sessionId(), tick, *revision)
            : std::optional<LatestWinsCombatSnapshot>{};
        if (!candidate || !baseline || !observations || !accepted || (supportsActors(connection) && !actorBaseline)
            || (supportsInteractiveObjects(connection) && !objectBaseline)
            || (supportsInventory(connection) && !inventoryBaseline) || (supportsCombat(connection) && !combatSnapshot))
        {
            cancel();
            return false;
        }

        try
        {
            std::vector<std::vector<std::byte>> frames;
            frames.reserve(8 + observations->size() * 2);
            auto addFrame = [&](MessageClass messageClass, MessageKind kind, std::vector<std::byte> payload) {
                auto encoded = encodeProtocolFrame(messageClass, kind, payload);
                if (!std::holds_alternative<std::vector<std::byte>>(encoded))
                    return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(encoded)));
                return true;
            };
            if (!addFrame(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
                    encodeAuthenticationAccepted(*accepted))
                || !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
                    encodeReliableInterestBaseline(baseline->baseline))
                || !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                    encodeLatestWinsSnapshot(baseline->view)))
            {
                cancel();
                return false;
            }
            const bool characterCapable = supportsCharacterCreation(connection);
            if (characterCapable
                && !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableCharacterProfile,
                    encodeReliableCharacterProfile({ *session->sessionId(), session->generation(),
                        resumedSession->playerId(), CharacterConfirmationResult::Confirmed, *resumedProfile })))
            {
                cancel();
                return false;
            }
            if (actorBaseline
                && (!addFrame(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
                        encodeReliableActorInterestBaseline(actorBaseline->baseline))
                    || !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsActorSnapshot,
                        encodeLatestWinsActorSnapshot(actorBaseline->view))))
            {
                cancel();
                return false;
            }
            if (objectBaseline
                && !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableInteractiveObjectInterestBaseline,
                    encodeReliableInteractiveObjectInterestBaseline(objectBaseline->baseline)))
            {
                cancel();
                return false;
            }
            if (combatSnapshot
                && !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsCombatSnapshot,
                    encodeLatestWinsCombatSnapshot(*combatSnapshot)))
            {
                cancel();
                return false;
            }

            std::vector<OutboundQueueSet::AtomicMessage> messages;
            messages.reserve(8 + observations->size() * 2);
            std::size_t nextFrameIdx = 0;
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextFrameIdx++] });
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextFrameIdx++] });
            messages.push_back({ connection, TransportChannel::LatestWins, frames[nextFrameIdx++] });
            if (characterCapable)
                messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextFrameIdx++] });
            if (actorBaseline)
            {
                messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextFrameIdx++] });
                messages.push_back({ connection, TransportChannel::LatestWins, frames[nextFrameIdx++] });
            }
            if (objectBaseline)
            {
                messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextFrameIdx++] });
            }
            if (combatSnapshot)
                messages.push_back({ connection, TransportChannel::LatestWins, frames[nextFrameIdx++] });
            for (const auto& delivery : *observations)
            {
                auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
                if (!target
                    || !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableObservationBatch,
                        encodeReliableObservationBatch(delivery.observations))
                    || !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                        encodeLatestWinsSnapshot(delivery.view)))
                {
                    cancel();
                    return false;
                }
                messages.push_back({ *target, TransportChannel::ReliableOrdered, frames[frames.size() - 2] });
                messages.push_back({ *target, TransportChannel::LatestWins, frames.back() });
            }
            if (inventoryBaseline && !appendInventoryInterestMessages(frames, messages, connection, *inventoryBaseline))
            {
                cancel();
                return false;
            }
            if (mWiring->queues.enqueueMessagesAtomically(messages) != TransportResult::Accepted)
            {
                cancel();
                return false;
            }
        }
        catch (...)
        {
            cancel();
            return false;
        }

        if (!session->commitPreparedResume())
        {
            (void)mWiring->lifecycle.cancel(lifecycle->id);
            return false;
        }
        if (!mWiring->lifecycle.commit(lifecycle->id))
        {
            (void)session->rollbackPreparedResume();
            return false;
        }
        return session->finalizePreparedResume();
    }

    bool ServerApplication::resyncConnection(TransportConnectionId connection, ServerTick tick) noexcept
    {
        auto request = mWiring->sessions.takeResyncRequest(connection);
        if (!request)
            return true;
        const auto resolved = resolveCanonicalResync(*request, mWiring->reducer.latestPublication());
        if (resolved.disposition() != CanonicalResyncDisposition::SnapshotRequired || !resolved.publication())
            return false;
        auto delivery = projectInterestBaseline(resolved.publication()->state(), request->sessionId(),
            resolved.publication()->checkpointTick(), mWiring->reducer.canonicalRevision(),
            resolved.publication()->stateVersion());
        if (!delivery)
            return false;
        const bool wantActors = supportsActors(connection);
        const bool wantObjects = supportsInteractiveObjects(connection);
        const bool wantInventory = supportsInventory(connection);
        const bool wantCombat = supportsCombat(connection);
        if (!wantActors && !wantObjects && !wantInventory && !wantCombat)
            return admitInterestBaseline(mWiring->queues, connection, *delivery);
        if (wantActors && !mWiring->actors)
            return false;
        if (wantObjects && !mWiring->interactiveObjects)
            return false;
        if (wantInventory && !mWiring->inventory)
            return false;
        if (wantCombat && (!mWiring->combat || !mWiring->actors))
            return false;
        auto actorDelivery = wantActors
            ? projectActorInterestBaseline(resolved.publication()->state(), *mWiring->actors, request->sessionId(),
                  tick, mWiring->reducer.canonicalRevision())
            : std::nullopt;
        if (wantActors && !actorDelivery)
            return false;
        auto objectDelivery = wantObjects
            ? projectInteractiveObjectInterestBaseline(resolved.publication()->state(), *mWiring->interactiveObjects,
                  request->sessionId(), tick, mWiring->reducer.canonicalRevision())
            : std::nullopt;
        if (wantObjects && !objectDelivery)
            return false;
        auto inventoryDelivery = wantInventory
            ? projectInventoryInterestBaseline(resolved.publication()->state(), *mWiring->inventory,
                  request->sessionId(), tick, mWiring->reducer.canonicalRevision())
            : std::nullopt;
        if (wantInventory && !inventoryDelivery)
            return false;
        auto combatDelivery = wantCombat
            ? projectCombatSnapshot(resolved.publication()->state(), *mWiring->actors, *mWiring->combat,
                  request->sessionId(), tick, mWiring->reducer.canonicalRevision())
            : std::nullopt;
        if (wantCombat && !combatDelivery)
            return false;
        try
        {
            std::vector<std::vector<std::byte>> frames;
            frames.reserve(7);
            const auto add = [&](MessageClass messageClass, MessageKind kind, std::vector<std::byte> payload) {
                auto frame = encodeProtocolFrame(messageClass, kind, payload);
                if (!std::holds_alternative<std::vector<std::byte>>(frame))
                    return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
                return true;
            };
            if (!add(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
                    encodeReliableInterestBaseline(delivery->baseline))
                || !add(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                    encodeLatestWinsSnapshot(delivery->view)))
                return false;
            if (wantActors
                && (!add(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
                        encodeReliableActorInterestBaseline(actorDelivery->baseline))
                    || !add(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsActorSnapshot,
                        encodeLatestWinsActorSnapshot(actorDelivery->view))))
                return false;
            if (wantObjects
                && !add(MessageClass::ReliableOperation, MessageKind::ReliableInteractiveObjectInterestBaseline,
                    encodeReliableInteractiveObjectInterestBaseline(objectDelivery->baseline)))
                return false;
            if (wantCombat
                && !add(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsCombatSnapshot,
                    encodeLatestWinsCombatSnapshot(*combatDelivery)))
                return false;
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            messages.reserve(frames.size());
            std::size_t nextIdx = 0;
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextIdx++] });
            messages.push_back({ connection, TransportChannel::LatestWins, frames[nextIdx++] });
            if (wantActors)
            {
                messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextIdx++] });
                messages.push_back({ connection, TransportChannel::LatestWins, frames[nextIdx++] });
            }
            if (wantObjects)
            {
                messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[nextIdx++] });
            }
            if (wantCombat)
                messages.push_back({ connection, TransportChannel::LatestWins, frames[nextIdx++] });
            if (wantInventory && !appendInventoryInterestMessages(frames, messages, connection, *inventoryDelivery))
                return false;
            return mWiring->queues.enqueueMessagesAtomically(messages) == TransportResult::Accepted;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ServerApplication::expireSessions(ServerTick tick) noexcept
    {
        while (true)
        {
            const auto before = mWiring->reducer.state();
            auto prepared = mWiring->lifecycle.prepareNextExpiration(mWiring->clock.now(), tick);
            if (const auto* error = std::get_if<ServerLifecycleError>(&prepared))
            {
                if (*error == ServerLifecycleError::DeadlineReached)
                    return true;
                return false;
            }
            auto* lifecycle = std::get_if<ServerLifecyclePreparation>(&prepared);
            if (!lifecycle)
                return false;
            const auto cancel = [this, id = lifecycle->id]() noexcept { (void)mWiring->lifecycle.cancel(id); };
            const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
            const auto revision = mWiring->lifecycle.candidateRevision(lifecycle->id);
            auto projected
                = candidate && revision ? projectInterestChanges(before, *candidate, tick, *revision) : std::nullopt;
            if (!projected)
            {
                cancel();
                return false;
            }
            std::vector<std::pair<TransportConnectionId, InterestDelivery>> routed;
            try
            {
                routed.reserve(projected->size());
                for (auto& delivery : *projected)
                {
                    auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
                    if (!target)
                    {
                        cancel();
                        return false;
                    }
                    routed.emplace_back(*target, std::move(delivery));
                }
            }
            catch (...)
            {
                cancel();
                return false;
            }
            if (!admitInterestChangesAtomically(mWiring->queues, routed))
            {
                cancel();
                return false;
            }
            const auto expiredSession = lifecycle->session;
            if (!mWiring->lifecycle.commit(lifecycle->id) || !mWiring->joins.releasePrincipal(lifecycle->principal))
                return false;
            std::erase_if(mDialogueChoiceResults,
                [expiredSession](const auto& entry) { return entry.first.first == expiredSession; });
        }
    }

    bool ServerApplication::pump(ServerTick tick) noexcept
    {
        if (!mRunning)
            return false;
        if (mWiring && mWiring->scripts && !mWiring->scripts->healthy())
        {
            mFailure = "server script delivery failed";
            return false;
        }
        std::array<TransportEvent, 128> events{};
        const auto result = mTransport.poll(events);
        if (result.result != TransportResult::Accepted)
        {
            mFailure = "transport poll failed";
            stop();
            return false;
        }
        if (!mWiring)
            return true;

        std::vector<TransportConnectionId> closedConnections;
        closedConnections.reserve(result.events);
        for (std::size_t index = 0; index < result.events; ++index)
        {
            const auto& event = events[index];
            if (event.kind == TransportEventKind::ConnectionAccepted)
            {
                if (!event.connection || !event.admissionScope
                    || mWiring->sessions.accept(*event.connection, *event.admissionScope)
                        != ConnectionSessionResult::Accepted)
                {
                    if (event.connection)
                    {
                        (void)failConnection(*event.connection, "connection admission rejected");
                        continue;
                    }
                    mFailure = "invalid connection event";
                    return false;
                }
            }
            else if (event.kind == TransportEventKind::ConnectionClosed && event.connection)
            {
                mRejectedCloseDeadlines.erase(*event.connection);
                closedConnections.push_back(*event.connection);
            }
            else if (event.kind == TransportEventKind::RuntimeFailed)
            {
                mFailure = "transport runtime failed";
                stop();
                return false;
            }
        }
        if (!closedConnections.empty() && !disconnectConnections(closedConnections, tick))
        {
            mFailure = "disconnect lifecycle failed";
            return false;
        }

        for (const auto connection : mWiring->sessions.connections())
        {
            const auto* session = mWiring->sessions.session(connection);
            if (session && session->state() == ServerSessionState::Rejected)
                continue;
            const auto timeout = mWiring->sessions.checkTimeout(connection);
            if (timeout == ConnectionSessionResult::TimedOut)
                (void)failConnection(connection, "connection timed out");
            else if (timeout != ConnectionSessionResult::Accepted)
            {
                mFailure = "session timeout check failed";
                return false;
            }
        }

        if (!expireSessions(tick))
        {
            mFailure = "expiration lifecycle failed";
            return false;
        }

        std::optional<CanonicalServerState> directMutationBase;
        std::optional<CanonicalRevision> directMutationBaseRevision;
        std::array<TransportMessage, TransportRuntime::MaxMessagesPerReceive> messages{};
        for (const auto connection : mWiring->sessions.connections())
        {
            const auto* currentSession = mWiring->sessions.session(connection);
            if (currentSession && currentSession->state() == ServerSessionState::Rejected)
                continue;
            const auto received = mTransport.receive(connection, messages);
            if (received.result != TransportResult::Accepted)
            {
                (void)failConnection(connection, "transport receive failed");
                continue;
            }
            bool closed = false;
            bool resyncRequested = false;
            for (std::size_t index = 0; index < received.messages; ++index)
            {
                if (messages[index].channel == TransportChannel::PresentationLatest)
                {
                    if (!relayPose(connection, messages[index]))
                    {
                        (void)failConnection(connection, "pose relay rejected");
                        closed = true;
                        break;
                    }
                    continue;
                }
                const auto beforeDispatch = mWiring->reducer.state();
                const auto revisionBeforeDispatch = mWiring->reducer.canonicalRevision();
                const auto dispatched = mWiring->sessions.dispatch(
                    connection, messages[index], mWiring->joins, mWiring->crypto, mWiring->intake, tick);
                if (dispatched == ConnectionSessionResult::CommandSubmitted && !directMutationBase
                    && mWiring->reducer.canonicalRevision() != revisionBeforeDispatch)
                {
                    directMutationBase = beforeDispatch;
                    directMutationBaseRevision = revisionBeforeDispatch;
                }
                if (dispatched == ConnectionSessionResult::ProtocolRejected
                    || dispatched == ConnectionSessionResult::QueueRejected
                    || dispatched == ConnectionSessionResult::UnknownConnection)
                {
                    (void)failConnection(connection, "connection dispatch rejected");
                    closed = true;
                    break;
                }
                if (dispatched == ConnectionSessionResult::ResyncRequested
                    || dispatched == ConnectionSessionResult::ResyncCoalesced)
                    resyncRequested = true;
                if (dispatched == ConnectionSessionResult::Joined)
                {
                    auto* joined = mWiring->sessions.session(connection);
                    if (!joined || !joined->principal() || !joined->sessionId()
                        || !mWiring->lifecycle.registerJoined(*joined->principal(), *joined->sessionId()))
                    {
                        (void)failConnection(connection, "lifecycle registration failed");
                        closed = true;
                        break;
                    }
                }
                else if (dispatched == ConnectionSessionResult::ResumePrepared)
                {
                    if (!resumeConnection(connection, tick))
                    {
                        (void)failConnection(connection, "resume composition failed");
                        closed = true;
                        break;
                    }
                }
            }
            if (closed)
                continue;
            if (resyncRequested && !resyncConnection(connection, tick))
            {
                (void)failConnection(connection, "resync composition failed");
                continue;
            }
            auto* session = mWiring->sessions.session(connection);
            if (session != nullptr && session->state() == ServerSessionState::AuthenticationPending)
            {
                const auto advanced
                    = mWiring->sessions.pollAuthentication(connection, mWiring->joins, mWiring->crypto, tick);
                if (advanced != ConnectionSessionResult::AuthenticationPending
                    && advanced != ConnectionSessionResult::Joined
                    && advanced != ConnectionSessionResult::ResumePrepared
                    && advanced != ConnectionSessionResult::SessionRejected)
                {
                    (void)failConnection(connection, "authentication rejected");
                    continue;
                }
                if (advanced == ConnectionSessionResult::Joined)
                {
                    session = mWiring->sessions.session(connection);
                    if (!session || !session->principal() || !session->sessionId()
                        || !mWiring->lifecycle.registerJoined(*session->principal(), *session->sessionId()))
                    {
                        (void)failConnection(connection, "lifecycle registration failed");
                        continue;
                    }
                }
                else if (advanced == ConnectionSessionResult::ResumePrepared && !resumeConnection(connection, tick))
                {
                    (void)failConnection(connection, "resume composition failed");
                    continue;
                }
            }
        }
        if ((mWiring->actorCatalog || mWiring->actors || mWiring->actorCollision)
            && (!mWiring->actorCatalog || !mWiring->actors || !mWiring->actorCollision))
        {
            mFailure = "actor composition incomplete";
            return false;
        }
        if ((mWiring->interactiveObjectCatalog || mWiring->interactiveObjects)
            && (!mWiring->interactiveObjectCatalog || !mWiring->interactiveObjects))
        {
            mFailure = "interactive object composition incomplete";
            return false;
        }
        if ((mWiring->itemCatalog || mWiring->inventory) && (!mWiring->itemCatalog || !mWiring->inventory))
        {
            mFailure = "inventory composition incomplete";
            return false;
        }
        if ((mWiring->globalCatalog || mWiring->world) && (!mWiring->globalCatalog || !mWiring->world))
        {
            mFailure = "world content wiring is incomplete";
            return false;
        }
        if ((mWiring->scriptStateCatalog || mWiring->scriptState)
            && (!mWiring->scriptStateCatalog || !mWiring->scriptState))
        {
            mFailure = "script state wiring is incomplete";
            return false;
        }
        const bool anyCombat = mWiring->combat || mWiring->meleeWeapons || mWiring->playerCombatTemplate
            || mWiring->meleeSettings || mWiring->meleePolicy || mWiring->meleeContact || mWiring->directMagic;
        if (anyCombat
            && (!mWiring->combat || !mWiring->actors || !mWiring->meleeSettings || !mWiring->inventory
                || !mWiring->itemCatalog || !mWiring->meleeWeapons || !mWiring->playerCombatTemplate
                || !mWiring->meleePolicy || !mWiring->meleeContact || !mWiring->directMagic))
        {
            mFailure = "combat composition incomplete";
            return false;
        }
        if (mWiring->meleePolicy && mWiring->meleePolicy->difficulty != mConfig.combatDifficulty)
        {
            mFailure = "combat difficulty composition mismatch";
            return false;
        }
        if (mWiring->meleeContactHistory && (mWiring->meleeContact != mWiring->meleeContactHistory || !mWiring->actors))
        {
            mFailure = "melee contact history composition incomplete";
            return false;
        }
        if (mWiring->scripts && !mWiring->scripts->healthy())
        {
            mFailure = "server script delivery failed";
            return false;
        }
        const auto pumpedCommands = mWiring->intake.pump();
        if (!pumpedCommands)
        {
            mFailure = "command intake failed";
            return false;
        }
        for (const auto& batch : pumpedCommands.batches())
        {
            ServerScriptPumpResult pumpedScripts;
            if (mWiring->scripts)
            {
                pumpedScripts = mWiring->scripts->pump(batch.scheduledTick().value());
                if (!pumpedScripts)
                {
                    mFailure = "server script command pump failed";
                    return false;
                }
            }
            const auto before = directMutationBase ? *directMutationBase : mWiring->reducer.state();
            const auto revisionBefore = directMutationBaseRevision.value_or(mWiring->reducer.canonicalRevision());
            directMutationBase.reset();
            directMutationBaseRevision.reset();
            CanonicalCommandWorlds commandWorlds{ mWiring->interactiveObjects, mWiring->interactiveObjectCatalog,
                mWiring->inventory, mWiring->itemCatalog, mWiring->combat, mWiring->actors, mWiring->meleeWeapons,
                mWiring->meleeSettings, mWiring->meleePolicy, mWiring->meleeContact, mWiring->directMagic };
            commandWorlds.world = mWiring->world;
            commandWorlds.globalCatalog = mWiring->globalCatalog;
            commandWorlds.scriptState = mWiring->scriptState;
            commandWorlds.scriptStateCatalog = mWiring->scriptStateCatalog;
            auto prepared = mWiring->reducer.prepareTick(batch, commandWorlds, pumpedScripts.commands());
            if (!prepared.result())
            {
                mFailure = "command reduction failed";
                return false;
            }
            std::vector<std::pair<TransportConnectionId, InterestDelivery>> routed;
            std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>> routedViews;
            std::vector<std::pair<TransportConnectionId, ActorInterestBaselineDelivery>> actorBaselines;
            std::vector<std::pair<TransportConnectionId, InteractiveObjectInterestBaselineDelivery>> objectBaselines;
            std::vector<std::pair<TransportConnectionId, InventoryInterestDelivery>> inventoryBaselines;
            std::vector<CellId> changedObjectCells;
            bool refreshInventoryBaselines = false;
            const auto dispositions = prepared.result().dispositions();
            const auto commands = batch.commands();
            auto dialogueChoiceResultsCandidate = mDialogueChoiceResults;
            std::erase_if(dialogueChoiceResultsCandidate, [&prepared](const auto& entry) {
                const auto* active = prepared.candidateState().findActiveSession(entry.first.first);
                return active && !active->containsFinalizedCommandId(entry.first.second);
            });
            std::vector<std::pair<TransportConnectionId, ReliableDialogueChoiceResult>> dialogueChoiceResults;
            for (std::size_t index = 0; index < dispositions.size(); ++index)
            {
                const auto* interaction
                    = std::get_if<InteractiveObjectCommandProposal>(&commands[index].proposal().payload());
                if (interaction && dispositions[index].disposition() == CommandDisposition::Applied
                    && std::ranges::find(changedObjectCells, interaction->cell()) == changedObjectCells.end())
                    changedObjectCells.push_back(interaction->cell());
                if (std::holds_alternative<InventoryCommandProposal>(commands[index].proposal().payload()))
                    refreshInventoryBaselines = true;
                if (std::holds_alternative<MeleeAttackCommandProposal>(commands[index].proposal().payload())
                    && dispositions[index].disposition() == CommandDisposition::Applied)
                    refreshInventoryBaselines = true;
                const auto* dialogue
                    = std::get_if<DialogueChoiceCommandProposal>(&commands[index].proposal().payload());
                if (!dialogue)
                    continue;
                const auto& proposal = commands[index].proposal();
                const auto key = std::pair{ proposal.sessionId(), proposal.commandId() };
                const bool duplicate = dispositions[index].disposition() == CommandDisposition::DuplicateCommandId
                    || dispositions[index].disposition() == CommandDisposition::AlreadyFinalized;
                DialogueChoiceDisposition dialogueDisposition = DialogueChoiceDisposition::Rejected;
                if (duplicate)
                {
                    const auto retained = dialogueChoiceResultsCandidate.find(key);
                    if (retained != dialogueChoiceResultsCandidate.end()
                        && retained->second.choice == dialogue->choice())
                        dialogueDisposition = retained->second.disposition;
                }
                else
                {
                    switch (dispositions[index].disposition())
                    {
                        case CommandDisposition::Applied:
                            dialogueDisposition = DialogueChoiceDisposition::Committed;
                            break;
                        case CommandDisposition::UnknownDialogueChoice:
                            dialogueDisposition = DialogueChoiceDisposition::UnknownChoice;
                            break;
                        case CommandDisposition::DialogueChoiceIneligible:
                            dialogueDisposition = DialogueChoiceDisposition::Ineligible;
                            break;
                        default:
                            dialogueDisposition = DialogueChoiceDisposition::Rejected;
                            break;
                    }
                    if (dispositions[index].acknowledgementAdvanced())
                    {
                        if (!dialogueChoiceResultsCandidate.contains(key)
                            && dialogueChoiceResultsCandidate.size() >= MaximumRetainedDialogueChoiceResults)
                        {
                            mFailure = "dialogue choice result capacity exceeded";
                            return false;
                        }
                        dialogueChoiceResultsCandidate.insert_or_assign(
                            key, RetainedDialogueChoiceResult{ dialogue->choice(), dialogueDisposition });
                    }
                }
                const auto connection = mWiring->sessions.connectionForSession(proposal.sessionId());
                if (!connection || !supportsDialogueChoices(*connection))
                {
                    mFailure = "dialogue choice result target missing";
                    return false;
                }
                dialogueChoiceResults.emplace_back(*connection,
                    ReliableDialogueChoiceResult{ proposal.sessionId(), proposal.sessionGeneration(),
                        proposal.commandSequence(), proposal.commandId(), dialogue->choice(), dialogueDisposition,
                        duplicate, prepared.candidateRevision() });
            }
            if (prepared.candidateRevision() != revisionBefore)
            {
                auto projected = projectInterestChanges(
                    before, prepared.candidateState(), batch.scheduledTick().value(), prepared.candidateRevision());
                if (!projected)
                {
                    mFailure = "observation projection failed";
                    return false;
                }
                routed.reserve(projected->size());
                for (auto& delivery : *projected)
                {
                    auto connection = mWiring->sessions.connectionForSession(delivery.targetSession);
                    if (!connection)
                    {
                        mFailure = "observation target missing";
                        return false;
                    }
                    routed.emplace_back(*connection, std::move(delivery));
                }
                auto views = projectInterestViews(
                    prepared.candidateState(), batch.scheduledTick().value(), prepared.candidateRevision());
                if (!views)
                {
                    mFailure = "movement view projection failed";
                    return false;
                }
                routedViews.reserve(views->size());
                for (auto& delivery : *views)
                {
                    auto connection = mWiring->sessions.connectionForSession(delivery.first);
                    if (!connection)
                    {
                        mFailure = "movement view target missing";
                        return false;
                    }
                    routedViews.emplace_back(*connection, std::move(delivery.second));
                }
                if (mWiring->actors)
                    for (const auto& target : prepared.candidateState().activeSessions())
                    {
                        const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                        const auto* oldSession = before.findActiveSession(target.sessionId());
                        const auto* oldPlayer = oldSession ? before.findPlayer(oldSession->playerId()) : nullptr;
                        const auto* newPlayer = prepared.candidateState().findPlayer(target.playerId());
                        if (!connection || !oldPlayer || !newPlayer
                            || oldPlayer->transform().cell() == newPlayer->transform().cell()
                            || !supportsActors(*connection))
                            continue;
                        auto baseline = projectActorInterestBaseline(prepared.candidateState(), *mWiring->actors,
                            target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision());
                        if (!baseline)
                        {
                            mFailure = "actor baseline projection failed";
                            return false;
                        }
                        actorBaselines.emplace_back(*connection, std::move(*baseline));
                    }
                if (mWiring->interactiveObjects)
                    for (const auto& target : prepared.candidateState().activeSessions())
                    {
                        const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                        const auto* oldSession = before.findActiveSession(target.sessionId());
                        const auto* oldPlayer = oldSession ? before.findPlayer(oldSession->playerId()) : nullptr;
                        const auto* newPlayer = prepared.candidateState().findPlayer(target.playerId());
                        const bool changedCell
                            = oldPlayer && newPlayer && oldPlayer->transform().cell() != newPlayer->transform().cell();
                        const bool objectChangedInCell = newPlayer
                            && std::ranges::find(changedObjectCells, newPlayer->transform().cell())
                                != changedObjectCells.end();
                        if (!connection || !newPlayer || (!changedCell && !objectChangedInCell)
                            || !supportsInteractiveObjects(*connection))
                            continue;
                        const auto& projectedObjects = prepared.candidateInteractiveObjects()
                            ? *prepared.candidateInteractiveObjects()
                            : *mWiring->interactiveObjects;
                        auto baseline
                            = projectInteractiveObjectInterestBaseline(prepared.candidateState(), projectedObjects,
                                target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision());
                        if (!baseline)
                        {
                            mFailure = "interactive object baseline projection failed";
                            return false;
                        }
                        objectBaselines.emplace_back(*connection, std::move(*baseline));
                    }
                if (mWiring->inventory)
                    for (const auto& target : prepared.candidateState().activeSessions())
                    {
                        const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                        const auto* oldSession = before.findActiveSession(target.sessionId());
                        const auto* oldPlayer = oldSession ? before.findPlayer(oldSession->playerId()) : nullptr;
                        const auto* newPlayer = prepared.candidateState().findPlayer(target.playerId());
                        const bool changedCell
                            = oldPlayer && newPlayer && oldPlayer->transform().cell() != newPlayer->transform().cell();
                        if (!connection || !newPlayer || (!changedCell && !refreshInventoryBaselines)
                            || !supportsInventory(*connection))
                            continue;
                        const auto& projectedInventory
                            = prepared.candidateInventory() ? *prepared.candidateInventory() : *mWiring->inventory;
                        auto baseline = projectInventoryInterestBaseline(prepared.candidateState(), projectedInventory,
                            target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision());
                        if (!baseline)
                        {
                            mFailure = "inventory baseline projection failed";
                            return false;
                        }
                        inventoryBaselines.emplace_back(*connection, std::move(*baseline));
                    }
            }
            std::optional<CanonicalCombatWorld> combatCandidate;
            std::optional<CanonicalInventoryWorld> combatInventoryCandidate;
            std::vector<AuthoritativeActorMeleeEvent> authoritativeActorEvents;
            if (mWiring->combat)
            {
                const auto& baseCombat = prepared.candidateCombat() ? *prepared.candidateCombat() : *mWiring->combat;
                const auto& baseInventory
                    = prepared.candidateInventory() ? *prepared.candidateInventory() : *mWiring->inventory;
                auto advancedCombat = advanceAuthoritativeCombat(baseCombat, baseInventory, *mWiring->itemCatalog,
                    *mWiring->meleeWeapons, prepared.candidateState(), *mWiring->actors, *mWiring->meleeSettings,
                    combatSimulationPolicy(mConfig.tickIntervalMilliseconds, mWiring->meleePolicy->difficulty),
                    batch.scheduledTick().value(), mWiring->directMagic);
                auto* step = std::get_if<CombatSimulationStep>(&advancedCombat);
                if (!step)
                {
                    mFailure = "combat simulation failed";
                    return false;
                }
                combatCandidate.emplace(std::move(step->combat));
                if (step->inventory)
                    combatInventoryCandidate.emplace(std::move(*step->inventory));
                authoritativeActorEvents = std::move(step->events);
            }
            std::optional<CanonicalActorWorld> actorCandidate;
            std::vector<std::pair<TransportConnectionId, LatestWinsActorSnapshot>> actorViews;
            if (mWiring->actors)
            {
                auto advanced = combatCandidate
                    ? advanceActorSimulation(*mWiring->actors, *mWiring->actorCatalog, prepared.candidateState(),
                          *combatCandidate, batch.scheduledTick().value(), mConfig.contentManifest.movementProfile(),
                          *mWiring->actorCollision)
                    : advanceActorSimulation(*mWiring->actors, *mWiring->actorCatalog, prepared.candidateState(),
                          batch.scheduledTick().value(), mConfig.contentManifest.movementProfile(),
                          *mWiring->actorCollision);
                auto* candidate = std::get_if<CanonicalActorWorld>(&advanced);
                if (!candidate)
                {
                    mFailure = "actor simulation failed";
                    return false;
                }
                actorCandidate.emplace(std::move(*candidate));
                for (const auto& target : prepared.candidateState().activeSessions())
                {
                    const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                    if (!connection || !supportsActors(*connection))
                        continue;
                    auto view = projectActorInterestView(prepared.candidateState(), *actorCandidate, target.sessionId(),
                        batch.scheduledTick().value(), prepared.candidateRevision());
                    if (!view)
                    {
                        mFailure = "actor view projection failed";
                        return false;
                    }
                    actorViews.emplace_back(*connection, std::move(*view));
                }
            }
            std::optional<CanonicalWorldState> worldCandidate;
            if (mWiring->world)
            {
                const auto& baseWorld = prepared.candidateWorld() ? *prepared.candidateWorld() : *mWiring->world;
                auto advancedWorld = advanceCanonicalWorldTime(
                    baseWorld, batch.scheduledTick().value(), mConfig.tickIntervalMilliseconds);
                auto* worldValue = std::get_if<CanonicalWorldState>(&advancedWorld);
                if (!worldValue)
                {
                    mFailure = "world time simulation failed";
                    return false;
                }
                auto advancedWeather
                    = advanceCanonicalWeather(*worldValue, batch.scheduledTick().value());
                auto* weatherValue = std::get_if<CanonicalWorldState>(&advancedWeather);
                if (!weatherValue)
                {
                    mFailure = "weather simulation failed";
                    return false;
                }
                worldCandidate.emplace(std::move(*weatherValue));
            }
            std::vector<std::pair<TransportConnectionId, LatestWinsCombatSnapshot>> combatViews;
            std::vector<std::pair<TransportConnectionId, ReliableCombatEventBatch>> combatEvents;
            if (mWiring->combat)
            {
                const auto& projectedCombat = combatCandidate ? *combatCandidate : *mWiring->combat;
                const auto& projectedActors = actorCandidate ? *actorCandidate : *mWiring->actors;
                for (const auto& target : prepared.candidateState().activeSessions())
                {
                    const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                    if (!connection || !supportsCombat(*connection))
                        continue;
                    auto view = projectCombatSnapshot(prepared.candidateState(), projectedActors, projectedCombat,
                        target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision());
                    auto eventBatch = projectCombatEvents(prepared.candidateState(), projectedActors,
                        target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision(),
                        prepared.combatEvents(), authoritativeActorEvents);
                    if (!view || !eventBatch)
                    {
                        mFailure = "combat projection failed";
                        return false;
                    }
                    combatViews.emplace_back(*connection, std::move(*view));
                    combatEvents.emplace_back(*connection, std::move(*eventBatch));
                }
            }
            if (combatInventoryCandidate)
            {
                for (const auto& target : prepared.candidateState().activeSessions())
                {
                    const auto connection = mWiring->sessions.connectionForSession(target.sessionId());
                    if (!connection || !supportsInventory(*connection))
                        continue;
                    auto baseline
                        = projectInventoryInterestBaseline(prepared.candidateState(), *combatInventoryCandidate,
                            target.sessionId(), batch.scheduledTick().value(), prepared.candidateRevision());
                    if (!baseline)
                    {
                        mFailure = "combat inventory projection failed";
                        return false;
                    }
                    const auto existing = std::ranges::find(inventoryBaselines, *connection,
                        &std::pair<TransportConnectionId, InventoryInterestDelivery>::first);
                    if (existing == inventoryBaselines.end())
                        inventoryBaselines.emplace_back(*connection, std::move(*baseline));
                    else
                        existing->second = std::move(*baseline);
                }
            }
            if (!mWiring->reducer.stageSimulationCandidates(prepared, mWiring->inventory,
                    std::move(combatInventoryCandidate), mWiring->combat, std::move(combatCandidate), mWiring->actors,
                    std::move(actorCandidate), mWiring->world, std::move(worldCandidate)))
            {
                mFailure = "simulation candidate staging failed";
                return false;
            }
            if (!admitCombinedInterestTickAtomically(mWiring->queues, routed, routedViews, actorBaselines, actorViews,
                    objectBaselines, inventoryBaselines, combatViews, combatEvents, dialogueChoiceResults))
            {
                mFailure = changedObjectCells.empty() ? "tick output admission failed"
                                                      : "interactive object output admission failed";
                return false;
            }
            const bool committed = mWiring->reducer.commit(std::move(prepared), commandWorlds);
            if (!committed)
            {
                mFailure = "canonical commit failed";
                mRunning = false;
                return false;
            }
            mDialogueChoiceResults = std::move(dialogueChoiceResultsCandidate);
            if (mWiring->scripts && !mWiring->scripts->healthy())
            {
                mFailure = "server script delivery failed";
                return false;
            }
        }
        if (mWiring->meleeContactHistory
            && !mWiring->meleeContactHistory->capture(tick, mWiring->reducer.state(), *mWiring->actors))
        {
            mFailure = "melee contact history capture failed";
            return false;
        }
        for (const auto connection : mWiring->sessions.connections())
        {
            const auto now = mWiring->clock.now().nanoseconds() / 1'000'000;
            const auto pumped = mWiring->queues.pump(mTransport, connection, now);
            if (!pumped || *pumped == OutboundPumpResult::TransportFailed || *pumped == OutboundPumpResult::InvalidTime
                || *pumped == OutboundPumpResult::SlowPeerEvicted)
            {
                (void)failConnection(connection, "connection send failed");
                continue;
            }
            auto* session = mWiring->sessions.session(connection);
            if (!session || session->state() != ServerSessionState::Rejected)
                continue;
            const auto pending = mWiring->queues.hasPending(connection);
            if (!pending)
            {
                (void)failConnection(connection, "rejection queue state missing");
                continue;
            }
            if (*pending)
            {
                mRejectedCloseDeadlines.erase(connection);
                continue;
            }
            auto deadline = mRejectedCloseDeadlines.find(connection);
            if (deadline == mRejectedCloseDeadlines.end())
            {
                const auto value = sessionDeadline(mWiring->clock.now(), RejectionDrainNanoseconds);
                if (!value)
                {
                    (void)failConnection(connection, "rejection drain deadline overflow");
                    continue;
                }
                mRejectedCloseDeadlines.emplace(connection, *value);
                continue;
            }
            if (mWiring->clock.now() < deadline->second)
                continue;
            mRejectedCloseDeadlines.erase(deadline);
            (void)mWiring->sessions.close(connection);
            (void)mTransport.close(connection, TransportCloseMode::Graceful);
        }
        return true;
    }

    bool ServerApplication::stop() noexcept
    {
        if (!mRunning && !mListener)
            return true;
        mRunning = false;
        bool success = true;
        if (mWiring && !mWiring->reducer.state().players().empty())
        {
            std::vector<PlayerId> players;
            players.reserve(mWiring->reducer.state().players().size());
            for (const auto& player : mWiring->reducer.state().players())
                players.push_back(player.playerId());
            success = mWiring->joins.persistPlayers(players);
        }
        if (mListener)
        {
            const auto stopped = mTransport.stopListener(*mListener);
            success = stopped == TransportResult::Accepted || stopped == TransportResult::AlreadyFinalized;
            mListener.reset();
        }
        const auto shutDown = mTransport.shutdown();
        mLatestPoses.clear();
        mRejectedCloseDeadlines.clear();
        success = success && (shutDown == TransportResult::Accepted || shutDown == TransportResult::AlreadyFinalized);
        if (!success)
            mFailure = "transport shutdown failed";
        return success;
    }
}
