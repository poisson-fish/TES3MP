#include "inventory_service_tests.hpp"
#include "inventory_service.hpp"
#include "inventory_host.hpp"
#include "../canonical_persistence_file.hpp"
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/readerscache.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace TES3MP::ServerApp::Testing
{
    void nativeInventoryApplication(NativeInventoryService&, CanonicalPersistenceFile&);
}

namespace TES3MP::Native::Testing
{
    namespace
    {
        void require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
        template<class T> T id(uint64_t value) { return T::fromValue(value).value(); }
        struct Content
        {
            MWWorld::ESMStore store;
            ESM::ReadersCache readers;
            const ESM::RefId actor = ESM::RefId::stringRefId("service_actor");
            const ESM::RefId shirt = ESM::RefId::stringRefId("service_shirt");
            const ESM::RefId container = ESM::RefId::stringRefId("service_container");
            Content()
            {
                ESM::NPC npc; npc.blank(); npc.mId = actor; store.insertStatic(npc);
                ESM::Clothing item; item.blank(); item.mId = shirt;
                item.mData.mType = ESM::Clothing::Shirt; store.insertStatic(item);
                ESM::Container shared; shared.blank(); shared.mId = container; store.insertStatic(shared);
            }
            InventoryServiceBinding binding() const
            {
                return { { id<PlayerId>(11), id<PlayerId>(22) }, id<ItemPrototypeId>(70), id<ContainerId>(90),
                    CellId::interior(id<CellSpaceId>(7)), Position3(0, 0, 0),
                    {{{ actor, shirt, 3 }, { actor, shirt, -5 }}}, container, { 1, 2, 3 } };
            }
        };
        CanonicalServerState players(SessionGeneration generation = SessionGeneration::initial())
        {
            const auto zero = Turn32::fromValue(0);
            const Transform transform(CellId::interior(id<CellSpaceId>(7)), Position3(0, 0, 0), Orientation3(zero, zero, zero));
            const std::array<CanonicalPlayerEntityState, 2> actors{{
                { id<PlayerId>(11), id<EntityId>(111), id<AppearanceId>(1), transform, LinearVelocity3(0, 0, 0),
                    EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial() },
                { id<PlayerId>(22), id<EntityId>(222), id<AppearanceId>(1), transform, LinearVelocity3(0, 0, 0),
                    EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial() } }};
            const std::array<CanonicalSessionProgress, 2> sessions{{
                { id<SessionId>(1), generation, id<PlayerId>(11), id<EntityId>(111), std::nullopt },
                { id<SessionId>(2), generation, id<PlayerId>(22), id<EntityId>(222), std::nullopt } }};
            return std::get<CanonicalServerState>(createCanonicalServerState(actors, sessions));
        }
        struct Clock final : MonotonicClock
        {
            uint64_t value = 0;
            MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(value); }
        };
        std::unique_ptr<ClientSessionStateMachine> client(Clock& clock, uint64_t session, SessionGeneration generation)
        {
            auto timeouts = SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000).value();
            auto result = std::get<std::unique_ptr<ClientSessionStateMachine>>(ClientSessionStateMachine::create(clock, timeouts, generation));
            const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
            const std::array capabilities{ inventoryReplicationCapability() };
            auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
            auto hello = std::get<ServerHello>(negotiateClientHello(ClientHello::fromOffer(offer), offer));
            result->handle(ClientEncryptedTransportReady{});
            result->handle(ClientServerHelloReceived{ std::move(hello) });
            result->handle(ClientAuthenticationSubmitted{});
            result->handle(ClientAuthenticationAccepted{});
            require(result->bindEstablishedSession(id<SessionId>(session)) == ClientSessionBindingResult::Bound, "Synthetic client establishment failed");
            return result;
        }
        // Real queue/frame/client receive code, fake transport and authentication.
        // This is explicitly NOT socket, desktop presentation or two-client evidence.
        class Delivery final : public TransportRuntime
        {
        public:
            std::array<std::unique_ptr<ClientSessionStateMachine>, 2> clients;
            size_t sent = 0;
            Delivery(Clock& clock, SessionGeneration generation)
                : clients{ client(clock, 1, generation), client(clock, 2, generation) } {}
            TransportResult send(TransportConnectionId connection, TransportChannel, std::span<const std::byte> bytes) override
            {
                auto decoded = decodeProtocolFrame(bytes);
                auto& frame = std::get<DecodedFrame>(decoded);
                auto& target = *clients.at(connection.value() - 1);
                InventoryReplicationReceiveResult result;
                switch (frame.messageKind())
                {
                    case MessageKind::ReliablePlayerInventoryBaseline:
                        result = target.receiveReliablePlayerInventoryBaseline(std::get<ReliablePlayerInventoryBaseline>(decodeReliablePlayerInventoryBaseline(frame.payload()))); break;
                    case MessageKind::ReliableContainerInventoryBaseline:
                        result = target.receiveReliableContainerInventoryBaseline(std::get<ReliableContainerInventoryBaseline>(decodeReliableContainerInventoryBaseline(frame.payload()))); break;
                    case MessageKind::ReliableGroundItemBaseline:
                        result = target.receiveReliableGroundItemBaseline(std::get<ReliableGroundItemBaseline>(decodeReliableGroundItemBaseline(frame.payload()))); break;
                    case MessageKind::LatestWinsEquipmentSnapshot:
                        result = target.receiveLatestWinsEquipmentSnapshot(std::get<LatestWinsEquipmentSnapshot>(decodeLatestWinsEquipmentSnapshot(frame.payload()))); break;
                    default: throw std::runtime_error("Unexpected native inventory delivery kind");
                }
                require(result == InventoryReplicationReceiveResult::Applied, "Client rejected native inventory baseline");
                ++sent;
                return TransportResult::Accepted;
            }
            TransportAdmission<ListenerId> startListener(const ListenerEndpoint&) override { return { TransportResult::InvalidInput, {} }; }
            TransportResult stopListener(ListenerId) override { return TransportResult::InvalidInput; }
            TransportAdmission<ConnectAttemptId> connect(const ConnectionEndpoint&) override { return { TransportResult::InvalidInput, {} }; }
            TransportResult cancelConnect(ConnectAttemptId) override { return TransportResult::InvalidInput; }
            TransportReceiveResult receive(TransportConnectionId, std::span<TransportMessage>) override { return { TransportResult::Accepted, 0 }; }
            TransportResult close(TransportConnectionId, TransportCloseMode) override { return TransportResult::Accepted; }
            TransportPollResult poll(std::span<TransportEvent>) override { return { TransportResult::Accepted, 0 }; }
            TransportResult shutdown() override { return TransportResult::Accepted; }
        };
        void publish(InventoryService& service, const CanonicalServerState& authority, Delivery& delivery, uint64_t tick)
        {
            auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2).value();
            std::vector<std::vector<std::byte>> frames;
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            for (uint64_t i = 1; i <= 2; ++i)
            {
                require(queues.attach(id<TransportConnectionId>(i)) == TransportResult::Accepted, "Queue attach failed");
                auto view = service.project(authority, id<SessionId>(i), id<ServerTick>(tick), id<CanonicalRevision>(tick));
                require(view && ServerApp::appendInventoryInterestMessages(frames, messages, id<TransportConnectionId>(i), *view), "Native baseline projection failed");
            }
            require(queues.enqueueMessagesAtomically(messages) == TransportResult::Accepted, "Native baseline admission failed");
            for (uint64_t i = 1; i <= 2; ++i)
                require(queues.pump(delivery, id<TransportConnectionId>(i), tick) == OutboundPumpResult::Progress, "Native baseline queue pump failed");
        }
        ClientInventoryTransactionCommand wire(InventoryService& service, const CanonicalServerState& authority,
            uint64_t session, bool drop, uint32_t count)
        {
            auto view = service.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
            const auto& inventory = view.playerInventory.front();
            const auto& shared = view.containers.front();
            return { id<SessionId>(session), authority.findActiveSession(id<SessionId>(session))->sessionGeneration(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                drop ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                shared.container, id<ItemPrototypeId>(70), (drop ? inventory.stacks : shared.stacks).front().stackId,
                count, {}, inventory.revision, shared.revision, {}, Position3(0, 0, 0) };
        }
        ServerApp::InventoryCommandBinding bind(const CanonicalServerState& authority, const ClientInventoryTransactionCommand& input)
        {
            // Pass through the production wire decoder and trusted caller resolver.
            const auto decoded = std::get<ClientInventoryTransactionCommand>(decodeClientInventoryTransactionCommand(encodeClientInventoryTransactionCommand(input)));
            return ServerApp::InventoryCommandBinding::resolve(authority, input.sessionId, input.sessionGeneration, decoded).value();
        }
    }

    void checkInventoryHost(const std::filesystem::path& scratch, const std::filesystem::path& config)
    {
        require(std::filesystem::create_directory(scratch), "Native host scratch already exists");
        auto crypto = makeProductionCredentialCrypto();
        require(bool(crypto), "Native host crypto unavailable");
        struct Identities final : PlayerIdentityPersistence
        { bool replace(std::span<const PersistedPlayerIdentity>) noexcept override { return true; } } storage;
        CharacterDerivedState derived;
        derived.attributes.fill(40); derived.skills.fill(10);
        const auto profile = CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter,
            CharacterCreationPhase::Complete, "Native participant",
            CharacterAppearance{id<RaceRecordId>(1), id<HeadRecordId>(1), id<HairRecordId>(1), CharacterSex::Male},
            CharacterClass{id<ClassRecordId>(1)}, id<BirthsignRecordId>(1), derived, {}, id<CharacterProfileRevision>(2)).value();
        std::vector<PersistedPlayerIdentity> records;
        for (uint64_t i : {1, 2})
        {
            CredentialDigest digest; digest.bytes.fill(std::byte(i));
            const auto zero = Turn32::fromValue(0);
            CanonicalPlayerEntityState saved(id<PlayerId>(i), id<EntityId>(i), id<AppearanceId>(1),
                Transform(CellId::interior(id<CellSpaceId>(7)), Position3(0, 0, 0), Orientation3(zero, zero, zero)),
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial());
            records.push_back({{id<PlayerId>(i), id<EntityId>(i), id<AppearanceId>(1), testContentManifestId()}, digest, saved, profile});
        }
        auto registered = PlayerIdentityRegistry::create(*crypto, storage, records);
        require(std::holds_alternative<std::unique_ptr<PlayerIdentityRegistry>>(registered), "Native host test identities invalid");
        auto registry = std::get<std::unique_ptr<PlayerIdentityRegistry>>(std::move(registered));
        std::filesystem::create_directory(scratch / "openmw");
        std::filesystem::copy_file(config, scratch / "openmw" / "openmw.cfg");
        const auto descriptor = scratch / "native.txt";
        {
            std::ofstream out(descriptor);
            out << "native-inventory-1\nmanifest ";
            for (auto byte : testContentManifestId().bytes())
                out << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(byte);
            out << "\nconfig \"openmw\"\nplayers 1 2\nactors \"player\" 3 \"player\" 5\n"
                   "shirt \"common_shirt_01\" 70\ncontainer \"barrel_01\" 90\ncell interior:7\nposition 0 0 0\n";
        }
        std::array<std::byte, 32> configuration{}; configuration[0] = std::byte{1};
        const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(configuration).value(), {}, {}).value();
        const auto path = scratch / "host.bin";
        auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        std::vector<std::byte> committed;
        {
            InventoryHost host(descriptor, testContentManifest(), *registry, *crypto, {});
            ServerApp::Testing::nativeInventoryApplication(host.service(), *file);
            committed.assign(host.service().inventoryImage().begin(), host.service().inventoryImage().end());
        }
        auto opened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        InventoryHost restored(descriptor, testContentManifest(), *registry, *crypto, opened->prefix().latest()->nativeInventory());
        require(std::ranges::equal(committed, restored.service().inventoryImage()), "Real-loadout host recovery diverged");
        ServerApp::Testing::nativeInventoryApplication(restored.service(), *opened);
        std::cout << "real-loadout production host: player/player, common_shirt_01, barrel_01; synthetic transport, no desktop clients\n";
    }

    void checkInventoryApplication(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Native application scratch already exists");
        Content content;
        auto binding = content.binding();
        binding.mPlayers = {id<PlayerId>(1), id<PlayerId>(2)};
        InventoryService service(content.store, content.readers, binding);
        std::array<std::byte, 32> configuration{};
        configuration[0] = std::byte{1};
        const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(configuration).value(), {}, {}).value();
        const auto path = scratch / "application.bin";
        auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        ServerApp::Testing::nativeInventoryApplication(service, *file);
        auto opened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        InventoryService recovered(content.store, content.readers, binding, true);
        const std::array references{content.actor, content.shirt};
        recovered.recover(opened->prefix().latest()->nativeInventory(), references);
        require(std::ranges::equal(recovered.inventoryImage(), service.inventoryImage()),
            "ServerApplication recovery did not restore both actors and container coherently");
        ServerApp::Testing::nativeInventoryApplication(recovered, *opened);
    }

    void checkCanonicalInventory(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Canonical inventory scratch already exists");
        Content content;
        InventoryService service(content.store, content.readers, content.binding());
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(players(), observability);
        const auto catalog = ServerScriptStateCatalog::create({}).value();
        auto scripts = CanonicalScriptState::initial(catalog).value();
        std::array<std::byte, 32> configuration{};
        configuration[0] = std::byte{1};
        const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(configuration).value(), {}, catalog, {}).value();
        const auto path = scratch / "canonical.bin";
        auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        struct Port final : CanonicalDurabilityPort
        {
            ServerApp::CanonicalPersistenceFile& file;
            InventoryService& service;
            CanonicalCommandReducer& reducer;
            bool reject = true;
            size_t calls = 0;
            Port(ServerApp::CanonicalPersistenceFile& f, InventoryService& s, CanonicalCommandReducer& r)
                : file(f), service(s), reducer(r) {}
            CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
                CanonicalRevision revision, std::span<const DurableCommandOrder> commands,
                const CanonicalInventoryWorld* inventory, const CanonicalCombatWorld* combat,
                const CanonicalInteractiveObjectWorld* objects, const CanonicalActorWorld* actors,
                const CanonicalWorldState* world, const CanonicalScriptState* scripts,
                std::span<const std::byte> image) noexcept override
            {
                ++calls;
                if (inventory || image.empty() || reducer.latestPublication() == candidate)
                    return CanonicalDurabilityResult::Failed;
                if (reject) return CanonicalDurabilityResult::Rejected;
                return file.commit(candidate, revision, commands, inventory, combat, objects, actors, world, scripts, image);
            }
        } port(*file, service, reducer);
        require(reducer.configureDurability(port, nullptr, nullptr, nullptr, nullptr, nullptr, &scripts, &service), "Native reducer composition failed");
        Clock clock;
        auto prepare = [&](const ServerCommandProposal& proposal, uint64_t tick, const ServerCommandProposal* second = nullptr) {
            ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(tick), IngressOrdinal::initial());
            require(intake.submit(proposal) == CommandSubmissionResult::Accepted, "Native intake rejected proposal");
            if (second) require(intake.submit(*second) == CommandSubmissionResult::Accepted, "Native contender intake rejected");
            clock.value += tick * 33'333'334;
            auto pumped = intake.pump();
            require(pumped && pumped.batches().size() == 1, "Native intake failed to pump");
            return reducer.prepareTick(pumped.batches().front());
        };
        const auto input = bind(reducer.state(), wire(service, reducer.state(), 1, true, 2)).proposal();
        const auto contender = bind(reducer.state(), wire(service, reducer.state(), 2, true, 1)).proposal();
        const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
        const auto published = reducer.latestPublication();
        auto prepared = prepare(input, 1, &contender);
        auto stalePreparation = prepare(input, 1);
        require(prepared.result().dispositions()[0].disposition() == CommandDisposition::Applied,
            "Reducer did not prepare native inventory");
        require(prepared.result().dispositions()[1].disposition() == CommandDisposition::InventoryTransactionRejected,
            "Native same-tick contention was not rejected in ingress order");
        auto candidate = service.projectInventory(prepared.candidateState(), id<SessionId>(1), id<ServerTick>(1),
            prepared.candidateRevision(), prepared.candidateNativeInventory());
        require(candidate && candidate->containers[0].stacks[0].count == 2, "Native candidate projection missing");
        require(!reducer.commit(std::move(prepared)) && reducer.latestPublication() == published
            && std::ranges::equal(before, service.inventoryImage()) && !file->prefix().latest(),
            "Rejected joint durability installed or published");
        port.reject = false;
        require(reducer.commit(std::move(prepared)), "Native prepared durability retry failed");
        require(!reducer.commit(std::move(prepared)), "Consumed canonical preparation committed twice");
        const auto calls = port.calls;
        require(!reducer.commit(std::move(stalePreparation)) && port.calls == calls,
            "Stale canonical preparation reached durability");
        const auto* latest = file->prefix().latest();
        require(latest && !latest->inventory() && std::ranges::equal(latest->nativeInventory(), service.inventoryImage())
            && latest->commands().size() == 2
            && latest->commands()[0].disposition == static_cast<uint8_t>(CommandDisposition::Applied),
            "Native image and command disposition were not persisted together");
        require(file->commit(reducer.latestPublication(), reducer.canonicalRevision(), {}, nullptr, nullptr, nullptr,
            nullptr, nullptr, &scripts) == CanonicalDurabilityResult::Rejected,
            "Canonical durability silently dropped an installed native domain");
        require(!CanonicalDurableTick::create(latest->stateVersion(), latest->canonicalRevision(), latest->checkpointTick(),
            latest->players(), {}, CanonicalChecksum(0), std::optional{CanonicalDurableInventoryState{}}, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt, latest->nativeInventory()), "Dual inventory persistence accepted");
        auto replay = prepare(input, 2);
        require(replay.result().dispositions()[0].disposition() != CommandDisposition::Applied
            && !replay.candidateNativeInventory() && reducer.commit(std::move(replay)), "Retry reapplied native mutation");
        auto staleInput = wire(service, reducer.state(), 2, false, 1);
        staleInput.commandSequence = *contender.commandSequence().next();
        staleInput.commandId = id<CommandId>(contender.commandId().value() + 1);
        staleInput.expectedInventoryRevision = InventoryRevision::initial();
        auto stale = prepare(bind(reducer.state(), staleInput).proposal(), 3);
        require(stale.result().dispositions()[0].disposition() == CommandDisposition::InventoryTransactionRejected
            && reducer.commit(std::move(stale)), "Stale native input was not durably rejected");
        auto opened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        InventoryService recovered(content.store, content.readers, content.binding(), true);
        const std::array references{content.actor, content.shirt};
        recovered.recover(opened->prefix().latest()->nativeInventory(), references);
        require(std::ranges::equal(recovered.inventoryImage(), service.inventoryImage()), "Canonical recovery changed coherent image");
        auto resumed = players(*SessionGeneration::initial().next());
        struct Collision final : ServerCollisionQuery
        {
            std::optional<ServerCollisionResult> resolve(const ServerCollisionRequest& request) noexcept override
            { return ServerCollisionResult{request.currentRoot.position(), LinearVelocity3(0, 0, 0)}; }
        } collision;
        CanonicalCommandReducer continued(resumed, *opened->restoredStateVersion(), *opened->restoredCanonicalRevision(),
            *opened->restoredCheckpointTick(), observability, {}, testContentManifest(), collision);
        require(continued.configureDurability(*opened, nullptr, nullptr, nullptr, nullptr, nullptr, &scripts, &recovered),
            "Recovered reducer composition failed");
        auto take = wire(recovered, resumed, 2, false, 1);
        ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(4), IngressOrdinal::initial());
        require(intake.submit(bind(resumed, take).proposal()) == CommandSubmissionResult::Accepted, "Continuation intake failed");
        clock.value += 4 * 33'333'334;
        auto pumped = intake.pump();
        auto next = continued.prepareTick(pumped.batches().front());
        require(next.result().dispositions()[0].disposition() == CommandDisposition::Applied
            && continued.commit(std::move(next)), "Recovered native continuation failed");
        const auto view = recovered.project(continued.state(), id<SessionId>(2), id<ServerTick>(4), continued.canonicalRevision());
        require(view && view->containers[0].stacks[0].count == 1, "Continuation lost shared container state");
        std::cout << "synthetic canonical integration: owned preparation, retry/stale rejection, joint file recovery and continuation\n";
    }

    void checkInventoryService(const std::filesystem::path& scratch, bool durability)
    {
        require(std::filesystem::create_directory(scratch), "Native inventory scratch already exists");
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{scratch};
        Content content;
        auto authority = players();
        Clock clock;
        Delivery delivery(clock, SessionGeneration::initial());
        const auto path = scratch / "session.equipment";
        const std::array references{ content.actor, content.shirt };
        EquipmentBytes bytes;
        std::unique_ptr<const InventoryTransferSuccess> success;
        FileFaults faults;
        if (durability)
        {
            for (const auto fault : { FileFault::Flush, FileFault::AfterReplace })
            {
                {
                    InventoryService service(content.store, content.readers, content.binding());
                    EquipmentFileSink file(path, true);
                    EquipmentFileCommitter sink(file, faults);
                    const auto command = bind(authority, wire(service, authority, 1, true, 2));
                    auto prepared = service.prepare(authority, command);
                    const auto* priorSuccess = success.get();
                    const auto priorBytes = bytes;
                    faults = { fault };
                    const auto result = service.commit(authority, prepared, sink, success, bytes);
                    require(result == (fault == FileFault::Flush ? PersistenceResult::Rejected : PersistenceResult::Uncertain), "Service fault result incorrect");
                    require(success.get() == priorSuccess && bytes == priorBytes, "Failed service commit changed owned publication");
                    if (fault == FileFault::Flush)
                    {
                        require(service.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1))->containers[0].stacks.empty(), "Failed durability installed native inventory");
                        faults = {};
                        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted, "Safe service retry failed");
                    }
                    else
                    {
                        require(!service.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)), "Uncertain native service published state");
                        EquipmentFileSink other(scratch / "other.equipment", true);
                        EquipmentFileCommitter alternate(other, faults);
                        require(service.commit(authority, prepared, alternate, success, bytes) == PersistenceResult::Uncertain, "Alternate sink reopened uncertain native service");
                    }
                }
                InventoryService restored(content.store, content.readers, content.binding(), true);
                faults = {};
                require(restored.recover(path, references, bytes, faults) == FileReadResult::Read, "Failed service recovery");
                auto view = restored.project(authority, id<SessionId>(2), id<ServerTick>(2), id<CanonicalRevision>(2));
                require(view && view->containers[0].stacks[0].count == 2, "Recovered service image mixed owner states");
            }
            std::cout << "synthetic service durability: safe retry, uncertain closure across sinks, coherent fresh recovery\n";
            return;
        }
        {
            InventoryService service(content.store, content.readers, content.binding());
            EquipmentFileSink file(path, true);
            EquipmentFileCommitter sink(file, faults);
            publish(service, authority, delivery, 1);
            auto input = wire(service, authority, 1, true, 2);
            require(!ServerApp::InventoryCommandBinding::resolve(authority, id<SessionId>(2), input.sessionGeneration, input), "Connection session spoof accepted");
            require(!ServerApp::InventoryCommandBinding::resolve(players(*SessionGeneration::initial().next()), input.sessionId, input.sessionGeneration, input), "Old authenticated generation accepted");
            const auto bound = bind(authority, input);
            require(bound.player() == id<PlayerId>(11), "Connection binding trusted a client actor identity");
            const auto otherItem = wire(service, authority, 2, true, 1).stackId;
            for (int test = 0; test < 7; ++test)
            {
                auto invalid = input;
                if (test == 0) invalid.stackId = otherItem;
                if (test == 1) invalid.count = 0;
                if (test == 2) invalid.count = MaximumTransferCount + 1;
                if (test == 3) invalid.containerId = id<ContainerId>(91);
                if (test == 4) invalid.expectedInventoryRevision = InventoryRevision::initial();
                if (test == 5) invalid.interactionOrigin = Position3(384 * 1024 + 1, 0, 0);
                if (test == 6) invalid.kind = InventoryTransactionKind::DropItem;
                const auto binding = ServerApp::InventoryCommandBinding::resolve(authority,
                    input.sessionId, input.sessionGeneration, invalid).value();
                bool rejected = false;
                try { (void)service.prepare(authority, binding); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected && faults.mWrites == 0, "Invalid service command reached durability");
            }
            auto atReach = wire(service, authority, 1, true, 1);
            atReach.interactionOrigin = Position3(384 * 1024, 0, 0);
            (void)service.prepare(authority, bind(authority, atReach));
            require(faults.mWrites == 0, "Reach-boundary preparation mutated durability");
            auto prepared = service.prepare(authority, bound);
            auto replacement = players(*SessionGeneration::initial().next());
            bool rejected = false;
            try { service.commit(replacement, prepared, sink, success, bytes); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !success && faults.mWrites == 0, "Deferred stale session reached persistence");
            require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted, "Service drop failed");
            publish(service, authority, delivery, 2);
            require(delivery.clients[0]->confirmedContainerInventoryBaselines()[0].stacks[0].count == 2
                && delivery.clients[1]->confirmedContainerInventoryBaselines()[0].stacks[0].count == 2,
                "Synthetic clients disagree about committed shared container");
            auto take = service.prepare(authority, bind(authority, wire(service, authority, 2, false, 1)));
            require(service.commit(authority, take, sink, success, bytes) == PersistenceResult::Accepted
                && success->mDestinationCount == -6, "Second bound actor take failed");
            publish(service, authority, delivery, 3);
        }
        const auto generation = *SessionGeneration::initial().next();
        authority = players(generation);
        Delivery reconnected(clock, generation);
        {
            auto wrong = content.binding();
            std::swap(wrong.mPlayers[0], wrong.mPlayers[1]);
            InventoryService swapped(content.store, content.readers, wrong, true);
            bool rejected = false;
            try { swapped.recover(path, references, bytes, faults); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && !swapped.project(authority, id<SessionId>(1), id<ServerTick>(4), id<CanonicalRevision>(4)),
                "Recovery silently rebound durable inventories to other players");
        }
        InventoryService restored(content.store, content.readers, content.binding(), true);
        require(!restored.project(authority, id<SessionId>(1), id<ServerTick>(4), id<CanonicalRevision>(4)), "Unrecovered service published starting inventory");
        require(restored.recover(path, references, bytes, faults) == FileReadResult::Read, "Native service restart failed");
        publish(restored, authority, reconnected, 4);
        EquipmentFileSink file(path, true);
        EquipmentFileCommitter sink(file, faults);
        auto take = restored.prepare(authority, bind(authority, wire(restored, authority, 1, false, 1)));
        require(restored.commit(authority, take, sink, success, bytes) == PersistenceResult::Accepted, "Recovered first actor continuation failed");
        auto drop = restored.prepare(authority, bind(authority, wire(restored, authority, 2, true, 1)));
        require(restored.commit(authority, drop, sink, success, bytes) == PersistenceResult::Accepted, "Recovered second actor continuation failed");
        publish(restored, authority, reconnected, 5);
        require(reconnected.clients[0]->confirmedContainerInventoryBaselines()[0].stacks[0].count == 1
            && reconnected.clients[1]->confirmedContainerInventoryBaselines()[0].stacks[0].count == 1,
            "Synthetic reconnected clients disagree after continuation");
        std::cout << "synthetic service: trusted session/player binding, native drop/take/recovery/continuation; "
            << delivery.sent + reconnected.sent << " frames through existing queue/encoder/client receive; no sockets or desktop clients\n";
    }
}
