#include <tes3mp/authentication.hpp>
#include <tes3mp/headless_client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <tes3mp/transport_gns.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <thread>
#include <variant>

namespace
{
    class SteadyClock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return TES3MP::MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(value));
        }
    };

    bool sendFrame(TES3MP::TransportRuntime& runtime, TES3MP::TransportConnectionId connection,
        TES3MP::MessageClass messageClass, TES3MP::MessageKind kind, std::span<const std::byte> payload)
    {
        auto frame = TES3MP::encodeProtocolFrame(messageClass, kind, payload);
        const auto* bytes = std::get_if<std::vector<std::byte>>(&frame);
        return bytes && runtime.send(connection, TES3MP::TransportChannel::ReliableOrdered, *bytes)
            == TES3MP::TransportResult::Accepted;
    }

    std::optional<std::uint64_t> number(std::string_view text)
    {
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
        return value;
    }
}

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 6)
    {
        std::cerr << "usage: tes3mp_headless_client <host> <port> <password-file> <timeout-ms> "
                     "[mover|observer|motion-one|motion-two]\n";
        return 2;
    }
    const auto port = number(argv[2]);
    const auto timeout = number(argv[4]);
    auto endpoint = port && *port <= 65535 ? TES3MP::ConnectionEndpoint::create(argv[1], static_cast<std::uint16_t>(*port)) : std::nullopt;
    if (!endpoint || !timeout || *timeout == 0 || *timeout > 60'000) return 2;

    std::ifstream passwordStream(argv[3], std::ios::binary);
    std::vector<std::byte> passwordBytes;
    char byte = 0;
    while (passwordStream.get(byte) && passwordBytes.size() <= TES3MP::MaximumAuthenticationMaterialBytes)
        passwordBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    if (!passwordBytes.empty() && passwordBytes.back() == std::byte{'\n'}) passwordBytes.pop_back();
    if (!passwordBytes.empty() && passwordBytes.back() == std::byte{'\r'}) passwordBytes.pop_back();
    auto password = TES3MP::AuthenticationMaterial::create(passwordBytes);
    std::fill(passwordBytes.begin(), passwordBytes.end(), std::byte{});
    if (!passwordStream.eof() || !password) return 2;

    auto limits = TES3MP::TransportLimits::create(1, 1, 1, 32);
    auto factory = limits ? TES3MP::makeGameNetworkingSocketsTransport(*limits) : TES3MP::TransportFactoryResult{};
    SteadyClock clock;
    auto timeouts = TES3MP::SessionTimeoutPolicy::create(*timeout * 1'000'000, *timeout * 1'000'000, *timeout * 1'000'000);
    auto created = factory && timeouts ? TES3MP::HeadlessClientSession::create(
        *factory.runtime, clock, *timeouts, TES3MP::SessionGeneration::initial()) : TES3MP::HeadlessClientCreateResult{TES3MP::SessionTransitionError{}};
    auto* sessionValue = std::get_if<std::unique_ptr<TES3MP::HeadlessClientSession>>(&created);
    if (!sessionValue || !*sessionValue || (*sessionValue)->connect(*endpoint) != TES3MP::HeadlessClientResult::Accepted) return 3;
    auto& session = **sessionValue;
    bool authenticationAccepted = false;
    const std::string_view mode = argc == 6 ? argv[5] : "join";
    if (mode != "join" && mode != "mover" && mode != "observer"
        && mode != "motion-one" && mode != "motion-two") return 2;
    std::optional<TES3MP::LatestWinsSnapshot> snapshot;
    bool bound = false;
    bool sentExterior = false;
    bool sentInterior = false;
    bool sawLeave = false;
    bool sawEnter = false;
    bool sentMotion = false;
    bool sawConvergedMovement = false;
    bool rejectedStaleView = false;
    std::optional<TES3MP::LatestWinsSnapshot> staleSnapshot;
    std::optional<std::chrono::steady_clock::time_point> motionNotBefore;
    std::optional<TES3MP::ReliableObservationBatch> pendingObservation;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(*timeout);

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto pumped = session.pump();
        if (pumped.result != TES3MP::HeadlessClientResult::Accepted) break;
        if (pumped.action == TES3MP::ClientSessionAction::SendClientHello)
        {
            auto range = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 0));
            auto offer = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(std::move(range), {}, {}));
            const auto payload = TES3MP::encodeClientHello(TES3MP::ClientHello::fromOffer(std::move(offer)));
            if (!session.connection() || !sendFrame(*factory.runtime, *session.connection(), TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload)) break;
        }
        if (session.connection())
        {
            std::array<TES3MP::TransportMessage, 8> messages{};
            const auto received = factory.runtime->receive(*session.connection(), messages);
            if (received.result != TES3MP::TransportResult::Accepted || received.messages > messages.size()) break;
            for (std::size_t index = 0; index < received.messages; ++index)
            {
                auto decoded = TES3MP::decodeProtocolFrame(messages[index].bytes);
                auto* frame = std::get_if<TES3MP::DecodedFrame>(&decoded);
                if (!frame) return 3;
                if (frame->messageKind() == TES3MP::MessageKind::ServerHello)
                {
                    auto hello = TES3MP::decodeServerHello(frame->payload());
                    auto* value = std::get_if<TES3MP::ServerHello>(&hello);
                    if (!value || session.handle(TES3MP::ClientServerHelloReceived{std::move(*value)}).action
                            != TES3MP::ClientSessionAction::AuthenticationInputReady) return 3;
                    auto request = TES3MP::AuthenticationRequest::join(std::move(*password));
                    const auto payload = TES3MP::encodeAuthenticationRequest(request);
                    if (!sendFrame(*factory.runtime, *session.connection(), TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::AuthenticationRequest, payload)
                        || session.handle(TES3MP::ClientAuthenticationSubmitted{}).action
                            != TES3MP::ClientSessionAction::AuthenticationSubmitted) return 3;
                }
                else if (frame->messageKind() == TES3MP::MessageKind::AuthenticationAccepted)
                {
                    auto accepted = TES3MP::decodeAuthenticationAccepted(frame->payload());
                    if (!std::holds_alternative<TES3MP::AuthenticationAcceptedMessage>(accepted)
                        || session.handle(TES3MP::ClientAuthenticationAccepted{}).action
                            != TES3MP::ClientSessionAction::SessionEstablished) return 3;
                    authenticationAccepted = true;
                }
                else if (frame->messageKind() == TES3MP::MessageKind::LatestWinsSnapshot)
                {
                    auto value = TES3MP::decodeLatestWinsSnapshot(frame->payload());
                    if (!std::holds_alternative<TES3MP::LatestWinsSnapshot>(value)) return 3;
                    snapshot = std::get<TES3MP::LatestWinsSnapshot>(std::move(value));
                }
                else if (frame->messageKind() == TES3MP::MessageKind::ReliableObservationBatch)
                {
                    auto value = TES3MP::decodeReliableObservationBatch(frame->payload());
                    auto* batch = std::get_if<TES3MP::ReliableObservationBatch>(&value);
                    if (!batch) return 3;
                    if (mode == "motion-one" || mode == "motion-two") continue;
                    if (!bound) pendingObservation = std::move(*batch);
                    else
                    {
                        const auto applied = session.receiveReliableObservationBatch(std::move(*batch));
                        if (applied != TES3MP::ReliableObservationReceiveResult::Applied)
                        { std::cerr << "observation rejected " << static_cast<int>(applied) << '\n'; return 3; }
                    }
                    if (!bound) continue;
                    for (const auto& change : session.stateMachine().confirmedObservationBatch()->changes())
                    {
                        sawLeave = sawLeave || change.kind == TES3MP::ObservationChangeKind::Leave;
                        sawEnter = sawEnter || (sawLeave && change.kind == TES3MP::ObservationChangeKind::Enter);
                    }
                }
            }
        }
        if (authenticationAccepted && snapshot && !bound)
        {
            if ((mode == "motion-one" || mode == "motion-two")
                && snapshot->view().entries().size() != 2)
            {
                snapshot.reset();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const auto sessionId = snapshot->header().targetSessionId();
            if (session.bindEstablishedSession(sessionId) != TES3MP::ClientSessionBindingResult::Bound
                || session.receiveLatestWinsSnapshot(std::move(*snapshot)) != TES3MP::LatestWinsSnapshotReceiveResult::Applied)
                return 3;
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            const auto foundSelf = std::ranges::find_if(confirmed.view().entries(), [&](const auto& entry) {
                return entry.playerId().value() == sessionId.value();
            });
            if (foundSelf == confirmed.view().entries().end()) return 3;
            const auto& entry = *foundSelf;
            std::cout << "{\"event\":\"joined\",\"session_id\":" << sessionId.value()
                      << ",\"player_id\":" << entry.playerId().value()
                      << ",\"entity_id\":" << entry.entityId().value() << "}" << std::endl;
            bound = true;
            motionNotBefore = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            snapshot.reset();
            if (pendingObservation)
            {
                if (session.receiveReliableObservationBatch(std::move(*pendingObservation))
                    != TES3MP::ReliableObservationReceiveResult::Applied) return 3;
                pendingObservation.reset();
            }
            if (mode == "join") { session.close(); factory.runtime->shutdown(); return 0; }
        }
        else if (bound && snapshot)
        {
            if (!staleSnapshot)
                staleSnapshot = *session.stateMachine().confirmedSnapshot();
            const auto result = session.receiveLatestWinsSnapshot(std::move(*snapshot));
            if (result != TES3MP::LatestWinsSnapshotReceiveResult::Applied
                && result != TES3MP::LatestWinsSnapshotReceiveResult::IdenticalDuplicate)
            { std::cerr << "snapshot rejected " << static_cast<int>(result) << '\n'; return 3; }
            snapshot.reset();
        }
        if (bound && mode == "mover" && session.connection())
        {
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            const auto self = std::ranges::find_if(confirmed.view().entries(), [&](const auto& entry) {
                return entry.playerId().value() == confirmed.header().targetSessionId().value(); });
            if (self != confirmed.view().entries().end())
            {
                const bool exterior = self->transform().cell().kind() == TES3MP::CellId::Kind::Exterior;
                if (!sentExterior || (exterior && !sentInterior))
                {
                    const auto sequence = TES3MP::CommandSequence::fromValue(sentExterior ? 2 : 1).value();
                    const auto commandId = TES3MP::CommandId::fromValue(sentExterior ? 2 : 1).value();
                    const auto cell = sentExterior
                        ? TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(7).value())
                        : TES3MP::CellId::exterior(TES3MP::CellSpaceId::fromValue(8).value(), 0, 0);
                    TES3MP::ClientCommandHeader header(confirmed.header().targetSessionId(),
                        confirmed.header().targetSessionGeneration(), sequence, commandId,
                        confirmed.header().serverTick());
                    TES3MP::ReliableOperationHeader reliable(header, TES3MP::EntityPrecondition(
                        self->entityId(), self->entityRevision(), self->authorityEpoch()));
                    auto operation = TES3MP::ReliableOperation::create(reliable, TES3MP::FixtureCellTransition(cell));
                    if (!std::holds_alternative<TES3MP::ReliableOperation>(operation)
                        || !sendFrame(*factory.runtime, *session.connection(), TES3MP::MessageClass::ReliableOperation,
                            TES3MP::MessageKind::ReliableOperation,
                            TES3MP::encodeReliableOperation(std::get<TES3MP::ReliableOperation>(operation)))) return 3;
                    if (!sentExterior) sentExterior = true; else sentInterior = true;
                }
            }
        }
        if (bound && (mode == "motion-one" || mode == "motion-two")
            && !sentMotion && session.connection() && motionNotBefore
            && std::chrono::steady_clock::now() >= *motionNotBefore)
        {
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            if (confirmed.view().entries().size() != 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (mode == "motion-two" && std::ranges::none_of(confirmed.view().entries(), [](const auto& entry) {
                    return entry.playerId().value() == 1 && entry.linearVelocity().x() == 1; }))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const auto self = std::ranges::find_if(confirmed.view().entries(), [&](const auto& entry) {
                return entry.playerId().value() == confirmed.header().targetSessionId().value(); });
            if (self == confirmed.view().entries().end()) return 3;
            const auto sequenceValue = 1;
            TES3MP::ClientCommandHeader header(confirmed.header().targetSessionId(),
                confirmed.header().targetSessionGeneration(), TES3MP::CommandSequence::fromValue(sequenceValue).value(),
                TES3MP::CommandId::fromValue(sequenceValue).value(), confirmed.header().serverTick());
            TES3MP::ReliableOperationHeader reliable(header, TES3MP::EntityPrecondition(
                self->entityId(), self->entityRevision(), self->authorityEpoch()));
            auto operation = TES3MP::ReliableOperation::create(reliable,
                TES3MP::PlayerMotionIntent(TES3MP::LinearVelocity3(mode == "motion-two" ? 2 : 1, 0, 0)));
            if (!std::holds_alternative<TES3MP::ReliableOperation>(operation)
                || !sendFrame(*factory.runtime, *session.connection(), TES3MP::MessageClass::ReliableOperation,
                    TES3MP::MessageKind::ReliableOperation,
                    TES3MP::encodeReliableOperation(std::get<TES3MP::ReliableOperation>(operation)))) return 3;
            sentMotion = true;
        }
        if (sentMotion)
        {
            const auto& entries = session.stateMachine().confirmedSnapshot()->view().entries();
            bool sawOne = false;
            bool sawTwo = false;
            for (const auto& entry : entries)
            {
                sawOne = sawOne || (entry.playerId().value() == 1 && entry.linearVelocity().x() == 1
                    && entry.transform().position().x() > 10);
                sawTwo = sawTwo || (entry.playerId().value() == 2 && entry.linearVelocity().x() == 2
                    && entry.transform().position().x() > 10);
            }
            sawConvergedMovement = sawOne && sawTwo;
            if (sawConvergedMovement && staleSnapshot)
            {
                rejectedStaleView = session.receiveLatestWinsSnapshot(std::move(*staleSnapshot))
                    == TES3MP::LatestWinsSnapshotReceiveResult::StaleTick;
                staleSnapshot.reset();
            }
        }
        if (sawConvergedMovement && rejectedStaleView)
        {
            std::cout << "{\"event\":\"movement_flow_complete\",\"role\":\"" << mode
                      << "\",\"stale_view_rejected\":true}\n";
            session.close(); factory.runtime->shutdown(); return 0;
        }
        if (bound && sawLeave && sawEnter && (mode == "observer" || sentInterior))
        {
            std::cout << "{\"event\":\"fixture_flow_complete\",\"role\":\"" << mode << "\"}\n";
            session.close(); factory.runtime->shutdown(); return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.close();
    factory.runtime->shutdown();
    if (bound && session.stateMachine().confirmedSnapshot())
    {
        for (const auto& entry : session.stateMachine().confirmedSnapshot()->view().entries())
            std::cerr << "player=" << entry.playerId().value() << " x=" << entry.transform().position().x()
                      << " vx=" << entry.linearVelocity().x() << '\n';
    }
    std::cerr << "join failed\n";
    return 3;
}
