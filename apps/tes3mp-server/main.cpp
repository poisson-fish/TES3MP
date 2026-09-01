#include "server_application.hpp"
#include "server_config.hpp"

#include <tes3mp/transport_gns.hpp>

#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <variant>

namespace
{
    volatile std::sig_atomic_t stopRequested = 0;
    void requestStop(int) { stopRequested = 1; }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: tes3mp_server <config-file>\n";
        return 2;
    }
    std::ifstream configStream(argv[1], std::ios::binary);
    if (!configStream)
    {
        std::cerr << "configuration unavailable\n";
        return 2;
    }
    std::string text;
    text.reserve(TES3MP::ServerApp::MaximumConfigBytes + 1);
    char byte = 0;
    while (configStream.get(byte) && text.size() <= TES3MP::ServerApp::MaximumConfigBytes)
        text.push_back(byte);
    auto parsed = TES3MP::ServerApp::parseServerConfig(text);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&parsed))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto config = std::get<TES3MP::ServerApp::ServerConfig>(std::move(parsed));
    auto password = TES3MP::ServerApp::loadJoinPassword(config.joinPasswordFile);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&password))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto limits = TES3MP::TransportLimits::create(1, 8, 8, 128);
    if (!limits)
    {
        std::cerr << "invalid compiled transport limits\n";
        return 3;
    }
    auto factory = TES3MP::makeGameNetworkingSocketsTransport(*limits);
    if (!factory)
    {
        std::cerr << "transport initialization failed\n";
        return 3;
    }
    auto retainedPassword = std::get<TES3MP::AuthenticationMaterial>(std::move(password));
    (void)retainedPassword;
    TES3MP::ServerApp::ServerApplication application(*factory.runtime, config);
    if (!application.start())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::cout << "server started\n";
    while (stopRequested == 0)
    {
        if (!application.pump())
        {
            std::cerr << application.failure() << '\n';
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config.tickIntervalMilliseconds));
    }
    if (!application.stop())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::cout << "server stopped\n";
    return 0;
}
