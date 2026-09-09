#include "client_connection.hpp"

#ifdef TES3MP_OPENMW_HAS_GNS
#include <tes3mp/transport_gns.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        class SteadyClock final : public MonotonicClock
        {
        public:
            MonotonicInstant now() const noexcept override
            {
                const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
                return MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(value));
            }
        };

        std::optional<OutboundQueuePolicy> outboundPolicy()
        {
            return OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
        }

        std::filesystem::path applicationDirectory() noexcept
        try
        {
#ifdef _WIN32
            std::wstring buffer(32768, L'\0');
            const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (size == 0 || size == buffer.size()) return {};
            buffer.resize(size);
            return std::filesystem::path(buffer).parent_path();
#else
            return std::filesystem::current_path();
#endif
        }
        catch (...) { return {}; }

        bool replaceCredentialFile(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
        {
#ifdef _WIN32
            return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                != 0;
#else
            return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
        }

        class TemporaryCredentialFileCleanup
        {
        public:
            explicit TemporaryCredentialFileCleanup(std::filesystem::path path) noexcept
                : mPath(std::move(path))
            {
            }

            void activate() noexcept { mActive = true; }

            ~TemporaryCredentialFileCleanup()
            {
                if (!mActive)
                    return;
                std::error_code ignored;
                std::filesystem::remove(mPath, ignored);
            }

        private:
            std::filesystem::path mPath;
            bool mActive = false;
        };

        struct CredentialBuffer
        {
            ~CredentialBuffer()
            {
                volatile std::byte* destination = bytes.data();
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    destination[index] = std::byte{};
            }
            std::array<std::byte, PlayerCredentialBytes> bytes{};
        };

        class FilePlayerCredentialPersistence final : public PlayerCredentialPersistence
        {
        public:
            explicit FilePlayerCredentialPersistence(std::filesystem::path path)
                : mPath(std::move(path))
            {
            }

            bool store(PlayerCredential credential) noexcept override
            try
            {
                CredentialBuffer buffer;
                if (!credential.copyTo(buffer.bytes))
                    return false;
                auto temporary = mPath;
                temporary += ".tmp";
                TemporaryCredentialFileCleanup cleanup(temporary);
                {
                    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                    if (!stream)
                        return false;
                    cleanup.activate();
#ifndef _WIN32
                    if (::chmod(temporary.c_str(), S_IRUSR | S_IWUSR) != 0)
                        return false;
#endif
                    stream.write(reinterpret_cast<const char*>(buffer.bytes.data()), buffer.bytes.size());
                    stream.flush();
                    if (!stream)
                        return false;
                }
                return replaceCredentialFile(temporary, mPath);
            }
            catch (...)
            {
                return false;
            }

        private:
            std::filesystem::path mPath;
        };

        std::optional<PlayerCredential> loadPlayerCredential(const std::filesystem::path& path) noexcept
        try
        {
            if (path.empty() || !std::filesystem::exists(path))
                return std::nullopt;
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != PlayerCredentialBytes)
                return std::nullopt;
            std::array<std::byte, PlayerCredentialBytes> bytes{};
            std::ifstream stream(path, std::ios::binary);
            stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            auto credential = stream && stream.peek() == std::char_traits<char>::eof() ? PlayerCredential::create(bytes)
                                                                                       : std::nullopt;
            std::fill(bytes.begin(), bytes.end(), std::byte{});
            return credential;
        }
        catch (...)
        {
            return std::nullopt;
        }

        std::optional<std::uint16_t> parsePort(std::string_view text) noexcept
        {
            unsigned value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0
                || value > (std::numeric_limits<std::uint16_t>::max)())
                return std::nullopt;
            return static_cast<std::uint16_t>(value);
        }

        std::string credentialFileName(const ConnectionEndpoint& endpoint)
        {
            std::uint64_t hashA = 1469598103934665603ull;
            std::uint64_t hashB = 1099511628211ull;
            for (const unsigned char value : endpoint.host())
            {
                hashA = (hashA ^ value) * 1099511628211ull;
                hashB = (hashB ^ (value + 0x9du)) * 14029467366897019727ull;
            }
            hashA = (hashA ^ endpoint.port()) * 1099511628211ull;
            hashB = (hashB ^ endpoint.port()) * 14029467366897019727ull;
            constexpr char digits[] = "0123456789abcdef";
            std::string suffix(32, '0');
            for (std::size_t index = 0; index < 16; ++index)
            {
                const auto value = index < 8 ? hashA : hashB;
                const auto shift = static_cast<unsigned>((index % 8) * 8);
                const auto byte = static_cast<unsigned char>(value >> shift);
                suffix[index * 2] = digits[byte >> 4u];
                suffix[index * 2 + 1] = digits[byte & 0x0fu];
            }
            std::string encoded;
            encoded.reserve(std::min<std::size_t>(endpoint.host().size(), 170) + 48);
            for (const unsigned char value : endpoint.host())
            {
                if (std::isalnum(value) || value == '-' || value == '.')
                    encoded.push_back(static_cast<char>(std::tolower(value)));
                else
                {
                    encoded.push_back('_');
                    encoded.push_back(digits[value >> 4u]);
                    encoded.push_back(digits[value & 0x0fu]);
                }
            }
            if (encoded.size() > 170)
                encoded.resize(170);
            encoded += "_" + std::to_string(endpoint.port()) + "_" + suffix + ".credential";
            return encoded;
        }

        const char* compositionFailure(ClientCompositionFailure failure) noexcept
        {
            switch (failure)
            {
                case ClientCompositionFailure::ProvidersUnavailable: return "multiplayer providers are unavailable";
                case ClientCompositionFailure::InvalidEndpoint: return "the server address is invalid";
                case ClientCompositionFailure::InvalidTimeout: return "the connection timeout is invalid";
                case ClientCompositionFailure::CredentialReadFailed: return "the saved player credential is invalid";
                case ClientCompositionFailure::CredentialRejected: return "the join credential is invalid";
                case ClientCompositionFailure::TransportUnavailable: return "the multiplayer transport is unavailable";
                case ClientCompositionFailure::RuntimeUnavailable: return "the multiplayer runtime is unavailable";
                case ClientCompositionFailure::ConnectionRejected: return "the connection could not be started";
            }
            return "multiplayer startup failed";
        }

        class DedicatedServerProcess
        {
        public:
            ~DedicatedServerProcess() { stop(); }

            bool start(const std::filesystem::path& executable, const std::filesystem::path& config) noexcept
            try
            {
                mFailure.clear();
                mCapturedOutput.clear();
                if (executable.empty() || config.empty() || !std::filesystem::is_regular_file(executable)
                    || !std::filesystem::is_regular_file(config))
                {
                    mFailure = "Dedicated server executable or configuration is unavailable.";
                    return false;
                }
#ifdef _WIN32
                SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
                HANDLE readPipe = nullptr;
                HANDLE writePipe = nullptr;
                if (!CreatePipe(&readPipe, &writePipe, &security, 0)
                    || !SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
                {
                    if (readPipe) CloseHandle(readPipe);
                    if (writePipe) CloseHandle(writePipe);
                    mFailure = "The dedicated server output pipe could not be created.";
                    return false;
                }
                std::wstring command = L"\"" + executable.wstring() + L"\" \"" + config.wstring() + L"\"";
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                startup.hStdOutput = writePipe;
                startup.hStdError = writePipe;
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &process))
                {
                    CloseHandle(readPipe);
                    CloseHandle(writePipe);
                    mFailure = "The dedicated server process could not be created.";
                    return false;
                }
                CloseHandle(process.hThread);
                CloseHandle(writePipe);
                mProcess = process.hProcess;
                mOutput = readPipe;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    drainOutput();
                    if (mCapturedOutput.find("server started") != std::string::npos)
                        return true;
                    if (WaitForSingleObject(mProcess, 0) != WAIT_TIMEOUT)
                    {
                        drainOutput();
                        mFailure = mCapturedOutput.empty() ? "The dedicated server exited during startup."
                                                          : mCapturedOutput;
                        stop();
                        return false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                mFailure = "The dedicated server did not become ready within 10 seconds.";
                stop();
                return false;
#else
                const auto child = ::fork();
                if (child < 0)
                    return false;
                if (child == 0)
                {
                    ::execl(executable.c_str(), executable.c_str(), config.c_str(), static_cast<char*>(nullptr));
                    ::_exit(127);
                }
                mProcess = child;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
                return true;
            }
            catch (...)
            {
                return false;
            }

            void stop() noexcept
            {
#ifdef _WIN32
                if (!mProcess)
                    return;
                if (WaitForSingleObject(mProcess, 0) == WAIT_TIMEOUT)
                {
                    (void)TerminateProcess(mProcess, 0);
                    (void)WaitForSingleObject(mProcess, 2000);
                }
                CloseHandle(mProcess);
                mProcess = nullptr;
                if (mOutput)
                {
                    CloseHandle(mOutput);
                    mOutput = nullptr;
                }
#else
                if (mProcess <= 0)
                    return;
                int status = 0;
                if (::waitpid(mProcess, &status, WNOHANG) == 0)
                {
                    (void)::kill(mProcess, SIGTERM);
                    if (::waitpid(mProcess, &status, WNOHANG) == 0)
                    {
                        (void)::kill(mProcess, SIGKILL);
                        (void)::waitpid(mProcess, &status, 0);
                    }
                }
                mProcess = -1;
#endif
            }

            std::string_view failure() const noexcept { return mFailure; }

        private:
#ifdef _WIN32
            void drainOutput() noexcept
            {
                if (!mOutput)
                    return;
                DWORD available = 0;
                while (PeekNamedPipe(mOutput, nullptr, 0, nullptr, &available, nullptr) && available != 0)
                {
                    std::array<char, 512> buffer{};
                    DWORD read = 0;
                    if (!ReadFile(mOutput, buffer.data(),
                            (std::min)(available, static_cast<DWORD>(buffer.size())), &read, nullptr)
                        || read == 0)
                        break;
                    if (mCapturedOutput.size() + read > 4096)
                        mCapturedOutput.erase(0, mCapturedOutput.size() + read - 4096);
                    mCapturedOutput.append(buffer.data(), read);
                }
            }
            HANDLE mProcess = nullptr;
            HANDLE mOutput = nullptr;
#else
            pid_t mProcess = -1;
#endif
            std::string mFailure;
            std::string mCapturedOutput;
        };

        class ClientLauncher final : public EngineCoordinator, private ConnectionStatusProvider
        {
        public:
            explicit ClientLauncher(ClientLauncherConfiguration configuration) noexcept
                : mConfiguration(std::move(configuration))
                , mReportedStatus(mConfiguration.providers.status)
            {
                const auto directory = applicationDirectory();
                if (!directory.empty() && mConfiguration.serverExecutable.is_relative())
                    mConfiguration.serverExecutable = directory / mConfiguration.serverExecutable;
                if (!directory.empty() && mConfiguration.serverConfig.is_relative())
                    mConfiguration.serverConfig = directory / mConfiguration.serverConfig;
            }

            ~ClientLauncher() override
            {
                mSession.reset();
                if (mHosted)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                mServer.stop();
            }

            void frame(float duration) noexcept override
            {
                if (!mSession)
                    return;
                mSession->setGameRunning(mGameRunning);
                mSession->frame(duration);
                if (mSession->multiplayerState() == MultiplayerState::Ready)
                    mState = MultiplayerState::Ready;
                else if (mSession->multiplayerState() == MultiplayerState::Failed)
                    mState = MultiplayerState::Failed;
            }

            MultiplayerState multiplayerState() const noexcept override { return mState; }

            bool connect(std::string_view address) noexcept override
            try
            {
                if (mState == MultiplayerState::Connecting || mState == MultiplayerState::Ready)
                    return false;
                if (mHosted)
                {
                    mSession.reset();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    mServer.stop();
                    mHosted = false;
                }
                const auto endpoint = parseServerAddress(address, mConfiguration.defaultPort);
                if (!endpoint)
                    return fail("Enter a valid host, host:port, or tes3mp:// URI.");
                std::error_code error;
                std::filesystem::create_directories(mConfiguration.playerCredentialDirectory, error);
                if (error)
                    return fail("The player credential directory could not be created.");
                auto providers = mConfiguration.providers;
                providers.status = this;
                auto created = makeClientCoordinator(endpoint->host(), endpoint->port(),
                    mConfiguration.timeoutMilliseconds, mConfiguration.passwordFile,
                    mConfiguration.playerCredentialDirectory / credentialFileName(*endpoint),
                    mConfiguration.contentManifest, providers, mJoinPassword);
                auto* session = std::get_if<std::unique_ptr<EngineCoordinator>>(&created);
                if (!session || !*session)
                {
                    const auto* failure = std::get_if<ClientCompositionFailure>(&created);
                    return fail(failure ? compositionFailure(*failure) : "multiplayer startup failed");
                }
                mFailure.clear();
                mSession = std::move(*session);
                mSession->setGameRunning(mGameRunning);
                mState = MultiplayerState::Connecting;
                return true;
            }
            catch (...)
            {
                return fail("multiplayer startup failed");
            }

            bool host(std::string_view address) noexcept override
            {
                if (mState == MultiplayerState::Connecting || mState == MultiplayerState::Ready)
                    return false;
                if (mHosted)
                {
                    mSession.reset();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    mServer.stop();
                    mHosted = false;
                }
                if (!mServer.start(mConfiguration.serverExecutable, mConfiguration.serverConfig))
                    return fail(mServer.failure().empty() ? "The dedicated server could not be started."
                                                          : std::string(mServer.failure()));
                const std::string local = address.empty()
                    ? "127.0.0.1:" + std::to_string(mConfiguration.defaultPort)
                    : std::string(address);
                if (connect(local))
                {
                    mHosted = true;
                    return true;
                }
                mServer.stop();
                return false;
            }

            std::string_view failure() const noexcept override { return mFailure; }
            void setJoinPassword(std::string_view value) noexcept override
            {
                if (value.size() <= MaximumAuthenticationMaterialBytes)
                    mJoinPassword.assign(value);
                else
                    mJoinPassword.clear();
            }
            bool gameStartRequested() const noexcept override
            {
                return mSession && mSession->gameStartRequested();
            }
            CharacterLifecycle characterLifecycle() const noexcept override
            {
                return mSession ? mSession->characterLifecycle() : CharacterLifecycle::NewCharacter;
            }
            CharacterProfileRevision characterProfileRevision() const noexcept override
            {
                return mSession ? mSession->characterProfileRevision() : CharacterProfileRevision::initial();
            }
            const ReliableCharacterProfile* confirmedCharacterProfile() const noexcept override
            { return mSession ? mSession->confirmedCharacterProfile() : nullptr; }
            bool submitCharacterCreation(CharacterCreationChoice choice) noexcept override
            { return mSession && mSession->submitCharacterCreation(std::move(choice)); }
            void setGameRunning(bool value) noexcept override
            {
                mGameRunning = value;
                if (mSession)
                    mSession->setGameRunning(value);
            }
            void confirmGameStart(bool running) noexcept override
            {
                mGameRunning = running;
                if (mSession)
                    mSession->confirmGameStart(running);
                if (!running)
                    mState = MultiplayerState::Failed;
            }

        private:
            bool fail(std::string message) noexcept
            {
                mFailure = std::move(message);
                mState = MultiplayerState::Failed;
                return false;
            }

            void report(ConnectionStatus status) noexcept override
            {
                if (status == ConnectionStatus::Resumed)
                    mState = MultiplayerState::Ready;
                else if (status == ConnectionStatus::Reconnecting)
                    mState = MultiplayerState::Connecting;
                else
                    mState = MultiplayerState::Failed;
                if (mReportedStatus)
                    mReportedStatus->report(status);
            }

            ClientLauncherConfiguration mConfiguration;
            ConnectionStatusProvider* mReportedStatus = nullptr;
            std::unique_ptr<EngineCoordinator> mSession;
            DedicatedServerProcess mServer;
            MultiplayerState mState = MultiplayerState::Idle;
            std::string mFailure;
            std::string mJoinPassword;
            bool mGameRunning = false;
            bool mHosted = false;
        };
    }

    std::optional<ConnectionEndpoint> parseServerAddress(
        std::string_view address, std::uint16_t defaultPort) noexcept
    {
        constexpr std::string_view scheme = "tes3mp://";
        const bool hasTes3mpScheme = address.size() >= scheme.size()
            && std::equal(scheme.begin(), scheme.end(), address.begin(), [](char expected, char actual) {
                return expected == static_cast<char>(std::tolower(static_cast<unsigned char>(actual)));
            });
        if (hasTes3mpScheme)
            address.remove_prefix(scheme.size());
        else if (address.find("://") != std::string_view::npos)
            return std::nullopt;
        if (address.empty() || address.size() > ConnectionEndpoint::MaxHostBytes + 8
            || address.find_first_of("/@?# \t\r\n") != std::string_view::npos)
            return std::nullopt;

        std::string_view host;
        std::uint16_t port = defaultPort;
        if (address.front() == '[')
        {
            const auto close = address.find(']');
            if (close == std::string_view::npos)
                return std::nullopt;
            host = address.substr(1, close - 1);
            const auto remainder = address.substr(close + 1);
            if (!remainder.empty())
            {
                if (remainder.front() != ':')
                    return std::nullopt;
                const auto parsed = parsePort(remainder.substr(1));
                if (!parsed)
                    return std::nullopt;
                port = *parsed;
            }
        }
        else
        {
            const auto colon = address.rfind(':');
            if (colon != std::string_view::npos)
            {
                if (address.find(':') != colon)
                    return std::nullopt;
                host = address.substr(0, colon);
                const auto parsed = parsePort(address.substr(colon + 1));
                if (!parsed)
                    return std::nullopt;
                port = *parsed;
            }
            else
                host = address;
        }
        return port == 0 ? std::nullopt : ConnectionEndpoint::create(host, port);
    }

    std::unique_ptr<EngineCoordinator> makeClientLauncher(ClientLauncherConfiguration configuration) noexcept
    try
    {
        if (configuration.defaultPort == 0 || configuration.timeoutMilliseconds == 0
            || configuration.timeoutMilliseconds > 60'000 || configuration.playerCredentialDirectory.empty()
            || !configuration.providers.input || !configuration.providers.presentation
            || !configuration.providers.status)
            return {};
        return std::make_unique<ClientLauncher>(std::move(configuration));
    }
    catch (...)
    {
        return {};
    }

    std::unique_ptr<PlayerCredentialPersistence> makeFilePlayerCredentialPersistence(std::filesystem::path path)
    {
        return std::make_unique<FilePlayerCredentialPersistence>(std::move(path));
    }

    ClientCoordinatorResult makeClientCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile,
        const std::filesystem::path& playerCredentialFile, ContentManifestId contentManifest,
        ClientProviders providers, std::string_view passwordOverride) noexcept
    try
    {
        if (!providers.input || !providers.presentation || !providers.status)
            return ClientCompositionFailure::ProvidersUnavailable;
        if (port == 0 || port > (std::numeric_limits<std::uint16_t>::max)())
            return ClientCompositionFailure::InvalidEndpoint;
        auto endpoint = ConnectionEndpoint::create(host, static_cast<std::uint16_t>(port));
        if (!endpoint)
            return ClientCompositionFailure::InvalidEndpoint;
        if (timeoutMilliseconds == 0 || timeoutMilliseconds > 60'000)
            return ClientCompositionFailure::InvalidTimeout;

        std::vector<std::byte> bytes;
        if (!passwordOverride.empty())
        {
            if (passwordOverride.size() > MaximumAuthenticationMaterialBytes)
                return ClientCompositionFailure::CredentialRejected;
            bytes.reserve(passwordOverride.size());
            for (const unsigned char byte : passwordOverride)
                bytes.push_back(static_cast<std::byte>(byte));
        }
        else if (!passwordFile.empty())
        {
            std::ifstream stream(passwordFile, std::ios::binary);
            char byte = 0;
            while (stream.get(byte) && bytes.size() <= MaximumAuthenticationMaterialBytes)
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
            if (!stream.eof())
            {
                std::fill(bytes.begin(), bytes.end(), std::byte{});
                return ClientCompositionFailure::CredentialReadFailed;
            }
            if (!bytes.empty() && bytes.back() == std::byte{ '\n' })
                bytes.pop_back();
            if (!bytes.empty() && bytes.back() == std::byte{ '\r' })
                bytes.pop_back();
        }
        auto password = AuthenticationMaterial::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        if (!password)
            return ClientCompositionFailure::CredentialRejected;
        if (playerCredentialFile.empty())
            return ClientCompositionFailure::CredentialRejected;
        const bool playerCredentialExists = std::filesystem::exists(playerCredentialFile);
        auto playerCredential = loadPlayerCredential(playerCredentialFile);
        if (playerCredentialExists && !playerCredential)
            return ClientCompositionFailure::CredentialReadFailed;

#ifdef TES3MP_OPENMW_HAS_GNS
        auto limits = TransportLimits::create(1, 1, 1, 32);
        auto transport = limits ? makeGameNetworkingSocketsTransport(*limits) : TransportFactoryResult{};
        if (!transport)
            return ClientCompositionFailure::TransportUnavailable;
        auto clock = std::make_unique<SteadyClock>();
        auto timeouts = SessionTimeoutPolicy::create(
            timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000);
        auto queue = outboundPolicy();
        auto created = timeouts && queue
            ? ClientSessionRuntime::create(*transport.runtime, *clock, *timeouts, SessionGeneration::initial(), *queue)
            : ClientRuntimeCreateResult{ SessionTransitionError{} };
        auto* runtime = std::get_if<std::unique_ptr<ClientSessionRuntime>>(&created);
        if (!runtime || !*runtime)
            return ClientCompositionFailure::RuntimeUnavailable;
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
        const std::array optional{ vrPoseCapability(), actorReplicationCapability(),
            interactiveObjectReplicationCapability(), inventoryReplicationCapability(), combatReplicationCapability(),
            characterCreationCapability() };
        auto offer
            = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(versions), optional, {}, contentManifest));
        if ((*runtime)->start(*endpoint, ClientHello::fromOffer(std::move(offer)),
                AuthenticationRequest::join(std::move(*password), std::move(playerCredential)))
            != HeadlessClientResult::Accepted)
            return ClientCompositionFailure::ConnectionRejected;
        return makeCoordinator(std::move(transport.runtime), std::move(clock), std::move(*runtime),
            ReconnectConfiguration{ *endpoint, *timeouts, *queue, contentManifest }, *providers.input,
            *providers.presentation, *providers.status, providers.control, providers.poseInput,
            makeFilePlayerCredentialPersistence(playerCredentialFile), providers.movementMetrics);
#else
        return ClientCompositionFailure::TransportUnavailable;
#endif
    }
    catch (...)
    {
        return ClientCompositionFailure::RuntimeUnavailable;
    }
}
