#include "authenticated_join_composition.hpp"

#include "actor_interest_projection.hpp"
#include "combat_interest_projection.hpp"
#include "connection_session_coordinator.hpp"
#include "interactive_object_interest_projection.hpp"
#include "interest_projection.hpp"
#include "inventory_interest_projection.hpp"
#include "weather_projection.hpp"
#include "world_time_projection.hpp"
#include "tes3mp/character_creation_protocol.hpp"
#include "tes3mp/protocol_frame.hpp"

#include <algorithm>
#include <variant>

namespace TES3MP::ServerApp
{
    bool TransportJoinResponseQueue::enqueueJoinResponses(std::span<const std::byte> authentication,
        std::span<const std::byte> snapshot, const CanonicalServerState& before, const CanonicalServerState& after,
        const AuthenticatedJoinResult& join, ServerTick tick, CanonicalStateVersion stateVersion) noexcept
    {
        try
        {
            if (!mSessions)
                return mQueues.enqueuePair(mConnection, TransportChannel::ReliableOrdered, authentication,
                           TransportChannel::LatestWins, snapshot)
                    == TransportResult::Accepted;
            const auto revision = join.initialSnapshot.header().canonicalRevision();
            auto baseline = projectInterestBaseline(after, join.session, tick, revision, stateVersion);
            auto projected = projectInterestChanges(before, after, tick, revision);
            if (!baseline || !projected)
                return false;
            const auto* joiningSession = mSessions->session(mConnection);
            const auto actorCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(
                    joiningSession->negotiatedHello()->negotiatedCapabilities(), actorReplicationCapability());
            auto actorBaseline = actorCapable && mActors
                ? projectActorInterestBaseline(after, *mActors, join.session, tick, revision)
                : std::optional<ActorInterestBaselineDelivery>{};
            if (actorCapable && (!mActors || !actorBaseline))
                return false;

            const auto objectCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(joiningSession->negotiatedHello()->negotiatedCapabilities(),
                    interactiveObjectReplicationCapability());
            auto objectBaseline = objectCapable && mObjects
                ? projectInteractiveObjectInterestBaseline(after, *mObjects, join.session, tick, revision)
                : std::optional<InteractiveObjectInterestBaselineDelivery>{};
            if (objectCapable && (!mObjects || !objectBaseline))
                return false;

            const auto weatherCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(
                    joiningSession->negotiatedHello()->negotiatedCapabilities(), weatherReplicationCapability());
            auto weatherBaseline = weatherCapable && mWorld
                ? projectWeatherBaseline(after, *mWorld, join.session, tick, revision)
                : std::optional<WeatherStateDelivery>{};
            if (weatherCapable && (!mWorld || !weatherBaseline))
                return false;

            const auto worldTimeCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(joiningSession->negotiatedHello()->negotiatedCapabilities(),
                    worldTimeReplicationCapability());
            auto worldTimeBaseline = worldTimeCapable && mWorld
                ? projectWorldTimeBaseline(after, *mWorld, join.session, tick, revision)
                : std::optional<ReliableWorldTimeState>{};
            if (worldTimeCapable && (!mWorld || !worldTimeBaseline))
                return false;

            const auto inventoryCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(
                    joiningSession->negotiatedHello()->negotiatedCapabilities(), inventoryReplicationCapability());
            std::vector<std::pair<TransportConnectionId, InventoryInterestDelivery>> inventoryBaselines;
            if (mInventory)
            {
                mPendingInventory = *mInventory;
                if (!mPendingInventory->ensurePlayer(join.player))
                    return false;
                if (join.characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter
                    && (!mItemCatalog
                        || !mPendingInventory->initializePlayerFromCharacter(join.player,
                            join.characterProfile.revision(), join.characterProfile.startingInventory(), tick)))
                    return false;
            }
            if (inventoryCapable)
            {
                if (!mPendingInventory)
                    return false;
                auto inventoryBaseline
                    = projectInventoryInterestBaseline(after, *mPendingInventory, join.session, tick, revision);
                if (!inventoryBaseline)
                    return false;
                inventoryBaselines.emplace_back(mConnection, std::move(*inventoryBaseline));
            }
            const auto combatCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(
                    joiningSession->negotiatedHello()->negotiatedCapabilities(), combatReplicationCapability());
            const auto characterCapable = joiningSession && joiningSession->negotiatedHello()
                && std::ranges::binary_search(
                    joiningSession->negotiatedHello()->negotiatedCapabilities(), characterCreationCapability());
            if (mCombat)
            {
                if (!mPendingInventory || !mPlayerCombatTemplate || !mItemCatalog)
                    return false;
                const auto* playerInventory = mPendingInventory->findPlayer(join.player);
                if (!playerInventory)
                    return false;
                mPendingCombat = *mCombat;
                const auto weight = playerInventory->totalWeight(*mItemCatalog);
                if (join.characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter)
                {
                    auto characterTemplate = deriveCharacterCombatTemplate(
                        join.characterProfile, *mPlayerCombatTemplate, mCharacterContent);
                    if (!characterTemplate
                        || !mPendingCombat->initializePlayerFromCharacter(
                            join.player, *characterTemplate, weight, join.characterProfile.revision()))
                        return false;
                }
                else if (!mPendingCombat->ensurePlayer(join.player, *mPlayerCombatTemplate, weight))
                    return false;
            }
            auto combatSnapshot = combatCapable && mPendingCombat && mActors
                ? projectCombatSnapshot(after, *mActors, *mPendingCombat, join.session, tick, revision)
                : std::optional<LatestWinsCombatSnapshot>{};
            if (combatCapable && (!mPendingCombat || !mActors || !combatSnapshot))
                return false;
            if (mInventory || mPendingInventory)
            {
                const auto& projectedInventory = mPendingInventory ? *mPendingInventory : *mInventory;
                for (const auto& target : after.activeSessions())
                {
                    if (target.sessionId() == join.session)
                        continue;
                    const auto connection = mSessions->connectionForSession(target.sessionId());
                    const auto* targetSession = connection ? mSessions->session(*connection) : nullptr;
                    const bool capable = targetSession && targetSession->negotiatedHello()
                        && std::ranges::binary_search(targetSession->negotiatedHello()->negotiatedCapabilities(),
                            inventoryReplicationCapability());
                    if (!connection || !capable)
                        continue;
                    auto delivery = projectInventoryInterestBaseline(
                        after, projectedInventory, target.sessionId(), tick, revision);
                    if (!delivery)
                        return false;
                    inventoryBaselines.emplace_back(*connection, std::move(*delivery));
                }
            }

            std::vector<std::vector<std::byte>> owned;
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            std::size_t frameCapacity
                = 9 + projected->size() * 2 + (weatherBaseline ? weatherBaseline->chunks.size() : 0);
            for (const auto& [connection, delivery] : inventoryBaselines)
            {
                (void)connection;
                frameCapacity += delivery.playerInventory.size() + delivery.containers.size()
                    + delivery.groundItems.size() + 1;
            }
            owned.reserve(frameCapacity);
            messages.reserve(owned.capacity());
            owned.emplace_back(authentication.begin(), authentication.end());
            auto baselineFrame = encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ReliableInterestBaseline, encodeReliableInterestBaseline(baseline->baseline));
            auto viewFrame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                encodeLatestWinsSnapshot(baseline->view));
            if (!std::holds_alternative<std::vector<std::byte>>(baselineFrame)
                || !std::holds_alternative<std::vector<std::byte>>(viewFrame))
                return false;
            owned.push_back(std::get<std::vector<std::byte>>(std::move(baselineFrame)));
            owned.push_back(std::get<std::vector<std::byte>>(std::move(viewFrame)));
            messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned[0] });
            messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned[1] });
            messages.push_back({ mConnection, TransportChannel::LatestWins, owned[2] });
            if (characterCapable)
            {
                auto profileFrame
                    = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableCharacterProfile,
                        encodeReliableCharacterProfile(
                            { join.session, join.initialSnapshot.header().targetSessionGeneration(), join.player,
                                CharacterConfirmationResult::Confirmed, join.characterProfile }));
                if (!std::holds_alternative<std::vector<std::byte>>(profileFrame))
                    return false;
                owned.push_back(std::get<std::vector<std::byte>>(std::move(profileFrame)));
                messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned.back() });
            }

            if (actorBaseline)
            {
                auto actorBaselineFrame
                    = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
                        encodeReliableActorInterestBaseline(actorBaseline->baseline));
                auto actorViewFrame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsActorSnapshot, encodeLatestWinsActorSnapshot(actorBaseline->view));
                if (!std::holds_alternative<std::vector<std::byte>>(actorBaselineFrame)
                    || !std::holds_alternative<std::vector<std::byte>>(actorViewFrame))
                    return false;
                owned.push_back(std::get<std::vector<std::byte>>(std::move(actorBaselineFrame)));
                messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned.back() });
                owned.push_back(std::get<std::vector<std::byte>>(std::move(actorViewFrame)));
                messages.push_back({ mConnection, TransportChannel::LatestWins, owned.back() });
            }

            if (objectBaseline)
            {
                auto objectBaselineFrame = encodeProtocolFrame(MessageClass::ReliableOperation,
                    MessageKind::ReliableInteractiveObjectInterestBaseline,
                    encodeReliableInteractiveObjectInterestBaseline(objectBaseline->baseline));
                if (!std::holds_alternative<std::vector<std::byte>>(objectBaselineFrame))
                    return false;
                owned.push_back(std::get<std::vector<std::byte>>(std::move(objectBaselineFrame)));
                messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned.back() });
            }

            if (combatSnapshot)
            {
                auto combatFrame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsCombatSnapshot, encodeLatestWinsCombatSnapshot(*combatSnapshot));
                if (!std::holds_alternative<std::vector<std::byte>>(combatFrame))
                    return false;
                owned.push_back(std::get<std::vector<std::byte>>(std::move(combatFrame)));
                messages.push_back({ mConnection, TransportChannel::LatestWins, owned.back() });
            }

            if (weatherBaseline && !appendWeatherMessages(owned, messages, mConnection, *weatherBaseline))
                return false;
            if (worldTimeBaseline && !appendWorldTimeMessage(owned, messages, mConnection, *worldTimeBaseline))
                return false;

            for (const auto& [connection, inventoryBaseline] : inventoryBaselines)
                if (!appendInventoryInterestMessages(owned, messages, connection, inventoryBaseline))
                    return false;

            for (const auto& delivery : *projected)
            {
                auto connection = mSessions->connectionForSession(delivery.targetSession);
                if (!connection)
                    return false;
                auto first = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableObservationBatch,
                    encodeReliableObservationBatch(delivery.observations));
                auto second = encodeProtocolFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                    encodeLatestWinsSnapshot(delivery.view));
                if (!std::holds_alternative<std::vector<std::byte>>(first)
                    || !std::holds_alternative<std::vector<std::byte>>(second))
                    return false;
                owned.push_back(std::get<std::vector<std::byte>>(std::move(first)));
                messages.push_back({ *connection, TransportChannel::ReliableOrdered, owned.back() });
                owned.push_back(std::get<std::vector<std::byte>>(std::move(second)));
                messages.push_back({ *connection, TransportChannel::LatestWins, owned.back() });
            }
            return mQueues.enqueueMessagesAtomically(messages) == TransportResult::Accepted;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TransportJoinResponseQueue::commitJoinState() noexcept
    {
        if (mPendingInventory && !mInventory)
            return false;
        if (mPendingCombat && !mCombat)
            return false;
        if (mPendingInventory)
            *mInventory = std::move(*mPendingInventory);
        if (mPendingCombat)
            *mCombat = std::move(*mPendingCombat);
        mPendingInventory.reset();
        mPendingCombat.reset();
        return true;
    }

    JoinCompositionOutcome AuthenticatedJoinComposition::join(PrincipalId principal, SessionGeneration generation,
        ServerTick tick, ResumeTokenContext context, std::optional<AuthenticatedAdmission::PlayerClaim> playerClaim,
        std::optional<PlayerCredential> providedCredential, std::string username) noexcept
    {
        const bool clientProvidedCredential = providedCredential.has_value();
        auto prepared = playerClaim
            ? mJoins.prepareReattach(principal, *playerClaim, generation, tick)
            : mJoins.prepare(principal, generation, tick, std::move(providedCredential), std::move(username));
        if (!std::holds_alternative<AuthenticatedJoinPreparation>(prepared))
            return { JoinCompositionResult::JoinRejected, std::nullopt };

        auto preparation = std::get<AuthenticatedJoinPreparation>(std::move(prepared));
        const auto cancel = [this, id = preparation.id]() noexcept { mJoins.cancel(id); };

        auto issued = mAuthentication.issueInitial(principal, preparation.join.session, generation, context);
        if (!std::holds_alternative<AuthenticationAcceptedMessage>(issued))
        {
            cancel();
            return { JoinCompositionResult::TokenRejected, std::nullopt };
        }

        try
        {
            auto accepted = std::get<AuthenticationAcceptedMessage>(std::move(issued));
            auto playerCredential = clientProvidedCredential ? std::optional<PlayerCredential>{}
                                                             : mJoins.copyPendingPlayerCredential(preparation.id);
            if (mJoins.pendingCreatesPersistentIdentity(preparation.id) && !clientProvidedCredential
                && !playerCredential)
            {
                cancel();
                return { JoinCompositionResult::EncodingRejected, std::nullopt };
            }
            if (playerCredential)
            {
                auto withPlayerCredential = AuthenticationAcceptedMessage::create(accepted.takeToken(),
                    accepted.lifetimeMilliseconds(), std::move(*playerCredential), preparation.join.characterLifecycle,
                    preparation.join.profileRevision);
                if (!withPlayerCredential)
                {
                    cancel();
                    return { JoinCompositionResult::EncodingRejected, std::nullopt };
                }
                accepted = std::move(*withPlayerCredential);
            }
            else
            {
                auto withCharacterState
                    = AuthenticationAcceptedMessage::create(accepted.takeToken(), accepted.lifetimeMilliseconds(),
                        std::nullopt, preparation.join.characterLifecycle, preparation.join.profileRevision);
                if (!withCharacterState)
                {
                    cancel();
                    return { JoinCompositionResult::EncodingRejected, std::nullopt };
                }
                accepted = std::move(*withCharacterState);
            }
            const auto authenticationPayload = encodeAuthenticationAccepted(accepted);
            const auto snapshotPayload = encodeLatestWinsSnapshot(preparation.join.initialSnapshot);
            auto authenticationFrame = encodeProtocolFrame(
                MessageClass::SessionControl, MessageKind::AuthenticationAccepted, authenticationPayload);
            auto snapshotFrame = encodeProtocolFrame(
                MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot, snapshotPayload);
            if (!std::holds_alternative<std::vector<std::byte>>(authenticationFrame)
                || !std::holds_alternative<std::vector<std::byte>>(snapshotFrame))
            {
                cancel();
                return { JoinCompositionResult::EncodingRejected, std::nullopt };
            }
            const auto& authenticationBytes = std::get<std::vector<std::byte>>(authenticationFrame);
            const auto& snapshotBytes = std::get<std::vector<std::byte>>(snapshotFrame);
            const auto* candidate = mJoins.candidateState(preparation.id);
            const auto stateVersion = mJoins.candidateStateVersion(preparation.id);
            if (!candidate || !stateVersion
                || !mResponses.enqueueJoinResponses(authenticationBytes, snapshotBytes, mJoins.state(), *candidate,
                    preparation.join, tick, *stateVersion))
            {
                cancel();
                return { JoinCompositionResult::QueueRejected, std::nullopt };
            }
        }
        catch (...)
        {
            cancel();
            return { JoinCompositionResult::EncodingRejected, std::nullopt };
        }

        auto committed = mJoins.commit(preparation.id, mResponses.pendingInventory(), mResponses.pendingCombat());
        if (auto* joined = std::get_if<AuthenticatedJoinResult>(&committed))
        {
            if (!mResponses.commitJoinState())
                return { JoinCompositionResult::CommitRejected, std::nullopt };
            return { JoinCompositionResult::Committed, std::move(*joined) };
        }
        return { JoinCompositionResult::CommitRejected, std::nullopt };
    }
}
