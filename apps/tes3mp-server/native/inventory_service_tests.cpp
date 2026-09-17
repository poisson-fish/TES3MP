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
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadnpc.hpp>
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

    void checkPlayerInventories(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Player inventory scratch already exists");
        Content content;
        ESM::NPC character; character.blank(); character.mId = ESM::RefId::stringRefId("loaded_character");
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
        bool wholeInterior, bool baseInventory)
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
            writeStartingPlugin(25);
            std::ofstream out(scratch / "openmw" / "openmw.cfg", std::ios::app);
            out << "\ndata=" << std::quoted(scratch.generic_string()) << "\ncontent=StartingInventories.esp\n";
        }
        const auto descriptor = scratch / "native.txt";
        {
            std::ofstream out(descriptor);
            out << (baseInventory ? "native-inventory-5" : wholeInterior ? "native-inventory-4" : "native-inventory-3") << "\nmanifest ";
            const auto manifestId = testContentManifestId();
            for (auto byte : manifestId.bytes())
                out << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(byte);
            out << "\nconfig \"openmw\"\nplayers 1 2\n";
            out << (baseInventory ? "actors \"player\" \"vnext_second_character\"\n"
                : "actors \"player\" 3 \"player\" 5\nshirt \"common_shirt_01\"\n");
            out << "loot 1 0\n";
            out << (wholeInterior ? "interior \"Seyda Neen, Fargoth's House\"\n"
                : "container \"Imperial Prison Ship\" \"Morrowind.esm\" 421490\n") << "cell interior:7\n";
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
                equip(1, "common_shirt_01", EquipmentSlot::Shirt, true);
                equip(1, "common_pants_01", EquipmentSlot::Pants, true);
                equip(1, "iron dagger", EquipmentSlot::CarriedRight, true);
                equip(1, "iron dagger", EquipmentSlot::CarriedRight, false);
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
                require(late.clients[0]->confirmedPlayerInventoryBaseline()->equipment.size() == 2
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
                    "shirt/pants equip, dagger equip/unequip/transfer/recipient equip, exact equipment restart/late join, legacy/content mismatch rejection; synthetic transport\n";
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
