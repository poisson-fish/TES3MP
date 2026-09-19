#ifndef TES3MP_SERVER_NATIVE_ENVIRONMENT_SERVICE_HPP
#define TES3MP_SERVER_NATIVE_ENVIRONMENT_SERVICE_HPP
#include <tes3mp/world_state.hpp>
namespace TES3MP::ServerApp
{
    // Owned boundary. Preparation reads a committed engine image and returns a
    // detached candidate; the existing world transaction installs/publishes it.
    class NativeEnvironmentService
    {
    public:
        virtual ~NativeEnvironmentService() = default;
        virtual CanonicalWorldState initialize(const CanonicalWorldState& base) const = 0;
        virtual void validate(const CanonicalWorldState& world) const = 0;
        virtual CanonicalWorldState advance(const CanonicalWorldState& world, ServerTick tick,
            unsigned int skippedHours = 0) const = 0;
    };
}
#endif
