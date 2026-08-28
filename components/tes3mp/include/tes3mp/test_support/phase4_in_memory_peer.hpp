#ifndef TES3MP_TEST_SUPPORT_PHASE4_IN_MEMORY_PEER_HPP
#define TES3MP_TEST_SUPPORT_PHASE4_IN_MEMORY_PEER_HPP

#include <tes3mp/protocol_exchange.hpp>
#include <tes3mp/value_types.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP::TestSupport
{
    enum class Phase4PeerError : std::uint8_t
    {
        None,
        InvalidFixture,
        LinkFailure,
        FrameFailure,
        ProtocolFailure,
        SessionFailure,
    };

    enum class Phase4PeerTraceStep : std::uint8_t
    {
        ClientHelloSent,
        ServerHelloAccepted,
        AuthenticationSucceeded,
        SessionBound,
        ReliableOperationDelivered,
        LatestWinsSnapshotApplied,
    };

    class Phase4InMemoryPeer
    {
    public:
        static std::variant<std::unique_ptr<Phase4InMemoryPeer>, Phase4PeerError> create(
            SessionId sessionId, SessionGeneration generation, PrincipalId principalId);

        Phase4InMemoryPeer(const Phase4InMemoryPeer&) = delete;
        Phase4InMemoryPeer& operator=(const Phase4InMemoryPeer&) = delete;
        ~Phase4InMemoryPeer();

        Phase4PeerError exchange(ReliableOperation operation, LatestWinsSnapshot snapshot);

        std::span<const Phase4PeerTraceStep> trace() const noexcept;
        const std::optional<ReliableOperation>& deliveredOperation() const noexcept;
        const std::optional<LatestWinsSnapshot>& confirmedSnapshot() const noexcept;

    private:
        class Impl;

        explicit Phase4InMemoryPeer(std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> mImpl;
    };
}

#endif
