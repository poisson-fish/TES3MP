#include <tes3mp/server_lifecycle.hpp>

#include <algorithm>
#include <limits>

namespace TES3MP
{
    ServerLifecycleCoordinator::ServerLifecycleCoordinator(
        std::uint64_t graceNanoseconds, CanonicalCommandReducer& reducer) noexcept
        : mGraceNanoseconds(graceNanoseconds), mReducer(reducer)
    {
        mBindings.reserve(MaximumCanonicalActiveSessions);
    }

    std::optional<ServerLifecycleCoordinator> ServerLifecycleCoordinator::create(
        std::uint64_t graceNanoseconds, CanonicalCommandReducer& reducer) noexcept
    {
        if (graceNanoseconds == 0) return std::nullopt;
        return ServerLifecycleCoordinator(graceNanoseconds, reducer);
    }

    bool ServerLifecycleCoordinator::registerJoined(PrincipalId principal, SessionId sessionId) noexcept
    {
        if (mPending || mBindings.size() >= MaximumCanonicalActiveSessions) return false;
        if (std::any_of(mBindings.begin(), mBindings.end(), [principal, sessionId](const Binding& value) {
                return value.principal == principal || value.session.sessionId() == sessionId;
            })) return false;
        const auto* session = mReducer.state().findActiveSession(sessionId);
        if (!session) return false;
        mBindings.push_back({ principal, *session, true, MonotonicInstant::fromNanoseconds(0) });
        return true;
    }

    ServerLifecyclePrepareResult ServerLifecycleCoordinator::prepareDisconnect(
        SessionId sessionId, MonotonicInstant now, ServerTick tick)
    {
        if (mPending) return ServerLifecycleError::PreparationPending;
        const auto found = std::find_if(mBindings.begin(), mBindings.end(),
            [sessionId](const Binding& value) { return value.session.sessionId() == sessionId; });
        if (found == mBindings.end()) return ServerLifecycleError::UnknownSession;
        if (!found->live) return ServerLifecycleError::AlreadyDisconnected;
        if (now.nanoseconds() > std::numeric_limits<std::uint64_t>::max() - mGraceNanoseconds)
            return ServerLifecycleError::DeadlineOverflow;
        auto canonical = mReducer.prepareDisconnect(sessionId, tick);
        if (!canonical) return ServerLifecycleError::CanonicalStateRejected;
        const auto* current = mReducer.state().findActiveSession(sessionId);
        if (!current) return ServerLifecycleError::CanonicalStateRejected;
        const auto index = static_cast<std::size_t>(found - mBindings.begin());
        const auto deadline = MonotonicInstant::fromNanoseconds(now.nanoseconds() + mGraceNanoseconds);
        const auto id = mNextPreparationId++;
        ServerLifecyclePreparation value{ id, ServerLifecycleAction::Disconnect, found->principal,
            sessionId, found->session.playerId(), found->session.sessionGeneration(), deadline };
        mPending.emplace(Pending{ { value }, { index }, { *current }, std::move(*canonical) });
        return value;
    }

    ServerLifecycleBatchPrepareResult ServerLifecycleCoordinator::prepareDisconnectBatch(
        std::span<const SessionId> sessionIds, MonotonicInstant now, ServerTick tick)
    {
        if (mPending) return ServerLifecycleError::PreparationPending;
        if (sessionIds.empty()) return ServerLifecycleError::UnknownSession;
        if (now.nanoseconds() > std::numeric_limits<std::uint64_t>::max() - mGraceNanoseconds)
            return ServerLifecycleError::DeadlineOverflow;
        std::vector<ServerLifecyclePreparation> values;
        std::vector<std::size_t> indices;
        std::vector<CanonicalSessionProgress> replacements;
        values.reserve(sessionIds.size());
        indices.reserve(sessionIds.size());
        replacements.reserve(sessionIds.size());
        const auto deadline = MonotonicInstant::fromNanoseconds(now.nanoseconds() + mGraceNanoseconds);
        const auto id = mNextPreparationId;
        for (const auto sessionId : sessionIds)
        {
            const auto found = std::find_if(mBindings.begin(), mBindings.end(),
                [sessionId](const Binding& value) { return value.session.sessionId() == sessionId; });
            if (found == mBindings.end()) return ServerLifecycleError::UnknownSession;
            if (!found->live) return ServerLifecycleError::AlreadyDisconnected;
            const auto index = static_cast<std::size_t>(found - mBindings.begin());
            if (std::ranges::find(indices, index) != indices.end()) return ServerLifecycleError::AlreadyDisconnected;
            indices.push_back(index);
            const auto* current = mReducer.state().findActiveSession(sessionId);
            if (!current) return ServerLifecycleError::CanonicalStateRejected;
            replacements.push_back(*current);
            values.push_back({ id, ServerLifecycleAction::Disconnect, found->principal, sessionId,
                found->session.playerId(), found->session.sessionGeneration(), deadline });
        }
        auto canonical = mReducer.prepareDisconnectBatch(sessionIds, tick);
        if (!canonical) return ServerLifecycleError::CanonicalStateRejected;
        ++mNextPreparationId;
        mPending.emplace(Pending{ values, indices, replacements, std::move(*canonical) });
        return ServerLifecycleBatchPreparation{ id, std::move(values) };
    }

