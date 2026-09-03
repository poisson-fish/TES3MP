#ifndef OPENMW_TES3MP_PROVIDERS_HPP
#define OPENMW_TES3MP_PROVIDERS_HPP

#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_exchange.hpp>

#include <optional>
#include <span>

namespace TES3MP::OpenMWAdapter
{
    enum class ConnectionStatus
    {
        ProtocolRejected,
        AuthenticationRejected,
        TimedOut,
        TransportFailed,
        Disconnected,
        Reconnecting,
        Resumed,
        ResumeFailed,
        ContentMappingFailed,
        PresentationFailed,
    };

    enum class ProviderResult
    {
        Accepted,
        ContentMappingFailed,
        PresentationFailed,
    };

    struct CellTransitionCapture
    {
        ProviderResult result = ProviderResult::Accepted;
        std::optional<FixtureCellTransition> transition;
    };

    class ConnectionStatusProvider
    {
    public:
        virtual ~ConnectionStatusProvider() = default;
        virtual void report(ConnectionStatus status) noexcept = 0;
    };

    class ConnectionControlProvider
    {
    public:
        virtual ~ConnectionControlProvider() = default;
        virtual bool disconnectRequested() noexcept = 0;
    };

    class SemanticInputProvider
    {
    public:
        virtual ~SemanticInputProvider() = default;
        virtual CellTransitionCapture captureCellTransition() noexcept = 0;
        virtual std::optional<PlayerMotionIntent> sampleCurrentIntent() noexcept = 0;
    };

    class PresentationProvider
    {
    public:
        virtual ~PresentationProvider() = default;
        virtual ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection,
            MonotonicInstant receivedAt) noexcept = 0;
        virtual ProviderResult advance(MonotonicInstant now) noexcept = 0;
        virtual void clear() noexcept = 0;
    };
}

#endif
