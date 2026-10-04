#include "inventory_host.hpp"
#include "inventory_service.hpp"
#include "actor_campaign.hpp"
#include <tes3mp/player_identity.hpp>
#include <tes3mp/server_authentication.hpp>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::Native;
    template<class T> T id(uint64_t value) { return T::fromValue(value).value(); }
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    std::vector<std::byte> image(const ServerApp::NativeInventoryService& owner)
    { return {owner.inventoryImage().begin(), owner.inventoryImage().end()}; }

    void check(const std::filesystem::path& scratch, const std::filesystem::path& config,
        const std::filesystem::path& settings)
    {
        require(std::filesystem::create_directory(scratch), "Exploration scratch already exists");
        const auto cell = [](int x) { return CellId::exterior(id<CellSpaceId>(1), x, 100); };
        const auto engine = [](int x) { return ESM::RefId::esm3ExteriorCell(x, 100); };
        std::vector<CellId> cells;
        for (int x = -2; x <= 9; ++x) cells.push_back(cell(x));
        cells.push_back(cell(20));
        const std::array spaces{CellSpaceDeclaration{id<CellSpaceId>(1), CellSpaceKind::Exterior}};
        const auto manifest = ContentManifest::create(testContentManifestId(), spaces, cells,
            id<AppearanceId>(4), testMovementProfile()).value();
        const auto manifestId = manifest.id();
        const auto descriptor = scratch / "native.txt";
        {
            std::ofstream out(descriptor);
            out << "native-inventory-77\nmanifest ";
            for (auto byte : manifestId.bytes()) out << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte);
            out << std::dec << "\nconfig " << std::quoted(config.string())
                << "\nplayers 1 2\nactors \"region_actor\" \"region_actor\"\nloot 1 0\n";
            for (size_t i = 0; i < cells.size(); ++i)
            {
                out << "exterior " << cells[i].asExterior()->gridX() << " 100\n";
                if (!i) out << "doors auto\n";
                out << "cell exterior:1:" << cells[i].asExterior()->gridX() << ":100\n";
                if (!i) out << "areas " << cells.size() << '\n';
            }
            out << "actors auto " << std::quoted(settings.string()) << "\nrespawn 27000\n";
        }
        const auto transform = [&](int x, int offset) {
            const auto zero = Turn32::fromValue(0);
            return Transform(cell(x), Position3((int64_t(x)*8192+offset)*1024,
                (100*8192+1000)*1024ll, 257*1024), Orientation3(zero, zero, zero));
        };
        std::vector<CanonicalPlayerEntityState> entities;
        std::vector<CanonicalSessionProgress> sessions;
        for (uint64_t i : {1, 2})
        {
            entities.emplace_back(id<PlayerId>(i), id<EntityId>(i), id<AppearanceId>(4), transform(3, int(i)*100),
                LinearVelocity3(0,0,0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial());
            sessions.emplace_back(id<SessionId>(i), SessionGeneration::initial(), id<PlayerId>(i), id<EntityId>(i), std::nullopt);
        }
        auto authority = std::get<CanonicalServerState>(createCanonicalServerState(entities, sessions));
        auto crypto = makeProductionCredentialCrypto(); require(bool(crypto), "Credential crypto unavailable");
        struct Storage : PlayerIdentityPersistence
        { bool replace(std::span<const PersistedPlayerIdentity>) noexcept override { return true; } } storage;
        CharacterDerivedState derived; derived.attributes.fill(100); derived.skills.fill(100);
        auto profile = CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter, CharacterCreationPhase::Complete,
            "Explorer", CharacterAppearance{id<RaceRecordId>(1), id<HeadRecordId>(1), id<HairRecordId>(1), CharacterSex::Male},
            CharacterClass{id<ClassRecordId>(1)}, id<BirthsignRecordId>(1), derived, {}, id<CharacterProfileRevision>(2)).value();
        std::vector<PersistedPlayerIdentity> records;
        for (uint64_t i : {1, 2})
        {
            CredentialDigest digest; digest.bytes.fill(std::byte(i));
            records.push_back({{id<PlayerId>(i),id<EntityId>(i),id<AppearanceId>(4),manifest.id()}, digest,
                entities[i-1], profile});
        }
        auto registry = std::get<std::unique_ptr<PlayerIdentityRegistry>>(PlayerIdentityRegistry::create(*crypto, storage, records));
        InventoryHost host(descriptor, manifest, *registry, *crypto, {});
        auto& service = host.service();
        auto& native = dynamic_cast<InventoryService&>(service);
        CanonicalWorldTimeState time; time.daysPassed = 42; time.day = 1; time.year = 427;
        const auto world = CanonicalWorldState::initial(time, GlobalVariableCatalog::create({}).value(),
            QuestJournalCatalog::create(manifestId, {}, {}).value(), FactionDialogueCatalog::create(manifestId, {}, {}).value()).value();
        uint64_t tick = 2;
        const auto occupy = [&](int a, int b, bool online = true) {
            for (size_t i = 0; i < entities.size(); ++i)
                entities[i] = std::get<CanonicalPlayerEntityState>(advanceCanonicalSpatialState(entities[i], id<ServerTick>(tick),
                    transform(i ? b : a, int(i+1)), LinearVelocity3(0,0,0)));
            authority = std::get<CanonicalServerState>(createCanonicalServerState(entities,
                online ? std::span<const CanonicalSessionProgress>(sessions) : std::span<const CanonicalSessionProgress>{}));
            service.synchronizeCells(authority);
        };
        const auto baseline = [&](uint64_t player) {
            return service.projectInventory(authority, id<SessionId>(player), id<ServerTick>(tick), id<CanonicalRevision>(tick));
        };
        occupy(3, 3);
        const auto first = native.actorCollisionScene().forCell(engine(2)).actorId();
        const auto near = baseline(1);
        const auto combat = service.projectCombat(authority, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick));
        require(near && near->equipment && near->equipment->motions.size() == 2 && combat && combat->actors().size() == 2,
            "Adjacent cell omitted actor movement/combat visibility");
        const auto identity = near->equipment->motions.front().placement;
        const auto initial = image(service);
        const auto inventory = near->playerInventory;
        const auto terrain = native.actorCollisionScene().terrainCount();
        require(terrain < cells.size(), "Occupied neighborhood retained all bound terrain");
        {
            // Adding an overlapping player demand must not add a simulation step.
            auto solo = std::get<CanonicalServerState>(createCanonicalServerState(entities,
                std::span<const CanonicalSessionProgress>(sessions).first(1)));
            InventoryHost single(descriptor, manifest, *registry, *crypto, initial);
            single.service().synchronizeCells(solo);
            std::vector<std::byte> oneImage, twoImage;
            const auto stage = [&](auto& owner, const auto& state, auto& output) {
                auto next = owner.prepareNativeTick(state, id<ServerTick>(tick), 1.f/30, {}, &world);
                require(next && next->commit([&](auto bytes) {
                    output.assign(bytes.begin(), bytes.end());
                    return CanonicalDurabilityResult::Rejected;
                }) == CanonicalDurabilityResult::Rejected, "Demand comparison did not stage a tick");
            };
            stage(single.service(), solo, oneImage);
            stage(service, authority, twoImage);
            const auto one = readActorCampaign({reinterpret_cast<const char*>(oneImage.data()), oneImage.size()});
            const auto two = readActorCampaign({reinterpret_cast<const char*>(twoImage.data()), twoImage.size()});
            require(std::ranges::equal(one.actor, two.actor) && image(service) == initial,
                "Overlapping player demand added an actor simulation step or leaked preparation");
        }
        const auto swing = [&](auto& owner) {
            const auto view = owner.projectCombat(authority, id<SessionId>(1), id<ServerTick>(tick), id<CanonicalRevision>(tick));
            const auto target = std::ranges::find(view->actors(), id<ActorId>(first), &ActorCombatSnapshot::actorId);
            require(target != view->actors().end(), "Cross-cell melee target omitted");
            const auto* player = authority.findPlayer(id<PlayerId>(1));
            ClientMeleeAttackCommand attack{id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(2), id<CanonicalRevision>(tick), id<ActorId>(first), id<ServerTick>(tick),
                view->selfCombatRevision(), target->combatRevision, MeleeAttackType::Chop, 1.f};
            ServerCommandProposal request(id<SessionId>(1), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(2), id<CanonicalRevision>(tick),
                EntityPrecondition(player->entityId(), player->entityRevision(), player->authorityEpoch()), MeleeAttackCommandProposal(attack));
            auto command = owner.prepareMeleeAttack(authority, request, id<ServerTick>(tick));
            require(bool(command), "Adjacent-cell melee intent rejected");
            return command;
        };
        auto pending = service.prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, swing(service), &world);
        require(pending && pending->commit([](auto) { return CanonicalDurabilityResult::Rejected; }) == CanonicalDurabilityResult::Rejected
            && image(service) == initial, "Rejected exploration tick mutated canonical state");
        require(pending->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
            "Exploration retry failed");
        auto committed = image(service);
        InventoryHost replay(descriptor, manifest, *registry, *crypto, initial);
        replay.service().synchronizeCells(authority);
        auto once = replay.service().prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, swing(replay.service()), &world);
        require(once && once->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed
            && image(replay.service()) == committed, "Retry diverged from one independently committed tick");
        for (unsigned step = 0; step < 40; ++step)
        {
            ++tick;
            for (auto* owner : {&service, &replay.service()})
            {
                auto next = owner->prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, {}, &world);
                require(next && next->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed,
                    "Cross-cell swing continuation failed");
            }
            require(image(service) == image(replay.service()), "Cross-cell hit replay duplicated or diverged");
        }
        committed = image(service);
        const auto outcome = readActorCampaign({reinterpret_cast<const char*>(committed.data()), committed.size()});
        require(outcome.combat && outcome.combat->actors[2][8][2] < 1000,
            "Accepted adjacent-cell swing did not commit damage");
        ++tick; occupy(3, 20);
        require(baseline(1)->equipment->motions.size() == 2 && baseline(2)->equipment->motions.size() == 1,
            "Separated areas leaked or lost actors");
        require(image(service) == committed, "Separating players changed durable state");
        const auto frozen = native.actorCollisionScene().forActor(first).selectedImage();
        ++tick; occupy(9, 20);
        auto quiet = service.prepareNativeTick(authority, id<ServerTick>(tick), 1.f/30, {}, &world);
        require(quiet && quiet->commit([](auto) { return CanonicalDurabilityResult::Committed; }) == CanonicalDurabilityResult::Committed
            && native.actorCollisionScene().forActor(first).selectedImage() == frozen, "Unused area advanced an actor");
        const auto saved = image(service);
        ++tick; occupy(9, 20, false);
        require(native.activeActorCollisionBodies() == 0 && image(service) == saved, "Disconnect failed to release resources or changed state");
        InventoryHost restarted(descriptor, manifest, *registry, *crypto, saved);
        require(image(restarted.service()) == saved, "Restart changed exploration lives/inventories/outcomes");
        ++tick; occupy(3, 3);
        restarted.service().synchronizeCells(authority);
        require(image(service) == saved && image(restarted.service()) == saved
            && baseline(1)->equipment->motions.front().placement == identity, "Regroup/reentry duplicated or reset actors");
        require(std::ranges::equal(baseline(1)->playerInventory.front().stacks, inventory.front().stacks,
            [](const auto& a, const auto& b) { return a.stackId == b.stackId && a.prototypeId == b.prototypeId && a.count == b.count; }),
            "Exploration duplicated or replaced inventory items");
        std::cout << "PASS exploration-host: synthetic adjacent combat/visibility, split/regroup, resource release, one writer, rollback/retry, unload/reconnect/restart\n";
    }
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 4, "Usage: exploration_tests NEW_SCRATCH REGION_FIXTURE_CONFIG SETTINGS");
        check(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL exploration-host: " << error.what() << '\n'; return 1; }
}
