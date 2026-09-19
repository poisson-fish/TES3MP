#ifndef OPENMW_MWWORLD_REGIONALWEATHER_HPP
#define OPENMW_MWWORLD_REGIONALWEATHER_HPP
#include <components/misc/rng.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/esm3/weatherstate.hpp>
#include <functional>
#include <array>
#include <string_view>
namespace MWWorld
{
    inline constexpr std::array<std::string_view, 10> WeatherNames{
        "Clear", "Cloudy", "Foggy", "Overcast", "Rain", "Thunderstorm", "Ashstorm", "Blight", "Snow", "Blizzard"};
    /// A class for storing a region's weather.
    class RegionWeather
    {
    public:
        explicit RegionWeather(const ESM::Region& region);
        explicit RegionWeather(const ESM::RegionWeatherState& state);

        operator ESM::RegionWeatherState() const;

        void setChances(const std::vector<uint8_t>& chances);
        void setChances(const std::vector<uint8_t>& chances, Misc::Rng::Generator& prng);

        void setWeather(int weatherID);

        int getWeather();
        int getWeather(Misc::Rng::Generator& prng);

    private:
        int mWeather;
        std::vector<uint8_t> mChances;

        void chooseNewWeather(Misc::Rng::Generator& prng);
    };

    inline uint64_t environmentRecordId(ESM::RefId id)
    {
        if (!id.is<ESM::StringRefId>()) throw std::invalid_argument("Native weather requires a TES3 record ID");
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : id.getRefIdString())
        {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            hash = (hash ^ c) * 1099511628211ull;
        }
        return (hash & 0x3fffffffffffffffull) | 0xc000000000000000ull;
    }
    // Shared stock transition/queue rules, independent of rendering and player context.
    struct WeatherTransition
    {
        int mCurrentWeather = 0;
        int mNextWeather = -1;
        int mQueuedWeather = -1;
        float mTransitionFactor = 0;
        bool mFastForward = false;
        void add(int weather);
        void advance(float elapsedRealSeconds, const std::function<float(int)>& deltaFor);
    };
    bool advanceWeatherSelection(float& remainingHours, float elapsedHours, float intervalHours);
}
#endif
