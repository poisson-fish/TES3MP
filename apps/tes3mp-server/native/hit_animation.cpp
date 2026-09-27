#include "hit_animation.hpp"
#include <components/sceneutil/animationkeys.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    unsigned KnockoutAnimation::advance(unsigned frame, bool exhausted) const
    {
        if (frame >= stop) throw std::invalid_argument("Native knockout frame outside resource");
        ++frame;
        if (exhausted && loopStop && frame >= loopStop) frame = loopStart;
        // Missing clips/loops retain incapacity until fatigue returns.
        if (exhausted && frame >= stop) frame = stop - 1;
        return frame;
    }

    KnockoutAnimation readKnockoutAnimation(std::span<const SceneUtil::TextKeyMap* const> sources, std::string_view group)
    {
        // Reuse the same bounds and winning-layer rules as hit recovery.
        (void)readHitAnimations(sources);
        const auto source = std::find_if(sources.rbegin(), sources.rend(), [group](const auto* keys) {
            return keys->hasGroupStart(group);
        });
        if (source == sources.rend()) return {};
        SceneUtil::AnimationKeys range;
        if (!SceneUtil::findAnimationKeys(**source, group, "start", "stop", range))
            throw std::invalid_argument("Native knockout clip incomplete");
        const float start = range.mStart->first, stop = range.mStop->first;
        if (stop - start > 60) throw std::invalid_argument("Native knockout duration invalid");
        KnockoutAnimation result;
        result.stop = std::max(1u, unsigned(std::ceil((stop - start) * 30.f)));
        SceneUtil::AnimationKeys loop;
        if (SceneUtil::findAnimationKeys(**source, group, "loop start", "loop stop", loop))
        {
            if (loop.mStart->first < start || loop.mStop->first > stop
                || loop.mStart->first >= loop.mStop->first)
                throw std::invalid_argument("Native knockout loop invalid");
            result.loopStart = unsigned(std::ceil((loop.mStart->first - start) * 30.f));
            result.loopStop = unsigned(std::ceil((loop.mStop->first - start) * 30.f));
            if (result.loopStart >= result.loopStop)
                throw std::invalid_argument("Native knockout loop below tick resolution");
        }
        return result;
    }

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
