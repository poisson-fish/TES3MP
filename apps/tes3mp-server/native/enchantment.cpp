#include "loadout.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    void Loadout::writeEnchantmentProbe(std::ostream& output, std::string_view enchantmentId) const
    {
        if (enchantmentId.empty() || enchantmentId.size() > 256)
            throw std::runtime_error("Enchantment probe expects a 1..256 byte ID");
        for (unsigned char ch : enchantmentId)
            if (ch < 32 || ch == 127)
                throw std::runtime_error("Enchantment probe ID contains a control character");
        const auto& enchantment = *mStore.get<ESM::Enchantment>().find(ESM::RefId::stringRefId(enchantmentId));
        const auto& data = enchantment.mData;
        if (data.mType < ESM::Enchantment::CastOnce || data.mType > ESM::Enchantment::ConstantEffect)
            throw std::runtime_error("Enchantment probe invalid enchantment type");
        if (enchantment.mEffects.mList.size() > 32)
            throw std::runtime_error("Enchantment probe exceeds 32 effects limit");
        const bool autocalc = data.mFlags & ESM::Enchantment::Autocalc;
        float multiplier = 0;
        int chargeMultiplier = 0;
        if (autocalc)
        {
            const auto bounded = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1e6; };
            multiplier = mStore.get<ESM::GameSetting>().find("fEffectCostMult")->mValue.getFloat();
            if (!bounded(multiplier))
                throw std::runtime_error("Enchantment probe invalid fEffectCostMult");
            // Validate before engine integer additions and float-to-int conversion.
            // These are conservative diagnostic limits, not new gameplay formulas.
            for (const auto& effect : enchantment.mEffects.mList)
            {
                const auto& value = effect.mData;
                if (value.mRange < ESM::RT_Self || value.mRange > ESM::RT_Target || !bounded(value.mArea)
                    || !bounded(value.mDuration) || !bounded(value.mMagnMin) || !bounded(value.mMagnMax))
                    throw std::runtime_error("Enchantment probe invalid effect fields");
                const auto& magicEffect = *mStore.get<ESM::MagicEffect>().find(value.mEffectID);
                if (!bounded(magicEffect.mData.mBaseCost))
                    throw std::runtime_error("Enchantment probe invalid magic effect base cost");
            }
            constexpr std::array settings{ "iMagicItemChargeOnce", "iMagicItemChargeStrike", "iMagicItemChargeUse",
                "iMagicItemChargeConst" };
            chargeMultiplier = mStore.get<ESM::GameSetting>().find(settings[data.mType])->mValue.getInteger();
            if (chargeMultiplier < 0)
                throw std::runtime_error("Enchantment probe invalid charge multiplier");
        }
        else if (data.mCost < 0 || data.mCharge < 0)
            throw std::runtime_error("Enchantment probe invalid stored cost/charge");

        const float cost = MWMechanics::getEnchantmentCastCost(enchantment, mStore);
        if (autocalc)
        {
            // Use the float rounding performed by the engine, then widen for the
            // range check: float(INT_MAX) itself rounds up beyond the int range.
            const double rounded = std::round(cost);
            if (!std::isfinite(rounded) || rounded < 0 || rounded > std::numeric_limits<int>::max()
                || rounded * chargeMultiplier > std::numeric_limits<int>::max())
                throw std::runtime_error("Enchantment probe cost/charge exceeds integer range");
        }
        const int charge = MWMechanics::getEnchantmentCharge(enchantment, mStore);

        // Every variable-size field is bounded above, so this report is bounded
        // before allocation (under 2 KiB, including escaped ID and numeric fields).
        // No publication until all validation and engine calculations succeed.
        std::ostringstream prepared;
        prepared.imbue(std::locale::classic());
        prepared << std::setprecision(std::numeric_limits<float>::max_digits10);
        prepared << "native-enchantment-probe\t1\nid\t" << std::quoted(Misc::StringUtils::lowerCase(enchantmentId))
                 << "\ntype\t" << data.mType << "\nautocalc\t" << autocalc << "\neffects\t"
                 << enchantment.mEffects.mList.size() << "\nstored-cost\t" << data.mCost << "\nstored-charge\t"
                 << data.mCharge << '\n';
        if (autocalc)
            prepared << "fEffectCostMult\t" << multiplier << "\ncharge-multiplier\t" << chargeMultiplier << '\n';
        prepared << "cast-cost-before-skill\t" << cost << "\nmaximum-charge\t" << charge << "\ncomplete\n";
        const auto report = prepared.str();
        output.write(report.data(), static_cast<std::streamsize>(report.size()));
        if (!output)
            throw std::runtime_error("Failed writing native enchantment probe");
    }
}
