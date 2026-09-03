#ifndef OPENMW_TES3MP_ENGINE_COORDINATOR_HPP
#define OPENMW_TES3MP_ENGINE_COORDINATOR_HPP

namespace TES3MP::OpenMWAdapter
{
    class EngineCoordinator
    {
    public:
        virtual ~EngineCoordinator() = default;
        virtual void frame(float frameDurationSeconds) noexcept = 0;
    };
}

#endif
