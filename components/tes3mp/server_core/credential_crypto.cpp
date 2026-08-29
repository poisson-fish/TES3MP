#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>

#if defined(TES3MP_USE_OPENSSL_CREDENTIAL_CRYPTO)
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace TES3MP
{
#if defined(TES3MP_USE_OPENSSL_CREDENTIAL_CRYPTO)
    namespace
    {
        class OpenSslCredentialCrypto final : public CredentialCrypto
        {
        public:
            bool randomBytes(std::span<std::byte> destination) noexcept override
            {
                if (destination.empty())
                    return true;
                if (destination.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    return false;
                return RAND_bytes(
                           reinterpret_cast<unsigned char*>(destination.data()), static_cast<int>(destination.size()))
                    == 1;
            }

            bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept override
            {
                static_assert(CredentialDigestBytes == SHA256_DIGEST_LENGTH);
                std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
                const unsigned char empty = 0;
                const auto* data = source.empty() ? &empty : reinterpret_cast<const unsigned char*>(source.data());
                if (SHA256(data, source.size(), digest.data()) == nullptr)
                    return false;
                std::transform(digest.begin(), digest.end(), destination.bytes.begin(),
                    [](unsigned char value) { return static_cast<std::byte>(value); });
                OPENSSL_cleanse(digest.data(), digest.size());
                return true;
            }

            bool constantTimeEqual(std::span<const std::byte> left, std::span<const std::byte> right) noexcept override
            {
                if (left.size() != right.size())
                    return false;
                if (left.empty())
                    return true;
                return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
            }
        };
    }
#endif

    std::unique_ptr<CredentialCrypto> makeProductionCredentialCrypto() noexcept
    {
#if defined(TES3MP_USE_OPENSSL_CREDENTIAL_CRYPTO)
        return std::unique_ptr<CredentialCrypto>(new (std::nothrow) OpenSslCredentialCrypto);
#else
        return {};
#endif
    }
}
