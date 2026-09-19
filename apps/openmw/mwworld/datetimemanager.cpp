#include "datetimemanager.hpp"

#include <components/l10n/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "globals.hpp"

namespace MWWorld
{
    void DateTimeManager::setup(Globals& globalVariables)
    {
        Calendar::setup(globalVariables);
        setSimulationTimeScale(1.0);
        mPaused = false;
        mPausedTags.clear();
    }

    void DateTimeManager::setGameTimeScale(float scale)
    {
        MWBase::Environment::get().getWorld()->setGlobalFloat(MWWorld::Globals::sTimeScale, scale);
    }

    static std::vector<std::string> getMonthNames()
    {
        auto calendarL10n = MWBase::Environment::get().getL10nManager()->getContext("Calendar");
        std::string prefix = "month";
        std::vector<std::string> months;
        int count = 12;
        months.reserve(count);
        for (int i = 1; i <= count; ++i)
            months.push_back(calendarL10n->formatMessage(prefix + std::to_string(i), {}, {}));
        return months;
    }

    std::string_view DateTimeManager::getMonthName(int month) const
    {
        static std::vector<std::string> months = getMonthNames();

        if (month == -1)
            month = mMonth;
        if (month < 0 || month >= static_cast<int>(months.size()))
            return {};
        else
            return months[month];
    }

    void DateTimeManager::setSimulationTimeScale(float scale)
    {
        mSimulationTimeScale = std::max(0.f, scale);
        MWBase::Environment::get().getSoundManager()->setSimulationTimeScale(mSimulationTimeScale);
    }

    void DateTimeManager::unpause(std::string_view tag)
    {
        auto it = mPausedTags.find(tag);
        if (it != mPausedTags.end())
            mPausedTags.erase(it);
    }

    void DateTimeManager::updateIsPaused()
    {
        auto stateManager = MWBase::Environment::get().getStateManager();
        auto wm = MWBase::Environment::get().getWindowManager();
        mPaused = !mPausedTags.empty() || wm->isConsoleMode() || wm->isPostProcessorHudVisible()
            || wm->isInteractiveMessageBoxActive() || stateManager->getState() == MWBase::StateManager::State_NoGame;
    }
}
