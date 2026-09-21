#ifndef OPENMW_COMPONENTS_SCENEUTIL_ANIMATIONKEYS_H
#define OPENMW_COMPONENTS_SCENEUTIL_ANIMATIONKEYS_H

#include "textkeymap.hpp"

namespace SceneUtil
{
    // Shared with rendered Animation::reset and detached gameplay playback.
    // Keep reverse selection (including duplicate groups), loop-start fallback,
    // and the engine's tolerance of suffix garbage on stop keys.
    struct AnimationKeys
    {
        decltype(TextKeyMap{}.rbegin()) mGroupEnd, mStart, mStop;
    };

    inline bool findAnimationKeys(const TextKeyMap& keys, std::string_view group,
        std::string_view start, std::string_view stop, AnimationKeys& result)
    {
        auto matches = [group](std::string_view text, std::string_view action, bool prefix = false) {
            if (!text.starts_with(group) || text.substr(group.size(), 2) != ": ") return false;
            const auto suffix = text.substr(group.size() + 2);
            return prefix ? suffix.starts_with(action) : suffix == action;
        };
        auto groupEnd = keys.rbegin();
        while (groupEnd != keys.rend()
            && (!groupEnd->second.starts_with(group) || groupEnd->second.compare(group.size(), 2, ": ") != 0))
            ++groupEnd;
        auto startKey = groupEnd;
        while (startKey != keys.rend() && !matches(startKey->second, start)) ++startKey;
        if (startKey == keys.rend() && start == "loop start")
        {
            startKey = groupEnd;
            while (startKey != keys.rend() && !matches(startKey->second, "start")) ++startKey;
        }
        if (startKey == keys.rend()) return false;
        auto stopKey = groupEnd;
        while (stopKey != keys.rend() && !matches(stopKey->second, stop, true)) ++stopKey;
        if (stopKey == keys.rend() || startKey->first > stopKey->first) return false;
        result = {groupEnd, startKey, stopKey};
        return true;
    }
}

#endif
