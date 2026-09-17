#include "inventory_service_tests.hpp"
#include "inventory_service.hpp"
#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/readerscache.hpp>
#include <iostream>
#include <stdexcept>

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
            MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); }
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
                if (test == 5) invalid.interactionOrigin = Position3(1000, 0, 0);
                if (test == 6) invalid.kind = InventoryTransactionKind::DropItem;
                const auto binding = ServerApp::InventoryCommandBinding::resolve(authority,
                    input.sessionId, input.sessionGeneration, invalid).value();
                bool rejected = false;
                try { (void)service.prepare(authority, binding); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected && faults.mWrites == 0, "Invalid service command reached durability");
            }
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
