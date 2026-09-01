#ifndef TES3MP_ADMISSION_SCOPE_HPP
#define TES3MP_ADMISSION_SCOPE_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace TES3MP
{
    inline constexpr std::size_t AdmissionScopeIdBytes = 32;

    class AdmissionScopeId
    {
    public:
        static std::optional<AdmissionScopeId> create(std::span<const std::byte> bytes) noexcept
        {
            if (bytes.size() != AdmissionScopeIdBytes)
                return std::nullopt;
            AdmissionScopeId result;
            std::copy(bytes.begin(), bytes.end(), result.mBytes.begin());
            return result;
        }

        friend constexpr bool operator==(AdmissionScopeId, AdmissionScopeId) noexcept = default;

    private:
        constexpr AdmissionScopeId() noexcept = default;

        std::array<std::byte, AdmissionScopeIdBytes> mBytes{};
    };
}

#endif
