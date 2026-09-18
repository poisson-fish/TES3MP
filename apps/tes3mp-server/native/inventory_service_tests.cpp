#include "inventory_service_tests.hpp"
#include "inventory_service.hpp"
#include "inventory_host.hpp"
#include "loadout.hpp"
#include "test_allocations.hpp"
#include "../canonical_persistence_file.hpp"
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwgui/inventoryitemmodel.hpp>
#include <apps/openmw/mwgui/sortfilteritemmodel.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
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
        content.store.insertStatic(chest);
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
        content.store.insertStatic(chest);
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
        bool wholeInterior, bool baseInventory, bool worldActors)
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
        // Generated override on actual game records. This is a synthetic mod,
        // not evidence for an arbitrary published mod or its script behavior.
        std::array<ESM::NPC, 2> startingCharacters;
        ESM::Creature startingCreature;
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
                ESM::Cell cell; cell.blank(); cell.mName = "vNext actor inventory test";
                cell.mData.mFlags = ESM::Cell::Interior; cell.updateId();
                out.startRecord(ESM::Cell::sRecordId, 0); cell.save(out);
                uint32_t index = 0;
                for (auto base : {ESM::RefId::stringRefId("vnext_dead_actor"), startingCreature.mId, npc.mId})
                {
                    ESM::CellRef placement; placement.blank(); placement.mRefNum = {++index, 0}; placement.mRefID = base;
                    placement.save(out);
                }
                out.endRecord(ESM::Cell::sRecordId);
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
            out << (worldActors ? "native-inventory-6" : baseInventory ? "native-inventory-5" : wholeInterior ? "native-inventory-4" : "native-inventory-3") << "\nmanifest ";
            const auto manifestId = testContentManifestId();
            for (auto byte : manifestId.bytes())
                out << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(byte);
            out << "\nconfig \"openmw\"\nplayers 1 2\n";
            out << (baseInventory ? "actors \"player\" \"vnext_second_character\"\n"
                : "actors \"player\" 3 \"player\" 5\nshirt \"common_shirt_01\"\n");
            out << "loot 1 0\n";
            out << (worldActors ? "interior \"vNext actor inventory test\"\n" : wholeInterior ? "interior \"Seyda Neen, Fargoth's House\"\n"
                : "container \"Imperial Prison Ship\" \"Morrowind.esm\" 421490\n") << "cell interior:7\n";
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

    void checkCanonicalInventory(const std::filesystem::path& scratch, bool equipment)
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
        const auto action = [&](InventoryService& current, const CanonicalServerState& state, uint64_t session, bool put, uint32_t count) {
            auto input = wire(current, state, session, equipment || put, equipment ? 1 : count);
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
        require(candidate && (equipment ? candidate->playerInventory[0].equipment.size() == 1
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
        auto take = action(recovered, resumed, 2, false, 1);
        ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(4), IngressOrdinal::initial());
        require(intake.submit(bind(resumed, take).proposal()) == CommandSubmissionResult::Accepted, "Continuation intake failed");
        clock.value += 4 * 33'333'334;
        auto pumped = intake.pump();
        auto next = continued.prepareTick(pumped.batches().front());
        require(next.result().dispositions()[0].disposition() == CommandDisposition::Applied
            && continued.commit(std::move(next)), "Recovered native continuation failed");
        const auto view = recovered.project(continued.state(), id<SessionId>(2), id<ServerTick>(4), continued.canonicalRevision());
        require(view && (equipment ? view->playerInventory[0].equipment.size() == 1
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
}
