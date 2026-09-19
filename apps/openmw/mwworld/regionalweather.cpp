#include "regionalweather.hpp"
namespace MWWorld
{
    namespace { constexpr int invalidWeatherID = -1; }
    RegionWeather::RegionWeather(const ESM::Region& region)
        : mWeather(invalidWeatherID)
        , mChances(region.mData.mProbabilities.begin(), region.mData.mProbabilities.end())
    {
    }

    RegionWeather::RegionWeather(const ESM::RegionWeatherState& state)
        : mWeather(state.mWeather)
        , mChances(state.mChances)
    {
    }

    RegionWeather::operator ESM::RegionWeatherState() const
    {
        ESM::RegionWeatherState state = { mWeather, mChances };

        return state;
    }

    void RegionWeather::setChances(const std::vector<uint8_t>& chances, Misc::Rng::Generator& prng)
    {
        mChances = chances;

        // Regional weather no longer supports the current type, select a new weather pattern.
        if ((static_cast<size_t>(mWeather) >= mChances.size()) || (mChances[mWeather] == 0))
        {
            chooseNewWeather(prng);
        }
    }

    void RegionWeather::setWeather(int weatherID)
    {
        mWeather = weatherID;
    }

    int RegionWeather::getWeather(Misc::Rng::Generator& prng)
    {
        // If the region weather was already set (by ChangeWeather, or by a previous call) then just return that value.
        // Note that the region weather will be expired periodically when the weather update timer expires.
        if (mWeather == invalidWeatherID)
        {
            chooseNewWeather(prng);
        }

        return mWeather;
    }

    void RegionWeather::chooseNewWeather(Misc::Rng::Generator& prng)
    {
        // All probabilities must add to 100 (responsibility of the user).
        // If chances A and B has values 30 and 70 then by generating 100 numbers 1..100, 30% will be lesser or equal 30
        // and 70% will be greater than 30 (in theory).
        unsigned int chance = static_cast<unsigned int>(Misc::Rng::rollDice(100, prng) + 1); // 1..100
        unsigned int sum = 0;
        for (size_t i = 0; i < mChances.size(); ++i)
        {
            sum += mChances[i];
            if (chance <= sum)
            {
                mWeather = static_cast<int>(i);
                return;
            }
        }

        // if we hit this path then the chances don't add to 100, choose a default weather instead
        mWeather = 0;
    }

    bool advanceWeatherSelection(float& remainingHours, float elapsedHours, float intervalHours)
    {
        remainingHours -= elapsedHours;
        if (remainingHours > 0.f) return false;
        remainingHours += intervalHours;
        return true;
    }
    void WeatherTransition::add(int weather)
    {
        if (mNextWeather == -1 && weather != mCurrentWeather)
        {
            mNextWeather = weather;
            mTransitionFactor = 1.f;
        }
        else if (mNextWeather != -1 && weather != mNextWeather)
            mQueuedWeather = weather;
    }
    void WeatherTransition::advance(float elapsedRealSeconds, const std::function<float(int)>& deltaFor)
    {
        // When a player chooses to train, wait, or serves jail time, any transitions will be fast forwarded to the last
        // weather type set, regardless of the remaining transition time.
        if (!mFastForward && (mNextWeather != -1))
        {
            const float delta = deltaFor(mNextWeather);
            mTransitionFactor -= elapsedRealSeconds * delta;
            if (mTransitionFactor <= 0.0f)
            {
                mCurrentWeather = mNextWeather;
                mNextWeather = mQueuedWeather;
                mQueuedWeather = -1;

                // We may have begun processing the queued transition, so we need to apply the remaining time towards
                // it.
                if ((mNextWeather != -1))
                {
                    const float newDelta = deltaFor(mNextWeather);
                    const float remainingSeconds = -(mTransitionFactor / delta);
                    mTransitionFactor = 1.0f - (remainingSeconds * newDelta);
                }
                else
                {
                    mTransitionFactor = 0.0f;
                }
            }
        }
        else
        {
            if (mQueuedWeather != -1)
            {
                mCurrentWeather = mQueuedWeather;
            }
            else if (mNextWeather != -1)
            {
                mCurrentWeather = mNextWeather;
            }

            mNextWeather = -1;
            mQueuedWeather = -1;
            mFastForward = false;
        }
    }
}
