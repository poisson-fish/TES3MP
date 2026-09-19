#include "environment.hpp"
#include <apps/openmw/mwworld/timestamp.hpp>
#include <tes3mp/fixed_tick_scheduler.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        constexpr float Seconds = 1.f / ServerTicksPerSecond;
        constexpr auto GlobalNames = std::array{MWWorld::Globals::sGameHour, MWWorld::Globals::sDay,
            MWWorld::Globals::sMonth, MWWorld::Globals::sYear, MWWorld::Globals::sDaysPassed,
            MWWorld::Globals::sTimeScale};
        void require(bool valid, const char* message)
        {
            if (!valid) throw std::invalid_argument(message);
        }
        float setting(const std::map<std::string, std::string>& settings, const std::string& name)
        {
            const auto found = settings.find(name);
            // Fallback::Map::getFloat returns zero for an omitted stock key.
            if (found == settings.end()) return 0.f;
            float value = 0;
            const auto& text = found->second;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
                && std::isfinite(value) && value >= 0 && value <= 10000, "Invalid native weather fallback");
            return value;
        }
        void validateCalendar(const MWWorld::Calendar& calendar)
        {
            const auto time = calendar.getEpochTimeStamp();
            const auto days = calendar.getTimeStamp().getDay();
            constexpr std::array<int, 12> limits{31,28,31,30,31,30,31,31,30,31,30,31};
            require(time.mMonth >= 0 && time.mMonth < 12 && time.mDay >= 1
                && time.mDay <= limits[time.mMonth] && time.mYear >= 0 && time.mYear < 1000000
                && days >= 0 && days < 100000000 && std::isfinite(time.mGameHour)
                && time.mGameHour >= 0 && time.mGameHour < 24 && std::isfinite(calendar.getGameTimeScale())
                && calendar.getGameTimeScale() >= 0 && calendar.getGameTimeScale() <= 1000,
                "Invalid native calendar");
        }
        CanonicalWorldTimeState projectTime(const MWWorld::Calendar& calendar, CanonicalWorldTimeState time)
        {
            validateCalendar(calendar);
            const auto stamp = calendar.getEpochTimeStamp();
            time.daysPassed = static_cast<uint32_t>(calendar.getTimeStamp().getDay());
            time.day = static_cast<uint8_t>(stamp.mDay);
            time.month = static_cast<uint8_t>(stamp.mMonth);
            time.year = stamp.mYear;
            time.millisecondsSinceMidnight = static_cast<uint32_t>(static_cast<double>(stamp.mGameHour) * WorldMillisecondsPerHour);
            time.timeScaleUnits = static_cast<uint32_t>(calendar.getGameTimeScale() * WorldTimeScaleUnitsPerOne);
            time.subMillisecondRemainder = 0;
            return time;
        }
        ServerTick offset(ServerTick tick, uint64_t ticks)
        {
            require(ticks <= MaximumWeatherTimingTicks && tick.value() <= std::numeric_limits<uint64_t>::max() - ticks,
                "Native environment tick overflow");
            const auto value = ServerTick::fromValue(tick.value() + ticks);
            require(value.has_value(), "Invalid native environment tick");
            return *value;
        }
    }

    Environment::Environment(const Loadout& loadout, ContentManifestId manifest, CredentialCrypto& crypto, uint32_t seed)
        : Environment(loadout.store(), loadout.options().mFallbacks, loadout.contentFingerprint(), manifest, crypto, seed) {}

    Environment::Environment(const MWWorld::ESMStore& store, const std::map<std::string, std::string>& fallbacks,
        std::string contentIdentity, ContentManifestId manifest, CredentialCrypto& crypto, uint32_t seed)
        : mInterval(setting(fallbacks, "Weather_Hours_Between_Weather_Changes")), mSeed(seed)
    {
        mGlobals.fill(store);
        std::ostringstream binding;
        binding.imbue(std::locale::classic());
        binding << "openmw-environment-1\n" << contentIdentity << '\n' << seed << '\n'
                << std::setprecision(std::numeric_limits<float>::max_digits10) << mInterval << '\n';
        for (const auto name : GlobalNames)
        {
            const float value = mGlobals[name].getFloat();
            // Validate floats before Calendar's integer-global conversions.
            require(std::isfinite(value) && value >= 0 && value <= 100000000, "Invalid native calendar global");
            binding << value << '\n';
        }
        for (size_t i = 0; i < MWWorld::WeatherNames.size(); ++i)
        {
            mDeltas[i] = setting(fallbacks, "Weather_" + std::string(MWWorld::WeatherNames[i]) + "_Transition_Delta");
            require(mDeltas[i] == 0 || 1.0 / mDeltas[i] * ServerTicksPerSecond < MaximumWeatherTimingTicks,
                "Native weather transition exceeds timing budget");
            mWeather.push_back(*WeatherId::fromValue(MWWorld::environmentRecordId(ESM::RefId::stringRefId(MWWorld::WeatherNames[i]))));
            binding << mDeltas[i] << '\n';
        }
        for (const auto& region : store.get<ESM::Region>())
        {
            require(mRegions.size() < MaximumWeatherRegions, "Native weather region budget exceeded");
            mRegions.push_back(region);
        }
        require(!mRegions.empty(), "Native weather loadout has no regions");
        std::ranges::sort(mRegions, {}, [](const auto& region) { return MWWorld::environmentRecordId(region.mId); });
        auto weatherIds = mWeather;
        std::ranges::sort(weatherIds);
        std::vector<WeatherRegionCatalogEntry> regions;
        for (const auto& region : mRegions)
        {
            const auto id = *WeatherRegionId::fromValue(MWWorld::environmentRecordId(region.mId));
            // Catalog is identity/validation only. Engine REGN probabilities,
            // timer and per-target deltas own selection and transitions.
            regions.push_back({id, mWeather[0], 1, 1, weatherIds});
            binding << region.mId.serializeText() << '\n';
            for (auto chance : region.mData.mProbabilities) binding << unsigned(chance) << ' ';
            binding << '\n';
        }
        mCatalog = WeatherCatalog::create(manifest, weatherIds, regions);
        require(mCatalog.has_value(), "Native weather identity collision");
        auto material = binding.str();
        CredentialDigest digest;
        require(crypto.sha256(std::as_bytes(std::span(material)), digest), "Native environment identity unavailable");
        std::ostringstream identity;
        for (const auto byte : digest.bytes) identity << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(byte);
        mIdentity = identity.str();
        (void)initial();
    }

    Environment::State Environment::initial() const
    {
        State state;
        state.globals = mGlobals;
        state.calendar.setup(state.globals);
        validateCalendar(state.calendar);
        state.random.seed(mSeed);
        state.selectionHours = mInterval;
        for (const auto& region : mRegions)
        {
            MWWorld::RegionWeather selection(region);
            state.regions.push_back({selection.getWeather(state.random)});
        }
        return state;
    }

    std::vector<std::byte> Environment::encode(const State& state) const
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "openmw-environment-1 " << mIdentity << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto name : GlobalNames)
        {
            if (name == MWWorld::Globals::sGameHour || name == MWWorld::Globals::sTimeScale)
                out << state.globals[name].getFloat();
            else out << state.globals[name].getInteger();
            out << ' ';
        }
        out << state.selectionHours << ' ' << Misc::Rng::serialize(state.random) << ' ' << state.regions.size() << '\n';
        for (const auto& region : state.regions)
            out << region.mCurrentWeather << ' ' << region.mNextWeather << ' ' << region.mQueuedWeather << ' '
                << region.mTransitionFactor << '\n';
        const auto value = out.str();
        require(value.size() <= MaximumNativeEnvironmentBytes, "Native environment image too large");
        auto bytes = std::as_bytes(std::span(value));
        return {bytes.begin(), bytes.end()};
    }

    Environment::State Environment::decode(const CanonicalWorldState& world) const
    {
        require(world.weather() && world.weatherCatalog() && *world.weatherCatalog() == *mCatalog,
            "Native environment catalog mismatch");
        const auto& image = world.weather()->nativeEnvironment;
        require(!image.empty() && image.size() <= MaximumNativeEnvironmentBytes, "Missing/oversized native environment image; new campaign required");
        std::istringstream in(std::string(reinterpret_cast<const char*>(image.data()), image.size()));
        in.imbue(std::locale::classic());
        std::string version, identity;
        require(bool(in >> version >> identity) && version == "openmw-environment-1" && identity == mIdentity,
            "Native environment content or settings mismatch");
        State state;
        state.globals = mGlobals;
        for (const auto name : GlobalNames)
        {
            if (name == MWWorld::Globals::sGameHour || name == MWWorld::Globals::sTimeScale)
            {
                float value = 0;
                require(bool(in >> value) && std::isfinite(value) && value >= 0 && value <= 1000,
                    "Invalid saved native calendar float");
                state.globals[name].setFloat(value);
            }
            else
            {
                int value = 0;
                require(bool(in >> value) && value >= 0 && value <= 100000000, "Invalid saved native calendar integer");
                state.globals[name].setInteger(value);
            }
        }
        state.calendar.setup(state.globals);
        validateCalendar(state.calendar);
        uint32_t random = 0;
        size_t count = 0;
        require(bool(in >> state.selectionHours >> random >> count) && std::isfinite(state.selectionHours)
            && state.selectionHours <= mInterval && count == mRegions.size()
            && random >= Misc::Rng::Generator::min() && random <= Misc::Rng::Generator::max(), "Invalid native weather state");
        Misc::Rng::deserialize(std::to_string(random), state.random);
        state.regions.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            MWWorld::WeatherTransition region;
            require(bool(in >> region.mCurrentWeather >> region.mNextWeather >> region.mQueuedWeather >> region.mTransitionFactor)
                && region.mCurrentWeather >= 0 && region.mCurrentWeather < 10
                && region.mNextWeather >= -1 && region.mNextWeather < 10
                && region.mQueuedWeather >= -1 && region.mQueuedWeather < 10
                && std::isfinite(region.mTransitionFactor) && region.mTransitionFactor <= 1.f
                && region.mTransitionFactor >= -10000.f
                && (region.mNextWeather != -1 || region.mQueuedWeather == -1)
                && (region.mNextWeather < 0 || mDeltas[region.mNextWeather] > 0 || region.mTransitionFactor == 1.f), "Invalid native weather transition");
            const auto& projected = world.weather()->regions[i];
            require(projected.currentWeather == mWeather[region.mCurrentWeather]
                && projected.targetWeather == mWeather[region.mNextWeather < 0 || mDeltas[region.mNextWeather] == 0 ? region.mCurrentWeather : region.mNextWeather],
                "Native weather projection mismatch");
            state.regions.push_back(region);
        }
        in >> std::ws;
        require(in.eof() && projectTime(state.calendar, world.time()) == world.time()
            && world.weather()->lastAdvanceTick == world.time().lastAdvanceTick, "Native environment projection mismatch");
        return state;
    }

    CanonicalWorldState Environment::project(const CanonicalWorldState& base, const State& state,
        ServerTick tick, bool initializing) const
    {
        auto time = projectTime(state.calendar, initializing ? CanonicalWorldTimeState{} : base.time());
        if (!initializing)
        {
            const auto revision = WorldTimeRevision::fromValue(time.revision.value() + 1);
            require(revision && revision->value() > time.revision.value(), "Native time revision exhausted");
            time.revision = *revision;
            time.lastChangeTick = tick;
        }
        time.lastAdvanceTick = tick;
        auto weather = base.weather().value_or(CanonicalWeatherState{{},
            Xoshiro256StarStar::fromWorldSeed(mSeed,
                RandomStreamKey::fromValues(0x5745415448455231ULL, 0).value()).snapshot()});
        weather.lastAdvanceTick = tick;
        weather.regions.clear();
        for (size_t i = 0; i < state.regions.size(); ++i)
        {
            const auto& native = state.regions[i];
            const auto current = mWeather[native.mCurrentWeather];
            // A stock zero-delta transition remains at factor 1 forever. Its
            // pending/queued target stays in the engine image; presentation is
            // exactly the current weather until a time skip finishes it.
            const auto target = mWeather[native.mNextWeather < 0 || mDeltas[native.mNextWeather] == 0 ? native.mCurrentWeather : native.mNextWeather];
            CanonicalWeatherRegionState region{mCatalog->regions()[i].id, current, target};
            if (!initializing) region = base.weather()->regions[i];
            // Preserve the original tick anchors between transitions. At a queued
            // handoff, project the engine's remaining real-time transition.
            if (initializing || current != region.currentWeather || target != region.targetWeather)
            {
                region.currentWeather = current;
                region.targetWeather = target;
                region.transitionStartTick = tick;
                region.transitionEndTick = tick;
                if (current != target)
                    region.transitionEndTick = offset(tick, std::max<uint64_t>(1,
                        static_cast<uint64_t>(std::ceil(std::max(0.f, native.mTransitionFactor) / mDeltas[native.mNextWeather] * ServerTicksPerSecond))));
                if (!initializing)
                {
                    const auto revision = WeatherRevision::fromValue(region.revision.value() + 1);
                    require(revision && revision->value() > region.revision.value(), "Native weather revision exhausted");
                    region.revision = *revision;
                    region.lastChangeTick = tick;
                }
                region.nextSelectionTick = region.transitionEndTick;
            }
            // Selection deadlines remain native game-time state. The wire field
            // is informational; it never drives native weather selection.
            weather.regions.push_back(region);
        }
        weather.nativeEnvironment = encode(state);
        auto result = CanonicalWorldState::create(time, base.globals(), *base.questJournalCatalog(),
            *base.factionDialogueCatalog(), *mCatalog, base.questJournal(), base.factionStates(), std::move(weather));
        require(result.has_value(), "Native environment projection invalid");
        return std::move(*result);
    }
    CanonicalWorldState Environment::initialize(const CanonicalWorldState& base) const
    {
        require((!base.weather() || base.weather()->nativeEnvironment.empty())
            && base.time().lastAdvanceTick == ServerTick::initial(),
            "Native environment requires a fresh campaign");
        return project(base, initial(), ServerTick::initial(), true);
    }
    void Environment::validate(const CanonicalWorldState& world) const { (void)decode(world); }
    CanonicalWorldState Environment::advance(const CanonicalWorldState& world, ServerTick tick, unsigned int skippedHours) const
    {
        require(tick > world.time().lastAdvanceTick && tick.value() - world.time().lastAdvanceTick.value() == 1
            && skippedHours <= 24, "Invalid native environment step");
        auto state = decode(world);
        const double hours = static_cast<double>(state.calendar.getGameTimeScale()) / (3600.0 * ServerTicksPerSecond) + skippedHours;
        state.calendar.advanceTime(hours, state.globals);
        if (MWWorld::advanceWeatherSelection(state.selectionHours, static_cast<float>(hours), mInterval))
            for (size_t i = 0; i < state.regions.size(); ++i)
            {
                MWWorld::RegionWeather selection(mRegions[i]);
                state.regions[i].add(selection.getWeather(state.random));
            }
        for (auto& region : state.regions)
        {
            region.mFastForward = skippedHours != 0;
            region.advance(Seconds, [&](int weather) { return mDeltas[weather]; });
        }
        return project(world, state, tick, false);
    }
}
