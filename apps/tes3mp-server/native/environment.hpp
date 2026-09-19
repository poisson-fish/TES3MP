#ifndef TES3MP_NATIVE_ENVIRONMENT_HPP
#define TES3MP_NATIVE_ENVIRONMENT_HPP
#include "../native_environment_service.hpp"
#include "loadout.hpp"
#include <tes3mp/server_authentication.hpp>
#include <apps/openmw/mwworld/calendar.hpp>
#include <apps/openmw/mwworld/globals.hpp>
#include <apps/openmw/mwworld/regionalweather.hpp>
namespace TES3MP::Native
{
    class Environment final : public ServerApp::NativeEnvironmentService
    {
        struct State
        {
            MWWorld::Globals globals;
            MWWorld::Calendar calendar;
            Misc::Rng::Generator random;
            float selectionHours = 0;
            std::vector<MWWorld::WeatherTransition> regions;
        };
        MWWorld::Globals mGlobals;
        std::vector<ESM::Region> mRegions;
        std::array<float, 10> mDeltas;
        std::vector<WeatherId> mWeather;
        std::optional<WeatherCatalog> mCatalog;
        float mInterval;
        uint32_t mSeed;
        std::string mIdentity;
        State initial() const;
        State decode(const CanonicalWorldState& world) const;
        std::vector<std::byte> encode(const State& state) const;
        CanonicalWorldState project(const CanonicalWorldState& base, const State& state,
            ServerTick tick, bool initializing) const;
    public:
        Environment(const Loadout& loadout, ContentManifestId manifest, CredentialCrypto& crypto, uint32_t seed);
        Environment(const MWWorld::ESMStore& store, const std::map<std::string, std::string>& fallbacks,
            std::string contentIdentity, ContentManifestId manifest, CredentialCrypto& crypto, uint32_t seed);
        CanonicalWorldState initialize(const CanonicalWorldState& base) const override;
        void validate(const CanonicalWorldState& world) const override;
        CanonicalWorldState advance(const CanonicalWorldState& world, ServerTick tick,
            unsigned int skippedHours = 0) const override;
    };
}
#endif
