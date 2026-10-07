#ifndef TES3MP_AMBIENT_REPLICATION_HPP
#define TES3MP_AMBIENT_REPLICATION_HPP

#include "canonical_state.hpp"
#include <span>
#include <string>
#include <vector>

namespace TES3MP
{
    // Transient output from one committed context-free script invocation.
    // It is never included in a baseline or retained across session generations.
    struct ReliableAmbientBatch
    {
        static constexpr size_t MaximumMessages = 8;
        static constexpr size_t MaximumMessageBytes = 256;
        static constexpr size_t MaximumScriptBytes = 128;
        static constexpr size_t MaximumPayloadBytes = 2300;
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        uint64_t invocation;
        std::string script;
        std::vector<std::string> messages;
        friend bool operator==(const ReliableAmbientBatch&, const ReliableAmbientBatch&) = default;
    };
    bool validAmbientBatch(const ReliableAmbientBatch&) noexcept;
    std::vector<std::byte> encodeReliableAmbientBatch(const ReliableAmbientBatch&);
    std::optional<ReliableAmbientBatch> decodeReliableAmbientBatch(std::span<const std::byte>);
}
#endif
