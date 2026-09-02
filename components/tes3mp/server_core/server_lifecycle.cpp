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
        const auto index = static_cast<std::size_t>(found - mBindings.begin());
        const auto deadline = MonotonicInstant::fromNanoseconds(now.nanoseconds() + mGraceNanoseconds);
        const auto id = mNextPreparationId++;
        ServerLifecyclePreparation value{ id, ServerLifecycleAction::Disconnect, found->principal,
            sessionId, found->session.playerId(), found->session.sessionGeneration(), deadline };
        mPending.emplace(Pending{ value, index, found->session, std::move(*canonical) });
        return value;
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
        mPending.emplace(Pending{ value, index, *session, std::move(*canonical) });
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
        mPending.emplace(Pending{ value, index, found->session, std::move(*canonical) });
        return value;
    }

    bool ServerLifecycleCoordinator::commit(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->publicValue.id != preparationId) return false;
        if (!mReducer.commit(std::move(mPending->canonical))) { mPending.reset(); return false; }
        const auto action = mPending->publicValue.action;
        const auto index = mPending->bindingIndex;
        if (action == ServerLifecycleAction::Disconnect)
        {
            mBindings[index].live = false;
            mBindings[index].deadline = mPending->publicValue.deadline;
        }
        else if (action == ServerLifecycleAction::Resume)
        {
            mBindings[index].session = mPending->replacementSession;
            mBindings[index].live = true;
        }
        else
            mBindings.erase(mBindings.begin() + static_cast<std::ptrdiff_t>(index));
        mPending.reset();
        return true;
    }

    bool ServerLifecycleCoordinator::cancel(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->publicValue.id != preparationId) return false;
        mPending.reset();
        return true;
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
