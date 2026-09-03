#ifndef OPENMW_TES3MP_PROVIDERS_HPP
#define OPENMW_TES3MP_PROVIDERS_HPP

#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_exchange.hpp>

#include <optional>
#include <span>

namespace TES3MP::OpenMWAdapter
{
    class SemanticInputProvider
    {
    public:
        virtual ~SemanticInputProvider() = default;
        virtual std::optional<PlayerMotionIntent> sampleCurrentIntent() noexcept = 0;
    };

    class PresentationProvider
    {
    public:
        virtual ~PresentationProvider() = default;
        virtual void applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers) noexcept = 0;
    };
}

#endif
