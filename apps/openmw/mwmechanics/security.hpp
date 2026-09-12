#ifndef MWMECHANICS_SECURITY_H
#define MWMECHANICS_SECURITY_H

#include "../mwworld/ptr.hpp"

#include <functional>

namespace MWMechanics
{

    /// @brief implementation of Security skill
    class Security
    {
    public:
        using AttemptInterceptor = std::function<bool(const MWWorld::Ptr&, const MWWorld::Ptr&, bool)>;

        Security(const MWWorld::Ptr& actor);

        static void setAttemptInterceptor(AttemptInterceptor interceptor);
        static void clearAttemptInterceptor();

        void pickLock(const MWWorld::Ptr& lock, const MWWorld::Ptr& lockpick, std::string_view& resultMessage,
            std::string_view& resultSound);
        void probeTrap(const MWWorld::Ptr& trap, const MWWorld::Ptr& probe, std::string_view& resultMessage,
            std::string_view& resultSound);

    private:
        static AttemptInterceptor sAttemptInterceptor;
        float mAgility, mLuck, mSecuritySkill, mFatigueTerm;
        MWWorld::Ptr mActor;
    };

}

#endif
