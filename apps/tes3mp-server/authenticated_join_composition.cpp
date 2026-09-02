#include "authenticated_join_composition.hpp"

#include "tes3mp/protocol_frame.hpp"

#include <variant>

namespace TES3MP::ServerApp
{
    bool TransportJoinResponseQueue::enqueueJoinResponses(std::span<const std::byte> authentication,
        std::span<const std::byte> snapshot) noexcept
    {
        try
        {
            return mQueues.enqueuePair(mConnection, TransportChannel::ReliableOrdered, authentication,
                       TransportChannel::LatestWins, snapshot) == TransportResult::Accepted;
        }
        catch (...)
        {
            return false;
        }
    }

    JoinCompositionResult AuthenticatedJoinComposition::join(PrincipalId principal,
        SessionGeneration generation, ServerTick tick, ResumeTokenContext context) noexcept
    {
        auto prepared = mJoins.prepare(principal, generation, tick);
        if (!std::holds_alternative<AuthenticatedJoinPreparation>(prepared))
            return JoinCompositionResult::JoinRejected;

        auto preparation = std::get<AuthenticatedJoinPreparation>(std::move(prepared));
        const auto cancel = [this, id = preparation.id]() noexcept { mJoins.cancel(id); };

        auto issued = mAuthentication.issueInitial(
            principal, preparation.join.session, generation, context);
        if (!std::holds_alternative<AuthenticationAcceptedMessage>(issued))
        {
            cancel();
            return JoinCompositionResult::TokenRejected;
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
                return JoinCompositionResult::EncodingRejected;
            }
            const auto& authenticationBytes = std::get<std::vector<std::byte>>(authenticationFrame);
            const auto& snapshotBytes = std::get<std::vector<std::byte>>(snapshotFrame);
            if (!mResponses.enqueueJoinResponses(authenticationBytes, snapshotBytes))
            {
                cancel();
                return JoinCompositionResult::QueueRejected;
            }
        }
        catch (...)
        {
            cancel();
            return JoinCompositionResult::EncodingRejected;
        }

        auto committed = mJoins.commit(preparation.id);
        return std::holds_alternative<AuthenticatedJoinResult>(committed)
            ? JoinCompositionResult::Committed : JoinCompositionResult::CommitRejected;
    }
}
