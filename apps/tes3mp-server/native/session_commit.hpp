#ifndef TES3MP_NATIVE_SESSION_COMMIT_H
#define TES3MP_NATIVE_SESSION_COMMIT_H

#include "persistence.hpp"
#include <span>

namespace TES3MP::Native
{
    // Trusted serialized application service. The image contains both actors
    // and their shared container, already encoded with the engine field codecs.
    // A production implementation must persist this image AND its command
    // acknowledgment in one durable transaction. Accepted authorizes install;
    // Rejected preserves live state; Uncertain closes the runtime.
    // The span is borrowed for this call only. No callbacks into gameplay,
    // reentry, or externally visible success here.
    class EquipmentSessionCommitter
    {
    public:
        virtual ~EquipmentSessionCommitter() = default;
        virtual PersistenceResult commit(std::span<const char> image) noexcept = 0;
    };
}
#endif
