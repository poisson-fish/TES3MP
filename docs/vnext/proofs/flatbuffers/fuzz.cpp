#include "proof_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    vnext::proof::selection::OwnedMotion output;
    vnext::proof::selection::AllocationProbe allocations;
    vnext::proof::selection::decodeV2(std::span(data, size), {}, output, allocations);
    return 0;
}
