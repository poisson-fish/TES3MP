#include <tes3mp/protocol_exchange.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto reliable = TES3MP::decodeReliableOperation(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableOperation>(&reliable))
    {
        const auto normalized = TES3MP::decodeReliableOperation(TES3MP::encodeReliableOperation(*value));
        const auto* normalizedValue = std::get_if<TES3MP::ReliableOperation>(&normalized);
        if (normalizedValue == nullptr || *normalizedValue != *value)
            std::abort();
    }

    const auto latestWins = TES3MP::decodeLatestWinsSnapshot(bytes);
    if (const auto* value = std::get_if<TES3MP::LatestWinsSnapshot>(&latestWins))
    {
        const auto normalized = TES3MP::decodeLatestWinsSnapshot(TES3MP::encodeLatestWinsSnapshot(*value));
        const auto* normalizedValue = std::get_if<TES3MP::LatestWinsSnapshot>(&normalized);
        if (normalizedValue == nullptr || *normalizedValue != *value)
            std::abort();
    }
    const auto observations = TES3MP::decodeReliableObservationBatch(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableObservationBatch>(&observations))
        if (TES3MP::decodeReliableObservationBatch(TES3MP::encodeReliableObservationBatch(*value)) != observations)
            std::abort();
    const auto baseline = TES3MP::decodeReliableInterestBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableInterestBaseline>(&baseline))
        if (TES3MP::decodeReliableInterestBaseline(TES3MP::encodeReliableInterestBaseline(*value)) != baseline)
            std::abort();
    const auto resync = TES3MP::decodeSessionResyncRequest(bytes);
    if (const auto* value = std::get_if<TES3MP::SessionResyncRequest>(&resync))
        if (TES3MP::decodeSessionResyncRequest(TES3MP::encodeSessionResyncRequest(*value)) != resync)
            std::abort();
    return 0;
}
