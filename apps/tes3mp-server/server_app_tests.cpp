#include "server_application.hpp"
#include "server_config.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace
{
    void require(bool condition)
    {
        if (!condition)
            std::abort();
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition))

    constexpr std::string_view validConfig =
        "bind_address = 127.0.0.1\nport = 25565\ntick_interval_ms = 16\n"
        "disconnect_grace_ms = 30000\njoin_password_file = password.txt\n";

    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            calls += 'L';
            if (rejectListen) return { TES3MP::TransportResult::AtCapacity, std::nullopt };
            return { TES3MP::TransportResult::Accepted, TES3MP::ListenerId::initial() };
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override
        { calls += 'S'; return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        { return { TES3MP::TransportResult::InvalidInput, std::nullopt }; }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportResult send(TES3MP::TransportConnectionId, TES3MP::TransportChannel,
            std::span<const std::byte>) override { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage>) override
        { return { TES3MP::TransportResult::UnknownId, 0 }; }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent>) override
        { calls += 'P'; return { pollResult, 0 }; }
        TES3MP::TransportResult shutdown() override
        { calls += 'X'; return TES3MP::TransportResult::Accepted; }

        bool rejectListen = false;
        TES3MP::TransportResult pollResult = TES3MP::TransportResult::Accepted;
        std::string calls;
    };

    TES3MP::ServerApp::ServerConfig parsedConfig()
    {
        auto result = TES3MP::ServerApp::parseServerConfig(validConfig);
        assert(std::holds_alternative<TES3MP::ServerApp::ServerConfig>(result));
        return std::get<TES3MP::ServerApp::ServerConfig>(std::move(result));
    }
}

int main()
{
    using namespace TES3MP::ServerApp;
    {
        auto result = parseServerConfig(validConfig);
        assert(std::holds_alternative<ServerConfig>(result));
        const auto& config = std::get<ServerConfig>(result);
        assert(config.endpoint.address() == "127.0.0.1" && config.endpoint.port() == 25565);
        assert(config.tickIntervalMilliseconds == 16 && config.disconnectGraceMilliseconds == 30000);
    }
    for (const auto invalid : { std::string{}, std::string("unknown = x\n"),
             std::string(validConfig) + "port = 2\n",
             std::string("bind_address = host\nport = 1\ntick_interval_ms = 1\n"
                         "disconnect_grace_ms = 0\njoin_password_file = p\n"),
             std::string("bind_address = 127.0.0.1 # no inline comment\nport = 1\n"
                         "tick_interval_ms = 1\ndisconnect_grace_ms = 0\njoin_password_file = p\n") })
        assert(std::holds_alternative<ConfigError>(parseServerConfig(invalid)));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigLineBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string("\xc0\x80", 2))));

    const auto temporary = std::filesystem::temp_directory_path() / "tes3mp-server-password-test";
    { std::ofstream stream(temporary, std::ios::binary); stream << "secret\r\n"; }
    auto password = loadJoinPassword(temporary);
    assert(std::holds_alternative<TES3MP::AuthenticationMaterial>(password));
    assert(std::get<TES3MP::AuthenticationMaterial>(password).size() == 6);
    std::filesystem::remove(temporary);
    assert(std::holds_alternative<ConfigError>(loadJoinPassword(temporary)));

    auto config = parsedConfig();
    FakeRuntime runtime;
    ServerApplication application(runtime, config);
    assert(application.start() && application.pump() && application.stop());
    assert(runtime.calls == "LPSX");
    assert(application.stop() && runtime.calls == "LPSX");

    FakeRuntime rejected;
    rejected.rejectListen = true;
    ServerApplication rejectedApplication(rejected, config);
    assert(!rejectedApplication.start());
    assert(rejected.calls == "LX");

    FakeRuntime failed;
    ServerApplication failedApplication(failed, config);
    assert(failedApplication.start());
    failed.pollResult = TES3MP::TransportResult::RuntimeFailed;
    assert(!failedApplication.pump());
    assert(failed.calls == "LPSX");
}
