#include "hit_animation.hpp"
#include <components/sceneutil/animationkeys.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    HitAnimations readHitAnimations(std::span<const SceneUtil::TextKeyMap* const> sources)
    {
        if (sources.size() > MaximumHitSources) throw std::invalid_argument("Native hit source count exceeded");
        for (const auto* keys : sources)
        {
            if (!keys) throw std::invalid_argument("Native hit source missing");
            size_t count = 0;
            for (const auto& [time, key] : *keys)
                if (++count > 4096 || !std::isfinite(time) || time < 0 || time > 3600 || key.size() > 256)
                    throw std::invalid_argument("Native hit text-key bounds exceeded");
        }
        HitAnimations result;
        for (unsigned i = 0; i <= result.ticks.size(); ++i)
        {
            const std::string group = "hit" + std::to_string(i + 1);
            const auto source = std::find_if(sources.rbegin(), sources.rend(), [&](const auto* keys) {
                return keys->hasGroupStart(group);
            });
            if (source == sources.rend()) break;
            if (i == result.ticks.size()) throw std::invalid_argument("Native hit recovery group count exceeded");
            SceneUtil::AnimationKeys range;
            if (!SceneUtil::findAnimationKeys(**source, group, "start", "stop", range))
                throw std::invalid_argument("Native hit recovery clip incomplete");
            const float duration = range.mStop->first - range.mStart->first;
            if (!std::isfinite(duration) || duration < 0 || duration > 60)
                throw std::invalid_argument("Native hit recovery clip duration invalid");
            result.ticks[i] = std::max(1u, unsigned(std::ceil(duration * 30.f)));
            ++result.count;
        }
        return result;
    }
}
