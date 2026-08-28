#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_exchange.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/phase4_in_memory_peer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    template <class Value>
    Value value(std::uint64_t raw)
    {
        return *Value::fromValue(raw);
    }

    SessionTimeoutPolicy timeoutPolicy()
    {
        return *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
    }

    ProtocolVersionRange versions()
    {
        return std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 1));
    }

    CapabilityOffer offer()
    {
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions(), {}, {}));
    }

    ReliableOperation operation(SessionId sessionId, SessionGeneration generation, std::uint64_t commandSequence = 1)
    {
        const ReliableOperationHeader header(
            ClientCommandHeader(sessionId, generation, value<CommandSequence>(commandSequence), value<CommandId>(31),
                value<ServerTick>(4)),
            EntityPrecondition(value<EntityId>(41), value<EntityRevision>(3), value<AuthorityEpoch>(2)));
        return std::get<ReliableOperation>(
            ReliableOperation::create(header, PlayerMotionIntent(LinearVelocity3(100, -200, 300))));
    }

    SpatialEntitySnapshot entry(std::uint64_t entityId, std::uint64_t tick = 8)
    {
        return SpatialEntitySnapshot(value<ServerTick>(tick), value<EntityId>(entityId), value<EntityRevision>(3),
            value<AuthorityEpoch>(2),
            Transform(CellId::exterior(value<CellSpaceId>(51), -2, 7), Position3(1000, -2000, 3000),
                Orientation3(Turn32::fromValue(11), Turn32::fromValue(12), Turn32::fromValue(13))),
            LinearVelocity3(14, 15, -16));
    }

    SpatialWorldView view(std::span<const SpatialEntitySnapshot> entries)
    {
        return std::get<SpatialWorldView>(SpatialWorldView::create(entries));
    }

    LatestWinsSnapshot snapshot(SessionId sessionId, SessionGeneration generation, std::uint64_t tick,
        std::optional<std::uint64_t> acknowledgement, std::span<const SpatialEntitySnapshot> entries)
    {
        std::optional<CommandSequence> acknowledged;
        if (acknowledgement)
            acknowledged = value<CommandSequence>(*acknowledgement);
        return LatestWinsSnapshot(
            LatestWinsSnapshotHeader(sessionId, generation, value<ServerTick>(tick), acknowledged), view(entries));
    }

    bool hasError(const auto& result, ExchangeDecodeErrorCode code)
    {
        const auto* failure = std::get_if<ExchangeDecodeError>(&result);
        return failure != nullptr && failure->code == code;
    }

    std::unique_ptr<ClientSessionStateMachine> establishedClient(
        ManualClock& clock, SessionGeneration generation, SessionId sessionId)
    {
        auto created = ClientSessionStateMachine::create(clock, timeoutPolicy(), generation);
        auto client = std::get<std::unique_ptr<ClientSessionStateMachine>>(std::move(created));
        client->handle(ClientEncryptedTransportReady{});
        const ClientHello clientHello = ClientHello::fromOffer(offer());
        auto negotiated = TES3MP::negotiateClientHello(clientHello, offer());
        client->handle(ClientServerHelloReceived{ std::get<ServerHello>(std::move(negotiated)) });
        client->handle(ClientAuthenticationSubmitted{});
        client->handle(ClientAuthenticationAccepted{});
        if (client->bindEstablishedSession(sessionId) != ClientSessionBindingResult::Bound)
            return nullptr;
        return client;
    }

    bool values_are_typed_bounded_and_not_default_constructible()
    {
        static_assert(!std::is_default_constructible_v<PlayerMotionIntent>);
        static_assert(!std::is_default_constructible_v<ReliableOperation>);
        static_assert(!std::is_default_constructible_v<SpatialWorldView>);
        static_assert(!std::is_default_constructible_v<LatestWinsSnapshot>);
        const auto missing = ReliableOperation::create(
            ReliableOperationHeader(ClientCommandHeader(value<SessionId>(1), SessionGeneration::initial(),
                                        CommandSequence::initial(), value<CommandId>(1), ServerTick::initial()),
                std::nullopt),
            PlayerMotionIntent(LinearVelocity3(0, 0, 0)));
        return MaximumSpatialWorldViewEntries == 256
            && hasError(missing, ExchangeDecodeErrorCode::MissingEntityPrecondition);
    }

    bool operation_and_snapshot_round_trip_as_owned_values()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const auto originalOperation = operation(sessionId, generation);
        const std::array entries{ entry(41), entry(42) };
        const auto originalSnapshot = snapshot(sessionId, generation, 9, 1, entries);
        auto operationBytes = encodeReliableOperation(originalOperation);
        auto snapshotBytes = encodeLatestWinsSnapshot(originalSnapshot);
        auto decodedOperation = decodeReliableOperation(operationBytes);
        auto decodedSnapshot = decodeLatestWinsSnapshot(snapshotBytes);
        std::fill(operationBytes.begin(), operationBytes.end(), std::byte{ 0 });
        std::fill(snapshotBytes.begin(), snapshotBytes.end(), std::byte{ 0 });
        const auto* ownedOperation = std::get_if<ReliableOperation>(&decodedOperation);
        const auto* ownedSnapshot = std::get_if<LatestWinsSnapshot>(&decodedSnapshot);
        return ownedOperation != nullptr && *ownedOperation == originalOperation && ownedSnapshot != nullptr
            && *ownedSnapshot == originalSnapshot;
    }

    bool deterministic_exchange_properties_round_trip()
    {
        const auto sessionId = value<SessionId>(1);
        const auto generation = SessionGeneration::initial();
        constexpr std::array signedSamples{ std::numeric_limits<std::int64_t>::min(), std::int64_t{ -1 },
            std::int64_t{ 0 }, std::int64_t{ 1 }, std::numeric_limits<std::int64_t>::max() };
        for (std::size_t index = 0; index < signedSamples.size(); ++index)
        {
            const auto raw = static_cast<std::uint64_t>(index + 1);
            const ReliableOperationHeader header(ClientCommandHeader(sessionId, generation, value<CommandSequence>(raw),
                                                     value<CommandId>(raw), *ServerTick::fromValue(raw - 1)),
                EntityPrecondition(value<EntityId>(raw), value<EntityRevision>(raw), value<AuthorityEpoch>(raw)));
            const auto created = ReliableOperation::create(header,
                PlayerMotionIntent(
                    LinearVelocity3(signedSamples[index], signedSamples[(index + 1) % signedSamples.size()],
                        signedSamples[(index + 2) % signedSamples.size()])));
            const auto* original = std::get_if<ReliableOperation>(&created);
            if (original == nullptr)
                return false;
            const auto decoded = decodeReliableOperation(encodeReliableOperation(*original));
            const auto* roundTripped = std::get_if<ReliableOperation>(&decoded);
            if (roundTripped == nullptr || *roundTripped != *original)
                return false;
        }

        constexpr std::array entryCounts{ std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 2 },
            MaximumSpatialWorldViewEntries };
        for (const auto count : entryCounts)
        {
            std::vector<SpatialEntitySnapshot> entries;
            entries.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto raw = static_cast<std::uint64_t>(index + 1);
                const auto signedValue = signedSamples[index % signedSamples.size()];
                const auto nextSignedValue = signedSamples[(index + 1) % signedSamples.size()];
                const auto lastSignedValue = signedSamples[(index + 2) % signedSamples.size()];
                const auto cell = index % 2 == 0
                    ? CellId::interior(value<CellSpaceId>(raw))
                    : CellId::exterior(
                          value<CellSpaceId>(raw), static_cast<std::int32_t>(index), -static_cast<std::int32_t>(index));
                entries.emplace_back(*ServerTick::fromValue(raw - 1), value<EntityId>(raw), value<EntityRevision>(raw),
                    value<AuthorityEpoch>(raw),
                    Transform(cell, Position3(signedValue, nextSignedValue, lastSignedValue),
                        Orientation3(Turn32::fromValue(static_cast<std::uint32_t>(index)),
                            Turn32::fromValue(static_cast<std::uint32_t>(index + 1)),
                            Turn32::fromValue(
                                std::numeric_limits<std::uint32_t>::max() - static_cast<std::uint32_t>(index)))),
                    LinearVelocity3(lastSignedValue, signedValue, nextSignedValue));
            }
            const auto createdView = SpatialWorldView::create(entries);
            const auto* originalView = std::get_if<SpatialWorldView>(&createdView);
            if (originalView == nullptr)
                return false;
            std::optional<CommandSequence> acknowledgement;
            if (count != 0)
                acknowledgement = value<CommandSequence>(count);
            const LatestWinsSnapshot original(
                LatestWinsSnapshotHeader(
                    sessionId, generation, *ServerTick::fromValue(static_cast<std::uint64_t>(count)), acknowledgement),
                *originalView);
            const auto decoded = decodeLatestWinsSnapshot(encodeLatestWinsSnapshot(original));
            const auto* roundTripped = std::get_if<LatestWinsSnapshot>(&decoded);
            if (roundTripped == nullptr || *roundTripped != original)
                return false;
        }
        return true;
    }

    bool view_bounds_ordering_and_payload_budget_are_enforced()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const auto empty = SpatialWorldView::create({});
        std::vector<SpatialEntitySnapshot> maximum;
        maximum.reserve(MaximumSpatialWorldViewEntries + 1);
        for (std::size_t index = 0; index < MaximumSpatialWorldViewEntries + 1; ++index)
            maximum.push_back(entry(index + 1));
        const auto accepted = SpatialWorldView::create(std::span(maximum).first(MaximumSpatialWorldViewEntries));
        const auto oversized = SpatialWorldView::create(maximum);
        const std::array duplicate{ entry(1), entry(1) };
        const std::array unsorted{ entry(2), entry(1) };
        if (!std::holds_alternative<SpatialWorldView>(empty) || !std::holds_alternative<SpatialWorldView>(accepted)
            || !hasError(oversized, ExchangeDecodeErrorCode::TooManySnapshotEntries)
            || !hasError(SpatialWorldView::create(duplicate), ExchangeDecodeErrorCode::SnapshotEntriesNotStrictlySorted)
            || !hasError(SpatialWorldView::create(unsorted), ExchangeDecodeErrorCode::SnapshotEntriesNotStrictlySorted))
            return false;
        const LatestWinsSnapshot maximumSnapshot(
            LatestWinsSnapshotHeader(sessionId, generation, value<ServerTick>(9), std::nullopt),
            std::get<SpatialWorldView>(std::move(accepted)));
        return encodeLatestWinsSnapshot(maximumSnapshot).size() <= LatestWinsSnapshotMaximumPayloadBytes;
    }

    bool every_truncation_identifier_and_trailing_byte_fail_without_partial_value()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        const std::array payloads{ encodeReliableOperation(operation(sessionId, generation)),
            encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, std::nullopt, entries)) };
        for (std::size_t payloadIndex = 0; payloadIndex < payloads.size(); ++payloadIndex)
        {
            const auto fails = [payloadIndex](std::span<const std::byte> bytes) {
                return payloadIndex == 0 ? std::holds_alternative<ExchangeDecodeError>(decodeReliableOperation(bytes))
                                         : std::holds_alternative<ExchangeDecodeError>(decodeLatestWinsSnapshot(bytes));
            };
            for (std::size_t size = 0; size < payloads[payloadIndex].size(); ++size)
            {
                if (!fails(std::span(payloads[payloadIndex]).first(size)))
                    return false;
            }
            auto wrongIdentifier = payloads[payloadIndex];
            wrongIdentifier[8] ^= std::byte{ 0xff };
            if (!fails(wrongIdentifier))
                return false;
            auto trailing = payloads[payloadIndex];
            trailing.push_back(std::byte{ 0 });
            if (!fails(trailing))
                return false;
        }
        return true;
    }

    bool closed_body_and_strong_value_mutations_fail_semantically()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        auto reliable = encodeReliableOperation(operation(sessionId, generation));
        auto latestWins = encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries));
        if (reliable.size() != 184 || latestWins.size() != 240)
            return false;

        auto unknownReliableBody = reliable;
        unknownReliableBody[31] = std::byte{ 2 };
        auto unknownSnapshotBody = latestWins;
        unknownSnapshotBody[31] = std::byte{ 2 };

        const std::array entityPattern{ std::byte{ 41 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 },
            std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } };
        const auto entityPosition
            = std::search(latestWins.begin(), latestWins.end(), entityPattern.begin(), entityPattern.end());
        if (entityPosition == latestWins.end())
            return false;
        std::fill(entityPosition, entityPosition + entityPattern.size(), std::byte{ 0 });

        const auto reliableResult = decodeReliableOperation(unknownReliableBody);
        const auto snapshotBodyResult = decodeLatestWinsSnapshot(unknownSnapshotBody);
        const auto strongValueResult = decodeLatestWinsSnapshot(latestWins);
        const bool reliableFailed = hasError(reliableResult, ExchangeDecodeErrorCode::UnknownBody);
        const bool snapshotBodyFailed = hasError(snapshotBodyResult, ExchangeDecodeErrorCode::UnknownBody)
            || hasError(snapshotBodyResult, ExchangeDecodeErrorCode::VerificationFailed);
        const bool strongValueFailed = hasError(strongValueResult, ExchangeDecodeErrorCode::InvalidStrongValue);
        return reliableFailed && snapshotBodyFailed && strongValueFailed;
    }

    bool every_single_bit_mutation_is_rejected_or_normalizes_to_an_owned_value()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        const std::array payloads{ encodeReliableOperation(operation(sessionId, generation)),
            encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries)) };
        for (std::size_t payloadIndex = 0; payloadIndex < payloads.size(); ++payloadIndex)
        {
            for (std::size_t index = 0; index < payloads[payloadIndex].size(); ++index)
            {
                for (unsigned bit = 0; bit < 8; ++bit)
                {
                    auto mutated = payloads[payloadIndex];
                    mutated[index] ^= static_cast<std::byte>(1u << bit);
                    if (payloadIndex == 0)
                    {
                        const auto decoded = decodeReliableOperation(mutated);
                        if (const auto* current = std::get_if<ReliableOperation>(&decoded))
                        {
                            const auto normalized = decodeReliableOperation(encodeReliableOperation(*current));
                            const auto* value = std::get_if<ReliableOperation>(&normalized);
                            if (value == nullptr || *value != *current)
                                return false;
                        }
                    }
                    else
                    {
                        const auto decoded = decodeLatestWinsSnapshot(mutated);
                        if (const auto* current = std::get_if<LatestWinsSnapshot>(&decoded))
                        {
                            const auto normalized = decodeLatestWinsSnapshot(encodeLatestWinsSnapshot(*current));
                            const auto* value = std::get_if<LatestWinsSnapshot>(&normalized);
                            if (value == nullptr || *value != *current)
                                return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    bool client_snapshot_guard_is_atomic_and_monotonic()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        auto client = establishedClient(clock, generation, sessionId);
        if (!client)
            return false;
        const std::array firstEntries{ entry(41, 8) };
        const auto first = snapshot(sessionId, generation, 9, 2, firstEntries);
        if (client->receiveLatestWinsSnapshot(first) != LatestWinsSnapshotReceiveResult::Applied)
            return false;
        const auto confirmed = *client->confirmedSnapshot();
        if (client->receiveLatestWinsSnapshot(first) != LatestWinsSnapshotReceiveResult::IdenticalDuplicate)
            return false;

        const std::array changedEntries{ entry(42, 8) };
        const auto contradictory = snapshot(sessionId, generation, 9, 2, changedEntries);
        const auto stale = snapshot(sessionId, generation, 8, 2, firstEntries);
        const auto regressingAck = snapshot(sessionId, generation, 10, 1, firstEntries);
        const auto wrongSession = snapshot(value<SessionId>(22), generation, 10, 2, firstEntries);
        const auto wrongGeneration = snapshot(sessionId, value<SessionGeneration>(3), 10, 2, firstEntries);
        return client->receiveLatestWinsSnapshot(contradictory)
            == LatestWinsSnapshotReceiveResult::ContradictorySameTick
            && client->receiveLatestWinsSnapshot(stale) == LatestWinsSnapshotReceiveResult::StaleTick
            && client->receiveLatestWinsSnapshot(regressingAck)
            == LatestWinsSnapshotReceiveResult::RegressingAcknowledgement
            && client->receiveLatestWinsSnapshot(wrongSession) == LatestWinsSnapshotReceiveResult::SessionMismatch
            && client->receiveLatestWinsSnapshot(wrongGeneration) == LatestWinsSnapshotReceiveResult::GenerationMismatch
            && *client->confirmedSnapshot() == confirmed;
    }

    bool fake_peer_negotiates_authenticates_and_exchanges_framed_state_in_memory()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        auto created = Phase4InMemoryPeer::create(sessionId, generation, value<PrincipalId>(71));
        auto* peer = std::get_if<std::unique_ptr<Phase4InMemoryPeer>>(&created);
        if (peer == nullptr)
            return false;
        const std::array entries{ entry(41), entry(42) };
        const auto sentOperation = operation(sessionId, generation);
        const auto sentSnapshot = snapshot(sessionId, generation, 9, 1, entries);
        if ((*peer)->exchange(sentOperation, sentSnapshot) != Phase4PeerError::None)
            return false;
        constexpr std::array expectedTrace{ Phase4PeerTraceStep::ClientHelloSent,
            Phase4PeerTraceStep::ServerHelloAccepted, Phase4PeerTraceStep::AuthenticationSucceeded,
            Phase4PeerTraceStep::SessionBound, Phase4PeerTraceStep::ReliableOperationDelivered,
            Phase4PeerTraceStep::LatestWinsSnapshotApplied };
        return std::ranges::equal((*peer)->trace(), expectedTrace) && (*peer)->deliveredOperation()
            && *(*peer)->deliveredOperation() == sentOperation && (*peer)->confirmedSnapshot()
            && *(*peer)->confirmedSnapshot() == sentSnapshot;
    }

    bool old_generation_operation_is_not_delivered()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        auto created = Phase4InMemoryPeer::create(sessionId, generation, value<PrincipalId>(71));
        auto* peer = std::get_if<std::unique_ptr<Phase4InMemoryPeer>>(&created);
        if (peer == nullptr)
            return false;
        const std::array entries{ entry(41) };
        const auto result = (*peer)->exchange(operation(sessionId, value<SessionGeneration>(3)),
            snapshot(sessionId, generation, 9, std::nullopt, entries));
        return result == Phase4PeerError::ProtocolFailure && !(*peer)->deliveredOperation()
            && (*peer)->trace().size() == 4;
    }

    bool two_sessions_may_receive_different_views_of_one_canonical_tick()
    {
        const auto generation = value<SessionGeneration>(2);
        const auto firstSession = value<SessionId>(21);
        const auto secondSession = value<SessionId>(22);
        auto firstCreated = Phase4InMemoryPeer::create(firstSession, generation, value<PrincipalId>(71));
        auto secondCreated = Phase4InMemoryPeer::create(secondSession, generation, value<PrincipalId>(72));
        auto* first = std::get_if<std::unique_ptr<Phase4InMemoryPeer>>(&firstCreated);
        auto* second = std::get_if<std::unique_ptr<Phase4InMemoryPeer>>(&secondCreated);
        if (first == nullptr || second == nullptr)
            return false;
        const std::array firstView{ entry(41) };
        const std::array secondView{ entry(41), entry(42) };
        return (*first)->exchange(
                   operation(firstSession, generation), snapshot(firstSession, generation, 9, std::nullopt, firstView))
            == Phase4PeerError::None
            && (*second)->exchange(operation(secondSession, generation),
                   snapshot(secondSession, generation, 9, std::nullopt, secondView))
            == Phase4PeerError::None
            && (*first)->confirmedSnapshot()->view().entries().size() == 1
            && (*second)->confirmedSnapshot()->view().entries().size() == 2;
    }

    std::uint64_t fnv1a(std::span<const std::byte> bytes)
    {
        std::uint64_t result = 14695981039346656037ull;
        for (const auto byte : bytes)
        {
            result ^= std::to_integer<std::uint8_t>(byte);
            result *= 1099511628211ull;
        }
        return result;
    }

    bool complete_roots_have_exact_golden_payloads()
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        const auto reliable = encodeReliableOperation(operation(sessionId, generation));
        const auto latestWins = encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries));
        return reliable.size() == 184 && fnv1a(reliable) == 0x1ad648997dba1cecull && latestWins.size() == 240
            && fnv1a(latestWins) == 0x9237b44a62e49c50ull;
    }

    void printBytes(std::span<const std::byte> bytes)
    {
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (index != 0)
                std::cout << ',';
            std::cout << static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[index]));
        }
        std::cout << '\n';
    }

    bool writeFile(const std::filesystem::path& path, std::span<const std::byte> value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
        return stream.good();
    }

    bool writeCorpus(const std::filesystem::path& directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        const auto reliable = encodeReliableOperation(operation(sessionId, generation));
        const auto latestWins = encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries));
        return writeFile(directory / "valid-reliable-operation", reliable)
            && writeFile(directory / "valid-latest-wins-snapshot", latestWins);
    }

    std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::nullopt;
        std::vector<std::byte> value(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
        if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            return std::nullopt;
        return value;
    }

    bool verifyCorpus(const std::filesystem::path& directory)
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        const auto reliable = readFile(directory / "valid-reliable-operation");
        const auto latestWins = readFile(directory / "valid-latest-wins-snapshot");
        return reliable && *reliable == encodeReliableOperation(operation(sessionId, generation))
            && std::holds_alternative<ReliableOperation>(decodeReliableOperation(*reliable)) && latestWins
            && *latestWins == encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries))
            && std::holds_alternative<LatestWinsSnapshot>(decodeLatestWinsSnapshot(*latestWins));
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--print-golden")
    {
        const auto sessionId = value<SessionId>(21);
        const auto generation = value<SessionGeneration>(2);
        const std::array entries{ entry(41) };
        printBytes(encodeReliableOperation(operation(sessionId, generation)));
        printBytes(encodeLatestWinsSnapshot(snapshot(sessionId, generation, 9, 1, entries)));
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        return writeCorpus(argv[2]) ? 0 : 1;
    if (argc == 3 && std::string_view(argv[1]) == "--verify-corpus")
        return verifyCorpus(argv[2]) ? 0 : 1;

    const auto check = [](bool passed, std::string_view name) {
        if (!passed)
            std::cerr << "failed: " << name << '\n';
        return passed;
    };
    bool passed = true;
    passed &= check(values_are_typed_bounded_and_not_default_constructible(), "typed bounded values");
    passed &= check(operation_and_snapshot_round_trip_as_owned_values(), "owned round trips");
    passed &= check(deterministic_exchange_properties_round_trip(), "deterministic properties");
    passed &= check(view_bounds_ordering_and_payload_budget_are_enforced(), "view bounds and ordering");
    passed &= check(every_truncation_identifier_and_trailing_byte_fail_without_partial_value(), "malformed inputs");
    passed &= check(closed_body_and_strong_value_mutations_fail_semantically(), "semantic mutations");
    passed &= check(every_single_bit_mutation_is_rejected_or_normalizes_to_an_owned_value(), "bit mutations");
    passed &= check(client_snapshot_guard_is_atomic_and_monotonic(), "client snapshot guard");
    passed &= check(fake_peer_negotiates_authenticates_and_exchanges_framed_state_in_memory(), "fake peer exchange");
    passed &= check(old_generation_operation_is_not_delivered(), "old generation operation");
    passed &= check(two_sessions_may_receive_different_views_of_one_canonical_tick(), "session-scoped views");
    passed &= check(complete_roots_have_exact_golden_payloads(), "golden payloads");
    return passed ? 0 : 1;
}