    ServerLifecyclePrepareResult ServerLifecycleCoordinator::prepareResume(
        PrincipalId principal, SessionId sessionId, MonotonicInstant now, ServerTick tick)
    {
        if (mPending) return ServerLifecycleError::PreparationPending;
        const auto found = std::find_if(mBindings.begin(), mBindings.end(), [principal](const Binding& value) {
            return value.principal == principal;
        });
        if (found == mBindings.end()) return ServerLifecycleError::UnknownPrincipal;
        if (found->session.sessionId() != sessionId) return ServerLifecycleError::UnknownSession;
        if (found->live) return ServerLifecycleError::AlreadyLive;
        if (now >= found->deadline) return ServerLifecycleError::DeadlineReached;
        const auto generation = found->session.sessionGeneration().next();
        if (!generation) return ServerLifecycleError::GenerationExhausted;
        auto replacement = createCanonicalSessionProgress(sessionId, *generation, found->session.playerId(),
            found->session.entityId(), found->session.highestContiguousFinalizedCommand(),
            found->session.finalizedCommandHistory());
        auto* session = std::get_if<CanonicalSessionProgress>(&replacement);
        if (!session) return ServerLifecycleError::CanonicalStateRejected;
        auto canonical = mReducer.prepareResume(*session, tick);
        if (!canonical) return ServerLifecycleError::CanonicalStateRejected;
        const auto index = static_cast<std::size_t>(found - mBindings.begin());
        const auto id = mNextPreparationId++;
        ServerLifecyclePreparation value{ id, ServerLifecycleAction::Resume, principal, sessionId,
            session->playerId(), *generation, found->deadline };
        mPending.emplace(Pending{ { value }, { index }, { *session }, std::move(*canonical) });
        return value;
    }

    ServerLifecyclePrepareResult ServerLifecycleCoordinator::prepareNextExpiration(
        MonotonicInstant now, ServerTick tick)
    {
        if (mPending) return ServerLifecycleError::PreparationPending;
        auto found = mBindings.end();
        for (auto current = mBindings.begin(); current != mBindings.end(); ++current)
            if (!current->live && now >= current->deadline
                && (found == mBindings.end() || current->deadline < found->deadline
                    || (current->deadline == found->deadline
                        && current->session.sessionId() < found->session.sessionId())))
                found = current;
        if (found == mBindings.end()) return ServerLifecycleError::DeadlineReached;
        auto canonical = mReducer.prepareExpiration(found->session.playerId(), found->session.sessionId(),
            found->session.sessionGeneration(), tick);
        if (!canonical) return ServerLifecycleError::CanonicalStateRejected;
        const auto index = static_cast<std::size_t>(found - mBindings.begin());
        const auto id = mNextPreparationId++;
        ServerLifecyclePreparation value{ id, ServerLifecycleAction::Expire, found->principal,
            found->session.sessionId(), found->session.playerId(), found->session.sessionGeneration(), found->deadline };
        mPending.emplace(Pending{ { value }, { index }, { found->session }, std::move(*canonical) });
        return value;
    }

    bool ServerLifecycleCoordinator::commit(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->publicValues.front().id != preparationId) return false;
        if (!mReducer.commit(std::move(mPending->canonical))) { mPending.reset(); return false; }
        const auto action = mPending->publicValues.front().action;
        if (action == ServerLifecycleAction::Disconnect)
        {
            for (std::size_t value = 0; value < mPending->bindingIndices.size(); ++value)
            {
                const auto index = mPending->bindingIndices[value];
                mBindings[index].session = mPending->replacementSessions[value];
                mBindings[index].live = false;
                mBindings[index].deadline = mPending->publicValues[value].deadline;
            }
        }
        else if (action == ServerLifecycleAction::Resume)
        {
            const auto index = mPending->bindingIndices.front();
            mBindings[index].session = mPending->replacementSessions.front();
            mBindings[index].live = true;
        }
        else
            mBindings.erase(mBindings.begin() + static_cast<std::ptrdiff_t>(mPending->bindingIndices.front()));
        mPending.reset();
        return true;
    }

    bool ServerLifecycleCoordinator::cancel(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->publicValues.front().id != preparationId) return false;
        mPending.reset();
        return true;
    }

    const CanonicalServerState* ServerLifecycleCoordinator::candidateState(
        std::uint64_t preparationId) const noexcept
    {
        if (!mPending || mPending->publicValues.front().id != preparationId) return nullptr;
        return &mPending->canonical.candidateState();
    }

    std::size_t ServerLifecycleCoordinator::liveCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(mBindings.begin(), mBindings.end(),
            [](const Binding& value) { return value.live; }));
    }

    std::size_t ServerLifecycleCoordinator::hiddenCount() const noexcept
    {
        return mBindings.size() - liveCount();
    }
}
