#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace
{
    using namespace TES3MP;

    bool production_crypto_matches_contract()
    {
        auto crypto = makeProductionCredentialCrypto();
        if (!crypto)
            return false;

        constexpr std::array<std::byte, CredentialDigestBytes> EmptySha256{
            std::byte{ 0xe3 },
            std::byte{ 0xb0 },
            std::byte{ 0xc4 },
            std::byte{ 0x42 },
            std::byte{ 0x98 },
            std::byte{ 0xfc },
            std::byte{ 0x1c },
            std::byte{ 0x14 },
            std::byte{ 0x9a },
            std::byte{ 0xfb },
            std::byte{ 0xf4 },
            std::byte{ 0xc8 },
            std::byte{ 0x99 },
            std::byte{ 0x6f },
            std::byte{ 0xb9 },
            std::byte{ 0x24 },
            std::byte{ 0x27 },
            std::byte{ 0xae },
            std::byte{ 0x41 },
            std::byte{ 0xe4 },
            std::byte{ 0x64 },
            std::byte{ 0x9b },
            std::byte{ 0x93 },
            std::byte{ 0x4c },
            std::byte{ 0xa4 },
            std::byte{ 0x95 },
            std::byte{ 0x99 },
            std::byte{ 0x1b },
            std::byte{ 0x78 },
            std::byte{ 0x52 },
            std::byte{ 0xb8 },
            std::byte{ 0x55 },
        };
        CredentialDigest digest;
        if (!crypto->sha256({}, digest) || digest.bytes != EmptySha256)
            return false;

        std::array<std::byte, ResumeTokenBytes> random{};
        random.fill(std::byte{ 0xa5 });
        if (!crypto->randomBytes(random)
            || std::all_of(random.begin(), random.end(), [](std::byte value) { return value == std::byte{ 0xa5 }; }))
            return false;

        auto changed = random;
        changed.back() ^= std::byte{ 1 };
        return crypto->constantTimeEqual(random, random) && !crypto->constantTimeEqual(random, changed)
            && !crypto->constantTimeEqual(random, std::span<const std::byte>(changed).first(changed.size() - 1))
            && crypto->constantTimeEqual({}, {});
    }
}

int main()
{
    return production_crypto_matches_contract() ? 0 : 1;
}
