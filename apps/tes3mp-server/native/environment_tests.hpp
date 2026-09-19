#ifndef TES3MP_NATIVE_ENVIRONMENT_TESTS_HPP
#define TES3MP_NATIVE_ENVIRONMENT_TESTS_HPP
#include <filesystem>
#include <map>
#include <string>
namespace TES3MP::ServerApp { class NativeEnvironmentService; }
namespace MWWorld { class ESMStore; }
namespace TES3MP::Native::Testing
{
    void populateEnvironment(MWWorld::ESMStore& store, int day = 30, int month = 0, int year = 427,
        float hour = 23.9999f, float scale = 300.f);
    std::map<std::string, std::string> environmentFallbacks();
    void checkEnvironment(const std::filesystem::path& scratch, const std::string& filter);
    void checkHostedEnvironment(ServerApp::NativeEnvironmentService& service, ServerApp::NativeEnvironmentService& recovered);
    void checkEnvironmentLoadout(const std::filesystem::path& config);
}
#endif
