#ifndef OPENMW_MWMECHANICS_CRIMERESULT_H
#define OPENMW_MWMECHANICS_CRIMERESULT_H

namespace MWMechanics
{
    // Keep the reported assault bounty bound to the winning OpenMW game setting.
    // The detached server actor tick and the ordinary MechanicsManager use the
    // same result, without giving the server a second crime tariff.
    template <class GameSettings>
    int reportedAssaultBounty(const GameSettings& settings)
    {
        return settings.find("iCrimeAttack")->mValue.getInteger();
    }
}

#endif
