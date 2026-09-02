#include "authenticated_join_composition.hpp"

#include "tes3mp/protocol_frame.hpp"
#include "connection_session_coordinator.hpp"
#include "fixture_observation_projection.hpp"

#include <variant>

namespace TES3MP::ServerApp
{
    bool TransportJoinResponseQueue::enqueueJoinResponses(std::span<const std::byte> authentication,
        std::span<const std::byte> snapshot, const CanonicalServerState& before,
        const CanonicalServerState& after, const AuthenticatedJoinResult& join, ServerTick tick) noexcept
    {
        try
        {
            if (!mSessions)
                return mQueues.enqueuePair(mConnection, TransportChannel::ReliableOrdered, authentication,
                           TransportChannel::LatestWins, snapshot) == TransportResult::Accepted;
            auto projected = projectFixtureObservations(before, after, tick);
            if (!projected) return false;
            std::vector<std::vector<std::byte>> owned;
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            owned.reserve(3 + projected->size() * 2);
            owned.emplace_back(authentication.begin(), authentication.end());
            owned.emplace_back(snapshot.begin(), snapshot.end());
            messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned[0] });

            std::vector<ObservationChange> initialChanges;
            for (const auto& entry : join.initialSnapshot.view().entries())
                initialChanges.push_back({ entry.playerId(), entry.entityId(), ObservationChangeKind::Enter });
            auto initial = ReliableObservationBatch::create(
                join.session, join.initialSnapshot.header().targetSessionGeneration(), tick, initialChanges);
            if (!std::holds_alternative<ReliableObservationBatch>(initial)) return false;
            auto initialFrame = encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ReliableObservationBatch,
                encodeReliableObservationBatch(std::get<ReliableObservationBatch>(initial)));
            if (!std::holds_alternative<std::vector<std::byte>>(initialFrame)) return false;
            owned.push_back(std::get<std::vector<std::byte>>(std::move(initialFrame)));
            messages.push_back({ mConnection, TransportChannel::ReliableOrdered, owned.back() });
            messages.push_back({ mConnection, TransportChannel::LatestWins, owned[1] });

            for (const auto& delivery : *projected)
            {
                auto connection = mSessions->connectionForSession(delivery.targetSession);
                if (!connection) return false;
                auto first = encodeProtocolFrame(MessageClass::ReliableOperation,
                    MessageKind::ReliableObservationBatch, encodeReliableObservationBatch(delivery.observations));
                auto second = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(delivery.view));
                if (!std::holds_alternative<std::vector<std::byte>>(first)
                    || !std::holds_alternative<std::vector<std::byte>>(second)) return false;
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

    JoinCompositionOutcome AuthenticatedJoinComposition::join(PrincipalId principal,
        SessionGeneration generation, ServerTick tick, ResumeTokenContext context) noexcept
    {
        auto prepared = mJoins.prepare(principal, generation, tick);
        if (!std::holds_alternative<AuthenticatedJoinPreparation>(prepared))
            return { JoinCompositionResult::JoinRejected, std::nullopt };

        auto preparation = std::get<AuthenticatedJoinPreparation>(std::move(prepared));
        const auto cancel = [this, id = preparation.id]() noexcept { mJoins.cancel(id); };

        auto issued = mAuthentication.issueInitial(
            principal, preparation.join.session, generation, context);
        if (!std::holds_alternative<AuthenticationAcceptedMessage>(issued))
        {
            cancel();
            return { JoinCompositionResult::TokenRejected, std::nullopt };
        }

        try
        {
            const auto authenticationPayload = encodeAuthenticationAccepted(
                std::get<AuthenticationAcceptedMessage>(issued));
            const auto snapshotPayload = encodeLatestWinsSnapshot(preparation.join.initialSnapshot);
            auto authenticationFrame = encodeProtocolFrame(MessageClass::SessionControl,
                MessageKind::AuthenticationAccepted, authenticationPayload);
            auto snapshotFrame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                MessageKind::LatestWinsSnapshot, snapshotPayload);
            if (!std::holds_alternative<std::vector<std::byte>>(authenticationFrame)
                || !std::holds_alternative<std::vector<std::byte>>(snapshotFrame))
            {
                cancel();
                return { JoinCompositionResult::EncodingRejected, std::nullopt };
            }
            const auto& authenticationBytes = std::get<std::vector<std::byte>>(authenticationFrame);
            const auto& snapshotBytes = std::get<std::vector<std::byte>>(snapshotFrame);
            const auto* candidate = mJoins.candidateState(preparation.id);
            if (!candidate || !mResponses.enqueueJoinResponses(authenticationBytes, snapshotBytes,
                    mJoins.state(), *candidate, preparation.join, tick))
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

        auto committed = mJoins.commit(preparation.id);
        if (auto* joined = std::get_if<AuthenticatedJoinResult>(&committed))
            return { JoinCompositionResult::Committed, std::move(*joined) };
        return { JoinCompositionResult::CommitRejected, std::nullopt };
    }
}
