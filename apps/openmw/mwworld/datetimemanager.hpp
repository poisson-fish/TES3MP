#ifndef GAME_MWWORLD_DATETIMEMANAGER_H
#define GAME_MWWORLD_DATETIMEMANAGER_H

#include <set>
#include <string_view>

#include "calendar.hpp"

namespace ESM
{
    struct EpochTimeStamp;
}

namespace MWWorld
{
    class Globals;
    class TimeStamp;
    class World;

    class DateTimeManager : public Calendar
    {
    public:
        // Game time.
        // Note that game time generally goes faster than the simulation time.
        std::string_view getMonthName(int month = -1) const; // -1: current month
        void setGameTimeScale(float scale); // game time to simulation time ratio

        // Rendering simulation time (summary simulation time of rendering frames since application start).
        double getRenderingSimulationTime() const { return mRenderingSimulationTime; }
        void setRenderingSimulationTime(double t) { mRenderingSimulationTime = t; }

        // World simulation time (the number of seconds passed from the beginning of the game).
        double getSimulationTime() const { return mSimulationTime; }
        void setSimulationTime(double t) { mSimulationTime = t; }
        float getSimulationTimeScale() const { return mSimulationTimeScale; }
        void setSimulationTimeScale(float scale); // simulation time to real time ratio

        // Whether the game is paused in the current frame.
        bool isPaused() const { return mPaused; }

        // Pauses the game starting from the next frame until `unpause` is called with the same tag.
        void pause(std::string_view tag) { mPausedTags.emplace(tag); }
        void unpause(std::string_view tag);
        const std::set<std::string, std::less<>>& getPausedTags() const { return mPausedTags; }

        // Updates mPaused; should be called once a frame.
        void updateIsPaused();

    private:
        friend class World;
        void setup(Globals& globalVariables);

        float mSimulationTimeScale = 1.0;
        double mRenderingSimulationTime = 0.0;
        double mSimulationTime = 0.0;
        bool mPaused = false;
        std::set<std::string, std::less<>> mPausedTags;
    };
}

#endif
