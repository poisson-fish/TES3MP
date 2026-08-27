#include <tes3mp/authentication.hpp>

#include <algorithm>

namespace TES3MP
{
    std::optional<AuthenticationMaterial> AuthenticationMaterial::create(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() > MaximumAuthenticationMaterialBytes)
            return std::nullopt;
        AuthenticationMaterial result;
        std::copy(bytes.begin(), bytes.end(), result.mBytes.begin());
        result.mSize = bytes.size();
        return result;
    }

    AuthenticationMaterial::AuthenticationMaterial(AuthenticationMaterial&& other) noexcept
        : mBytes(other.mBytes)
        , mSize(other.mSize)
    {
        other.clear();
    }

    AuthenticationMaterial& AuthenticationMaterial::operator=(AuthenticationMaterial&& other) noexcept
    {
        if (this == &other)
            return *this;
        clear();
        mBytes = other.mBytes;
        mSize = other.mSize;
        other.clear();
        return *this;
    }

    AuthenticationMaterial::~AuthenticationMaterial()
    {
        clear();
    }

    void AuthenticationMaterial::clear() noexcept
    {
        volatile std::byte* destination = mBytes.data();
        for (std::size_t index = 0; index < mBytes.size(); ++index)
            destination[index] = std::byte{ 0 };
        mSize = 0;
    }
}
