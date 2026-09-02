#include "server_application.hpp"

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
                (void)mWiring->sessions.close(*event.connection);
            else if (event.kind == TransportEventKind::RuntimeFailed)
            {
                mFailure = "transport runtime failed";
                stop();
                return false;
            }
        }

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
                    connection, messages[index], mWiring->joins, mWiring->crypto, tick);
                if (dispatched == ConnectionSessionResult::ProtocolRejected
                    || dispatched == ConnectionSessionResult::QueueRejected
                    || dispatched == ConnectionSessionResult::SessionRejected
                    || dispatched == ConnectionSessionResult::UnknownConnection)
                {
                    (void)failConnection(connection, "connection dispatch rejected");
                    closed = true;
                    break;
                }
            }
            if (closed) continue;
            auto* session = mWiring->sessions.session(connection);
            if (session != nullptr && session->state() == ServerSessionState::AuthenticationPending)
            {
                const auto advanced = mWiring->sessions.pollAuthentication(
                    connection, mWiring->joins, mWiring->crypto, tick);
                if (advanced != ConnectionSessionResult::AuthenticationPending
                    && advanced != ConnectionSessionResult::Joined)
                {
                    (void)failConnection(connection, "authentication rejected");
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
