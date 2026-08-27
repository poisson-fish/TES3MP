#ifndef TES3MP_AUTHENTICATION_HPP
#define TES3MP_AUTHENTICATION_HPP

#include "session_types.hpp"
#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace TES3MP
{
    inline constexpr std::size_t MaximumAuthenticationMaterialBytes = 256;

    class AuthenticationMaterial
    {
    public:
        static std::optional<AuthenticationMaterial> create(std::span<const std::byte> bytes) noexcept;

        AuthenticationMaterial(const AuthenticationMaterial&) = delete;
        AuthenticationMaterial& operator=(const AuthenticationMaterial&) = delete;
        AuthenticationMaterial(AuthenticationMaterial&& other) noexcept;
        AuthenticationMaterial& operator=(AuthenticationMaterial&& other) noexcept;
        ~AuthenticationMaterial();

        std::size_t size() const noexcept { return mSize; }
        bool empty() const noexcept { return mSize == 0; }

    private:
        friend class AuthenticationProvider;

        AuthenticationMaterial() noexcept = default;
        std::span<const std::byte> secretBytes() const noexcept { return { mBytes.data(), mSize }; }
        void clear() noexcept;

        std::array<std::byte, MaximumAuthenticationMaterialBytes> mBytes{};
        std::size_t mSize = 0;
    };

    struct AuthenticationAttempt
    {
        AuthenticationAttemptId id;
        SessionGeneration generation;

        friend constexpr bool operator==(AuthenticationAttempt, AuthenticationAttempt) noexcept = default;
    };

    struct AuthenticatedPrincipal
    {
        PrincipalId id;

        friend constexpr bool operator==(AuthenticatedPrincipal, AuthenticatedPrincipal) noexcept = default;
    };

    struct AuthenticationRejected
    {
        AuthenticationRejectionReason reason;

        friend constexpr bool operator==(AuthenticationRejected, AuthenticationRejected) noexcept = default;
    };

    using AuthenticationResult = std::variant<AuthenticatedPrincipal, AuthenticationRejected>;

    struct AuthenticationPending
    {
        friend constexpr bool operator==(AuthenticationPending, AuthenticationPending) noexcept = default;
    };

    struct AuthenticationCompletion
    {
        AuthenticationAttempt attempt;
        AuthenticationResult result;

        friend constexpr bool operator==(const AuthenticationCompletion&, const AuthenticationCompletion&) noexcept
            = default;
    };

    using AuthenticationPollResult = std::variant<AuthenticationPending, AuthenticationCompletion>;

    class AuthenticationOperation
    {
    public:
        virtual ~AuthenticationOperation() = default;
        virtual AuthenticationPollResult poll() noexcept = 0;
        virtual void cancel() noexcept = 0;
    };

    class AuthenticationProvider
    {
    public:
        virtual ~AuthenticationProvider() = default;
        virtual std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, AuthenticationMaterial material) noexcept = 0;

    protected:
        static std::span<const std::byte> materialBytes(const AuthenticationMaterial& material) noexcept
        {
            return material.secretBytes();
        }
    };
}

#endif
