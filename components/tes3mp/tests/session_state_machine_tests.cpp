#include <tes3mp/authentication.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/server_session.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <ostream>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    template <class Type>
    concept EqualityComparable = requires(const Type& left, const Type& right) { left == right; };

    template <class Type>
    concept StreamInsertable = requires(std::ostream& stream, const Type& value) { stream << value; };

    constexpr std::uint64_t TestTimeoutNanoseconds = MinimumSessionStageTimeoutNanoseconds;

    static_assert(!std::is_copy_constructible_v<AuthenticationMaterial>);
    static_assert(!std::is_copy_assignable_v<AuthenticationMaterial>);
    static_assert(std::is_move_constructible_v<AuthenticationMaterial>);
    static_assert(!std::is_default_constructible_v<AuthenticationMaterial>);
    static_assert(!EqualityComparable<AuthenticationMaterial>);
    static_assert(!StreamInsertable<AuthenticationMaterial>);
    static_assert(sizeof(AuthenticatedPrincipal) == sizeof(PrincipalId));
    static_assert(std::variant_size_v<AuthenticationResult> == 2);
    static_assert(std::variant_size_v<ServerSessionEvent> == 7);
    static_assert(std::variant_size_v<ClientSessionEvent> == 9);

    ProtocolVersionRange versionRange(std::uint16_t major, std::uint16_t minimum, std::uint16_t maximum)
    {
        return std::get<ProtocolVersionRange>(ProtocolVersionRange::create(major, minimum, maximum));
    }

    CapabilityId capability(std::uint32_t value)
    {
        return *CapabilityId::fromValue(value);
    }

    CapabilityOffer offer(ProtocolVersionRange versions, std::initializer_list<std::uint32_t> optional,
        std::initializer_list<std::uint32_t> required)
    {
        std::vector<CapabilityId> optionalValues;
        std::vector<CapabilityId> requiredValues;
        for (const auto value : optional)
            optionalValues.push_back(capability(value));
        for (const auto value : required)
            requiredValues.push_back(capability(value));
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, optionalValues, requiredValues));
    }

    ClientHello clientHello(std::uint16_t major = 1)
    {
        return ClientHello::fromOffer(offer(versionRange(major, 0, 1), { 1, 3, 99 }, { 5 }));
    }

    CapabilityOffer serverOffer()
    {
        return offer(versionRange(1, 0, 1), { 1, 4, 5 }, { 3 });
    }

    SessionTimeoutPolicy timeoutPolicy()
    {
        return *SessionTimeoutPolicy::create(TestTimeoutNanoseconds, TestTimeoutNanoseconds, TestTimeoutNanoseconds);
    }

    AuthenticationMaterial material(std::span<const std::byte> bytes = {})
    {
        return std::move(*AuthenticationMaterial::create(bytes));
    }

    PrincipalId principal(std::uint64_t value = 7)
    {
        return *PrincipalId::fromValue(value);
    }

    enum class ProviderMode
    {
        ImmediateSuccess,
        DelayedSuccess,
        Rejected,
        MalformedRejected,
        WrongThenSuccess,
        WrongGenerationThenSuccess,
        Unavailable,
    };

    struct ProviderControl
    {
        std::size_t begins = 0;
        std::size_t polls = 0;
        std::size_t cancellations = 0;
        std::size_t observedBytes = 0;
        bool canaryMatched = false;
        AuthenticationAttempt lastAttempt{ AuthenticationAttemptId::initial(), SessionGeneration::initial() };
    };

    class FakeAuthenticationOperation final : public AuthenticationOperation
    {
    public:
        FakeAuthenticationOperation(
            std::shared_ptr<ProviderControl> control, AuthenticationAttempt attempt, ProviderMode mode)
            : mControl(std::move(control))
            , mAttempt(attempt)
            , mMode(mode)
        {
        }

        AuthenticationPollResult poll() noexcept override
        {
            ++mControl->polls;
            if (mMode == ProviderMode::DelayedSuccess && mPollIndex++ == 0)
                return AuthenticationPending{};
            if (mMode == ProviderMode::WrongThenSuccess && mPollIndex++ == 0)
            {
                const auto wrong = mAttempt.id.next().value();
                return AuthenticationCompletion{ AuthenticationAttempt{ wrong, mAttempt.generation },
                    AuthenticatedPrincipal{ principal() } };
            }
            if (mMode == ProviderMode::WrongGenerationThenSuccess && mPollIndex++ == 0)
            {
                const auto wrong = mAttempt.generation.next().value();
                return AuthenticationCompletion{ AuthenticationAttempt{ mAttempt.id, wrong },
                    AuthenticatedPrincipal{ principal() } };
            }
            if (mMode == ProviderMode::Rejected || mMode == ProviderMode::MalformedRejected)
            {
                return AuthenticationCompletion{ mAttempt,
                    AuthenticationRejected{ mMode == ProviderMode::MalformedRejected
                            ? AuthenticationRejectionReason::MalformedInput
                            : AuthenticationRejectionReason::Denied } };
            }
            return AuthenticationCompletion{ mAttempt, AuthenticatedPrincipal{ principal() } };
        }

        void cancel() noexcept override
        {
            if (!mCancelled)
            {
                ++mControl->cancellations;
                mCancelled = true;
            }
        }

    private:
        std::shared_ptr<ProviderControl> mControl;
        AuthenticationAttempt mAttempt;
        ProviderMode mMode;
        std::size_t mPollIndex = 0;
        bool mCancelled = false;
    };

    class FakeAuthenticationProvider final : public AuthenticationProvider
    {
    public:
        FakeAuthenticationProvider(ProviderMode mode, std::span<const std::byte> expected = {})
            : control(std::make_shared<ProviderControl>())
            , mMode(mode)
            , mExpected(expected.begin(), expected.end())
        {
        }

        std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, AuthenticationMaterial authenticationMaterial) noexcept override
        {
            ++control->begins;
            control->lastAttempt = attempt;
            const auto bytes = materialBytes(authenticationMaterial);
            control->observedBytes = bytes.size();
            control->canaryMatched = std::equal(bytes.begin(), bytes.end(), mExpected.begin(), mExpected.end());
            if (mMode == ProviderMode::Unavailable)
                return {};
            return std::make_unique<FakeAuthenticationOperation>(control, attempt, mMode);
        }

        std::shared_ptr<ProviderControl> control;

    private:
        ProviderMode mMode;
        std::vector<std::byte> mExpected;
    };

    struct ObservationFixture
    {
        std::unique_ptr<RecordingMetricSink> metrics = RecordingMetricSink::create(128);
        std::unique_ptr<RecordingStructuredEventSink> events = RecordingStructuredEventSink::create(128);
        Observability observability{ *metrics, *events };
    };

    std::unique_ptr<ServerSessionStateMachine> makeServer(ManualClock& clock, ObservationFixture& observations,
        FakeAuthenticationProvider& provider, CapabilityOffer configuredOffer = serverOffer())
    {
        auto created = ServerSessionStateMachine::create(clock, observations.observability, timeoutPolicy(),
            SessionGeneration::initial(), std::move(configuredOffer), provider);
        return std::move(std::get<std::unique_ptr<ServerSessionStateMachine>>(created));
    }

    std::unique_ptr<ClientSessionStateMachine> makeClient(ManualClock& clock)
    {
        auto created = ClientSessionStateMachine::create(clock, timeoutPolicy(), SessionGeneration::initial());
        return std::move(std::get<std::unique_ptr<ClientSessionStateMachine>>(created));
    }

    ServerHello advanceServerToAuthenticationInput(ServerSessionStateMachine& server)
    {
        const auto encrypted = server.handle(ServerEncryptedTransportReady{});
        const auto negotiated = server.handle(ServerClientHelloReceived{ clientHello() });
        if (!encrypted.accepted() || !negotiated.accepted() || negotiated.action != ServerSessionAction::SendServerHello
            || !server.negotiatedHello())
            return std::get<ServerHello>(negotiateClientHello(clientHello(), serverOffer()));
        return *server.negotiatedHello();
    }

    void moveServerToState(ServerSessionStateMachine& server, ManualClock& clock, ServerSessionState destination)
    {
        if (destination == ServerSessionState::AwaitingEncryptedTransport)
            return;
        if (destination == ServerSessionState::TimedOut)
        {
            clock.advance(TestTimeoutNanoseconds);
            server.handle(ServerCheckTimeout{});
            return;
        }
        if (destination == ServerSessionState::Cancelled)
        {
            server.handle(ServerCancel{});
            return;
        }
        if (destination == ServerSessionState::Closed)
        {
            server.handle(ServerClose{});
            return;
        }
        server.handle(ServerEncryptedTransportReady{});
        if (destination == ServerSessionState::AwaitingClientHello)
            return;
        if (destination == ServerSessionState::Rejected)
        {
            server.handle(ServerClientHelloReceived{ clientHello(2) });
            return;
        }
        server.handle(ServerClientHelloReceived{ clientHello() });
        if (destination == ServerSessionState::AwaitingAuthenticationInput)
            return;
        server.handle(ServerAuthenticationSubmitted{ material() });
        if (destination == ServerSessionState::AuthenticationPending)
            return;
        server.handle(ServerPollAuthentication{});
    }

    ServerSessionEvent serverEvent(ServerSessionEventKind kind)
    {
        switch (kind)
        {
            case ServerSessionEventKind::EncryptedTransportReady:
                return ServerEncryptedTransportReady{};
            case ServerSessionEventKind::ClientHelloReceived:
                return ServerClientHelloReceived{ clientHello() };
            case ServerSessionEventKind::AuthenticationSubmitted:
                return ServerAuthenticationSubmitted{ material() };
            case ServerSessionEventKind::PollAuthentication:
                return ServerPollAuthentication{};
            case ServerSessionEventKind::CheckTimeout:
                return ServerCheckTimeout{};
            case ServerSessionEventKind::Cancel:
                return ServerCancel{};
            case ServerSessionEventKind::Close:
                return ServerClose{};
        }
        return ServerClose{};
    }

    void moveClientToState(ClientSessionStateMachine& client, ManualClock& clock, ClientSessionState destination)
    {
        if (destination == ClientSessionState::AwaitingEncryptedTransport)
            return;
        if (destination == ClientSessionState::TimedOut)
        {
            clock.advance(TestTimeoutNanoseconds);
            client.handle(ClientCheckTimeout{});
            return;
        }
        if (destination == ClientSessionState::Cancelled)
        {
            client.handle(ClientCancel{});
            return;
        }
        if (destination == ClientSessionState::Closed)
        {
            client.handle(ClientClose{});
            return;
        }
        client.handle(ClientEncryptedTransportReady{});
        if (destination == ClientSessionState::AwaitingServerHello)
            return;
        if (destination == ClientSessionState::Rejected)
        {
            client.handle(ClientSessionRejectedReceived{
                std::get<SessionRejected>(negotiateClientHello(clientHello(2), serverOffer())) });
            return;
        }
        client.handle(
            ClientServerHelloReceived{ std::get<ServerHello>(negotiateClientHello(clientHello(), serverOffer())) });
        if (destination == ClientSessionState::AwaitingAuthenticationInput)
            return;
        client.handle(ClientAuthenticationSubmitted{});
        if (destination == ClientSessionState::AwaitingAuthenticationResult)
            return;
        client.handle(ClientAuthenticationAccepted{});
    }

    ClientSessionEvent clientEvent(ClientSessionEventKind kind)
    {
        switch (kind)
        {
            case ClientSessionEventKind::EncryptedTransportReady:
                return ClientEncryptedTransportReady{};
            case ClientSessionEventKind::ServerHelloReceived:
                return ClientServerHelloReceived{ std::get<ServerHello>(
                    negotiateClientHello(clientHello(), serverOffer())) };
            case ClientSessionEventKind::SessionRejectedReceived:
                return ClientSessionRejectedReceived{ std::get<SessionRejected>(
                    negotiateClientHello(clientHello(2), serverOffer())) };
            case ClientSessionEventKind::AuthenticationSubmitted:
                return ClientAuthenticationSubmitted{};
            case ClientSessionEventKind::AuthenticationAccepted:
                return ClientAuthenticationAccepted{};
            case ClientSessionEventKind::AuthenticationRejected:
                return ClientAuthenticationRejected{ AuthenticationRejectionReason::Denied };
            case ClientSessionEventKind::CheckTimeout:
                return ClientCheckTimeout{};
            case ClientSessionEventKind::Cancel:
                return ClientCancel{};
            case ClientSessionEventKind::Close:
                return ClientClose{};
        }
        return ClientClose{};
    }

    bool bounded_secret_and_timeout_factories_fail_closed()
    {
        const std::array<std::byte, MaximumAuthenticationMaterialBytes> exact{};
        const std::array<std::byte, MaximumAuthenticationMaterialBytes + 1> excessive{};
        auto empty = AuthenticationMaterial::create({});
        auto maximum = AuthenticationMaterial::create(exact);
        if (!empty || !empty->empty() || !maximum || maximum->size() != MaximumAuthenticationMaterialBytes
            || AuthenticationMaterial::create(excessive))
            return false;

        AuthenticationMaterial moved(std::move(*maximum));
        if (moved.size() != MaximumAuthenticationMaterialBytes || !maximum->empty())
            return false;

        return !SessionTimeoutPolicy::create(
                   MinimumSessionStageTimeoutNanoseconds - 1, TestTimeoutNanoseconds, TestTimeoutNanoseconds)
            && !SessionTimeoutPolicy::create(
                MaximumSessionStageTimeoutNanoseconds + 1, TestTimeoutNanoseconds, TestTimeoutNanoseconds)
            && SessionTimeoutPolicy::create(
                MinimumSessionStageTimeoutNanoseconds, MaximumSessionStageTimeoutNanoseconds, TestTimeoutNanoseconds)
            && !sessionDeadline(MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max()), 1);
    }

    bool successful_client_server_path_is_ordered_owned_and_observable()
    {
        const std::array canary{ std::byte{ 's' }, std::byte{ 'e' }, std::byte{ 'c' }, std::byte{ 'r' },
            std::byte{ 'e' }, std::byte{ 't' } };
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess, canary);
        auto server = makeServer(clock, observations, provider);
        auto client = makeClient(clock);

        const auto early = server->handle(ServerAuthenticationSubmitted{ material(canary) });
        if (early.accepted() || server->state() != ServerSessionState::AwaitingEncryptedTransport
            || provider.control->begins != 0)
            return false;

        if (client->handle(ClientEncryptedTransportReady{}).action != ClientSessionAction::SendClientHello
            || server->handle(ServerEncryptedTransportReady{}).action != ServerSessionAction::None)
            return false;
        if (server->handle(ServerClientHelloReceived{ clientHello() }).action != ServerSessionAction::SendServerHello)
            return false;
        const auto serverHello = *server->negotiatedHello();
        if (client->handle(ClientServerHelloReceived{ serverHello }).action
            != ClientSessionAction::AuthenticationInputReady)
            return false;
        if (client->handle(ClientAuthenticationSubmitted{}).action != ClientSessionAction::AuthenticationSubmitted
            || server->handle(ServerAuthenticationSubmitted{ material(canary) }).action
                != ServerSessionAction::AuthenticationStarted)
            return false;
        if (server->handle(ServerPollAuthentication{}).action != ServerSessionAction::AuthenticationPending
            || server->handle(ServerPollAuthentication{}).action != ServerSessionAction::SessionEstablished
            || client->handle(ClientAuthenticationAccepted{}).action != ClientSessionAction::SessionEstablished)
            return false;

        const bool sawAuthenticationSuccess = std::any_of(observations.events->events().begin(),
            observations.events->events().end(), [](const StructuredEvent& event) {
                const auto* lifecycle = std::get_if<SessionLifecycleEvent>(&event.payload());
                return lifecycle != nullptr && lifecycle->outcome == SessionObservationOutcome::AuthenticationSucceeded;
            });
        return server->state() == ServerSessionState::Established && client->state() == ClientSessionState::Established
            && server->principal() && server->principal()->id == principal() && server->negotiatedHello()
            && client->negotiatedHello() && server->negotiatedHello()->selectedVersion() == ProtocolVersion{ 1, 1 }
        && provider.control->begins == 1 && provider.control->polls == 2
            && provider.control->observedBytes == canary.size() && provider.control->canaryMatched
            && !observations.metrics->observations().empty() && sawAuthenticationSuccess;
    }

    bool protocol_and_authentication_rejections_are_terminal()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::Rejected);
        auto incompatible = makeServer(clock, observations, provider);
        incompatible->handle(ServerEncryptedTransportReady{});
        const auto protocol = incompatible->handle(ServerClientHelloReceived{ clientHello(2) });
        if (protocol.action != ServerSessionAction::SendSessionRejected
            || incompatible->state() != ServerSessionState::Rejected || !incompatible->protocolRejection()
            || provider.control->begins != 0)
            return false;

        auto server = makeServer(clock, observations, provider);
        const auto negotiated = advanceServerToAuthenticationInput(*server);
        (void)negotiated;
        server->handle(ServerAuthenticationSubmitted{ material() });
        const auto rejected = server->handle(ServerPollAuthentication{});
        if (rejected.action != ServerSessionAction::AuthenticationRejected
            || server->state() != ServerSessionState::Rejected || !server->authenticationRejection()
            || server->authenticationRejection()->reason != AuthenticationRejectionReason::Denied
            || server->principal())
            return false;

        auto client = makeClient(clock);
        client->handle(ClientEncryptedTransportReady{});
        client->handle(
            ClientServerHelloReceived{ std::get<ServerHello>(negotiateClientHello(clientHello(), serverOffer())) });
        client->handle(ClientAuthenticationSubmitted{});
        const auto clientRejected
            = client->handle(ClientAuthenticationRejected{ AuthenticationRejectionReason::Denied });
        if (clientRejected.action != ClientSessionAction::SessionRejected
            || client->state() != ClientSessionState::Rejected
            || client->authenticationRejection() != AuthenticationRejectionReason::Denied)
            return false;

        auto protocolClient = makeClient(clock);
        protocolClient->handle(ClientEncryptedTransportReady{});
        const auto clientProtocol = protocolClient->handle(ClientSessionRejectedReceived{
            std::get<SessionRejected>(negotiateClientHello(clientHello(2), serverOffer())) });
        return clientProtocol.action == ClientSessionAction::SessionRejected
            && protocolClient->state() == ClientSessionState::Rejected && protocolClient->protocolRejection()
            && protocolClient->protocolRejection()->reason() == SessionRejectionReason::ProtocolMajorMismatch
            && clientRejected.action == ClientSessionAction::SessionRejected
            && client->state() == ClientSessionState::Rejected
            && client->authenticationRejection() == AuthenticationRejectionReason::Denied;
    }

    bool provider_outcomes_and_exact_secret_bound_are_closed()
    {
        const std::array<std::byte, MaximumAuthenticationMaterialBytes> exact{};
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider immediate(ProviderMode::ImmediateSuccess, exact);
        auto success = makeServer(clock, observations, immediate);
        advanceServerToAuthenticationInput(*success);
        if (success->handle(ServerAuthenticationSubmitted{ material(exact) }).action
                != ServerSessionAction::AuthenticationStarted
            || success->handle(ServerPollAuthentication{}).action != ServerSessionAction::SessionEstablished
            || immediate.control->begins != 1 || immediate.control->observedBytes != exact.size()
            || !immediate.control->canaryMatched)
            return false;

        FakeAuthenticationProvider unavailable(ProviderMode::Unavailable);
        auto rejected = makeServer(clock, observations, unavailable);
        advanceServerToAuthenticationInput(*rejected);
        const auto outcome = rejected->handle(ServerAuthenticationSubmitted{ material() });
        if (outcome.action != ServerSessionAction::AuthenticationRejected
            || rejected->state() != ServerSessionState::Rejected || unavailable.control->begins != 1
            || !rejected->authenticationRejection()
            || rejected->authenticationRejection()->reason != AuthenticationRejectionReason::ProviderUnavailable)
            return false;

        FakeAuthenticationProvider malformed(ProviderMode::MalformedRejected);
        auto malformedSession = makeServer(clock, observations, malformed);
        advanceServerToAuthenticationInput(*malformedSession);
        malformedSession->handle(ServerAuthenticationSubmitted{ material() });
        const auto malformedOutcome = malformedSession->handle(ServerPollAuthentication{});
        return malformedOutcome.action == ServerSessionAction::AuthenticationRejected
            && malformedSession->authenticationRejection()
            && malformedSession->authenticationRejection()->reason == AuthenticationRejectionReason::MalformedInput;
    }

    bool exact_deadlines_cancel_once_and_overflow_is_atomic()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess);
        auto server = makeServer(clock, observations, provider);
        clock.advance(TestTimeoutNanoseconds - 1);
        if (server->handle(ServerCheckTimeout{}).action != ServerSessionAction::None
            || server->state() != ServerSessionState::AwaitingEncryptedTransport)
            return false;
        clock.advance(1);
        if (server->handle(ServerCheckTimeout{}).action != ServerSessionAction::SessionTimedOut
            || server->state() != ServerSessionState::TimedOut)
            return false;

        ManualClock providerClock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture providerObservations;
        FakeAuthenticationProvider pending(ProviderMode::DelayedSuccess);
        auto pendingServer = makeServer(providerClock, providerObservations, pending);
        advanceServerToAuthenticationInput(*pendingServer);
        pendingServer->handle(ServerAuthenticationSubmitted{ material() });
        providerClock.advance(TestTimeoutNanoseconds - 1);
        if (pendingServer->handle(ServerCheckTimeout{}).action != ServerSessionAction::None
            || pending.control->cancellations != 0)
            return false;
        providerClock.advance(1);
        if (pendingServer->handle(ServerCheckTimeout{}).action != ServerSessionAction::SessionTimedOut
            || pending.control->cancellations != 1)
            return false;
        pendingServer->handle(ServerCancel{});
        pendingServer->handle(ServerCheckTimeout{});
        if (pending.control->cancellations != 1)
            return false;

        ManualClock overflowClock(
            MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max() - TestTimeoutNanoseconds + 1));
        auto overflow = ServerSessionStateMachine::create(overflowClock, providerObservations.observability,
            timeoutPolicy(), SessionGeneration::initial(), serverOffer(), pending);
        return std::holds_alternative<SessionTransitionError>(overflow)
            && std::get<SessionTransitionError>(overflow).code == SessionTransitionErrorCode::DeadlineOverflow;
    }

    bool every_server_and_client_stage_times_out_at_the_exact_deadline()
    {
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess);

        ManualClock serverInputClock(MonotonicInstant::fromNanoseconds(0));
        auto serverInput = makeServer(serverInputClock, observations, provider);
        advanceServerToAuthenticationInput(*serverInput);
        serverInputClock.advance(TestTimeoutNanoseconds - 1);
        if (serverInput->handle(ServerCheckTimeout{}).action != ServerSessionAction::None)
            return false;
        serverInputClock.advance(1);
        if (serverInput->handle(ServerCheckTimeout{}).action != ServerSessionAction::SessionTimedOut
            || serverInput->state() != ServerSessionState::TimedOut)
            return false;

        ManualClock clientTransportClock(MonotonicInstant::fromNanoseconds(0));
        auto clientTransport = makeClient(clientTransportClock);
        clientTransportClock.advance(TestTimeoutNanoseconds - 1);
        if (clientTransport->handle(ClientCheckTimeout{}).action != ClientSessionAction::None)
            return false;
        clientTransportClock.advance(1);
        if (clientTransport->handle(ClientCheckTimeout{}).action != ClientSessionAction::SessionTimedOut)
            return false;

        const auto hello = std::get<ServerHello>(negotiateClientHello(clientHello(), serverOffer()));
        ManualClock clientInputClock(MonotonicInstant::fromNanoseconds(0));
        auto clientInput = makeClient(clientInputClock);
        clientInput->handle(ClientEncryptedTransportReady{});
        clientInput->handle(ClientServerHelloReceived{ hello });
        clientInputClock.advance(TestTimeoutNanoseconds - 1);
        if (clientInput->handle(ClientCheckTimeout{}).action != ClientSessionAction::None)
            return false;
        clientInputClock.advance(1);
        if (clientInput->handle(ClientCheckTimeout{}).action != ClientSessionAction::SessionTimedOut)
            return false;

        ManualClock clientProviderClock(MonotonicInstant::fromNanoseconds(0));
        auto clientProvider = makeClient(clientProviderClock);
        clientProvider->handle(ClientEncryptedTransportReady{});
        clientProvider->handle(ClientServerHelloReceived{ hello });
        clientProvider->handle(ClientAuthenticationSubmitted{});
        clientProviderClock.advance(TestTimeoutNanoseconds - 1);
        if (clientProvider->handle(ClientCheckTimeout{}).action != ClientSessionAction::None)
            return false;
        clientProviderClock.advance(1);
        return clientProvider->handle(ClientCheckTimeout{}).action == ClientSessionAction::SessionTimedOut
            && clientProvider->state() == ClientSessionState::TimedOut;
    }

    bool stale_completion_and_cancellation_cannot_resurrect_session()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::WrongThenSuccess);
        auto server = makeServer(clock, observations, provider);
        advanceServerToAuthenticationInput(*server);
        server->handle(ServerAuthenticationSubmitted{ material() });
        const auto stale = server->handle(ServerPollAuthentication{});
        if (stale.action != ServerSessionAction::AuthenticationStaleCompletion
            || server->state() != ServerSessionState::AuthenticationPending || server->principal())
            return false;
        if (server->handle(ServerPollAuthentication{}).action != ServerSessionAction::SessionEstablished
            || !server->principal())
            return false;

        FakeAuthenticationProvider wrongGeneration(ProviderMode::WrongGenerationThenSuccess);
        auto generationChecked = makeServer(clock, observations, wrongGeneration);
        advanceServerToAuthenticationInput(*generationChecked);
        generationChecked->handle(ServerAuthenticationSubmitted{ material() });
        if (generationChecked->handle(ServerPollAuthentication{}).action
                != ServerSessionAction::AuthenticationStaleCompletion
            || generationChecked->state() != ServerSessionState::AuthenticationPending
            || generationChecked->handle(ServerPollAuthentication{}).action != ServerSessionAction::SessionEstablished)
            return false;

        FakeAuthenticationProvider cancellable(ProviderMode::DelayedSuccess);
        auto cancelled = makeServer(clock, observations, cancellable);
        advanceServerToAuthenticationInput(*cancelled);
        cancelled->handle(ServerAuthenticationSubmitted{ material() });
        const auto duplicate = cancelled->handle(ServerAuthenticationSubmitted{ material() });
        if (duplicate.accepted() || cancellable.control->begins != 1)
            return false;
        if (cancelled->handle(ServerCancel{}).action != ServerSessionAction::SessionCancelled
            || cancellable.control->cancellations != 1)
            return false;
        const auto delayed = cancelled->handle(ServerPollAuthentication{});
        cancelled->handle(ServerCancel{});
        if (delayed.accepted() || cancelled->state() != ServerSessionState::Cancelled || cancelled->principal()
            || cancellable.control->cancellations != 1)
            return false;

        FakeAuthenticationProvider closeable(ProviderMode::DelayedSuccess);
        auto closed = makeServer(clock, observations, closeable);
        advanceServerToAuthenticationInput(*closed);
        closed->handle(ServerAuthenticationSubmitted{ material() });
        if (closed->handle(ServerClose{}).action != ServerSessionAction::SessionClosed
            || closeable.control->cancellations != 1)
            return false;
        const auto afterClose = closed->handle(ServerPollAuthentication{});
        return !afterClose.accepted() && closed->state() == ServerSessionState::Closed && !closed->principal();
    }

    bool destruction_cancels_one_live_operation_once()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess);
        {
            auto server = makeServer(clock, observations, provider);
            advanceServerToAuthenticationInput(*server);
            server->handle(ServerAuthenticationSubmitted{ material() });
        }
        return provider.control->cancellations == 1;
    }

    bool illegal_and_terminal_transitions_preserve_state_and_identity()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        ObservationFixture observations;
        FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess);
        auto server = makeServer(clock, observations, provider);
        const auto beforeDeadline = server->deadline();
        const auto illegal = server->handle(ServerPollAuthentication{});
        if (illegal.accepted() || server->state() != ServerSessionState::AwaitingEncryptedTransport
            || server->deadline() != beforeDeadline || provider.control->begins != 0)
            return false;
        server->handle(ServerCancel{});
        if (server->handle(ServerCancel{}).action != ServerSessionAction::None
            || server->handle(ServerCheckTimeout{}).action != ServerSessionAction::None
            || server->state() != ServerSessionState::Cancelled)
            return false;
        if (server->handle(ServerClose{}).action != ServerSessionAction::SessionClosed
            || server->handle(ServerClose{}).action != ServerSessionAction::None)
            return false;

        auto client = makeClient(clock);
        const auto clientIllegal = client->handle(ClientAuthenticationAccepted{});
        if (clientIllegal.accepted() || client->state() != ClientSessionState::AwaitingEncryptedTransport)
            return false;
        client->handle(ClientCancel{});
        client->handle(ClientAuthenticationAccepted{});
        return client->state() == ClientSessionState::Cancelled && !client->negotiatedHello();
    }

    bool exhaustive_state_event_matrices_are_atomic()
    {
        constexpr std::array serverStates{ ServerSessionState::AwaitingEncryptedTransport,
            ServerSessionState::AwaitingClientHello, ServerSessionState::AwaitingAuthenticationInput,
            ServerSessionState::AuthenticationPending, ServerSessionState::Established, ServerSessionState::Rejected,
            ServerSessionState::TimedOut, ServerSessionState::Cancelled, ServerSessionState::Closed };
        constexpr std::array serverEvents{ ServerSessionEventKind::EncryptedTransportReady,
            ServerSessionEventKind::ClientHelloReceived, ServerSessionEventKind::AuthenticationSubmitted,
            ServerSessionEventKind::PollAuthentication, ServerSessionEventKind::CheckTimeout,
            ServerSessionEventKind::Cancel, ServerSessionEventKind::Close };
        for (const auto state : serverStates)
        {
            for (const auto event : serverEvents)
            {
                ManualClock clock(MonotonicInstant::fromNanoseconds(0));
                ObservationFixture observations;
                FakeAuthenticationProvider provider(ProviderMode::ImmediateSuccess);
                auto server = makeServer(clock, observations, provider);
                moveServerToState(*server, clock, state);
                const auto beforeState = server->state();
                const auto beforeDeadline = server->deadline();
                const auto beforeBegins = provider.control->begins;
                const bool legal = event == ServerSessionEventKind::CheckTimeout
                    || event == ServerSessionEventKind::Cancel || event == ServerSessionEventKind::Close
                    || (state == ServerSessionState::AwaitingEncryptedTransport
                        && event == ServerSessionEventKind::EncryptedTransportReady)
                    || (state == ServerSessionState::AwaitingClientHello
                        && event == ServerSessionEventKind::ClientHelloReceived)
                    || (state == ServerSessionState::AwaitingAuthenticationInput
                        && event == ServerSessionEventKind::AuthenticationSubmitted)
                    || (state == ServerSessionState::AuthenticationPending
                        && event == ServerSessionEventKind::PollAuthentication);
                const auto transition = server->handle(serverEvent(event));
                if (transition.accepted() != legal)
                    return false;
                if (!legal
                    && (transition.action != ServerSessionAction::None || server->state() != beforeState
                        || server->deadline() != beforeDeadline || provider.control->begins != beforeBegins))
                    return false;
            }
        }

        constexpr std::array clientStates{ ClientSessionState::AwaitingEncryptedTransport,
            ClientSessionState::AwaitingServerHello, ClientSessionState::AwaitingAuthenticationInput,
            ClientSessionState::AwaitingAuthenticationResult, ClientSessionState::Established,
            ClientSessionState::Rejected, ClientSessionState::TimedOut, ClientSessionState::Cancelled,
            ClientSessionState::Closed };
        constexpr std::array clientEvents{ ClientSessionEventKind::EncryptedTransportReady,
            ClientSessionEventKind::ServerHelloReceived, ClientSessionEventKind::SessionRejectedReceived,
            ClientSessionEventKind::AuthenticationSubmitted, ClientSessionEventKind::AuthenticationAccepted,
            ClientSessionEventKind::AuthenticationRejected, ClientSessionEventKind::CheckTimeout,
            ClientSessionEventKind::Cancel, ClientSessionEventKind::Close };
        for (const auto state : clientStates)
        {
            for (const auto event : clientEvents)
            {
                ManualClock clock(MonotonicInstant::fromNanoseconds(0));
                auto client = makeClient(clock);
                moveClientToState(*client, clock, state);
                const auto beforeState = client->state();
                const auto beforeDeadline = client->deadline();
                const bool legal = event == ClientSessionEventKind::CheckTimeout
                    || event == ClientSessionEventKind::Cancel || event == ClientSessionEventKind::Close
                    || (state == ClientSessionState::AwaitingEncryptedTransport
                        && event == ClientSessionEventKind::EncryptedTransportReady)
                    || (state == ClientSessionState::AwaitingServerHello
                        && (event == ClientSessionEventKind::ServerHelloReceived
                            || event == ClientSessionEventKind::SessionRejectedReceived))
                    || (state == ClientSessionState::AwaitingAuthenticationInput
                        && event == ClientSessionEventKind::AuthenticationSubmitted)
                    || (state == ClientSessionState::AwaitingAuthenticationResult
                        && (event == ClientSessionEventKind::AuthenticationAccepted
                            || event == ClientSessionEventKind::AuthenticationRejected));
                const auto transition = client->handle(clientEvent(event));
                if (transition.accepted() != legal)
                    return false;
                if (!legal
                    && (transition.action != ClientSessionAction::None || client->state() != beforeState
                        || client->deadline() != beforeDeadline))
                    return false;
            }
        }
        return true;
    }

    bool identical_event_order_reproduces_the_same_trace()
    {
        const auto run = [] {
            ManualClock clock(MonotonicInstant::fromNanoseconds(0));
            ObservationFixture observations;
            FakeAuthenticationProvider provider(ProviderMode::DelayedSuccess);
            auto server = makeServer(clock, observations, provider);
            std::vector<std::pair<ServerSessionState, ServerSessionAction>> trace;
            const auto record
                = [&](ServerSessionTransition transition) { trace.emplace_back(server->state(), transition.action); };
            record(server->handle(ServerEncryptedTransportReady{}));
            record(server->handle(ServerClientHelloReceived{ clientHello() }));
            record(server->handle(ServerAuthenticationSubmitted{ material() }));
            record(server->handle(ServerPollAuthentication{}));
            record(server->handle(ServerPollAuthentication{}));
            return trace;
        };
        return run() == run();
    }
}

int main()
{
    return bounded_secret_and_timeout_factories_fail_closed()
            && successful_client_server_path_is_ordered_owned_and_observable()
            && protocol_and_authentication_rejections_are_terminal()
            && provider_outcomes_and_exact_secret_bound_are_closed()
            && exact_deadlines_cancel_once_and_overflow_is_atomic()
            && every_server_and_client_stage_times_out_at_the_exact_deadline()
            && stale_completion_and_cancellation_cannot_resurrect_session()
            && destruction_cancels_one_live_operation_once()
            && illegal_and_terminal_transitions_preserve_state_and_identity()
            && exhaustive_state_event_matrices_are_atomic() && identical_event_order_reproduces_the_same_trace()
        ? 0
        : 1;
}
