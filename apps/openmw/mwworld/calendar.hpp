#ifndef OPENMW_MWWORLD_CALENDAR_HPP
#define OPENMW_MWWORLD_CALENDAR_HPP
#include "globalvariablename.hpp"
namespace ESM { struct EpochTimeStamp; }
namespace MWWorld
{
    class Globals;
    class TimeStamp;
    // Engine calendar without player, UI, audio or Environment dependencies.
    // DateTimeManager and the authoritative server both use these operations.
    class Calendar
    {
    public:
        void setup(Globals& globals);
        void advanceTime(double hours, Globals& globals);
        void updateGlobalInt(GlobalVariableName name, int value);
        void updateGlobalFloat(GlobalVariableName name, float value);
        TimeStamp getTimeStamp() const;
        ESM::EpochTimeStamp getEpochTimeStamp() const;
        double getGameTime() const { return (static_cast<double>(mDaysPassed) * 24 + mGameHour) * 3600.0; }
        float getGameTimeScale() const { return mGameTimeScale; }
    protected:
        void setHour(double hour);
        void setDay(int day);
        void setMonth(int month);
        int mDaysPassed = 0;
        int mDay = 0;
        int mMonth = 0;
        int mYear = 0;
        float mGameHour = 0.f;
        float mGameTimeScale = 0.f;
    };
}
#endif
