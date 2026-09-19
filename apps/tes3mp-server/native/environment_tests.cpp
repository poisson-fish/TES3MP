#include "environment_tests.hpp"
#include "environment.hpp"
#include "../canonical_persistence_file.hpp"
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/server_command_intake.hpp>
#include <tes3mp/world_time_replication.hpp>
#include <tes3mp/weather_replication.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace TES3MP::Native::Testing
{
    namespace
    {
        void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
        template<class T> T id(uint64_t value) { return T::fromValue(value).value(); }
        template<class F> void rejects(F&& action, const char* message)
        {
            bool rejected = false;
            try { action(); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, message);
        }
        CanonicalWorldState baseWorld()
        {
            const auto globals = GlobalVariableCatalog::create({}).value();
            const auto quests = QuestJournalCatalog::create(testContentManifestId(), {}, {}).value();
            const auto factions = FactionDialogueCatalog::create(testContentManifestId(), {}, {}).value();
            const std::array ids{id<WeatherId>(1)};
            const std::array regions{WeatherRegionCatalogEntry{id<WeatherRegionId>(1), ids[0], 2, 1, {ids[0]}}};
            const auto weather = WeatherCatalog::create(testContentManifestId(), ids, regions).value();
            return CanonicalWorldState::initial({}, globals, quests, factions, weather,
                Xoshiro256StarStar::fromWorldSeed(99, *RandomStreamKey::fromValues(1, 1)).snapshot()).value();
        }
        CanonicalWorldState withImage(const CanonicalWorldState& source, std::vector<std::byte> image)
        {
            auto weather = *source.weather(); weather.nativeEnvironment = std::move(image);
            return CanonicalWorldState::create(source.time(), source.globals(), *source.questJournalCatalog(),
                *source.factionDialogueCatalog(), *source.weatherCatalog(), source.questJournal(), source.factionStates(),
                std::move(weather)).value();
        }
        CanonicalPersistenceIdentity identity()
        {
            std::array<std::byte, 32> bytes{}; bytes[0] = std::byte{1};
            return CanonicalPersistenceIdentity::create(testContentManifestId(),
                ServerConfigurationId::fromBytes(bytes).value(), {}, {}).value();
        }
        CanonicalWorldState roundTrip(const CanonicalWorldState& world)
        {
            auto tick = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1),
                world.time().lastAdvanceTick, {}, {}, CanonicalChecksum(0), nullptr, nullptr, nullptr, nullptr, &world);
            require(tick.has_value(), "Native environment durable tick invalid");
            auto prefix = CanonicalDurablePrefix::create(identity(), {*tick}).value();
            auto bytes = encodeCanonicalDurablePrefixV2(prefix);
            auto decoded = decodeCanonicalDurablePrefix(bytes, identity());
            auto* value = std::get_if<CanonicalDurablePrefix>(&decoded);
            require(value && *value->latest()->world() == world, "Native environment durable codec lost fields");
            bytes.pop_back();
            require(!std::holds_alternative<CanonicalDurablePrefix>(decodeCanonicalDurablePrefix(bytes, identity())),
                "Truncated native environment transaction accepted");
            return *value->latest()->world();
        }
    }
    void populateEnvironment(MWWorld::ESMStore& store, int day, int month, int year, float hour, float scale)
    {
        const auto global = [&](const char* name, ESM::Variant value) {
            ESM::Global record; record.blank(); record.mId = ESM::RefId::stringRefId(name); record.mValue = value;
            store.insertStatic(record);
        };
        global("gamehour", ESM::Variant(hour)); global("timescale", ESM::Variant(scale));
        global("day", ESM::Variant(day)); global("month", ESM::Variant(month));
        global("year", ESM::Variant(year)); global("dayspassed", ESM::Variant(42));
        for (int i = 0; i < 3; ++i)
        {
            ESM::Region region; region.blank(); region.mId = ESM::RefId::stringRefId("native_region_" + std::to_string(i));
            region.mData.mProbabilities.fill(0);
            if (i < 2) region.mData.mProbabilities[i ? 8 : 0] = 100;
            else { region.mData.mProbabilities[0] = 30; region.mData.mProbabilities[4] = 70; }
            store.insertStatic(region);
        }
    }
    std::map<std::string, std::string> environmentFallbacks()
    {
        std::map<std::string, std::string> result{{"Weather_Hours_Between_Weather_Changes", "0.01"}};
        for (const auto name : MWWorld::WeatherNames)
            result.emplace("Weather_" + std::string(name) + "_Transition_Delta", ".5");
        return result;
    }
    void checkEnvironment(const std::filesystem::path& scratch, const std::string& filter)
    {
        require(std::filesystem::create_directory(scratch), "Environment scratch already exists");
        auto crypto = makeProductionCredentialCrypto(); require(bool(crypto), "Crypto unavailable");
        MWWorld::ESMStore store;
        populateEnvironment(store);
        auto settings = environmentFallbacks();
        Environment service(store, settings, "fixture", testContentManifestId(), *crypto, 17);
        auto world = service.initialize(baseWorld());
        require(world.time().daysPassed == 42 && world.time().year == 427 && world.time().day == 30,
            "Engine time globals not used");
        if (filter == "environment-clock")
        {
            const auto saved = world;
            auto next = service.advance(world, id<ServerTick>(1));
            require(world == saved && next.time().day == 31 && next.time().month == 0 && next.time().daysPassed == 43,
                "January day 31 or isolated time preparation failed");
            next = service.advance(next, id<ServerTick>(2), 24);
            require(next.time().day == 1 && next.time().month == 1 && next.time().daysPassed == 44, "January rollover failed");
            for (const auto [day, month, nextMonth, nextYear] : {std::array{28,1,2,427}, std::array{31,11,0,428}})
            {
                MWWorld::ESMStore calendarStore; populateEnvironment(calendarStore, day, month);
                Environment calendar(calendarStore, settings, "calendar", testContentManifestId(), *crypto, 17);
                auto result = calendar.advance(calendar.initialize(baseWorld()), id<ServerTick>(1));
                require(result.time().day == 1 && result.time().month == nextMonth && result.time().year == nextYear
                    && result.time().daysPassed == 43, "Engine calendar rollover failed");
            }
            MWWorld::ESMStore pausedStore; populateEnvironment(pausedStore, 12, 7, 500, 13.f, 0.f);
            Environment paused(pausedStore, settings, "paused", testContentManifestId(), *crypto, 17);
            auto stopped = paused.initialize(baseWorld());
            auto ticked = paused.advance(stopped, id<ServerTick>(1));
            require(ticked.time().hour() == 13 && ticked.time().daysPassed == 42, "Zero timescale advanced time");
            auto skipped = paused.advance(ticked, id<ServerTick>(2), 24);
            require(skipped.time().day == 13 && skipped.time().hour() == 13 && skipped.time().daysPassed == 43,
                "Explicit time skip ignored at zero timescale");
            ReliableWorldTimeState wire{id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(1),
                id<CanonicalRevision>(1), service.advance(world, id<ServerTick>(1)).time(), true};
            require(std::get<ReliableWorldTimeState>(decodeReliableWorldTimeState(encodeReliableWorldTimeState(wire))) == wire,
                "Day 31 or elapsed days lost by wire codec");
            (void)roundTrip(next);
            (void)roundTrip(baseWorld()); // Untagged legacy world encoding still round-trips.
            const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
            const std::array oldCapabilities{weatherReplicationCapability(), worldTimeReplicationCapability()};
            const std::array required{weatherReplicationCapability(), worldTimeReplicationCapability(), nativeEnvironmentCapability()};
            const auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, required));
            auto oldOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, oldCapabilities, {}));
            require(std::holds_alternative<SessionRejected>(negotiateClientHello(ClientHello::fromOffer(std::move(oldOffer)), offer)),
                "Native environment accepted a client without elapsed-day/content mapping support");

            require(std::holds_alternative<CanonicalWorldMutationError>(advanceCanonicalWorldTime(world, id<ServerTick>(1), 33))
                && std::holds_alternative<CanonicalWorldMutationError>(advanceCanonicalWorldTimeByHours(world, id<ServerTick>(1), 1)),
                "Legacy clock can mutate native time");
            rejects([&] { service.advance(world, id<ServerTick>(2)); }, "Skipped native tick accepted");
            rejects([&] { service.advance(world, id<ServerTick>(1), 25); }, "Oversized native time skip accepted");
        }
        else if (filter == "environment-weather")
        {
            const auto weatherId = [](int index) { return id<WeatherId>(MWWorld::environmentRecordId(ESM::RefId::stringRefId(MWWorld::WeatherNames[index]))); };
            const auto regionId = [](int index) { return id<WeatherRegionId>(MWWorld::environmentRecordId(ESM::RefId::stringRefId("native_region_" + std::to_string(index)))); };
            require(world.findWeather(regionId(0))->currentWeather == weatherId(0)
                && world.findWeather(regionId(1))->currentWeather == weatherId(8), "Regional probabilities were ignored");
            // Independently account for the stock RNG stream and weighted draw.
            Misc::Rng::Generator stockRandom(17);
            for (const auto& projected : world.weather()->regions)
                for (const auto& record : store.get<ESM::Region>())
                    if (MWWorld::environmentRecordId(record.mId) == projected.region.value())
                    {
                        MWWorld::RegionWeather region(record);
                        require(projected.currentWeather == weatherId(region.getWeather(stockRandom)), "Native RNG/input binding differs from stock region selection");
                    }
            MWWorld::WeatherTransition queued;
            queued.add(4); queued.advance(.5f, [](int) { return .5f; }); queued.add(8);
            require(queued.mCurrentWeather == 0 && queued.mNextWeather == 4 && queued.mQueuedWeather == 8,
                "Stock weather queue was lost");
            queued.advance(2.f, [](int) { return .5f; });
            require(queued.mCurrentWeather == 4 && queued.mNextWeather == 8 && std::abs(queued.mTransitionFactor - .75f) < .00001f,
                "Queued weather did not consume remaining transition time");
            queued.mFastForward = true; queued.advance(0.f, [](int) { return .5f; });
            require(queued.mCurrentWeather == 8 && queued.mNextWeather == -1, "Fast forward did not finish weather");
            auto restored = world;
            bool transitioning = false, changed = false;
            for (uint64_t tick = 1; tick <= 600; ++tick)
            {
                world = service.advance(world, id<ServerTick>(tick));
                Environment restarted(store, settings, "fixture", testContentManifestId(), *crypto, 17);
                restored = restarted.advance(restored, id<ServerTick>(tick));
                require(restored == world, "Restart changed regional weather, clock or RNG continuation");
                if (tick % 71 == 0) restored = roundTrip(restored);
                for (const auto& region : world.weather()->regions)
                {
                    transitioning |= region.currentWeather != region.targetWeather;
                    changed |= region.revision != WeatherRevision::initial();
                }
                require(world.findWeather(regionId(0))->currentWeather == weatherId(0)
                    && world.findWeather(regionId(1))->currentWeather == weatherId(8), "Regions interfered with one another");
            }
            require(transitioning && changed, "Test did not exercise a regional transition");
            auto zeroSettings = settings;
            for (const auto name : MWWorld::WeatherNames)
                zeroSettings.erase("Weather_" + std::string(name) + "_Transition_Delta");
            Environment zeroService(store, zeroSettings, "zero", testContentManifestId(), *crypto, 17);
            auto zeroWorld = zeroService.initialize(baseWorld());
            const auto initialRegions = zeroWorld.weather()->regions;
            for (uint64_t tick = 1; tick <= 120; ++tick) zeroWorld = zeroService.advance(zeroWorld, id<ServerTick>(tick));
            require(zeroWorld.weather()->regions == initialRegions, "Missing/zero stock delta did not freeze weather presentation");
            zeroService.validate(roundTrip(zeroWorld));
            auto fastForwarded = zeroService.advance(zeroWorld, id<ServerTick>(121), 1);
            zeroService.validate(fastForwarded);
            MWWorld::RegionWeather fallback(ESM::RegionWeatherState{-1, std::vector<uint8_t>(10, 0)});
            require(fallback.getWeather(stockRandom) == 0, "Underweight stock probabilities lost clear fallback");

            auto badImage = world.weather()->nativeEnvironment; badImage.back() = std::byte{'x'};
            rejects([&] { service.validate(withImage(world, badImage)); }, "Malformed environment image accepted");
            auto changedSettings = settings; changedSettings["Weather_Clear_Transition_Delta"] = ".7";
            Environment changedService(store, changedSettings, "fixture", testContentManifestId(), *crypto, 17);
            rejects([&] { changedService.validate(world); }, "Changed native fallback accepted on recovery");
            auto invalidSettings = settings; invalidSettings["Weather_Rain_Transition_Delta"] = "nan";
            rejects([&] { Environment invalid(store, invalidSettings, "fixture", testContentManifestId(), *crypto, 17); },
                "Non-finite weather fallback accepted");
            rejects([&] { service.validate(withImage(world, {})); }, "Missing native image silently reset weather");
            auto oversized = *world.weather(); oversized.nativeEnvironment.resize(MaximumNativeEnvironmentBytes + 1);
            require(!CanonicalWorldState::create(world.time(), world.globals(), *world.questJournalCatalog(),
                *world.factionDialogueCatalog(), *world.weatherCatalog(), world.questJournal(), world.factionStates(),
                std::move(oversized)), "Oversized native image accepted");

            require(std::holds_alternative<CanonicalWorldMutationError>(advanceCanonicalWeather(world, id<ServerTick>(601))),
                "Legacy weather can mutate native state");
        }
        else if (filter == "environment-durability")
        {
            NullMetricSink metrics; NullStructuredEventSink events; Observability observability(metrics, events);
            auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
            CanonicalCommandReducer reducer(empty, observability, testContentManifest());
            auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(scratch / "world.bin", identity()));
            struct Port final : CanonicalDurabilityPort
            {
                ServerApp::CanonicalPersistenceFile& file;
                bool reject = true;
                unsigned calls = 0;
                explicit Port(ServerApp::CanonicalPersistenceFile& value) : file(value) {}
                CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& publication,
                    CanonicalRevision revision, std::span<const DurableCommandOrder> commands,
                    const CanonicalInventoryWorld* inventory, const CanonicalCombatWorld* combat,
                    const CanonicalInteractiveObjectWorld* objects, const CanonicalActorWorld* actors,
                    const CanonicalWorldState* world, const CanonicalScriptState* scripts,
                    std::span<const std::byte> native) noexcept override
                {
                    ++calls;
                    if (reject) return CanonicalDurabilityResult::Rejected;
                    return file.commit(publication, revision, commands, inventory, combat, objects, actors, world, scripts, native);
                }
            } port(*file);
            const auto scriptCatalog = ServerScriptStateCatalog::create({}).value();
            auto scriptState = CanonicalScriptState::initial(scriptCatalog).value();
            require(reducer.configureDurability(port, nullptr, nullptr, nullptr, nullptr, &world, &scriptState), "Environment durability composition failed");
            struct Clock final : MonotonicClock { MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); } } clock;
            ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), id<ServerTick>(1), IngressOrdinal::initial(), TickEpoch::NextTick);
            auto batches = intake.pump();
            require(batches && batches.batches().size() == 1, "Environment tick scheduling failed");
            auto pending = reducer.prepareTick(batches.batches().front());
            CanonicalCommandWorlds domains; domains.world = &world; domains.scriptState = &scriptState;
            const auto before = world;
            const auto publication = reducer.latestPublication();
            const auto candidate = service.advance(world, id<ServerTick>(1));
            require(reducer.stageSimulationCandidates(pending, nullptr, {}, nullptr, {}, nullptr, {}, &world, candidate), "Environment candidate staging failed");
            require(!reducer.commit(std::move(pending), domains) && world == before && reducer.latestPublication() == publication
                && port.calls == 1 && file->prefix().latest() == nullptr, "Rejected durability leaked native environment state/publication");
            port.reject = false;
            require(bool(reducer.commit(std::move(pending), domains)) && world == candidate, "Durable environment installation failed");
            auto reopened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(scratch / "world.bin", identity()));
            require(reopened->restoredWorld() && *reopened->restoredWorld() == world, "Disk recovery lost native environment");
            require(service.advance(*reopened->restoredWorld(), id<ServerTick>(2)) == service.advance(world, id<ServerTick>(2)),
                "Disk recovery changed RNG/time continuation");
        }
        else throw std::invalid_argument("Unknown environment test filter");
        std::cout << "native environment: " << filter << " passed\n";
    }
    void checkHostedEnvironment(ServerApp::NativeEnvironmentService& service, ServerApp::NativeEnvironmentService& recovered)
    {
        auto world = service.initialize(baseWorld());
        for (uint64_t tick = 1; tick <= 64; ++tick) world = service.advance(world, id<ServerTick>(tick));
        auto restored = roundTrip(world);
        recovered.validate(restored);
        require(service.advance(world, id<ServerTick>(65)) == recovered.advance(restored, id<ServerTick>(65)),
            "V12 host recovery changed environment continuation");
    }
    void checkEnvironmentLoadout(const std::filesystem::path& config)
    {
        const std::string path = config.string();
        const char* args[]{"environment-test", "--config", path.c_str()};
        Loadout loadout(readLoadoutOptions(3, args));
        auto crypto = makeProductionCredentialCrypto();
        Environment service(loadout, testContentManifestId(), *crypto, 17);
        auto world = service.initialize(baseWorld());
        for (uint64_t tick = 1; tick <= 90; ++tick) world = service.advance(world, id<ServerTick>(tick), tick == 1 ? 24 : 0);
        service.validate(roundTrip(world));
        std::cout << "native real loadout regions=" << world.weather()->regions.size() << " day=" << unsigned(world.time().day)
            << " daysPassed=" << *world.time().daysPassed << " persisted and resumed\n";
    }
}
