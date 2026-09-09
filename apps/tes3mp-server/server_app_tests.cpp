#include "actor_content.hpp"
#include "actor_interest_projection.hpp"
#include "authenticated_join_composition.hpp"
#include "combat_content.hpp"
#include "character_content.hpp"
#include "connection_session_coordinator.hpp"
#include "content_collision.hpp"
#include "interactive_object_content.hpp"
#include "interactive_object_interest_projection.hpp"
#include "interest_projection.hpp"
#include "inventory_content.hpp"
#include "inventory_interest_projection.hpp"
#include "phase7_proof_profile.hpp"
#include "phase7_queue_telemetry.hpp"
#include "player_identity_file.hpp"
#include "resume_token_context.hpp"
#include "server_application.hpp"
#include "server_config.hpp"
#include "tes3mp/interactive_object_catalog.hpp"
#include "tes3mp/interactive_object_replication.hpp"
#include "tes3mp/interactive_object_world.hpp"
#include "tes3mp/inventory_replication.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class Value>
    Value id(std::uint64_t value)
    {
        return Value::fromValue(value).value();
    }

    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "server_app_tests failure at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    constexpr std::string_view validConfig
        = "bind_address = 127.0.0.1\nport = 25565\ntick_interval_ms = 16\n"
          "disconnect_grace_ms = 30000\njoin_password_file = password.txt\n"
          "content_manifest_id = 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
          "cell_spaces = interior:7;exterior:8\nallowed_cells = interior:7;exterior:8:0:0\n"
          "spawn_cell = interior:7\ndefault_appearance_id = 1\n"
          "spawn_positions = -10:20:30;40:50:60\n"
          "movement_profile = sneak:1024;walk:4097;run:8192;jump:4096\n"
          "collision_content_file = collision.txt\n"
          "actor_content_file = actors.txt\n"
          "interactive_object_content_file = objects.txt\n"
          "inventory_content_file = inventory.txt\n"
          "combat_content_file = combat.txt\n"
          "player_identity_file = players.txt\n";

    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            calls += 'L';
            if (rejectListen)
                return { TES3MP::TransportResult::AtCapacity, std::nullopt };
            return { TES3MP::TransportResult::Accepted, TES3MP::ListenerId::initial() };
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override
        {
            calls += 'S';
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        {
            return { TES3MP::TransportResult::InvalidInput, std::nullopt };
        }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        {
            return TES3MP::TransportResult::UnknownId;
        }
        TES3MP::TransportResult send(TES3MP::TransportConnectionId connection, TES3MP::TransportChannel channel,
            std::span<const std::byte> bytes) override
        {
            sentConnections.push_back(connection);
            sentChannels.push_back(channel);
            sent.emplace_back(bytes.begin(), bytes.end());
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId connection, std::span<TES3MP::TransportMessage> output) override
        {
            if (receiveResult != TES3MP::TransportResult::Accepted)
                return { receiveResult, 0 };
            auto& source = incomingByConnection.contains(connection) ? incomingByConnection[connection] : incoming;
            const auto count = std::min(output.size(), source.size());
            for (std::size_t index = 0; index < count; ++index)
                output[index] = std::move(source[index]);
            source.erase(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(count));
            return { TES3MP::TransportResult::Accepted, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        {
            ++closes;
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            calls += 'P';
            const auto count = std::min(output.size(), events.size());
            for (std::size_t index = 0; index < count; ++index)
                output[index] = events[index];
            events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
            return { pollResult, count };
        }
        TES3MP::TransportResult shutdown() override
        {
            calls += 'X';
            return TES3MP::TransportResult::Accepted;
        }

        bool rejectListen = false;
        TES3MP::TransportResult pollResult = TES3MP::TransportResult::Accepted;
        TES3MP::TransportResult receiveResult = TES3MP::TransportResult::Accepted;
        std::string calls;
        std::vector<std::vector<std::byte>> sent;
        std::vector<TES3MP::TransportConnectionId> sentConnections;
        std::vector<TES3MP::TransportChannel> sentChannels;
        std::vector<TES3MP::TransportEvent> events;
        std::vector<TES3MP::TransportMessage> incoming;
        std::map<TES3MP::TransportConnectionId, std::vector<TES3MP::TransportMessage>> incomingByConnection;
        std::size_t closes = 0;
    };

    class AcceptedOperation final : public AuthenticationOperation
    {
    public:
        AcceptedOperation(AuthenticationAttempt attempt, PrincipalId principal)
            : mAttempt(attempt)
            , mPrincipal(principal)
        {
        }
        AuthenticationPollResult poll() noexcept override
        {
            return AuthenticationCompletion{ mAttempt, AuthenticatedAdmission::initial(mPrincipal) };
        }
        void cancel() noexcept override {}

    private:
        AuthenticationAttempt mAttempt;
        PrincipalId mPrincipal;
    };

    class FakeAuthentication final : public ServerAuthenticationService
    {
    public:
        std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, ServerAuthenticationSubmission) noexcept override
        {
            const auto principal = id<PrincipalId>(nextPrincipal++);
            return std::make_unique<AcceptedOperation>(attempt, principal);
        }

        ResumeTokenIssueResult issueInitial(
            PrincipalId, SessionId, SessionGeneration, ResumeTokenContext) noexcept override
        {
            ++issues;
            if (reject)
                return ResumeTokenStoreError::RandomUnavailable;
            std::array<std::byte, ResumeTokenBytes> bytes{};
            auto token = ResumeToken::create(bytes);
            return std::move(
                *AuthenticationAcceptedMessage::create(std::move(*token), MinimumResumeTokenLifetimeMilliseconds));
        }

        bool reject = false;
        std::size_t issues = 0;
        std::uint64_t nextPrincipal = 9;
    };

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(nanoseconds); }
        std::uint64_t nanoseconds = 0;
    };

    class RecordingCrypto final : public CredentialCrypto
    {
    public:
        bool randomBytes(std::span<std::byte>) noexcept override { return false; }
        bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept override
        {
            inputs.emplace_back(source.begin(), source.end());
            if (failOnCall == inputs.size())
                return false;
            std::byte folded{};
            for (const auto byte : source)
                folded ^= byte;
            destination.bytes.fill(folded);
            destination.bytes[0] = static_cast<std::byte>(source.size() & 0xff);
            return true;
        }
        bool constantTimeEqual(std::span<const std::byte>, std::span<const std::byte>) noexcept override
        {
            return false;
        }

        std::size_t failOnCall = 0;
        std::vector<std::vector<std::byte>> inputs;
    };

    CapabilityOffer emptyOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, {}));
    }

    CapabilityOffer poseOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
        const std::array capabilities{ vrPoseCapability() };
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
    }

    CapabilityOffer actorOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
        const std::array capabilities{ actorReplicationCapability() };
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
    }

    CapabilityOffer locomotionOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 3, 3));
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, {}));
    }

    CapabilityOffer objectOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
        const std::array capabilities{ interactiveObjectReplicationCapability() };
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
    }

    CapabilityOffer inventoryObjectOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
        const std::array capabilities{ interactiveObjectReplicationCapability(), inventoryReplicationCapability() };
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
    }

    InteractiveObjectCatalog sampleObjectCatalog(const ContentManifest& manifest)
    {
        const auto zero = Turn32::fromValue(0);
        const auto tr7
            = Transform(CellId::interior(id<CellSpaceId>(7)), Position3(100, 20, 30), Orientation3(zero, zero, zero));
        const auto tr8 = Transform(
            CellId::exterior(id<CellSpaceId>(8), 0, 0), Position3(500, 20, 30), Orientation3(zero, zero, zero));
        const std::array entries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
                                      InteractiveObjectKind::StandardDoor, CellId::interior(id<CellSpaceId>(7)), tr7,
                                      std::nullopt, ObjectLockDeclaration{ false, 0, std::nullopt },
                                      ObjectTrapDeclaration{ false, std::nullopt } },
            InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(2), InteractiveObjectKind::StandardDoor,
                CellId::exterior(id<CellSpaceId>(8), 0, 0), tr8, std::nullopt,
                ObjectLockDeclaration{ false, 0, std::nullopt }, ObjectTrapDeclaration{ false, std::nullopt } } };
        auto created = InteractiveObjectCatalog::create(manifest, entries);
        assert(created.has_value());
        return std::move(*created);
    }

    AdmissionScopeId scope(std::byte value)
    {
        std::array<std::byte, AdmissionScopeIdBytes> bytes{};
        bytes.fill(value);
        return *AdmissionScopeId::create(bytes);
    }

    class FakeJoinQueue final : public TES3MP::ServerApp::JoinResponseQueue
    {
    public:
        bool enqueueJoinResponses(std::span<const std::byte> authentication, std::span<const std::byte> snapshot,
            const CanonicalServerState&, const CanonicalServerState&, const AuthenticatedJoinResult& join, ServerTick,
            CanonicalStateVersion) noexcept override
        {
            ++attempts;
            if (reject)
                return false;
            auto authenticationFrame = decodeProtocolFrame(authentication);
            auto snapshotFrame = decodeProtocolFrame(snapshot);
            valid = std::holds_alternative<DecodedFrame>(authenticationFrame)
                && std::get<DecodedFrame>(authenticationFrame).messageKind() == MessageKind::AuthenticationAccepted
                && std::holds_alternative<DecodedFrame>(snapshotFrame)
                && std::get<DecodedFrame>(snapshotFrame).messageKind() == MessageKind::LatestWinsSnapshot;
            revisions.push_back(join.initialSnapshot.header().canonicalRevision());
            return valid;
        }

        bool reject = false;
        bool valid = false;
        std::size_t attempts = 0;
        std::vector<CanonicalRevision> revisions;
    };

    AuthenticatedJoinCoordinator joinCoordinator(CanonicalCommandReducer& reducer)
    {
        const auto zero = Turn32::fromValue(0);
        auto spawn
            = Transform(CellId::interior(id<CellSpaceId>(7)), Position3(10, 20, 30), Orientation3(zero, zero, zero));
        return *AuthenticatedJoinCoordinator::create(
            spawn, id<AppearanceId>(1), { id<SessionId>(1), id<PlayerId>(1), id<EntityId>(1) }, reducer);
    }

    struct JoinFixture
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        CanonicalCommandReducer reducer{ std::get<CanonicalServerState>(createCanonicalServerState({}, {})),
            observability };
        AuthenticatedJoinCoordinator joins{ joinCoordinator(reducer) };
    };

    TES3MP::ServerApp::ServerConfig parsedConfig()
    {
        auto result = TES3MP::ServerApp::parseServerConfig(validConfig);
        assert(std::holds_alternative<TES3MP::ServerApp::ServerConfig>(result));
        return std::get<TES3MP::ServerApp::ServerConfig>(std::move(result));
    }

    CanonicalServerState fixtureState(bool secondExterior)
    {
        const auto zero = Turn32::fromValue(0);
        const auto interior = CellId::interior(id<CellSpaceId>(7));
        const auto exterior = CellId::exterior(id<CellSpaceId>(8), 0, 0);
        std::vector<CanonicalPlayerEntityState> players{
            { id<PlayerId>(1), id<EntityId>(1), id<AppearanceId>(1),
                Transform(interior, Position3(1, 0, 0), Orientation3(zero, zero, zero)), LinearVelocity3(0, 0, 0),
                id<EntityRevision>(2), AuthorityEpoch::initial(), id<ServerTick>(4) },
            { id<PlayerId>(2), id<EntityId>(2), id<AppearanceId>(1),
                Transform(secondExterior ? exterior : interior, Position3(2, 0, 0), Orientation3(zero, zero, zero)),
                LinearVelocity3(0, 0, 0), id<EntityRevision>(secondExterior ? 2 : 1), AuthorityEpoch::initial(),
                id<ServerTick>(4) }
        };
        std::vector<CanonicalSessionProgress> sessions{ { id<SessionId>(1), SessionGeneration::initial(),
                                                            id<PlayerId>(1), id<EntityId>(1), std::nullopt },
            { id<SessionId>(2), SessionGeneration::initial(), id<PlayerId>(2), id<EntityId>(2), std::nullopt } };
        return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
    }
}

