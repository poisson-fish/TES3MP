#include "placement_tests.hpp"
#include "environment.hpp"
#include "environment_tests.hpp"
#include <components/esm3/loadstat.hpp>
#include "inventory_service_tests.hpp"
#include "inventory_service.hpp"
#include "inventory_host.hpp"
#include "actor_campaign.hpp"
#include "magic_runtime.hpp"
#include "loadout.hpp"
#include <apps/openmw/tes3mp/remote_motion.hpp>
#include <chrono>
#include "test_allocations.hpp"
#include "../canonical_persistence_file.hpp"
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwgui/inventoryitemmodel.hpp>
#include <apps/openmw/mwgui/sortfilteritemmodel.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/formatversion.hpp>
#include <iostream>
#include <bit>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace TES3MP::ServerApp::Testing
{
    void nativeInventoryApplication(NativeInventoryService&, CanonicalPersistenceFile&, NativeEnvironmentService* = nullptr);
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
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                {
                    ESM::Attribute attribute;
                    attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
                    store.insertStatic(attribute);
                }
                for (int i = 0; i < ESM::Skill::Length; ++i)
                {
                    ESM::Skill skill; skill.blank();
                    skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
                    store.insertStatic(skill);
                }
                for (const auto [name, value] : std::initializer_list<std::pair<const char*, float>>{
                        {"fNPCbaseMagickaMult", 2.f}, {"fUnarmoredBase1", .1f}, {"fUnarmoredBase2", .1f},
                        {"fLightMaxMod", .3f}, {"fMedMaxMod", .6f}})
                {
                    ESM::GameSetting setting; setting.mId = ESM::RefId::stringRefId(name);
                    setting.mValue = ESM::Variant(value); store.insertStatic(setting);
                }
                for (const auto name : {"iBaseArmorSkill", "iHelmWeight", "iCuirassWeight", "iPauldronWeight",
                        "iGreavesWeight", "iBootsWeight", "iGauntletWeight", "iShieldWeight"})
                {
                    ESM::GameSetting setting; setting.mId = ESM::RefId::stringRefId(name);
                    setting.mValue = ESM::Variant(30); store.insertStatic(setting);
                }
                ESM::Race race; race.blank(); race.mId = ESM::RefId::stringRefId("service_race"); store.insertStatic(race);
                ESM::NPC npc; npc.blank(); npc.mId = actor; npc.mRace = race.mId; store.insertStatic(npc);
                ESM::Clothing item; item.blank(); item.mId = shirt;
                item.mData.mType = ESM::Clothing::Shirt; store.insertStatic(item);
                ESM::Container shared; shared.blank(); shared.mId = container; shared.mWeight = 1000; store.insertStatic(shared);
            }
            InventoryServiceBinding binding() const
            {
                return { { id<PlayerId>(11), id<PlayerId>(22) }, id<ItemPrototypeId>(70),
                    {{{ actor, shirt, 3 }, { actor, shirt, -5 }}}, { 1, 2, 3 },
                    {{id<ContainerId>(90), CellId::interior(id<CellSpaceId>(7)), Position3(0, 0, 0), container, {}}} };
            }
        };
        CanonicalServerState players(SessionGeneration generation = SessionGeneration::initial(), uint64_t first = 11, uint64_t second = 22)
        {
            const auto zero = Turn32::fromValue(0);
            const Transform transform(CellId::interior(id<CellSpaceId>(7)), Position3(0, 0, 0), Orientation3(zero, zero, zero));
            const std::array<CanonicalPlayerEntityState, 2> actors{{
                { id<PlayerId>(first), id<EntityId>(111), id<AppearanceId>(1), transform, LinearVelocity3(0, 0, 0),
                    EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial() },
                { id<PlayerId>(second), id<EntityId>(222), id<AppearanceId>(1), transform, LinearVelocity3(0, 0, 0),
                    EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial() } }};
            const std::array<CanonicalSessionProgress, 2> sessions{{
                { id<SessionId>(1), generation, id<PlayerId>(first), id<EntityId>(111), std::nullopt },
                { id<SessionId>(2), generation, id<PlayerId>(second), id<EntityId>(222), std::nullopt } }};
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
            const auto expected = delivery.sent + messages.size();
            // Multiple containers may exceed a single reliable burst. Exercise
            // bounded queue draining instead of assuming one pump delivers all.
            for (size_t pass = 0; pass < messages.size() && delivery.sent < expected; ++pass)
                for (uint64_t i = 1; i <= 2; ++i)
                    (void)queues.pump(delivery, id<TransportConnectionId>(i), tick + pass * 1000);
            require(delivery.sent == expected, "Native baseline queue did not drain");
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
        void unequipStartingItems(InventoryService& service, const CanonicalServerState& authority)
        {
            // Manual-equipment/transfer tests begin with empty slots through the
            // same public command path now that base inventories auto-equip.
            for (uint64_t session : {1, 2})
                for (;;)
                {
                    const auto inventory = service.project(authority, id<SessionId>(session), id<ServerTick>(1),
                        id<CanonicalRevision>(1))->playerInventory.front();
                    if (inventory.equipment.empty()) break;
                    const auto gear = inventory.equipment.front();
                    const auto item = std::ranges::find(inventory.stacks, gear.stackId, &CanonicalItemStack::stackId);
                    const ClientInventoryTransactionCommand input{id<SessionId>(session),
                        authority.findActiveSession(id<SessionId>(session))->sessionGeneration(), CommandSequence::initial(),
                        id<CommandId>(1), id<CanonicalRevision>(1), InventoryTransactionKind::UnequipItem, {}, item->prototypeId,
                        gear.stackId, 1, gear.slot, inventory.revision, {}, {}, Position3(0, 0, 0)};
                    auto prepared = service.prepareInventory(authority, bind(authority, input).proposal());
                    require(prepared && prepared->commit([](auto) { return CanonicalDurabilityResult::Committed; })
                        == CanonicalDurabilityResult::Committed, "Fixture starting equipment could not be removed");
                }
        }
    }

    namespace
    {
        ClientInventoryTransactionCommand worldWire(InventoryService& service, const CanonicalServerState& state,
            uint64_t session, bool pickup, size_t index = 0, uint32_t count = 1)
        {
            const auto view = service.projectInventory(state, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
            const auto& inventory = view.playerInventory.front();
            const auto& ground = view.groundItems.front();
            const auto& stack = pickup ? ground.items.at(index).stack : inventory.stacks.at(index);
            return {id<SessionId>(session), state.findActiveSession(id<SessionId>(session))->sessionGeneration(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                pickup ? InventoryTransactionKind::PickupItem : InventoryTransactionKind::DropItem,
                {}, stack.prototypeId, stack.stackId, pickup ? stack.count : count, {}, inventory.revision, {},
                pickup ? std::optional{ground.items.at(index).revision} : std::nullopt,
                state.findPlayer(state.findActiveSession(id<SessionId>(session))->playerId())->transform().position()};
        }
    }
    namespace
    {
        // Real reducer, durability file, baseline codec and client receiver;
        // synthetic transport/authentication, no graphical presentation claim.
        void teleportRoundTrip(InventoryService& service, CanonicalServerState initial,
            const std::filesystem::path& scratch, bool exterior = false)
        {
            NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
            const std::array spaces{CellSpaceDeclaration{id<CellSpaceId>(7), CellSpaceKind::Interior},
                CellSpaceDeclaration{id<CellSpaceId>(8), exterior ? CellSpaceKind::Exterior : CellSpaceKind::Interior}};
            const auto second = exterior ? CellId::exterior(id<CellSpaceId>(8), -1, 50) : CellId::interior(id<CellSpaceId>(8));
            const std::array cells{CellId::interior(id<CellSpaceId>(7)), second};
            const auto manifest = ContentManifest::create(testContentManifestId(), spaces, cells, id<AppearanceId>(1), testMovementProfile()).value();
            CanonicalCommandReducer reducer(initial, observability, manifest);
            const auto catalog = ServerScriptStateCatalog::create({}).value();
            auto scripts = CanonicalScriptState::initial(catalog).value();
            std::array<std::byte, 32> configuration{}; configuration[0] = std::byte{1};
            const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
                ServerConfigurationId::fromBytes(configuration).value(), {}, catalog, {}).value();
            const auto path = scratch / "teleports.bin";
            auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(path, identity));
            struct Port final : CanonicalDurabilityPort
            {
                ServerApp::CanonicalPersistenceFile& file;
                CanonicalCommandReducer& reducer;
                bool reject = false;
                Port(ServerApp::CanonicalPersistenceFile& f, CanonicalCommandReducer& r) : file(f), reducer(r) {}
                CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
                    CanonicalRevision revision, std::span<const DurableCommandOrder> commands,
                    const CanonicalInventoryWorld* inventory, const CanonicalCombatWorld* combat,
                    const CanonicalInteractiveObjectWorld* objects, const CanonicalActorWorld* actors,
                    const CanonicalWorldState* world, const CanonicalScriptState* scripts,
                    std::span<const std::byte> image) noexcept override
                {
                    if (reducer.latestPublication() == candidate || image.empty()) return CanonicalDurabilityResult::Failed;
                    if (reject) return CanonicalDurabilityResult::Rejected;
                    return file.commit(candidate, revision, commands, inventory, combat, objects, actors, world, scripts, image);
                }
            } port(*file, reducer);
            require(reducer.configureDurability(port, nullptr, nullptr, nullptr, nullptr, nullptr, &scripts, &service),
                "Teleport canonical composition failed");
            Clock clock; Delivery delivery(clock, SessionGeneration::initial());
            uint64_t tick = 1;
            const auto first = CellId::interior(id<CellSpaceId>(7));
            const auto aliceId = initial.players()[0].playerId(), bobId = initial.players()[1].playerId();
            const auto view = [&](uint64_t session) {
                return service.projectInventory(reducer.state(), id<SessionId>(session), id<ServerTick>(tick), id<CanonicalRevision>(tick)).value();
            };
            const auto proposal = [&](uint64_t session, auto payload) {
                const auto* caller = reducer.state().findActiveSession(id<SessionId>(session));
                const auto& player = *reducer.state().findPlayer(caller->playerId());
                const auto sequence = caller->highestContiguousFinalizedCommand()
                    ? *caller->highestContiguousFinalizedCommand()->next() : CommandSequence::initial();
                return ServerCommandProposal(caller->sessionId(), caller->sessionGeneration(), sequence, id<CommandId>(sequence.value()),
                    reducer.canonicalRevision(), EntityPrecondition(player.entityId(), player.entityRevision(), player.authorityEpoch()),
                    std::move(payload));
            };
            const auto activate = [&](uint64_t session, uint64_t door) {
                const auto& player = *reducer.state().findPlayer(session == 1 ? aliceId : bobId);
                return proposal(session, InteractiveObjectCommandProposal(id<InteractiveObjectId>(door), player.transform().cell(),
                    player.transform().position(), ObjectRevision::initial(), ObjectInteractionKind::Activate, {}));
            };
            const auto prepare = [&](const ServerCommandProposal& command, const ServerCommandProposal* after = nullptr) {
                ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(++tick), IngressOrdinal::initial());
                require(intake.submit(command) == CommandSubmissionResult::Accepted, "Teleport intake failed");
                if (after) require(intake.submit(*after) == CommandSubmissionResult::Accepted, "Trailing motion intake failed");
                clock.value += tick * 33'333'334;
                auto batches = intake.pump();
                require(batches && batches.batches().size() == 1, "Teleport tick failed");
                return reducer.prepareTick(batches.batches().front());
            };
            const auto commit = [&](auto& pending) {
                require(pending.result() && pending.result().dispositions()[0].disposition() == CommandDisposition::Applied
                    && reducer.commit(std::move(pending)), "Teleport scenario mutation failed");
                service.synchronizeCells(reducer.state());
                publish(service, reducer.state(), delivery, ++tick);
            };
            const auto mutate = [&](ClientInventoryTransactionCommand input) {
                const auto resolved = bind(reducer.state(), input).proposal();
                auto pending = prepare(proposal(input.sessionId.value(), std::get<InventoryCommandProposal>(resolved.payload())));
                commit(pending);
            };
            service.synchronizeCells(initial);
            publish(service, initial, delivery, tick);
            const auto baseline = view(1).groundItems[0];
            require(baseline.teleportDoors.size() == 1
                && delivery.clients[0]->confirmedGroundItemBaseline()->teleportDoors == baseline.teleportDoors,
                "Supported teleport identity lost on wire");
            const auto outward = baseline.teleportDoors[0];
            for (int invalid = 0; invalid < 5; ++invalid)
            {
                auto bad = baseline;
                if (invalid == 0) bad.teleportDoors.resize(33, outward);
                if (invalid == 1) bad.teleportDoors.push_back(outward);
                if (invalid == 2) bad.teleportDoors[0] = 1;
                if (invalid == 3) bad.nativeWorld = false;
                if (invalid == 4) bad.teleportDoors[0] = bad.door->placement;
                require(std::holds_alternative<InventoryReplicationDecodeError>(
                    decodeReliableGroundItemBaseline(encodeReliableGroundItemBaseline(bad))), "Malformed teleport baseline accepted");
            }
            const auto stalePickup = worldWire(service, reducer.state(), 1, true);
            mutate(worldWire(service, reducer.state(), 1, true));
            const auto beforeExit = view(1);
            auto direct = prepare(proposal(1, CellTransitionCommandProposal(second)));
            require(direct.result().dispositions()[0].disposition() == CommandDisposition::ObjectInteractionRejected,
                "Client bypassed native doors with direct cell intent");
            for (int invalid = 0; invalid < 5; ++invalid)
            {
                auto request = proposal(1, InteractiveObjectCommandProposal(
                    id<InteractiveObjectId>(invalid == 0 ? outward + 5000 : outward), invalid == 1 ? second : first,
                    Position3(invalid == 2 ? 10000 * 1024 : 0, 0, 0),
                    id<ObjectRevision>(invalid == 3 ? 2 : 1),
                    invalid == 4 ? ObjectInteractionKind::PickLock : ObjectInteractionKind::Activate, {}));
                auto bad = prepare(request);
                require(bad.result().dispositions()[0].disposition() == CommandDisposition::ObjectInteractionRejected
                    && !bad.candidateNativeInventory(), "Invalid teleport activation admitted");
            }
            const auto exit = activate(1, outward);
            for (int invalid = 0; invalid < 2; ++invalid)
            {
                const auto precondition = invalid ? EntityPrecondition(id<EntityId>(999), EntityRevision::initial(), AuthorityEpoch::initial())
                    : exit.entityPrecondition();
                const ServerCommandProposal forged(exit.sessionId(), invalid ? exit.sessionGeneration() : id<SessionGeneration>(2),
                    exit.commandSequence(), exit.commandId(), exit.observedCanonicalRevision(), precondition,
                    std::get<InteractiveObjectCommandProposal>(exit.payload()));
                require(!service.prepareDoorActivation(reducer.state(), forged), "Forged teleport session or actor accepted");
            }
            const auto contender = activate(2, outward);
            auto contested = prepare(exit, &contender);
            require(contested.result().dispositions()[0].disposition() == CommandDisposition::Applied
                && contested.result().dispositions()[1].disposition() == CommandDisposition::ObjectInteractionRejected,
                "Same-tick native teleport contention bypassed mutation budget");
            const auto& oldAlice = *reducer.state().findPlayer(aliceId);
            const auto epoch = oldAlice.authorityEpoch();
            const auto nextSequence = *exit.commandSequence().next();
            ServerCommandProposal trailing(exit.sessionId(), exit.sessionGeneration(), nextSequence, id<CommandId>(nextSequence.value()),
                reducer.canonicalRevision(), EntityPrecondition(oldAlice.entityId(), oldAlice.entityRevision(), epoch),
                PlayerMotionCommandProposal(LinearVelocity3(100, 0, 0)));
            auto leaving = prepare(exit, &trailing);
            require(leaving.result().dispositions()[1].disposition() == CommandDisposition::AuthorityEpochMismatch,
                "Pre-teleport movement overwrote destination");
            const auto expected = *leaving.candidateNativeInventory()->playerDestination();
            require(expected.cell() == second && expected.position() == Position3((exterior ? -8192 + 32 : 32) * 1024,
                    exterior ? (409600 + 64) * 1024 : 0, 0)
                && std::abs(int64_t(expected.orientation().z().value()) - int64_t(0xc0000000)) < 64,
                "OpenMW destination pose was not preserved");
            const auto staged = service.projectInventory(leaving.candidateState(), id<SessionId>(1), id<ServerTick>(tick),
                leaving.candidateRevision(), leaving.candidateNativeInventory());
            require(staged && staged->groundItems[0].cell == second && staged->groundItems[0].teleportDoors.size() == 1,
                "Teleport destination baseline could not be staged");
            const auto publication = reducer.latestPublication();
            const auto sent = delivery.sent;
            port.reject = true;
            require(!reducer.commit(std::move(leaving)) && reducer.latestPublication() == publication
                && reducer.state().findPlayer(aliceId)->transform().cell() == first && delivery.sent == sent
                && service.activeCells() == std::array{true, false}, "Rejected durability leaked relocation/baseline/activity");
            port.reject = false;
            commit(leaving);
            require(reducer.state().findPlayer(aliceId)->transform() == expected
                && reducer.state().findPlayer(aliceId)->authorityEpoch() == *epoch.next()
                && reducer.state().findPlayer(bobId)->transform() == initial.findPlayer(bobId)->transform()
                && service.activeCells() == std::array{true, true}, "Teleport moved Bob or lost destination");
            require(file->restoredState()->findPlayer(aliceId)->transform() == expected,
                "Destination baseline preceded persisted player state");
            auto replay = prepare(exit);
            require(replay.result().dispositions()[0].disposition() != CommandDisposition::Applied
                && !replay.candidateNativeInventory(), "Repeated teleport was applied twice");
            auto wrongCell = prepare(activate(1, outward));
            require(wrongCell.result().dispositions()[0].disposition() == CommandDisposition::ObjectInteractionRejected,
                "Previous-cell teleport remained usable");
            require(!service.prepareInventory(reducer.state(), bind(reducer.state(), stalePickup).proposal()),
                "Previous-cell pickup remained usable");
            const auto inward = view(1).groundItems[0].teleportDoors[0];
            require(delivery.clients[0]->confirmedGroundItemBaseline()->cell == second
                && delivery.clients[1]->confirmedGroundItemBaseline()->cell == first
                && delivery.clients[0]->confirmedGroundItemBaseline()->teleportDoors == std::vector{inward},
                "Split wire clients retained old-cell activators");
            const auto pickedUp = worldWire(service, reducer.state(), 1, true);
            mutate(pickedUp);
            const auto inventory = view(1).playerInventory[0];
            const auto pickedStack = std::ranges::find(inventory.stacks, pickedUp.prototypeId, &CanonicalItemStack::prototypeId);
            require(pickedStack != inventory.stacks.end(), "Picked item missing from destination inventory");
            auto drop = worldWire(service, reducer.state(), 1, false, size_t(pickedStack - inventory.stacks.begin()));
            drop.placement = placementTestView(0, 0, true);
            mutate(drop);
            const auto savedDrop = view(1).groundItems[0].items;
            if (exterior)
            {
                require(savedDrop.size() == 1, "Exterior drop membership changed");
                if (savedDrop[0].position.z() != -66 * 1024)
                    throw std::runtime_error("Exterior LAND drop height mismatch: "
                        + std::to_string(savedDrop[0].position.z()));
            }
            while (!view(2).groundItems[0].items.empty()) mutate(worldWire(service, reducer.state(), 2, true));
            const auto shared = view(2).containers[0];
            if (!shared.stacks.empty())
            {
                const auto playerInventory = view(2).playerInventory[0];
                mutate({id<SessionId>(2), SessionGeneration::initial(), CommandSequence::initial(), id<CommandId>(1),
                    CanonicalRevision::initial(), InventoryTransactionKind::TakeAllFromContainer, shared.container,
                    shared.stacks[0].prototypeId, shared.stacks[0].stackId, 1, {}, playerInventory.revision,
                    shared.revision, {}, Position3(0,0,0)});
            }
            auto returning = prepare(activate(1, inward)); commit(returning);
            require(reducer.state().findPlayer(aliceId)->transform().cell() == first && view(1).groundItems[0].items.empty()
                && view(1).containers[0].stacks.empty() && service.activeCells() == std::array{true, false}
                && delivery.clients[0]->confirmedGroundItemBaseline()->teleportDoors == std::vector{outward}
                && delivery.clients[0]->confirmedContainerInventoryBaselines().size() == view(1).containers.size(),
                "Alice's return regenerated loot or retained stale references");
            auto leavingAgain = prepare(activate(1, outward)); commit(leavingAgain);
            const auto revisited = view(1).groundItems[0].items;
            require(revisited.size() == savedDrop.size() && revisited[0].stack == savedDrop[0].stack
                && revisited[0].position == savedDrop[0].position, "Reentry regenerated destination loot or changed drop identity");
            auto reopened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(path, identity));
            require(reopened->restoredState()->findPlayer(aliceId)->transform() == expected
                && reopened->restoredState()->findPlayer(bobId)->transform() == initial.findPlayer(bobId)->transform()
                && std::ranges::equal(reopened->prefix().latest()->nativeInventory(), service.inventoryImage()),
                "Disk restart lost player destination or coherent loot image");
            std::cout << "Teleport round trip: server validation, durable destination, epoch reset, split wire baselines, loot and disk state passed\n";
        }
    }

    void checkTeleportTraversal(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Teleport scratch already exists");
        Content content;
        auto chest = *content.store.get<ESM::Container>().find(content.container);
        chest.mInventory.mList = {{4, content.shirt}}; content.store.overrideRecord(chest);
        ESM::Door door; door.blank(); door.mId = ESM::RefId::stringRefId("teleport_test"); content.store.insertStatic(door);
        auto binding = content.binding();
        const auto first = binding.mContainers[0].mCell, second = CellId::interior(id<CellSpaceId>(8));
        binding.mContainers.push_back({id<ContainerId>(91), second, Position3(0,0,0), content.container, {}});
        auto item = ESM::makeBlankCellRef(); item.mRefID = content.shirt; item.mRefNum = {500, 0};
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{first, {{MWWorld::PlacedRefTag | 500, item}}});
        item.mRefNum = {501, 0};
        binding.mSecondWorldItems.emplace(InventoryServiceBinding::WorldItems{second, {{MWWorld::PlacedRefTag | 501, item}}});
        binding.mDoor = ESM::makeBlankCellRef(); binding.mDoor->mRefID = door.mId; binding.mDoor->mRefNum = {700, 0};
        binding.mDoorId = MWWorld::PlacedRefTag | 700;
        const auto zero = Turn32::fromValue(0);
        binding.mTeleportDoors = std::vector<InventoryServiceBinding::TeleportDoor>{
            {MWWorld::PlacedRefTag | 701, first, Position3(0,0,0), Transform(second, Position3(32 * 1024,0,0),
                Orientation3(zero, zero, Turn32::fromValue(0xc0000000)))},
            {MWWorld::PlacedRefTag | 702, second, Position3(0,0,0), Transform(first, Position3(0,0,0), Orientation3(zero,zero,zero))}};
        InventoryService service(content.store, content.readers, binding);
        teleportRoundTrip(service, players(), scratch);
        InventoryService recovered(content.store, content.readers, binding, true);
        const std::array references{content.actor, content.container, content.shirt, door.mId};
        recovered.recover(service.inventoryImage(), references);
        require(std::ranges::equal(recovered.inventoryImage(), service.inventoryImage()), "Teleport recovery changed native state");
        const auto authority = players();
        const auto& alice = authority.players()[0];
        const ServerCommandProposal activation(id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
            id<CommandId>(1), CanonicalRevision::initial(), EntityPrecondition(alice.entityId(), alice.entityRevision(), alice.authorityEpoch()),
            InteractiveObjectCommandProposal(id<InteractiveObjectId>(MWWorld::PlacedRefTag | 701), first, Position3(0,0,0),
                ObjectRevision::initial(), ObjectInteractionKind::Activate, {}));
        auto uncertain = recovered.prepareDoorActivation(authority, activation);
        require(uncertain && uncertain->commit([](auto) { return CanonicalDurabilityResult::Failed; }) == CanonicalDurabilityResult::Failed
            && recovered.inventoryImage().empty() && !recovered.prepareDoorActivation(authority, activation),
            "Uncertain teleport durability did not fail closed");
        const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1,10,10));
        const std::array required{inventoryReplicationCapability(), nativeDoorCapability(), nativeTeleportCapability()};
        const auto server = std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, required));
        const auto oldClient = std::get<CapabilityOffer>(CapabilityOffer::create(versions, std::span(required).first(2), {}));
        require(!std::holds_alternative<ServerHello>(negotiateClientHello(ClientHello::fromOffer(oldClient), server)),
            "V11 accepted a client unable to present teleport activators");
    }

    void checkCellLifecycle(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Cell lifecycle scratch already exists");
        Content content;
        auto chest = *content.store.get<ESM::Container>().find(content.container);
        chest.mInventory.mList = {{4, content.shirt}};
        content.store.overrideRecord(chest);
        ESM::Door door; door.blank(); door.mId = ESM::RefId::stringRefId("cell_door"); content.store.insertStatic(door);
        auto binding = content.binding();
        const auto first = binding.mContainers[0].mCell, second = CellId::interior(id<CellSpaceId>(8));
        binding.mContainers.push_back({id<ContainerId>(91), second, Position3(0, 0, 0), content.container, {}});
        auto placed = ESM::makeBlankCellRef(); placed.mRefID = content.shirt; placed.mRefNum = {500, 0};
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{first, {{MWWorld::PlacedRefTag | 500, placed}}});
        placed.mRefNum = {501, 0};
        binding.mSecondWorldItems.emplace(InventoryServiceBinding::WorldItems{second, {{MWWorld::PlacedRefTag | 501, placed}}});
        binding.mDoor = ESM::makeBlankCellRef(); binding.mDoor->mRefID = door.mId; binding.mDoor->mRefNum = {700, 0};
        binding.mDoorId = MWWorld::PlacedRefTag | 700;
        std::array<bool, 2> loaded{};
        binding.mCellActivity = [&](const auto& active) { loaded = active; };
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        uint64_t tick = 1;
        const auto move = [&](size_t index, CellId cell) {
            auto entities = std::vector(authority.players().begin(), authority.players().end());
            const auto& entity = entities[index];
            entities[index] = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(entity, id<ServerTick>(++tick),
                Transform(cell, Position3(0, 0, 0), entity.transform().orientation()), LinearVelocity3(0, 0, 0)));
            authority = std::get<CanonicalServerState>(createCanonicalServerState(entities, authority.activeSessions()));
            service.synchronizeCells(authority);
        };
        const auto view = [&](InventoryService& owner, uint64_t session) {
            return owner.projectInventory(authority, id<SessionId>(session), id<ServerTick>(++tick), id<CanonicalRevision>(tick)).value();
        };
        const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
        const auto mutate = [&](ClientInventoryTransactionCommand input) {
            auto pending = service.prepareInventory(authority, bind(authority, input).proposal());
            require(pending && pending->commit(accepted) == CanonicalDurabilityResult::Committed, "Cell inventory mutation failed");
        };
        service.synchronizeCells(authority);
        require(loaded == std::array{true, false}, "Starting occupancy loaded the wrong interiors");
        const auto initialSecondId = id<ItemStackId>(MWWorld::PlacedRefTag | 501);
        Clock clock; Delivery delivery(clock, SessionGeneration::initial());
        publish(service, authority, delivery, ++tick);
        const auto stalePickup = worldWire(service, authority, 1, true);
        const auto staleContainer = wire(service, authority, 1, false, 1);
        move(0, second);
        require(loaded == std::array{true, true}, "Alice's move unloaded Bob's occupied interior");
        require(!service.prepareInventory(authority, bind(authority, stalePickup).proposal())
            && !service.prepareInventory(authority, bind(authority, staleContainer).proposal()), "Cross-cell pickup/container accepted");
        auto alice = view(service, 1), bob = view(service, 2);
        require(alice.containers.size() == 1 && alice.containers[0].container == id<ContainerId>(91)
            && alice.groundItems[0].items[0].stack.stackId == initialSecondId && !alice.groundItems[0].door
            && bob.containers[0].container == id<ContainerId>(90) && bob.groundItems[0].door,
            "Split occupancy leaked references or door state across interiors");
        publish(service, authority, delivery, ++tick);
        require(delivery.clients[0]->confirmedGroundItemBaseline()->cell == second
            && delivery.clients[0]->confirmedGroundItemBaseline()->nativePlacements == alice.groundItems[0].nativePlacements
            && !delivery.clients[0]->confirmedGroundItemBaseline()->door
            && delivery.clients[0]->confirmedContainerInventoryBaselines().size() == 1
            && delivery.clients[0]->confirmedContainerInventoryBaselines()[0].container == id<ContainerId>(91)
            && delivery.clients[1]->confirmedGroundItemBaseline()->cell == first,
            "Wire clients retained old-cell ground presentation");
        const auto alicePickup = worldWire(service, authority, 1, true);
        auto pending = service.prepareInventory(authority, bind(authority, alicePickup).proposal());
        const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
        require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
            && std::ranges::equal(before, service.inventoryImage()) && view(service, 1).groundItems[0].items.size() == 1,
            "Failed cell pickup leaked membership or inventory");
        require(pending->commit(accepted) == CanonicalDurabilityResult::Committed, "Cell pickup retry failed");
        mutate(worldWire(service, authority, 1, false));
        const auto dropped = view(service, 1).groundItems[0].items.front().stack.stackId;
        require(dropped != initialSecondId, "Dropped world item reused a placed identity");
        mutate(wire(service, authority, 2, false, 4));
        mutate(worldWire(service, authority, 2, true));
        const auto& bobEntity = *authority.findPlayer(id<PlayerId>(22));
        ServerCommandProposal activate(id<SessionId>(2), SessionGeneration::initial(), CommandSequence::initial(), id<CommandId>(1),
            CanonicalRevision::initial(), EntityPrecondition(bobEntity.entityId(), bobEntity.entityRevision(), bobEntity.authorityEpoch()),
            InteractiveObjectCommandProposal(id<InteractiveObjectId>(binding.mDoorId), first, Position3(0,0,0),
                ObjectRevision::initial(), ObjectInteractionKind::Activate, {}));
        auto opening = service.prepareDoorActivation(authority, activate);
        require(opening && opening->commit(accepted) == CanonicalDurabilityResult::Committed, "Bob could not activate his door");
        auto motion = service.prepareDoorStep(authority, id<ServerTick>(++tick), .05f);
        require(motion && motion->commit(accepted) == CanonicalDurabilityResult::Committed, "Occupied door did not advance");
        const auto doorState = *view(service, 2).groundItems[0].door;
        service.reportDoorObstruction(authority, {id<SessionId>(2), SessionGeneration::initial(), id<ServerTick>(tick),
            binding.mDoorId, doorState.motion, 1, true}, id<ServerTick>(tick));
        move(0, first);
        alice = view(service, 1);
        require(alice.containers[0].stacks.empty() && alice.groundItems[0].items.empty()
            && alice.groundItems[0].door == doorState && loaded == std::array{true, false},
            "Returning Alice did not see Bob's committed inventory/world/door changes");
        const auto playerInventory = alice.playerInventory[0];
        // Bob's old contact cannot reappear after leaving and returning while
        // Alice keeps the first cell active throughout.
        move(1, second); move(1, first);
        auto unblocked = service.prepareDoorStep(authority, id<ServerTick>(++tick), .05f);
        require(unblocked && !service.projectInventory(authority, id<SessionId>(1), id<ServerTick>(tick),
            id<CanonicalRevision>(tick), unblocked.get())->groundItems[0].door->blocked,
            "Returning player revived a contact report from the previous visit");
        move(0, second); move(1, second);
        require(loaded == std::array{false, true} && !service.prepareDoorStep(authority, id<ServerTick>(++tick), .05f),
            "Empty first interior remained active or advanced its door");
        require(view(service, 1).groundItems[0].items.front().stack.stackId == dropped, "Reload regenerated placed loot or drop identity");
        // Persist while neither cell is active; inactive character positions cannot pin cells.
        const auto sessions = std::vector(authority.activeSessions().begin(), authority.activeSessions().end());
        authority = std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), {}));
        service.synchronizeCells(authority);
        require(loaded == std::array{false, false}, "Disconnected characters pinned native cells");
        EquipmentFileSink file(scratch / "cells.bin", true); FileFaults faults;
        const auto image = service.inventoryImage();
        require(file.writeSessionImage({reinterpret_cast<const char*>(image.data()), image.size()}, faults) == PersistenceResult::Accepted,
            "Inactive cell image was not durable");
        EquipmentBytes disk;
        require(readBoundedFile(scratch / "cells.bin", MaxEquipmentSessionBytes, disk, faults) == FileReadResult::Read, "Cell image read failed");
        InventoryService restarted(content.store, content.readers, binding, true);
        const std::array references{content.actor, content.shirt, content.container, door.mId};
        // Format 6: header + two owners + two containers + world + door lengths.
        const size_t membership = 24 + 8 * 6;
        for (int failure = 0; failure < 4; ++failure)
        {
            auto bad = disk;
            if (failure == 0) bad[membership + 16] = 2; // Unsupported cell.
            if (failure == 1) bad[membership] = 65; // Excessive map before allocation.
            if (failure == 2) bad[membership + 8] ^= 127; // Orphaned membership.
            if (failure == 3) bad.pop_back(); // Truncated owner image.
            bool rejected = false;
            try { restarted.recover(std::as_bytes(std::span(bad)), references); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && restarted.inventoryImage().empty(), "Invalid cell membership partially restored");
        }
        restarted.recover(std::as_bytes(std::span(disk)), references);
        require(std::ranges::equal(service.inventoryImage(), restarted.inventoryImage()), "Restart changed inactive state");
        std::vector<CanonicalSessionProgress> resumed;
        for (const auto& session : sessions)
            resumed.emplace_back(session.sessionId(), id<SessionGeneration>(2), session.playerId(), session.entityId(), std::nullopt);
        authority = std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), resumed));
        restarted.synchronizeCells(authority);
        require(restarted.activeCells() == std::array{false, true}
            && view(restarted, 1).groundItems[0].items.front().stack.stackId == dropped
            && view(restarted, 1).playerInventory[0].stacks == playerInventory.stacks,
            "Restart/reconnect changed inventory or second-cell drops");
        Delivery reconnected(clock, id<SessionGeneration>(2));
        publish(restarted, authority, reconnected, ++tick);
        require(reconnected.clients[0]->confirmedGroundItemBaseline()->items.front().stack.stackId == dropped,
            "Reconnected client did not receive saved cell membership");
        move(0, first); move(1, first); restarted.synchronizeCells(authority);
        const auto returned = view(restarted, 1);
        require(returned.containers[0].stacks.empty() && returned.groundItems[0].items.empty()
            && returned.groundItems[0].nativePlacements.size() == 1
            && returned.groundItems[0].door->angle == doorState.angle && returned.groundItems[0].door->direction == doorState.direction,
            "Both players returning after restart regenerated loot or reset the door");
        std::cout << "Cell lifecycle: split occupancy, cell-filtered wire clients, rejection, unload, reconnect and disk restart passed\n";
    }

    void checkWorldItems(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "World item scratch already exists");
        Content content;
        const auto soul = ESM::RefId::stringRefId("world_soul");
        ESM::Creature creature; creature.blank(); creature.mId = soul; content.store.insertStatic(creature);
        ESM::Miscellaneous gem; gem.blank(); gem.mId = ESM::RefId::stringRefId("world_gem"); content.store.insertStatic(gem);
        auto binding = content.binding();
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{binding.mContainers[0].mCell, {}});
        auto ref = ESM::makeBlankCellRef();
        ref.mRefID = gem.mId; ref.mRefNum = {0, 0}; ref.mCount = 3;
        ref.mSoul = soul; ref.mChargeInt = 17; ref.mChargeIntRemainder = .375f;
        ref.mEnchantmentCharge = 12.25f; ref.mScale = 1.25f;
        ref.mPos.pos[0] = 10; ref.mPos.rot[2] = .75f;
        binding.mWorldItems->mPlacements.emplace_back(MWWorld::PlacedRefTag, ref);
        const std::array references{content.actor, content.shirt, gem.mId, soul};
        // These synthetic legacy owners bind at 1, 3 and 5 before world items;
        // decode through the production field codec to inspect non-wire fields.
        const std::array envelopes{EquipmentEnvelope{"native-inventory-1/11/22/70/90", binding.mContent, {1, -1}},
            EquipmentEnvelope{"native-inventory-1/11/22/70/90", binding.mContent, {3, -1}},
            EquipmentEnvelope{"native-inventory-1/11/22/70/90", binding.mContent, {5, -1}}};
        const std::array<EquipmentBindings, 2> actorBindings{{{envelopes[0], content.store, references, {}},
            {envelopes[1], content.store, references, {}}}};
        const std::array<EquipmentBindings, 1> containerBindings{{{envelopes[2], content.store, references, {}}}};
        const auto decode = [&](std::span<const std::byte> bytes) {
            EquipmentSessionValues values;
            decodeEquipmentSession({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, actorBindings,
                values, containerBindings, &actorBindings[0]);
            return values;
        };
        auto authority = players();
        InventoryService service(content.store, content.readers, binding);
        Clock clock;
        Delivery delivery(clock, SessionGeneration::initial());
        publish(service, authority, delivery, 1);
        require(delivery.clients[0]->confirmedGroundItemBaseline()->items.size() == 1
            && delivery.clients[0]->confirmedGroundItemBaseline()->nativePlacements.size() == 1
            && delivery.clients[0]->confirmedGroundItemBaseline()->presentation.front().scale == 1.25f,
            "Native ground projection omitted placement domain or visual fields");
        const auto pickup = worldWire(service, authority, 1, true);
        for (int test = 0; test < 7; ++test)
        {
            auto bad = pickup;
            switch (test)
            {
                case 0: bad.count = 2; break;
                case 1: bad.prototypeId = id<ItemPrototypeId>(123); break;
                case 2: bad.stackId = id<ItemStackId>(999); break;
                case 3: bad.expectedInventoryRevision = InventoryRevision::initial(); break;
                case 4: bad.expectedWorldItemRevision = WorldItemRevision::initial(); break;
                case 5: bad.interactionOrigin = Position3(999999999, 0, 0); break;
                case 6: bad.sessionGeneration = *SessionGeneration::initial().next(); break;
            }
            const auto bound = ServerApp::InventoryCommandBinding::resolve(authority, bad.sessionId, bad.sessionGeneration, bad);
            require(!bound || !service.prepareInventory(authority, bound->proposal()), "Invalid native pickup admitted");
        }
        const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
        auto first = service.prepareInventory(authority, bind(authority, pickup).proposal());
        auto second = service.prepareInventory(authority, bind(authority, worldWire(service, authority, 2, true)).proposal());
        require(first && second, "Native pickup preparation failed");
        for (uint64_t session : {1, 2})
        {
            auto candidate = service.projectInventory(authority, id<SessionId>(session), id<ServerTick>(2), id<CanonicalRevision>(2), first.get());
            require(candidate && candidate->groundItems.front().items.empty()
                && candidate->playerInventory.front().stacks.size() == (session == 1 ? 2 : 1),
                "World pickup candidate is not coherent across observers");
        }
        require(first->commit([](auto) { return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
            && std::ranges::equal(before, service.inventoryImage()), "Rejected pickup changed world or inventory");
        require(first->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
            "Pickup retry after safe rejection failed");
        size_t calls = 0;
        require(second->commit([&](auto) { ++calls; return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Rejected
            && calls == 0, "Simultaneous pickup granted the item twice");
        require(!service.prepareInventory(authority, bind(authority, pickup).proposal()), "Pickup replay admitted");
        publish(service, authority, delivery, 2);
        for (const auto& client : delivery.clients)
            require(client->confirmedGroundItemBaseline()->items.empty()
                && client->confirmedGroundItemBaseline()->nativePlacements.size() == 1, "Pickup not removed for both clients");
        const auto resumed = players(*SessionGeneration::initial().next());
        Delivery reconnect(clock, *SessionGeneration::initial().next());
        publish(service, resumed, reconnect, 3);
        require(reconnect.clients[1]->confirmedGroundItemBaseline()->items.empty(), "Reconnect respawned placed item");
        InventoryService recovered(content.store, content.readers, binding, true);
        recovered.recover(service.inventoryImage(), references);
        require(recovered.projectInventory(resumed, id<SessionId>(1), id<ServerTick>(3), id<CanonicalRevision>(3))
            ->groundItems.front().items.empty(), "Restart refilled picked world placement");
        auto view = recovered.projectInventory(resumed, id<SessionId>(1), id<ServerTick>(3), id<CanonicalRevision>(3)).value();
        const auto itemIndex = size_t(std::ranges::find(view.playerInventory.front().stacks,
            id<ItemPrototypeId>(MWWorld::inventoryRecordId(gem.mId)), &CanonicalItemStack::prototypeId) - view.playerInventory.front().stacks.begin());
        const auto picked = view.playerInventory.front().stacks.at(itemIndex);
        require(picked.count == 3 && picked.condition == std::bit_cast<uint32_t>(ref.mChargeInt)
            && picked.enchantmentCharge == std::bit_cast<uint32_t>(ref.mEnchantmentCharge)
            && picked.soulPrototype == id<ActorPrototypeId>(MWWorld::inventoryRecordId(soul)), "Pickup lost item properties");
        auto dropInput = worldWire(recovered, resumed, 1, false, itemIndex, 2);
        auto drop = recovered.prepareInventory(resumed, bind(resumed, dropInput).proposal());
        require(drop && drop->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
            "Partial world drop failed");
        auto dropped = recovered.projectInventory(resumed, id<SessionId>(2), id<ServerTick>(4), id<CanonicalRevision>(4)).value();
        require(dropped.groundItems[0].items[0].stack.count == 2
            && dropped.groundItems[0].items[0].position == Position3(0, 0, 0)
            && dropped.groundItems[0].items[0].stack.stackId != picked.stackId,
            "Drop did not split at server position with a fresh identity");
        const auto droppedState = decode(recovered.inventoryImage());
        const auto& droppedRef = droppedState.mWorldItems->mObjects.front().mRef;
        require(droppedRef.mChargeIntRemainder == ref.mChargeIntRemainder && droppedRef.mScale == ref.mScale
            && droppedRef.mSoul == ref.mSoul && droppedRef.mChargeInt == ref.mChargeInt
            && droppedRef.mEnchantmentCharge == ref.mEnchantmentCharge, "World loop lost saved instance fields");
        // Invalid restore remains unpublished and cannot fall back to base loot.
        auto corrupt = droppedState;
        corrupt.mWorldItems->mObjects[0].mRef.mRefNum = corrupt.mActors[0].mObjects[0].mRef.mRefNum;
        EquipmentBytes invalid;
        bool rejected = false;
        try { encodeEquipmentSession(corrupt, actorBindings, invalid, containerBindings, &actorBindings[0]); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && invalid.empty(), "World/inventory alias admitted into coherent image");
        InventoryService malformed(content.store, content.readers, binding, true);
        rejected = false;
        try { malformed.recover(recovered.inventoryImage().first(recovered.inventoryImage().size() - 1), references); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && malformed.inventoryImage().empty(), "Truncated world restore published or refilled");
        InventoryService afterDrop(content.store, content.readers, binding, true);
        afterDrop.recover(recovered.inventoryImage(), references);
        for (size_t round = 0; round < 70; ++round)
        {
            auto take = afterDrop.prepareInventory(resumed, bind(resumed, worldWire(afterDrop, resumed, 2, true)).proposal());
            require(take && take->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                "Dropped item pickup failed or identities exhausted");
            if (round == 69) break;
            const auto current = afterDrop.projectInventory(resumed, id<SessionId>(2), id<ServerTick>(5), id<CanonicalRevision>(5)).value();
            const auto& items = current.playerInventory[0].stacks;
            const auto index = size_t(std::ranges::find(items, picked.prototypeId, &CanonicalItemStack::prototypeId) - items.begin());
            auto put = afterDrop.prepareInventory(resumed, bind(resumed, worldWire(afterDrop, resumed, 2, false, index, 2)).proposal());
            require(put && put->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                "Repeated world drop failed");
        }
        const auto final = afterDrop.projectInventory(resumed, id<SessionId>(2), id<ServerTick>(6), id<CanonicalRevision>(6)).value();
        require(final.groundItems[0].items.empty(), "Drop/pickup loop left a world duplicate");
        auto fullBinding = binding;
        fullBinding.mWorldItems->mPlacements.clear();
        for (uint32_t i = 0; i < 64; ++i)
        {
            auto placed = ref; placed.mRefNum.mIndex = 501 + i;
            fullBinding.mWorldItems->mPlacements.emplace_back(MWWorld::PlacedRefTag | (501 + i), placed);
        }
        InventoryService fullWorld(content.store, content.readers, fullBinding);
        const auto fullImage = std::vector(fullWorld.inventoryImage().begin(), fullWorld.inventoryImage().end());
        require(!fullWorld.prepareInventory(authority, bind(authority, worldWire(fullWorld, authority, 1, false)).proposal())
            && std::ranges::equal(fullImage, fullWorld.inventoryImage()), "World capacity rejection changed inventory");
        auto foreign = worldWire(fullWorld, authority, 1, false);
        foreign.stackId = worldWire(fullWorld, authority, 2, false).stackId;
        require(!fullWorld.prepareInventory(authority, bind(authority, foreign).proposal()), "Foreign inventory drop admitted");
        // Full inventory: 64 distinct records, no partial pickup or removal.
        auto fullNpc = *content.store.get<ESM::NPC>().find(content.actor);
        for (int i = 0; i < 64; ++i)
        {
            ESM::Miscellaneous item; item.blank(); item.mId = ESM::RefId::stringRefId("full_world_" + std::to_string(i));
            content.store.insertStatic(item); fullNpc.mInventory.mList.push_back({1, item.mId});
        }
        content.store.overrideRecord(fullNpc);
        auto fullInventoryBinding = binding; fullInventoryBinding.mShirt.reset();
        for (auto& actor : fullInventoryBinding.mActors) actor = {content.actor, {}, 0, false, true};
        InventoryService fullInventory(content.store, content.readers, fullInventoryBinding);
        require(!fullInventory.prepareInventory(authority, bind(authority, worldWire(fullInventory, authority, 1, true)).proposal())
            && fullInventory.projectInventory(authority, id<SessionId>(2), id<ServerTick>(1), id<CanonicalRevision>(1))
                ->groundItems.front().items.size() == 1, "Inventory capacity failure removed the world item");
        auto emptyBinding = binding;
        emptyBinding.mContainers.clear(); emptyBinding.mWorldItems->mPlacements.clear();
        InventoryService emptyWorld(content.store, content.readers, emptyBinding);
        const auto empty = emptyWorld.projectInventory(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1));
        require(empty && empty->containers.empty() && empty->groundItems.front().items.empty()
            && empty->groundItems.front().nativeWorld, "Empty native world omitted its domain marker");
        auto emptyDrop = emptyWorld.prepareInventory(authority, bind(authority, worldWire(emptyWorld, authority, 1, false)).proposal());
        require(emptyDrop && emptyDrop->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
            "Drop into an empty world-only domain failed");
        InventoryService emptyRestored(content.store, content.readers, emptyBinding, true);
        emptyRestored.recover(emptyWorld.inventoryImage(), references);
        require(std::ranges::equal(emptyWorld.inventoryImage(), emptyRestored.inventoryImage()), "Empty-domain world recovery changed image");
        // Uncertain durability closes both projection and mutation until recovery.
        InventoryService uncertain(content.store, content.readers, binding);
        auto pending = uncertain.prepareInventory(authority, bind(authority, worldWire(uncertain, authority, 1, true)).proposal());
        require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Failed; }) == CanonicalDurabilityResult::Failed
            && uncertain.inventoryImage().empty() && !uncertain.projectInventory(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)),
            "Uncertain world durability left service open");
        std::cout << "synthetic world items: coherent two-client pickup, contention, fields, retry/reconnect/restart, partial drop and repeated pickup\n";
    }

    void checkBulkTakeAll(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Bulk inventory scratch already exists");
        Content content;
        const auto ref = [](const char* text) { return ESM::RefId::stringRefId(text); };
        ESM::Weapon bow; bow.blank(); bow.mId = ref("bulk_bow");
        bow.mData.mType = ESM::Weapon::MarksmanBow; bow.mData.mHealth = 100; bow.mData.mChop[1] = 12;
        content.store.insertStatic(bow);
        auto arrow = bow; arrow.mId = ref("bulk_arrow"); arrow.mData.mType = ESM::Weapon::Arrow;
        content.store.insertStatic(arrow);
        auto chest = *content.store.get<ESM::Container>().find(content.container);
        chest.mInventory.mList = {{2, content.shirt}, {1, bow.mId}, {5, arrow.mId}};
        content.store.overrideRecord(chest);
        auto dead = *content.store.get<ESM::NPC>().find(content.actor);
        dead.mId = ref("bulk_corpse"); dead.mNpdt.mHealth = 0; dead.mNpdt.mSkills.fill(30);
        dead.mInventory = chest.mInventory; content.store.insertStatic(dead);
        auto living = dead; living.mId = ref("bulk_living"); living.mNpdt.mHealth = 40;
        content.store.insertStatic(living);
        ESM::Creature creature; creature.blank(); creature.mId = ref("bulk_creature");
        creature.mData.mHealth = 0; creature.mData.mCombat = 30; creature.mFlags |= ESM::Creature::Weapon;
        creature.mInventory = chest.mInventory; content.store.insertStatic(creature);
        std::vector<ESM::RefId> references{content.actor, content.shirt, content.container,
            bow.mId, arrow.mId, dead.mId, living.mId, creature.mId};
        const auto authority = players();
        const auto view = [&](InventoryService& service, uint64_t session = 1) {
            return service.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto intent = [&](InventoryService& service, uint64_t session = 1) {
            const auto snapshot = view(service, session);
            const auto& source = snapshot.containers.front();
            const auto& witness = source.stacks.back(); // Not necessarily first in stock order.
            return ClientInventoryTransactionCommand{id<SessionId>(session), SessionGeneration::initial(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                InventoryTransactionKind::TakeAllFromContainer, source.container, witness.prototypeId,
                witness.stackId, 1, {}, snapshot.playerInventory.front().revision, source.revision, {}, Position3(0, 0, 0)};
        };
        for (auto base : {chest.mId, dead.mId, creature.mId})
        {
            auto binding = content.binding();
            ESM::CellRef placement; placement.blank(); placement.mRefNum = {1, 0}; placement.mRefID = base;
            binding.mContainers.front().mBase = base;
            binding.mContainers.front().mPlacement = placement;
            auto livePlacement = placement; livePlacement.mRefNum = {2, 0}; livePlacement.mRefID = living.mId;
            binding.mContainers.push_back({id<ContainerId>(91), CellId::interior(id<CellSpaceId>(7)),
                Position3(0, 0, 0), living.mId, livePlacement});
            InventoryService service(content.store, content.readers, binding);
            const auto initial = view(service);
            const auto other = view(service, 2).playerInventory.front();
            const auto input = intent(service);
            const auto rival = intent(service, 2);
            const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
            for (int invalid = 0; invalid < 5; ++invalid)
            {
                auto bad = input;
                switch (invalid)
                {
                    case 0: bad.containerId = id<ContainerId>(91); break; // Living owner.
                    case 1: bad.interactionOrigin = Position3(1'000'000, 0, 0); break;
                    case 2: bad.expectedInventoryRevision = InventoryRevision::initial(); break;
                    case 3: bad.stackId = other.stacks.front().stackId; bad.prototypeId = other.stacks.front().prototypeId; break;
                    case 4: bad.expectedContainerRevision = ContainerRevision::initial(); break;
                }
                require(!service.prepareInventory(authority, bind(authority, bad).proposal()), "Invalid bulk access accepted");
            }
            auto staleSession = input; staleSession.sessionGeneration = id<SessionGeneration>(2);
            require(!ServerApp::InventoryCommandBinding::resolve(authority, input.sessionId,
                input.sessionGeneration, staleSession), "Bulk command crossed session generations");
            auto prepared = service.prepareInventory(authority, bind(authority, input).proposal());
            auto contending = service.prepareInventory(authority, bind(authority, rival).proposal());
            require(prepared && contending, "Bulk preparation failed");
            for (uint64_t session : {1, 2})
            {
                const auto candidate = service.projectInventory(authority, id<SessionId>(session), id<ServerTick>(1),
                    id<CanonicalRevision>(1), prepared.get());
                require(candidate && candidate->containers.front().stacks.empty()
                    && candidate->containers.front().equipment.empty(), "Bulk candidate did not empty source/slots for both clients");
            }
            require(std::ranges::equal(before, service.inventoryImage())
                && view(service).containers.front().stacks == initial.containers.front().stacks,
                "Bulk preparation mutated live state");
            size_t writes = 0;
            require(prepared->commit([&](auto) { ++writes; return CanonicalDurabilityResult::Rejected; })
                == CanonicalDurabilityResult::Rejected && writes == 1
                && std::ranges::equal(before, service.inventoryImage()), "Failed bulk durability partially installed");
            std::vector<std::byte> saved;
            MWWorld::Testing::Allocations::Trace trace;
            CanonicalDurabilityResult committed;
            {
                MWWorld::Testing::Allocations::Observe observe(trace);
                committed = prepared->commit([&](auto image) {
                    ++writes; saved.assign(image.begin(), image.end()); return CanonicalDurabilityResult::Committed;
                });
            }
            require(committed == CanonicalDurabilityResult::Committed && writes == 2
                && trace.visits(Allocations::Phase::Installation) > 0
                && trace.allocations(Allocations::Phase::Installation) == 0
                && trace.allocations(Allocations::Phase::Publication) == 0, "Bulk commit was not one nonthrowing installation");
            const auto result = view(service);
            require(result.containers.front().stacks.empty() && result.containers.front().equipment.empty()
                && view(service, 2).containers.front().stacks.empty(), "Bulk transfer left source items or equipment");
            std::map<ItemPrototypeId, uint32_t> expected, actual;
            for (const auto& stack : initial.playerInventory.front().stacks) expected[stack.prototypeId] += stack.count;
            for (const auto& stack : initial.containers.front().stacks) expected[stack.prototypeId] += stack.count;
            for (const auto& stack : result.playerInventory.front().stacks) actual[stack.prototypeId] += stack.count;
            require(actual == expected && result.playerInventory.front().stacks.size() == 3
                && view(service, 2).playerInventory.front().stacks == other.stacks,
                "Bulk transfer lost/duplicated counts, failed stock merging, or changed the other player");
            require(!service.prepareInventory(authority, bind(authority, input).proposal())
                && !service.prepareInventory(authority, bind(authority, rival).proposal()), "Bulk replay or stale contender applied");
            require(contending->commit([&](auto) { ++writes; return CanonicalDurabilityResult::Committed; })
                == CanonicalDurabilityResult::Rejected && writes == 2, "Stale bulk preparation reached durability");
            InventoryService restored(content.store, content.readers, binding, true);
            restored.recover(saved, references);
            require(view(restored).playerInventory.front().stacks == result.playerInventory.front().stacks
                && view(restored, 2).playerInventory.front().stacks == other.stacks
                && view(restored).containers.front().stacks.empty() && view(restored).containers.front().equipment.empty(),
                "Bulk restart refilled source or lost player items");
            InventoryService uncertain(content.store, content.readers, binding);
            auto pending = uncertain.prepareInventory(authority, bind(authority, intent(uncertain)).proposal());
            require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Failed; })
                == CanonicalDurabilityResult::Failed && uncertain.inventoryImage().empty()
                && !uncertain.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)),
                "Uncertain bulk durability left service open");
        }
        // Full bounded source, then destination overflow after several otherwise
        // valid transfers. Both outcomes must use the same detached writer.
        chest.mInventory.mList.clear();
        for (unsigned i = 0; i < 64; ++i)
        {
            ESM::Miscellaneous item; item.blank(); item.mId = ESM::RefId::stringRefId("bulk_item_" + std::to_string(i));
            content.store.insertStatic(item); chest.mInventory.mList.push_back({int(i + 1), item.mId});
        }
        content.store.overrideRecord(chest);
        auto binding = content.binding();
        binding.mShirt.reset();
        binding.mActors = {{{content.actor, {}, 0, false, true}, {content.actor, {}, 0, false, true}}};
        InventoryService maximum(content.store, content.readers, binding);
        auto all = maximum.prepare(authority, bind(authority, intent(maximum)));
        struct Sink final : EquipmentSessionCommitter
        {
            PersistenceResult commit(std::span<const char>) noexcept override { return PersistenceResult::Accepted; }
        } sink;
        std::unique_ptr<const InventoryTransferSuccess> success; EquipmentBytes bytes;
        require(maximum.commit(authority, all, sink, success, bytes) == PersistenceResult::Accepted
            && view(maximum).containers.front().stacks.empty() && view(maximum).playerInventory.front().stacks.size() == 64,
            "Maximum 64-stack Take All failed");
        struct Consumer final : InventoryNotificationConsumer
        {
            size_t calls = 0;
            void receive(InventoryNotificationIntent) override { ++calls; }
        } consumer;
        require(consumeInventoryNotifications(success, consumer).mStatus == InventoryNotificationDeliveryStatus::Delivered
            && consumer.calls == 64 * 4, "Bulk notifications omitted stacks");
        require(consumeInventoryNotifications(success, consumer).mStatus == InventoryNotificationDeliveryStatus::NoPendingSuccess,
            "Bulk notifications replayed");
        // An equipped shirt consumes the 65th destination node.
        auto carrier = *content.store.get<ESM::NPC>().find(content.actor);
        carrier.mId = ref("bulk_carrier"); carrier.mInventory.mList = {{1, content.shirt}};
        content.store.insertStatic(carrier);
        binding.mActors[0].mBase = carrier.mId;
        InventoryService overflow(content.store, content.readers, binding);
        const std::vector prior(overflow.inventoryImage().begin(), overflow.inventoryImage().end());
        require(!overflow.prepareInventory(authority, bind(authority, intent(overflow)).proposal())
            && std::ranges::equal(prior, overflow.inventoryImage())
            && view(overflow).containers.front().stacks.size() == 64, "Bulk capacity rejection transferred a prefix");
    }

    void checkWorldActorInventories(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Actor inventory scratch already exists");
        Content content;
        const auto ref = [](const char* text) { return ESM::RefId::stringRefId(text); };
        ESM::Weapon bow; bow.blank(); bow.mId = ref("corpse_bow");
        bow.mData.mType = ESM::Weapon::MarksmanBow; bow.mData.mHealth = 100; bow.mData.mChop[1] = 12;
        content.store.insertStatic(bow);
        auto arrow = bow; arrow.mId = ref("corpse_arrow"); arrow.mData.mType = ESM::Weapon::Arrow;
        arrow.mData.mChop[1] = 4; content.store.insertStatic(arrow);
        ESM::ItemLevList list; list.blank(); list.mId = ref("corpse_leveled"); list.mChanceNone = 0;
        list.mList.push_back({arrow.mId, 1}); content.store.insertStatic(list);
        auto dead = *content.store.get<ESM::NPC>().find(content.actor);
        dead.mId = ref("corpse_npc"); dead.mNpdt.mHealth = 0; dead.mNpdt.mSkills.fill(30);
        dead.mInventory.mList = {{2, content.shirt}, {1, bow.mId}, {3, arrow.mId}, {2, list.mId}};
        content.store.insertStatic(dead);
        auto live = dead; live.mId = ref("living_npc"); live.mNpdt.mHealth = 40; content.store.insertStatic(live);
        ESM::Creature creature; creature.blank(); creature.mId = ref("corpse_creature");
        creature.mData.mHealth = 0; creature.mInventory.mList = {{4, arrow.mId}}; content.store.insertStatic(creature);
        auto armed = creature; armed.mId = ref("corpse_armed_creature"); armed.mFlags |= ESM::Creature::Weapon;
        armed.mData.mCombat = 30; armed.mInventory.mList = {{1, bow.mId}, {4, arrow.mId}}; content.store.insertStatic(armed);
        auto liveArmed = armed; liveArmed.mId = ref("living_armed_creature"); liveArmed.mData.mHealth = 40;
        content.store.insertStatic(liveArmed);
        auto liveUnarmed = creature; liveUnarmed.mId = ref("living_unarmed_creature"); liveUnarmed.mData.mHealth = 40;
        content.store.insertStatic(liveUnarmed);
        auto bare = live; bare.mId = ref("living_bare_npc"); bare.mInventory.mList.clear(); content.store.insertStatic(bare);
        auto binding = content.binding(); binding.mShirt.reset();
        binding.mActors = {{{content.actor, {}, 0, false, true}, {content.actor, {}, 0, false, true}}};
        for (auto base : {dead.mId, live.mId, creature.mId, armed.mId, liveArmed.mId, liveUnarmed.mId, bare.mId})
        {
            const auto index = static_cast<uint32_t>(binding.mContainers.size());
            ESM::CellRef placement; placement.blank(); placement.mRefNum = {index, 0}; placement.mRefID = base;
            binding.mContainers.push_back({id<ContainerId>(90 + index), CellId::interior(id<CellSpaceId>(7)),
                Position3(0, 0, 0), base, placement});
        }
        std::vector<ESM::RefId> references{content.actor, content.shirt, content.container, bow.mId, arrow.mId,
            dead.mId, live.mId, creature.mId, armed.mId, liveArmed.mId, liveUnarmed.mId, bare.mId};
        auto authority = players();
        const auto view = [&](InventoryService& service, uint64_t session = 1) {
            return service.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto shared = [&](InventoryService& service, uint64_t owner) {
            const auto current = view(service);
            const auto found = std::ranges::find(current.containers, id<ContainerId>(owner), &ReliableContainerInventoryBaseline::container);
            require(found != current.containers.end(), "Actor baseline missing");
            return *found;
        };
        InventoryService service(content.store, content.readers, binding);
        InventoryService twin(content.store, content.readers, binding);
        require(std::ranges::equal(service.inventoryImage(), twin.inventoryImage()), "Actor startup is not deterministic");
        require(view(service).containers.size() == 4 && shared(service, 91).equipment.size() == 3
            && shared(service, 94).equipment.size() == 2 && shared(service, 93).equipment.empty(),
            "NPC/creature storage or stock equipment selection incorrect, or living loot published");
        require(shared(service, 91).stacks.size() == 4, "NPC equipment split or leveled loot lost");
        const auto publicActors = view(service).equipment->actors;
        const auto prototype = [](const ESM::RefId& record) { return id<ItemPrototypeId>(MWWorld::inventoryRecordId(record)); };
        require(publicActors.size() == 4 && publicActors[0].actor == id<ContainerId>(92)
            && publicActors[0].slots[InventoryStore::Slot_Shirt] == prototype(content.shirt)
            && publicActors[0].slots[InventoryStore::Slot_CarriedRight] == prototype(bow.mId)
            && publicActors[0].slots[InventoryStore::Slot_Ammunition] == prototype(arrow.mId)
            && publicActors[1].actor == id<ContainerId>(95)
            && publicActors[1].slots[InventoryStore::Slot_CarriedRight] == prototype(bow.mId)
            && publicActors[1].slots[InventoryStore::Slot_Ammunition] == prototype(arrow.mId)
            && std::ranges::none_of(publicActors[2].slots, [](auto slot) { return slot.has_value(); })
            && std::ranges::none_of(publicActors[3].slots, [](auto slot) { return slot.has_value(); })
            && view(service, 2).equipment->actors == publicActors,
            "Living NPC/creature public slots differ between clients or expose non-equipped loot");
        auto elsewhere = binding;
        for (auto& owner : elsewhere.mContainers) owner.mCell = CellId::interior(id<CellSpaceId>(8));
        InventoryService outOfInterest(content.store, content.readers, elsewhere);
        require(view(outOfInterest).equipment->actors.empty() && view(outOfInterest).containers.empty(),
            "Out-of-cell actor equipment was broadcast");
        {
            auto model = std::make_unique<MWGui::InventoryItemModel>(MWWorld::Ptr{});
            auto* owner = model.get();
            MWGui::SortFilterItemModel view(std::make_unique<MWGui::SortFilterItemModel>(std::move(model)));
            require(&view.getTransferTarget() == owner, "Quick-transfer proxies lost their inventory owner");
        }
        // Exercise the exact engine presentation helpers used by the desktop
        // adapter, without constructing a renderer, scripts or global player.
        {
            MWWorld::WorldModel localWorld(content.store, content.readers, 1);
            MWWorld::LocalScripts localScripts(content.store);
            MWWorld::InventoryStore local;
            local.clearAuthoritative(localScripts);
            MWWorld::ManualRef source(content.store, content.shirt);
            const auto first = *local.addAuthoritative(source.getPtr(), 1, localWorld);
            const auto second = *local.addAuthoritative(source.getPtr(), 1, localWorld);
            require(first != second && local.count(content.shirt) == 2, "Remote split stacks merged");
            const std::array slots{std::pair{MWWorld::InventoryStore::Slot_Shirt, first}};
            local.applyAuthoritativeEquipment(slots);
            require(*local.getSlot(MWWorld::InventoryStore::Slot_Shirt) == first, "Remote equipment selected a different instance");
            bool rejected = false;
            try { const std::array duplicate{slots[0], slots[0]}; local.applyAuthoritativeEquipment(duplicate); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && *local.getSlot(MWWorld::InventoryStore::Slot_Shirt) == first,
                "Invalid remote slots changed presentation equipment");
            local.clearAuthoritative(localScripts);
            const auto repeatedFirst = *local.addAuthoritative(source.getPtr(), 1, localWorld);
            const auto repeatedSecond = *local.addAuthoritative(source.getPtr(), 1, localWorld);
            require(repeatedFirst == first && repeatedSecond == second && local.count(content.shirt) == 2,
                "Repeated baseline leaked retired presentation nodes");
            const std::array appearance{std::pair{InventoryStore::Slot_Shirt, content.shirt},
                std::pair{InventoryStore::Slot_CarriedRight, bow.mId},
                std::pair{InventoryStore::Slot_Ammunition, arrow.mId}};
            local.applyAuthoritativeAppearance(appearance, content.store, localScripts, localWorld);
            require(local.count(content.shirt) == 1 && local.count(arrow.mId) == 1
                && local.getSlot(InventoryStore::Slot_Ammunition) != local.end(),
                "Public appearance retained local private loot or lost ammunition");
            const auto displayed = *local.getSlot(InventoryStore::Slot_Shirt);
            local.applyAuthoritativeAppearance(appearance, content.store, localScripts, localWorld);
            require(*local.getSlot(InventoryStore::Slot_Shirt) == displayed, "Identical public appearance replaced nodes");
            for (const auto& invalid : std::vector<std::vector<std::pair<int, ESM::RefId>>>{
                    {{InventoryStore::Slot_Shirt, content.shirt}, {InventoryStore::Slot_Shirt, content.shirt}},
                    {{InventoryStore::Slot_Shirt, content.shirt}, {InventoryStore::Slot_Helmet, arrow.mId}},
                    {{InventoryStore::Slot_Shirt, content.shirt}, {InventoryStore::Slot_Helmet, ref("missing")}}})
            {
                rejected = false;
                try { local.applyAuthoritativeAppearance(invalid, content.store, localScripts, localWorld); }
                catch (const std::exception&) { rejected = true; }
                require(rejected && local.count(arrow.mId) == 1 && *local.getSlot(InventoryStore::Slot_Shirt) == displayed,
                    "Malformed public appearance partially installed");
            }
            local.applyAuthoritativeAppearance({}, content.store, localScripts, localWorld);
            require(local.begin() == local.end() && local.getSlot(InventoryStore::Slot_Shirt) == local.end(),
                "Empty public appearance kept local equipment");
        }
        const auto intent = [&](InventoryService& current, uint64_t session, uint64_t owner, CanonicalItemStack item,
            bool put = false, uint32_t count = 0) {
            const auto snapshot = view(current, session);
            return ClientInventoryTransactionCommand{id<SessionId>(session),
                authority.findActiveSession(id<SessionId>(session))->sessionGeneration(), CommandSequence::initial(),
                id<CommandId>(1), id<CanonicalRevision>(1),
                put ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                id<ContainerId>(owner), item.prototypeId, item.stackId, count ? count : item.count, {},
                snapshot.playerInventory.front().revision, shared(current, 91).revision, {}, Position3(0, 0, 0)};
        };
        EquipmentFileSink file(scratch / "actors.equipment", true);
        FileFaults faults; EquipmentFileCommitter sink(file, faults);
        EquipmentBytes bytes; std::unique_ptr<const InventoryTransferSuccess> success;
        const auto initial = shared(service, 91);
        const auto slot = std::ranges::find(initial.equipment, EquipmentSlot::Shirt, &EquipmentBinding::slot);
        const auto worn = *std::ranges::find(initial.stacks, slot->stackId, &CanonicalItemStack::stackId);
        for (auto bad : {intent(service, 1, 92, worn), intent(service, 1, 90, worn)})
            require(!service.prepareInventory(authority, bind(authority, bad).proposal()), "Live or foreign owner accepted corpse loot");
        auto distant = intent(service, 1, 91, worn); distant.interactionOrigin = Position3(1'000'000, 0, 0);
        require(!service.prepareInventory(authority, bind(authority, distant).proposal()), "Distant corpse loot accepted");
        const auto take = intent(service, 1, 91, worn);
        auto prepared = service.prepare(authority, bind(authority, take));
        const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
        faults = {FileFault::Flush};
        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Rejected
            && std::ranges::equal(before, service.inventoryImage()) && shared(service, 91).equipment == initial.equipment,
            "Failed corpse loot changed live equipment or inventory");
        faults = {};
        MWWorld::Testing::Allocations::Trace trace;
        PersistenceResult committed;
        {
            MWWorld::Testing::Allocations::Observe observe(trace);
            committed = service.commit(authority, prepared, sink, success, bytes);
        }
        require(trace.visits(Allocations::Phase::Installation) > 0 && trace.allocations(Allocations::Phase::Installation) == 0
            && trace.allocations(Allocations::Phase::Publication) == 0, "Corpse installation allocated after durability");
        require(committed == PersistenceResult::Accepted
            && shared(service, 91).equipment.size() == 2, "Equipped corpse item was not removed atomically");
        require(!service.prepareInventory(authority, bind(authority, take).proposal()), "Retried corpse loot duplicated an item");
        // An equipped ammunition stack keeps its slot during partial looting.
        auto npc = shared(service, 91);
        const auto ammoSlot = std::ranges::find(npc.equipment, EquipmentSlot::Ammunition, &EquipmentBinding::slot);
        const auto ammo = *std::ranges::find(npc.stacks, ammoSlot->stackId, &CanonicalItemStack::stackId);
        auto partial = service.prepare(authority, bind(authority, intent(service, 2, 91, ammo, false, 2)));
        require(service.commit(authority, partial, sink, success, bytes) == PersistenceResult::Accepted
            && shared(service, 91).equipment == npc.equipment, "Partial ammunition loot changed the remaining slots");
        // Every remaining NPC/creature item, including armed-creature slots, uses the same writer.
        for (uint64_t owner : {91, 93, 94})
            for (const auto item : shared(service, owner).stacks)
            {
                auto next = service.prepare(authority, bind(authority, intent(service, 2, owner, item)));
                require(service.commit(authority, next, sink, success, bytes) == PersistenceResult::Accepted,
                    "NPC/creature loot transfer failed");
            }
        for (uint64_t owner : {91, 93, 94})
            require(shared(service, owner).stacks.empty() && shared(service, owner).equipment.empty(), "Corpse did not empty");
        for (uint64_t owner : {92, 95, 96, 97})
            for (uint64_t session : {1, 2})
            {
                auto put = intent(service, session, owner, view(service, session).playerInventory.front().stacks.front(), true);
                require(!service.prepareInventory(authority, bind(authority, put).proposal()), "Public appearance granted living inventory access");
            }
        const auto received = view(service).playerInventory.front().stacks.front();
        auto put = service.prepare(authority, bind(authority, intent(service, 1, 91, received, true)));
        require(service.commit(authority, put, sink, success, bytes) == PersistenceResult::Accepted
            && shared(service, 91).equipment.empty(), "Put into corpse incorrectly selected replacement gear");
        // A small live inventory must survive more exchanges than the storage
        // and pending-effect budgets. Retired stacks are not carried loot.
        for (unsigned exchange = 0; exchange < 512; ++exchange)
        {
            try
            {
                const bool putBack = exchange % 2 != 0;
                const auto item = putBack ? view(service).playerInventory.front().stacks.front()
                                          : shared(service, 91).stacks.front();
                auto next = service.prepare(authority, bind(authority, intent(service, 1, 91, item, putBack)));
                if (exchange == 80)
                {
                    const std::vector prior(service.inventoryImage().begin(), service.inventoryImage().end());
                    faults = {FileFault::Flush};
                    require(service.commit(authority, next, sink, success, bytes) == PersistenceResult::Rejected
                        && std::ranges::equal(prior, service.inventoryImage()), "Failed retirement changed the saved inventory");
                    faults = {};
                }
                require(service.commit(authority, next, sink, success, bytes) == PersistenceResult::Accepted,
                    "Repeated corpse exchange did not commit");
                const auto contents = putBack ? shared(service, 91).stacks : view(service).playerInventory.front().stacks;
                require(contents.size() == 1 && contents.front().count == received.count
                    && contents.front().stackId != item.stackId,
                    "Repeated corpse exchange changed the live stack count");
                auto stale = intent(service, 1, 91, item, putBack);
                require(!service.prepareInventory(authority, bind(authority, stale).proposal()),
                    "Retired stack identity was accepted with fresh revisions");
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error("Corpse exchange " + std::to_string(exchange) + ": " + error.what());
            }
        }
        const std::vector saved(service.inventoryImage().begin(), service.inventoryImage().end());
        InventoryService broken(content.store, content.readers, binding, true);
        bool rejected = false;
        try { broken.recover(std::span(saved).first(saved.size() - 1), references); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && broken.inventoryImage().empty(), "Partial actor recovery published a world");
        auto wrongShape = binding;
        wrongShape.mContainers[1].mBase = content.container;
        wrongShape.mContainers[1].mPlacement->mRefID = content.container;
        InventoryService wrong(content.store, content.readers, wrongShape, true);
        rejected = false;
        try { wrong.recover(before, references); } catch (const std::exception&) { rejected = true; }
        require(rejected && wrong.inventoryImage().empty(), "Actor equipment restored into plain container storage");
        InventoryService uncertain(content.store, content.readers, binding);
        auto pending = uncertain.prepareInventory(authority, bind(authority, intent(uncertain, 1, 91, worn)).proposal());
        require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Failed; }) == CanonicalDurabilityResult::Failed
            && uncertain.inventoryImage().empty() && !uncertain.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)),
            "Uncertain corpse transfer stayed open");
        // Recovery must not load any actor loot or auto-select equipment.
        const_cast<ESM::NPC*>(content.store.get<ESM::NPC>().find(dead.mId))->mInventory.mList = {{1, ref("missing_loot")}};
        const_cast<ESM::Creature*>(content.store.get<ESM::Creature>().find(armed.mId))->mInventory.mList = {{1, ref("missing_loot")}};
        const_cast<ESM::NPC*>(content.store.get<ESM::NPC>().find(live.mId))->mInventory.mList = {{1, ref("missing_loot")}};
        const_cast<ESM::Creature*>(content.store.get<ESM::Creature>().find(liveArmed.mId))->mInventory.mList = {{1, ref("missing_loot")}};
        InventoryService restored(content.store, content.readers, binding, true);
        restored.recover(saved, references);
        require(std::ranges::equal(saved, restored.inventoryImage()) && shared(restored, 91).equipment.empty()
            && shared(restored, 94).stacks.empty(), "Recovery rerolled actor loot or equipment");
        Clock clock; Delivery delivery(clock, SessionGeneration::initial()); publish(restored, authority, delivery, 2);
        for (const auto& client : delivery.clients)
            require(client->confirmedContainerInventoryBaselines().size() == 4
                && client->confirmedEquipmentSnapshot()->actors == publicActors, "Late join lost public equipment or exposed living loot");
        authority = players(SessionGeneration::initial().next().value());
        Delivery reconnect(clock, SessionGeneration::initial().next().value()); publish(restored, authority, reconnect, 3);
        for (auto& client : reconnect.clients)
        {
            const auto current = *client->confirmedEquipmentSnapshot();
            require(current.actors == publicActors, "Reconnect lost saved living equipment");
            auto stale = current; stale.serverTick = id<ServerTick>(2); stale.actors.clear();
            require(client->receiveLatestWinsEquipmentSnapshot(stale) == InventoryReplicationReceiveResult::StaleTick,
                "Stale equipment replaced living appearance");
            stale = current; stale.targetSessionGeneration = SessionGeneration::initial();
            require(client->receiveLatestWinsEquipmentSnapshot(stale) == InventoryReplicationReceiveResult::GenerationMismatch
                && client->confirmedEquipmentSnapshot()->actors == publicActors, "Old session replaced living appearance");
        }
        // The actual protocol/client path must preserve occupied slots too.
        Delivery equipped(clock, SessionGeneration::initial()); publish(twin, players(), equipped, 1);
        require(equipped.clients[0]->confirmedContainerInventoryBaselines()[1].equipment == initial.equipment,
            "Wire/client assembly lost corpse equipment");
        auto malformed = initial.equipment; malformed[1].stackId = malformed[0].stackId;
        require(std::holds_alternative<InventoryReplicationDecodeError>(ReliableContainerInventoryBaseline::create(
            initial.header, initial.container, initial.cell, initial.position, initial.revision, 0, initial.stacks, malformed)),
            "Duplicate corpse equipment identity accepted by protocol");
        std::cout << "world actors: NPC/creature loot, stock equipment, leveled/split identities, live/reach/foreign guards, "
            "equipped and partial-ammo looting, failed durability/retry, 512 corpse exchanges, quick-transfer proxies, exact recovery without refill, "
            "public living NPC/creature slots without private loot, appearance replacement/validation, equipment wire/client assembly, "
            "late join/reconnect and stale-session rejection; synthetic transport\n";
    }

    void checkEquipmentSlots(const std::filesystem::path& scratch)
    {
        Content content;
        ESM::Race race; race.blank(); race.mId = ESM::RefId::stringRefId("equipment_human");
        content.store.insertStatic(race);
        ESM::NPC npc; npc.blank(); npc.mId = ESM::RefId::stringRefId("slotted_actor"); npc.mRace = race.mId;
        std::array<ESM::RefId, MWWorld::InventoryStore::Slots> bases{};
        std::vector<ESM::RefId> references{npc.mId};
        const auto add = [&]<class T>(int slot, int type) {
            T item; item.blank(); item.mId = ESM::RefId::stringRefId("slot_" + std::to_string(slot));
            item.mData.mType = type;
            if constexpr (std::is_same_v<T, ESM::Armor> || std::is_same_v<T, ESM::Weapon>) item.mData.mHealth = 100;
            if constexpr (std::is_same_v<T, ESM::Armor>)
                if (slot == 7) item.mParts.mParts.push_back({ESM::PRT_LFoot, {}, {}});
            content.store.insertStatic(item);
            bases[slot] = item.mId; references.push_back(item.mId);
            npc.mInventory.mList.push_back({3, item.mId});
        };
        for (auto [slot, type] : std::array<std::pair<int, int>, 9>{{
                {0, ESM::Armor::Helmet}, {1, ESM::Armor::Cuirass}, {2, ESM::Armor::Greaves},
                {3, ESM::Armor::LPauldron}, {4, ESM::Armor::RPauldron}, {5, ESM::Armor::LGauntlet},
                {6, ESM::Armor::RGauntlet}, {7, ESM::Armor::Boots}, {17, ESM::Armor::Shield}}})
            add.operator()<ESM::Armor>(slot, type);
        for (auto [slot, type] : std::array<std::pair<int, int>, 7>{{
                {8, ESM::Clothing::Shirt}, {9, ESM::Clothing::Pants}, {10, ESM::Clothing::Skirt},
                {11, ESM::Clothing::Robe}, {12, ESM::Clothing::Ring}, {14, ESM::Clothing::Amulet},
                {15, ESM::Clothing::Belt}}})
            add.operator()<ESM::Clothing>(slot, type);
        bases[13] = bases[12];
        add.operator()<ESM::Weapon>(16, ESM::Weapon::LongBladeTwoHand);
        add.operator()<ESM::Weapon>(18, ESM::Weapon::Arrow);
        ESM::Enchantment enchant; enchant.blank(); enchant.mId = ESM::RefId::stringRefId("slot_enchantment");
        content.store.insertStatic(enchant);
        auto enchanted = *content.store.get<ESM::Armor>().find(bases[0]);
        enchanted.mId = ESM::RefId::stringRefId("slot_enchanted"); enchanted.mEnchant = enchant.mId;
        content.store.insertStatic(enchanted); references.push_back(enchanted.mId);
        npc.mInventory.mList.push_back({1, enchanted.mId});
        auto broken = enchanted; broken.mId = ESM::RefId::stringRefId("slot_broken");
        broken.mEnchant = {}; broken.mData.mHealth = 0;
        content.store.insertStatic(broken); references.push_back(broken.mId);
        npc.mInventory.mList.push_back({1, broken.mId});
        content.store.insertStatic(npc);
        race.mId = ESM::RefId::stringRefId("equipment_beast"); race.mData.mFlags |= ESM::Race::Beast;
        content.store.insertStatic(race);
        auto beast = npc; beast.mId = ESM::RefId::stringRefId("equipment_beast_actor"); beast.mRace = race.mId;
        content.store.insertStatic(beast); references.push_back(beast.mId);
        auto binding = content.binding(); binding.mShirt.reset();
        binding.mActors = {{{npc.mId, {}, 0, false, true}, {beast.mId, {}, 0, false, true}}};
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        unequipStartingItems(service, authority);
        uint64_t tick = 1;
        const auto view = [&](InventoryService& source, uint64_t session = 1, const PreparedNativeInventory* candidate = nullptr) {
            return source.projectInventory(authority, id<SessionId>(session), id<ServerTick>(tick), id<CanonicalRevision>(tick), candidate).value();
        };
        const auto input = [&](InventoryService& source, ESM::RefId base, int slot, bool equip, uint64_t session = 1) {
            const auto inventory = view(source, session).playerInventory.front();
            const auto prototype = id<ItemPrototypeId>(MWWorld::inventoryRecordId(base));
            const auto stack = std::ranges::find_if(inventory.stacks, [&](const auto& stack) {
                const bool worn = std::ranges::any_of(inventory.equipment, [&](const auto& gear) { return gear.stackId == stack.stackId; });
                return stack.prototypeId == prototype && (equip ? !worn : std::ranges::any_of(inventory.equipment,
                    [&](const auto& gear) { return gear.stackId == stack.stackId && int(gear.slot) == slot; }));
            });
            require(stack != inventory.stacks.end(), "Equipment fixture stack unavailable");
            return ClientInventoryTransactionCommand{id<SessionId>(session), authority.findActiveSession(id<SessionId>(session))->sessionGeneration(),
                CommandSequence::initial(), id<CommandId>(tick), id<CanonicalRevision>(tick),
                equip ? InventoryTransactionKind::EquipItem : InventoryTransactionKind::UnequipItem, {}, prototype,
                stack->stackId, 1, static_cast<EquipmentSlot>(slot), inventory.revision, {}, {}, Position3(0, 0, 0)};
        };
        size_t writes = 0;
        const NativeInventoryCommit durable = [&](std::span<const std::byte> image) {
            require(!image.empty(), "Equipment durability omitted coherent image");
            ++writes; return CanonicalDurabilityResult::Committed;
        };
        const auto rejected = [&](const auto& command) {
            const std::vector prior(service.inventoryImage().begin(), service.inventoryImage().end());
            require(!service.prepareInventory(authority, bind(authority, command).proposal()), "Invalid equipment command admitted");
            require(std::ranges::equal(prior, service.inventoryImage()), "Rejected equipment changed saved image");
        };
        rejected(input(service, enchanted.mId, 0, true));
        rejected(input(service, broken.mId, 0, true));
        rejected(input(service, bases[7], 7, true, 2));
        rejected(input(service, bases[9], 0, true));
        auto foreign = input(service, bases[0], 0, true); foreign.sessionId = id<SessionId>(2); rejected(foreign);
        const auto other = view(service, 2).playerInventory.front().stacks;
        std::vector<int> occupied;
        for (int slot = 0; slot < int(bases.size()); ++slot)
        {
            if (bases[slot].empty()) continue;
            const auto command = input(service, bases[slot], slot, true);
            auto prepared = service.prepareInventory(authority, bind(authority, command).proposal());
            require(bool(prepared), "Ordinary equipment preparation rejected");
            auto stale = service.prepareInventory(authority, bind(authority, command).proposal());
            const auto staged = view(service, 1, prepared.get()).playerInventory.front();
            require(staged.equipment.size() == occupied.size() + 1
                && view(service).playerInventory.front().equipment.size() == occupied.size(), "Equipment candidate leaked or lost slots");
            if (slot == 0)
            {
                const std::vector prior(service.inventoryImage().begin(), service.inventoryImage().end());
                require(prepared->commit([](auto) { return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                    && view(service).playerInventory.front().equipment.empty() && std::ranges::equal(prior, service.inventoryImage()),
                    "Rejected durability installed equipment");
            }
            MWWorld::Testing::Allocations::Trace trace;
            CanonicalDurabilityResult result;
            {
                MWWorld::Testing::Allocations::Observe observe(trace);
                result = prepared->commit(durable);
            }
            require(result == CanonicalDurabilityResult::Committed
                && trace.visits(Allocations::Phase::Installation) > 0 && trace.visits(Allocations::Phase::Publication) > 0
                && trace.allocations(Allocations::Phase::Installation) == 0 && trace.allocations(Allocations::Phase::Publication) == 0,
                "Equipment durable install failed or allocated after durability");
            const auto calls = writes;
            require(stale->commit(durable) == CanonicalDurabilityResult::Rejected
                && prepared->commit(durable) == CanonicalDurabilityResult::Rejected && calls == writes,
                "Stale or consumed equipment reached durability");
            occupied.push_back(slot); ++tick;
            require(view(service).playerInventory.front().equipment == staged.equipment
                && view(service, 2).playerInventory.front().stacks == other, "Equipment changed wrong actor or candidate identities");
        }
        const auto equipped = view(service).playerInventory.front().equipment;
        require(equipped.size() == 19, "Not all clothing, armor and weapon slots were equipped");
        const auto ammo = std::ranges::find(equipped, EquipmentSlot::Ammunition, &EquipmentBinding::slot)->stackId;
        require(std::ranges::find(view(service).playerInventory.front().stacks, ammo, &CanonicalItemStack::stackId)->count == 3,
            "Stock ammunition stack was split while equipping");
        const auto publicView = view(service, 2).equipment->members.front();
        for (int slot : occupied)
            require(publicView.slots[slot] == id<ItemPrototypeId>(MWWorld::inventoryRecordId(bases[slot])), "Public equipment omitted a slot");
        auto duplicate = input(service, bases[12], 13, true);
        duplicate.stackId = std::ranges::find(equipped, EquipmentSlot::LeftRing, &EquipmentBinding::slot)->stackId;
        rejected(duplicate);
        // Transfers must preserve every unrelated equipped iterator through relocation.
        auto put = input(service, bases[8], 8, true);
        put.kind = InventoryTransactionKind::PutIntoContainer; put.slot.reset();
        const auto shared = view(service).containers.front(); put.containerId = shared.container; put.expectedContainerRevision = shared.revision;
        auto transfer = service.prepareInventory(authority, bind(authority, put).proposal());
        require(transfer && transfer->commit(durable) == CanonicalDurabilityResult::Committed
            && view(service).playerInventory.front().equipment == equipped, "Transfer lost unrelated equipment slots");
        ++tick;
        put.stackId = equipped.front().stackId; put.prototypeId = id<ItemPrototypeId>(MWWorld::inventoryRecordId(bases[0]));
        put.expectedInventoryRevision = view(service).playerInventory.front().revision;
        put.expectedContainerRevision = view(service).containers.front().revision;
        rejected(put);
        const std::vector saved(service.inventoryImage().begin(), service.inventoryImage().end());
        // Malformed full slot tables must reject before any owner installs.
        for (int corruption = 0; corruption < 3; ++corruption)
        {
            auto bad = saved;
            const std::array tag{std::byte{'S'}, std::byte{'L'}, std::byte{'O'}, std::byte{'T'}};
            auto found = std::search(bad.begin(), bad.end(), tag.begin(), tag.end());
            require(found != bad.end(), "Full slot image missing SLOT field");
            const auto offset = size_t(found - bad.begin()) + 8;
            if (corruption == 0) std::copy_n(bad.begin() + offset, 8, bad.begin() + offset + 8); // duplicate/mismatched slot
            if (corruption == 1) std::fill_n(bad.begin() + offset, 8, std::byte{0x7f}); // foreign identity
            if (corruption == 2) bad[offset - 4] = std::byte{1}; // wrong bounded field size
            InventoryService recovering(content.store, content.readers, binding, true);
            bool failed = false;
            try { recovering.recover(bad, references); } catch (const std::exception&) { failed = true; }
            require(failed && recovering.inventoryImage().empty(), "Malformed slots partially recovered");
            recovering.recover(saved, references);
            require(view(recovering).playerInventory.front().equipment == equipped, "Recovery retry lost equipment");
        }
        InventoryService restored(content.store, content.readers, binding, true);
        restored.recover(saved, references);
        require(std::ranges::equal(saved, restored.inventoryImage()) && view(restored).playerInventory.front().equipment == equipped,
            "Equipment restart was not exact");
        InventoryService uncertain(content.store, content.readers, binding, true);
        uncertain.recover(saved, references);
        auto pending = uncertain.prepareInventory(authority, bind(authority, input(uncertain, bases[0], 0, false)).proposal());
        require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Failed; }) == CanonicalDurabilityResult::Failed
            && uncertain.inventoryImage().empty()
            && !uncertain.project(authority, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick)),
            "Uncertain equipment persistence did not close the service");
        const auto calls = writes;
        require(pending->commit(durable) != CanonicalDurabilityResult::Committed && writes == calls,
            "Uncertain equipment permitted an in-place retry");
        Clock clock; Delivery delivery(clock, SessionGeneration::initial());
        publish(restored, authority, delivery, tick++);
        require(delivery.clients[0]->confirmedPlayerInventoryBaseline()->equipment == equipped, "Late client lost equipped slots");
        authority = players(SessionGeneration::fromValue(2).value());
        Delivery reconnect(clock, SessionGeneration::fromValue(2).value());
        publish(restored, authority, reconnect, tick++);
        require(reconnect.clients[0]->confirmedPlayerInventoryBaseline()->equipment == equipped, "Reconnect lost equipped slots");
        for (int slot : occupied)
        {
            auto prepared = restored.prepareInventory(authority, bind(authority, input(restored, bases[slot], slot, false)).proposal());
            require(prepared && prepared->commit(durable) == CanonicalDurabilityResult::Committed, "Restored unequip failed");
            ++tick;
        }
        require(view(restored).playerInventory.front().equipment.empty(), "Unequip left occupied slots");
        std::cout << "equipment: all 19 slots (clothing/armor/weapons), two rings, ammunition stacks, stock split/restack, explicit beast/condition checks, "
            "atomic durability/retry, allocation-free installation/publication, uncertain-write closure, stale/foreign/unsupported rejection, transfer preservation, malformed recovery, "
            "exact restart, private/public late join/reconnect; synthetic clients\n";
    }

    void checkStockedInventory(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Stocked inventory scratch already exists");
        Content content;
        ESM::Container chest; chest.blank(); chest.mId = ESM::RefId::stringRefId("stocked_chest");
        chest.mWeight = 1000;
        std::vector<ESM::RefId> references{content.actor, content.shirt};
        const auto add = [&]<class T>(const char* name) {
            T item; item.blank(); item.mId = ESM::RefId::stringRefId(name);
            if constexpr (std::is_same_v<T, ESM::Clothing>) item.mData.mType = ESM::Clothing::Ring;
            if constexpr (std::is_same_v<T, ESM::Weapon>)
            {
                ESM::Enchantment enchant; enchant.blank(); enchant.mId = ESM::RefId::stringRefId("mixed_enchantment");
                enchant.mData.mType = ESM::Enchantment::WhenUsed; enchant.mData.mCharge = 75;
                content.store.insertStatic(enchant); item.mEnchant = enchant.mId; item.mData.mHealth = 50;
            }
            content.store.insertStatic(item); references.push_back(item.mId);
            chest.mInventory.mList.push_back({2, item.mId});
        };
        add.operator()<ESM::Potion>("mixed_potion"); add.operator()<ESM::Apparatus>("mixed_apparatus");
        add.operator()<ESM::Armor>("mixed_armor"); add.operator()<ESM::Book>("mixed_book");
        add.operator()<ESM::Clothing>("mixed_ring"); add.operator()<ESM::Ingredient>("mixed_ingredient");
        add.operator()<ESM::Light>("mixed_light"); add.operator()<ESM::Lockpick>("mixed_lockpick");
        add.operator()<ESM::Miscellaneous>("mixed_misc"); add.operator()<ESM::Probe>("mixed_probe");
        add.operator()<ESM::Repair>("mixed_repair"); add.operator()<ESM::Weapon>("mixed_weapon");
        ESM::ItemLevList loot; loot.blank(); loot.mId = ESM::RefId::stringRefId("mixed_loot");
        loot.mList.push_back({references.back(), 1});
        content.store.insertStatic(loot);
        chest.mInventory.mList.push_back({1, loot.mId});
        content.store.overrideRecord(chest);
        auto binding = content.binding(); binding.mContainers[0].mBase = chest.mId;
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        const auto view = [&](InventoryService& value, uint64_t session = 1) {
            return value.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto initial = view(service).containers.front().stacks;
        require(initial.size() == 12, "OpenMW did not load every item type");
        // Damaged/charged/light/soul state uses the same typed engine field
        // serializers and detached storage as production recovery.
        ESM::Creature soul; soul.blank(); soul.mId = ESM::RefId::stringRefId("mixed_soul");
        content.store.insertStatic(soul);
        auto stateReferences = references; stateReferences.push_back(soul.mId);
        MWWorld::PlainEquipmentValues states;
        states.mActor = {99, -1}; states.mLastGenerated = {200, -1};
        for (size_t i = 2; i < references.size(); ++i)
        {
            MWWorld::ManualRef item(content.store, references[i]);
            item.getPtr().getCellRef().setRefNum({uint32_t(100 + i), -1});
            auto& object = states.mObjects.emplace_back(); object.blank();
            item.getPtr().getCellRef().writeState(object);
            item.getPtr().getRefData().write(object, Compiler::Locals{});
            object.mHasCustomState = false;
            const auto type = content.store.find(references[i]);
            if (type == ESM::Weapon::sRecordId)
            {
                object.mRef.mChargeInt = 9; object.mRef.mChargeIntRemainder = 0.25f;
                object.mRef.mEnchantmentCharge = 42.5f;
            }
            if (type == ESM::Light::sRecordId) object.mRef.mChargeFloat = 12.5f;
            if (type == ESM::Miscellaneous::sRecordId) object.mRef.mSoul = soul.mId;
        }
        const EquipmentEnvelope envelope{"mixed-state-roundtrip", binding.mContent, states.mActor};
        const EquipmentBindings stateBinding{envelope, content.store, stateReferences, {}};
        EquipmentBytes encoded, roundtrip;
        encodeEquipment(states, stateBinding, encoded);
        MWWorld::PlainEquipmentValues decoded;
        decodeEquipment(encoded, stateBinding, decoded);
        auto detached = MWWorld::RestoredPlainEquipment::restore(decoded, content.store, states.mActor, {}, true);
        detached.exportValues(decoded);
        encodeEquipment(decoded, stateBinding, roundtrip);
        require(encoded == roundtrip, "Typed condition/charge/light/soul recovery lost fields");
        const auto weaponId = id<ItemPrototypeId>(MWWorld::inventoryRecordId(references.back()));
        require(std::ranges::find(initial, weaponId, &CanonicalItemStack::prototypeId)->count == 3,
            "Levelled loot did not use stock loading/restacking");
        EquipmentFileSink file(scratch / "mixed.equipment", true);
        FileFaults faults;
        EquipmentFileCommitter sink(file, faults);
        std::unique_ptr<const InventoryTransferSuccess> success;
        EquipmentBytes bytes;
        bool injectFailure = true;
        const auto move = [&](InventoryService& value, bool put, CanonicalItemStack stack, uint64_t session) {
            const auto current = view(value, session);
            ClientInventoryTransactionCommand input{id<SessionId>(session), SessionGeneration::initial(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                put ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                binding.mContainers[0].mId, stack.prototypeId, stack.stackId, stack.count, {},
                current.playerInventory.front().revision, current.containers.front().revision, {}, Position3(0,0,0)};
            auto prepared = value.prepare(authority, bind(authority, input));
            if (injectFailure)
            {
                injectFailure = false;
                const auto prior = std::vector(value.inventoryImage().begin(), value.inventoryImage().end());
                faults = {FileFault::Flush};
                require(value.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Rejected
                    && std::ranges::equal(prior, value.inventoryImage()) && !success && bytes.empty(),
                    "Failed mixed-inventory durability installed or published loot");
                faults = {};
            }
            require(value.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
                "Mixed item transfer failed");
        };
        // A forged prototype must not alias a real stack, even at the right revision.
        auto wrong = wire(service, authority, 1, false, 1);
        bool rejected = false;
        try { service.prepare(authority, bind(authority, wrong)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Mismatched stack/prototype accepted");
        for (const auto& stack : initial) move(service, false, stack, 1);
        require(view(service).containers.front().stacks.empty(), "Chest did not empty");
        const auto taken = view(service).playerInventory.front().stacks;
        InventoryService restored(content.store, content.readers, binding, true);
        restored.recover(service.inventoryImage(), references);
        require(std::ranges::equal(service.inventoryImage(), restored.inventoryImage())
            && view(restored).containers.front().stacks.empty()
            && view(restored).playerInventory.front().stacks == taken,
            "Recovery refilled loot or changed instance/record identities");
        Clock clock; Delivery late(clock, SessionGeneration::initial());
        publish(restored, authority, late, 2);
        require(late.clients[1]->confirmedContainerInventoryBaselines().front().stacks.empty(),
            "Late join received refilled chest");
        for (const auto& stack : taken)
            if (stack.prototypeId != binding.mShirt) move(restored, true, stack, 1);
        require(view(restored).containers.front().stacks.size() == 12, "Mixed put lost item types");
        InventoryService again(content.store, content.readers, binding, true);
        again.recover(restored.inventoryImage(), references);
        require(std::ranges::equal(restored.inventoryImage(), again.inventoryImage()), "Mixed put recovery diverged");
        authority = players(*SessionGeneration::initial().next());
        Delivery resumed(clock, *SessionGeneration::initial().next()); publish(again, authority, resumed, 3);
        require(resumed.clients[0]->confirmedContainerInventoryBaselines().front().stacks
            == resumed.clients[1]->confirmedContainerInventoryBaselines().front().stacks,
            "Mixed inventory reconnect disagreed");
        const auto before = std::vector(again.inventoryImage().begin(), again.inventoryImage().end());
        for (unsigned test = 0; test < 7; ++test)
        {
            Content invalid;
            ESM::Container bad; bad.blank(); bad.mId = ESM::RefId::stringRefId("invalid_loot_container");
            if (test == 0) bad.mScript = ESM::RefId::stringRefId("unavailable_container_script");
            else if (test == 1)
            {
                ESM::Miscellaneous item; item.blank(); item.mId = ESM::RefId::stringRefId("scripted_loot");
                item.mScript = ESM::RefId::stringRefId("unavailable_item_script");
                invalid.store.insertStatic(item); bad.mInventory.mList.push_back({1, item.mId});
            }
            else if (test == 2) bad.mInventory.mList.push_back({1, invalid.actor});
            else if (test == 3) bad.mInventory.mList.push_back({1000001, invalid.shirt});
            else if (test == 4)
            {
                ESM::ItemLevList cycle; cycle.blank(); cycle.mId = ESM::RefId::stringRefId("cyclic_loot");
                cycle.mList.push_back({cycle.mId, 1}); invalid.store.insertStatic(cycle);
                bad.mInventory.mList.push_back({1, cycle.mId});
            }
            invalid.store.insertStatic(bad);
            auto badBinding = invalid.binding(); badBinding.mContainers[0].mBase = bad.mId;
            if (test >= 5)
            {
                ESM::CellRef placement; placement.blank(); placement.mRefNum = {1, 0}; placement.mRefID = bad.mId;
                if (test == 5) placement.mIsLocked = true;
                else placement.mTrap = ESM::RefId::stringRefId("unavailable_trap");
                badBinding.mContainers[0].mPlacement = placement;
            }
            bool refused = false;
            try { InventoryService refusedService(invalid.store, invalid.readers, badBinding); }
            catch (const std::invalid_argument&) { refused = true; }
            require(refused && std::ranges::equal(before, again.inventoryImage()),
                "Unsupported script/type/count/cyclic loot accepted or changed existing inventory");
        }
        for (bool organic : {false, true})
        {
            Content limited;
            ESM::Clothing heavy; heavy.blank(); heavy.mId = ESM::RefId::stringRefId("heavy_shirt");
            heavy.mData.mType = ESM::Clothing::Shirt; heavy.mData.mWeight = 2;
            limited.store.insertStatic(heavy);
            ESM::Container base; base.blank(); base.mId = ESM::RefId::stringRefId("limited_chest");
            base.mWeight = organic ? 1000.f : 1.f;
            if (organic) base.mFlags |= ESM::Container::Organic;
            limited.store.insertStatic(base);
            auto limitedBinding = limited.binding(); limitedBinding.mContainers[0].mBase = base.mId;
            for (auto& actor : limitedBinding.mActors) actor.mShirt = heavy.mId;
            InventoryService limitedService(limited.store, limited.readers, limitedBinding);
            const auto initialImage = std::vector(limitedService.inventoryImage().begin(), limitedService.inventoryImage().end());
            const auto current = players();
            bool refused = false;
            try { limitedService.prepare(current, bind(current, wire(limitedService, current, 1, true, 1))); }
            catch (const std::invalid_argument&) { refused = true; }
            require(refused && std::ranges::equal(initialImage, limitedService.inventoryImage()),
                "Organic or over-capacity put bypassed engine rules");
        }
        std::cout << "all 12 item types: stock fixed/levelled loading, enchanted item transfer, empty and full recovery, late join/resume\n";
    }

    void checkCellInventories(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Cell inventory scratch already exists");
        Content content;
        ESM::Miscellaneous loot; loot.blank(); loot.mId = ESM::RefId::stringRefId("cell_loot");
        content.store.insertStatic(loot);
        ESM::ItemLevList list; list.blank(); list.mId = ESM::RefId::stringRefId("cell_leveled_loot");
        list.mList.push_back({loot.mId, 1}); content.store.insertStatic(list);
        ESM::Container chest; chest.blank(); chest.mId = ESM::RefId::stringRefId("cell_chest");
        chest.mWeight = 1000; chest.mInventory.mList = {{1, loot.mId}, {1, list.mId}};
        content.store.overrideRecord(chest);
        auto binding = content.binding();
        binding.mContainers.clear();
        for (uint32_t i = 0; i < 3; ++i)
        {
            ESM::CellRef placed; placed.blank(); placed.mRefID = chest.mId; placed.mRefNum = {100 + i, 0};
            binding.mContainers.push_back({id<ContainerId>(90 + i), CellId::interior(id<CellSpaceId>(i == 2 ? 8 : 7)),
                Position3(i * 1024, 0, 0), chest.mId, placed});
        }
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        const auto view = [&](InventoryService& value, uint64_t session = 1) {
            return value.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto initial = view(service);
        require(initial.containers.size() == 2 && initial.containers[0].stacks[0].count == 2
            && initial.containers[1].stacks[0].count == 2
            && initial.containers[0].stacks[0].stackId != initial.containers[1].stacks[0].stackId,
            "Loaded inventories lost loot, interest filtering or unique instance identities");
        const auto intent = [&](InventoryService& value, uint64_t session, size_t container, bool put,
                                CanonicalItemStack stack, uint32_t count) {
            const auto current = view(value, session);
            return ClientInventoryTransactionCommand{id<SessionId>(session),
                authority.findActiveSession(id<SessionId>(session))->sessionGeneration(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                put ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                binding.mContainers[container].mId, stack.prototypeId, stack.stackId, count, {},
                current.playerInventory[0].revision, current.containers[0].revision, {}, Position3(0, 0, 0)};
        };
        auto take = intent(service, 1, 0, false, initial.containers[0].stacks[0], 2);
        const std::vector initialImage(service.inventoryImage().begin(), service.inventoryImage().end());
        for (int test = 0; test < 4; ++test)
        {
            auto bad = take;
            if (test == 0) bad.containerId = binding.mContainers[1].mId; // The item is in the other chest.
            if (test == 1) bad.containerId = binding.mContainers[2].mId; // Outside the player's cell.
            if (test == 2) bad.containerId = id<ContainerId>(999);
            if (test == 3) bad.interactionOrigin = Position3(500 * 1024, 0, 0);
            bool rejected = false;
            try { service.prepare(authority, bind(authority, bad)); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && std::ranges::equal(initialImage, service.inventoryImage()),
                "Wrong chest, cell, identity or reach mutated the domain");
        }
        EquipmentFileSink file(scratch / "cell.equipment", true);
        FileFaults faults; EquipmentFileCommitter sink(file, faults);
        std::unique_ptr<const InventoryTransferSuccess> success; EquipmentBytes bytes;
        auto prepared = service.prepare(authority, bind(authority, take));
        auto candidate = service.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1), &prepared);
        require(candidate && candidate->containers[0].stacks.empty()
            && candidate->containers[1].stacks == initial.containers[1].stacks,
            "Candidate changed an unrelated shared inventory");
        faults = {FileFault::Flush};
        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Rejected
            && !success && bytes.empty() && std::ranges::equal(initialImage, service.inventoryImage()),
            "Multi-container durability failure partially installed");
        faults = {};
        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
            "Multi-container durability retry failed");
        bool stale = false;
        try { service.prepare(authority, bind(authority, take)); }
        catch (const std::invalid_argument&) { stale = true; }
        require(stale, "Stale loaded-cell take replayed");
        const auto moved = *std::ranges::find(view(service).playerInventory[0].stacks,
            initial.containers[0].stacks[0].prototypeId, &CanonicalItemStack::prototypeId);
        auto put = service.prepare(authority, bind(authority, intent(service, 1, 1, true, moved, 2)));
        require(service.commit(authority, put, sink, success, bytes) == PersistenceResult::Accepted,
            "Put into the second shared chest failed");
        auto other = service.prepare(authority, bind(authority,
            intent(service, 2, 1, false, view(service).containers[1].stacks[0], 1)));
        require(service.commit(authority, other, sink, success, bytes) == PersistenceResult::Accepted,
            "Second player could not take from the second shared chest");
        Clock clock; Delivery late(clock, SessionGeneration::initial()); publish(service, authority, late, 2);
        require(late.clients[0]->confirmedContainerInventoryBaselines().size() == 2
            && late.clients[1]->confirmedContainerInventoryBaselines()[0].stacks.empty()
            && late.clients[1]->confirmedContainerInventoryBaselines()[1].stacks[0].count == 3,
            "Late join did not converge across both shared inventories");
        const std::array references{content.actor, content.shirt, loot.mId};
        const std::vector committed(service.inventoryImage().begin(), service.inventoryImage().end());
        for (int test = 0; test < 5; ++test)
        {
            auto damaged = committed;
            auto changed = binding;
            if (test == 0) damaged.pop_back();
            if (test == 1) std::fill_n(damaged.begin() + 16, 8, std::byte{0xff}); // Untrusted count.
            if (test == 2) std::fill_n(damaged.begin() + 24, 8, std::byte{0xff}); // Untrusted length.
            if (test == 3) ++changed.mContainers[1].mPlacement->mRefNum.mIndex;
            if (test == 4) changed.mContainers.pop_back();
            // Avoid colliding with the third placement when testing a wrong binding.
            if (test == 3) changed.mContainers[1].mPlacement->mRefNum.mIndex += 10;
            InventoryService fresh(content.store, content.readers, changed, true);
            bool rejected = false;
            try { fresh.recover(damaged, references); }
            catch (const std::exception&) { rejected = true; }
            require(rejected && fresh.inventoryImage().empty() && !fresh.project(authority,
                id<SessionId>(1), id<ServerTick>(3), id<CanonicalRevision>(3)),
                "Malformed or incomplete multi-container recovery partially published");
        }
        // Recovery must not execute base or leveled loading, even if it would now fail.
        const_cast<ESM::Container*>(content.store.get<ESM::Container>().find(chest.mId))->mInventory.mList
            = {{1, ESM::RefId::stringRefId("unavailable_recovery_loot")}};
        InventoryService restored(content.store, content.readers, binding, true);
        restored.recover(committed, references);
        require(std::ranges::equal(committed, restored.inventoryImage())
            && view(restored).containers[0].stacks.empty() && view(restored).containers[1].stacks[0].count == 3,
            "Recovery refilled or mixed loaded inventories");
        authority = players(*SessionGeneration::initial().next());
        Delivery resumed(clock, *SessionGeneration::initial().next()); publish(restored, authority, resumed, 3);
        require(resumed.clients[0]->confirmedContainerInventoryBaselines()[1].stacks
            == resumed.clients[1]->confirmedContainerInventoryBaselines()[1].stacks,
            "Resumed clients disagree about the second chest");
        auto continued = restored.prepare(authority, bind(authority,
            intent(restored, 2, 1, false, view(restored).containers[1].stacks[0], 1)));
        require(restored.commit(authority, continued, sink, success, bytes) == PersistenceResult::Accepted,
            "Recovered multi-container continuation failed");
        for (int test = 0; test < 3; ++test)
        {
            auto bad = binding;
            if (test == 0) bad.mContainers[1].mId = bad.mContainers[0].mId;
            if (test == 1) bad.mContainers[1].mPlacement = bad.mContainers[0].mPlacement;
            if (test == 2)
                while (bad.mContainers.size() <= MaxEquipmentContainers) bad.mContainers.push_back(binding.mContainers[0]);
            bool rejected = false;
            try { InventoryService invalid(content.store, content.readers, bad, true); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Duplicate or oversized loaded inventory domain accepted");
        }
        std::cout << "three placed inventories: fixed/leveled loot, cell interest, cross-chest identity rejection, "
            "atomic retry, two-player transfer, malformed recovery, no refill, late join/resume; synthetic transport\n";
    }

    void checkStartingEquipment(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Starting equipment scratch already exists");
        Content content;
        const auto replace = [&]<class T>(const T& record) {
            *const_cast<T*>(content.store.get<T>().find(record.mId)) = record;
        };
        const auto ref = [](const char* name) { return ESM::RefId::stringRefId(name); };
        auto npc = *content.store.get<ESM::NPC>().find(content.actor);
        npc.mNpdt.mSkills.fill(1);
        const auto setSkill = [&](ESM::RefId skill, int value) { npc.mNpdt.mSkills[ESM::Skill::refIdToIndex(skill)] = value; };
        setSkill(ESM::Skill::Marksman, 90); setSkill(ESM::Skill::ShortBlade, 80);
        setSkill(ESM::Skill::LongBlade, 70); setSkill(ESM::Skill::LightArmor, 90);
        setSkill(ESM::Skill::HeavyArmor, 60); setSkill(ESM::Skill::Unarmored, 0);
        std::vector<ESM::RefId> references{npc.mId};
        const auto add = [&](const auto& record, int count = 1) {
            content.store.insertStatic(record); references.push_back(record.mId);
            npc.mInventory.mList.push_back({count, record.mId});
        };
        const auto weapon = [&](const char* name, int type, int damage, int count = 1) {
            ESM::Weapon item; item.blank(); item.mId = ref(name); item.mData.mType = type;
            item.mData.mHealth = 100; item.mData.mChop[1] = damage; add(item, count);
        };
        weapon("start_bow", ESM::Weapon::MarksmanBow, 8, 2);
        weapon("start_dagger", ESM::Weapon::ShortBladeOneHand, 10);
        weapon("start_dagger_tie", ESM::Weapon::ShortBladeOneHand, 10, 2);
        weapon("start_sword", ESM::Weapon::LongBladeTwoHand, 100);
        weapon("start_arrow", ESM::Weapon::Arrow, 3, 12);
        weapon("start_arrow_best", ESM::Weapon::Arrow, 8, 9);
        weapon("start_arrow_tie", ESM::Weapon::Arrow, 8, 7);
        const auto clothing = [&](const char* name, int type, int value, int count = 1) {
            ESM::Clothing item; item.blank(); item.mId = ref(name); item.mData.mType = type;
            item.mData.mValue = value; add(item, count);
        };
        clothing("start_shirt", ESM::Clothing::Shirt, 2);
        clothing("start_shirt_best", ESM::Clothing::Shirt, 20, 2);
        clothing("start_ring", ESM::Clothing::Ring, 2);
        clothing("start_ring_best", ESM::Clothing::Ring, 10, -3);
        const auto armor = [&](const char* name, int type, float weight, int rating, int health = 100) {
            ESM::Armor item; item.blank(); item.mId = ref(name); item.mData.mType = type;
            item.mData.mWeight = weight; item.mData.mArmor = rating; item.mData.mHealth = health;
            if (type == ESM::Armor::Boots) item.mParts.mParts.push_back({ESM::PRT_LFoot, {}, {}});
            add(item);
        };
        armor("start_light", ESM::Armor::Cuirass, 5, 10);
        armor("start_heavy", ESM::Armor::Cuirass, 30, 20);
        armor("start_boots", ESM::Armor::Boots, 0, 10);
        armor("start_shield", ESM::Armor::Shield, 0, 10);
        armor("start_broken", ESM::Armor::Helmet, 0, 100, 0);
        ESM::Light light; light.blank(); light.mId = ref("start_light_carried"); add(light);
        replace(npc);
        auto beastRace = *content.store.get<ESM::Race>().find(npc.mRace);
        beastRace.mId = ref("start_beast"); beastRace.mData.mFlags |= ESM::Race::Beast;
        beastRace.mData.mAttributeValues.fill(40); content.store.insertStatic(beastRace);
        ESM::Class actorClass; actorClass.blank(); actorClass.mId = ref("start_class");
        for (auto& skills : actorClass.mData.mSkills) skills.fill(-1);
        actorClass.mData.mSkills[0][1] = ESM::Skill::refIdToIndex(ESM::Skill::LongBlade);
        content.store.insertStatic(actorClass);
        auto beast = npc; beast.mId = ref("start_auto_actor"); beast.mRace = beastRace.mId;
        beast.mClass = actorClass.mId; beast.mNpdtType = ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS;
        beast.mNpdt.mLevel = 4; content.store.insertStatic(beast); references.push_back(beast.mId);
        auto binding = content.binding(); binding.mShirt.reset();
        binding.mActors = {{{npc.mId, {}, 0, false, true}, {beast.mId, {}, 0, false, true}}};
        auto authority = players();
        const auto view = [&](InventoryService& service, uint64_t session = 1) {
            return service.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto equipped = [&](InventoryService& service, int slot, uint64_t session = 1) {
            const auto inventory = view(service, session).playerInventory.front();
            const auto found = std::ranges::find(inventory.equipment, static_cast<EquipmentSlot>(slot), &EquipmentBinding::slot);
            if (found == inventory.equipment.end()) return std::optional<CanonicalItemStack>{};
            return std::optional{*std::ranges::find(inventory.stacks, found->stackId, &CanonicalItemStack::stackId)};
        };
        const auto expect = [&](InventoryService& service, int slot, const char* base, uint64_t session = 1) {
            const auto item = equipped(service, slot, session);
            require(base ? item && item->prototypeId == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ref(base))) : !item,
                "Stock starting equipment selection disagrees with expected slot");
        };
        InventoryService service(content.store, content.readers, binding);
        expect(service, 16, "start_bow"); expect(service, 18, "start_arrow_tie");
        expect(service, 8, "start_shirt_best"); expect(service, 12, "start_ring_best"); expect(service, 13, "start_ring_best");
        expect(service, 1, "start_heavy"); expect(service, 7, "start_boots"); expect(service, 17, "start_shield");
        expect(service, 0, nullptr); expect(service, 16, "start_sword", 2); expect(service, 18, nullptr, 2);
        expect(service, 7, nullptr, 2); expect(service, 17, "start_shield", 2);
        require(equipped(service, 18)->count == 7 && equipped(service, 16)->count == 1
            && equipped(service, 12)->count == 1 && equipped(service, 13)->count == 1
            && equipped(service, 12)->stackId != equipped(service, 13)->stackId,
            "Starting ammo, signed stacks or separate rings were split incorrectly");
        for (uint64_t session : {1, 2})
        {
            std::map<ItemPrototypeId, uint32_t> expected, actual;
            for (const auto& item : npc.mInventory.mList)
                expected[id<ItemPrototypeId>(MWWorld::inventoryRecordId(item.mItem))] += std::abs(item.mCount);
            const auto current = view(service, session);
            for (const auto& item : current.playerInventory.front().stacks) actual[item.prototypeId] += item.count;
            require(expected == actual, "Auto-equipment duplicated or lost carried items");
        }
        InventoryService deterministic(content.store, content.readers, binding);
        require(std::ranges::equal(service.inventoryImage(), deterministic.inventoryImage()), "Starting equipment identities are not deterministic");
        const auto initial = view(service).playerInventory.front().equipment;
        Clock clock; Delivery join(clock, SessionGeneration::initial()); publish(service, authority, join, 1);
        require(join.clients[0]->confirmedPlayerInventoryBaseline()->equipment == initial, "Initial client baseline omitted auto-equipment");
        const auto publicGear = view(service, 2).equipment->members.front();
        require(publicGear.slots[16] == equipped(service, 16)->prototypeId && publicGear.slots[12] == equipped(service, 12)->prototypeId,
            "Public starting equipment differs from private inventory");
        EquipmentFileSink file(scratch / "starting.equipment", true); FileFaults faults;
        require(file.writeSessionImage({reinterpret_cast<const char*>(service.inventoryImage().data()), service.inventoryImage().size()}, faults)
            == PersistenceResult::Accepted, "Initial equipment image was not durable");
        std::ifstream disk(scratch / "starting.equipment", std::ios::binary);
        const std::vector<char> diskImage((std::istreambuf_iterator<char>(disk)), {});
        InventoryService initialRestart(content.store, content.readers, binding, true);
        initialRestart.recover(std::as_bytes(std::span(diskImage)), references);
        require(view(initialRestart).playerInventory.front().equipment == initial
            && std::ranges::equal(service.inventoryImage(), initialRestart.inventoryImage()),
            "Durable starting equipment did not recover exactly");
        // Saved empty slots are intentional. Recovery must not rerun selection,
        // even if the source loot can no longer initialize a character.
        unequipStartingItems(service, authority);
        const std::vector saved(service.inventoryImage().begin(), service.inventoryImage().end());
        npc.mInventory.mList = {{1, ref("missing_start")}}; replace(npc);
        InventoryService restored(content.store, content.readers, binding, true); restored.recover(saved, references);
        require(std::ranges::equal(saved, restored.inventoryImage()) && view(restored).playerInventory.front().equipment.empty(),
            "Recovery rerolled loot or re-equipped deliberately empty slots");
        authority = players(*SessionGeneration::initial().next());
        Delivery reconnect(clock, *SessionGeneration::initial().next()); publish(restored, authority, reconnect, 2);
        require(reconnect.clients[0]->confirmedPlayerInventoryBaseline()->equipment.empty(), "Reconnect re-equipped saved empty slots");
        // Missing ammunition falls back to the next skill; ties use stock order.
        npc = beast; npc.mId = content.actor; npc.mNpdtType = ESM::NPC::NPC_DEFAULT;
        npc.mRace = ESM::RefId::stringRefId("service_race");
        std::erase_if(npc.mInventory.mList, [&](const auto& item) {
            return item.mItem == ref("start_arrow") || item.mItem == ref("start_arrow_best") || item.mItem == ref("start_arrow_tie");
        });
        replace(npc);
        InventoryService noAmmo(content.store, content.readers, binding); expect(noAmmo, 16, "start_dagger_tie"); expect(noAmmo, 18, nullptr);
        auto broken = *content.store.get<ESM::Weapon>().find(ref("start_dagger_tie")); broken.mData.mHealth = 0;
        replace(broken);
        InventoryService brokenWinner(content.store, content.readers, binding); expect(brokenWinner, 16, "start_sword");
        setSkill(ESM::Skill::Unarmored, 100); replace(npc);
        InventoryService unarmored(content.store, content.readers, binding); expect(unarmored, 1, nullptr); expect(unarmored, 17, nullptr);
        auto enchanted = *content.store.get<ESM::Clothing>().find(ref("start_shirt_best")); enchanted.mEnchant = ref("missing_effect_service");
        replace(enchanted);
        bool rejected = false;
        try { InventoryService unsupported(content.store, content.readers, binding); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Winning enchanted starting equipment was silently equipped or skipped");
        enchanted.mEnchant = {}; replace(enchanted);
        npc.mInventory.mList.clear();
        for (int i = 0; i < 64; ++i)
        {
            ESM::Clothing item; item.blank(); item.mId = ESM::RefId::stringRefId("bounded_start_" + std::to_string(i));
            item.mData.mType = ESM::Clothing::Shirt; item.mData.mValue = i; content.store.insertStatic(item);
            npc.mInventory.mList.push_back({2, item.mId});
        }
        replace(npc); rejected = false;
        try { InventoryService oversized(content.store, content.readers, binding); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Auto-equipment exceeded the inventory node bound while splitting");
        std::cout << "starting equipment: explicit/auto NPDT, weapon skill/damage/ties/ammo/fallback, armor/unarmored, "
            "beast restrictions, signed ring splits, item conservation, deterministic IDs, private/public baselines, "
            "durable image, recovery without selection, reconnect, unavailable effects and split bound; synthetic transport\n";
    }

    void checkPlayerInventories(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Player inventory scratch already exists");
        Content content;
        ESM::NPC character; character.blank(); character.mId = ESM::RefId::stringRefId("loaded_character");
        character.mRace = content.store.get<ESM::NPC>().find(content.actor)->mRace;
        std::vector<ESM::RefId> references{content.actor, character.mId};
        const auto add = [&]<class T>(const char* name) {
            T item; item.blank(); item.mId = ESM::RefId::stringRefId(name);
            if constexpr (std::is_same_v<T, ESM::Clothing>) item.mData.mType = ESM::Clothing::Pants;
            if constexpr (std::is_same_v<T, ESM::Weapon>) item.mData.mHealth = 100;
            content.store.insertStatic(item); references.push_back(item.mId);
            character.mInventory.mList.push_back({2, item.mId});
        };
        add.operator()<ESM::Potion>("starting_potion"); add.operator()<ESM::Apparatus>("starting_apparatus");
        add.operator()<ESM::Armor>("starting_armor"); add.operator()<ESM::Book>("starting_book");
        add.operator()<ESM::Clothing>("starting_pants"); add.operator()<ESM::Ingredient>("starting_ingredient");
        add.operator()<ESM::Light>("starting_light"); add.operator()<ESM::Lockpick>("starting_lockpick");
        add.operator()<ESM::Miscellaneous>("starting_misc"); add.operator()<ESM::Probe>("starting_probe");
        add.operator()<ESM::Repair>("starting_repair"); add.operator()<ESM::Weapon>("starting_weapon");
        ESM::ItemLevList list; list.blank(); list.mId = ESM::RefId::stringRefId("starting_leveled");
        list.mList = {{references.back(), 1}, {references[2], 1}};
        list.mFlags = ESM::ItemLevList::Each;
        content.store.insertStatic(list);
        character.mInventory.mList.push_back({3, list.mId});
        content.store.insertStatic(character);
        auto binding = content.binding(); binding.mShirt.reset(); binding.mLootSeed = 97;
        binding.mActors = {{{character.mId, {}, 0, false, true}, {content.actor, {}, 0, false, true}}};
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        unequipStartingItems(service, authority);
        const auto view = [&](InventoryService& value, uint64_t session = 1) {
            return value.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
        };
        const auto initial = view(service).playerInventory[0].stacks;
        uint32_t total = 0;
        for (const auto& stack : initial)
        {
            total += stack.count;
            require(stack.prototypeId != id<ItemPrototypeId>(MWWorld::inventoryRecordId(content.shirt)),
                "Loaded character received a seeded shirt");
        }
        require(initial.size() == 12 && total == 27 && view(service, 2).playerInventory[0].stacks.empty(),
            "Fixed/leveled character inventory or empty starting character changed");
        InventoryService deterministic(content.store, content.readers, binding);
        unequipStartingItems(deterministic, authority);
        require(std::ranges::equal(service.inventoryImage(), deterministic.inventoryImage()),
            "Same starting loadout and seed produced different identities or loot");
        auto twins = binding; twins.mActors[1] = twins.mActors[0];
        InventoryService distinct(content.store, content.readers, twins);
        const auto firstTwin = view(distinct), secondTwin = view(distinct, 2);
        for (const auto& a : firstTwin.playerInventory[0].stacks)
            for (const auto& b : secondTwin.playerInventory[0].stacks)
                require(a.stackId != b.stackId, "Two characters share a starting item instance");
        EquipmentFileSink file(scratch / "characters.equipment", true);
        FileFaults faults; EquipmentFileCommitter sink(file, faults);
        EquipmentBytes bytes; std::unique_ptr<const InventoryTransferSuccess> success;
        const auto intent = [&](InventoryService& value, uint64_t session, bool put, CanonicalItemStack stack) {
            const auto current = view(value, session);
            return ClientInventoryTransactionCommand{id<SessionId>(session),
                authority.findActiveSession(id<SessionId>(session))->sessionGeneration(),
                CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                put ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                current.containers[0].container, stack.prototypeId, stack.stackId, stack.count, {},
                current.playerInventory[0].revision, current.containers[0].revision, {}, Position3(0, 0, 0)};
        };
        const auto first = intent(service, 1, true, initial.front());
        const std::vector before(service.inventoryImage().begin(), service.inventoryImage().end());
        auto prepared = service.prepare(authority, bind(authority, first));
        faults = {FileFault::Flush};
        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Rejected
            && !success && bytes.empty() && std::ranges::equal(before, service.inventoryImage()),
            "Starting inventory transfer escaped failed durability");
        faults = {};
        require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
            "Starting inventory transfer retry failed");
        bool stale = false;
        try { service.prepare(authority, bind(authority, first)); }
        catch (const std::invalid_argument&) { stale = true; }
        require(stale, "Starting inventory replay duplicated a transfer");
        for (size_t i = 1; i < initial.size(); ++i)
        {
            auto put = service.prepare(authority, bind(authority, intent(service, 1, true, initial[i])));
            require(service.commit(authority, put, sink, success, bytes) == PersistenceResult::Accepted,
                "Starting item type could not be put into a container");
        }
        require(view(service).playerInventory[0].stacks.empty(), "Starting character did not empty");
        const auto shared = view(service).containers[0].stacks;
        for (const auto& stack : shared)
        {
            auto take = service.prepare(authority, bind(authority, intent(service, 2, false, stack)));
            require(service.commit(authority, take, sink, success, bytes) == PersistenceResult::Accepted,
                "Empty character could not take a starting item");
        }
        const auto received = view(service, 2).playerInventory[0].stacks;
        require(received.size() == 12 && view(service).containers[0].stacks.empty(),
            "Starting loadout transfer lost item types or left duplicates");
        const std::vector committed(service.inventoryImage().begin(), service.inventoryImage().end());
        InventoryService truncated(content.store, content.readers, binding, true);
        bool rejected = false;
        try { truncated.recover(std::span(committed).first(committed.size() - 1), references); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && truncated.inventoryImage().empty() && !truncated.project(authority,
            id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)),
            "Partial character recovery published inventory");
        // A direct-runtime fixture deliberately invalidates the loot source.
        // Production fingerprints reject changed content; here it proves recovery
        // never reads or rerolls a starting inventory, including an emptied one.
        const_cast<ESM::NPC*>(content.store.get<ESM::NPC>().find(character.mId))->mInventory.mList
            = {{1, ESM::RefId::stringRefId("unavailable_starting_item")}};
        InventoryService restored(content.store, content.readers, binding, true);
        restored.recover(committed, references);
        require(std::ranges::equal(committed, restored.inventoryImage())
            && view(restored).playerInventory[0].stacks.empty() && view(restored, 2).playerInventory[0].stacks == received,
            "Character recovery refilled, rerolled or changed identities");
        Clock clock; Delivery late(clock, SessionGeneration::initial()); publish(restored, authority, late, 2);
        authority = players(*SessionGeneration::initial().next());
        Delivery resumed(clock, *SessionGeneration::initial().next()); publish(restored, authority, resumed, 3);
        require(resumed.clients[1]->confirmedPlayerInventoryBaseline()->stacks == received,
            "Reconnect lost the receiving character inventory");
        auto continued = restored.prepare(authority, bind(authority, intent(restored, 2, true, received.front())));
        require(restored.commit(authority, continued, sink, success, bytes) == PersistenceResult::Accepted,
            "Character inventory could not continue after recovery");
        for (int test = 0; test < 6; ++test)
        {
            Content invalid; auto bad = invalid.binding(); bad.mShirt.reset();
            bad.mActors = {{{invalid.actor, {}, 0, false, true}, {invalid.actor, {}, 0, false, true}}};
            auto* npc = const_cast<ESM::NPC*>(invalid.store.get<ESM::NPC>().find(invalid.actor));
            if (test == 0) npc->mScript = ESM::RefId::stringRefId("actor_script");
            if (test == 1)
            {
                auto* item = const_cast<ESM::Clothing*>(invalid.store.get<ESM::Clothing>().find(invalid.shirt));
                item->mScript = ESM::RefId::stringRefId("item_script");
                npc->mInventory.mList = {{1, invalid.shirt}};
            }
            if (test == 2) npc->mInventory.mList = {{1, ESM::RefId::stringRefId("missing_item")}};
            if (test == 3) npc->mInventory.mList.resize(257, {1, invalid.shirt});
            if (test == 4) npc->mInventory.mList = {{std::numeric_limits<int>::min(), invalid.shirt}};
            if (test == 5) bad.mShirt = id<ItemPrototypeId>(70);
            bool failed = false;
            try { InventoryService unsupported(invalid.store, invalid.readers, bad); }
            catch (const std::exception&) { failed = true; }
            require(failed, "Unsupported or oversized character initialization was silently accepted");
        }
        std::cout << "character inventory: all 12 types, fixed/leveled loot, empty character, unique identities, "
            "determinism, atomic failure/retry, cross-player transfer, no refill on recovery, late join/reconnect, guards; synthetic transport\n";
    }

    void checkInventoryHost(const std::filesystem::path& scratch, const std::filesystem::path& config,
        bool wholeInterior, bool baseInventory, bool worldActors, bool worldItems, bool stockPlacement, bool door, bool twoCells, bool teleports, bool environment, bool exterior)
    {
        require(std::filesystem::create_directory(scratch), "Native host scratch already exists");
        auto manifest = testContentManifest();
        if (twoCells)
        {
            const std::array spaces{CellSpaceDeclaration{id<CellSpaceId>(7), CellSpaceKind::Interior},
                CellSpaceDeclaration{id<CellSpaceId>(8), exterior ? CellSpaceKind::Exterior : CellSpaceKind::Interior}};
            const std::array cells{CellId::interior(id<CellSpaceId>(7)),
                exterior ? CellId::exterior(id<CellSpaceId>(8), -1, 50) : CellId::interior(id<CellSpaceId>(8))};
            manifest = ContentManifest::create(testContentManifestId(), spaces, cells, id<AppearanceId>(1), testMovementProfile()).value();
        }
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
        // Generated override on actual game records. This is a synthetic mod,
        // not evidence for an arbitrary published mod or its script behavior.
        std::array<ESM::NPC, 2> startingCharacters;
        ESM::Creature startingCreature;
        if (stockPlacement) writePlacementFixtureModels(scratch);
        const auto writeStartingPlugin = [&](int gold) {
            std::ofstream stream(scratch / "StartingInventories.esp", std::ios::binary);
            ESM::ESMWriter out;
            out.setVersion(); out.setFormatVersion(ESM::DefaultFormatVersion); out.setType(0);
            out.addMaster("Morrowind.esm", 0); out.save(stream);
            for (size_t i = 0; i < startingCharacters.size(); ++i)
            {
                auto npc = startingCharacters[i];
                if (i == 0) npc.mInventory.mList.back().mCount = gold;
                out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
            }
            if (worldActors)
            {
                auto npc = startingCharacters[0]; npc.mScript = {};
                npc.mNpdtType = ESM::NPC::NPC_DEFAULT; npc.mNpdt.mHealth = 0;
                npc.mId = ESM::RefId::stringRefId("vnext_dead_actor");
                out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
                npc.mId = ESM::RefId::stringRefId("vnext_living_actor"); npc.mNpdt.mHealth = 40;
                out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
                out.startRecord(ESM::Creature::sRecordId, 0); startingCreature.save(out); out.endRecord(ESM::Creature::sRecordId);
                if (stockPlacement)
                {
                    for (auto name : {"placement-floor", "placement-table"})
                    {
                        ESM::Static mesh; mesh.blank(); mesh.mId = ESM::RefId::stringRefId(name);
                        mesh.mModel = std::string(name) + ".osgt";
                        out.startRecord(ESM::Static::sRecordId,0); mesh.save(out); out.endRecord(ESM::Static::sRecordId);
                    }
                    ESM::Weapon dagger; dagger.blank(); dagger.mId = ESM::RefId::stringRefId("iron dagger");
                    dagger.mModel = "placement-item.osgt"; dagger.mName = "Placement test dagger";
                    dagger.mData.mType = ESM::Weapon::ShortBladeOneHand;
                    dagger.mData.mHealth = 100;
                    out.startRecord(ESM::Weapon::sRecordId,0); dagger.save(out); out.endRecord(ESM::Weapon::sRecordId);
                    ESM::Miscellaneous pile; pile.blank(); pile.mId = ESM::RefId::stringRefId("gold_025");
                    pile.mModel = "placement-gold.osgt"; pile.mData.mValue = 25;
                    out.startRecord(ESM::Miscellaneous::sRecordId,0); pile.save(out); out.endRecord(ESM::Miscellaneous::sRecordId);
                }
                ESM::Cell cell; cell.blank(); cell.mName = "vNext actor inventory test";
                cell.mData.mFlags = ESM::Cell::Interior; cell.updateId();
                out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
                uint32_t index = 0;
                for (auto base : {ESM::RefId::stringRefId("vnext_dead_actor"), startingCreature.mId, npc.mId})
                {
                    ESM::CellRef placement; placement.blank(); placement.mRefNum = {++index, 0}; placement.mRefID = base;
                    placement.save(out);
                }
                if (worldItems)
                    for (auto base : {ESM::RefId::stringRefId("iron dagger"), ESM::RefId::stringRefId("gold_100")})
                    {
                        ESM::CellRef placement; placement.blank(); placement.mRefNum = {++index, 0}; placement.mRefID = base;
                        placement.mPos.pos[0] = 16;
                        if (stockPlacement && base == "iron dagger") placement.mCount = 5;
                        placement.save(out);
                    }
                if (stockPlacement)
                {
                    for (int surface = 0; surface < 4; ++surface)
                    {
                        ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index,0};
                        placed.mRefID = ESM::RefId::stringRefId(surface == 0 ? "placement-floor" : "placement-table");
                        placed.mPos.pos[0] = surface == 0 ? 0.f : surface == 1 ? 80.f : surface == 2 ? 200.f : -200.f;
                        placed.mPos.pos[2] = surface == 0 ? -20.f : 40.f;
                        if (surface >= 2) placed.mPos.rot[1] = osg::DegreesToRadians(surface == 2 ? 31.f : 29.f);
                        placed.save(out);
                    }
                }
                if (door)
                    for (int i = 0; i < 2; ++i)
                    {
                        ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0};
                        placed.mRefID = ESM::RefId::stringRefId("in_c_door_arched");
                        placed.mPos.pos[0] = 1000.f + 1000.f * i;
                        placed.save(out);
                    }
                if (teleports)
                {
                    // One supported outward door, plus locked/trapped/exterior/
                    // unrelated destinations which must remain unsupported.
                    for (int variant = 0; variant < 5; ++variant)
                    {
                        ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0};
                        placed.mRefID = ESM::RefId::stringRefId("in_c_door_arched");
                        placed.mTeleport = true; placed.mDestCell = "VNEXT SECOND INTERIOR";
                        placed.mDoorDest.pos[0] = 32;
                        placed.mDoorDest.rot[2] = osg::DegreesToRadians(90.f);
                        if (variant == 1) { placed.mIsLocked = true; placed.mLockLevel = 0; }
                        if (variant == 2) placed.mTrap = ESM::RefId::stringRefId("test_trap");
                        if (exterior)
                        {
                            placed.mDestCell.clear();
                            placed.mDoorDest.pos[0] -= 8192;
                            placed.mDoorDest.pos[1] = 409600 + 64;
                        }
                        if (variant == 3)
                        {
                            placed.mDestCell.clear();
                            if (exterior) placed.mDoorDest.pos[0] = 8192; // unbound exterior
                        }
                        if (variant == 4) placed.mDestCell = "unbound interior";
                        placed.save(out);
                    }
                }
                out.endRecord(ESM::Cell::sRecordId);
                if (twoCells)
                {
                    cell.mName = "vNext second interior";
                    if (exterior)
                    {
                        cell.mName = "vNext exterior";
                        cell.mData.mFlags = 0; cell.mData.mX = -1; cell.mData.mY = 50;
                        ESM::Land land; land.blank(); land.mX = -1; land.mY = 50;
                        land.mFlags = ESM::Land::Flag_HeightsNormals;
                        land.mLandData->mHeights.fill(-64.f);
                        land.mLandData->mMinHeight = land.mLandData->mMaxHeight = -64.f;
                        out.startRecord(ESM::Land::sRecordId, 0); land.save(out); out.endRecord(ESM::Land::sRecordId);
                    }
                    cell.updateId();
                    out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
                    for (auto base : {ESM::RefId::stringRefId("vnext_dead_actor"), ESM::RefId::stringRefId("iron dagger"),
                            ESM::RefId::stringRefId("placement-floor")})
                    {
                        ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0}; placed.mRefID = base;
                        if (exterior)
                        {
                            if (base == "placement-floor") continue; // exercise LAND, not a fixture mesh floor
                            placed.mPos.pos[0] = -8192 + 32; placed.mPos.pos[1] = 409600 + 64;
                        }
                        if (base == "placement-floor") placed.mPos.pos[2] = -60.f;
                        placed.save(out);
                    }
                    if (teleports)
                    {
                        ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0};
                        placed.mRefID = ESM::RefId::stringRefId("in_c_door_arched");
                        placed.mTeleport = true; placed.mDestCell = "vNext actor inventory test";
                        if (exterior) { placed.mPos.pos[0] = -8192 + 32; placed.mPos.pos[1] = 409600 + 64; }
                        placed.save(out);
                    }
                    out.endRecord(ESM::Cell::sRecordId);
                }
            }
            out.close();
        };
        if (baseInventory)
        {
            const auto directory = (scratch / "openmw").string();
            const char* arguments[]{"native-test", "--config", directory.c_str()};
            Loadout base(readLoadoutOptions(3, arguments));
            startingCharacters.fill(*base.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player")));
            startingCharacters[0].mInventory.mList = {{1, ESM::RefId::stringRefId("common_shirt_01")},
                {1, ESM::RefId::stringRefId("common_pants_01")}, {1, ESM::RefId::stringRefId("iron dagger")},
                {25, ESM::RefId::stringRefId("gold_001")}};
            startingCharacters[1].mId = ESM::RefId::stringRefId("vnext_second_character");
            startingCharacters[1].mInventory.mList = {{7, ESM::RefId::stringRefId("gold_001")}};
            if (worldActors)
            {
                startingCreature = *base.store().get<ESM::Creature>().find(ESM::RefId::stringRefId("rat"));
                startingCreature.mId = ESM::RefId::stringRefId("vnext_dead_creature"); startingCreature.mScript = {};
                startingCreature.mData.mHealth = 0;
                startingCreature.mInventory.mList = {{3, ESM::RefId::stringRefId("gold_001")}};
            }
            writeStartingPlugin(25);
            std::ofstream out(scratch / "openmw" / "openmw.cfg", std::ios::app);
            out << "\ndata=" << std::quoted(scratch.generic_string()) << "\ncontent=StartingInventories.esp\n";
        }
        const auto descriptor = scratch / "native.txt";
        {
            std::ofstream out(descriptor);
            out << (exterior ? "native-inventory-13" : environment ? "native-inventory-12" : teleports ? "native-inventory-11" : twoCells ? "native-inventory-10" : door ? "native-inventory-9" : stockPlacement ? "native-inventory-8" : worldItems ? "native-inventory-7" : worldActors ? "native-inventory-6" : baseInventory ? "native-inventory-5" : wholeInterior ? "native-inventory-4" : "native-inventory-3") << "\nmanifest ";
            const auto manifestId = testContentManifestId();
            for (auto byte : manifestId.bytes())
                out << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(byte);
            out << "\nconfig \"openmw\"\nplayers 1 2\n";
            out << (baseInventory ? "actors \"player\" \"vnext_second_character\"\n"
                : "actors \"player\" 3 \"player\" 5\nshirt \"common_shirt_01\"\n");
            out << "loot 1 0\n";
            out << (worldActors ? "interior \"vNext actor inventory test\"\n" : wholeInterior ? "interior \"Seyda Neen, Fargoth's House\"\n"
                : "container \"Imperial Prison Ship\" \"Morrowind.esm\" 421490\n");
            if (door) out << "door \"StartingInventories.esp\" 10\n";
            out << "cell interior:7\n";
            if (twoCells) out << (exterior ? "exterior -1 50\ncell exterior:8:-1:50\n"
                : "interior \"vNext second interior\"\ncell interior:8\n");
        }
        if (twoCells)
        {
            auto authority = players(SessionGeneration::initial(), 1, 2);
            InventoryHost host(descriptor, manifest, *registry, *crypto, {});
            auto& service = dynamic_cast<InventoryService&>(host.service());
            if (environment && !exterior)
            {
                InventoryHost recovered(descriptor, manifest, *registry, *crypto, service.inventoryImage());
                require(host.environment() && recovered.environment(), "V12 host did not bind native environment");
                checkHostedEnvironment(*host.environment(), *recovered.environment());
                require(std::ranges::equal(service.inventoryImage(), recovered.service().inventoryImage()),
                    "V12 environment recovery regenerated inventory");
                std::cout << "V12 host: OpenMW content, environment initialization, native inventory and environment recovery passed\n";
                return;
            }
            service.synchronizeCells(authority);
            unequipStartingItems(service, authority);
            if (teleports)
            {
                teleportRoundTrip(service, authority, scratch, exterior);
                InventoryHost recovered(descriptor, manifest, *registry, *crypto, service.inventoryImage());
                require(std::ranges::equal(recovered.service().inventoryImage(), service.inventoryImage()),
                    "OpenMW teleport host recovery regenerated loot");
                if (exterior)
                {
                    require(host.environment() && recovered.environment(), "V13 lost native environment");
                    checkHostedEnvironment(*host.environment(), *recovered.environment());
                    std::ifstream source(descriptor);
                    const std::string text{std::istreambuf_iterator<char>(source), {}};
                    for (const auto& [from, to] : {
                        std::pair{std::string("native-inventory-13"), std::string("native-inventory-12")},
                        std::pair{std::string("exterior -1 50"), std::string("exterior -2 50")},
                        std::pair{std::string("exterior -1 50"), std::string("exterior 2147483648 50")},
                        std::pair{std::string("exterior:8:-1:50"), std::string("interior:8")}})
                    {
                        auto invalid = text;
                        invalid.replace(invalid.find(from), from.size(), to);
                        const auto path = scratch / "invalid.txt";
                        { std::ofstream out(path); out << invalid; }
                        bool rejected = false;
                        try { InventoryHost bad(path, manifest, *registry, *crypto, service.inventoryImage()); }
                        catch (const std::invalid_argument&) { rejected = true; }
                        require(rejected, "Exterior descriptor version/bounds/mapping validation failed");
                    }
                }
                std::cout << "Teleport host: Morrowind/generated winning placements, terrain and destination resolution; no graphical clients\n";
                return;
            }
            uint64_t tick = 1;
            const auto first = CellId::interior(id<CellSpaceId>(7)), second = CellId::interior(id<CellSpaceId>(8));
            const auto move = [&](size_t index, CellId cell, Position3 position = Position3(0,0,0)) {
                auto entities = std::vector(authority.players().begin(), authority.players().end());
                const auto& player = entities[index];
                entities[index] = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(player, id<ServerTick>(++tick),
                    Transform(cell, position, player.transform().orientation()), LinearVelocity3(0,0,0)));
                authority = std::get<CanonicalServerState>(createCanonicalServerState(entities, authority.activeSessions()));
                service.synchronizeCells(authority);
            };
            const auto view = [&](InventoryService& owner, uint64_t session) {
                return owner.projectInventory(authority, id<SessionId>(session), id<ServerTick>(++tick), id<CanonicalRevision>(tick)).value();
            };
            const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
            const auto mutate = [&](ClientInventoryTransactionCommand input) {
                auto command = service.prepareInventory(authority, bind(authority, input).proposal());
                require(command && command->commit(accepted) == CanonicalDurabilityResult::Committed, "Two-cell host inventory operation failed");
            };
            const auto firstView = view(service, 2);
            move(0, second);
            const auto secondView = view(service, 1);
            require(service.activeCells() == std::array{true, true} && secondView.containers.size() == 1
                && secondView.groundItems[0].items.size() == 1 && !secondView.groundItems[0].door
                && firstView.groundItems[0].items.size() == 2
                && firstView.containers[0].container != secondView.containers[0].container,
                "OpenMW two-interior discovery or stable IDs incorrect");
            mutate(worldWire(service, authority, 1, true));
            auto alice = view(service, 1);
            const auto dagger = id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("iron dagger")));
            const auto index = size_t(std::ranges::find(alice.playerInventory[0].stacks, dagger, &CanonicalItemStack::prototypeId)
                - alice.playerInventory[0].stacks.begin());
            auto drop = worldWire(service, authority, 1, false, index);
            drop.placement = placementTestView(0,0,true);
            mutate(drop);
            const auto savedDrop = view(service, 1).groundItems[0].items.front();
            require(savedDrop.position.z() == -62 * 1024, "Second interior used the first interior's placement scene");
            for (int i = 0; i < 2; ++i) mutate(worldWire(service, authority, 2, true));
            auto bob = view(service, 2);
            const auto& shared = bob.containers[0];
            mutate({id<SessionId>(2), SessionGeneration::initial(), CommandSequence::initial(), id<CommandId>(1), CanonicalRevision::initial(),
                InventoryTransactionKind::TakeAllFromContainer, shared.container, shared.stacks[0].prototypeId, shared.stacks[0].stackId,
                1, {}, bob.playerInventory[0].revision, shared.revision, {}, Position3(0,0,0)});
            move(1, first, Position3(1000 * 1024, 0, 0));
            const auto& entity = authority.players()[1];
            ServerCommandProposal activation(id<SessionId>(2), SessionGeneration::initial(), CommandSequence::initial(), id<CommandId>(1),
                CanonicalRevision::initial(), EntityPrecondition(entity.entityId(), entity.entityRevision(), entity.authorityEpoch()),
                InteractiveObjectCommandProposal(id<InteractiveObjectId>(firstView.groundItems[0].door->placement), first,
                    entity.transform().position(), ObjectRevision::initial(), ObjectInteractionKind::Activate, {}));
            auto opened = service.prepareDoorActivation(authority, activation);
            require(opened && opened->commit(accepted) == CanonicalDurabilityResult::Committed, "Two-cell host door activation failed");
            auto motion = service.prepareDoorStep(authority, id<ServerTick>(++tick), .05f);
            require(motion && motion->commit(accepted) == CanonicalDurabilityResult::Committed, "Two-cell host door motion failed");
            move(0, first);
            alice = view(service, 1);
            const auto doorState = *alice.groundItems[0].door;
            require(alice.containers[0].stacks.empty() && alice.groundItems[0].items.empty() && doorState.angle > 0
                && service.activeCells() == std::array{true, false}, "Alice did not see Bob's changes on return");
            move(0, second); move(1, second);
            require(service.activeCells() == std::array{false, true}
                && !service.prepareDoorStep(authority, id<ServerTick>(++tick), .05f)
                && view(service, 1).groundItems[0].items[0].stack == savedDrop.stack,
                "Unloaded interior simulated or reloaded loot");
            const auto sessions = std::vector(authority.activeSessions().begin(), authority.activeSessions().end());
            authority = std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), {}));
            service.synchronizeCells(authority);
            require(service.activeCells() == std::array{false, false}, "Disconnected host retained active interiors");
            EquipmentFileSink file(scratch / "cells.bin", true); FileFaults faults;
            const auto image = service.inventoryImage();
            require(file.writeSessionImage({reinterpret_cast<const char*>(image.data()), image.size()}, faults) == PersistenceResult::Accepted,
                "Two-cell host write failed");
            EquipmentBytes disk;
            require(readBoundedFile(scratch / "cells.bin", MaxEquipmentSessionBytes, disk, faults) == FileReadResult::Read, "Two-cell host read failed");
            InventoryHost restarted(descriptor, manifest, *registry, *crypto, std::as_bytes(std::span(disk)));
            auto& recovered = dynamic_cast<InventoryService&>(restarted.service());
            authority = std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), sessions));
            recovered.synchronizeCells(authority);
            const auto recoveredDrop = view(recovered, 1).groundItems[0].items[0];
            require(recoveredDrop.stack == savedDrop.stack && recoveredDrop.position == savedDrop.position,
                "OpenMW restart lost second interior drop");
            move(0, first); move(1, first); recovered.synchronizeCells(authority);
            const auto returned = view(recovered, 1);
            require(returned.containers[0].stacks.empty() && returned.groundItems[0].items.empty()
                && returned.groundItems[0].door->angle == doorState.angle, "OpenMW reload/restart regenerated first interior state");
            std::ifstream source(descriptor); const std::string text{std::istreambuf_iterator<char>(source), {}};
            for (const auto& [from, to] : {std::pair{std::string("cell interior:8"), std::string("cell interior:7")},
                    std::pair{std::string("vNext second interior"), std::string("vNext actor inventory test")}})
            {
                auto invalid = text; invalid.replace(invalid.find(from), from.size(), to);
                const auto path = scratch / "invalid.txt";
                { std::ofstream output(path); output << invalid; }
                bool rejected = false;
                try { InventoryHost bad(path, manifest, *registry, *crypto, image); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected, "Duplicate native cell mapping accepted");
            }
            std::cout << "cell host: Morrowind/generated interiors, split occupancy, per-cell stock placement, empty-cell release, committed loot/door return and disk restart passed; no graphical clients\n";
            return;
        }
        if (door)
        {
            auto authority = players(SessionGeneration::initial(), 1, 2);
            InventoryHost host(descriptor, testContentManifest(), *registry, *crypto, {});
            auto image = std::vector(host.service().inventoryImage().begin(), host.service().inventoryImage().end());
            const auto get64 = [](auto bytes, size_t offset) {
                uint64_t value = 0;
                for (size_t i = 0; i < 8; ++i) value |= uint64_t(std::to_integer<unsigned char>(bytes[offset + i])) << (8 * i);
                return value;
            };
            const auto doorLengthOffset = size_t(24 + 8 * (3 + get64(std::span(image), 16)));
            const auto directory = (scratch / "openmw").string();
            const char* args[]{"door-host", "--config", directory.c_str()};
            Loadout loadout(readLoadoutOptions(3, args));
            const auto placement = loadout.resolveDoor("vNext actor inventory test", "StartingInventories.esp", 10);
            DoorBinding binding(*loadout.store().get<ESM::Door>().find(placement.mRef.mRefID), placement.mRef);
            const auto tail = [&](auto bytes) {
                const auto size = size_t(get64(bytes, doorLengthOffset));
                return std::span(reinterpret_cast<const char*>(bytes.data() + bytes.size() - size), size);
            };
            require(decodeDoor(tail(std::span(image)), binding)->mDoorState == 0, "V9 host initial door missing");
            auto nearPlayers = std::vector(authority.players().begin(), authority.players().end());
            const auto& player = nearPlayers.front();
            nearPlayers.front() = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(player,
                id<ServerTick>(1), Transform(player.transform().cell(), Position3(1000 * 1024, 0, 0),
                    player.transform().orientation()), LinearVelocity3(0, 0, 0)));
            auto nearDoor = std::get<CanonicalServerState>(createCanonicalServerState(nearPlayers, authority.activeSessions()));
            const auto& caller = nearDoor.players().front();
            ServerCommandProposal activation(id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(1), CanonicalRevision::initial(),
                EntityPrecondition(caller.entityId(), caller.entityRevision(), caller.authorityEpoch()),
                InteractiveObjectCommandProposal(id<InteractiveObjectId>(placement.mIdentity), caller.transform().cell(),
                    caller.transform().position(), ObjectRevision::initial(), ObjectInteractionKind::Activate, {}));
            auto activated = host.service().prepareDoorActivation(nearDoor, activation);
            require(activated && activated->commit([](auto) { return CanonicalDurabilityResult::Committed; })
                == CanonicalDurabilityResult::Committed, "Real door activation failed");
            auto advanced = host.service().prepareDoorStep(nearDoor, id<ServerTick>(2), .4f);
            require(advanced && advanced->commit([](auto) { return CanonicalDurabilityResult::Committed; })
                == CanonicalDurabilityResult::Committed, "Real door step failed");
            image.assign(host.service().inventoryImage().begin(), host.service().inventoryImage().end());
            const auto moving = *decodeDoor(tail(std::span(image)), binding);
            require(moving.mDoorState == 1 && moving.mPosition.rot[2] > 0, "Real door motion missing");
            EquipmentFileSink file(scratch / "session.bin", true);
            FileFaults faults;
            require(file.writeSessionImage({reinterpret_cast<const char*>(image.data()), image.size()}, faults)
                == PersistenceResult::Accepted, "V9 host session write failed");
            EquipmentBytes disk;
            require(readBoundedFile(scratch / "session.bin", MaxEquipmentSessionBytes, disk, faults) == FileReadResult::Read,
                "V9 host session read failed");
            InventoryHost restarted(descriptor, testContentManifest(), *registry, *crypto, std::as_bytes(std::span(disk)));
            require(std::ranges::equal(image, restarted.service().inventoryImage()), "V9 host restart changed partial motion");
            auto& service = dynamic_cast<InventoryService&>(restarted.service());
            auto pickup = service.prepareInventory(authority, bind(authority, worldWire(service, authority, 1, true)).proposal());
            require(pickup && pickup->commit([&](auto next) {
                return file.writeSessionImage({reinterpret_cast<const char*>(next.data()), next.size()}, faults) == PersistenceResult::Accepted
                    ? CanonicalDurabilityResult::Committed : CanonicalDurabilityResult::Rejected;
            }) == CanonicalDurabilityResult::Committed, "V9 host inventory continuation failed");
            InventoryHost continued(descriptor, testContentManifest(), *registry, *crypto, service.inventoryImage());
            require(std::ranges::equal(service.inventoryImage(), continued.service().inventoryImage())
                && decodeDoor(tail(continued.service().inventoryImage()), binding)->mPosition == moving.mPosition,
                "V9 host inventory commit or restart reset door state");
            std::ifstream original(descriptor);
            std::string text((std::istreambuf_iterator<char>(original)), {});
            const auto alternative = scratch / "alternative.txt";
            const auto reject = [&](const std::string& changed, std::span<const std::byte> saved) {
                { std::ofstream out(alternative); out << changed; }
                bool failed = false;
                try { InventoryHost wrong(alternative, testContentManifest(), *registry, *crypto, saved); }
                catch (const std::exception&) { failed = true; }
                require(failed, "V9 host accepted incompatible descriptor/campaign");
            };
            auto other = text;
            other.replace(other.find(".esp\" 10"), 8, ".esp\" 11");
            reject(other, image);
            auto legacy = text;
            legacy.replace(legacy.find("native-inventory-9"), 18, "native-inventory-8");
            legacy.erase(legacy.find("door \""), std::string("door \"StartingInventories.esp\" 10\n").size());
            reject(legacy, image);
            { std::ofstream out(alternative); out << legacy; }
            InventoryHost v8(alternative, testContentManifest(), *registry, *crypto, {});
            InventoryHost v8Restored(alternative, testContentManifest(), *registry, *crypto, v8.service().inventoryImage());
            require(std::ranges::equal(v8.service().inventoryImage(), v8Restored.service().inventoryImage()),
                "V8 campaign recovery changed bytes");
            reject(text, v8.service().inventoryImage());
            reject(legacy.replace(legacy.find("native-inventory-8"), 18, "native-inventory-9"), {});
            writeStartingPlugin(26);
            reject(text, image);
            std::cout << "door host: real base/generated placement, activation and committed motion, inventory continuation/restart, binding rejection, unchanged v8 recovery; no graphical clients\n";
            return;
        }
        if (worldActors)
        {
            auto authority = players(SessionGeneration::initial(), 1, 2);
            InventoryHost host(descriptor, testContentManifest(), *registry, *crypto, {});
            auto& service = dynamic_cast<InventoryService&>(host.service());
            auto before = service.project(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
            require(before.containers.size() == 2 && before.containers[0].equipment.size() == 3,
                "Real content host did not bind corpse inventories/equipment or exposed living loot");
            require(before.equipment->actors.size() == 1
                && before.equipment->actors[0].slots[InventoryStore::Slot_Shirt]
                    == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("common_shirt_01")))
                && before.equipment->actors[0].slots[InventoryStore::Slot_Pants]
                    == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("common_pants_01")))
                && before.equipment->actors[0].slots[InventoryStore::Slot_CarriedRight]
                    == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("iron dagger"))),
                "Real content living actor public slots missing");
            if (worldItems)
            {
                const auto& starting = before.playerInventory.front();
                const auto equipped = starting.equipment.front().stackId;
                const auto gearIndex = size_t(std::ranges::find(starting.stacks, equipped, &CanonicalItemStack::stackId) - starting.stacks.begin());
                require(!service.prepareInventory(authority, bind(authority, worldWire(service, authority, 1, false, gearIndex)).proposal()),
                    "Equipped player item dropped without authoritative unequip");
                require(before.groundItems.front().items.size() == 2 && before.groundItems.front().nativePlacements.size() == 2,
                    "V7 host omitted placed world items");
                for (int i = 0; i < 2; ++i)
                {
                    auto take = service.prepareInventory(authority, bind(authority, worldWire(service, authority, 2, true)).proposal());
                    require(take && take->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                        "Real placed item pickup failed");
                }
                auto current = service.project(authority, id<SessionId>(2), id<ServerTick>(2), id<CanonicalRevision>(2)).value();
                const auto& stacks = current.playerInventory.front().stacks;
                const auto gold = id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("gold_001")));
                const auto dagger = id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("iron dagger")));
                require(std::ranges::find(stacks, gold, &CanonicalItemStack::prototypeId)->count == 107,
                    "World gold denomination did not use stock pickup conversion");
                const auto index = size_t(std::ranges::find(stacks, dagger, &CanonicalItemStack::prototypeId) - stacks.begin());
                if (stockPlacement)
                {
                    auto proposed = worldWire(service, authority, 2, false, index, 2);
                    proposed.placement = placementTestView(80,40);
                    const std::vector beforeDrop(service.inventoryImage().begin(),service.inventoryImage().end());
                    for (int invalid = 0; invalid < 7; ++invalid)
                    {
                        auto bad = proposed;
                        if (invalid == 0) bad.placement.reset();
                        if (invalid == 1) bad.placement->view[0] = std::numeric_limits<double>::quiet_NaN();
                        if (invalid == 2) bad.placement->projection.fill(0);
                        if (invalid == 3) bad.placement->cursorX = 2;
                        if (invalid == 4) bad.placement->view.fill(0);
                        if (invalid == 5) bad.count = 6;
                        if (invalid == 6) bad.placement->view[0] *= 0.1;
                        // Bypass the wire decoder to also exercise the native boundary's checks.
                        const auto bound = ServerApp::InventoryCommandBinding::resolve(authority,
                            bad.sessionId,bad.sessionGeneration,bad).value();
                        require(!service.prepareInventory(authority, bound.proposal())
                            && std::ranges::equal(beforeDrop,service.inventoryImage()), "Invalid placement changed durable state");
                    }
                    for (auto [x, expectedZ] : {std::pair{200.f,-22.f}, std::pair{-200.f,38.f}})
                    {
                        auto slope = proposed; slope.placement = placementTestView(x,40);
                        auto candidate = service.prepareInventory(authority,bind(authority,slope).proposal());
                        require(bool(candidate),"Stock slope query failed");
                        const auto placed = service.projectInventory(authority,id<SessionId>(2),id<ServerTick>(2),
                            id<CanonicalRevision>(2),candidate.get()).value();
                        require(placed.groundItems[0].items[0].position.z() == int64_t(expectedZ*1024),
                            "Server slope selection did not preserve stock ground fallback");
                    }
                    auto rejected = service.prepareInventory(authority,bind(authority,proposed).proposal());
                    require(rejected && rejected->commit([](auto){return CanonicalDurabilityResult::Rejected;})
                        == CanonicalDurabilityResult::Rejected && std::ranges::equal(beforeDrop,service.inventoryImage()),
                        "Rejected placement durability changed inventory or world");
                    auto accepted = service.prepareInventory(authority,bind(authority,proposed).proposal());
                    require(accepted && accepted->commit([](auto){return CanonicalDurabilityResult::Committed;})
                        == CanonicalDurabilityResult::Committed,"Table placement failed");
                    const auto table = service.projectInventory(authority,id<SessionId>(2),id<ServerTick>(3),id<CanonicalRevision>(3)).value();
                    require(table.groundItems[0].items[0].position == Position3(77*1024,-4*1024,38*1024)
                        && table.groundItems[0].items[0].stack.count == 2,"Table placement transform/count incorrect");
                    const auto remaining = std::ranges::find(table.playerInventory[0].stacks,dagger,&CanonicalItemStack::prototypeId);
                    require(remaining != table.playerInventory[0].stacks.end() && remaining->count == 3,"Partial placement subtracted wrong count");
                    const auto floorIndex = size_t(remaining - table.playerInventory[0].stacks.begin());
                    auto floor = worldWire(service,authority,2,false,floorIndex);
                    floor.placement = placementTestView(0,0,true);
                    auto fallback = service.prepareInventory(authority,bind(authority,floor).proposal());
                    require(fallback && fallback->commit([](auto){return CanonicalDurabilityResult::Committed;})
                        == CanonicalDurabilityResult::Committed,"Cursor miss did not drop on floor");
                    const auto saved = service.projectInventory(authority,id<SessionId>(2),id<ServerTick>(4),id<CanonicalRevision>(4)).value();
                    require(saved.groundItems[0].items.back().position == Position3(-3*1024,-4*1024,-22*1024),
                        "Committed floor transform differs");
                    const auto coin = std::ranges::find(saved.playerInventory[0].stacks,gold,&CanonicalItemStack::prototypeId);
                    auto coins = worldWire(service,authority,2,false,size_t(coin-saved.playerInventory[0].stacks.begin()),25);
                    coins.placement = placementTestView(80,46);
                    auto pile = service.prepareInventory(authority,bind(authority,coins).proposal());
                    require(pile && pile->commit([](auto){return CanonicalDurabilityResult::Committed;})
                        == CanonicalDurabilityResult::Committed,"Gold pile placement failed");
                    const auto goldView = service.projectInventory(authority,id<SessionId>(2),id<ServerTick>(4),id<CanonicalRevision>(4)).value();
                    const auto& droppedPile = goldView.groundItems[0].items.back();
                    require(droppedPile.stack.prototypeId == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("gold_025")))
                        && droppedPile.stack.count == 25 && droppedPile.position == Position3(72*1024,-10*1024,44*1024),
                        "Gold placement did not use stock pile bounds or committed supporting item");
                    Clock clock; Delivery both(clock,SessionGeneration::initial()); publish(service,authority,both,4);
                    require(both.clients[0]->confirmedGroundItemBaseline()->items
                        == both.clients[1]->confirmedGroundItemBaseline()->items,"Clients received different placement results");
                    const std::vector image(service.inventoryImage().begin(),service.inventoryImage().end());
                    InventoryHost restarted(descriptor,testContentManifest(),*registry,*crypto,image);
                    auto& restored = dynamic_cast<InventoryService&>(restarted.service());
                    require(std::ranges::equal(image,restored.inventoryImage()),"Restart changed placement image");
                    auto rejoined = players(*SessionGeneration::initial().next(),1,2);
                    Delivery reconnect(clock,*SessionGeneration::initial().next()); publish(restored,rejoined,reconnect,5);
                    require(reconnect.clients[1]->confirmedGroundItemBaseline()->items
                        == both.clients[0]->confirmedGroundItemBaseline()->items,"Reconnect moved or duplicated placements");
                    for (int i=0;i<3;++i)
                    {
                        auto pickup = restored.prepareInventory(rejoined,bind(rejoined,worldWire(restored,rejoined,1,true)).proposal());
                        require(pickup && pickup->commit([](auto){return CanonicalDurabilityResult::Committed;})
                            == CanonicalDurabilityResult::Committed,"Placed item pickup after restart failed");
                    }
                    std::ofstream changedMesh(scratch/"meshes"/"placement-table.osgt",std::ios::app); changedMesh << "\n# changed\n"; changedMesh.close();
                    bool mismatch = false;
                    try { InventoryHost changed(descriptor,testContentManifest(),*registry,*crypto,image); }
                    catch (const std::exception&) { mismatch = true; }
                    require(mismatch,"Changed placement mesh recovered an incompatible campaign");
                    std::cout << "stock placement host: real records, synthetic meshes, table/floor/slopes, invalid input, partial stacks, atomic rejection, two clients, reconnect/restart and mesh binding\n";
                    return;
                }
                auto drop = service.prepareInventory(authority, bind(authority, worldWire(service, authority, 2, false, index)).proposal());
                require(drop && drop->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                    "Real dagger drop failed");
                const std::vector image(service.inventoryImage().begin(), service.inventoryImage().end());
                InventoryHost restart(descriptor, testContentManifest(), *registry, *crypto, image);
                auto& restored = dynamic_cast<InventoryService&>(restart.service());
                auto pickup = restored.prepareInventory(authority, bind(authority, worldWire(restored, authority, 1, true)).proposal());
                require(pickup && pickup->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                    "Real dropped dagger pickup after restart failed");
                Clock clock; Delivery delivery(clock, SessionGeneration::initial()); publish(restored, authority, delivery, 3);
                for (const auto& client : delivery.clients)
                    require(client->confirmedGroundItemBaseline()->items.empty()
                        && client->confirmedGroundItemBaseline()->nativePlacements.size() == 2,
                        "Real world loop refilled original placements");
                writeStartingPlugin(26);
                bool rejected = false;
                try { InventoryHost changed(descriptor, testContentManifest(), *registry, *crypto, image); }
                catch (const std::exception&) { rejected = true; }
                require(rejected, "Changed world loadout recovered an incompatible campaign");
                std::cout << "real Morrowind.esm plus generated cell: v7 pickup, stock gold denomination, drop/restart/pickup; synthetic clients\n";
                return;
            }
            EquipmentFileSink file(scratch / "actors.equipment", true);
            FileFaults faults; EquipmentFileCommitter sink(file, faults);
            EquipmentBytes bytes; std::unique_ptr<const InventoryTransferSuccess> success;
            for (const auto& owner : before.containers)
                for (const auto& stack : owner.stacks)
                {
                    const auto current = service.project(authority, id<SessionId>(2), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
                    auto input = ClientInventoryTransactionCommand{id<SessionId>(2), SessionGeneration::initial(),
                        CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                        InventoryTransactionKind::TakeFromContainer, owner.container, stack.prototypeId, stack.stackId,
                        stack.count, {}, current.playerInventory.front().revision, current.containers.front().revision, {}, Position3(0, 0, 0)};
                    auto prepared = service.prepare(authority, bind(authority, input));
                    require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
                        "Real content equipped corpse loot failed");
                }
            const std::vector image(service.inventoryImage().begin(), service.inventoryImage().end());
            InventoryHost recovered(descriptor, testContentManifest(), *registry, *crypto, image);
            auto& restored = dynamic_cast<InventoryService&>(recovered.service());
            require(std::ranges::equal(image, restored.inventoryImage()), "Real actor restart changed image");
            Clock clock; Delivery delivery(clock, SessionGeneration::initial()); publish(restored, authority, delivery, 2);
            for (const auto& client : delivery.clients)
            {
                require(client->confirmedEquipmentSnapshot()->actors == before.equipment->actors,
                    "Real actor restart/late join lost living equipment");
                for (const auto& corpse : client->confirmedContainerInventoryBaselines())
                    require(corpse.stacks.empty() && corpse.equipment.empty(), "Real actor restart/late join refilled corpse");
            }
            writeStartingPlugin(26);
            bool rejected = false;
            try { InventoryHost changed(descriptor, testContentManifest(), *registry, *crypto, image); }
            catch (const std::exception&) { rejected = true; }
            require(rejected, "Changed actor loadout recovered an incompatible campaign");
            std::cout << "real Morrowind.esm plus generated actor cell: v6 NPC/creature placements, ordinary equipment, "
                "equipped corpse loot, living access closed, exact restart/late join, content mismatch rejected; synthetic clients\n";
            return;
        }
        if (wholeInterior)
        {
            auto authority = players(SessionGeneration::initial(), 1, 2);
            InventoryHost host(descriptor, testContentManifest(), *registry, *crypto, {});
            auto& service = dynamic_cast<InventoryService&>(host.service());
            const auto view = [&](InventoryService& value, uint64_t session) {
                return value.project(authority, id<SessionId>(session), id<ServerTick>(1), id<CanonicalRevision>(1)).value();
            };
            const auto before = view(service, 1);
            if (baseInventory)
                for (size_t i = 0; i < startingCharacters.size(); ++i)
                {
                    std::map<ItemPrototypeId, uint32_t> expected, actual;
                    for (const auto& item : startingCharacters[i].mInventory.mList)
                        expected[id<ItemPrototypeId>(MWWorld::inventoryRecordId(item.mItem))] += item.mCount;
                    const auto current = view(service, i + 1);
                    for (const auto& stack : current.playerInventory[0].stacks)
                        actual[stack.prototypeId] += stack.count;
                    if (expected != actual)
                    {
                        std::cerr << "starting character " << i << ": expected";
                        for (const auto& [item, count] : expected) std::cerr << ' ' << item.value() << ':' << count;
                        std::cerr << "; actual";
                        for (const auto& [item, count] : actual) std::cerr << ' ' << item.value() << ':' << count;
                        std::cerr << '\n';
                    }
                    require(expected == actual, "Host did not use the winning character inventory override");
                }
            require(before.containers.size() == 9, "Real interior did not bootstrap every placed inventory");
            require(before.containers[0].stacks.size() == 1 && before.containers[0].stacks[0].count == 10
                && before.containers[1].stacks.size() == 1 && before.containers[1].stacks[0].count == 10,
                "Real fixed saltrice inventories were not loaded");
            EquipmentFileSink file(scratch / "interior.equipment", true);
            FileFaults faults; EquipmentFileCommitter sink(file, faults);
            EquipmentBytes bytes; std::unique_ptr<const InventoryTransferSuccess> success;
            const auto equip = [&](uint64_t session, std::string_view record, EquipmentSlot slot, bool on) {
                const auto current = view(service, session);
                const auto& inventory = current.playerInventory.front();
                const auto prototype = id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId(record)));
                const auto item = std::ranges::find(inventory.stacks, prototype, &CanonicalItemStack::prototypeId);
                require(item != inventory.stacks.end(), "Real equipment item missing");
                const ClientInventoryTransactionCommand input{id<SessionId>(session), SessionGeneration::initial(),
                    CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                    on ? InventoryTransactionKind::EquipItem : InventoryTransactionKind::UnequipItem,
                    {}, prototype, item->stackId, 1, slot, inventory.revision, {}, {}, Position3(0, 0, 0)};
                auto prepared = service.prepareInventory(authority, bind(authority, input).proposal());
                require(prepared && prepared->commit([&](auto image) {
                    return file.writeSessionImage({reinterpret_cast<const char*>(image.data()), image.size()}, faults)
                        == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed : CanonicalDurabilityResult::Rejected;
                }) == CanonicalDurabilityResult::Committed, "Real starting equipment operation failed");
            };
            if (baseInventory)
            {
                const auto initial = view(service, 1).playerInventory.front().equipment;
                require(initial.size() == 3 && view(service, 2).playerInventory.front().equipment.empty(),
                    "Real starting loadouts did not auto-equip exactly shirt, pants and dagger");
                const auto publicGear = view(service, 2).equipment->members.front();
                for (const auto& [slot, name] : std::initializer_list<std::pair<int, const char*>>{
                        {8, "common_shirt_01"}, {9, "common_pants_01"}, {16, "iron dagger"}})
                    require(publicGear.slots[slot] == id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId(name))),
                        "Real automatic equipment was not publicly projected");
                equip(1, "iron dagger", EquipmentSlot::CarriedRight, false);
                equip(1, "common_shirt_01", EquipmentSlot::Shirt, false);
            }
            for (uint64_t session : {1, 2})
            {
                const auto current = view(service, session);
                const auto& shared = current.containers[session - 1];
                const auto stack = shared.stacks[0];
                const ClientInventoryTransactionCommand input{id<SessionId>(session), SessionGeneration::initial(),
                    CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                    InventoryTransactionKind::TakeFromContainer, shared.container, stack.prototypeId, stack.stackId,
                    stack.count, {}, current.playerInventory[0].revision, shared.revision, {}, Position3(0, 0, 0)};
                auto prepared = service.prepare(authority, bind(authority, input));
                require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
                    "Real loaded-cell inventory transfer failed");
            }
            const auto after = view(service, 1);
            require(after.containers[0].stacks.empty() && after.containers[1].stacks.empty(),
                "Real loaded inventories did not empty independently");
            for (size_t i = 2; i < before.containers.size(); ++i)
                require(before.containers[i].stacks == after.containers[i].stacks,
                    "Real transfer changed another placed inventory");
            if (baseInventory)
            {
                // Transfer the actual starting dagger through the emptied chest.
                for (uint64_t session : {1, 2})
                {
                    const auto current = view(service, session);
                    const auto& stacks = session == 1 ? current.playerInventory[0].stacks : current.containers[0].stacks;
                    const auto found = std::ranges::find(stacks,
                        id<ItemPrototypeId>(MWWorld::inventoryRecordId(ESM::RefId::stringRefId("iron dagger"))),
                        &CanonicalItemStack::prototypeId);
                    require(found != stacks.end(), "Starting dagger missing before real transfer");
                    ClientInventoryTransactionCommand input{id<SessionId>(session), SessionGeneration::initial(),
                        CommandSequence::initial(), id<CommandId>(1), id<CanonicalRevision>(1),
                        session == 1 ? InventoryTransactionKind::PutIntoContainer : InventoryTransactionKind::TakeFromContainer,
                        current.containers[0].container, found->prototypeId, found->stackId, 1, {},
                        current.playerInventory[0].revision, current.containers[0].revision, {}, Position3(0, 0, 0)};
                    auto prepared = service.prepare(authority, bind(authority, input));
                    require(service.commit(authority, prepared, sink, success, bytes) == PersistenceResult::Accepted,
                        "Real starting dagger could not transfer between players");
                }
                equip(2, "iron dagger", EquipmentSlot::CarriedRight, true);
            }
            const std::vector image(service.inventoryImage().begin(), service.inventoryImage().end());
            InventoryHost restored(descriptor, testContentManifest(), *registry, *crypto, image);
            auto& restoredService = dynamic_cast<InventoryService&>(restored.service());
            require(std::ranges::equal(image, restoredService.inventoryImage())
                && view(restoredService, 1).containers[0].stacks.empty()
                && view(restoredService, 2).containers[1].stacks.empty()
                && view(restoredService, 1).playerInventory[0].stacks == view(service, 1).playerInventory[0].stacks
                && view(restoredService, 2).playerInventory[0].stacks == view(service, 2).playerInventory[0].stacks,
                "Real whole-interior recovery refilled or changed inventories");
            for (uint64_t session : {1, 2})
                require(view(restoredService, session).playerInventory[0].equipment == view(service, session).playerInventory[0].equipment,
                    "Real whole-interior recovery lost equipment");
            Clock clock; Delivery late(clock, SessionGeneration::initial()); publish(restoredService, authority, late, 2);
            for (const auto& client : late.clients)
                require(client->confirmedContainerInventoryBaselines().size() == 9,
                    "Real interior late join lost container baselines");
            if (baseInventory)
                require(late.clients[0]->confirmedPlayerInventoryBaseline()->equipment.size() == 1
                    && late.clients[1]->confirmedPlayerInventoryBaseline()->equipment.size() == 1,
                    "Real starting equipment lost on late join");
            // This real cell exceeds the bound and also contains scripted barrels.
            std::ifstream input(descriptor);
            std::string altered((std::istreambuf_iterator<char>(input)), {});
            const std::string interior = "Seyda Neen, Fargoth's House";
            altered.replace(altered.find(interior), interior.size(), "Imperial Prison Ship");
            const auto unsupported = scratch / "unsupported.txt";
            { std::ofstream out(unsupported); out << altered; }
            bool rejected = false;
            try { InventoryHost wrong(unsupported, testContentManifest(), *registry, *crypto, {}); }
            catch (const std::invalid_argument& error)
            {
                const std::string_view message(error.what());
                rejected = message.find("requires script, lock or trap services") != std::string_view::npos
                    || message.find("exceeds the startup budget") != std::string_view::npos;
            }
            require(rejected, "Whole-cell discovery silently skipped an unsupported real container");
            if (baseInventory)
            {
                // A v4 save must never silently reinterpret its seeded actors as
                // v5 character inventories, or reset a saved character on mismatch.
                std::ifstream original(descriptor);
                std::string legacy((std::istreambuf_iterator<char>(original)), {});
                legacy.replace(legacy.find("native-inventory-5"), 18, "native-inventory-4");
                const std::string actors = "actors \"player\" \"vnext_second_character\"";
                legacy.replace(legacy.find(actors), actors.size(),
                    "actors \"player\" 3 \"vnext_second_character\" 5\nshirt \"common_shirt_01\"");
                const auto previous = scratch / "legacy.txt";
                { std::ofstream out(previous); out << legacy; }
                InventoryHost legacyHost(previous, testContentManifest(), *registry, *crypto, {});
                rejected = false;
                try { InventoryHost wrong(descriptor, testContentManifest(), *registry, *crypto, legacyHost.service().inventoryImage()); }
                catch (const std::exception&) { rejected = true; }
                require(rejected, "Legacy seed campaign silently migrated to character inventories");
                writeStartingPlugin(26);
                rejected = false;
                try { InventoryHost wrong(descriptor, testContentManifest(), *registry, *crypto, image); }
                catch (const std::exception&) { rejected = true; }
                require(rejected && std::ranges::equal(image, restoredService.inventoryImage()),
                    "Changed starting-loadout plugin recovered or mutated the committed campaign");
                std::cout << "real Morrowind.esm plus generated NPC override: distinct 4/1-stack starting loadouts, "
                    "automatic shirt/pants/dagger equipment, deliberate unequip, dagger transfer/recipient equip, "
                    "exact equipment restart/late join without re-equipping, legacy/content mismatch rejection; synthetic transport\n";
            }
            std::cout << "real Morrowind.esm: Fargoth's House, 9 inventories, two independent takes, unchanged peers, "
                "exact recovery and late join; oversized/scripted Prison Ship rejected; synthetic transport\n";
            return;
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
        // Selecting another placement of the SAME base must not recover this
        // campaign, even though both placements are empty and otherwise valid.
        std::ifstream descriptorInput(descriptor);
        std::string altered((std::istreambuf_iterator<char>(descriptorInput)), {});
        altered.replace(altered.find("421490"), 6, "421497");
        const auto mismatch = scratch / "mismatch.txt";
        { std::ofstream out(mismatch); out << altered; }
        bool rejected = false;
        try { InventoryHost wrong(mismatch, testContentManifest(), *registry, *crypto, committed); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && std::ranges::equal(committed, restored.service().inventoryImage()),
            "Different placed reference recovered or mutated committed image");
        ServerApp::Testing::nativeInventoryApplication(restored.service(), *opened);
        std::cout << "real-loadout host: placed barrel_01_empty 421490, recovery rejects another placement; synthetic transport\n";
    }

    void checkInventoryApplication(const std::filesystem::path& scratch, bool environment)
    {
        require(std::filesystem::create_directory(scratch), "Native application scratch already exists");
        Content content;
        std::unique_ptr<Environment> nativeEnvironment;
        auto crypto = makeProductionCredentialCrypto();
        if (environment)
        {
            populateEnvironment(content.store);
            nativeEnvironment = std::make_unique<Environment>(content.store, environmentFallbacks(), "application-fixture",
                testContentManifestId(), *crypto, 17);
        }
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
        ServerApp::Testing::nativeInventoryApplication(service, *file, nativeEnvironment.get());
        auto opened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        InventoryService recovered(content.store, content.readers, binding, true);
        const std::array references{content.actor, content.shirt};
        recovered.recover(opened->prefix().latest()->nativeInventory(), references);
        require(std::ranges::equal(recovered.inventoryImage(), service.inventoryImage()),
            "ServerApplication recovery did not restore both actors and container coherently");
        ServerApp::Testing::nativeInventoryApplication(recovered, *opened, nativeEnvironment.get());
    }

    void checkLeveledActorPersistence(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Spawn persistence scratch already exists");
        Content content;
        auto living = *content.store.get<ESM::NPC>().find(content.actor);
        living.mNpdt.mHealth = 50; content.store.overrideRecord(living);
        auto binding = content.binding();
        binding.mStreamExteriors = true;
        const auto cell = binding.mContainers[0].mCell;
        auto ref = ESM::makeBlankCellRef(); ref.mRefID = content.actor; ref.mRefNum = {701, 0};
        const auto actorId = MWWorld::PlacedRefTag | 701, noneId = MWWorld::PlacedRefTag | 702;
        const auto recordId = MWWorld::inventoryRecordId(content.actor);
        binding.mContainers.push_back({id<ContainerId>(actorId), cell, Position3(0,0,0), content.actor, ref});
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{cell, {}});
        binding.mWorldItems->mActorSpawns = {{actorId, recordId}, {noneId, 0}};
        binding.mActorSelections = std::vector<ActorSpawnSelection>{{actorId, recordId}, {noneId, 0}};
        InventoryService service(content.store, content.readers, binding);
        auto authority = players(); service.synchronizeCells(authority);
        const auto before = service.projectInventory(authority, id<SessionId>(1), id<ServerTick>(1), id<CanonicalRevision>(1));
        require(before && before->equipment->actors.size() == 1 && before->equipment->actors[0].actor.value() == actorId
            && before->groundItems[0].actorSpawns == binding.mWorldItems->mActorSpawns, "Spawn baseline lost identity or chance-none");
        const auto wire = decodeReliableGroundItemBaseline(encodeReliableGroundItemBaseline(before->groundItems[0]));
        require(std::holds_alternative<ReliableGroundItemBaseline>(wire)
            && std::get<ReliableGroundItemBaseline>(wire) == before->groundItems[0], "Actor spawn wire round trip changed choices");
        Clock clock; Delivery delivery(clock, SessionGeneration::initial()); publish(service, authority, delivery, 1);
        require(delivery.clients[0]->confirmedGroundItemBaseline()->actorSpawns == before->groundItems[0].actorSpawns
            && delivery.clients[1]->confirmedGroundItemBaseline()->actorSpawns == before->groundItems[0].actorSpawns,
            "Client session baseline assembly lost leveled actor selections");
        const std::vector<std::byte> saved(service.inventoryImage().begin(), service.inventoryImage().end());
        InventoryService restored(content.store, content.readers, binding, true);
        const std::array refs{content.actor, content.shirt, content.container};
        restored.recover(saved, refs);
        require(std::ranges::equal(saved, restored.inventoryImage()), "Actor choices changed across inventory recovery");
        service.synchronizeCells(std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), {})));
        authority = players(id<SessionGeneration>(2)); service.synchronizeCells(authority);
        const auto resumed = service.projectInventory(authority, id<SessionId>(1), id<ServerTick>(2), id<CanonicalRevision>(2));
        require(resumed && resumed->groundItems[0].actorSpawns == before->groundItems[0].actorSpawns
            && resumed->equipment->actors == before->equipment->actors, "Unload/reconnect reset spawned actors");
        for (int test = 0; test < 6; ++test)
        {
            auto bad = saved;
            if (test == 0) bad.resize(7);
            if (test == 1) bad[8] = std::byte{255}; // Oversized/truncated count.
            if (test == 2) bad[16] = std::byte{0}; // Unknown marker.
            if (test == 3) bad[24] ^= std::byte{1}; // Altered selected actor.
            if (test == 4) std::copy_n(bad.begin() + 16, 16, bad.begin() + 32); // Duplicate.
            if (test == 5) bad.push_back(std::byte{0});
            bool rejected = false;
            try { restored.recover(bad, refs); } catch (const std::exception&) { rejected = true; }
            require(rejected && std::ranges::equal(saved, restored.inventoryImage()), "Malformed spawn image mutated installed state");
        }
        std::cout << "Leveled actors: durable selected/none choices, wire appearance, unload/reconnect, restart and atomic malformed-image rejection passed\n";
    }

    void checkAreaCrossings(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Area crossing scratch already exists");
        Content content;
        auto binding = content.binding();
        binding.mStreamExteriors = true;
        binding.mTeleportDoors.emplace();
        const auto interior = binding.mContainers[0].mCell;
        const auto exterior = [](int x) { return CellId::exterior(id<CellSpaceId>(8), x, 0); };
        const std::array cells{interior, exterior(-1), exterior(0), exterior(1), exterior(2), exterior(8), exterior(9)};
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{cells[0], {}});
        binding.mSecondWorldItems.emplace(InventoryServiceBinding::WorldItems{cells[1], {}});
        for (size_t i = 2; i < cells.size(); ++i)
        {
            auto ref = ESM::makeBlankCellRef(); ref.mRefID = content.shirt;
            ref.mRefNum = {uint32_t(500 + i), 0};
            ref.mPos.pos[0] = float(cells[i].asExterior()->gridX() * 8192 + 32);
            binding.mAdditionalWorldItems.push_back({cells[i], {{MWWorld::PlacedRefTag | (500 + i), ref}}});
        }
        ESM::Door base; base.blank(); base.mId = ESM::RefId::stringRefId("area_door"); content.store.insertStatic(base);
        auto doorRef = ESM::makeBlankCellRef(); doorRef.mRefID = base.mId; doorRef.mRefNum = {700, 0};
        binding.mDoors.push_back({MWWorld::PlacedRefTag | 700, exterior(0), doorRef});
        InventoryService service(content.store, content.readers, binding);
        auto initial = players();
        auto entities = std::vector(initial.players().begin(), initial.players().end());
        for (size_t i = 0; i < entities.size(); ++i)
            entities[i] = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(entities[i], id<ServerTick>(1),
                Transform(exterior(i ? 8 : -1), Position3(i ? int64_t(8) * 8192 * 1024 : -1, 0, 0),
                    entities[i].transform().orientation()), LinearVelocity3(0,0,0)));
        initial = std::get<CanonicalServerState>(createCanonicalServerState(entities, initial.activeSessions()));
        service.synchronizeCells(initial);
        require(service.activeAreas() == std::vector<bool>({false, true, true, false, false, true, true}),
            "Split players did not retain the union of exterior neighborhoods");
        constexpr int64_t width = 8192 * 1024;
        require(service.movementCell(exterior(0), Position3(-1,0,0)) == exterior(-1)
            && service.movementCell(exterior(-1), Position3(0,0,0)) == exterior(0)
            && service.movementCell(exterior(0), Position3(width-1,0,0)) == exterior(0)
            && service.movementCell(exterior(0), Position3(width,0,0)) == exterior(1)
            && !service.movementCell(exterior(0), Position3(2*width,0,0))
            && !service.movementCell(exterior(2), Position3(3*width,0,0))
            && !service.movementCell(exterior(0), Position3(0,-1,0))
            && !service.allowsCellTransition(exterior(0), interior, Position3(0,0,0)),
            "Exterior boundary, negative coordinate, adjacency or domain validation failed");
        NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
        const std::array spaces{CellSpaceDeclaration{id<CellSpaceId>(7), CellSpaceKind::Interior},
            CellSpaceDeclaration{id<CellSpaceId>(8), CellSpaceKind::Exterior}};
        const auto manifest = ContentManifest::create(testContentManifestId(), spaces, cells, id<AppearanceId>(1), testMovementProfile()).value();
        CanonicalCommandReducer reducer(initial, observability, manifest);
        const auto catalog = ServerScriptStateCatalog::create({}).value();
        auto scripts = CanonicalScriptState::initial(catalog).value();
        std::array<std::byte,32> config{}; config[0] = std::byte{1};
        const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(config).value(), {}, catalog, {}).value();
        const auto path = scratch / "crossings.bin";
        auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(ServerApp::CanonicalPersistenceFile::open(path, identity));
        require(reducer.configureDurability(*file, nullptr, nullptr, nullptr, nullptr, nullptr, &scripts, &service),
            "Crossing durability composition failed");
        Clock clock; Delivery delivery(clock, SessionGeneration::initial());
        uint64_t tick = 1;
        const auto execute = [&](auto payload, CommandDisposition expected) {
            const auto& caller = *reducer.state().findActiveSession(id<SessionId>(1));
            const auto& player = *reducer.state().findPlayer(caller.playerId());
            const auto sequence = caller.highestContiguousFinalizedCommand()
                ? *caller.highestContiguousFinalizedCommand()->next() : CommandSequence::initial();
            ServerCommandProposal command(caller.sessionId(), caller.sessionGeneration(), sequence, id<CommandId>(sequence.value()),
                reducer.canonicalRevision(), EntityPrecondition(player.entityId(), player.entityRevision(), player.authorityEpoch()), payload);
            ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(++tick), IngressOrdinal::initial());
            require(intake.submit(command) == CommandSubmissionResult::Accepted, "Crossing intake failed");
            clock.value += tick * 33'333'334;
            auto batches = intake.pump();
            require(batches && batches.batches().size() == 1, "Crossing batch failed");
            auto pending = reducer.prepareTick(batches.batches().front());
            require(pending.result().dispositions()[0].disposition() == expected, "Crossing command disposition mismatch");
            require(reducer.commit(std::move(pending)), "Crossing commit failed");
            service.synchronizeCells(reducer.state());
            publish(service, reducer.state(), delivery, ++tick);
        };
        const auto move = [&](int64_t x, CommandDisposition expected = CommandDisposition::Applied) {
            execute(PlayerLocomotionCommandProposal(id<LocomotionInputTick>(tick+1), id<LocomotionInputSequence>(tick+1),
                LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0,0,0), Position3(x,0,0))), expected);
        };
        move(0);
        require(reducer.state().findPlayer(id<PlayerId>(11))->transform().cell() == exterior(0)
            && service.activeAreas() == std::vector<bool>({false,true,true,true,false,true,true}),
            "Position crossing did not atomically change cell and active neighborhoods");
        auto baseline = *delivery.clients[0]->confirmedGroundItemBaseline();
        require(baseline.cell == exterior(0) && baseline.neighbors.size() == 2 && baseline.doors.size() == 1
            && baseline.items.size() == 1 && delivery.clients[1]->confirmedGroundItemBaseline()->cell == exterior(8),
            "Crossing wire baselines lost neighborhood loot/door or changed the other player");
        const auto item = baseline.items[0];
        execute(InteractiveObjectCommandProposal(id<InteractiveObjectId>(binding.mDoors[0].mId), exterior(0), Position3(0,0,0),
            ObjectRevision::initial(), ObjectInteractionKind::Activate, {}), CommandDisposition::Applied);
        const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
        auto step = service.prepareDoorStep(reducer.state(), id<ServerTick>(++tick), .05f);
        require(step && step->commit(accepted) == CanonicalDurabilityResult::Committed, "Active area door did not move");
        const auto angle = service.projectInventory(reducer.state(), id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick))->groundItems[0].doors[0].angle;
        move(width);
        require(delivery.clients[0]->confirmedGroundItemBaseline()->cell == exterior(1), "Adjacent exterior crossing failed");
        move(2*width);
        require(!service.activeAreas()[2] && !service.prepareDoorStep(reducer.state(), id<ServerTick>(++tick), .05f),
            "Unloaded exterior continued advancing its door");
        const auto before = reducer.state().findPlayer(id<PlayerId>(11))->transform();
        move(3*width, CommandDisposition::UnknownCell);
        execute(CellTransitionCommandProposal(interior), CommandDisposition::ObjectInteractionRejected);
        require(reducer.state().findPlayer(id<PlayerId>(11))->transform() == before, "Rejected crossing changed canonical position");
        move(width); move(0);
        baseline = *delivery.clients[0]->confirmedGroundItemBaseline();
        require(baseline.items[0].stack == item.stack && baseline.items[0].position == item.position
            && baseline.doors[0].angle == angle && baseline.doors[0].direction == 1,
            "Area reload reset loot identity or frozen door motion");
        Delivery late(clock, id<SessionGeneration>(2));
        auto resumed = players(id<SessionGeneration>(2));
        resumed = std::get<CanonicalServerState>(createCanonicalServerState(reducer.state().players(), resumed.activeSessions()));
        publish(service, resumed, late, ++tick);
        require(late.clients[0]->confirmedGroundItemBaseline()->doors == baseline.doors
            && late.clients[0]->confirmedGroundItemBaseline()->neighbors.size() == baseline.neighbors.size()
            && late.clients[0]->confirmedGroundItemBaseline()->items[0].stack == item.stack,
            "Reconnected client did not converge to the area baseline");
        InventoryService recovered(content.store, content.readers, binding, true);
        const std::array refs{content.actor, content.shirt, base.mId};
        recovered.recover(service.inventoryImage(), refs);
        recovered.synchronizeCells(resumed);
        const auto restored = recovered.projectInventory(resumed, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick));
        require(restored && restored->groundItems[0].doors[0].angle == angle && restored->groundItems[0].items[0].stack == item.stack,
            "V14 restart lost area door or loot state");
        auto reopened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(ServerApp::CanonicalPersistenceFile::open(path, identity));
        require(reopened->restoredState()->findPlayer(id<PlayerId>(11))->transform().cell() == exterior(0)
            && reopened->restoredState()->findPlayer(id<PlayerId>(22))->transform() == initial.findPlayer(id<PlayerId>(22))->transform(),
            "Crossings were not durable or moved the other player");
        service.synchronizeCells(std::get<CanonicalServerState>(createCanonicalServerState(resumed.players(), {})));
        require(std::ranges::none_of(service.activeAreas(), [](bool active) { return active; }), "Disconnected players pinned area scenes");
        std::cout << "V14 crossings: signed boundaries, reducer rejection, split neighborhoods, wire loot/doors, unload/reentry, reconnect and persistence passed; synthetic clients\n";
    }

    void checkDoorService(const std::filesystem::path& scratch, bool streaming)
    {
        require(std::filesystem::create_directory(scratch), "Door scratch already exists");
        Content content;
        ESM::Door base; base.blank(); base.mId = ESM::RefId::stringRefId("service_door");
        content.store.insertStatic(base);
        auto binding = content.binding();
        binding.mDoor = ESM::makeBlankCellRef();
        binding.mDoor->mRefID = base.mId;
        binding.mDoor->mRefNum = {700, 0};
        binding.mDoorId = MWWorld::PlacedRefTag | 700;
        binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{binding.mContainers[0].mCell, {}});
        if (streaming)
        {
            binding.mStreamExteriors = true;
            binding.mDoors.push_back({binding.mDoorId, binding.mWorldItems->mCell, *binding.mDoor});
            binding.mDoor.reset();
        }
        InventoryService service(content.store, content.readers, binding);
        auto authority = players();
        service.synchronizeCells(authority);
        const auto view = [&](InventoryService& owner, const PreparedNativeInventory* pending = nullptr) {
            const auto projected = owner.projectInventory(authority, id<SessionId>(1), id<ServerTick>(1),
                id<CanonicalRevision>(1), pending);
            require(projected.has_value(), "Door projection failed");
            const auto& ground = projected->groundItems[0];
            return streaming ? ground.doors.at(0) : *ground.door;
        };
        const auto command = [&](InventoryService& owner, uint64_t session = 1) {
            const auto* active = authority.findActiveSession(id<SessionId>(session));
            const auto* player = authority.findPlayer(active->playerId());
            return ServerCommandProposal(active->sessionId(), active->sessionGeneration(), CommandSequence::initial(),
                id<CommandId>(1), CanonicalRevision::initial(),
                EntityPrecondition(player->entityId(), player->entityRevision(), player->authorityEpoch()),
                InteractiveObjectCommandProposal(id<InteractiveObjectId>(binding.mDoorId), player->transform().cell(),
                    player->transform().position(), id<ObjectRevision>(view(owner).motion), ObjectInteractionKind::Activate, {}));
        };
        const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
        const NativeInventoryCommit rejected = [](auto) { return CanonicalDurabilityResult::Rejected; };
        auto activation = service.prepareDoorActivation(authority, command(service));
        auto contender = service.prepareDoorActivation(authority, command(service, 2));
        auto inventoryBeforeDoor = service.prepareInventory(authority, bind(authority, wire(service, authority, 1, true, 1)).proposal());
        const auto initial = view(service);
        const std::vector initialImage(service.inventoryImage().begin(), service.inventoryImage().end());
        require(activation && view(service, activation.get()).direction == 1 && view(service) == initial,
            "Door preparation leaked state or lacked opening candidate");
        require(activation->commit(rejected) == CanonicalDurabilityResult::Rejected && view(service) == initial
            && std::ranges::equal(initialImage, service.inventoryImage()), "Rejected door activation changed state");
        require(activation->commit(accepted) == CanonicalDurabilityResult::Committed, "Door activation retry failed");
        require(contender->commit(accepted) == CanonicalDurabilityResult::Rejected, "Stale door contender committed");
        const auto opened = view(service);
        // V14 seals the latest independent area-door state at inventory commit;
        // the legacy door shares the runtime revision and rejects that candidate.
        require(inventoryBeforeDoor && inventoryBeforeDoor->commit(accepted)
                == (streaming ? CanonicalDurabilityResult::Committed : CanonicalDurabilityResult::Rejected)
            && view(service) == opened, "Inventory prepared before door commit changed the door state");
        const auto step = [&](uint64_t tick, const CanonicalServerState& state) {
            auto pending = service.prepareDoorStep(state, id<ServerTick>(tick), .05f);
            require(pending && pending->commit(accepted) == CanonicalDurabilityResult::Committed, "Door step failed");
        };
        step(2, authority);
        const auto moving = view(service);
        require(moving.angle > 0 && moving.direction == 1, "Door did not advance");
        auto rejectedStep = service.prepareDoorStep(authority, id<ServerTick>(3), .05f);
        const std::vector beforeStep(service.inventoryImage().begin(), service.inventoryImage().end());
        require(rejectedStep->commit(rejected) == CanonicalDurabilityResult::Rejected && view(service) == moving
            && std::ranges::equal(beforeStep, service.inventoryImage()), "Rejected step leaked angle or persistence");
        const ClientDoorObstruction codecSample{id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(2),
            binding.mDoorId, moving.motion, 1, true};
        auto invalidReport = encodeClientDoorObstruction(codecSample);
        invalidReport[49] = std::byte{2};
        require(!decodeClientDoorObstruction(invalidReport), "Non-boolean door report accepted");
        invalidReport = encodeClientDoorObstruction(codecSample);
        invalidReport.resize(51);
        require(!decodeClientDoorObstruction(invalidReport), "Trailing report bytes accepted");
        invalidReport = encodeClientDoorObstruction(codecSample);
        std::fill(invalidReport.begin() + 41, invalidReport.begin() + 49, std::byte{0});
        require(!decodeClientDoorObstruction(invalidReport), "Zero report sequence accepted");
        const auto report = [&](uint64_t session, uint64_t seq, bool blocked, uint64_t observed, uint64_t received,
                                uint64_t motion = 0) {
            ClientDoorObstruction sample{id<SessionId>(session), SessionGeneration::initial(), id<ServerTick>(observed),
                binding.mDoorId, motion ? motion : view(service).motion, seq, blocked};
            const auto bytes = encodeClientDoorObstruction(sample);
            require(decodeClientDoorObstruction(bytes) == sample, "Door report codec roundtrip failed");
            require(!decodeClientDoorObstruction(std::span(bytes).first(bytes.size() - 1)), "Truncated door report accepted");
            service.reportDoorObstruction(authority, *decodeClientDoorObstruction(bytes), id<ServerTick>(received));
        };
        report(1, 2, true, 2, 3); // Late contact applies to current motion, no angle rewind.
        step(3, authority);
        require(view(service).angle == moving.angle && view(service).blocked, "Late local obstruction did not stall");
        report(2, 1, true, 3, 3);
        report(1, 3, false, 3, 3);
        step(4, authority);
        require(view(service).angle == moving.angle, "One clear report overrode another player's block");
        report(2, 2, false, 4, 4);
        report(2, 1, true, 3, 4); // Reordered packet must not replace the newer clear.
        step(5, authority);
        require(view(service).angle > moving.angle && !view(service).blocked, "Reordered block replaced fresh clear");
        report(1, 4, true, 5, 5);
        auto reverse = service.prepareDoorActivation(authority, command(service));
        require(reverse && reverse->commit(accepted) == CanonicalDurabilityResult::Committed, "Door reversal failed");
        const auto reversed = view(service);
        report(1, 99, true, 5, 6, moving.motion);
        step(6, authority);
        require(view(service).motion != moving.motion && view(service).direction == 2
            && view(service).angle < reversed.angle && !view(service).blocked, "Old motion blocked reversed door");
        report(1, 1, true, 6, 6);
        step(7, authority);
        const auto stalled = view(service);
        require(stalled.blocked, "Current motion block ignored");
        report(1, 2, true, 6, 16); // Receipt time cannot renew old observations.
        step(16, authority);
        require(view(service).angle < stalled.angle && !view(service).blocked, "Expired obstruction survived");
        auto reopen = service.prepareDoorActivation(authority, command(service));
        require(reopen->commit(accepted) == CanonicalDurabilityResult::Committed, "Reopen failed");
        step(17, authority);
        report(1, 1, true, 17, 17);
        const std::array remaining{authority.activeSessions()[1]};
        auto disconnected = std::get<CanonicalServerState>(createCanonicalServerState(authority.players(), remaining));
        step(18, disconnected);
        require(!view(service).blocked, "Disconnected player retained obstruction");
        auto resumed = players(id<SessionGeneration>(2));
        step(19, resumed);
        require(!view(service).blocked, "Prior session generation retained obstruction");
        report(1, 2, false, 19, 19);
        report(1, 3, true, 30, 20); // Future observation is not applicable.
        step(20, authority);
        require(!view(service).blocked, "Future report admitted");

        Clock clock;
        Delivery delivery(clock, SessionGeneration::initial());
        publish(service, authority, delivery, 21);
        for (auto& client : delivery.clients)
        {
            const auto& ground = *client->confirmedGroundItemBaseline();
            require((streaming ? ground.doors.at(0) : *ground.door) == view(service),
                "Door angle lost on client baseline assembly");
        }
        InventoryService recovered(content.store, content.readers, binding, true);
        const std::array references{content.actor, content.shirt, base.mId};
        recovered.recover(service.inventoryImage(), references);
        recovered.synchronizeCells(resumed);
        require(view(recovered).angle == view(service).angle && view(recovered).direction == view(service).direction,
            "Restart lost moving door state");
        require(!view(recovered).blocked, "Restart restored transient obstruction");
        auto afterRestart = recovered.prepareDoorStep(resumed, id<ServerTick>(22), .05f);
        require(afterRestart && afterRestart->commit(accepted) == CanonicalDurabilityResult::Committed,
            "Restarted door did not resume");
        auto uncertain = recovered.prepareDoorStep(resumed, id<ServerTick>(23), .05f);
        require(uncertain && uncertain->commit([](auto) { return CanonicalDurabilityResult::Failed; })
                == CanonicalDurabilityResult::Failed && recovered.inventoryImage().empty(),
            "Uncertain door write did not close native service");

        // A real canonical file transaction must accept an autonomous door step,
        // and retain command disposition + door image together for activation.
        NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
        InventoryService canonical(content.store, content.readers, binding);
        canonical.synchronizeCells(authority);
        CanonicalCommandReducer reducer(players(), observability);
        const auto catalog = ServerScriptStateCatalog::create({}).value();
        auto scripts = CanonicalScriptState::initial(catalog).value();
        std::array<std::byte, 32> configuration{}; configuration[0] = std::byte{1};
        const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(configuration).value(), {}, catalog, {}).value();
        auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(scratch / "doors.bin", identity));
        require(reducer.configureDurability(*file, nullptr, nullptr, nullptr, nullptr, nullptr, &scripts, &canonical),
            "Canonical door binding failed");
        Clock tickClock;
        ServerCommandIntakeCoordinator intake(tickClock, observability, tickClock.now(), id<ServerTick>(1), IngressOrdinal::initial());
        require(intake.submit(command(canonical)) == CommandSubmissionResult::Accepted, "Door command intake failed");
        tickClock.value += 33'333'334;
        auto batch = intake.pump();
        require(batch && batch.batches().size() == 1, "Door command batch failed");
        auto pending = reducer.prepareTick(batch.batches().front());
        require(pending.result().dispositions()[0].disposition() == CommandDisposition::Applied
            && reducer.commit(std::move(pending)), "Canonical door activation failed");
        tickClock.value += 33'333'334;
        batch = intake.pump();
        require(batch && batch.batches().size() == 1, "Door motion batch failed");
        auto motion = reducer.prepareTick(batch.batches().front());
        require(reducer.stageNativeDoorStep(motion, id<ServerTick>(2), .05f) && motion.candidateNativeInventory()
            && reducer.commit(std::move(motion)) && view(canonical).angle > 0,
            "Autonomous door step did not commit canonically");
        require(std::ranges::equal(file->prefix().latest()->nativeInventory(), canonical.inventoryImage()),
            "Canonical door image differs from durable image");
        if (streaming)
        {
            std::cout << "V14 area door: atomic activation/motion, contention, two-player contacts, expiry/reversal/disconnect, wire and recovery passed\n";
            return;
        }
        auto applicationBinding = binding;
        applicationBinding.mPlayers = {id<PlayerId>(1), id<PlayerId>(2)};
        InventoryService applicationService(content.store, content.readers, applicationBinding);
        auto applicationFile = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(scratch / "door-application.bin", identity));
        ServerApp::Testing::nativeInventoryApplication(applicationService, *applicationFile);
        std::cout << "Door service: transaction, two-player telemetry, expiry, reversal, restart and wire clients passed\n";
    }

    void checkCanonicalInventory(const std::filesystem::path& scratch, bool equipment, bool takeAll, bool worldItems)
    {
        require(std::filesystem::create_directory(scratch), "Canonical inventory scratch already exists");
        Content content;
        const auto extra = ESM::RefId::stringRefId("bulk_canonical_item");
        if (takeAll)
        {
            ESM::Miscellaneous item; item.blank(); item.mId = extra; content.store.insertStatic(item);
            auto chest = *content.store.get<ESM::Container>().find(content.container);
            chest.mInventory.mList = {{2, content.shirt}, {3, extra}};
            content.store.overrideRecord(chest);
        }
        auto binding = content.binding();
        if (worldItems)
        {
            auto placed = ESM::makeBlankCellRef(); placed.mRefNum = {500, 0}; placed.mRefID = content.shirt; placed.mCount = 2;
            binding.mWorldItems.emplace(InventoryServiceBinding::WorldItems{binding.mContainers[0].mCell,
                {{MWWorld::PlacedRefTag | 500, placed}}});
        }
        InventoryService service(content.store, content.readers, binding);
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
        const auto action = [&](InventoryService& current, const CanonicalServerState& state, uint64_t session, bool put, uint32_t count) {
            if (worldItems) return worldWire(current, state, session, put);
            auto input = wire(current, state, session, equipment || (takeAll ? !put : put), equipment ? 1 : count);
            if (takeAll && put)
            {
                input.kind = InventoryTransactionKind::TakeAllFromContainer;
                input.count = 1;
            }
            if (equipment)
            {
                input.kind = InventoryTransactionKind::EquipItem; input.slot = EquipmentSlot::Shirt;
                input.containerId.reset(); input.expectedContainerRevision.reset();
            }
            return input;
        };
        const auto input = bind(reducer.state(), action(service, reducer.state(), 1, true, 2)).proposal();
        const auto contender = bind(reducer.state(), action(service, reducer.state(), 2, true, 1)).proposal();
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
        require(candidate && (worldItems ? candidate->groundItems[0].items.empty() : equipment ? candidate->playerInventory[0].equipment.size() == 1
            : takeAll ? candidate->containers[0].stacks.empty() && candidate->playerInventory[0].stacks.size() == 2
            : candidate->containers[0].stacks[0].count == 2), "Native candidate projection missing");
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
        auto staleInput = action(service, reducer.state(), 2, false, 1);
        staleInput.commandSequence = *contender.commandSequence().next();
        staleInput.commandId = id<CommandId>(contender.commandId().value() + 1);
        staleInput.expectedInventoryRevision = InventoryRevision::initial();
        auto stale = prepare(bind(reducer.state(), staleInput).proposal(), 3);
        require(stale.result().dispositions()[0].disposition() == CommandDisposition::InventoryTransactionRejected
            && reducer.commit(std::move(stale)), "Stale native input was not durably rejected");
        auto opened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
            ServerApp::CanonicalPersistenceFile::open(path, identity));
        InventoryService recovered(content.store, content.readers, binding, true);
        std::vector references{content.actor, content.shirt};
        if (takeAll) references.push_back(extra);
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
        auto take = action(recovered, resumed, 2, false, 1);
        ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(4), IngressOrdinal::initial());
        require(intake.submit(bind(resumed, take).proposal()) == CommandSubmissionResult::Accepted, "Continuation intake failed");
        clock.value += 4 * 33'333'334;
        auto pumped = intake.pump();
        auto next = continued.prepareTick(pumped.batches().front());
        require(next.result().dispositions()[0].disposition() == CommandDisposition::Applied
            && continued.commit(std::move(next)), "Recovered native continuation failed");
        const auto view = recovered.project(continued.state(), id<SessionId>(2), id<ServerTick>(4), continued.canonicalRevision());
        require(view && (worldItems ? view->groundItems[0].items.size() == 1 : equipment ? view->playerInventory[0].equipment.size() == 1
            : view->containers[0].stacks[0].count == 1), "Continuation lost native state");
        std::cout << (equipment ? "equipment" : "inventory")
            << " synthetic canonical integration: owned preparation, retry/stale rejection, joint file recovery and continuation\n";
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
    void checkNavigatingActor(const std::filesystem::path& scratch, const std::filesystem::path& config,
        const std::filesystem::path& settings)
    {
        require(std::filesystem::create_directory(scratch), "Navigation scratch already exists");
        auto crypto=makeProductionCredentialCrypto(); require(bool(crypto), "Navigation crypto unavailable");
        struct Identities final : PlayerIdentityPersistence
        { bool replace(std::span<const PersistedPlayerIdentity>) noexcept override { return true; } } storage;
        CharacterDerivedState derived; derived.attributes.fill(40); derived.skills.fill(10);
        const auto profile=CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter, CharacterCreationPhase::Complete,
            "Navigation participant", CharacterAppearance{id<RaceRecordId>(1),id<HeadRecordId>(1),id<HairRecordId>(1),CharacterSex::Male},
            CharacterClass{id<ClassRecordId>(1)},id<BirthsignRecordId>(1),derived,{},id<CharacterProfileRevision>(2)).value();
        std::vector<PersistedPlayerIdentity> records;
        for (uint64_t i : {1,2})
        {
            CredentialDigest digest; digest.bytes.fill(std::byte(i));
            records.push_back({{id<PlayerId>(i),id<EntityId>(i==1 ? 111 : 222),id<AppearanceId>(1),testContentManifestId()},
                digest, *players(SessionGeneration::initial(),1,2).findPlayer(id<PlayerId>(i)), profile});
        }
        auto registry=std::get<std::unique_ptr<PlayerIdentityRegistry>>(PlayerIdentityRegistry::create(*crypto,storage,records));
        const auto descriptor=scratch/"native.txt";
        {
            std::ofstream out(descriptor);
            out << "native-inventory-16\nmanifest " << ([] { std::ostringstream out; const auto manifestId=testContentManifestId(); for (auto byte : manifestId.bytes()) out << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte); return out.str(); })() << "\nconfig " << std::quoted(config.string())
                << "\nplayers 1 2\nactors \"raflod the braggart\" \"player\"\nloot 1 0\ninterior \"Seyda Neen, Arrille's Tradehouse\""
                << "\ndoors auto\ncell interior:7\nareas 1\nnpc \"raflod the braggart\" " << std::quoted(settings.string())
                << "\ndestination -550 70 385 120\n";
        }
        using namespace TES3MP::OpenMWAdapter;
        for (uint64_t leaving : {1,2})
        {
            InventoryHost host(descriptor,testContentManifest(),*registry,*crypto,{});
            auto& service=host.service(); auto initial=players(SessionGeneration::initial(),1,2);
            service.synchronizeCells(initial);
            NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics,events);
            CanonicalCommandReducer reducer(initial,observability,testContentManifest());
            const auto catalog=ServerScriptStateCatalog::create({}).value();
            auto scripts=CanonicalScriptState::initial(catalog).value();
            std::array<std::byte,32> configuration{}; configuration[0]=std::byte{16};
            const auto identity=CanonicalPersistenceIdentity::create(testContentManifestId(),ServerConfigurationId::fromBytes(configuration).value(),{},catalog,{}).value();
            auto file=std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(scratch/("campaign-"+std::to_string(leaving)),identity));
            struct Port final : CanonicalDurabilityPort
            {
                ServerApp::CanonicalPersistenceFile& file; ServerApp::NativeInventoryService& service;
                bool reject=false; size_t commits=0;
                Port(ServerApp::CanonicalPersistenceFile& f,ServerApp::NativeInventoryService& s):file(f),service(s){}
                CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
                    CanonicalRevision revision,std::span<const DurableCommandOrder> commands,const CanonicalInventoryWorld* inventory,
                    const CanonicalCombatWorld* combat,const CanonicalInteractiveObjectWorld* objects,const CanonicalActorWorld* actors,
                    const CanonicalWorldState* world,const CanonicalScriptState* scripts,std::span<const std::byte> image) noexcept override
                {
                    if (reject) return CanonicalDurabilityResult::Rejected;
                    ++commits; return file.commit(candidate,revision,commands,inventory,combat,objects,actors,world,scripts,image);
                }
            } port(*file,service);
            require(reducer.configureDurability(port,nullptr,nullptr,nullptr,nullptr,nullptr,&scripts,&service),"Navigation durability composition failed");
            Clock clock;
            std::array clients{client(clock,1,SessionGeneration::initial()),client(clock,2,SessionGeneration::initial())};
            std::array<BoundedMovementMetricSink,2> movementMetrics;
            std::array buffers{RemoteMotionBuffer(movementMetrics[0]),RemoteMotionBuffer(movementMetrics[1])};
            std::array<std::optional<RemoteMotionPose>,2> previous;
            std::array<uint64_t,2> lastTick{};
            std::array<double,2> maxFrameStep{};
            struct Packet { uint64_t arrival; size_t client; std::vector<std::byte> bytes; };
            std::vector<Packet> packets;
            const auto cell=CellId::interior(id<CellSpaceId>(7));
            auto view=[&](uint64_t session,uint64_t tick,const PreparedNativeInventory* candidate=nullptr) {
                return service.projectInventory(reducer.state(),id<SessionId>(session),id<ServerTick>(tick),reducer.canonicalRevision(),candidate).value();
            };
            const auto start=view(1,1).equipment->motions.at(0);
            {
                auto joining = client(clock, 1, SessionGeneration::initial());
                auto baseline = *view(1, 2).equipment;
                auto moving = baseline;
                moving.canonicalRevision = *baseline.canonicalRevision.next();
                moving.motions[0].tick = 2;
                moving.motions[0].position[0] += 1;
                require(joining->receiveLatestWinsEquipmentSnapshot(baseline) == InventoryReplicationReceiveResult::Applied
                    && joining->receiveLatestWinsEquipmentSnapshot(moving) == InventoryReplicationReceiveResult::Applied
                    && joining->receiveLatestWinsEquipmentSnapshot(baseline) == InventoryReplicationReceiveResult::StaleTick
                    && joining->receiveLatestWinsEquipmentSnapshot(moving) == InventoryReplicationReceiveResult::IdenticalDuplicate,
                    "Same-tick join baseline/movement commits were not ordered by canonical revision");
                moving.motions[0].position[0] += 1;
                require(joining->receiveLatestWinsEquipmentSnapshot(moving) == InventoryReplicationReceiveResult::ContradictorySameTick,
                    "Conflicting actor state for the same durable commit was accepted");
            }
            double maxTickMs=0; size_t overruns=0, dropped=0, delivered=0; std::array<float,3> atDisconnect{};
            for (uint64_t frame=2; frame<=660; ++frame)
            {
                const auto tick=frame/2;
                clock.value=frame*16'666'667;
                if (frame%2==0 && tick<=300)
                {
                    const auto began=std::chrono::steady_clock::now();
                    std::optional<ServerCommandProposal> command;
                    if (tick==2)
                    {
                        const auto inventory=view(1,tick).playerInventory.front();
                        require(!inventory.equipment.empty(),"Real inventory composition fixture has no equipped item");
                        const auto slot=inventory.equipment.front();
                        const auto item=std::ranges::find(inventory.stacks,slot.stackId,&CanonicalItemStack::stackId);
                        ClientInventoryTransactionCommand input{id<SessionId>(1),SessionGeneration::initial(),CommandSequence::initial(),id<CommandId>(1),
                            reducer.canonicalRevision(),InventoryTransactionKind::UnequipItem,{},item->prototypeId,item->stackId,1,slot.slot,
                            inventory.revision,{},{},Position3(0,0,0)};
                        command=bind(reducer.state(),input).proposal();
                    }
                    auto prepare=[&]() {
                        Clock ingressClock;
                        ServerCommandIntakeCoordinator intake(ingressClock,observability,ingressClock.now(),id<ServerTick>(tick),IngressOrdinal::initial());
                        if (command) require(intake.submit(*command)==CommandSubmissionResult::Accepted,"Navigation command intake failed");
                        ingressClock.value=tick*33'333'334;
                        auto batches=intake.pump(); require(batches && batches.batches().size()==1,"Navigation tick intake failed");
                        auto pending=reducer.prepareTick(batches.batches().front());
                        require(pending.result() && reducer.stageNativeDoorStep(pending,id<ServerTick>(tick),1.f/30),"Navigation tick staging failed");
                        if (command) require(pending.result().dispositions()[0].disposition()==CommandDisposition::Applied,"Composed inventory command rejected");
                        return pending;
                    };
                    auto pending=prepare();
                    const std::vector before(service.inventoryImage().begin(),service.inventoryImage().end());
                    if (tick==2 || tick==10)
                    {
                        port.reject=true;
                        require(!reducer.commit(std::move(pending)),"Rejected navigation durability acknowledged");
                        require(std::ranges::equal(before,service.inventoryImage()),"Rejected navigation tick leaked movement/inventory");
                        port.reject=false; pending=prepare();
                    }
                    require(reducer.commit(std::move(pending)),"Navigation durable tick failed");
                    if (tick==45)
                    {
                        atDisconnect=view(3-leaving,tick).equipment->motions.at(0).position;
                        auto disconnect=reducer.prepareDisconnect(id<SessionId>(leaving),id<ServerTick>(tick));
                        require(disconnect && reducer.commit(std::move(*disconnect)),"Navigation player disconnect failed");
                        service.synchronizeCells(reducer.state());
                    }
                    for (size_t peer=0; peer<2; ++peer)
                    {
                        if (tick>=45 && peer+1==leaving) continue;
                        const auto motion=view(peer+1,tick).equipment.value();
                        if ((tick+peer*3)%10==0) { ++dropped; continue; }
                        // 100 ms one-way, +/- 33 ms jitter, deterministic 10% loss,
                        // plus occasional reordering; both receivers use production codecs/buffers.
                        packets.push_back({frame+4+(tick*7+peer*3)%5+(tick%23==0 ? 8 : 0),peer,encodeLatestWinsEquipmentSnapshot(motion)});
                    }
                    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
                    maxTickMs=std::max(maxTickMs,ms); overruns+=ms>33.333334;
                    if (tick == 30)
                    {
                        // Restart with a nonempty path, not only at the destination.
                        InventoryHost midway(descriptor, testContentManifest(), *registry, *crypto, service.inventoryImage());
                        midway.service().synchronizeCells(reducer.state());
                        auto original = service.prepareNativeTick(reducer.state(), id<ServerTick>(31), 1.f/30, {});
                        auto restarted = midway.service().prepareNativeTick(reducer.state(), id<ServerTick>(31), 1.f/30, {});
                        std::vector<std::byte> expected, actual;
                        require(original->commit([&](auto bytes) { expected.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; })
                                == CanonicalDurabilityResult::Rejected
                            && restarted->commit([&](auto bytes) { actual.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; })
                                == CanonicalDurabilityResult::Rejected
                            && !expected.empty() && expected == actual,
                            "Mid-path restart changed the next staged actor/inventory tick");
                    }
                }
                for (auto it=packets.begin(); it!=packets.end();)
                {
                    if (it->arrival>frame) { ++it; continue; }
                    const auto decoded=decodeLatestWinsEquipmentSnapshot(it->bytes);
                    require(std::holds_alternative<LatestWinsEquipmentSnapshot>(decoded),"Native motion codec rejected projection");
                    auto& receiver=*clients[it->client];
                    receiver.receiveLatestWinsEquipmentSnapshot(std::get<LatestWinsEquipmentSnapshot>(decoded));
                    const auto& confirmed=receiver.confirmedEquipmentSnapshot();
                    if (confirmed && !confirmed->motions.empty() && confirmed->motions[0].tick>lastTick[it->client])
                    {
                        require(buffers[it->client].observe(confirmed->motions[0],cell,clock.now()),"Native motion interpolation rejected sample");
                        lastTick[it->client]=confirmed->motions[0].tick; ++delivered;
                    }
                    it=packets.erase(it);
                }
                for (size_t peer=0; peer<2; ++peer)
                {
                    if (!buffers[peer].sampleCount() || (frame/2>=45 && peer+1==leaving)) continue;
                    auto pose=buffers[peer].advance(clock.now()); require(bool(pose),"Native interpolation has no pose");
                    if (previous[peer]) maxFrameStep[peer]=std::max(maxFrameStep[peer],std::hypot(pose->x-previous[peer]->x,pose->y-previous[peer]->y)/1024);
                    previous[peer]=pose;
                }
            }
            const auto end=view(3-leaving,300).equipment->motions.at(0);
            require(std::hypot(end.position[0]+550,end.position[1]-70)<16,"Native NPC did not arrive after disconnect");
            require(std::hypot(end.position[0]-atDisconnect[0],end.position[1]-atDisconnect[1])>50,"Disconnect did not exercise continued motion");
            for (size_t peer=0; peer<2; ++peer)
                require(maxFrameStep[peer]<16,"Impaired native interpolation exceeded frame displacement budget");
            const auto& pose=previous[2-leaving];
            require(pose && std::hypot(pose->x/1024-end.position[0],pose->y/1024-end.position[1])<.1,"Surviving client did not converge");
            const auto saved=file->prefix().latest()->nativeInventory();
            InventoryHost recovered(descriptor,testContentManifest(),*registry,*crypto,saved);
            require(std::ranges::equal(saved,recovered.service().inventoryImage()),"Native recovery changed actor/inventory bytes");
            recovered.service().synchronizeCells(reducer.state());
            auto resumed=recovered.service().prepareNativeTick(reducer.state(),id<ServerTick>(301),1.f/30,{});
            require(resumed && resumed->commit([](auto) { return CanonicalDurabilityResult::Rejected; })==CanonicalDurabilityResult::Rejected,
                "Recovered rejected tick failed");
            auto bad=std::vector<std::byte>(saved.begin(),saved.end()); bad.pop_back();
            bool rejected=false;
            try { InventoryHost invalid(descriptor,testContentManifest(),*registry,*crypto,bad); } catch (...) { rejected=true; }
            require(rejected,"Truncated native motion campaign accepted");
            auto malformed=*view(3-leaving,300).equipment; malformed.motions[0].position[0]=std::numeric_limits<float>::quiet_NaN();
            require(std::holds_alternative<InventoryReplicationDecodeError>(decodeLatestWinsEquipmentSnapshot(encodeLatestWinsEquipmentSnapshot(malformed))),
                "Nonfinite native motion accepted");
            require(resumed->commit([](auto) { return CanonicalDurabilityResult::Failed; })==CanonicalDurabilityResult::Failed
                && recovered.service().inventoryImage().empty(),"Uncertain actor durability did not close service");
            std::cout << "native-navigation leaving=" << leaving << " committed=" << port.commits << " delivered=" << delivered << " lost=" << dropped
                << " max-frame-step=" << maxFrameStep[0] << ',' << maxFrameStep[1] << " max-tick-ms=" << maxTickMs << " overruns=" << overruns
                << " hard-snaps=" << movementMetrics[0].summary(MovementMetricKey::HardSnaps).total << ','
                << movementMetrics[1].summary(MovementMetricKey::HardSnaps).total << " end=" << end.position[0] << ',' << end.position[1]
                << " rejection=atomic recovery=exact mid-path-recovery=exact inventory=composed\n";
        }
    }

    void checkTravelerNeighborhood(const std::filesystem::path& scratch, const std::filesystem::path& config,
        const std::filesystem::path& settings)
    {
        require(std::filesystem::create_directory(scratch), "Traveler scratch already exists");
        std::filesystem::create_directory(scratch / "openmw");
        std::filesystem::copy_file(config / "openmw.cfg", scratch / "openmw/openmw.cfg");
        const auto npcId = ESM::RefId::stringRefId("neighborhood_traveler");
        constexpr float y = 100 * 8192.f + 1000;
        {
            const auto directory = config.string();
            const char* arguments[]{"traveler-neighborhood", "--config", directory.c_str()};
            Loadout base(readLoadoutOptions(3, arguments));
            auto npc = *base.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player"));
            npc.mId = npcId; npc.mScript = {}; npc.mInventory.mList.clear();
            std::ofstream stream(scratch / "Traveler.esp", std::ios::binary);
            ESM::ESMWriter out; out.setVersion(); out.setFormatVersion(ESM::DefaultFormatVersion); out.setType(0);
            out.addMaster("Morrowind.esm", 0); out.save(stream);
            out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
            for (int x : {0, -1, 1})
            {
                ESM::Cell cell; cell.blank(); cell.mData.mX = x; cell.mData.mY = 100; cell.updateId();
                out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
                if (x == 0)
                {
                    ESM::CellRef ref; ref.blank(); ref.mRefNum = {1, 0}; ref.mRefID = npcId;
                    ref.mPos = {{150, y, 257}, {0, 0, 0}}; ref.save(out);
                }
                out.endRecord(ESM::Cell::sRecordId);
                ESM::Land land; land.blank(); land.mX = x; land.mY = 100;
                land.mFlags = ESM::Land::Flag_HeightsNormals;
                land.add(ESM::Land::DATA_VHGT);
                land.mLandData->mHeights.fill(256);
                land.mLandData->mMinHeight = land.mLandData->mMaxHeight = 256;
                out.startRecord(ESM::Land::sRecordId, 0); land.save(out); out.endRecord(ESM::Land::sRecordId);
            }
            out.close();
            std::ofstream cfg(scratch / "openmw/openmw.cfg", std::ios::app);
            cfg << "\ndata=" << std::quoted(scratch.generic_string()) << "\ncontent=Traveler.esp\n";
        }
        const auto exterior = [](int x) { return CellId::exterior(id<CellSpaceId>(8), x, 100); };
        const std::array cells{exterior(0), exterior(-1), exterior(1)};
        const std::array spaces{CellSpaceDeclaration{id<CellSpaceId>(8), CellSpaceKind::Exterior}};
        const auto manifest = ContentManifest::create(testContentManifestId(), spaces, cells, id<AppearanceId>(1), testMovementProfile()).value();
        auto observed = players(SessionGeneration::initial(), 1, 2);
        std::vector entities(observed.players().begin(), observed.players().end());
        for (size_t i = 0; i < entities.size(); ++i)
            entities[i] = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(entities[i], id<ServerTick>(1),
                Transform(exterior(i ? -1 : 1), Position3((i ? -100 : 8200) * 1024, int64_t(y) * 1024, 257 * 1024),
                    entities[i].transform().orientation()), LinearVelocity3(0,0,0)));
        observed = std::get<CanonicalServerState>(createCanonicalServerState(entities, observed.activeSessions()));
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState(entities, {}));
        auto crypto = makeProductionCredentialCrypto();
        struct Identities final : PlayerIdentityPersistence
        { bool replace(std::span<const PersistedPlayerIdentity>) noexcept override { return true; } } storage;
        CharacterDerivedState derived; derived.attributes.fill(40); derived.skills.fill(10);
        const auto profile = CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter, CharacterCreationPhase::Complete,
            "Traveler observer", CharacterAppearance{id<RaceRecordId>(1),id<HeadRecordId>(1),id<HairRecordId>(1),CharacterSex::Male},
            CharacterClass{id<ClassRecordId>(1)},id<BirthsignRecordId>(1),derived,{},id<CharacterProfileRevision>(2)).value();
        std::vector<PersistedPlayerIdentity> records;
        for (const auto& entity : entities)
        {
            CredentialDigest digest; digest.bytes.fill(std::byte(entity.playerId().value()));
            records.push_back({{entity.playerId(),entity.entityId(),id<AppearanceId>(1),testContentManifestId()},digest,entity,profile});
        }
        auto registry = std::get<std::unique_ptr<PlayerIdentityRegistry>>(PlayerIdentityRegistry::create(*crypto, storage, records));
        const auto descriptor = [&](size_t cellBudget, size_t steps) {
            auto path = scratch / ("native-" + std::to_string(cellBudget) + "-" + std::to_string(steps) + ".txt");
            std::ofstream out(path);
            out << "native-inventory-20\nmanifest ";
            const auto manifestId = testContentManifestId();
            for (auto byte : manifestId.bytes()) out << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte);
            out << std::dec << "\nconfig \"openmw\"\nplayers 1 2\nactors \"player\" \"player\"\nloot 1 0\n"
                << "exterior 0 100\ndoors auto\ncell exterior:8:0:100\nareas 3\n"
                << "exterior -1 100\ncell exterior:8:-1:100\nexterior 1 100\ncell exterior:8:1:100\n"
                << "npc \"neighborhood_traveler\" " << std::quoted(settings.string())
                << "\ndestination -150 " << int(y) << " 257 120\nprocessing " << cellBudget << ' ' << steps << '\n';
            return path;
        };
        const auto normal = descriptor(3, 2), cellsFull = descriptor(2, 2), workFull = descriptor(3, 1);
        const auto accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
        const auto rejected = [](auto) { return CanonicalDurabilityResult::Rejected; };
        const auto bytes = [](auto& service) { return std::vector(service.inventoryImage().begin(), service.inventoryImage().end()); };
        const auto actorImage = [](const auto& image) {
            auto actor = readActorCampaign({reinterpret_cast<const char*>(image.data()), image.size()}).actor;
            return std::vector(actor.begin(), actor.end());
        };
        InventoryHost host(normal, manifest, *registry, *crypto, {});
        auto& service = host.service(); service.synchronizeCells(empty);
        require(dynamic_cast<InventoryService&>(service).activeAreas() == std::vector<bool>({true,true,true}),
            "Unattended traveler did not retain its processing neighborhood");
        const auto initial = bytes(service);
        {
            std::ifstream input(normal);
            std::string text((std::istreambuf_iterator<char>(input)), {});
            text.replace(text.find("destination -150 "), std::string("destination -150 ").size(), "destination 15000 ");
            const auto pendingPath = scratch / "pending.txt";
            { std::ofstream output(pendingPath); output << text; }
            InventoryHost pending(pendingPath, manifest, *registry, *crypto, {});
            pending.service().synchronizeCells(empty);
            auto tick = pending.service().prepareNativeTick(empty, id<ServerTick>(1), 1.f/30, {});
            require(tick->commit(accepted) == CanonicalDurabilityResult::Committed
                && pending.service().travelDiagnostics()->status == ServerApp::NativeTravelDiagnostics::Status::NoPath
                && !pending.service().travelDiagnostics()->completed, "Unavailable bounded path lost its destination or completed");
            InventoryHost recovered(pendingPath, manifest, *registry, *crypto, pending.service().inventoryImage());
            require(bytes(recovered.service()) == bytes(pending.service())
                && !recovered.service().travelDiagnostics()->completed, "Unavailable path changed across restart");
        }
        {
            auto falling = entities;
            for (auto& player : falling)
                player = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(player, id<ServerTick>(1),
                    player.transform(), LinearVelocity3(0, 0, -1000000)));
            const auto offline = std::get<CanonicalServerState>(createCanonicalServerState(falling, {}));
            NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
            CanonicalCommandReducer reducer(offline, observability, manifest);
            struct Port final : CanonicalDurabilityPort
            {
                CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>&,
                    CanonicalRevision, std::span<const DurableCommandOrder>, const CanonicalInventoryWorld*,
                    const CanonicalCombatWorld*, const CanonicalInteractiveObjectWorld*, const CanonicalActorWorld*,
                    const CanonicalWorldState*, const CanonicalScriptState*, std::span<const std::byte>) noexcept override
                { return CanonicalDurabilityResult::Committed; }
            } port;
            require(reducer.configureDurability(port, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &service),
                "Offline travel reducer composition failed");
            Clock clock;
            ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(2), IngressOrdinal::initial());
            clock.value = 100'000'000;
            const auto pump = intake.pump();
            require(pump && !pump.batches().empty(), "Offline tick missing");
            auto pending = reducer.prepareTick(pump.batches().front());
            require(pending.result() && std::ranges::equal(pending.candidateState().players(), offline.players()),
                "Offline inherited velocity reentered legacy player simulation");
        }
        for (const auto& path : {cellsFull, workFull})
        {
            InventoryHost limited(path, manifest, *registry, *crypto, initial);
            auto& paused = limited.service(); paused.synchronizeCells(empty);
            auto step = paused.prepareNativeTick(empty, id<ServerTick>(1), 1.f/30, {});
            require(step->commit(accepted) == CanonicalDurabilityResult::Committed, "Saturated tick failed");
            using Status = ServerApp::NativeTravelDiagnostics::Status;
            const auto report = *paused.travelDiagnostics();
            require(report.status == (path == cellsFull ? Status::CellCapacity : Status::StepCapacity)
                && !report.completed && report.demandedCells == 3 && actorImage(bytes(paused)) == actorImage(initial),
                "Capacity diagnostic lost destination, completion or partially stepped the NPC");
            InventoryHost resumed(normal, manifest, *registry, *crypto, paused.inventoryImage());
            require(actorImage(bytes(resumed.service())) == actorImage(initial), "Increasing capacity lost the retained trip");
        }
        InventoryHost occupied(normal, manifest, *registry, *crypto, initial);
        occupied.service().synchronizeCells(observed);
        std::unique_ptr<InventoryHost> restarted;
        bool crossed = false, completed = false;
        for (uint64_t tick = 1; tick <= 160; ++tick)
        {
            auto step = service.prepareNativeTick(empty, id<ServerTick>(tick), 1.f/30, {});
            const auto before = bytes(service);
            if (tick == 38)
                require(step->commit(rejected) == CanonicalDurabilityResult::Rejected && bytes(service) == before,
                    "Rejected boundary tick changed the committed trip");
            require(step->commit(accepted) == CanonicalDurabilityResult::Committed, "Unattended neighborhood tick failed");
            auto overlap = occupied.service().prepareNativeTick(observed, id<ServerTick>(tick), 1.f/30, {});
            require(overlap->commit(accepted) == CanonicalDurabilityResult::Committed
                && bytes(occupied.service()) == bytes(service), "Overlapping player and traveler neighborhoods stepped twice");
            if (restarted)
            {
                auto resume = restarted->service().prepareNativeTick(empty, id<ServerTick>(tick), 1.f/30, {});
                require(resume->commit(accepted) == CanonicalDurabilityResult::Committed
                    && bytes(restarted->service()) == bytes(service), "Mid-trip restart changed physics, path or destination");
            }
            if (tick == 25)
            {
                restarted = std::make_unique<InventoryHost>(normal, manifest, *registry, *crypto, service.inventoryImage());
                restarted->service().synchronizeCells(empty);
            }
            const auto report = *service.travelDiagnostics();
            crossed |= report.position[0] < 0;
            require(report.position[2] > 255 && report.position[2] < 260, "Crossing lost stock terrain collision");
            auto a = service.projectInventory(observed, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick));
            auto b = service.projectInventory(observed, id<SessionId>(2), id<ServerTick>(tick), id<CanonicalRevision>(tick));
            require(a && b && a->equipment->actors.size() == 1 && b->equipment->actors.size() == 1
                && a->equipment->motions.size() == 1 && a->equipment->motions == b->equipment->motions,
                "Crossing or overlapping interest duplicated/lost the returning clients' traveler");
            require(!service.projectInventory(empty, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick)),
                "Traveler simulation manufactured an observer");
            if (report.completed)
            {
                completed = true;
                require(crossed && std::abs(report.position[0] + 150) < 16, "Travel completed in the wrong cell");
                service.synchronizeCells(empty);
                require(!dynamic_cast<InventoryService&>(service).activeActorCollisionBodies(), "Completion did not release the neighborhood");
                InventoryHost done(normal, manifest, *registry, *crypto, service.inventoryImage());
                require(done.service().travelDiagnostics()->completed, "Completed restart restarted travel");
                service.synchronizeCells(observed);
                require(bytes(service) == bytes(done.service()), "Reload changed completed travel");
                std::cout << "neighborhood crossing=x:0->-1 completion_tick=" << tick
                    << " cell-capacity=retained step-capacity=retained union=once restart=exact clients=converged\n";
                break;
            }
        }
        require(completed, "Neighborhood traveler did not complete");
    }

    void checkNpcDoors(const std::filesystem::path& scratch, const std::filesystem::path& config,
        const std::filesystem::path& settings, bool avoidance, bool traveler, bool melee, bool combat,
        bool lifecycle, bool spell, bool projectile, bool timed)
    {
        require(std::filesystem::create_directory(scratch), "NPC door scratch already exists");
        writePlacementFixtureModels(scratch);
        writeDoorFixtureModel(scratch);
        std::filesystem::create_directory(scratch / "openmw");
        std::filesystem::copy_file(config / "openmw.cfg", scratch / "openmw" / "openmw.cfg");
        // Synthetic room/placements on the retained real loadout. NPC hull and
        // inventory mechanics come from OpenMW; this is not a published-mod proof.
        {
            const auto directory = config.string();
            const char* arguments[]{"npc-door-test", "--config", directory.c_str()};
            Loadout base(readLoadoutOptions(3, arguments));
            auto npc = *base.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player"));
            npc.mId = ESM::RefId::stringRefId("npc_door_actor"); npc.mScript = {};
            npc.mInventory.mList = {{1, ESM::RefId::stringRefId("common_shirt_01")}};
            if (combat)
            {
                npc.mNpdtType = ESM::NPC::NPC_DEFAULT;
                npc.mNpdt.mSkills[ESM::Skill::refIdToIndex(ESM::Skill::ShortBlade)] = 100;
                npc.mNpdt.mSkills[ESM::Skill::refIdToIndex(ESM::Skill::HandToHand)] = 50;
                npc.mInventory.mList.push_back({1, ESM::RefId::stringRefId("iron shortsword")});
            }
            ESM::Spell restore;
            ESM::Spell mixedRestore;
            ESM::Spell targetRestore;
            ESM::Spell targetDamage;
            ESM::Spell timedResistance;
            ESM::Spell targetResistance;
            ESM::Enchantment rangedEnchantment;
            ESM::Enchantment usedEnchantment;
            ESM::Clothing usedItem;
            if (spell)
            {
                restore.blank();
                restore.mId = ESM::RefId::stringRefId("npc_instant_restore");
                restore.mData.mType = ESM::Spell::ST_Spell;
                restore.mData.mFlags = ESM::Spell::F_Always;
                restore.mData.mCost = 1;
                restore.mEffects.populate({{ESM::MagicEffect::RestoreHealth, {}, {}, ESM::RT_Self, 0, 0, 20, 20}});
                npc.mSpells.mList.push_back(restore.mId);
                mixedRestore.blank();
                mixedRestore.mId = ESM::RefId::stringRefId("npc_mixed_restore");
                mixedRestore.mData.mType = ESM::Spell::ST_Spell;
                mixedRestore.mData.mFlags = ESM::Spell::F_Always;
                mixedRestore.mData.mCost = 1;
                mixedRestore.mEffects.populate({
                    {ESM::MagicEffect::RestoreHealth, {}, {}, ESM::RT_Self, 0, 0, 20, 20},
                    {ESM::MagicEffect::RestoreMagicka, {}, {}, ESM::RT_Self, 0, 0, 1, 1}});
                npc.mSpells.mList.push_back(mixedRestore.mId);
                targetRestore.blank();
                targetRestore.mId = ESM::RefId::stringRefId("npc_target_restore");
                targetRestore.mData.mType = ESM::Spell::ST_Spell;
                targetRestore.mData.mFlags = ESM::Spell::F_Always;
                targetRestore.mData.mCost = 1;
                targetRestore.mEffects.populate({
                    {ESM::MagicEffect::RestoreHealth, {}, {}, ESM::RT_Target, 0, 0, 7, 7}});
                npc.mSpells.mList.push_back(targetRestore.mId);
                if (timed)
                {
                    timedResistance.blank();
                    timedResistance.mId = ESM::RefId::stringRefId("npc_timed_resistance");
                    timedResistance.mData.mType = ESM::Spell::ST_Spell;
                    timedResistance.mData.mFlags = ESM::Spell::F_Always;
                    timedResistance.mData.mCost = 1;
                    timedResistance.mEffects.populate({
                        {ESM::MagicEffect::ResistMagicka, {}, {}, ESM::RT_Self, 0, 1, 100, 100}});
                    npc.mSpells.mList.push_back(timedResistance.mId);
                    targetResistance = timedResistance;
                    targetResistance.mId = ESM::RefId::stringRefId("npc_target_resistance");
                    targetResistance.mEffects.populate({
                        {ESM::MagicEffect::ResistMagicka, {}, {}, ESM::RT_Target, 0, 1, 100, 100}});
                    npc.mSpells.mList.push_back(targetResistance.mId);
                }
                if (projectile)
                {
                    targetDamage.blank();
                    targetDamage.mId = ESM::RefId::stringRefId("npc_target_damage");
                    targetDamage.mData.mType = ESM::Spell::ST_Spell;
                    targetDamage.mData.mFlags = ESM::Spell::F_Always;
                    targetDamage.mData.mCost = 1;
                    targetDamage.mEffects.populate({
                        {ESM::MagicEffect::DamageHealth, {}, {}, ESM::RT_Target, 0, 0, 10, 10}});
                    npc.mSpells.mList.push_back(targetDamage.mId);
                    usedEnchantment.blank();
                    usedEnchantment.mId = ESM::RefId::stringRefId("npc_used_damage");
                    usedEnchantment.mData.mType = ESM::Enchantment::WhenUsed;
                    usedEnchantment.mData.mCost = 2;
                    usedEnchantment.mData.mCharge = 20;
                    usedEnchantment.mEffects.populate({
                        {ESM::MagicEffect::RestoreHealth, {}, {}, ESM::RT_Self, 0, 0, 5, 5},
                        {ESM::MagicEffect::DamageHealth, {}, {}, ESM::RT_Target, 0, 0, 10, 10}});
                    usedItem = *base.store().get<ESM::Clothing>().find(
                        ESM::RefId::stringRefId("common_shirt_01"));
                    usedItem.mId = ESM::RefId::stringRefId("npc_used_shirt");
                    usedItem.mEnchant = usedEnchantment.mId;
                    usedItem.mScript = {};
                    npc.mInventory.mList.push_back({1, usedItem.mId});
                }
                rangedEnchantment.blank();
                rangedEnchantment.mId = ESM::RefId::stringRefId("npc_ranged_enchantment");
                rangedEnchantment.mData.mType = ESM::Enchantment::WhenUsed;
                rangedEnchantment.mData.mCost = 1;
                rangedEnchantment.mData.mCharge = 20;
                rangedEnchantment.mEffects.populate({
                    {ESM::MagicEffect::RestoreFatigue, {}, {}, ESM::RT_Self, 0, 0, 5, 5},
                    {ESM::MagicEffect::RestoreHealth, {}, {}, ESM::RT_Target, 0, 0, 7, 7}});
            }
            std::ofstream stream(scratch / "NpcDoors.esp", std::ios::binary);
            ESM::ESMWriter out; out.setVersion(); out.setFormatVersion(ESM::DefaultFormatVersion); out.setType(0);
            out.addMaster("Morrowind.esm", 0); out.save(stream);
            out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
            if (spell)
            {
                out.startRecord(ESM::Spell::sRecordId, 0); restore.save(out); out.endRecord(ESM::Spell::sRecordId);
                out.startRecord(ESM::Spell::sRecordId, 0); mixedRestore.save(out); out.endRecord(ESM::Spell::sRecordId);
                out.startRecord(ESM::Spell::sRecordId, 0); targetRestore.save(out); out.endRecord(ESM::Spell::sRecordId);
                if (timed)
                {
                    out.startRecord(ESM::Spell::sRecordId, 0);
                    timedResistance.save(out); out.endRecord(ESM::Spell::sRecordId);
                    out.startRecord(ESM::Spell::sRecordId, 0);
                    targetResistance.save(out); out.endRecord(ESM::Spell::sRecordId);
                }
                if (projectile)
                {
                    out.startRecord(ESM::Spell::sRecordId, 0);
                    targetDamage.save(out); out.endRecord(ESM::Spell::sRecordId);
                }
                out.startRecord(ESM::Enchantment::sRecordId, 0);
                rangedEnchantment.save(out); out.endRecord(ESM::Enchantment::sRecordId);
                if (projectile)
                {
                    out.startRecord(ESM::Enchantment::sRecordId, 0);
                    usedEnchantment.save(out); out.endRecord(ESM::Enchantment::sRecordId);
                    out.startRecord(ESM::Clothing::sRecordId, 0);
                    usedItem.save(out); out.endRecord(ESM::Clothing::sRecordId);
                }
            }
            ESM::Static floor; floor.blank(); floor.mId = ESM::RefId::stringRefId("npc_door_floor");
            floor.mModel = "placement-floor.osgt";
            out.startRecord(ESM::Static::sRecordId, 0); floor.save(out); out.endRecord(ESM::Static::sRecordId);
            ESM::Door door; door.blank(); door.mId = ESM::RefId::stringRefId("npc_door"); door.mModel = "npc-door.osgt";
            out.startRecord(ESM::Door::sRecordId, 0); door.save(out); out.endRecord(ESM::Door::sRecordId);
            ESM::Cell cell; cell.blank(); cell.mName = "NPC Door Contact Test";
            cell.mData.mFlags = ESM::Cell::Interior; cell.updateId();
            out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
            uint32_t index = 0;
            for (auto record : {npc.mId, floor.mId, door.mId})
            {
                ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0}; placed.mRefID = record;
                if (record == npc.mId) placed.mPos = {{60, -32, 1}, {0, 0, 0}};
                placed.save(out);
            }
            out.endRecord(ESM::Cell::sRecordId);
            cell.mName = "NPC Door Path Test"; cell.updateId();
            out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
            for (auto record : {npc.mId, floor.mId, door.mId})
            {
                ESM::CellRef placed; placed.blank(); placed.mRefNum = {++index, 0}; placed.mRefID = record;
                if (record == npc.mId) placed.mPos = {{0, -220, 1}, {0, 0, 0}};
                placed.save(out);
            }
            out.endRecord(ESM::Cell::sRecordId); out.close();
            std::ofstream cfg(scratch / "openmw" / "openmw.cfg", std::ios::app);
            cfg << "\ndata=" << std::quoted(scratch.generic_string()) << "\ncontent=NpcDoors.esp\n";
        }
        {
            const auto directory = (scratch / "openmw").string();
            const char* arguments[]{"npc-door-path", "--config", directory.c_str()};
            Loadout loadout(readLoadoutOptions(3, arguments));
            if (spell)
            {
                const auto& content = loadout.store();
                if (timed)
                {
                    const auto& resistance = *content.get<ESM::Spell>().find(
                        ESM::RefId::stringRefId("npc_timed_resistance"));
                    require(prepareInstantEffects(resistance.mEffects, content).has_value(),
                        "Timed resistance record failed effect preparation");
                    require(prepareInstantSpell(resistance, content).has_value(),
                        "Timed resistance record failed spell preparation");
                }
                const auto& enchantment = *content.get<ESM::Enchantment>().find(
                    ESM::RefId::stringRefId("npc_ranged_enchantment"));
                const auto& targetSpell = *content.get<ESM::Spell>().find(
                    ESM::RefId::stringRefId("npc_target_restore"));
                const auto preparedTarget = prepareInstantSpell(targetSpell, content);
                require(preparedTarget && preparedTarget->effects.hasRange(ESM::RT_Target)
                    && !preparedTarget->effects.onlyRange(ESM::RT_Self),
                    "Target spell failed source-neutral range preparation");
                auto damageSpell = targetSpell;
                damageSpell.mEffects.populate({
                    {ESM::MagicEffect::DamageHealth, {}, {}, ESM::RT_Target, 0, 0, 10, 10}});
                const auto preparedDamage = prepareInstantSpell(damageSpell, content);
                require(preparedDamage && preparedDamage->effects.onlyRange(ESM::RT_Target),
                    "Resistible Target damage did not enter the shared effect plan");
                MWMechanics::NpcStats unresisted(content), resisted(content);
                const auto& actorRecord = *content.get<ESM::NPC>().find(
                    ESM::RefId::stringRefId("npc_door_actor"));
                unresisted.initializeExplicitStats(actorRecord, 2.f);
                resisted.initializeExplicitStats(actorRecord, 2.f);
                resisted.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::ResistMagicka),
                    MWMechanics::EffectParam(100.f));
                Misc::Rng::Generator first(123), second(123);
                const auto unresistedHit = applyInstantEffects(preparedDamage->effects,
                    ESM::RT_Target, unresisted, &first, &content);
                const auto resistedHit = applyInstantEffects(preparedDamage->effects,
                    ESM::RT_Target, resisted, &second, &content);
                require(unresistedHit.health < 0 && unresistedHit.health >= -10
                    && resistedHit.health == 0 && first == second,
                    "Target damage did not consume the shared resistance roll or honor resistance");
                auto effects = prepareInstantEffects(enchantment.mEffects, content);
                require(effects && effects->hasRange(ESM::RT_Self) && effects->hasRange(ESM::RT_Target)
                    && !effects->onlyRange(ESM::RT_Self),
                    "Enchantment effects did not prepare through the source-neutral range path");
                auto invalid = enchantment.mEffects;
                invalid.mList.front().mData.mDuration = 1;
                require(!prepareInstantEffects(invalid, content),
                    "Unsupported duration entered the instant effect path");
                MWMechanics::NpcStats target(content);
                target.initializeExplicitStats(*content.get<ESM::NPC>().find(
                    ESM::RefId::stringRefId("npc_door_actor")), 2.f);
                auto health = target.getHealth(); health.setCurrent(health.getCurrent() - 10.f); target.setHealth(health);
                auto fatigue = target.getFatigue(); fatigue.setCurrent(fatigue.getCurrent() - 10.f);
                target.setFatigue(fatigue);
                const auto self = applyInstantEffects(*effects, ESM::RT_Self, target);
                require(self.fatigue == 5.f && self.health == 0.f,
                    "Self range applied the target enchantment effect");
                const auto hit = applyInstantEffects(*effects, ESM::RT_Target, target);
                require(hit.health == 7.f && hit.fatigue == 0.f,
                    "Target range applied the self enchantment effect");
            }
            const auto cell = ESM::RefId::stringRefId("NPC Door Path Test");
            const auto actor = loadout.placedActors(cell).at(0).mIdentity;
            const auto door = loadout.ordinaryDoors(cell, 128).at(0).mIdentity;
            InteriorActorScene scene(loadout, "NPC Door Path Test", actor, "meshes/base_anim.nif", "meshes/base_animkna.nif");
            const std::array ids{door}; scene.bindDoors(ids, avoidance); scene.enableNavigation(settings.string());
            scene.travelTo({0, -40, 1});
            const auto initial = scene.image();
            const std::array closed{ActorSceneDoor{door, 0}}, opened{ActorSceneDoor{door, osg::PIf / 2}};
            for (int tick = 0; tick < 60; ++tick)
            {
                auto step = scene.prepareNavigation(120, closed); scene.install(*step);
            }
            require(scene.snapshot().mPosition[1] > -65, "Closed door obstructed a path south of its geometry");
            auto restored = scene.prepareRestore(initial, opened); scene.install(*restored);
            bool contacted = false;
            for (int tick = 0; tick < 60; ++tick)
            {
                const auto before = scene.image();
                auto step = scene.prepareNavigation(120, opened);
                require(scene.image() == before, "Door collision preparation mutated the NPC frame");
                scene.install(*step);
                const auto snapshot = scene.snapshot();
                contacted |= std::ranges::find(snapshot.mContacts, door) != snapshot.mContacts.end();
            }
            require((avoidance || contacted) && scene.snapshot().mPosition[1] < -120,
                "NPC passed through the committed open door");
            if (avoidance)
            {
                // A rejected candidate must not leave a query using staged
                // geometry, even though the derived navigator cache was warmed.
                auto reset = scene.prepareRestore(initial, closed); scene.install(*reset);
                const auto closedPath = scene.pathTo({0, -40, 1});
                auto rejected = scene.prepareNavigation(120, opened);
                require(scene.pathTo({0, -40, 1}) == closedPath && scene.image() == initial,
                    "Rejected door navigation leaked into committed queries");
                auto rotated = scene.prepareRestore(initial, opened); scene.install(*rotated);
                const auto openPath = scene.pathTo({100, -40, 1});
                auto unrotated = scene.prepareRestore(initial, closed); scene.install(*unrotated);
                require(openPath != scene.pathTo({100, -40, 1}), "Door rotation did not change navigation geometry");
                const std::array moving{ActorSceneDoor{door, 0, true, true}};
                for (int tick = 0; tick < 40; ++tick)
                {
                    auto pending = scene.prepareNavigation(.01f, moving); scene.install(*pending);
                }
                const auto stuck = scene.image();
                require(!std::equal(stuck.end()-8, stuck.end(), initial.end()-8),
                    "Stuck avoidance did not advance its durable random stream");
                auto future = scene.prepareNavigation(.01f, moving);
                auto replay = scene.prepareRestore(stuck, moving); scene.install(*replay);
                auto repeated = scene.prepareNavigation(.01f, moving);
                require(std::ranges::equal(future->image(), repeated->image()), "Avoidance random/timer recovery diverged");
                if (traveler)
                {
                    const std::vector expected(future->image().begin(), future->image().end());
                    future.reset(); repeated.reset(); replay.reset();
                    const auto position = scene.snapshot().mPosition;
                    bool outside = false;
                    try { scene.travelTo({10000000, 10000000, 1}); }
                    catch (const std::invalid_argument&) { outside = true; }
                    require(outside && scene.image() == stuck, "Out-of-range travel discarded the retained destination");
                    scene.unload();
                    require(!scene.loaded() && scene.bodyCount() == 0 && scene.image() == stuck
                        && scene.snapshot().mPosition == position && !scene.arrived(),
                        "Unloading lost the active travel image or retained collision bodies");
                    InteriorActorScene fresh(loadout, "NPC Door Path Test", actor,
                        "meshes/base_anim.nif", "meshes/base_animkna.nif");
                    fresh.bindDoors(ids, true); fresh.enableNavigation(settings.string());
                    fresh.travelTo({20, -40, 1});
                    bool mismatch = false;
                    try { scene.reload(fresh); } catch (const std::invalid_argument&) { mismatch = true; }
                    require(mismatch && !scene.loaded() && scene.image() == stuck,
                        "Mismatched reload changed the dormant destination");
                    fresh.travelTo({0, -40, 1}); scene.reload(fresh);
                    require(scene.loaded() && scene.bodyCount() > 0 && scene.image() == stuck,
                        "Reload did not restore exact travel state");
                    auto next = scene.prepareNavigation(.01f, moving);
                    require(std::ranges::equal(next->image(), expected),
                        "Unload/reload changed the next avoidance, RNG or physics step");
                }
                for (size_t tail : {size_t(8), size_t(16), size_t(48), size_t(56)})
                {
                    auto bad = stuck;
                    std::fill(bad.end()-tail, bad.end()-tail+8, char(-1));
                    bool invalid = false;
                    try { (void)scene.prepareRestore(bad, moving); } catch (const std::invalid_argument&) { invalid = true; }
                    require(invalid && scene.image() == stuck, "Invalid avoidance image changed committed state");
                }
            }
            const auto before = scene.image();
            const std::array invalid{ActorSceneDoor{door, std::numeric_limits<float>::quiet_NaN()}};
            bool rejected = false;
            try { (void)scene.prepareNavigation(120, invalid); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && scene.image() == before, "Invalid door angle mutated the NPC scene");
            std::cout << "npc-door collision: same path clear when closed, obstructed when open; invalid angle atomic\n";
        }
        auto crypto = makeProductionCredentialCrypto(); require(bool(crypto), "NPC door crypto unavailable");
        struct Identities final : PlayerIdentityPersistence
        { bool replace(std::span<const PersistedPlayerIdentity>) noexcept override { return true; } } storage;
        CharacterDerivedState derived; derived.attributes.fill(40); derived.skills.fill(10);
        const auto profile = CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter, CharacterCreationPhase::Complete,
            "Door participant", CharacterAppearance{id<RaceRecordId>(1),id<HeadRecordId>(1),id<HairRecordId>(1),CharacterSex::Male},
            CharacterClass{id<ClassRecordId>(1)},id<BirthsignRecordId>(1),derived,{},id<CharacterProfileRevision>(2)).value();
        auto authority = players(SessionGeneration::initial(), 1, 2);
        std::vector<PersistedPlayerIdentity> records;
        for (uint64_t i : {1, 2})
        {
            CredentialDigest digest; digest.bytes.fill(std::byte(i));
            records.push_back({{id<PlayerId>(i),id<EntityId>(i == 1 ? 111 : 222),id<AppearanceId>(1),testContentManifestId()},
                digest, *authority.findPlayer(id<PlayerId>(i)), profile});
        }
        auto registry = std::get<std::unique_ptr<PlayerIdentityRegistry>>(PlayerIdentityRegistry::create(*crypto, storage, records));
        const auto descriptor = scratch / "native.txt";
        {
            std::ofstream out(descriptor); out << (timed ? "native-inventory-29\nmanifest "
                : projectile ? "native-inventory-28\nmanifest "
                : spell ? "native-inventory-26\nmanifest "
                : lifecycle ? "native-inventory-25\nmanifest "
                : combat ? "native-inventory-24\nmanifest "
                : melee ? "native-inventory-22\nmanifest "
                : traveler ? "native-inventory-19\nmanifest "
                : avoidance ? "native-inventory-18\nmanifest " : "native-inventory-17\nmanifest ");
            for (auto byte : testContentManifestId().bytes()) out << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte);
            out << "\nconfig \"openmw\"\nplayers 1 2\nactors \"npc_door_actor\" \"npc_door_actor\"\nloot 1 0\n"
                << "interior \"NPC Door Contact Test\"\ndoors auto\ncell interior:7\nareas 1\n"
                << "npc \"npc_door_actor\" " << std::quoted(settings.string())
                << (melee ? "\ndestination 60 -32 1 120\n" : "\ndestination 60 -240 1 120\n");
            if (melee) out << "processing 1 2\nmelee \"weapononehand\" \"chop\" 1\n";
            if (lifecycle) out << "respawn 3\n";
        }
        InventoryHost host(descriptor, testContentManifest(), *registry, *crypto, {});
        require(host.environment() != nullptr, "V17 lost the native time/weather owner");
        auto& service = host.service(); service.synchronizeCells(authority);
        if (melee)
        {
            if (combat)
                require(dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition().value_or(0) > 0,
                    "V23 did not bind the selected NPC's equipped weapon condition");
            const auto image = [&] { return std::vector(service.inventoryImage().begin(), service.inventoryImage().end()); };
            const auto initial = image();
            const auto before = readActorCampaign({reinterpret_cast<const char*>(initial.data()), initial.size()});
            if (combat)
                require(before.combat && before.combat->rng >= 1
                    && std::ranges::all_of(before.combat->actors,
                        [](const auto& stats) { return stats[0][0] > 0; }),
                    "V23 did not stage both players' and the NPC's OpenMW stats and combat RNG");
            require(before.melee && before.melee->identity.find("meshes/xbase_anim.kf") != std::string::npos
                && before.melee->state.mPhase == MeleeAnimation::Phase::WindUp,
                "Bound melee source or initial swing was not saved with the native actor");
            const NativeInventoryCommit rejected = [](auto) { return CanonicalDurabilityResult::Rejected; };
            const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
            auto first = service.prepareNativeTick(authority, id<ServerTick>(1), 1.f/30, {});
            require(first->commit(rejected) == CanonicalDurabilityResult::Rejected && image() == initial,
                "Rejected actor tick advanced the bound swing");
            if (combat)
                require(dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition().value_or(0) > 0,
                    "Rejected tick changed equipped weapon condition");
            require(first->commit(accepted) == CanonicalDurabilityResult::Committed,
                "Retried bound swing tick did not commit");
            const auto committed = image();
            const auto after = readActorCampaign({reinterpret_cast<const char*>(committed.data()), committed.size()});
            require(after.melee && after.melee->state.mTime > before.melee->state.mTime
                && after.melee->identity == before.melee->identity
                && !after.melee->state.mReleased && after.melee->target == 0 && !after.melee->contact,
                "Committed actor tick did not advance the durable bound swing");
            if (combat) require(after.combat == before.combat,
                "Ordinary motion changed combat stats or RNG before a hit");
            InventoryHost restart(descriptor, testContentManifest(), *registry, *crypto, committed);
            auto& resumed = restart.service(); resumed.synchronizeCells(authority);
            require(std::ranges::equal(committed, resumed.inventoryImage()),
                "Bound swing recovery changed the committed image");
            if (combat)
                require(dynamic_cast<InventoryService&>(resumed).selectedNpcWeaponCondition()
                    == dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition(),
                    "Restart changed the equipped weapon condition");
            auto wrongResource = committed;
            wrongResource.at(64) ^= std::byte{1}; // First identity byte after the bounded V22 header.
            bool invalid = false;
            try { InventoryHost mismatch(descriptor, testContentManifest(), *registry, *crypto, wrongResource); }
            catch (const std::invalid_argument&) { invalid = true; }
            require(invalid && std::ranges::equal(committed, resumed.inventoryImage()),
                "Changed saved KF identity passed recovery or changed the live campaign");
            if (combat)
            {
                auto corrupt = committed;
                const auto parts = readActorCampaign({reinterpret_cast<const char*>(corrupt.data()), corrupt.size()});
                const auto offset = lifecycle ? size_t(56 + 8 + parts.melee->identity.size() + 7 * 8)
                    : size_t(parts.inventory.data() - reinterpret_cast<const char*>(corrupt.data()))
                        - (1 + 3 * ActorCampaignCombat::StatCount * 5) * 8;
                std::fill_n(corrupt.begin() + offset, 8, std::byte{});
                bool invalidRng = false;
                try { InventoryHost bad(descriptor, testContentManifest(), *registry, *crypto, corrupt); }
                catch (const std::invalid_argument&) { invalidRng = true; }
                require(invalidRng && std::ranges::equal(committed, resumed.inventoryImage()),
                    "Invalid combat RNG installed during restart");
                corrupt = committed;
                const std::array<std::byte, 8> nan{std::byte{0}, std::byte{0}, std::byte{0xc0},
                    std::byte{0x7f}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
                std::copy(nan.begin(), nan.end(), corrupt.begin() + offset + 8);
                bool invalidStat = false;
                try { InventoryHost bad(descriptor, testContentManifest(), *registry, *crypto, corrupt); }
                catch (const std::invalid_argument&) { invalidStat = true; }
                require(invalidStat && std::ranges::equal(committed, resumed.inventoryImage()),
                    "Nonfinite combat stat installed during restart");
            }
            auto originalTick = service.prepareNativeTick(authority, id<ServerTick>(2), 1.f/30, {});
            auto resumedTick = resumed.prepareNativeTick(authority, id<ServerTick>(2), 1.f/30, {});
            std::vector<std::byte> originalCandidate, resumedCandidate;
            originalTick->commit([&](auto bytes) { originalCandidate.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
            resumedTick->commit([&](auto bytes) { resumedCandidate.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
            require(!originalCandidate.empty() && originalCandidate == resumedCandidate,
                "Restart changed the next bound swing tick");
            bool hit = false;
            bool composedHit = false;
            uint64_t releaseTick = 0;
            std::vector<std::byte> releaseImage;
            for (uint64_t time = 2; time <= 64 && !hit; ++time)
            {
                auto pending = service.prepareNativeTick(authority, id<ServerTick>(time), 1.f/30, {});
                const auto beforeHit = image();
                const auto prior = readActorCampaign({reinterpret_cast<const char*>(beforeHit.data()), beforeHit.size()});
                const auto conditionBefore = combat
                    ? dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition() : std::optional<int>{};
                std::vector<std::byte> proposal;
                size_t rejectedWrites = 0;
                require(pending->commit([&](auto bytes) { ++rejectedWrites; proposal.assign(bytes.begin(), bytes.end());
                    return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                    && rejectedWrites == 1 && image() == beforeHit,
                    "Rejected melee contact tick wrote more than once or mutated the campaign");
                if (combat) require(dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition() == conditionBefore,
                    "Rejected hit-key tick changed the equipped weapon condition");
                auto candidate = readActorCampaign({reinterpret_cast<const char*>(proposal.data()), proposal.size()});
                hit = candidate.melee && candidate.melee->state.mHit;
                if (combat && hit && candidate.melee->contact)
                {
                    const auto observed = service.projectInventory(authority, id<SessionId>(1),
                        id<ServerTick>(time), id<CanonicalRevision>(time));
                    require(observed && !observed->playerInventory.front().equipment.empty(),
                        "Hit-key inventory fixture has no equipped player item");
                    const auto& inventory = observed->playerInventory.front();
                    const auto slot = inventory.equipment.front();
                    const auto item = std::ranges::find(inventory.stacks, slot.stackId, &CanonicalItemStack::stackId);
                    require(item != inventory.stacks.end(), "Hit-key equipped item missing from player inventory");
                    ClientInventoryTransactionCommand input{id<SessionId>(1),SessionGeneration::initial(),
                        CommandSequence::initial(),id<CommandId>(time),id<CanonicalRevision>(time),
                        InventoryTransactionKind::UnequipItem,{},item->prototypeId,item->stackId,1,slot.slot,
                        inventory.revision,{},{},Position3(0,0,0)};
                    auto intent = service.prepareInventory(authority, bind(authority, input).proposal());
                    require(bool(intent), "Simultaneous player inventory intent rejected before hit composition");
                    pending = service.prepareNativeTick(authority, id<ServerTick>(time), 1.f/30, std::move(intent));
                    proposal.clear();
                    require(pending->commit([&](auto bytes) { proposal.assign(bytes.begin(), bytes.end());
                        return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                        && image() == beforeHit, "Rejected composed hit/inventory tick leaked state");
                    candidate = readActorCampaign({reinterpret_cast<const char*>(proposal.data()), proposal.size()});
                    require(candidate.melee && candidate.melee->state.mHit && candidate.melee->contact
                        && candidate.combat && candidate.combat->rng != prior.combat->rng,
                        "Player inventory command displaced the KF hit resolution");
                    const auto installedView = service.projectInventory(authority, id<SessionId>(1),
                        id<ServerTick>(time), id<CanonicalRevision>(time));
                    const auto stagedView = service.projectInventory(authority, id<SessionId>(1),
                        id<ServerTick>(time), id<CanonicalRevision>(time), pending.get());
                    require(pending->changesInventory() && installedView && stagedView,
                        "Composed hit-key candidate lost staged inventory projection");
                    require(stagedView->playerInventory.front().revision.value()
                            == installedView->playerInventory.front().revision.value() + 2,
                        "Composed hit-key candidate lost second inventory revision");
                    require(std::ranges::none_of(stagedView->playerInventory.front().equipment,
                        [&](const auto& equipped) { return equipped.slot == slot.slot; }),
                        "Composed hit-key candidate lost player unequip intent");
                    const auto hitEvents = service.projectCombatEvents(authority, id<SessionId>(2),
                        id<ServerTick>(time), id<CanonicalRevision>(time), pending.get());
                    require(hitEvents && hitEvents->actorEvents().size() == 1
                        && hitEvents->actorEvents()[0].targetPlayerId == id<PlayerId>(1)
                        && hitEvents->actorEvents()[0].hit
                        && hitEvents->actorEvents()[0].damage > 0,
                        "Composed hit-key candidate omitted the reliable actor event");
                    composedHit = true;
                }
                std::vector<std::byte> durable;
                size_t acceptedWrites = 0;
                require(pending->commit([&](auto bytes) { ++acceptedWrites; durable.assign(bytes.begin(), bytes.end());
                    return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed
                    && acceptedWrites == 1,
                    "Retried melee contact tick did not commit");
                require(durable == proposal && image() == durable,
                    "Retried hit-key tick did not install its single durable candidate");
                if (combat && hit && candidate.melee->contact)
                {
                    require(prior.combat && candidate.combat
                        && candidate.combat->rng != prior.combat->rng
                        && candidate.combat->actors[2][10][2] < prior.combat->actors[2][10][2]
                        && candidate.combat->actors[0][8][2] < prior.combat->actors[0][8][2]
                        && dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition()
                            == *conditionBefore - 1,
                        "KF hit did not atomically resolve RNG, fatigue, player health and weapon wear");
                    InventoryHost hitRestart(descriptor, testContentManifest(), *registry, *crypto, durable);
                    require(std::ranges::equal(durable, hitRestart.service().inventoryImage())
                        && dynamic_cast<InventoryService&>(hitRestart.service()).selectedNpcWeaponCondition()
                            == dynamic_cast<InventoryService&>(service).selectedNpcWeaponCondition(),
                        "Hit-key restart changed the resolved tick or weapon condition");
                }
                if (!releaseTick && candidate.melee && candidate.melee->state.mReleased && !hit)
                { releaseTick = time; releaseImage = image(); }
            }
            const auto contactImage = image();
            const auto contacted = readActorCampaign({reinterpret_cast<const char*>(contactImage.data()), contactImage.size()});
            require(hit && (!combat || composedHit) && contacted.melee && contacted.melee->contact && contacted.melee->target == 1,
                "Server contact was not recorded at the bound KF hit key");
            require(releaseTick && !releaseImage.empty(), "Server attack did not retain a release-before-hit state");
            if (spell)
            {
                require(contacted.combat && contacted.combat->actors[0][8][2] < contacted.combat->actors[0][8][0],
                    "Instant spell fixture did not injure its caster");
                const uint64_t castTick = contacted.tick + 1;
                const uint64_t sourceId = [] {
                    uint64_t value = 14695981039346656037ull;
                    for (unsigned char c : std::string_view("npc_instant_restore"))
                        value = (value ^ c) * 1099511628211ull;
                    return value;
                }();
                ClientMagicUseCommand use{id<SessionId>(1), SessionGeneration::initial(),
                    CommandSequence::initial(), id<CommandId>(castTick), id<CanonicalRevision>(castTick),
                    MagicUseSourceKind::Spell, sourceId, MagicUseTargetKind::Self, 0,
                    id<ServerTick>(castTick), CombatRevision::initial(), CombatRevision::initial(),
                    InventoryRevision::initial()};
                const auto* caster = authority.findPlayer(id<PlayerId>(1));
                const auto proposal = [&](const ClientMagicUseCommand& input) {
                    return ServerCommandProposal(id<SessionId>(1), SessionGeneration::initial(),
                        input.commandSequence, input.commandId, input.observedCanonicalRevision,
                        EntityPrecondition(caster->entityId(), caster->entityRevision(), caster->authorityEpoch()),
                        MagicUseCommandProposal(input));
                };
                if (timed)
                {
                    InventoryHost timedHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                    auto& timedService = timedHost.service(); timedService.synchronizeCells(authority);
                    auto resistUse = use;
                    resistUse.sourceId = [] {
                        uint64_t value = 14695981039346656037ull;
                        for (unsigned char c : std::string_view("npc_timed_resistance"))
                            value = (value ^ c) * 1099511628211ull;
                        return value;
                    }();
                    auto prepared = timedService.prepareMagicUse(authority, proposal(resistUse), id<ServerTick>(castTick));
                    require(bool(prepared), "Known timed Resist Magicka spell rejected");
                    auto cast = timedService.prepareNativeTick(authority, id<ServerTick>(castTick), 1.f/30,
                        std::move(prepared));
                    std::vector<std::byte> timedImage;
                    require(cast && cast->commit([&](auto bytes) {
                        timedImage.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected;
                    }) == CanonicalDurabilityResult::Rejected
                        && std::ranges::equal(timedService.inventoryImage(), contactImage),
                        "Rejected timed cast installed an effect");
                    auto staged = readActorCampaign({reinterpret_cast<const char*>(timedImage.data()), timedImage.size()});
                    require(staged.timedEffects.size() == 1 && staged.timedEffects.front().actor == 0
                        && staged.timedEffects.front().magnitude == 100.f
                        && staged.timedEffects.front().expiresTick == castTick + 30,
                        "Timed resistance did not stage with the cast tick");
                    require(cast->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Timed resistance cast did not commit");
                    InventoryHost timedRestart(descriptor, testContentManifest(), *registry, *crypto, timedImage);
                    auto& restored = timedRestart.service(); restored.synchronizeCells(authority);
                    const auto restoredState = readActorCampaign({reinterpret_cast<const char*>(
                        restored.inventoryImage().data()), restored.inventoryImage().size()});
                    require(restoredState.timedEffects == staged.timedEffects,
                        "Restart lost active Resist Magicka magnitude or deadline");
                    InventoryHost offlineHost(descriptor, testContentManifest(), *registry, *crypto, timedImage);
                    const auto offlinePlayers = std::get<CanonicalServerState>(createCanonicalServerState(
                        std::vector(authority.players().begin(), authority.players().end()), {}));
                    auto& offlineService = offlineHost.service(); offlineService.synchronizeCells(offlinePlayers);
                    auto paused = offlineService.prepareNativeTick(offlinePlayers,
                        id<ServerTick>(castTick + 30), 1.f/30, {});
                    require(paused && paused->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Offline timed resistance pause did not commit");
                    const auto pausedState = readActorCampaign({reinterpret_cast<const char*>(
                        offlineService.inventoryImage().data()), offlineService.inventoryImage().size()});
                    require(pausedState.timedEffects.size() == 1
                        && pausedState.timedEffects.front().expiresTick == castTick + 60,
                        "Offline player resistance advanced instead of freezing");
                    auto expiry = restored.prepareNativeTick(authority, id<ServerTick>(castTick + 30), 1.f/30, {});
                    std::vector<std::byte> expiredImage;
                    require(expiry && expiry->commit([&](auto bytes) {
                        expiredImage.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected;
                    }) == CanonicalDurabilityResult::Rejected
                        && readActorCampaign({reinterpret_cast<const char*>(expiredImage.data()),
                            expiredImage.size()}).timedEffects.empty()
                        && expiry->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Timed resistance did not expire atomically on its deadline");
                }
                auto invalidUse = use; invalidUse.sourceId ^= 1;
                require(!service.prepareMagicUse(authority, proposal(invalidUse), id<ServerTick>(castTick)),
                    "Unknown spell ID entered native combat");
                invalidUse = use; invalidUse.targetKind = MagicUseTargetKind::Actor;
                invalidUse.targetId = service.projectInventory(authority, id<SessionId>(1),
                    id<ServerTick>(castTick), id<CanonicalRevision>(castTick))->equipment->motions.front().placement;
                require(!service.prepareMagicUse(authority, proposal(invalidUse), id<ServerTick>(castTick)),
                    "Unsupported target escaped the bounded spell slice");
                invalidUse = use;
                invalidUse.sourceId = [] {
                    uint64_t value = 14695981039346656037ull;
                    for (unsigned char c : std::string_view("npc_target_restore"))
                        value = (value ^ c) * 1099511628211ull;
                    return value;
                }();
                require(!service.prepareMagicUse(authority, proposal(invalidUse), id<ServerTick>(castTick)),
                    "Prepared Target effect was applied through a Self cast without projectile contact");
                auto preparedUse = service.prepareMagicUse(authority, proposal(use), id<ServerTick>(castTick));
                require(bool(preparedUse), "Known self Restore Health spell rejected");
                auto cast = service.prepareNativeTick(authority, id<ServerTick>(castTick), 1.f/30,
                    std::move(preparedUse));
                const auto prior = image();
                std::vector<std::byte> candidate;
                require(cast && cast->commit([&](auto bytes) { candidate.assign(bytes.begin(), bytes.end());
                    return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                    && image() == prior, "Rejected spell cast changed the campaign");
                const auto next = readActorCampaign({reinterpret_cast<const char*>(candidate.data()), candidate.size()});
                require(next.combat && next.combat->actors[0][8][2] > contacted.combat->actors[0][8][2]
                    && next.combat->actors[0][9][2] == contacted.combat->actors[0][9][2] - 1
                    && next.combat->rng != contacted.combat->rng,
                    "OpenMW instant Restore Health did not share the durable magicka and health result");
                const auto aliceEvent = service.projectCombatEvents(authority, id<SessionId>(1),
                    id<ServerTick>(castTick), id<CanonicalRevision>(castTick), cast.get());
                const auto bobEvent = service.projectCombatEvents(authority, id<SessionId>(2),
                    id<ServerTick>(castTick), id<CanonicalRevision>(castTick), cast.get());
                require(aliceEvent && bobEvent && aliceEvent->magicEvents().size() == 1
                    && std::ranges::equal(aliceEvent->magicEvents(), bobEvent->magicEvents())
                    && aliceEvent->magicEvents().front().castSucceeded,
                    "Instant spell event did not reach both clients");
                require(cast->commit(accepted) == CanonicalDurabilityResult::Committed && image() == candidate,
                    "Instant spell did not install its one durable candidate");
                InventoryHost restarted(descriptor, testContentManifest(), *registry, *crypto, candidate);
                const auto reconnected = players(id<SessionGeneration>(2), 1, 2);
                restarted.service().synchronizeCells(reconnected);
                const auto alice = restarted.service().projectCombat(reconnected, id<SessionId>(1),
                    id<ServerTick>(castTick), id<CanonicalRevision>(castTick));
                const auto bob = restarted.service().projectCombat(reconnected, id<SessionId>(2),
                    id<ServerTick>(castTick), id<CanonicalRevision>(castTick));
                require(alice && bob && alice->selfHealth() == bob->players().front().health
                    && alice->selfHealth() == next.combat->actors[0][8][2]
                    && alice->selfMagicka() == next.combat->actors[0][9][2],
                    "Restart/reconnect diverged on the server-owned spell result");
                InventoryHost routedHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                auto& routedService = routedHost.service(); routedService.synchronizeCells(authority);
                NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
                CanonicalCommandReducer reducer(authority, observability, testContentManifest());
                const auto catalog = ServerScriptStateCatalog::create({}).value();
                auto scripts = CanonicalScriptState::initial(catalog).value();
                std::array<std::byte, 32> configuration{}; configuration[0] = std::byte{26};
                const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
                    ServerConfigurationId::fromBytes(configuration).value(), {}, catalog, {}).value();
                auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                    ServerApp::CanonicalPersistenceFile::open(scratch / "routed-spell.bin", identity));
                require(reducer.configureDurability(*file, nullptr, nullptr, nullptr, nullptr, nullptr,
                    &scripts, &routedService), "Native spell reducer durability wiring failed");
                auto routedUse = use;
                routedUse.observedCanonicalRevision = reducer.canonicalRevision();
                const auto routed = proposal(routedUse);
                Clock clock;
                ServerCommandIntakeCoordinator intake(clock, observability, MonotonicInstant::fromNanoseconds(0),
                    id<ServerTick>(castTick), IngressOrdinal::initial());
                require(intake.submit(routed) == CommandSubmissionResult::Accepted,
                    "Authenticated spell intake failed");
                clock.value = castTick * 33'333'334;
                auto batch = intake.pump();
                require(batch && batch.batches().size() == 1, "Spell intake produced no tick");
                auto pending = reducer.prepareTick(batch.batches().front());
                require(pending.result() && pending.result().dispositions()[0].disposition() == CommandDisposition::Applied
                    && reducer.stageNativeDoorStep(pending, id<ServerTick>(castTick), 1.f/30)
                    && reducer.commit(std::move(pending)), "Authenticated spell did not commit through the server reducer");
                const auto routedState = readActorCampaign({reinterpret_cast<const char*>(routedService.inventoryImage().data()),
                    routedService.inventoryImage().size()});
                require(routedState.combat && routedState.combat->actors[0][8][2] == next.combat->actors[0][8][2]
                    && routedState.combat->actors[0][9][2] == next.combat->actors[0][9][2],
                    "Server reducer routed spell to a different combat writer");
                ServerCommandIntakeCoordinator duplicateIntake(clock, observability,
                    MonotonicInstant::fromNanoseconds(0), id<ServerTick>(castTick + 1), IngressOrdinal::initial());
                require(duplicateIntake.submit(routed) == CommandSubmissionResult::Accepted,
                    "Duplicate spell intake failed");
                clock.value = (castTick + 1) * 33'333'334;
                auto duplicateBatch = duplicateIntake.pump();
                require(duplicateBatch && duplicateBatch.batches().size() == 1,
                    "Duplicate spell intake produced no tick");
                auto duplicate = reducer.prepareTick(duplicateBatch.batches().front());
                const auto disposition = duplicate.result().dispositions()[0].disposition();
                require((disposition == CommandDisposition::DuplicateCommandId
                        || disposition == CommandDisposition::AlreadyFinalized)
                    && reducer.stageNativeDoorStep(duplicate, id<ServerTick>(castTick + 1), 1.f/30)
                    && reducer.commit(std::move(duplicate)), "Duplicate spell request escaped its receipt");
                const auto deduplicated = readActorCampaign({reinterpret_cast<const char*>(routedService.inventoryImage().data()),
                    routedService.inventoryImage().size()});
                require(deduplicated.combat && deduplicated.combat->actors[0][9][2] == routedState.combat->actors[0][9][2],
                    "Duplicate spell spent magicka twice");
                InventoryHost mixedHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                auto& mixedService = mixedHost.service(); mixedService.synchronizeCells(authority);
                auto mixedUse = use;
                mixedUse.sourceId = [] {
                    uint64_t value = 14695981039346656037ull;
                    for (unsigned char c : std::string_view("npc_mixed_restore"))
                        value = (value ^ c) * 1099511628211ull;
                    return value;
                }();
                auto mixedPrepared = mixedService.prepareMagicUse(authority, proposal(mixedUse), id<ServerTick>(castTick));
                require(bool(mixedPrepared), "Known mixed instant effects did not enter the shared cast path");
                auto mixedCast = mixedService.prepareNativeTick(authority, id<ServerTick>(castTick), 1.f/30,
                    std::move(mixedPrepared));
                require(mixedCast && mixedCast->commit(accepted) == CanonicalDurabilityResult::Committed,
                    "Mixed instant effects did not commit atomically");
                const auto mixedImage = mixedService.inventoryImage();
                const auto mixedState = readActorCampaign({reinterpret_cast<const char*>(mixedImage.data()), mixedImage.size()});
                require(mixedState.combat && mixedState.combat->actors[0][8][2] > contacted.combat->actors[0][8][2]
                    && mixedState.combat->actors[0][9][2] == contacted.combat->actors[0][9][2],
                    "Mixed instant effects failed to combine healing with net magicka cost");
                InventoryHost mixedRestart(descriptor, testContentManifest(), *registry, *crypto, mixedImage);
                require(std::ranges::equal(mixedRestart.service().inventoryImage(), mixedImage),
                    "Mixed instant effect state changed on restart");
                if (projectile)
                {
                    auto targetEntities = std::vector(authority.players().begin(), authority.players().end());
                    const auto& originalCaster = targetEntities.front();
                    targetEntities.front() = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(
                        originalCaster, id<ServerTick>(1), Transform(originalCaster.transform().cell(),
                            Position3(60 * 1024, -100 * 1024, 1024), originalCaster.transform().orientation()),
                        LinearVelocity3(0, 0, 0)));
                    const auto targetPlayers = std::get<CanonicalServerState>(createCanonicalServerState(
                        targetEntities, authority.activeSessions()));
                    InventoryHost targetHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                    auto& targetService = targetHost.service(); targetService.synchronizeCells(targetPlayers);
                    auto targetUse = use;
                    targetUse.sourceId = [] {
                        uint64_t value = 14695981039346656037ull;
                        for (unsigned char c : std::string_view("npc_target_damage"))
                            value = (value ^ c) * 1099511628211ull;
                        return value;
                    }();
                    targetUse.targetKind = MagicUseTargetKind::Actor;
                    targetUse.targetId = targetService.projectInventory(targetPlayers, id<SessionId>(1),
                        id<ServerTick>(castTick), id<CanonicalRevision>(castTick))->equipment->motions.front().placement;
                    if (timed)
                    {
                        InventoryHost resistanceHost(descriptor, testContentManifest(), *registry, *crypto,
                            contactImage);
                        auto& resistanceService = resistanceHost.service();
                        resistanceService.synchronizeCells(targetPlayers);
                        auto resistanceUse = targetUse;
                        resistanceUse.sourceId = [] {
                            uint64_t value = 14695981039346656037ull;
                            for (unsigned char c : std::string_view("npc_target_resistance"))
                                value = (value ^ c) * 1099511628211ull;
                            return value;
                        }();
                        auto resistanceLaunch = resistanceService.prepareMagicUse(targetPlayers,
                            proposal(resistanceUse), id<ServerTick>(castTick));
                        require(bool(resistanceLaunch), "Target resistance spell rejected before launch");
                        auto resistanceTick = resistanceService.prepareNativeTick(targetPlayers,
                            id<ServerTick>(castTick), 1.f/30, std::move(resistanceLaunch));
                        require(resistanceTick && resistanceTick->commit(accepted) == CanonicalDurabilityResult::Committed,
                            "Target resistance launch did not commit");
                        uint64_t contactTick = 0;
                        for (uint64_t time = castTick + 1; time < castTick + 30 && !contactTick; ++time)
                        {
                            auto step = resistanceService.prepareNativeTick(targetPlayers,
                                id<ServerTick>(time), 1.f/30, {});
                            require(step && step->commit(accepted) == CanonicalDurabilityResult::Committed,
                                "Target resistance flight did not commit");
                            const auto state = readActorCampaign({reinterpret_cast<const char*>(
                                resistanceService.inventoryImage().data()), resistanceService.inventoryImage().size()});
                            if (!state.projectile && state.timedEffects.size() == 1
                                && state.timedEffects.front().actor == 2)
                                contactTick = time;
                        }
                        require(contactTick, "Target resistance did not land on the NPC");
                        const auto resistanceImage = std::vector(resistanceService.inventoryImage().begin(),
                            resistanceService.inventoryImage().end());
                        InventoryHost resistantRestart(descriptor, testContentManifest(), *registry, *crypto,
                            resistanceImage);
                        auto& resistantService = resistantRestart.service();
                        resistantService.synchronizeCells(targetPlayers);
                        const auto savedResistance = readActorCampaign({reinterpret_cast<const char*>(
                            resistantService.inventoryImage().data()), resistantService.inventoryImage().size()});
                        require(savedResistance.timedEffects.size() == 1
                            && savedResistance.timedEffects.front().magnitude == 100.f,
                            "NPC resistance did not survive restart");
                        auto resistedUse = targetUse;
                        resistedUse.sourceServerTick = id<ServerTick>(contactTick + 1);
                        auto resistedLaunch = resistantService.prepareMagicUse(targetPlayers,
                            proposal(resistedUse), id<ServerTick>(contactTick + 1));
                        require(bool(resistedLaunch), "Post-restart damage spell rejected");
                        auto launchTick = resistantService.prepareNativeTick(targetPlayers,
                            id<ServerTick>(contactTick + 1), 1.f/30, std::move(resistedLaunch));
                        require(launchTick && launchTick->commit(accepted) == CanonicalDurabilityResult::Committed,
                            "Post-restart damage launch did not commit");
                        bool resisted = false;
                        for (uint64_t time = contactTick + 2;
                            time < savedResistance.timedEffects.front().expiresTick && !resisted; ++time)
                        {
                            auto step = resistantService.prepareNativeTick(targetPlayers,
                                id<ServerTick>(time), 1.f/30, {});
                            require(step && step->commit(accepted) == CanonicalDurabilityResult::Committed,
                                "Post-restart damage flight did not commit");
                            const auto state = readActorCampaign({reinterpret_cast<const char*>(
                                resistantService.inventoryImage().data()), resistantService.inventoryImage().size()});
                            if (!state.projectile)
                            {
                                require(state.combat->actors[2][8][2] == savedResistance.combat->actors[2][8][2],
                                    "Restarted Resist Magicka failed to stop Target damage");
                                resisted = true;
                            }
                        }
                        require(resisted, "Damage projectile did not reach active resistance before expiry");
                    }
                    auto launch = targetService.prepareMagicUse(targetPlayers, proposal(targetUse), id<ServerTick>(castTick));
                    require(bool(launch), "Known Target Damage Health spell rejected before launch");
                    auto pendingLaunch = targetService.prepareNativeTick(targetPlayers, id<ServerTick>(castTick),
                        1.f/30, std::move(launch));
                    std::vector<std::byte> launched;
                    require(pendingLaunch->commit([&](auto bytes) {
                        launched.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected;
                    }) == CanonicalDurabilityResult::Rejected
                        && std::ranges::equal(targetService.inventoryImage(), contactImage),
                        "Rejected target launch spent magicka or installed a projectile");
                    const auto launchedState = readActorCampaign({reinterpret_cast<const char*>(launched.data()), launched.size()});
                    require(launchedState.projectile && launchedState.combat
                        && launchedState.combat->actors[0][9][2] < contacted.combat->actors[0][9][2]
                        && launchedState.combat->actors[2][8][2] == contacted.combat->actors[2][8][2],
                        "Target launch failed to stage cost and pending projectile without premature damage");
                    require(pendingLaunch->commit(accepted) == CanonicalDurabilityResult::Committed
                        && std::ranges::equal(targetService.inventoryImage(), launched),
                        "Target launch did not install its durable projectile");
                    auto unknownSource = launched;
                    const size_t sourceOffset = size_t(launchedState.inventory.data()
                        - reinterpret_cast<const char*>(launched.data())) - (timed ? 104 : 96);
                    unknownSource[sourceOffset] ^= std::byte{1};
                    bool rejectedSource = false;
                    try { InventoryHost invalidFlight(descriptor, testContentManifest(), *registry, *crypto,
                        unknownSource); }
                    catch (const std::invalid_argument&) { rejectedSource = true; }
                    require(rejectedSource && std::ranges::equal(targetService.inventoryImage(), launched),
                        "Unknown saved projectile spell source installed on recovery");
                    require(!targetService.prepareMagicUse(targetPlayers, proposal(targetUse), id<ServerTick>(castTick + 1)),
                        "Duplicate Target cast entered while its outcome was pending");
                    InventoryHost flightHost(descriptor, testContentManifest(), *registry, *crypto, launched);
                    const auto offlinePlayers = std::get<CanonicalServerState>(createCanonicalServerState(
                        targetEntities, {}));
                    auto& flight = flightHost.service(); flight.synchronizeCells(offlinePlayers);
                    require(dynamic_cast<InventoryService&>(flight).activeActorCollisionBodies() > 0,
                        "Pending projectile did not retain server collision after both clients left");
                    bool resolved = false;
                    for (uint64_t time = castTick + 1; time < castTick + 90 && !resolved; ++time)
                    {
                        if (time == castTick + 2) flight.synchronizeCells(targetPlayers);
                        const auto& tickPlayers = time == castTick + 1 ? offlinePlayers : targetPlayers;
                        const auto priorImage = std::vector(flight.inventoryImage().begin(), flight.inventoryImage().end());
                        auto nextTick = flight.prepareNativeTick(tickPlayers, id<ServerTick>(time), 1.f/30, {});
                        std::vector<std::byte> candidateImage;
                        require(nextTick->commit([&](auto bytes) {
                            candidateImage.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected;
                        }) == CanonicalDurabilityResult::Rejected
                            && std::ranges::equal(flight.inventoryImage(), priorImage),
                            "Rejected projectile flight/contact changed the campaign");
                        const auto candidateState = readActorCampaign({reinterpret_cast<const char*>(candidateImage.data()),
                            candidateImage.size()});
                        if (!candidateState.projectile)
                        {
                            const auto aliceOutcome = flight.projectCombatEvents(targetPlayers, id<SessionId>(1),
                                id<ServerTick>(time), id<CanonicalRevision>(time), nextTick.get());
                            const auto bobOutcome = flight.projectCombatEvents(targetPlayers, id<SessionId>(2),
                                id<ServerTick>(time), id<CanonicalRevision>(time), nextTick.get());
                            require(aliceOutcome && bobOutcome && aliceOutcome->magicEvents().size() == 1
                                && std::ranges::equal(aliceOutcome->magicEvents(), bobOutcome->magicEvents())
                                && aliceOutcome->magicEvents().front().castSucceeded
                                && aliceOutcome->magicEvents().front().targetHealthDelta < 0
                                && candidateState.combat->actors[2][8][2] < launchedState.combat->actors[2][8][2],
                                "Authoritative contact failed to apply resisted Target damage for both clients");
                            resolved = true;
                        }
                        require(nextTick->commit(accepted) == CanonicalDurabilityResult::Committed,
                            "Projectile step did not commit");
                        if (resolved)
                        {
                            InventoryHost outcomeRestart(descriptor, testContentManifest(), *registry, *crypto,
                                candidateImage);
                            outcomeRestart.service().synchronizeCells(reconnected);
                            const auto aliceTarget = outcomeRestart.service().projectCombat(reconnected,
                                id<SessionId>(1), id<ServerTick>(time), id<CanonicalRevision>(time));
                            const auto bobTarget = outcomeRestart.service().projectCombat(reconnected,
                                id<SessionId>(2), id<ServerTick>(time), id<CanonicalRevision>(time));
                            require(aliceTarget && bobTarget && aliceTarget->actors().size() == 1
                                && bobTarget->actors().size() == 1
                                && aliceTarget->actors().front() == bobTarget->actors().front()
                                && aliceTarget->actors().front().health == candidateState.combat->actors[2][8][2]
                                && std::ranges::equal(outcomeRestart.service().inventoryImage(), candidateImage),
                                "Two-client reconnect changed the resolved Target outcome");
                        }
                    }
                    require(resolved, "Target projectile never resolved authoritative contact");
                    auto blockedEntities = targetEntities;
                    const auto& clearCaster = blockedEntities.front();
                    blockedEntities.front() = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(
                        clearCaster, id<ServerTick>(2), Transform(clearCaster.transform().cell(),
                            Position3(60 * 1024, 40 * 1024, 1024), clearCaster.transform().orientation()),
                        LinearVelocity3(0, 0, 0)));
                    const auto blockedPlayers = std::get<CanonicalServerState>(createCanonicalServerState(
                        blockedEntities, authority.activeSessions()));
                    InventoryHost blockedHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                    auto& blockedService = blockedHost.service(); blockedService.synchronizeCells(blockedPlayers);
                    auto blockedLaunch = blockedService.prepareMagicUse(blockedPlayers, proposal(targetUse),
                        id<ServerTick>(castTick));
                    require(bool(blockedLaunch), "Blocked Target cast rejected before paying its cost");
                    auto blockedCast = blockedService.prepareNativeTick(blockedPlayers, id<ServerTick>(castTick),
                        1.f/30, std::move(blockedLaunch));
                    require(blockedCast && blockedCast->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Blocked Target launch did not commit");
                    const auto blockedImage = std::vector(blockedService.inventoryImage().begin(),
                        blockedService.inventoryImage().end());
                    InventoryHost blockedRestart(descriptor, testContentManifest(), *registry, *crypto, blockedImage);
                    auto& blockedFlight = blockedRestart.service(); blockedFlight.synchronizeCells(blockedPlayers);
                    bool missed = false;
                    for (uint64_t time = castTick + 1; time < castTick + 90 && !missed; ++time)
                    {
                        auto segment = blockedFlight.prepareNativeTick(blockedPlayers, id<ServerTick>(time), 1.f/30, {});
                        const auto outcome = blockedFlight.projectCombatEvents(blockedPlayers, id<SessionId>(1),
                            id<ServerTick>(time), id<CanonicalRevision>(time), segment.get());
                        require(segment->commit(accepted) == CanonicalDurabilityResult::Committed,
                            "Blocked projectile step did not commit");
                        const auto blockedState = readActorCampaign({reinterpret_cast<const char*>(
                            blockedFlight.inventoryImage().data()), blockedFlight.inventoryImage().size()});
                        missed = !blockedState.projectile;
                        if (missed)
                            require(outcome && outcome->magicEvents().size() == 1
                                && !outcome->magicEvents().front().castSucceeded
                                && blockedState.combat->actors[2][8][2] == contacted.combat->actors[2][8][2]
                                && blockedState.combat->actors[0][9][2] < contacted.combat->actors[0][9][2],
                                "World obstruction missed Target without preserving paid cost and target health");
                    }
                    require(missed, "Blocked projectile never resolved as a miss");
                    const auto runItem = [&](bool obstructed) {
                        const auto& itemPlayers = obstructed ? blockedPlayers : targetPlayers;
                        InventoryHost itemHost(descriptor, testContentManifest(), *registry, *crypto, contactImage);
                        auto& itemService = itemHost.service(); itemService.synchronizeCells(itemPlayers);
                        const auto baseline = itemService.projectInventory(itemPlayers, id<SessionId>(1),
                            id<ServerTick>(castTick), id<CanonicalRevision>(castTick));
                        require(baseline && !baseline->playerInventory.empty(), "Enchanted item inventory missing");
                        const auto& inventory = baseline->playerInventory.front();
                        const auto item = std::ranges::find(inventory.stacks,
                            id<ItemPrototypeId>(MWWorld::inventoryRecordId(
                                ESM::RefId::stringRefId("npc_used_shirt"))),
                            &CanonicalItemStack::prototypeId);
                        require(item != inventory.stacks.end(), "WhenUsed item absent from caster inventory");
                        auto itemUse = targetUse;
                        itemUse.sourceKind = MagicUseSourceKind::EnchantedItem;
                        itemUse.sourceId = item->stackId.value();
                        itemUse.expectedInventoryRevision = inventory.revision;
                        auto wrongItem = itemUse; wrongItem.sourceId ^= 1;
                        require(!itemService.prepareMagicUse(itemPlayers, proposal(wrongItem), id<ServerTick>(castTick)),
                            "Unknown WhenUsed instance entered the launch path");
                        auto staleItem = itemUse;
                        staleItem.expectedInventoryRevision = InventoryRevision::initial();
                        if (staleItem.expectedInventoryRevision != inventory.revision)
                            require(!itemService.prepareMagicUse(itemPlayers, proposal(staleItem),
                                id<ServerTick>(castTick)), "Stale item inventory revision entered the launch path");
                        auto prepared = itemService.prepareMagicUse(itemPlayers, proposal(itemUse), id<ServerTick>(castTick));
                        require(bool(prepared), "Owned WhenUsed item rejected before launch");
                        auto launch = itemService.prepareNativeTick(itemPlayers, id<ServerTick>(castTick),
                            1.f/30, std::move(prepared));
                        std::vector<std::byte> candidate;
                        require(launch->commit([&](auto bytes) {
                            candidate.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected;
                        }) == CanonicalDurabilityResult::Rejected
                            && std::ranges::equal(itemService.inventoryImage(), contactImage),
                            "Rejected WhenUsed launch spent charge or installed a projectile");
                        const auto pending = readActorCampaign({reinterpret_cast<const char*>(candidate.data()), candidate.size()});
                        require(pending.projectile && pending.projectile->sourceKind == 1
                            && pending.projectile->source == itemUse.sourceId && pending.projectile->effectSource
                            && pending.combat->actors[0][9][2] == contacted.combat->actors[0][9][2],
                            "WhenUsed launch failed to retain item identity without magicka cost");
                        const auto launchedEffect = itemService.projectCombatEvents(itemPlayers, id<SessionId>(1),
                            id<ServerTick>(castTick), id<CanonicalRevision>(castTick), launch.get());
                        require(launchedEffect && launchedEffect->magicEvents().size() == 1
                            && launchedEffect->magicEvents().front().selfHealthDelta > 0
                            && pending.combat->actors[0][8][2] > contacted.combat->actors[0][8][2]
                            && launchedEffect->magicEvents().front().targetHealthDelta == 0,
                            "WhenUsed Self effect was not separated from pending Target damage");
                        require(launch->commit(accepted) == CanonicalDurabilityResult::Committed,
                            "WhenUsed launch did not commit");
                        auto unknownEffect = candidate;
                        const size_t effectOffset = size_t(pending.inventory.data()
                            - reinterpret_cast<const char*>(candidate.data())) - (timed ? 64 : 56);
                        unknownEffect[effectOffset] ^= std::byte{1};
                        bool rejectedEffect = false;
                        try { InventoryHost invalidItemFlight(descriptor, testContentManifest(), *registry, *crypto,
                            unknownEffect); }
                        catch (const std::invalid_argument&) { rejectedEffect = true; }
                        require(rejectedEffect, "Unknown saved enchantment source installed on recovery");
                        const auto charged = itemService.projectInventory(itemPlayers, id<SessionId>(1),
                            id<ServerTick>(castTick), id<CanonicalRevision>(castTick));
                        const auto spent = std::ranges::find(charged->playerInventory.front().stacks,
                            item->stackId, &CanonicalItemStack::stackId);
                        require(spent != charged->playerInventory.front().stacks.end()
                            && std::bit_cast<float>(spent->enchantmentCharge) >= 0
                            && std::bit_cast<float>(spent->enchantmentCharge) < 20,
                            "WhenUsed item charge was not installed with projectile");
                        require(!itemService.prepareMagicUse(itemPlayers, proposal(itemUse), id<ServerTick>(castTick + 1)),
                            "Duplicate WhenUsed request entered during pending flight");
                        InventoryHost resumed(descriptor, testContentManifest(), *registry, *crypto, candidate);
                        auto& flight = resumed.service(); flight.synchronizeCells(itemPlayers);
                        const auto durableCharge = flight.projectInventory(itemPlayers, id<SessionId>(1),
                            id<ServerTick>(castTick), id<CanonicalRevision>(castTick));
                        const auto restoredItem = std::ranges::find(durableCharge->playerInventory.front().stacks,
                            item->stackId, &CanonicalItemStack::stackId);
                        require(restoredItem != durableCharge->playerInventory.front().stacks.end()
                            && restoredItem->enchantmentCharge == spent->enchantmentCharge,
                            "Flight restart restored pre-launch item charge");
                        bool finished = false;
                        for (uint64_t time = castTick + 1; time < castTick + 90 && !finished; ++time)
                        {
                            auto step = flight.prepareNativeTick(itemPlayers, id<ServerTick>(time), 1.f/30, {});
                            const auto alice = flight.projectCombatEvents(itemPlayers, id<SessionId>(1),
                                id<ServerTick>(time), id<CanonicalRevision>(time), step.get());
                            const auto bob = flight.projectCombatEvents(itemPlayers, id<SessionId>(2),
                                id<ServerTick>(time), id<CanonicalRevision>(time), step.get());
                            require(step->commit(accepted) == CanonicalDurabilityResult::Committed,
                                "WhenUsed projectile flight failed to commit");
                            const auto state = readActorCampaign({reinterpret_cast<const char*>(
                                flight.inventoryImage().data()), flight.inventoryImage().size()});
                            finished = !state.projectile;
                            if (finished)
                            {
                                require(alice && bob && alice->magicEvents().size() == 1
                                    && std::ranges::equal(alice->magicEvents(), bob->magicEvents())
                                    && alice->magicEvents().front().sourceKind == MagicUseSourceKind::EnchantedItem
                                    && alice->magicEvents().front().castSucceeded == !obstructed
                                    && (obstructed ? state.combat->actors[2][8][2] == contacted.combat->actors[2][8][2]
                                        : state.combat->actors[2][8][2] < contacted.combat->actors[2][8][2]),
                                    "WhenUsed contact or miss diverged across clients");
                                InventoryHost restored(descriptor, testContentManifest(), *registry, *crypto,
                                    std::vector<std::byte>(flight.inventoryImage().begin(), flight.inventoryImage().end()));
                                restored.service().synchronizeCells(reconnected);
                                const auto a = restored.service().projectCombat(reconnected, id<SessionId>(1),
                                    id<ServerTick>(time), id<CanonicalRevision>(time));
                                const auto b = restored.service().projectCombat(reconnected, id<SessionId>(2),
                                    id<ServerTick>(time), id<CanonicalRevision>(time));
                                require(a && b && std::ranges::equal(a->actors(), b->actors())
                                    && a->actors().front().health == state.combat->actors[2][8][2],
                                    "Two-client WhenUsed reconnect lost durable outcome");
                            }
                        }
                        require(finished, "WhenUsed projectile never resolved");
                    };
                    runItem(false);
                    runItem(true);
                }
                std::cout << "instant spell=OpenMW mixed restore cost=atomic clients=two restart=reconnected\n";
                return;
            }
            if (combat)
            {
                auto lowSkill = releaseImage;
                const auto parts = readActorCampaign({reinterpret_cast<const char*>(lowSkill.data()), lowSkill.size()});
                const size_t combatEnd = size_t(parts.inventory.data() - reinterpret_cast<const char*>(lowSkill.data()));
                const size_t skill = 11 + ESM::Skill::refIdToIndex(ESM::Skill::ShortBlade);
                const size_t base = lifecycle
                    ? 56 + 8 + parts.melee->identity.size() + 7 * 8 + 8
                        + 2 * ActorCampaignCombat::StatCount * 5 * 8 + skill * 5 * 8
                    : combatEnd - ActorCampaignCombat::StatCount * 5 * 8 + skill * 5 * 8;
                std::fill_n(lowSkill.begin() + base, 8, std::byte{});
                InventoryHost missRollHost(descriptor, testContentManifest(), *registry, *crypto, lowSkill);
                auto& missRoll = missRollHost.service(); missRoll.synchronizeCells(authority);
                bool rolled = false;
                for (uint64_t time = releaseTick + 1; time <= 64 && !rolled; ++time)
                {
                    const auto priorImage = missRoll.inventoryImage();
                    const auto prior = readActorCampaign({reinterpret_cast<const char*>(priorImage.data()), priorImage.size()});
                    const auto conditionBefore = dynamic_cast<InventoryService&>(missRoll).selectedNpcWeaponCondition();
                    auto pending = missRoll.prepareNativeTick(authority, id<ServerTick>(time), 1.f/30, {});
                    require(pending->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Accuracy-miss tick did not commit");
                    const auto image = missRoll.inventoryImage();
                    const auto next = readActorCampaign({reinterpret_cast<const char*>(image.data()), image.size()});
                    rolled = next.melee && next.melee->state.mHit;
                    if (rolled) require(next.melee->contact && prior.combat && next.combat
                        && next.combat->rng != prior.combat->rng
                        && next.combat->actors[0][8][2] == prior.combat->actors[0][8][2]
                        && next.combat->actors[2][10][2] < prior.combat->actors[2][10][2]
                        && dynamic_cast<InventoryService&>(missRoll).selectedNpcWeaponCondition() == *conditionBefore - 1,
                        "Accuracy miss omitted the RNG/fatigue/wear cost or dealt damage");
                }
                require(rolled, "Low-skill swing never reached the KF hit key");
            }
            std::vector<CanonicalPlayerEntityState> distant(authority.players().begin(), authority.players().end());
            for (auto& player : distant)
                player = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(player,
                    id<ServerTick>(1), Transform(player.transform().cell(), Position3(1000 * 1024, 0, 0),
                        player.transform().orientation()), LinearVelocity3(0, 0, 0)));
            const auto farPlayers = std::get<CanonicalServerState>(createCanonicalServerState(distant, authority.activeSessions()));
            InventoryHost missHost(descriptor, testContentManifest(), *registry, *crypto, releaseImage);
            auto& missService = missHost.service(); missService.synchronizeCells(farPlayers);
            bool missed = false;
            for (uint64_t time = releaseTick + 1; time <= 64 && !missed; ++time)
            {
                const auto priorImage = missService.inventoryImage();
                const auto prior = readActorCampaign({reinterpret_cast<const char*>(priorImage.data()), priorImage.size()});
                const auto conditionBefore = combat
                    ? dynamic_cast<InventoryService&>(missService).selectedNpcWeaponCondition() : std::optional<int>{};
                auto pending = missService.prepareNativeTick(farPlayers, id<ServerTick>(time), 1.f/30, {});
                require(pending->commit(accepted) == CanonicalDurabilityResult::Committed,
                    "Out-of-reach melee tick did not commit");
                const auto missImage = missService.inventoryImage();
                const auto candidate = readActorCampaign({reinterpret_cast<const char*>(missImage.data()), missImage.size()});
                missed = candidate.melee && candidate.melee->state.mHit;
                if (missed)
                {
                    require(!candidate.melee->contact && candidate.melee->target == 1,
                        "Out-of-reach player became a melee contact or changed the selected target");
                    if (combat) require(prior.combat && candidate.combat
                        && candidate.combat->rng == prior.combat->rng
                        && candidate.combat->actors[2][10][2] < prior.combat->actors[2][10][2]
                        && candidate.combat->actors[0][8][2] == prior.combat->actors[0][8][2]
                        && dynamic_cast<InventoryService&>(missService).selectedNpcWeaponCondition() == conditionBefore,
                        "Out-of-reach swing changed RNG, health or wear, or omitted its fatigue cost");
                }
            }
            require(missed, "Out-of-reach path never reached the KF hit key");
            if (combat)
            {
                const auto accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
                const auto denied = [](auto) { return CanonicalDurabilityResult::Rejected; };
                const auto* attackingPlayer = authority.findPlayer(id<PlayerId>(2));
                require(attackingPlayer != nullptr, "Second combat participant missing");
                const auto atContact = image();
                {
                    InventoryHost bareHost(descriptor, testContentManifest(), *registry, *crypto, atContact);
                    auto& bare = bareHost.service(); bare.synchronizeCells(authority);
                    const auto start = readActorCampaign({reinterpret_cast<const char*>(atContact.data()), atContact.size()});
                    const auto inventory = bare.projectInventory(authority, id<SessionId>(2),
                        id<ServerTick>(start.tick), id<CanonicalRevision>(start.tick));
                    require(inventory && !inventory->playerInventory.empty(), "Unarmed player inventory unavailable");
                    const auto& carried = inventory->playerInventory.front();
                    const auto equipped = std::ranges::find(carried.equipment, EquipmentSlot::CarriedRight,
                        &EquipmentBinding::slot);
                    require(equipped != carried.equipment.end(), "Unarmed fixture has no right-hand weapon");
                    const auto stack = std::ranges::find(carried.stacks, equipped->stackId, &CanonicalItemStack::stackId);
                    require(stack != carried.stacks.end(), "Unarmed fixture weapon stack missing");
                    const uint64_t unequipTick = start.tick + 1;
                    ClientInventoryTransactionCommand unequip{id<SessionId>(2), SessionGeneration::initial(),
                        CommandSequence::initial(), id<CommandId>(unequipTick), id<CanonicalRevision>(unequipTick),
                        InventoryTransactionKind::UnequipItem, {}, stack->prototypeId, stack->stackId, 1,
                        EquipmentSlot::CarriedRight, carried.revision, {}, {}, Position3(0,0,0)};
                    auto intent = bare.prepareInventory(authority, bind(authority, unequip).proposal());
                    require(bool(intent), "Unarmed fixture unequip rejected");
                    auto prepared = bare.prepareNativeTick(authority, id<ServerTick>(unequipTick), 1.f/30,
                        std::move(intent));
                    require(prepared && prepared->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Unarmed fixture unequip did not commit");
                    bool fatigueHit = false;
                    for (uint64_t time = unequipTick + 1; time <= unequipTick + 128 && !fatigueHit; ++time)
                    {
                        const std::vector beforeBare(bare.inventoryImage().begin(), bare.inventoryImage().end());
                        const auto prior = readActorCampaign({reinterpret_cast<const char*>(beforeBare.data()), beforeBare.size()});
                        const auto view = bare.projectInventory(authority, id<SessionId>(2),
                            id<ServerTick>(time), id<CanonicalRevision>(time));
                        require(view && view->equipment && view->equipment->motions.size() == 1,
                            "Unarmed attack lost the target");
                        ClientMeleeAttackCommand attack{id<SessionId>(2), SessionGeneration::initial(),
                            CommandSequence::initial(), id<CommandId>(time), id<CanonicalRevision>(time),
                            id<ActorId>(view->equipment->motions.front().placement), id<ServerTick>(time),
                            CombatRevision::initial(), CombatRevision::initial(), MeleeAttackType::Chop, 1.f};
                        const ServerCommandProposal proposal(id<SessionId>(2), SessionGeneration::initial(),
                            CommandSequence::initial(), id<CommandId>(time), id<CanonicalRevision>(time),
                            EntityPrecondition(attackingPlayer->entityId(), attackingPlayer->entityRevision(),
                                attackingPlayer->authorityEpoch()), MeleeAttackCommandProposal(attack));
                        auto request = bare.prepareMeleeAttack(authority, proposal, id<ServerTick>(time));
                        require(bool(request), "Authenticated unarmed attack rejected");
                        auto tick = bare.prepareNativeTick(authority, id<ServerTick>(time), 1.f/30,
                            std::move(request));
                        std::vector<std::byte> candidate;
                        require(tick && tick->commit([&](auto bytes) { candidate.assign(bytes.begin(), bytes.end());
                            return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                            && std::ranges::equal(beforeBare, bare.inventoryImage()),
                            "Rejected unarmed effect mutated the campaign");
                        const auto next = readActorCampaign({reinterpret_cast<const char*>(candidate.data()), candidate.size()});
                        const auto firstEvent = bare.projectCombatEvents(authority, id<SessionId>(1),
                            id<ServerTick>(time), id<CanonicalRevision>(time), tick.get());
                        const auto secondEvent = bare.projectCombatEvents(authority, id<SessionId>(2),
                            id<ServerTick>(time), id<CanonicalRevision>(time), tick.get());
                        require(firstEvent && secondEvent && firstEvent->events().size() == 1
                            && std::ranges::equal(firstEvent->events(), secondEvent->events())
                            && firstEvent->events().front().damagedStat == MeleeDamageStat::Fatigue,
                            "Unarmed effect event did not reach both clients");
                        fatigueHit = firstEvent->events().front().hit;
                        if (fatigueHit)
                            require(prior.combat && next.combat
                                && firstEvent->events().front().damage > 0
                                && next.combat->actors[2][10][2] < prior.combat->actors[2][10][2]
                                && next.combat->actors[2][8][2] == prior.combat->actors[2][8][2],
                                "OpenMW unarmed hit did not damage fatigue only");
                        require(tick->commit(accepted) == CanonicalDurabilityResult::Committed
                            && std::ranges::equal(candidate, bare.inventoryImage()),
                            "Unarmed effect did not commit its single durable candidate");
                        if (fatigueHit)
                        {
                            InventoryHost resumedBare(descriptor, testContentManifest(), *registry, *crypto, candidate);
                            const auto reconnected = players(id<SessionGeneration>(2), 1, 2);
                            resumedBare.service().synchronizeCells(reconnected);
                            const auto alice = resumedBare.service().projectCombat(reconnected, id<SessionId>(1),
                                id<ServerTick>(time), id<CanonicalRevision>(time));
                            const auto bob = resumedBare.service().projectCombat(reconnected, id<SessionId>(2),
                                id<ServerTick>(time), id<CanonicalRevision>(time));
                            require(alice && bob && alice->actors().size() == 1 && bob->actors().size() == 1
                                && alice->actors().front() == bob->actors().front()
                                && alice->actors().front().fatigue == next.combat->actors[2][10][2],
                                "Unarmed fatigue effect diverged across restart and two-client reconnect");
                        }
                    }
                    require(fatigueHit, "Unarmed attack never applied fatigue damage");
                    std::cout << "unarmed effect=OpenMW fatigue durability=atomic clients=two restart=reconnected\n";
                }
                const auto current = readActorCampaign({reinterpret_cast<const char*>(atContact.data()), atContact.size()});
                bool dead = false;
                uint64_t deathTick = 0;
                for (uint64_t time = current.tick + 1; time <= current.tick + 128 && !dead; ++time)
                {
                    const auto beforeAttack = image();
                    const auto beforeParts = readActorCampaign({reinterpret_cast<const char*>(beforeAttack.data()), beforeAttack.size()});
                    const auto view = service.projectInventory(authority, id<SessionId>(2),
                        id<ServerTick>(time), id<CanonicalRevision>(time));
                    require(view && view->equipment && view->equipment->motions.size() == 1,
                        "Second client lost the native NPC identity before attacking");
                    ClientMeleeAttackCommand attack{id<SessionId>(2),SessionGeneration::initial(),
                        CommandSequence::initial(),id<CommandId>(time),id<CanonicalRevision>(time),
                        id<ActorId>(view->equipment->motions[0].placement),id<ServerTick>(time),
                        CombatRevision::initial(),CombatRevision::initial(),MeleeAttackType::Chop,1.f};
                    const ServerCommandProposal proposal(id<SessionId>(2),SessionGeneration::initial(),
                        CommandSequence::initial(),id<CommandId>(time),id<CanonicalRevision>(time),
                        EntityPrecondition(attackingPlayer->entityId(),attackingPlayer->entityRevision(),attackingPlayer->authorityEpoch()),
                        MeleeAttackCommandProposal(attack));
                    auto request = service.prepareMeleeAttack(authority, proposal, id<ServerTick>(time));
                    require(bool(request), "Authenticated second-player attack did not enter native tick");
                    auto pending = service.prepareNativeTick(authority, id<ServerTick>(time), 1.f/30, std::move(request));
                    std::vector<std::byte> candidate;
                    require(pending->commit([&](auto bytes) { candidate.assign(bytes.begin(), bytes.end());
                        return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                        && image() == beforeAttack, "Rejected player attack changed the native campaign");
                    const auto proposed = readActorCampaign({reinterpret_cast<const char*>(candidate.data()), candidate.size()});
                    require(proposed.combat && beforeParts.combat
                        && proposed.combat->rng != beforeParts.combat->rng
                        && proposed.combat->actors[1][10][2] < beforeParts.combat->actors[1][10][2],
                        "Player attack omitted durable RNG or fatigue cost");
                    const auto staged = service.projectInventory(authority, id<SessionId>(1),
                        id<ServerTick>(time), id<CanonicalRevision>(time), pending.get());
                    require(staged && staged->playerInventory.front().revision.value() > 0,
                        "Player attack candidate could not project to the other client");
                    const auto attackEvents = service.projectCombatEvents(authority, id<SessionId>(1),
                        id<ServerTick>(time), id<CanonicalRevision>(time), pending.get());
                    require(attackEvents && attackEvents->events().size() == 1
                        && attackEvents->events()[0].attackerPlayerId == id<PlayerId>(2)
                        && attackEvents->events()[0].targetActorId == attack.targetActorId,
                        "Player attack candidate omitted the reliable event");
                    require(pending->commit(accepted) == CanonicalDurabilityResult::Committed
                        && image() == candidate && pending->commit(accepted) == CanonicalDurabilityResult::Rejected,
                        "Player attack retry or duplicate changed the durable result");
                    dead = proposed.combat->actors[2][8][2] <= 0;
                    if (dead) deathTick = time;
                }
                require(dead, "Authenticated player attacks never killed the shared NPC");
                const auto firstLifeItems = lifecycle
                    ? dynamic_cast<InventoryService&>(service).selectedNpcItemIdentities()
                    : std::vector<ESM::RefNum>{};
                const auto corpseImage = image();
                InventoryHost corpseRestart(descriptor, testContentManifest(), *registry, *crypto, corpseImage);
                auto& corpse = corpseRestart.service(); corpse.synchronizeCells(authority);
                require(std::ranges::equal(corpseImage, corpse.inventoryImage()),
                    "NPC death changed across restart");
                const auto first = corpse.projectInventory(authority, id<SessionId>(1),
                    id<ServerTick>(deathTick), id<CanonicalRevision>(deathTick));
                const auto second = corpse.projectInventory(authority, id<SessionId>(2),
                    id<ServerTick>(deathTick), id<CanonicalRevision>(deathTick));
                require(first && second && first->containers.size() == 1 && second->containers.size() == 1
                    && first->containers[0].stacks == second->containers[0].stacks
                    && !first->containers[0].stacks.empty() && first->equipment && second->equipment
                    && first->equipment->motions.size() == 1 && second->equipment->motions.size() == 1,
                    "Two clients did not see one durable NPC corpse inventory");
                const auto firstCombat = corpse.projectCombat(authority, id<SessionId>(1),
                    id<ServerTick>(deathTick), id<CanonicalRevision>(deathTick));
                const auto secondCombat = corpse.projectCombat(authority, id<SessionId>(2),
                    id<ServerTick>(deathTick), id<CanonicalRevision>(deathTick));
                require(firstCombat && secondCombat && firstCombat->actors().size() == 1
                    && secondCombat->actors().size() == 1 && firstCombat->actors()[0].dead
                    && secondCombat->actors()[0].dead
                    && firstCombat->actors()[0] == secondCombat->actors()[0],
                    "Two clients did not receive the same native NPC death snapshot");
                const auto& corpseView = second->containers.front();
                const auto loot = corpseView.stacks.front();
                ClientInventoryTransactionCommand take{id<SessionId>(2),SessionGeneration::initial(),
                    CommandSequence::initial(),id<CommandId>(deathTick + 1),id<CanonicalRevision>(deathTick + 1),
                    InventoryTransactionKind::TakeFromContainer,corpseView.container,loot.prototypeId,
                    loot.stackId,1,{},second->playerInventory.front().revision,corpseView.revision,{},
                    corpseView.position};
                auto transfer = corpse.prepareInventory(authority, bind(authority, take).proposal());
                require(bool(transfer), "Fresh NPC corpse rejected authenticated loot");
                const auto beforeLoot = std::vector(corpse.inventoryImage().begin(), corpse.inventoryImage().end());
                auto looted = corpse.prepareNativeTick(authority, id<ServerTick>(deathTick + 1), 1.f/30,
                    std::move(transfer));
                std::vector<std::byte> lootCandidate;
                require(looted->commit([&](auto bytes) { lootCandidate.assign(bytes.begin(), bytes.end());
                    return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                    && std::ranges::equal(beforeLoot, corpse.inventoryImage()),
                    "Rejected corpse loot changed death or inventory");
                require(looted->commit(accepted) == CanonicalDurabilityResult::Committed
                    && std::ranges::equal(lootCandidate, corpse.inventoryImage())
                    && !corpse.prepareInventory(authority, bind(authority, take).proposal()),
                    "Corpse loot did not commit once or a retry duplicated the stack");
                const auto afterLoot = std::vector(corpse.inventoryImage().begin(), corpse.inventoryImage().end());
                InventoryHost lootRestart(descriptor, testContentManifest(), *registry, *crypto, afterLoot);
                const auto reconnected = players(id<SessionGeneration>(2), 1, 2);
                lootRestart.service().synchronizeCells(reconnected);
                const auto alice = lootRestart.service().projectInventory(reconnected, id<SessionId>(1),
                    id<ServerTick>(deathTick + 1), id<CanonicalRevision>(deathTick + 1));
                const auto bob = lootRestart.service().projectInventory(reconnected, id<SessionId>(2),
                    id<ServerTick>(deathTick + 1), id<CanonicalRevision>(deathTick + 1));
                require(alice && bob && std::ranges::equal(afterLoot, lootRestart.service().inventoryImage())
                    && alice->containers.size() == 1 && bob->containers.size() == 1
                    && alice->containers[0].stacks == bob->containers[0].stacks
                    && alice->containers[0].stacks != corpseView.stacks,
                    "Two-client reconnect refilled or diverged on the looted corpse");
                const auto resumedCombat = lootRestart.service().projectCombat(reconnected, id<SessionId>(2),
                    id<ServerTick>(deathTick + 1), id<CanonicalRevision>(deathTick + 1));
                require(resumedCombat && resumedCombat->actors().size() == 1 && resumedCombat->actors()[0].dead,
                    "Reconnect lost the NPC death presentation state");
                if (lifecycle)
                {
                    const auto deadImage = std::vector(lootRestart.service().inventoryImage().begin(),
                        lootRestart.service().inventoryImage().end());
                    const auto deadState = readActorCampaign({reinterpret_cast<const char*>(deadImage.data()), deadImage.size()});
                    require(deadState.life && deadState.life->generation == 1
                        && deadState.life->respawnTick == deathTick + 3
                        && deadState.life->deaths.size() == 1
                        && deadState.life->deaths[0] == ActorDeathEvent{1, deathTick, 2},
                        "NPC death lost its life, deadline or attributed event");
                    auto invalidHistory = deadImage;
                    const auto killerOffset = size_t(deadState.inventory.data()
                        - reinterpret_cast<const char*>(deadImage.data())) - (timed ? 16 : 8);
                    std::fill_n(invalidHistory.begin() + killerOffset, 8, std::byte{});
                    bool rejectedHistory = false;
                    try { InventoryHost bad(descriptor, testContentManifest(), *registry, *crypto, invalidHistory); }
                    catch (const std::invalid_argument&) { rejectedHistory = true; }
                    require(rejectedHistory && std::ranges::equal(deadImage, lootRestart.service().inventoryImage()),
                        "Malformed attributed death installed or changed the surviving corpse");
                    auto waiting = lootRestart.service().prepareNativeTick(reconnected,
                        id<ServerTick>(deathTick + 2), 1.f/30, {});
                    require(waiting->commit(accepted) == CanonicalDurabilityResult::Committed,
                        "Dead NPC deadline did not survive recovery");
                    auto respawning = lootRestart.service().prepareNativeTick(reconnected,
                        id<ServerTick>(deathTick + 3), 1.f/30, {});
                    const auto stagedLife = lootRestart.service().projectCombat(reconnected, id<SessionId>(2),
                        id<ServerTick>(deathTick + 3), id<CanonicalRevision>(deathTick + 3), respawning.get());
                    const auto stagedItems = lootRestart.service().projectInventory(reconnected, id<SessionId>(2),
                        id<ServerTick>(deathTick + 3), id<CanonicalRevision>(deathTick + 3), respawning.get());
                    require(stagedLife && stagedLife->actors().size() == 1 && !stagedLife->actors()[0].dead
                        && stagedItems && stagedItems->containers.empty() && stagedItems->equipment
                        && stagedItems->equipment->actors.size() == 1,
                        "Respawn candidate did not project restored life and closed corpse together");
                    std::vector<std::byte> respawnCandidate;
                    require(respawning->commit([&](auto bytes) { respawnCandidate.assign(bytes.begin(), bytes.end());
                        return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
                        && readActorCampaign({reinterpret_cast<const char*>(lootRestart.service().inventoryImage().data()),
                            lootRestart.service().inventoryImage().size()}).life->generation == 1,
                        "Rejected respawn changed the old corpse life");
                    require(respawning->commit(accepted) == CanonicalDurabilityResult::Committed
                        && std::ranges::equal(respawnCandidate, lootRestart.service().inventoryImage()),
                        "NPC actor/inventory respawn did not commit atomically");
                    const auto revived = readActorCampaign({reinterpret_cast<const char*>(respawnCandidate.data()),
                        respawnCandidate.size()});
                    require(revived.life && revived.life->generation == 2 && revived.life->bornTick == deathTick + 3
                        && !revived.life->respawnTick && revived.life->deaths == deadState.life->deaths
                        && revived.combat && revived.combat->actors[2] == revived.life->spawnStats,
                        "NPC respawn lost its generation, history or restored stats");
                    const auto fresh = lootRestart.service().projectInventory(reconnected, id<SessionId>(2),
                        id<ServerTick>(deathTick + 3), id<CanonicalRevision>(deathTick + 3));
                    auto staleTake = take;
                    staleTake.sessionGeneration = id<SessionGeneration>(2);
                    require(fresh && fresh->containers.empty() && fresh->equipment
                        && fresh->equipment->actors.size() == 1
                        && dynamic_cast<InventoryService&>(lootRestart.service()).selectedNpcWeaponCondition().value_or(0) > 0
                        && !lootRestart.service().prepareInventory(reconnected, bind(reconnected, staleTake).proposal()),
                        "Respawn left corpse access open or accepted stale loot");
                    const auto secondLifeItems = dynamic_cast<InventoryService&>(lootRestart.service())
                        .selectedNpcItemIdentities();
                    require(secondLifeItems.size() == firstLifeItems.size()
                        && std::ranges::none_of(secondLifeItems, [&](auto item) {
                            return std::ranges::find(firstLifeItems, item) != firstLifeItems.end(); }),
                        "NPC respawn did not replace the complete inventory with fresh identities");
                    require(fresh->equipment->motions.size() == 1, "Revived NPC motion identity missing");
                    const auto* resumedAttacker = reconnected.findPlayer(id<PlayerId>(2));
                    ClientMeleeAttackCommand staleAttack{id<SessionId>(2),id<SessionGeneration>(2),
                        CommandSequence::initial(),id<CommandId>(deathTick + 4),
                        id<CanonicalRevision>(deathTick + 4),
                        id<ActorId>(fresh->equipment->motions[0].placement),id<ServerTick>(deathTick + 1),
                        CombatRevision::initial(),CombatRevision::initial(),MeleeAttackType::Chop,1.f};
                    const ServerCommandProposal staleProposal(id<SessionId>(2),id<SessionGeneration>(2),
                        CommandSequence::initial(),id<CommandId>(deathTick + 4),
                        id<CanonicalRevision>(deathTick + 4),
                        EntityPrecondition(resumedAttacker->entityId(),resumedAttacker->entityRevision(),
                            resumedAttacker->authorityEpoch()),MeleeAttackCommandProposal(staleAttack));
                    require(!lootRestart.service().prepareMeleeAttack(reconnected, staleProposal,
                        id<ServerTick>(deathTick + 4)), "Previous-life attack reached the revived NPC");
                    staleAttack.sourceServerTick = id<ServerTick>(deathTick + 4);
                    const ServerCommandProposal staleRevisionProposal(id<SessionId>(2),id<SessionGeneration>(2),
                        CommandSequence::initial(),id<CommandId>(deathTick + 4),
                        id<CanonicalRevision>(deathTick + 4),
                        EntityPrecondition(resumedAttacker->entityId(),resumedAttacker->entityRevision(),
                            resumedAttacker->authorityEpoch()),MeleeAttackCommandProposal(staleAttack));
                    require(!lootRestart.service().prepareMeleeAttack(reconnected, staleRevisionProposal,
                        id<ServerTick>(deathTick + 4)), "Previous-life target revision reached the revived NPC");
                    try
                    {
                        InventoryHost revivedRestart(descriptor, testContentManifest(), *registry, *crypto, respawnCandidate);
                        require(std::ranges::equal(respawnCandidate, revivedRestart.service().inventoryImage()),
                            "NPC new life changed across restart");
                    }
                    catch (const std::exception& error)
                    { throw std::runtime_error(std::string("NPC new life restart: ") + error.what()); }
                }
                InventoryHost routedHost(descriptor, testContentManifest(), *registry, *crypto, atContact);
                auto& routedService = routedHost.service(); routedService.synchronizeCells(authority);
                NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
                CanonicalCommandReducer reducer(authority, observability, testContentManifest());
                const auto catalog = ServerScriptStateCatalog::create({}).value();
                auto scripts = CanonicalScriptState::initial(catalog).value();
                std::array<std::byte,32> configuration{}; configuration[0] = std::byte{24};
                const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
                    ServerConfigurationId::fromBytes(configuration).value(), {}, catalog, {}).value();
                auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                    ServerApp::CanonicalPersistenceFile::open(scratch / "routed-attack.bin", identity));
                require(reducer.configureDurability(*file, nullptr, nullptr, nullptr, nullptr, nullptr,
                    &scripts, &routedService), "Native combat reducer durability wiring failed");
                const auto routedView = routedService.projectInventory(authority, id<SessionId>(2),
                    id<ServerTick>(current.tick), id<CanonicalRevision>(current.tick));
                require(routedView && routedView->equipment && routedView->equipment->motions.size() == 1,
                    "Routed combat target not visible to the second player");
                const uint64_t routedTick = current.tick + 1;
                ClientMeleeAttackCommand routedAttack{id<SessionId>(2),SessionGeneration::initial(),
                    CommandSequence::initial(),id<CommandId>(9000),reducer.canonicalRevision(),
                    id<ActorId>(routedView->equipment->motions[0].placement),id<ServerTick>(routedTick),
                    CombatRevision::initial(),CombatRevision::initial(),MeleeAttackType::Chop,1.f};
                const ServerCommandProposal routed(id<SessionId>(2),SessionGeneration::initial(),
                    CommandSequence::initial(),id<CommandId>(9000),reducer.canonicalRevision(),
                    EntityPrecondition(attackingPlayer->entityId(),attackingPlayer->entityRevision(),attackingPlayer->authorityEpoch()),
                    MeleeAttackCommandProposal(routedAttack));
                Clock ingressClock;
                const auto route = [&](uint64_t tick) {
                    ServerCommandIntakeCoordinator intake(ingressClock, observability,
                        MonotonicInstant::fromNanoseconds(0),
                        id<ServerTick>(tick), IngressOrdinal::initial());
                    require(intake.submit(routed) == CommandSubmissionResult::Accepted,
                        "Authenticated attack command intake failed");
                    ingressClock.value = tick * 33'333'334;
                    auto batch = intake.pump();
                    require(batch && batch.batches().size() == 1, "Attack intake produced no scheduled tick");
                    auto pending = reducer.prepareTick(batch.batches().front());
                    require(pending.result() && reducer.stageNativeDoorStep(pending, id<ServerTick>(tick), 1.f/30),
                        "Native attack reducer staging failed");
                    return pending;
                };
                auto routedPending = route(routedTick);
                require(routedPending.result().dispositions()[0].disposition() == CommandDisposition::Applied
                    && reducer.commit(std::move(routedPending)),
                    "Authenticated player attack was not durably applied by the reducer");
                const auto onceImage = std::vector(routedService.inventoryImage().begin(), routedService.inventoryImage().end());
                const auto once = readActorCampaign({reinterpret_cast<const char*>(onceImage.data()), onceImage.size()});
                auto duplicate = route(routedTick + 1);
                const auto disposition = duplicate.result().dispositions()[0].disposition();
                require((disposition == CommandDisposition::DuplicateCommandId
                        || disposition == CommandDisposition::AlreadyFinalized)
                    && reducer.commit(std::move(duplicate)), "Duplicate authenticated attack was not finalized");
                const auto replayImage = std::vector(routedService.inventoryImage().begin(), routedService.inventoryImage().end());
                const auto replay = readActorCampaign({reinterpret_cast<const char*>(replayImage.data()), replayImage.size()});
                require(once.combat && replay.combat && replay.combat->actors[2][8][2] == once.combat->actors[2][8][2],
                    "Duplicate authenticated request damaged the NPC twice");
                std::cout << "player attacks=authenticated native tick death=durable corpse=shared loot=once reconnect=two\n";
            }
            std::cout << "melee campaign resource=bound release=server contact=hit-key retry=once restart=exact"
                << (combat ? " damage=fatigue+health+wear rng=durable" : "")
                << " (synthetic actor, real KF)\n";
            return;
        }
        const auto observers = authority;
        const auto view = [&](ServerApp::NativeInventoryService& owner, uint64_t tick, uint64_t session = 1,
            const PreparedNativeInventory* pending = nullptr) {
            auto value = owner.projectInventory(authority, id<SessionId>(session), id<ServerTick>(tick), id<CanonicalRevision>(tick), pending);
            require(bool(value) && value->groundItems.size() == 1 && value->groundItems[0].doors.size() == 1
                && value->equipment && value->equipment->motions.size() == 1, "NPC door projection domain incomplete");
            return *value;
        };
        const auto doorView = [&](ServerApp::NativeInventoryService& owner, uint64_t tick, const PreparedNativeInventory* pending = nullptr) {
            return view(owner, tick, 1, pending).groundItems[0].doors[0];
        };
        const auto activation = [&](ServerApp::NativeInventoryService& owner, uint64_t tick) {
            const auto door = doorView(owner, tick);
            const auto& player = *authority.findPlayer(id<PlayerId>(1));
            const auto command = ServerCommandProposal(id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(tick), id<CanonicalRevision>(tick),
                EntityPrecondition(player.entityId(), player.entityRevision(), player.authorityEpoch()),
                InteractiveObjectCommandProposal(id<InteractiveObjectId>(door.placement), player.transform().cell(),
                    player.transform().position(), id<ObjectRevision>(door.motion), ObjectInteractionKind::Activate, {}));
            auto result = owner.prepareDoorActivation(authority, command);
            require(bool(result), "NPC door activation rejected");
            return result;
        };
        const NativeInventoryCommit rejected = [](auto) { return CanonicalDurabilityResult::Rejected; };
        const NativeInventoryCommit accepted = [](auto) { return CanonicalDurabilityResult::Committed; };
        const std::vector initial(service.inventoryImage().begin(), service.inventoryImage().end());
        bool invalidTime = false;
        try { (void)service.prepareNativeTick(authority, id<ServerTick>(1), std::numeric_limits<float>::quiet_NaN(), {}); }
        catch (const std::invalid_argument&) { invalidTime = true; }
        require(invalidTime && std::ranges::equal(initial, service.inventoryImage()), "Nonfinite composed tick accepted");
        auto first = service.prepareNativeTick(authority, id<ServerTick>(1), 1.f/30, activation(service, 1));
        const auto blocked = doorView(service, 1, first.get());
        require(blocked.blocked && blocked.angle == 0 && blocked.direction == 1,
            "NPC did not stall the proposed opening swing");
        require(doorView(service, 1).direction == 0 && std::ranges::equal(initial, service.inventoryImage()),
            "Staged NPC/door state leaked before durability");
        require(first->commit(rejected) == CanonicalDurabilityResult::Rejected && std::ranges::equal(initial, service.inventoryImage()),
            "Rejected NPC/door tick changed the live image");
        require(first->commit(accepted) == CanonicalDurabilityResult::Committed, "NPC/door retry failed");
        const std::vector saved(service.inventoryImage().begin(), service.inventoryImage().end());
        InventoryHost recovered(descriptor, testContentManifest(), *registry, *crypto, saved);
        auto& restored = recovered.service(); restored.synchronizeCells(authority);
        require(std::ranges::equal(saved, restored.inventoryImage()), "NPC/door recovery changed durable bytes");
        auto next = service.prepareNativeTick(authority, id<ServerTick>(2), 1.f/30, {});
        auto replay = restored.prepareNativeTick(authority, id<ServerTick>(2), 1.f/30, {});
        std::vector<std::byte> expected, actual;
        next->commit([&](auto bytes) { expected.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
        replay->commit([&](auto bytes) { actual.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
        require(!expected.empty() && expected == actual, "Recovered NPC/door physics changed the next tick");
        // An inventory command must not steal the moving door's simulation slot.
        const auto inventory = view(service, 2).playerInventory.front();
        require(!inventory.equipment.empty(), "NPC door inventory fixture has no equipment");
        const auto slot = inventory.equipment.front();
        const auto item = std::ranges::find(inventory.stacks, slot.stackId, &CanonicalItemStack::stackId);
        ClientInventoryTransactionCommand input{id<SessionId>(1),SessionGeneration::initial(),CommandSequence::initial(),id<CommandId>(2),
            id<CanonicalRevision>(2),InventoryTransactionKind::UnequipItem,{},item->prototypeId,item->stackId,1,slot.slot,
            inventory.revision,{},{},Position3(0,0,0)};
        auto composed = service.prepareNativeTick(authority, id<ServerTick>(2), 1.f/30,
            service.prepareInventory(authority, bind(authority, input).proposal()));
        require(view(service, 2, 1, composed.get()).playerInventory.front().equipment.empty(), "Composed inventory projection lost unequip");
        require(composed->commit(rejected) == CanonicalDurabilityResult::Rejected && std::ranges::equal(saved, service.inventoryImage()),
            "Rejected inventory/NPC/door composition leaked");
        require(composed->commit(accepted) == CanonicalDurabilityResult::Committed, "Inventory/NPC/door commit failed");
        require(next->commit(accepted) == CanonicalDurabilityResult::Rejected, "Stale NPC/door candidate installed");
        bool advanced = doorView(service, 2).angle > 0;
        for (uint64_t tick = 3; tick <= 12; ++tick)
        {
            auto pending = service.prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, {});
            require(pending->commit(accepted) == CanonicalDurabilityResult::Committed, "NPC door continuation failed");
            const auto a = view(service, tick, 1), b = view(service, tick, 2);
            require(a.groundItems[0].doors == b.groundItems[0].doors && a.equipment->motions == b.equipment->motions,
                "Two player projections diverged on NPC/door state");
            advanced |= a.groundItems[0].doors[0].angle > 0;
        }
        require(advanced, "Door remained blocked after the NPC moved clear");
        if (avoidance)
        {
            require(view(service, 12).equipment->motions[0].position[0] > 62,
                "Contact did not interrupt travel with an outward retreat");
            InventoryHost continuation(descriptor, testContentManifest(), *registry, *crypto, service.inventoryImage());
            auto& resume = continuation.service(); resume.synchronizeCells(authority);
            for (uint64_t tick = 13; tick <= 150; ++tick)
            {
                auto pending = resume.prepareNativeTick(authority, id<ServerTick>(tick), 1.f / 30, {});
                require(pending->commit(accepted) == CanonicalDurabilityResult::Committed, "Avoidance continuation failed");
            }
            const auto end = view(resume, 150).equipment->motions[0].position;
            require(std::abs(end[0] - 60) < 8 && std::abs(end[1] + 240) < 8
                && doorView(resume, 150).direction == 0 && doorView(resume, 150).angle > 1.5f,
                "NPC failed to resume its original destination after clearing the opening door");
        }
        {
            InventoryHost midSwing(descriptor, testContentManifest(), *registry, *crypto, service.inventoryImage());
            midSwing.service().synchronizeCells(authority);
            const auto restoredDoor = doorView(midSwing.service(), 12);
            require(restoredDoor.angle == doorView(service, 12).angle && restoredDoor.angle > 0,
                "Recovery lost a partially open door angle");
            auto original = service.prepareNativeTick(authority, id<ServerTick>(13), 1.f/30, {});
            auto resumed = midSwing.service().prepareNativeTick(authority, id<ServerTick>(13), 1.f/30, {});
            expected.clear(); actual.clear();
            original->commit([&](auto bytes) { expected.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
            resumed->commit([&](auto bytes) { actual.assign(bytes.begin(), bytes.end()); return CanonicalDurabilityResult::Rejected; });
            require(!expected.empty() && expected == actual, "Mid-swing recovery changed composed door/actor continuation");
        }
        auto reversal = service.prepareNativeTick(authority, id<ServerTick>(13), 1.f/30, activation(service, 13));
        require(doorView(service, 13, reversal.get()).angle < doorView(service, 12).angle,
            "NPC door reversal did not retreat");
        require(reversal->commit(accepted) == CanonicalDurabilityResult::Committed, "NPC door reversal failed");
        const auto dropSession = [&](uint64_t session) {
            std::vector<CanonicalSessionProgress> sessions;
            for (const auto& active : authority.activeSessions()) if (active.sessionId().value() != session) sessions.push_back(active);
            const std::array actors{*authority.findPlayer(id<PlayerId>(1)), *authority.findPlayer(id<PlayerId>(2))};
            authority = std::get<CanonicalServerState>(createCanonicalServerState(actors, sessions));
            service.synchronizeCells(authority);
        };
        dropSession(1);
        const auto before = view(service, 14, 2).equipment->motions[0].position;
        auto survivor = service.prepareNativeTick(authority, id<ServerTick>(14), 1.f/30, {});
        require(survivor->commit(accepted) == CanonicalDurabilityResult::Committed
            && view(service, 14, 2).equipment->motions[0].position != before, "Disconnect stopped the surviving player's simulation");
        dropSession(2);
        const auto frozen = std::vector(service.inventoryImage().begin(), service.inventoryImage().end());
        if (traveler)
        {
            const auto bodies = [](ServerApp::NativeInventoryService& owner) {
                return dynamic_cast<InventoryService&>(owner).activeActorCollisionBodies();
            };
            require(bodies(service) > 0, "Both players leaving unloaded an active traveler");
            require(!service.projectInventory(authority, id<SessionId>(1), id<ServerTick>(15), id<CanonicalRevision>(15)),
                "Traveler demand manufactured client replication interest");
            auto rejectedStep = service.prepareNativeTick(authority, id<ServerTick>(15), 1.f/30, {});
            require(rejectedStep->commit(rejected) == CanonicalDurabilityResult::Rejected
                && std::ranges::equal(frozen, service.inventoryImage()), "Rejected unattended tick leaked");
            rejectedStep.reset();
            InventoryHost occupied(descriptor, testContentManifest(), *registry, *crypto, service.inventoryImage());
            occupied.service().synchronizeCells(observers);
            std::unique_ptr<InventoryHost> restart;
            bool completed = false;
            for (uint64_t tick = 15; tick <= 240; ++tick)
            {
                if (tick == 25)
                {
                    restart = std::make_unique<InventoryHost>(descriptor, testContentManifest(), *registry, *crypto,
                        service.inventoryImage());
                    restart->service().synchronizeCells(authority);
                    require(bodies(restart->service()) > 0, "Restart forgot unattended travel demand");
                }
                const bool traveling = bodies(service) > 0;
                auto step = service.prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, {});
                require(step->commit(accepted) == CanonicalDurabilityResult::Committed, "Unattended travel tick failed");
                service.synchronizeCells(authority);
                auto connectedStep = occupied.service().prepareNativeTick(observers, id<ServerTick>(tick), 1.f/30, {});
                require(connectedStep->commit(accepted) == CanonicalDurabilityResult::Committed, "Occupied comparison tick failed");
                occupied.service().synchronizeCells(observers);
                if (traveling)
                    require(std::ranges::equal(service.inventoryImage(), occupied.service().inventoryImage()),
                        "Player/traveler area union stepped the NPC twice or changed unattended simulation");
                if (restart)
                {
                    auto continued = restart->service().prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, {});
                    require(continued->commit(accepted) == CanonicalDurabilityResult::Committed, "Unattended restart tick failed");
                    restart->service().synchronizeCells(authority);
                    require(std::ranges::equal(service.inventoryImage(), restart->service().inventoryImage()),
                        "Restart changed unattended destination, completion, doors or RNG");
                }
                if (restart && !bodies(service))
                {
                    completed = true;
                    const auto projection = service.projectInventory(observers, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick));
                    require(projection && projection->equipment && projection->equipment->motions.size() == 1,
                        "Dormant traveler lost its canonical snapshot");
                    const auto end = projection->equipment->motions[0].position;
                    require(std::abs(end[0]-60) < 8 && std::abs(end[1]+240) < 8,
                        "Unattended traveler released demand before reaching the retained destination");
                    const std::vector done(service.inventoryImage().begin(), service.inventoryImage().end());
                    InventoryHost completionRestart(descriptor, testContentManifest(), *registry, *crypto, done);
                    completionRestart.service().synchronizeCells(authority);
                    require(!bodies(completionRestart.service())
                        && std::ranges::equal(done, completionRestart.service().inventoryImage()),
                        "Restart restarted completed travel or retained idle collision resources");
                    service.synchronizeCells(observers);
                    require(bodies(service) > 0 && std::ranges::equal(done, service.inventoryImage()),
                        "Returning players failed to reload the completed scene exactly");
                    auto stale = service.prepareNativeTick(observers, id<ServerTick>(tick+1), 1.f/30, {});
                    service.synchronizeCells(authority);
                    service.synchronizeCells(observers);
                    require(!service.projectInventory(observers, id<SessionId>(1), id<ServerTick>(tick+1),
                            id<CanonicalRevision>(tick+1), stale.get())
                        && stale->commit(accepted) == CanonicalDurabilityResult::Rejected
                        && std::ranges::equal(done, service.inventoryImage()),
                        "Scene reload accepted a candidate holding retired collision references");
                    service.synchronizeCells(authority);
                    require(!bodies(service), "Completed traveler remained active after observers left");
                    auto idle = service.prepareNativeTick(authority, id<ServerTick>(tick+1), 1.f/30, {});
                    require(idle->commit(accepted) == CanonicalDurabilityResult::Committed, "Dormant tick failed");
                    const auto previous = readActorCampaign({reinterpret_cast<const char*>(done.data()), done.size()});
                    const auto current = service.inventoryImage();
                    const auto parts = readActorCampaign({reinterpret_cast<const char*>(current.data()), current.size()});
                    require(std::ranges::equal(previous.actor, parts.actor) && std::ranges::equal(previous.inventory, parts.inventory),
                        "Completed travel or door state changed while unloaded");
                    std::cout << "traveler completion_tick=" << tick << " empty=continued union=once mid-travel-restart=exact "
                        << "unload-reload=exact completed-restart=idle range-rejection=atomic\n";
                    break;
                }
            }
            require(completed, "Unattended traveler did not complete within its bounded work allowance");
        }
        else
        {
            auto inactive = service.prepareNativeTick(authority, id<ServerTick>(15), 1.f/30, {});
            require(inactive->commit(accepted) == CanonicalDurabilityResult::Committed, "Inactive NPC/door tick failed");
            const auto oldImage = readActorCampaign({reinterpret_cast<const char*>(frozen.data()), frozen.size()});
            const auto newImage = service.inventoryImage();
            const auto newParts = readActorCampaign({reinterpret_cast<const char*>(newImage.data()), newImage.size()});
            require(std::ranges::equal(oldImage.actor, newParts.actor) && std::ranges::equal(oldImage.inventory, newParts.inventory),
                "Unoccupied NPC/door scene did not freeze coherently");
        }
        require(replay->commit([](auto) { return CanonicalDurabilityResult::Failed; }) == CanonicalDurabilityResult::Failed
            && restored.inventoryImage().empty(), "Uncertain NPC/door commit did not close the service");
        auto truncated = saved; truncated.pop_back(); bool invalid = false;
        try { InventoryHost bad(descriptor, testContentManifest(), *registry, *crypto, truncated); }
        catch (const std::invalid_argument&) { invalid = true; }
        require(invalid, "Truncated NPC/door campaign accepted");
        std::cout << "npc-door contact=server-owned rejection=atomic inventory=composed reversal=shared recovery=exact "
            << "disconnect=continued empty=" << (traveler ? "travel" : "freeze")
            << " uncertain=closed (synthetic room on retained loadout)\n";
    }

}
