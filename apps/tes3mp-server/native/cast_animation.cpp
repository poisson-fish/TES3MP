#include "cast_animation.hpp"
#include <components/sceneutil/animationkeys.hpp>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    std::array<CastAnimation, 3> readCastAnimations(const SceneUtil::TextKeyMap& keys)
    {
        size_t count = 0;
        for (const auto& [time, key] : keys)
            if (++count > 4096 || !std::isfinite(time) || time < 0 || time > 3600 || key.size() > 256)
                throw std::invalid_argument("Native cast text-key bounds exceeded");
        std::array<CastAnimation, 3> result;
        const std::array<std::string, 3> names{"self", "touch", "target"};
        for (size_t i = 0; i < names.size(); ++i)
        {
            SceneUtil::AnimationKeys range;
            if (!SceneUtil::findAnimationKeys(keys, "spellcast", names[i] + " start", names[i] + " stop", range))
                throw std::invalid_argument("Native cast animation range missing");
            float release = -1;
            for (auto it = keys.lowerBound(range.mStart->first); it != keys.end() && it->first <= range.mStop->first; ++it)
                if (it->second == "spellcast: " + names[i] + " release")
                {
                    if (release >= 0) throw std::invalid_argument("Native cast release key ambiguous");
                    release = it->first;
                }
            const float start = range.mStart->first, stop = range.mStop->first;
            if (release <= start || release >= stop || stop - start > 60)
                throw std::invalid_argument("Native cast animation timing invalid");
            result[i] = {uint32_t(std::ceil((release - start) * 30.f)), uint32_t(std::ceil((stop - start) * 30.f))};
            if (result[i].stopTicks <= result[i].releaseTicks)
                throw std::invalid_argument("Native cast recovery is shorter than one tick");
        }
        return result;
    }
}
