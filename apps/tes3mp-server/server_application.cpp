#include "server_application.hpp"
#include "fixture_observation_projection.hpp"

#include <array>

namespace TES3MP::ServerApp
{
    ServerApplication::ServerApplication(TransportRuntime& transport, const ServerConfig& config) noexcept
        : mTransport(transport), mConfig(config) {}

    ServerApplication::ServerApplication(TransportRuntime& transport, const ServerConfig& config,
        ServerApplicationWiring wiring) noexcept
        : mTransport(transport), mConfig(config), mWiring(wiring) {}

    ServerApplication::~ServerApplication() { stop(); }

    bool ServerApplication::start() noexcept
    {
        if (mRunning || mListener) { mFailure = "server already started"; return false; }
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

    bool ServerApplication::pump() noexcept { return pump(ServerTick::initial()); }

    bool ServerApplication::failConnection(TransportConnectionId connection, std::string_view failure) noexcept
    {
        if (mWiring) (void)mWiring->sessions.close(connection);
        (void)mTransport.close(connection, TransportCloseMode::Abort);
        mFailure = failure;
        return true;
    }

    bool ServerApplication::disconnectConnection(TransportConnectionId connection, ServerTick tick) noexcept
    {
        auto* session = mWiring->sessions.session(connection);
        if (session == nullptr || !session->sessionId())
            return mWiring->sessions.close(connection) == ConnectionSessionResult::Accepted;
        const auto before = mWiring->reducer.state();
        auto prepared = mWiring->lifecycle.prepareDisconnect(*session->sessionId(), mWiring->clock.now(), tick);
        auto* lifecycle = std::get_if<ServerLifecyclePreparation>(&prepared);
        if (!lifecycle) return false;
        const auto cancel = [this, id = lifecycle->id]() noexcept { (void)mWiring->lifecycle.cancel(id); };
        const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
        auto projected = candidate ? projectFixtureObservations(before, *candidate, tick) : std::nullopt;
        if (!projected) { cancel(); return false; }
        std::vector<std::pair<TransportConnectionId, FixtureObservationDelivery>> routed;
        routed.reserve(projected->size());
        for (auto& delivery : *projected)
        {
            auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
            if (!target || *target == connection) { cancel(); return false; }
            routed.emplace_back(*target, std::move(delivery));
        }
        if (!admitFixtureObservationsAtomically(mWiring->queues, routed)) { cancel(); return false; }
        if (!mWiring->lifecycle.commit(lifecycle->id)) return false;
        return mWiring->sessions.close(connection) == ConnectionSessionResult::Accepted;
    }

    bool ServerApplication::resumeConnection(TransportConnectionId connection, ServerTick tick) noexcept
    {
        auto* session = mWiring->sessions.session(connection);
        if (!session || !session->principal() || !session->sessionId() || !session->preparedResumeId()) return false;
        const auto before = mWiring->reducer.state();
        auto prepared = mWiring->lifecycle.prepareResume(
            *session->principal(), *session->sessionId(), mWiring->clock.now(), tick);
        auto* lifecycle = std::get_if<ServerLifecyclePreparation>(&prepared);
        if (!lifecycle || lifecycle->generation != session->generation())
        {
            if (lifecycle) (void)mWiring->lifecycle.cancel(lifecycle->id);
            (void)session->cancelPreparedResume();
            return false;
        }
        const auto cancel = [&]() noexcept {
            (void)mWiring->lifecycle.cancel(lifecycle->id);
            (void)session->cancelPreparedResume();
        };
        const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
        auto views = candidate ? projectFixtureViews(*candidate, tick) : std::nullopt;
        auto observations = candidate ? projectFixtureObservations(before, *candidate, tick) : std::nullopt;
        auto accepted = session->takeAuthenticationAccepted();
        if (!candidate || !views || !observations || !accepted) { cancel(); return false; }
        const auto view = std::ranges::find_if(*views,
            [&](const auto& value) { return value.first == *session->sessionId(); });
        if (view == views->end()) { cancel(); return false; }

        try
        {
            std::vector<ObservationChange> initialChanges;
            for (const auto& entry : view->second.view().entries())
                initialChanges.push_back({ entry.playerId(), entry.entityId(), ObservationChangeKind::Enter });
            auto initial = ReliableObservationBatch::create(
                *session->sessionId(), session->generation(), tick, initialChanges);
            if (!std::holds_alternative<ReliableObservationBatch>(initial)) { cancel(); return false; }

            std::vector<std::vector<std::byte>> frames;
            frames.reserve(3 + observations->size() * 2);
            auto addFrame = [&](MessageClass messageClass, MessageKind kind, std::vector<std::byte> payload) {
                auto encoded = encodeProtocolFrame(messageClass, kind, payload);
                if (!std::holds_alternative<std::vector<std::byte>>(encoded)) return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(encoded)));
                return true;
            };
            if (!addFrame(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
                    encodeAuthenticationAccepted(*accepted))
                || !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableObservationBatch,
                    encodeReliableObservationBatch(std::get<ReliableObservationBatch>(initial)))
                || !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                    encodeLatestWinsSnapshot(view->second))) { cancel(); return false; }

            std::vector<OutboundQueueSet::AtomicMessage> messages;
            messages.reserve(3 + observations->size() * 2);
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[0] });
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames[1] });
            messages.push_back({ connection, TransportChannel::LatestWins, frames[2] });
            for (const auto& delivery : *observations)
            {
                auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
                if (!target
                    || !addFrame(MessageClass::ReliableOperation, MessageKind::ReliableObservationBatch,
                        encodeReliableObservationBatch(delivery.observations))
                    || !addFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
                        encodeLatestWinsSnapshot(delivery.view))) { cancel(); return false; }
                messages.push_back({ *target, TransportChannel::ReliableOrdered, frames[frames.size() - 2] });
                messages.push_back({ *target, TransportChannel::LatestWins, frames.back() });
            }
            if (mWiring->queues.enqueueMessagesAtomically(messages) != TransportResult::Accepted)
            { cancel(); return false; }
        }
        catch (...) { cancel(); return false; }

        if (!session->commitPreparedResume()) { (void)mWiring->lifecycle.cancel(lifecycle->id); return false; }
        if (!mWiring->lifecycle.commit(lifecycle->id))
        { (void)session->rollbackPreparedResume(); return false; }
        return session->finalizePreparedResume();
    }

    bool ServerApplication::expireSessions(ServerTick tick) noexcept
    {
        while (true)
        {
            const auto before = mWiring->reducer.state();
            auto prepared = mWiring->lifecycle.prepareNextExpiration(mWiring->clock.now(), tick);
            if (const auto* error = std::get_if<ServerLifecycleError>(&prepared))
            {
                if (*error == ServerLifecycleError::DeadlineReached) return true;
                return false;
            }
            auto* lifecycle = std::get_if<ServerLifecyclePreparation>(&prepared);
            if (!lifecycle) return false;
            const auto cancel = [this, id = lifecycle->id]() noexcept { (void)mWiring->lifecycle.cancel(id); };
            const auto* candidate = mWiring->lifecycle.candidateState(lifecycle->id);
            auto projected = candidate ? projectFixtureObservations(before, *candidate, tick) : std::nullopt;
            if (!projected) { cancel(); return false; }
            std::vector<std::pair<TransportConnectionId, FixtureObservationDelivery>> routed;
            try
            {
                routed.reserve(projected->size());
                for (auto& delivery : *projected)
                {
                    auto target = mWiring->sessions.connectionForSession(delivery.targetSession);
                    if (!target) { cancel(); return false; }
                    routed.emplace_back(*target, std::move(delivery));
                }
            }
            catch (...) { cancel(); return false; }
            if (!admitFixtureObservationsAtomically(mWiring->queues, routed)) { cancel(); return false; }
            if (!mWiring->lifecycle.commit(lifecycle->id)) return false;
        }
    }

    bool ServerApplication::pump(ServerTick tick) noexcept
    {
        if (!mRunning) return false;
        std::array<TransportEvent, 128> events{};
        const auto result = mTransport.poll(events);
        if (result.result != TransportResult::Accepted)
        {
            mFailure = "transport poll failed";
            stop();
            return false;
        }
        if (!mWiring) return true;

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
                if (!disconnectConnection(*event.connection, tick))
                { mFailure = "disconnect lifecycle failed"; return false; }
            }
            else if (event.kind == TransportEventKind::RuntimeFailed)
            {
                mFailure = "transport runtime failed";
                stop();
                return false;
            }
        }

        if (!expireSessions(tick)) { mFailure = "expiration lifecycle failed"; return false; }

        std::array<TransportMessage, TransportRuntime::MaxMessagesPerReceive> messages{};
        for (const auto connection : mWiring->sessions.connections())
        {
            const auto received = mTransport.receive(connection, messages);
            if (received.result != TransportResult::Accepted)
            {
                (void)failConnection(connection, "transport receive failed");
                continue;
            }
            bool closed = false;
            for (std::size_t index = 0; index < received.messages; ++index)
            {
                const auto dispatched = mWiring->sessions.dispatch(
                    connection, messages[index], mWiring->joins, mWiring->crypto, mWiring->intake, tick);
                if (dispatched == ConnectionSessionResult::ProtocolRejected
                    || dispatched == ConnectionSessionResult::QueueRejected
                    || dispatched == ConnectionSessionResult::SessionRejected
                    || dispatched == ConnectionSessionResult::UnknownConnection)
                {
                    (void)failConnection(connection, "connection dispatch rejected");
                    closed = true;
                    break;
                }
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
            if (closed) continue;
            auto* session = mWiring->sessions.session(connection);
            if (session != nullptr && session->state() == ServerSessionState::AuthenticationPending)
            {
                const auto advanced = mWiring->sessions.pollAuthentication(
                    connection, mWiring->joins, mWiring->crypto, tick);
                if (advanced != ConnectionSessionResult::AuthenticationPending
                    && advanced != ConnectionSessionResult::Joined
                    && advanced != ConnectionSessionResult::ResumePrepared)
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
                else if (advanced == ConnectionSessionResult::ResumePrepared
                    && !resumeConnection(connection, tick))
                {
                    (void)failConnection(connection, "resume composition failed");
                    continue;
                }
            }
            const auto now = mWiring->clock.now().nanoseconds() / 1'000'000;
            const auto pumped = mWiring->queues.pump(mTransport, connection, now);
            if (!pumped || *pumped == OutboundPumpResult::TransportFailed
                || *pumped == OutboundPumpResult::InvalidTime
                || *pumped == OutboundPumpResult::SlowPeerEvicted)
            {
                (void)failConnection(connection, "connection send failed");
                continue;
            }
        }
        const auto pumpedCommands = mWiring->intake.pump();
        if (!pumpedCommands) { mFailure = "command intake failed"; return false; }
        for (const auto& batch : pumpedCommands.batches())
        {
            const auto before = mWiring->reducer.state();
            auto prepared = mWiring->reducer.prepareTick(batch);
            if (!prepared.result()) { mFailure = "command reduction failed"; return false; }
            auto projected = projectFixtureObservations(before, prepared.candidateState(), batch.scheduledTick().value());
            if (!projected) { mFailure = "observation projection failed"; return false; }
            std::vector<std::pair<TransportConnectionId, FixtureObservationDelivery>> routed;
            routed.reserve(projected->size());
            for (auto& delivery : *projected)
            {
                auto connection = mWiring->sessions.connectionForSession(delivery.targetSession);
                if (!connection) { mFailure = "observation target missing"; return false; }
                routed.emplace_back(*connection, std::move(delivery));
            }
            auto views = projectFixtureViews(prepared.candidateState(), batch.scheduledTick().value());
            if (!views) { mFailure = "movement view projection failed"; return false; }
            std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>> routedViews;
            routedViews.reserve(views->size());
            for (auto& delivery : *views)
            {
                auto connection = mWiring->sessions.connectionForSession(delivery.first);
                if (!connection) { mFailure = "movement view target missing"; return false; }
                routedViews.emplace_back(*connection, std::move(delivery.second));
            }
            if (!admitFixtureTickAtomically(mWiring->queues, routed, routedViews))
            { mFailure = "tick output admission failed"; return false; }
            if (!mWiring->reducer.commit(std::move(prepared)))
            { mFailure = "canonical commit failed"; return false; }
        }
        return true;
    }

    bool ServerApplication::stop() noexcept
    {
        if (!mRunning && !mListener) return true;
        mRunning = false;
        bool success = true;
        if (mListener)
        {
            const auto stopped = mTransport.stopListener(*mListener);
            success = stopped == TransportResult::Accepted || stopped == TransportResult::AlreadyFinalized;
            mListener.reset();
        }
        const auto shutDown = mTransport.shutdown();
        success = success && (shutDown == TransportResult::Accepted || shutDown == TransportResult::AlreadyFinalized);
        if (!success) mFailure = "transport shutdown failed";
        return success;
    }
}