int main()
{
    using namespace TES3MP::ServerApp;
    {
        const auto manifestId = ContentManifestId::fromHex(
            "5c3c8c2cbd20e25901b59b3ece33d36b7ef0e3d60ad8d11828bcc61a5ead1647");
        const auto spaces = parseCellSpaceDeclarations("interior:1;interior:2;interior:3;exterior:4");
        const auto cells = parseContentCells("interior:1;interior:2;interior:3;exterior:4:-2:-9");
        const auto movement = parseMovementProfile("sneak:4;walk:8;run:16;jump:12");
        const auto appearance = AppearanceId::fromValue(100);
        auto manifest = manifestId && spaces && cells && movement && appearance
            ? ContentManifest::create(*manifestId, *spaces, *cells, *appearance, *movement)
            : std::nullopt;
        assert(manifest);
        const auto path = std::filesystem::path(TES3MP_SOURCE_ROOT)
            / "files/data/tes3mp/vanilla-characters.txt";
        auto loaded = loadCharacterContent(path, *manifest);
        const auto* catalog = std::get_if<CharacterContentCatalog>(&loaded);
        const CharacterAppearance stockMale{
            *RaceRecordId::fromValue(*characterRecordId("Dark Elf")),
            *HeadRecordId::fromValue(*characterRecordId("b_n_dark elf_m_head_01")),
            *HairRecordId::fromValue(*characterRecordId("b_n_dark elf_m_hair_01")), CharacterSex::Male };
        const CharacterAppearance stockFemale{
            *RaceRecordId::fromValue(*characterRecordId("Dark Elf")),
            *HeadRecordId::fromValue(*characterRecordId("b_n_dark elf_f_head_01")),
            *HairRecordId::fromValue(*characterRecordId("b_n_dark elf_f_hair_01")), CharacterSex::Female };
        const auto* darkElf = catalog ? catalog->find(stockMale.race) : nullptr;
        constexpr std::array vanillaRaces{ "Argonian", "Breton", "Dark Elf", "High Elf", "Imperial",
            "Khajiit", "Nord", "Orc", "Redguard", "Wood Elf" };
        std::size_t packagedAppearanceCount = 0;
        for (const std::string_view raceName : vanillaRaces)
        {
            const auto raceId = characterRecordId(raceName);
            const auto* race = raceId ? catalog->find(*RaceRecordId::fromValue(*raceId)) : nullptr;
            assert(race);
            packagedAppearanceCount += race->appearances.size();
        }
        assert(catalog && catalog->creationSpawn().cell() == CellId::interior(id<CellSpaceId>(1))
            && catalog->creationSpawn().position() == Position3(62464, -138240, 24576)
            && catalog->creationSpawn().orientation().z() == Turn32::fromValue(4056358002u)
            && catalog->completionSpawn().cell() == CellId::exterior(id<CellSpaceId>(4), -2, -9)
            && catalog->completionSpawn().position() == Position3(-10443674, -73251021, 237568)
            && catalog->completionSpawn().orientation().z() == Turn32::fromValue(536870912u)
            && packagedAppearanceCount == 1181
            && darkElf && std::ranges::find(darkElf->appearances, stockMale) != darkElf->appearances.end()
            && std::ranges::find(darkElf->appearances, stockFemale) != darkElf->appearances.end()
            && catalog->find(*ClassRecordId::fromValue(*characterRecordId("Warrior")))
            && catalog->find(*BirthsignRecordId::fromValue(*characterRecordId("Fay"))));
    }
    {
        TES3MP::ServerApp::Phase7QueueTelemetry telemetry;
        const auto record = [&](TransportTelemetryKind kind, TransportChannel channel, std::uint64_t value) {
            assert(telemetry.tryRecord({ kind, TransportTelemetryDirection::Outbound, channel, value })
                == TransportTelemetryResult::Accepted);
        };
        record(TransportTelemetryKind::QueuedMessages, TransportChannel::ReliableOrdered, 3);
        record(TransportTelemetryKind::QueuedBytes, TransportChannel::ReliableOrdered, 30);
        record(TransportTelemetryKind::QueuedMessages, TransportChannel::LatestWins, 1);
        record(TransportTelemetryKind::QueuedBytes, TransportChannel::LatestWins, 10);
        assert(!telemetry.takeDrainEvidence());
        record(TransportTelemetryKind::QueuedMessages, TransportChannel::ReliableOrdered, 0);
        record(TransportTelemetryKind::QueuedBytes, TransportChannel::ReliableOrdered, 0);
        record(TransportTelemetryKind::QueuedMessages, TransportChannel::LatestWins, 0);
        record(TransportTelemetryKind::QueuedBytes, TransportChannel::LatestWins, 0);
        const auto evidence = telemetry.takeDrainEvidence();
        assert(evidence && evidence->reliableHighWaterMessages == 3 && evidence->reliableHighWaterBytes == 30
            && evidence->latestHighWaterMessages == 1 && evidence->latestHighWaterBytes == 10
            && !telemetry.takeDrainEvidence());
    }
    static_assert(Phase7ProtocolMajor == 1 && Phase7ProtocolMinimumMinor == 2 && Phase7ProtocolMaximumMinor == 3);
    static_assert(Phase7SourceAuthenticationBurst == 4 && Phase7GlobalAuthenticationBurst == 32
        && Phase7AuthenticationRefillMilliseconds == 1'000 && Phase7ConnectionCapacity == 8);
    static_assert(!phase7ProofDisconnectGraceAccepted(MinimumResumeTokenLifetimeMilliseconds - 1));
    static_assert(phase7ProofDisconnectGraceAccepted(MinimumResumeTokenLifetimeMilliseconds));
    static_assert(phase7ProofDisconnectGraceAccepted(MaximumResumeTokenLifetimeMilliseconds));
    static_assert(!phase7ProofDisconnectGraceAccepted(MaximumResumeTokenLifetimeMilliseconds + 1));
    {
        const auto before = fixtureState(false);
        const auto after = fixtureState(true);
        auto baseline = projectInterestBaseline(
            before, id<SessionId>(1), id<ServerTick>(4), id<CanonicalRevision>(4), id<CanonicalStateVersion>(4));
        assert(baseline && baseline->baseline.members().size() == 2 && baseline->view.view().entries().size() == 2
            && sharesInterest(before, id<SessionId>(1), id<SessionId>(2))
            && !sharesInterest(after, id<SessionId>(1), id<SessionId>(2)));
        auto projected = projectInterestChanges(before, after, id<ServerTick>(4), id<CanonicalRevision>(4));
        assert(projected && projected->size() == 2);
        assert((*projected)[0].targetSession == id<SessionId>(1));
        assert(((*projected)[0].observations.changes().size() == 1
            && (*projected)[0].observations.changes()[0]
                == ObservationChange{ id<PlayerId>(2), id<EntityId>(2), ObservationChangeKind::Leave }));
        assert((*projected)[0].view.view().entries().size() == 1
            && (*projected)[0].view.view().entries()[0].playerId() == id<PlayerId>(1));
        assert(((*projected)[1].observations.changes().size() == 1
            && (*projected)[1].observations.changes()[0]
                == ObservationChange{ id<PlayerId>(1), id<EntityId>(1), ObservationChangeKind::Leave }));
        assert((*projected)[1].view.view().entries().size() == 1
            && (*projected)[1].view.view().entries()[0].playerId() == id<PlayerId>(2));
        assert(projectInterestChanges(after, after, id<ServerTick>(5), id<CanonicalRevision>(5))->empty());

        const std::array remainingPlayers{ before.players()[0] };
        const std::array remainingSessions{ before.activeSessions()[0] };
        const auto expired
            = std::get<CanonicalServerState>(createCanonicalServerState(remainingPlayers, remainingSessions));
        auto expirationOutput = projectInterestChanges(before, expired, id<ServerTick>(5), id<CanonicalRevision>(5));
        assert(expirationOutput && expirationOutput->size() == 1
            && (*expirationOutput)[0].targetSession == id<SessionId>(1));
        assert(((*expirationOutput)[0].observations.changes().size() == 1
            && (*expirationOutput)[0].observations.changes()[0]
                == ObservationChange{ id<PlayerId>(2), id<EntityId>(2), ObservationChangeKind::Leave }));

        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 1);
        const auto connection = TransportConnectionId::initial();
        assert(queues->attach(connection) == TransportResult::Accepted);
        assert(admitInterestChange(*queues, connection, (*projected)[0]));
        FakeRuntime runtime;
        assert(queues->pump(runtime, connection, 0) == OutboundPumpResult::Progress);
        assert(runtime.sent.size() == 2);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[0])).messageKind()
            == MessageKind::ReliableObservationBatch);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[1])).messageKind()
            == MessageKind::LatestWinsSnapshot);

        auto blockedPolicy = OutboundQueuePolicy{};
        blockedPolicy.reliableMessages = 0;
        auto blocked = OutboundQueueSet::create(blockedPolicy, 1);
        assert(blocked->attach(connection) == TransportResult::Accepted);
        assert(!admitInterestChange(*blocked, connection, (*projected)[0]));
        assert(blocked->pump(runtime, connection, 1) == OutboundPumpResult::Idle);
    }
    {
        const auto negotiated
            = std::get<ServerHello>(negotiateClientHello(ClientHello::fromOffer(emptyOffer()), emptyOffer()));
        RecordingCrypto first;
        RecordingCrypto second;
        const auto firstContext = makeResumeTokenContext(negotiated, first);
        const auto secondContext = makeResumeTokenContext(negotiated, second);
        assert(firstContext && secondContext && *firstContext == *secondContext);
        assert(first.inputs.size() == 2);
        const auto expectedContent = negotiated.contentManifest().bytes();
        assert(first.inputs[1] == std::vector<std::byte>(expectedContent.begin(), expectedContent.end()));

        auto newerVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 1, 1));
        auto newerClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(newerVersions, {}, {}));
        auto newerServerOffer = std::get<CapabilityOffer>(CapabilityOffer::create(newerVersions, {}, {}));
        const auto newer = std::get<ServerHello>(
            negotiateClientHello(ClientHello::fromOffer(std::move(newerClientOffer)), newerServerOffer));
        RecordingCrypto changed;
        const auto changedContext = makeResumeTokenContext(newer, changed);
        assert(changedContext && changedContext->protocol != firstContext->protocol
            && changedContext->content == firstContext->content);

        RecordingCrypto failed;
        failed.failOnCall = 2;
        assert(!makeResumeTokenContext(negotiated, failed));
    }
    {
        auto result = parseServerConfig(validConfig);
        assert(std::holds_alternative<ServerConfig>(result));
        const auto& config = std::get<ServerConfig>(result);
        assert(config.endpoint.address() == "127.0.0.1" && config.endpoint.port() == 25565);
        assert(config.tickIntervalMilliseconds == 16 && config.disconnectGraceMilliseconds == 30000);
        assert(config.collisionContentFile == std::filesystem::path("collision.txt"));
        assert(config.actorContentFile == std::filesystem::path("actors.txt"));
        assert(config.interactiveObjectContentFile == std::filesystem::path("objects.txt"));
        assert(config.inventoryContentFile == std::filesystem::path("inventory.txt"));
        assert(config.combatContentFile == std::filesystem::path("combat.txt"));
        const std::vector<Position3> expectedSpawns{ Position3(-10, 20, 30), Position3(40, 50, 60) };
        assert(config.spawnPositions == expectedSpawns);
        assert(config.contentManifest.movementProfile().speed(LocomotionMode::Sneak) == 1024
            && config.contentManifest.movementProfile().speed(LocomotionMode::Jump) == 4096);
    }
    for (const auto invalid : { std::string{}, std::string("unknown = x\n"), std::string(validConfig) + "port = 2\n",
             std::string("bind_address = host\nport = 1\ntick_interval_ms = 1\n"
                         "disconnect_grace_ms = 0\njoin_password_file = p\n"),
             std::string("bind_address = 127.0.0.1 # no inline comment\nport = 1\n"
                         "tick_interval_ms = 1\ndisconnect_grace_ms = 0\njoin_password_file = p\n") })
        assert(std::holds_alternative<ConfigError>(parseServerConfig(invalid)));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigLineBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string("\xc0\x80", 2))));
    auto duplicateContentIds = std::string(validConfig);
    duplicateContentIds.replace(duplicateContentIds.find("cell_spaces = interior:7;exterior:8"),
        std::string("cell_spaces = interior:7;exterior:8").size(), "cell_spaces = interior:7;exterior:7");
    assert(std::holds_alternative<ConfigError>(parseServerConfig(duplicateContentIds)));
    auto invalidMovementProfile = std::string(validConfig);
    invalidMovementProfile.replace(invalidMovementProfile.find("sneak:1024;walk:4097;run:8192;jump:4096"),
        std::string("sneak:1024;walk:4097;run:8192;jump:4096").size(), "sneak:4097;walk:1024;run:8192;jump:4096");
    assert(std::holds_alternative<ConfigError>(parseServerConfig(invalidMovementProfile)));
    auto invalidSpawnPositions = std::string(validConfig);
    invalidSpawnPositions.replace(
        invalidSpawnPositions.find("-10:20:30;40:50:60"), std::string("-10:20:30;40:50:60").size(), "1:2");
    assert(std::holds_alternative<ConfigError>(parseServerConfig(invalidSpawnPositions)));

    const auto collisionPath = std::filesystem::temp_directory_path() / "tes3mp-server-collision-content-test";
    const auto writeCollision = [&](std::string_view content) {
        std::ofstream stream(collisionPath, std::ios::binary | std::ios::trunc);
        stream << content;
        assert(static_cast<bool>(stream));
    };
    constexpr std::string_view collisionHeader
        = "TES3MP_COLLISION_V1\n"
          "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
    writeCollision(
        std::string(collisionHeader) + "cell interior 7\ncell exterior 8 0 0\nsolid interior 7 15 0 0 20 40 60\n");
    auto collisionResult = ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest);
    assert(std::holds_alternative<std::unique_ptr<ContentCollisionProvider>>(collisionResult));
    auto collision = std::move(std::get<std::unique_ptr<ContentCollisionProvider>>(collisionResult));
    const auto zero = Turn32::fromValue(0);
    const auto interior = CellId::interior(id<CellSpaceId>(7));
    const auto root = Transform(interior, Position3(10, 20, 30), Orientation3(zero, zero, zero));
    assert(collision->canOccupy(interior, root.position()) && !collision->canOccupy(interior, Position3(16, 20, 30)));
    const auto blocked = collision->resolve({ testContentManifestId(), id<EntityId>(1), ServerTick::initial(), root,
        Position3(25, 20, 30), LinearVelocity3(15, 0, 0), LocomotionMode::Walk });
    assert(blocked && blocked->position == root.position() && blocked->velocity == LinearVelocity3(0, 0, 0));
    const auto clear = collision->resolve({ testContentManifestId(), id<EntityId>(1), ServerTick::initial(), root,
        Position3(10, 25, 30), LinearVelocity3(0, 5, 0), LocomotionMode::Walk });
    assert(clear && clear->position == Position3(10, 25, 30) && clear->velocity == LinearVelocity3(0, 5, 0));
    assert(!collision->resolve({ testContentManifestId(), id<EntityId>(1), ServerTick::initial(), root,
        Position3(25, 20, 30), LinearVelocity3(14, 0, 0), LocomotionMode::Walk }));
    const auto boundaryRoot
        = Transform(interior, Position3(MaximumCollisionCoordinate, 20, 30), Orientation3(zero, zero, zero));
    const auto boundary
        = collision->resolve({ testContentManifestId(), id<EntityId>(1), ServerTick::initial(), boundaryRoot,
            Position3(MaximumCollisionCoordinate + 1, 20, 30), LinearVelocity3(1, 0, 0), LocomotionMode::Walk });
    assert(boundary && boundary->position == boundaryRoot.position() && boundary->velocity == LinearVelocity3(0, 0, 0));
    writeCollision(std::string(collisionHeader) + "cell interior 7\n");
    assert(
        std::get<ContentCollisionError>(ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest))
        == ContentCollisionError::IncompleteCells);
    writeCollision(
        "TES3MP_COLLISION_V1\n"
        "manifest 0202030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
        "cell interior 7\ncell exterior 8 0 0\n");
    assert(
        std::get<ContentCollisionError>(ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest))
        == ContentCollisionError::ManifestMismatch);
    writeCollision(
        std::string(collisionHeader) + "cell interior 7\ncell exterior 8 0 0\nsolid interior 7 20 0 0 15 40 60\n");
    assert(
        std::get<ContentCollisionError>(ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest))
        == ContentCollisionError::Malformed);
    writeCollision(std::string(MaximumCollisionContentBytes + 1, 'x'));
    assert(
        std::get<ContentCollisionError>(ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest))
        == ContentCollisionError::TooLarge);
    std::filesystem::remove(collisionPath);
    assert(
        std::get<ContentCollisionError>(ContentCollisionProvider::load(collisionPath, parsedConfig().contentManifest))
        == ContentCollisionError::Unavailable);

    const auto actorPath = std::filesystem::temp_directory_path() / "tes3mp-server-actor-content-test";
    const auto writeActors = [&](std::string_view content) {
        std::ofstream stream(actorPath, std::ios::binary | std::ios::trunc);
        stream << content;
        assert(static_cast<bool>(stream));
    };
    constexpr std::string_view actorHeader
        = "TES3MP_ACTORS_V1\n"
          "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
    writeActors(std::string(actorHeader) + "actor 1 100 200 interior 7 10 20 30 0 0 0 travel 10 25 30\n");
    auto actorContent = loadActorContent(actorPath, parsedConfig().contentManifest);
    assert(std::holds_alternative<ActorCatalog>(actorContent));
    const auto& actorCatalog = std::get<ActorCatalog>(actorContent);
    assert(actorCatalog.entries().size() == 1 && actorCatalog.entries()[0].entityId == id<EntityId>(100));
    auto actorWorld = createInitialCanonicalActorWorld(actorCatalog);
    assert(std::holds_alternative<CanonicalActorWorld>(actorWorld));
    auto actors = std::get<CanonicalActorWorld>(std::move(actorWorld));
    const auto players = fixtureState(false);
    auto actorBaseline
        = projectActorInterestBaseline(players, actors, id<SessionId>(1), id<ServerTick>(4), id<CanonicalRevision>(2));
    assert(actorBaseline && actorBaseline->baseline.members().size() == 1
        && actorBaseline->view.view().entries().size() == 1);
    writeActors(std::string(actorHeader) + "actor 1 100 200 interior 9 10 20 30 0 0 0 idle\n");
    assert(std::get<ActorContentError>(loadActorContent(actorPath, parsedConfig().contentManifest))
        == ActorContentError::Malformed);
    std::filesystem::remove(actorPath);

    const auto combatPath = std::filesystem::temp_directory_path() / "tes3mp-server-combat-content-test";
    const auto writeCombat = [&](std::string_view content) {
        std::ofstream stream(combatPath, std::ios::binary | std::ios::trunc);
        stream << content;
        assert(static_cast<bool>(stream));
    };
    constexpr std::string_view combatHeader
        = "TES3MP_COMBAT_V1\n"
          "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
          "seed 42\n";
    constexpr std::string_view combatBody
        = "settings 0.2 5 1 0.1 1 1 0.1 0.1 1 1 1.5 1\n"
          "player 50 40 40 1 0 0 10 20 30 40 50 25 100 500 0\n"
          "actor 1 20 50 0 0 0 25 0 0 0 0 0\n"
          "weapon 4 1 1 10 1 10 1 10 5 1 1\n";
    const std::array combatItemDeclarations{ ItemPrototypeDeclaration{ id<ItemPrototypeId>(4), ItemCategory::Weapon,
        5, 1, 100, 0, slotToMask(EquipmentSlot::CarriedRight), false, std::nullopt } };
    auto combatItems = *ItemPrototypeCatalog::create(parsedConfig().contentManifest, combatItemDeclarations);
    writeCombat(std::string(combatHeader) + std::string(combatBody));
    auto loadedCombat = loadCombatContent(combatPath, parsedConfig().contentManifest, actorCatalog, combatItems);
    assert(std::holds_alternative<CombatContent>(loadedCombat));
    const auto& combat = std::get<CombatContent>(loadedCombat);
    assert(combat.world.actors().size() == 1 && combat.world.players().empty()
        && combat.weapons.find(id<ItemPrototypeId>(4))
        && combat.playerTemplate.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::LongBlade)] == 20.f);
    writeCombat(std::string(combatHeader)
        + "settings nan 5 1 0.1 1 1 0.1 0.1 1 1 1.5 1\n"
          "player 50 40 40 1 0 0 10 20 30 40 50 25 100 500 0\n"
          "actor 1 20 50 0 0 0 25 0 0 0 0 0\n");
    const auto malformedCombat
        = std::get<CombatContentError>(loadCombatContent(combatPath, parsedConfig().contentManifest,
            actorCatalog, combatItems));
    assert(malformedCombat.code == CombatContentErrorCode::InvalidSettings && malformedCombat.line == 4
        && describeCombatContentError(malformedCombat) == "invalid settings at line 4");
    writeCombat(std::string(combatHeader)
        + "settings 0.2 5 1 0.1 1 1 0.1 0.1 1 1 1.5 1\n"
          "player 50 40 40 1 0 0 10 20 30 40 50 25 100 500 0\n");
    assert(std::get<CombatContentError>(loadCombatContent(combatPath, parsedConfig().contentManifest,
               actorCatalog, combatItems)).code == CombatContentErrorCode::InvalidActorSet);
    writeCombat("TES3MP_COMBAT_V1\n"
                "manifest 0202030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
                "seed 42\n" + std::string(combatBody));
    assert(std::get<CombatContentError>(loadCombatContent(combatPath, parsedConfig().contentManifest,
               actorCatalog, combatItems)).code == CombatContentErrorCode::ManifestMismatch);
    std::filesystem::remove(combatPath);

    const auto objectPath = std::filesystem::temp_directory_path() / "tes3mp-server-object-content-test";
    const auto writeObjects = [&](std::string_view content) {
        std::ofstream stream(objectPath, std::ios::binary | std::ios::trunc);
        stream << content;
        assert(static_cast<bool>(stream));
    };
    constexpr std::string_view objectHeader
        = "TES3MP_INTERACTIVE_OBJECTS_V1\n"
          "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
    writeObjects(std::string(objectHeader)
        + "object 1 standard interior 7 100 20 30 0 0 0 0 none none\n"
          "object 2 teleport interior 7 20 20 30 0 0 0 0 none none exterior 8 0 0 500 20 30 0 0 0\n");
    auto objectContent = loadInteractiveObjectContent(objectPath, parsedConfig().contentManifest);
    assert(std::holds_alternative<InteractiveObjectCatalog>(objectContent));
    const auto& loadedObjects = std::get<InteractiveObjectCatalog>(objectContent);
    assert(loadedObjects.entries().size() == 2 && loadedObjects.entries()[0].objectId == id<InteractiveObjectId>(1)
        && loadedObjects.entries()[1].destination
        && loadedObjects.entries()[1].destination->cell == CellId::exterior(id<CellSpaceId>(8), 0, 0));
    auto loadedWorld = createInitialCanonicalInteractiveObjectWorld(loadedObjects);
    assert(std::holds_alternative<CanonicalInteractiveObjectWorld>(loadedWorld));
    writeObjects(std::string(objectHeader) + "object 1 standard interior 7 0 0 0 0 0 0 0 99 none\n");
    assert(std::get<InteractiveObjectContentError>(
               loadInteractiveObjectContent(objectPath, parsedConfig().contentManifest))
        == InteractiveObjectContentError::Malformed);
    std::filesystem::remove(objectPath);

    const auto inventoryPath = std::filesystem::temp_directory_path() / "tes3mp-server-inventory-content-test";
    const auto writeInventory = [&](std::string_view content) {
        std::ofstream stream(inventoryPath, std::ios::binary | std::ios::trunc);
        stream << content;
        assert(static_cast<bool>(stream));
    };
    constexpr std::string_view inventoryHeader
        = "TES3MP_INVENTORY_V1\n"
          "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
    writeInventory(std::string(inventoryHeader)
        + "prototype 1 8 1 5 0 0 0 1 1\n"
          "container 1 interior 7 20 20 30 100\n"
          "container_item 1 10 1 1 0 0 none\n"
          "ground_item 11 1 1 0 0 none interior 7 25 20 30\n");
    auto inventoryContent = loadInventoryContent(inventoryPath, parsedConfig().contentManifest);
    assert(std::holds_alternative<InventoryContent>(inventoryContent));
    auto loadedInventory = std::get<InventoryContent>(std::move(inventoryContent));
    assert(loadedInventory.catalog.declarations().size() == 1 && loadedInventory.world.containers().size() == 1
        && loadedInventory.world.worldItems().size() == 1);
    assert(loadedInventory.world.ensurePlayer(id<PlayerId>(1)) && loadedInventory.world.ensurePlayer(id<PlayerId>(2)));
    auto inventoryProjection = projectInventoryInterestBaseline(
        fixtureState(false), loadedInventory.world, id<SessionId>(1), id<ServerTick>(4), id<CanonicalRevision>(2));
    assert(inventoryProjection && inventoryProjection->playerInventory.size() == 1
        && inventoryProjection->playerInventory[0].player == id<PlayerId>(1)
        && inventoryProjection->containers.size() == 1 && inventoryProjection->groundItems.size() == 1
        && inventoryProjection->equipment && inventoryProjection->equipment->members.size() == 2);
    writeInventory(std::string(inventoryHeader) + "prototype 0 8 1 5 0 0 0 1 none\n");
    assert(std::get<InventoryContentError>(loadInventoryContent(inventoryPath, parsedConfig().contentManifest))
        == InventoryContentError::Malformed);
    std::filesystem::remove(inventoryPath);

    const auto temporary = std::filesystem::temp_directory_path() / "tes3mp-server-password-test";
    {
        std::ofstream stream(temporary, std::ios::binary);
        stream << "secret\r\n";
    }
    auto password = loadJoinPassword(temporary);
    assert(std::holds_alternative<TES3MP::AuthenticationMaterial>(password));
    assert(std::get<TES3MP::AuthenticationMaterial>(password).size() == 6);
    std::filesystem::remove(temporary);
    assert(std::holds_alternative<ConfigError>(loadJoinPassword(temporary)));

    const auto identityPath = std::filesystem::temp_directory_path() / "tes3mp-server-player-identities-test";
    std::filesystem::remove(identityPath);
    auto identityFileResult = PlayerIdentityFile::open(identityPath);
    assert(std::holds_alternative<std::unique_ptr<PlayerIdentityFile>>(identityFileResult));
    auto identityFile = std::move(std::get<std::unique_ptr<PlayerIdentityFile>>(identityFileResult));
    CredentialDigest identityDigest;
    identityDigest.bytes.fill(std::byte{ 0x4a });
    const auto zeroTurn = Turn32::fromValue(0);
    const CanonicalPlayerEntityState savedIdentityPlayer(id<PlayerId>(3), id<EntityId>(5), id<AppearanceId>(7),
        Transform(CellId::interior(id<CellSpaceId>(7)), Position3(100, 200, 300),
            Orientation3(zeroTurn, zeroTurn, zeroTurn)),
        LinearVelocity3(1, 2, 3), id<EntityRevision>(4), id<AuthorityEpoch>(2), id<ServerTick>(8),
        LocomotionMode::Run);
    CharacterDerivedState savedDerived;
    savedDerived.attributes.fill(40);
    savedDerived.skills.fill(5);
    savedDerived.startingSpells = { id<SpellRecordId>(41) };
    auto savedCharacter = CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter,
        CharacterCreationPhase::Complete, "Nerevar",
        CharacterAppearance{ id<RaceRecordId>(11), id<HeadRecordId>(12), id<HairRecordId>(13), CharacterSex::Male },
        CharacterClass{ id<ClassRecordId>(21) }, id<BirthsignRecordId>(31), std::move(savedDerived),
        std::vector<StartingItem>{ { id<ItemPrototypeId>(51), 1, 3 } }, id<CharacterProfileRevision>(6));
    assert(savedCharacter);
    const std::array identityRecords{ PersistedPlayerIdentity{
        { id<PlayerId>(3), id<EntityId>(5), id<AppearanceId>(7), testContentManifestId() }, identityDigest,
        savedIdentityPlayer, *savedCharacter } };
    assert(identityFile->replace(identityRecords));
    auto identityTemporaryPath = identityPath;
    identityTemporaryPath += ".tmp";
    assert(!std::filesystem::exists(identityTemporaryPath));
    auto reopenedResult = PlayerIdentityFile::open(identityPath);
    assert(std::holds_alternative<std::unique_ptr<PlayerIdentityFile>>(reopenedResult));
    auto reopened = std::move(std::get<std::unique_ptr<PlayerIdentityFile>>(reopenedResult));
    assert(reopened->records().size() == 1 && reopened->records()[0] == identityRecords[0]);
    auto unsafeIdentity = identityRecords[0];
    unsafeIdentity.characterProfile = CharacterProfile::fresh();
    assert(!identityFile->replace(std::span<const PersistedPlayerIdentity>(&unsafeIdentity, 1)));
    auto unchangedResult = PlayerIdentityFile::open(identityPath);
    assert(std::holds_alternative<std::unique_ptr<PlayerIdentityFile>>(unchangedResult));
    assert(std::get<std::unique_ptr<PlayerIdentityFile>>(unchangedResult)->records()[0] == identityRecords[0]);
    {
        std::ofstream stream(identityPath, std::ios::binary | std::ios::trunc);
        stream << "TES3MP_PLAYER_IDENTITIES_V1\n3 5 7 "
               << "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 "
               << std::string(CredentialDigestBytes * 2, 'a') << '\n';
    }
    assert(std::holds_alternative<PlayerIdentityFileError>(PlayerIdentityFile::open(identityPath)));
    {
        std::ofstream stream(identityPath, std::ios::binary | std::ios::trunc);
        stream << "TES3MP_PLAYER_IDENTITIES_V3\n3 5 7 "
               << "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 "
               << std::string(CredentialDigestBytes * 2, 'a') << " 0 9 1 1 - 0 0 0 0 0 0\n";
    }
    assert(std::holds_alternative<PlayerIdentityFileError>(PlayerIdentityFile::open(identityPath)));
    {
        std::ofstream stream(identityPath, std::ios::binary | std::ios::trunc);
        stream << "TES3MP_PLAYER_IDENTITIES_V4\n";
    }
    assert(std::holds_alternative<std::unique_ptr<PlayerIdentityFile>>(PlayerIdentityFile::open(identityPath)));
    {
        std::ofstream stream(identityPath, std::ios::binary | std::ios::trunc);
        stream << "TES3MP_PLAYER_IDENTITIES_V5\n";
    }
    assert(std::holds_alternative<PlayerIdentityFileError>(PlayerIdentityFile::open(identityPath)));
    std::filesystem::remove(identityPath);

    auto config = parsedConfig();
    FakeRuntime runtime;
    ServerApplication application(runtime, config);
    assert(application.start() && application.pump() && application.stop());
    assert(runtime.calls == "LPSX");
    assert(application.stop() && runtime.calls == "LPSX");

    FakeRuntime rejected;
    rejected.rejectListen = true;
    ServerApplication rejectedApplication(rejected, config);
    assert(!rejectedApplication.start());
    assert(rejected.calls == "LX");

    FakeRuntime failed;
    ServerApplication failedApplication(failed, config);
    assert(failedApplication.start());
    failed.pollResult = TES3MP::TransportResult::RuntimeFailed;
    assert(!failedApplication.pump());
    assert(failed.calls == "LPSX");

    {
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        FakeAuthentication authentication;
        FakeJoinQueue responses;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        assert(composition
                   .join(id<PrincipalId>(1), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::Committed);
        assert(composition
                   .join(id<PrincipalId>(2), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::Committed);
        assert(authentication.issues == 2 && responses.attempts == 2 && responses.valid
            && responses.revisions.size() == 2 && responses.revisions[0] < responses.revisions[1]);
        assert(joins.liveBindings() == 2 && joins.state().players().size() == 2);
    }
    {
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        FakeAuthentication authentication;
        FakeJoinQueue responses;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        authentication.reject = true;
        assert(composition
                   .join(id<PrincipalId>(3), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::TokenRejected);
        assert(joins.liveBindings() == 0 && joins.state().players().empty());
        authentication.reject = false;
        responses.reject = true;
        assert(composition
                   .join(id<PrincipalId>(3), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::QueueRejected);
        assert(joins.liveBindings() == 0 && joins.state().players().empty());
        responses.reject = false;
        assert(composition
                   .join(id<PrincipalId>(3), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::Committed);
        assert(joins.liveBindings() == 1);
    }
    {
        auto queues = OutboundQueueSet::create(*OutboundQueuePolicy::create(1, 64 * 1024, 4, 2, 4, 1, 1, 1, 3, 100), 1);
        const auto connection = TransportConnectionId::initial();
        assert(queues && queues->attach(connection) == TransportResult::Accepted);
        TransportJoinResponseQueue responses(*queues, connection);
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        FakeAuthentication authentication;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        auto joined = composition.join(
            id<PrincipalId>(4), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{});
        assert(joined.result == JoinCompositionResult::Committed && joined.committed
            && joined.committed->session == id<SessionId>(1));
        assert(joins.liveBindings() == 1);

        JoinFixture rejectedJoinFixture;
        auto& rejectedJoins = rejectedJoinFixture.joins;
        TransportJoinResponseQueue missing(*queues, *connection.next());
        AuthenticatedJoinComposition rejectedComposition(rejectedJoins, authentication, missing);
        assert(rejectedComposition
                   .join(id<PrincipalId>(5), SessionGeneration::initial(), ServerTick::initial(), ResumeTokenContext{})
                   .result
            == JoinCompositionResult::QueueRejected);
        assert(rejectedJoins.liveBindings() == 0 && rejectedJoins.state().players().empty());
    }
    {
        const auto combatConfig = parsedConfig();
        const std::array<ItemPrototypeDeclaration, 0> noItems{};
        auto itemCatalog = *ItemPrototypeCatalog::create(combatConfig.contentManifest, noItems);
        auto inventory = *CanonicalInventoryWorld::create(combatConfig.contentManifest, itemCatalog, {}, {});
        const auto randomKey = *RandomStreamKey::fromValues(5, 0);
        auto combat = std::get<CanonicalCombatWorld>(createCanonicalCombatWorld({}, {},
            Xoshiro256StarStar::fromWorldSeed(42, randomKey).snapshot()));
        CanonicalPlayerCombatTemplate playerTemplate;
        playerTemplate.stats.strength = 45.f;
        playerTemplate.stats.fatigue = 80.f;
        playerTemplate.maximumEncumbranceWeightUnits = 200;

        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 1);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(clock, observability, timeouts, emptyOffer(), authentication, *queues,
            1, nullptr, nullptr, &inventory, &combat, &playerTemplate, &itemCatalog);
        JoinFixture fixture;
        const auto connection = TransportConnectionId::initial();
        assert(sessions.accept(connection, scope(std::byte{ 6 })) == ConnectionSessionResult::Accepted);
        const auto hello = std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::SessionControl,
            MessageKind::ClientHello, encodeClientHello(ClientHello::fromOffer(emptyOffer()))));
        assert(sessions.dispatch(connection, { TransportChannel::ReliableOrdered, hello }, fixture.joins, crypto,
                   ServerTick::initial()) == ConnectionSessionResult::Accepted);
        auto material = AuthenticationMaterial::create({});
        const auto authenticationRequest = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::AuthenticationRequest,
            encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*material)))));
        assert(sessions.dispatch(connection, { TransportChannel::ReliableOrdered, authenticationRequest },
                   fixture.joins, crypto, ServerTick::initial()) == ConnectionSessionResult::Joined);
        const auto* joinedInventory = inventory.findPlayer(id<PlayerId>(1));
        const auto* joinedCombat = combat.findPlayer(id<PlayerId>(1));
        assert(joinedInventory && joinedCombat && joinedCombat->stats.strength == 45.f
            && joinedCombat->stats.fatigue == 80.f && joinedCombat->stats.normalizedEncumbrance == 0.f);

        auto rejectedInventory
            = *CanonicalInventoryWorld::create(combatConfig.contentManifest, itemCatalog, {}, {});
        auto rejectedCombat = std::get<CanonicalCombatWorld>(createCanonicalCombatWorld({}, {},
            Xoshiro256StarStar::fromWorldSeed(43, randomKey).snapshot()));
        auto narrowQueues = OutboundQueueSet::create(
            *OutboundQueuePolicy::create(1, 64 * 1024, 4, 2, 4, 1, 1, 1, 3, 100), 1);
        ConnectionSessionCoordinator rejectedSessions(clock, observability, timeouts, emptyOffer(), authentication,
            *narrowQueues, 1, nullptr, nullptr, &rejectedInventory, &rejectedCombat, &playerTemplate, &itemCatalog);
        JoinFixture rejectedFixture;
        assert(rejectedSessions.accept(connection, scope(std::byte{ 7 })) == ConnectionSessionResult::Accepted);
        assert(rejectedSessions.dispatch(connection, { TransportChannel::ReliableOrdered, hello }, rejectedFixture.joins,
                   crypto, ServerTick::initial()) == ConnectionSessionResult::Accepted);
        FakeRuntime drain;
        assert(narrowQueues->pump(drain, connection, 0) == OutboundPumpResult::Progress);
        auto rejectedMaterial = AuthenticationMaterial::create({});
        const auto rejectedRequest = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::AuthenticationRequest,
            encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*rejectedMaterial)))));
        assert(rejectedSessions.dispatch(connection, { TransportChannel::ReliableOrdered, rejectedRequest },
                   rejectedFixture.joins, crypto, ServerTick::initial()) == ConnectionSessionResult::ProtocolRejected);
        assert(rejectedFixture.joins.state().players().empty() && rejectedInventory.players().empty()
            && rejectedCombat.players().empty());
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(clock, observability, timeouts, emptyOffer(), authentication, *queues, 1);
        const auto first = TransportConnectionId::initial();
        const auto second = *first.next();
        assert(sessions.accept(first, scope(std::byte{ 1 })) == ConnectionSessionResult::Accepted);
        assert(sessions.size() == 1 && queues->connections() == 1);
        assert(sessions.session(first)->state() == ServerSessionState::AwaitingClientHello);
        assert(*sessions.admissionScope(first) == scope(std::byte{ 1 }));
        assert(sessions.accept(first, scope(std::byte{ 2 })) == ConnectionSessionResult::Duplicate);
        assert(sessions.accept(second, scope(std::byte{ 2 })) == ConnectionSessionResult::AtCapacity);
        assert(sessions.close(first) == ConnectionSessionResult::Accepted);
        assert(sessions.size() == 0 && queues->connections() == 0);
        assert(sessions.close(first) == ConnectionSessionResult::UnknownConnection);
        assert(sessions.accept(second, scope(std::byte{ 2 })) == ConnectionSessionResult::Accepted);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, locomotionOffer(), authentication, *queues, 2);
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        const auto connection = TransportConnectionId::initial();
        assert(sessions.accept(connection, scope(std::byte{ 4 })) == ConnectionSessionResult::Accepted);

        const auto helloPayload = encodeClientHello(ClientHello::fromOffer(locomotionOffer()));
        const auto helloFrame = std::get<std::vector<std::byte>>(
            encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload));
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, helloFrame }, joins,
                   crypto, ServerTick::initial())
            == ConnectionSessionResult::Accepted);

        auto material = AuthenticationMaterial::create({});
        const auto authenticationPayload
            = encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*material)));
        const auto authenticationFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::AuthenticationRequest, authenticationPayload));
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, authenticationFrame },
                   joins, crypto, ServerTick::initial())
            == ConnectionSessionResult::Joined);
        assert(joins.liveBindings() == 1 && sessions.session(connection)->sessionId() == id<SessionId>(1));

        FakeRuntime runtime;
        assert(queues->pump(runtime, connection, 0) == OutboundPumpResult::Progress);
        assert(runtime.sent.size() == 4);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[0])).messageKind() == MessageKind::ServerHello);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[1])).messageKind()
            == MessageKind::AuthenticationAccepted);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[2])).messageKind()
            == MessageKind::ReliableInterestBaseline);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[3])).messageKind()
            == MessageKind::LatestWinsSnapshot);

        const SessionResyncRequest resync(id<SessionId>(1), SessionGeneration::initial(), ResyncReason::LocalFeedGap,
            joinFixture.reducer.stateVersion());
        const auto resyncFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::SessionResyncRequest, encodeSessionResyncRequest(resync)));
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, resyncFrame }, joins,
                   crypto, ServerTick::initial())
            == ConnectionSessionResult::ResyncRequested);
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, resyncFrame }, joins,
                   crypto, ServerTick::initial())
            == ConnectionSessionResult::ResyncCoalesced);
        assert(sessions.takeResyncRequest(connection) == resync);
        assert(!sessions.takeResyncRequest(connection));

        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        const ReliableOperationHeader operationHeader(
            ClientCommandHeader(id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(1), id<CanonicalRevision>(1)),
            EntityPrecondition(id<EntityId>(1), EntityRevision::initial(), AuthorityEpoch::initial()));
        const PlayerLocomotionInput locomotion(*LocomotionInputTick::fromValue(1), LocomotionInputSequence::initial(),
            LocomotionIntent(LocomotionMode::Run, Turn32::fromValue(7), LinearVelocity3(100, 0, 0)));
        const auto operation = std::get<ReliableOperation>(ReliableOperation::create(operationHeader, locomotion));
        const auto operationFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::ReliableOperation, MessageKind::ReliableOperation, encodeReliableOperation(operation)));
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, operationFrame },
                   joins, crypto, intake, ServerTick::initial())
            == ConnectionSessionResult::CommandSubmitted);
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, operationFrame },
                   joins, crypto, intake, ServerTick::initial())
            == ConnectionSessionResult::ProtocolRejected);

        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::LatestWins, { std::byte{ 1 } } },
                   joins, crypto, ServerTick::initial())
            == ConnectionSessionResult::ProtocolRejected);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, actorOffer(), authentication, *queues, 2, &actors);
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        auto lifecycle
            = ServerLifecycleCoordinator::create(config.disconnectGraceMilliseconds * 1'000'000, joinFixture.reducer);
        assert(lifecycle);
        FakeRuntime incompleteRuntime;
        ServerApplicationWiring incompleteWiring{
            sessions, joins, crypto, *queues, clock, intake, joinFixture.reducer, *lifecycle };
        incompleteWiring.actorCatalog = &actorCatalog;
        incompleteWiring.actors = &actors;
        incompleteWiring.actorCollision = collision.get();
        incompleteWiring.meleeWeapons = &combat.weapons;
        ServerApplication incomplete(incompleteRuntime, config, incompleteWiring);
        assert(incomplete.start());
        assert(!incomplete.pump(ServerTick::initial()) && incomplete.failure() == "combat composition incomplete");

        FakeRuntime wiredRuntime;
        const auto connection = TransportConnectionId::initial();
        wiredRuntime.events.push_back(
            { TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt, std::nullopt, connection,
                std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 8 }) });
        const auto helloPayload = encodeClientHello(ClientHello::fromOffer(actorOffer()));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)) });
        ServerApplication wired(wiredRuntime, config,
            ServerApplicationWiring{ sessions, joins, crypto, *queues, clock, intake, joinFixture.reducer, *lifecycle,
                &actorCatalog, &actors, collision.get() });
        assert(wired.start() && wired.pump(ServerTick::initial()));
        assert(sessions.size() == 1 && wiredRuntime.sent.size() == 1);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(wiredRuntime.sent[0])).messageKind()
            == MessageKind::ServerHello);

        auto material = AuthenticationMaterial::create({});
        const auto authenticationPayload
            = encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*material)));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(
                MessageClass::SessionControl, MessageKind::AuthenticationRequest, authenticationPayload)) });
        assert(wired.pump(ServerTick::initial()));
        assert(lifecycle->liveCount() == 1 && joinFixture.reducer.state().activeSessions().size() == 1);
        assert(actors.find(id<ActorId>(1))->root().position() == Position3(10, 20, 30));
        clock.nanoseconds = 34'000'000;
        assert(wired.pump(id<ServerTick>(1)));
        assert(actors.find(id<ActorId>(1))->root().position() == Position3(10, 25, 30));
        bool sawActorBaseline = false;
        bool sawActorView = false;
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(frame))
                continue;
            sawActorBaseline = sawActorBaseline
                || std::get<DecodedFrame>(frame).messageKind() == MessageKind::ReliableActorInterestBaseline;
            sawActorView
                = sawActorView || std::get<DecodedFrame>(frame).messageKind() == MessageKind::LatestWinsActorSnapshot;
        }
        assert(sawActorBaseline && sawActorView);

        const auto movedRevision = actors.find(id<ActorId>(1))->revision();
        clock.nanoseconds = 100'000'000;
        assert(wired.pump(id<ServerTick>(2)));
        assert(actors.find(id<ActorId>(1))->activity() == ActorActivity::Idle
            && actors.find(id<ActorId>(1))->revision() > movedRevision);

        const auto publicationBeforeResync = joinFixture.reducer.latestPublication();
        assert(publicationBeforeResync);
        const SessionResyncRequest resync(id<SessionId>(1), SessionGeneration::initial(), ResyncReason::LocalFeedGap,
            publicationBeforeResync->stateVersion());
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::SessionControl,
                MessageKind::SessionResyncRequest, encodeSessionResyncRequest(resync))) });
        wiredRuntime.sent.clear();
        assert(wired.pump(id<ServerTick>(3)));
        for (std::uint64_t now = 101; now < 109; ++now)
        {
            const auto drained = queues->pump(wiredRuntime, connection, now);
            assert(drained && *drained != OutboundPumpResult::TransportFailed
                && *drained != OutboundPumpResult::InvalidTime);
            if (*drained == OutboundPumpResult::Idle)
                break;
        }
        bool sawCheckpointPlayerBaseline = false;
        bool sawCheckpointPlayerView = false;
        bool sawCurrentActorView = false;
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto decoded = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(decoded))
                continue;
            const auto& value = std::get<DecodedFrame>(decoded);
            if (value.messageKind() == MessageKind::ReliableInterestBaseline)
            {
                const auto baseline = decodeReliableInterestBaseline(value.payload());
                const auto* accepted = std::get_if<ReliableInterestBaseline>(&baseline);
                sawCheckpointPlayerBaseline
                    = accepted && accepted->serverTick() == publicationBeforeResync->checkpointTick();
            }
            else if (value.messageKind() == MessageKind::LatestWinsSnapshot)
            {
                const auto snapshot = decodeLatestWinsSnapshot(value.payload());
                const auto* accepted = std::get_if<LatestWinsSnapshot>(&snapshot);
                sawCheckpointPlayerView = accepted && !accepted->view().entries().empty()
                    && accepted->view().entries().front().serverTick() == publicationBeforeResync->checkpointTick();
            }
            else if (value.messageKind() == MessageKind::LatestWinsActorSnapshot)
            {
                const auto snapshot = decodeLatestWinsActorSnapshot(value.payload());
                const auto* accepted = std::get_if<LatestWinsActorSnapshot>(&snapshot);
                sawCurrentActorView = accepted != nullptr;
            }
        }
        assert(sawCheckpointPlayerBaseline && sawCheckpointPlayerView && sawCurrentActorView);

        wiredRuntime.events.push_back({ TransportEventKind::ConnectionClosed, TransportFailure::None, std::nullopt,
            std::nullopt, connection, std::nullopt, TransportSecurity::EncryptedUnauthenticated, std::nullopt });
        assert(wired.pump(id<ServerTick>(4)));
        assert(sessions.size() == 0 && queues->connections() == 0);
        assert(lifecycle->liveCount() == 0 && lifecycle->hiddenCount() == 1);
        assert(joinFixture.reducer.state().activeSessions().empty());
        assert(joinFixture.reducer.state().players().size() == 1);

        clock.nanoseconds = (config.disconnectGraceMilliseconds + 100) * 1'000'000 - 1;
        assert(wired.pump(id<ServerTick>(5)));
        assert(lifecycle->hiddenCount() == 1 && joinFixture.reducer.state().players().size() == 1);

        clock.nanoseconds = (config.disconnectGraceMilliseconds + 100) * 1'000'000;
        assert(wired.pump(id<ServerTick>(6)));
        assert(lifecycle->hiddenCount() == 0 && joinFixture.reducer.state().players().empty());
        const auto publication = joinFixture.reducer.latestPublication();
        assert(publication && publication->sessionLifecycle().size() == 1
            && publication->sessionLifecycle()[0].kind == CanonicalSessionLifecycleKind::Expired);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(clock, observability, timeouts, poseOffer(), authentication, *queues, 2);
        JoinFixture joinFixture;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        auto lifecycle
            = ServerLifecycleCoordinator::create(config.disconnectGraceMilliseconds * 1'000'000, joinFixture.reducer);
        assert(lifecycle);
        FakeRuntime runtime;
        const auto first = id<TransportConnectionId>(1);
        const auto second = id<TransportConnectionId>(2);
        runtime.events
            = { { TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt, std::nullopt, first,
                    std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 1 }) },
                  { TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt, std::nullopt, second,
                      std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 2 }) } };
        const auto hello = TransportMessage{ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello,
                encodeClientHello(ClientHello::fromOffer(poseOffer())))) };
        runtime.incomingByConnection[first].push_back(hello);
        runtime.incomingByConnection[second].push_back(hello);
        ServerApplication application(runtime, config,
            ServerApplicationWiring{
                sessions, joinFixture.joins, crypto, *queues, clock, intake, joinFixture.reducer, *lifecycle });
        assert(application.start() && application.pump(ServerTick::initial()));

        const auto authenticationPayload
            = encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*AuthenticationMaterial::create({}))));
        const auto authenticationFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::AuthenticationRequest, authenticationPayload));
        runtime.incomingByConnection[first].push_back({ TransportChannel::ReliableOrdered, authenticationFrame });
        runtime.incomingByConnection[second].push_back({ TransportChannel::ReliableOrdered, authenticationFrame });
        runtime.sent.clear();
        runtime.sentConnections.clear();
        runtime.sentChannels.clear();
        assert(application.pump(ServerTick::initial()));

        std::size_t firstSnapshots = 0;
        for (std::size_t index = 0; index < runtime.sent.size(); ++index)
        {
            if (runtime.sentConnections[index] != first || runtime.sentChannels[index] != TransportChannel::LatestWins)
                continue;
            const auto frame = decodeProtocolFrame(runtime.sent[index]);
            assert(std::holds_alternative<DecodedFrame>(frame));
            const auto snapshot = decodeLatestWinsSnapshot(std::get<DecodedFrame>(frame).payload());
            assert(std::holds_alternative<LatestWinsSnapshot>(snapshot));
            assert(std::get<LatestWinsSnapshot>(snapshot).view().entries().size() == 2);
            ++firstSnapshots;
        }
        assert(firstSnapshots == 1);

        runtime.sent.clear();
        runtime.sentConnections.clear();
        runtime.sentChannels.clear();
        const auto zero = Turn32::fromValue(0);
        const auto tracked = VrTrackedTransform(*VrPoseOffset3::create(10, 20, 30), Orientation3(zero, zero, zero));
        const auto sample = ClientVrPoseSample(id<SessionId>(1), SessionGeneration::initial(), id<EntityId>(1),
            AuthorityEpoch::initial(), PoseSampleSequence::initial(), tracked, std::nullopt, std::nullopt);
        runtime.incomingByConnection[first].push_back({ TransportChannel::PresentationLatest,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::PresentationSample,
                MessageKind::ClientVrPoseSample, encodeClientVrPoseSample(sample))) });
        assert(application.pump(id<ServerTick>(1)));
        std::size_t relayed = 0;
        for (std::size_t index = 0; index < runtime.sent.size(); ++index)
        {
            if (runtime.sentConnections[index] != second
                || runtime.sentChannels[index] != TransportChannel::PresentationLatest)
                continue;
            const auto poseFrame = decodeProtocolFrame(runtime.sent[index]);
            assert(std::holds_alternative<DecodedFrame>(poseFrame));
            const auto pose = decodeServerVrPoseSnapshot(std::get<DecodedFrame>(poseFrame).payload());
            assert(std::holds_alternative<ServerVrPoseSnapshot>(pose));
            const auto& value = std::get<ServerVrPoseSnapshot>(pose);
            assert(value.targetSessionId() == id<SessionId>(2) && value.sourcePlayerId() == id<PlayerId>(1)
                && value.rootEntityId() == id<EntityId>(1) && value.sampleSequence() == PoseSampleSequence::initial());
            ++relayed;
        }
        assert(relayed == 1);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        const auto zero = Turn32::fromValue(0);
        const auto interior = CellId::interior(id<CellSpaceId>(7));
        std::vector<CanonicalPlayerEntityState> players{ { id<PlayerId>(1), id<EntityId>(1), id<AppearanceId>(1),
            Transform(interior, Position3(10, 20, 30), Orientation3(zero, zero, zero)), LinearVelocity3(2, -3, 4),
            id<EntityRevision>(1), AuthorityEpoch::initial(), ServerTick::initial() } };
        std::vector<CanonicalSessionProgress> progress{ { id<SessionId>(1), SessionGeneration::initial(),
            id<PlayerId>(1), id<EntityId>(1), std::nullopt } };
        CanonicalCommandReducer reducer(
            std::get<CanonicalServerState>(createCanonicalServerState(players, progress)), observability);
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        const auto batches = intake.pump();
        assert(batches && batches.batches().size() == 1);
        auto prepared = reducer.prepareTick(batches.batches()[0]);
        assert(prepared.result());
        const auto* moved = prepared.candidateState().findPlayer(id<PlayerId>(1));
        assert(moved && moved->transform().position() == Position3(12, 17, 34)
            && moved->linearVelocity() == LinearVelocity3(2, -3, 4)
            && moved->entityRevision() == id<EntityRevision>(2));
        assert(reducer.commit(std::move(prepared)));
        auto views = projectInterestViews(reducer.state(), ServerTick::initial(), reducer.canonicalRevision());
        assert(views && views->size() == 1 && (*views)[0].second.view().entries().size() == 1
            && (*views)[0].second.view().entries()[0].transform().position() == Position3(12, 17, 34)
            && (*views)[0].second.view().entries()[0].locomotionMode() == LocomotionMode::Walk);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        const auto zero = Turn32::fromValue(0);
        const auto interior = CellId::interior(id<CellSpaceId>(7));
        std::vector<CanonicalPlayerEntityState> players{
            { id<PlayerId>(1), id<EntityId>(1), id<AppearanceId>(1),
                Transform(interior, Position3(std::numeric_limits<std::int64_t>::max(), 0, 0),
                    Orientation3(zero, zero, zero)),
                LinearVelocity3(1, 0, 0), id<EntityRevision>(1), AuthorityEpoch::initial(), ServerTick::initial() },
            { id<PlayerId>(2), id<EntityId>(2), id<AppearanceId>(1),
                Transform(interior, Position3(5, 0, 0), Orientation3(zero, zero, zero)), LinearVelocity3(1, 0, 0),
                id<EntityRevision>(1), AuthorityEpoch::initial(), ServerTick::initial() }
        };
        std::vector<CanonicalSessionProgress> progress{ { id<SessionId>(1), SessionGeneration::initial(),
                                                            id<PlayerId>(1), id<EntityId>(1), std::nullopt },
            { id<SessionId>(2), SessionGeneration::initial(), id<PlayerId>(2), id<EntityId>(2), std::nullopt } };
        CanonicalCommandReducer reducer(
            std::get<CanonicalServerState>(createCanonicalServerState(players, progress)), observability);
        const auto before = reducer.state();
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        const auto batches = intake.pump();
        auto prepared = reducer.prepareTick(batches.batches()[0]);
        assert(!prepared.result() && prepared.result().error() == CommandBatchReductionError::SpatialIntegrationOverflow
            && reducer.state() == before);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);

        const auto objectCatalog = sampleObjectCatalog(config.contentManifest);
        auto initialObjects = createInitialCanonicalInteractiveObjectWorld(objectCatalog);
        assert(std::holds_alternative<CanonicalInteractiveObjectWorld>(initialObjects));
        auto objects = std::get<CanonicalInteractiveObjectWorld>(std::move(initialObjects));

        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, objectOffer(), authentication, *queues, 2, nullptr, &objects);
        JoinFixture joinFixture;
        auto& joins = joinFixture.joins;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        auto lifecycle
            = ServerLifecycleCoordinator::create(config.disconnectGraceMilliseconds * 1'000'000, joinFixture.reducer);
        assert(lifecycle);
        FakeRuntime wiredRuntime;
        const auto connection = TransportConnectionId::initial();
        wiredRuntime.events.push_back(
            { TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt, std::nullopt, connection,
                std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 8 }) });
        const auto helloPayload = encodeClientHello(ClientHello::fromOffer(objectOffer()));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)) });

        ServerApplication wired(wiredRuntime, config,
            ServerApplicationWiring{ sessions, joins, crypto, *queues, clock, intake, joinFixture.reducer, *lifecycle,
                nullptr, nullptr, nullptr, &objectCatalog, &objects });
        assert(wired.start() && wired.pump(ServerTick::initial()));
        assert(sessions.size() == 1 && wiredRuntime.sent.size() == 1);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(wiredRuntime.sent[0])).messageKind()
            == MessageKind::ServerHello);

        auto material = AuthenticationMaterial::create({});
        const auto authenticationPayload
            = encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*material)));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(
                MessageClass::SessionControl, MessageKind::AuthenticationRequest, authenticationPayload)) });
        assert(wired.pump(ServerTick::initial()));
        assert(lifecycle->liveCount() == 1 && joinFixture.reducer.state().activeSessions().size() == 1);

        // Verify initial join received ReliableInteractiveObjectInterestBaseline for spawn cell (interior:7)
        bool sawObjectBaseline = false;
        ObjectRevision initialDoorRevision = ObjectRevision::initial();
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(frame))
                continue;
            if (std::get<DecodedFrame>(frame).messageKind() == MessageKind::ReliableInteractiveObjectInterestBaseline)
            {
                auto decoded = decodeReliableInteractiveObjectInterestBaseline(std::get<DecodedFrame>(frame).payload());
                assert(std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(decoded));
                const auto& baseline = std::get<ReliableInteractiveObjectInterestBaseline>(decoded);
                assert(baseline.members().size() == 1);
                assert(baseline.members()[0].objectId == id<InteractiveObjectId>(1));
                assert(baseline.members()[0].doorState == DoorState::Closed);
                initialDoorRevision = baseline.members()[0].revision;
                sawObjectBaseline = true;
            }
        }
        assert(sawObjectBaseline);

        // Key possession has no authoritative inventory source until Phase 15.
        // Reject key-unlock commands at intake instead of treating an empty key set as verification.
        const ClientInteractObjectCommand deferredUnlock{ .sessionId = id<SessionId>(1),
            .sessionGeneration = SessionGeneration::initial(),
            .commandSequence = id<CommandSequence>(1),
            .commandId = id<CommandId>(99),
            .observedCanonicalRevision = joinFixture.reducer.canonicalRevision(),
            .objectId = id<InteractiveObjectId>(1),
            .targetCell = CellId::interior(id<CellSpaceId>(7)),
            .interactionOrigin = Position3(-10, 20, 30),
            .expectedRevision = initialDoorRevision,
            .kind = ObjectInteractionKind::UnlockWithKey,
            .requestedKey = id<KeyPrototypeId>(1) };
        const auto deferredUnlockFrame
            = std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ClientInteractObjectCommand, encodeClientInteractObjectCommand(deferredUnlock)));
        assert(sessions.dispatch(connection, TransportMessage{ TransportChannel::ReliableOrdered, deferredUnlockFrame },
                   joins, crypto, intake, ServerTick::initial())
            == ConnectionSessionResult::ProtocolRejected);

        // Interaction command: player in cell 7 activates door 1
        wiredRuntime.sent.clear();
        ClientInteractObjectCommand interactCmd{ .sessionId = id<SessionId>(1),
            .sessionGeneration = SessionGeneration::initial(),
            .commandSequence = id<CommandSequence>(1),
            .commandId = id<CommandId>(1),
            .observedCanonicalRevision = joinFixture.reducer.canonicalRevision(),
            .objectId = id<InteractiveObjectId>(1),
            .targetCell = CellId::interior(id<CellSpaceId>(7)),
            .interactionOrigin = Position3(-10, 20, 30),
            .expectedRevision = initialDoorRevision,
            .kind = ObjectInteractionKind::Activate,
            .requestedKey = std::nullopt };
        const auto interactFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::ReliableOperation,
            MessageKind::ClientInteractObjectCommand, encodeClientInteractObjectCommand(interactCmd)));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered, interactFrame });

        clock.nanoseconds = 34'000'000;
        assert(wired.pump(id<ServerTick>(1)));

        // Verify that door 1 in objects is now Open with advanced revision
        const auto* doorObj = objects.find(id<InteractiveObjectId>(1));
        assert(doorObj && doorObj->doorState() == DoorState::Open);
        assert(doorObj->revision() > initialDoorRevision);
        const auto* finalizedInteraction = joinFixture.reducer.state().findActiveSession(interactCmd.sessionId);
        const auto interactionPublication = joinFixture.reducer.latestPublication();
        assert(finalizedInteraction
            && finalizedInteraction->highestContiguousFinalizedCommand() == interactCmd.commandSequence
            && interactionPublication && interactionPublication->changes().size() == 1
            && interactionPublication->changes()[0].commandId() == interactCmd.commandId
            && interactionPublication->changes()[0].objectInteractionOutcome()
            && interactionPublication->changes()[0].objectInteractionOutcome()->code
                == ObjectInteractionResultCode::Success);

        // Drain queue so messages reach wiredRuntime.sent
        for (std::uint64_t now = 35; now < 50; ++now)
        {
            const auto drained = queues->pump(wiredRuntime, connection, now);
            assert(drained && *drained != OutboundPumpResult::TransportFailed);
            if (*drained == OutboundPumpResult::Idle)
                break;
        }

        // Verify client received updated baseline with doorState == DoorState::Open
        bool sawUpdatedDoor = false;
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(frame))
                continue;
            if (std::get<DecodedFrame>(frame).messageKind() == MessageKind::ReliableInteractiveObjectInterestBaseline)
            {
                auto decoded = decodeReliableInteractiveObjectInterestBaseline(std::get<DecodedFrame>(frame).payload());
                if (std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(decoded))
                {
                    const auto& baseline = std::get<ReliableInteractiveObjectInterestBaseline>(decoded);
                    if (!baseline.members().empty() && baseline.members()[0].objectId == id<InteractiveObjectId>(1)
                        && baseline.members()[0].doorState == DoorState::Open)
                    {
                        sawUpdatedDoor = true;
                    }
                }
            }
        }
        assert(sawUpdatedDoor);

        // Scenario 11: Unloaded cell preserves canonical object modifications without ticking
        // Player disconnects, cell 7 is now empty
        const auto closedRevision = doorObj->revision();
        wiredRuntime.events.push_back({ TransportEventKind::ConnectionClosed, TransportFailure::None, std::nullopt,
            std::nullopt, connection, std::nullopt, TransportSecurity::EncryptedUnauthenticated, std::nullopt });
        assert(wired.pump(id<ServerTick>(2)));
        assert(sessions.size() == 0);
        // Advance several ticks with cell 7 completely empty
        clock.nanoseconds = 100'000'000;
        assert(wired.pump(id<ServerTick>(3)));
        clock.nanoseconds = 200'000'000;
        assert(wired.pump(id<ServerTick>(4)));
        // Verify object 1 in cell 7 still preserves DoorState::Open without ticking
        const auto* preserved = objects.find(id<InteractiveObjectId>(1));
        assert(preserved && preserved->doorState() == DoorState::Open && preserved->revision() == closedRevision);

        // Scenario 12: Late join and resync receive complete cell interactive object baseline
        // Second player joins into cell 7
        const auto conn2 = id<TransportConnectionId>(2);
        wiredRuntime.events.push_back({ TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt,
            std::nullopt, conn2, std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 9 }) });
        wiredRuntime.incomingByConnection[conn2].push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)) });
        wiredRuntime.sent.clear();
        assert(wired.pump(id<ServerTick>(5)));

        auto mat2 = AuthenticationMaterial::create({});
        const auto auth2Payload = encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*mat2)));
        wiredRuntime.incomingByConnection[conn2].push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::AuthenticationRequest, auth2Payload)) });
        assert(wired.pump(id<ServerTick>(5)));

        // Late joiner conn2 receives baseline reflecting modified DoorState::Open!
        bool lateJoinSawModified = false;
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(frame))
                continue;
            if (std::get<DecodedFrame>(frame).messageKind() == MessageKind::ReliableInteractiveObjectInterestBaseline)
            {
                auto decoded = decodeReliableInteractiveObjectInterestBaseline(std::get<DecodedFrame>(frame).payload());
                if (std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(decoded))
                {
                    const auto& baseline = std::get<ReliableInteractiveObjectInterestBaseline>(decoded);
                    if (!baseline.members().empty() && baseline.members()[0].objectId == id<InteractiveObjectId>(1)
                        && baseline.members()[0].doorState == DoorState::Open)
                    {
                        lateJoinSawModified = true;
                    }
                }
            }
        }
        assert(lateJoinSawModified);

        // Resync test: conn2 requests resync
        const auto pubBeforeResync = joinFixture.reducer.latestPublication();
        assert(pubBeforeResync);
        const auto sess2Id = sessions.session(conn2)->sessionId();
        assert(sess2Id);
        const SessionResyncRequest resyncReq(
            *sess2Id, SessionGeneration::initial(), ResyncReason::LocalFeedGap, pubBeforeResync->stateVersion());
        wiredRuntime.incomingByConnection[conn2].push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::SessionControl,
                MessageKind::SessionResyncRequest, encodeSessionResyncRequest(resyncReq))) });
        wiredRuntime.sent.clear();
        assert(wired.pump(id<ServerTick>(6)));

        for (std::uint64_t now = 201; now < 215; ++now)
        {
            const auto drained = queues->pump(wiredRuntime, conn2, now);
            assert(drained && *drained != OutboundPumpResult::TransportFailed);
            if (*drained == OutboundPumpResult::Idle)
                break;
        }

        bool resyncSawModified = false;
        for (const auto& bytes : wiredRuntime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (!std::holds_alternative<DecodedFrame>(frame))
                continue;
            if (std::get<DecodedFrame>(frame).messageKind() == MessageKind::ReliableInteractiveObjectInterestBaseline)
            {
                auto decoded = decodeReliableInteractiveObjectInterestBaseline(std::get<DecodedFrame>(frame).payload());
                if (std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(decoded))
                {
                    const auto& baseline = std::get<ReliableInteractiveObjectInterestBaseline>(decoded);
                    if (!baseline.members().empty() && baseline.members()[0].objectId == id<InteractiveObjectId>(1)
                        && baseline.members()[0].doorState == DoorState::Open)
                    {
                        resyncSawModified = true;
                    }
                }
            }
        }
        assert(resyncSawModified);

        // Object state and its reliable publication commit together. Saturating
        // the target queue must leave the canonical door unchanged.
        const auto* secondSession = joinFixture.reducer.state().findActiveSession(*sess2Id);
        const auto* secondPlayer
            = secondSession ? joinFixture.reducer.state().findPlayer(secondSession->playerId()) : nullptr;
        assert(secondPlayer);
        const auto beforeRejectedPublish = *objects.find(id<InteractiveObjectId>(1));
        ClientInteractObjectCommand rejectedPublish{ .sessionId = *sess2Id,
            .sessionGeneration = SessionGeneration::initial(),
            .commandSequence = id<CommandSequence>(1),
            .commandId = id<CommandId>(2),
            .observedCanonicalRevision = joinFixture.reducer.canonicalRevision(),
            .objectId = id<InteractiveObjectId>(1),
            .targetCell = beforeRejectedPublish.cell(),
            .interactionOrigin = secondPlayer->transform().position(),
            .expectedRevision = beforeRejectedPublish.revision(),
            .kind = ObjectInteractionKind::Activate,
            .requestedKey = std::nullopt };
        const auto rejectedPublishFrame
            = std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ClientInteractObjectCommand, encodeClientInteractObjectCommand(rejectedPublish)));
        const std::array<std::byte, 1> filler{};
        for (std::size_t index = 0; index < OutboundQueuePolicy::MaxReliableMessages; ++index)
            assert(queues->enqueue(conn2, TransportChannel::ReliableOrdered, filler) == TransportResult::Accepted);
        wiredRuntime.incomingByConnection[conn2].push_back({ TransportChannel::ReliableOrdered, rejectedPublishFrame });
        clock.nanoseconds = 234'000'000;
        assert(!wired.pump(id<ServerTick>(7)));
        const auto* afterRejectedPublish = objects.find(id<InteractiveObjectId>(1));
        const auto* uncommittedInteraction = joinFixture.reducer.state().findActiveSession(*sess2Id);
        assert(afterRejectedPublish && *afterRejectedPublish == beforeRejectedPublish && uncommittedInteraction
            && !uncommittedInteraction->highestContiguousFinalizedCommand()
            && wired.failure() == "interactive object output admission failed");
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 1);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);

        const auto keyId = id<KeyPrototypeId>(1);
        const std::array prototypes{ ItemPrototypeDeclaration{
            id<ItemPrototypeId>(1), ItemCategory::Miscellaneous, 1, 1, 0, 0, 0, true, keyId } };
        auto catalogValue = ItemPrototypeCatalog::create(config.contentManifest, prototypes);
        assert(catalogValue);
        auto itemCatalog = std::move(*catalogValue);
        const auto spawnCell = CellId::interior(id<CellSpaceId>(7));
        const CanonicalItemStack keyStack{ id<ItemStackId>(10), id<ItemPrototypeId>(1), 1, 0, 0, std::nullopt };
        const std::array containers{ CanonicalContainerInventoryState{ id<ContainerId>(1), spawnCell,
            Position3(20, 20, 30), ContainerRevision::initial(), ServerTick::initial(), 0, { keyStack } } };
        auto inventoryValue = CanonicalInventoryWorld::create(config.contentManifest, itemCatalog, {}, containers);
        assert(inventoryValue);
        auto inventory = std::move(*inventoryValue);

        const auto zero = Turn32::fromValue(0);
        const std::array objectEntries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, spawnCell,
            Transform(spawnCell, Position3(30, 20, 30), Orientation3(zero, zero, zero)), std::nullopt,
            ObjectLockDeclaration{ true, 10, keyId }, ObjectTrapDeclaration{ false, std::nullopt } } };
        auto objectCatalogValue = InteractiveObjectCatalog::create(config.contentManifest, objectEntries);
        assert(objectCatalogValue);
        auto objectCatalog = std::move(*objectCatalogValue);
        auto objectsValue = createInitialCanonicalInteractiveObjectWorld(objectCatalog);
        assert(std::holds_alternative<CanonicalInteractiveObjectWorld>(objectsValue));
        auto objects = std::get<CanonicalInteractiveObjectWorld>(std::move(objectsValue));

        ConnectionSessionCoordinator sessions(clock, observability, timeouts, inventoryObjectOffer(), authentication,
            *queues, 1, nullptr, &objects, &inventory);
        JoinFixture fixture;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::initial(), IngressOrdinal::initial());
        auto lifecycle
            = ServerLifecycleCoordinator::create(config.disconnectGraceMilliseconds * 1'000'000, fixture.reducer);
        assert(lifecycle);
        FakeRuntime runtime;
        const auto connection = TransportConnectionId::initial();
        runtime.events.push_back(
            { TransportEventKind::ConnectionAccepted, TransportFailure::None, std::nullopt, std::nullopt, connection,
                std::nullopt, TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 12 }) });
        const auto hello = encodeClientHello(ClientHello::fromOffer(inventoryObjectOffer()));
        runtime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, hello)) });
        ServerApplication application(runtime, config,
            { sessions, fixture.joins, crypto, *queues, clock, intake, fixture.reducer, *lifecycle, nullptr, nullptr,
                nullptr, &objectCatalog, &objects, &itemCatalog, &inventory });
        assert(application.start() && application.pump(ServerTick::initial()));
        auto material = AuthenticationMaterial::create({});
        runtime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(
                encodeProtocolFrame(MessageClass::SessionControl, MessageKind::AuthenticationRequest,
                    encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*material))))) });
        assert(application.pump(ServerTick::initial()));
        assert(inventory.findPlayer(id<PlayerId>(1)));
        bool sawPrivateBaseline = false;
        for (const auto& bytes : runtime.sent)
        {
            const auto frame = decodeProtocolFrame(bytes);
            if (const auto* decoded = std::get_if<DecodedFrame>(&frame);
                decoded && decoded->messageKind() == MessageKind::ReliablePlayerInventoryBaseline)
            {
                auto baseline = decodeReliablePlayerInventoryBaseline(decoded->payload());
                sawPrivateBaseline = std::holds_alternative<ReliablePlayerInventoryBaseline>(baseline)
                    && std::get<ReliablePlayerInventoryBaseline>(baseline).player == id<PlayerId>(1);
            }
        }
        assert(sawPrivateBaseline);

        const ClientInventoryTransactionCommand take{ .sessionId = id<SessionId>(1),
            .sessionGeneration = SessionGeneration::initial(),
            .commandSequence = id<CommandSequence>(1),
            .commandId = id<CommandId>(1),
            .observedCanonicalRevision = fixture.reducer.canonicalRevision(),
            .kind = InventoryTransactionKind::TakeFromContainer,
            .containerId = id<ContainerId>(1),
            .prototypeId = id<ItemPrototypeId>(1),
            .stackId = id<ItemStackId>(10),
            .count = 1,
            .expectedInventoryRevision = InventoryRevision::initial(),
            .expectedContainerRevision = ContainerRevision::initial(),
            .interactionOrigin = Position3(20, 20, 30) };
        runtime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ClientInventoryTransactionCommand, encodeClientInventoryTransactionCommand(take))) });
        clock.nanoseconds = 34'000'000;
        assert(application.pump(id<ServerTick>(1)));
        assert(inventory.findPlayer(id<PlayerId>(1))->stacks.size() == 1
            && inventory.findContainer(id<ContainerId>(1))->stacks.empty());

        const ClientInteractObjectCommand unlock{ .sessionId = id<SessionId>(1),
            .sessionGeneration = SessionGeneration::initial(),
            .commandSequence = id<CommandSequence>(2),
            .commandId = id<CommandId>(2),
            .observedCanonicalRevision = fixture.reducer.canonicalRevision(),
            .objectId = id<InteractiveObjectId>(1),
            .targetCell = spawnCell,
            .interactionOrigin = Position3(30, 20, 30),
            .expectedRevision = ObjectRevision::initial(),
            .kind = ObjectInteractionKind::UnlockWithKey,
            .requestedKey = keyId };
        runtime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(MessageClass::ReliableOperation,
                MessageKind::ClientInteractObjectCommand, encodeClientInteractObjectCommand(unlock))) });
        clock.nanoseconds = 68'000'000;
        assert(application.pump(id<ServerTick>(2)));
        assert(objects.find(id<InteractiveObjectId>(1))->lockState() == LockState::Unlocked);
    }
}
