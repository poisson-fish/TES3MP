#ifndef TES3MP_SERVER_CONFIG_HPP
#define TES3MP_SERVER_CONFIG_HPP

#include <tes3mp/authentication.hpp>
#include <tes3mp/transport.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumConfigBytes = 4096;
    inline constexpr std::size_t MaximumConfigLineBytes = 512;
    inline constexpr std::size_t MaximumPasswordPathBytes = 1024;

    enum class ConfigErrorCode : std::uint8_t
    {
        Empty,
        TooLarge,
        InvalidUtf8,
        LineTooLong,
        MalformedAssignment,
        UnknownKey,
        DuplicateKey,
        MissingKey,
        InvalidValue,
        PasswordFileUnavailable,
        PasswordFileTooLarge,
        InvalidPassword,
    };

    struct ConfigError
    {
        ConfigErrorCode code = ConfigErrorCode::Empty;
        std::size_t line = 0;
        std::string key;
    };

    struct ServerConfig
    {
        ListenerEndpoint endpoint;
        std::uint64_t tickIntervalMilliseconds = 0;
        std::uint64_t disconnectGraceMilliseconds = 0;
        std::filesystem::path joinPasswordFile;
    };

    using ConfigParseResult = std::variant<ServerConfig, ConfigError>;
    using PasswordLoadResult = std::variant<AuthenticationMaterial, ConfigError>;

    ConfigParseResult parseServerConfig(std::string_view text);
    PasswordLoadResult loadJoinPassword(const std::filesystem::path& path);
    std::string describeConfigError(const ConfigError& error);
}

#endif
