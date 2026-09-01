#include "server_application.hpp"

#include <array>

namespace TES3MP::ServerApp
{
    ServerApplication::ServerApplication(TransportRuntime& transport, const ServerConfig& config) noexcept
        : mTransport(transport), mConfig(config) {}

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

    bool ServerApplication::pump() noexcept
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
