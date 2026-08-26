#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

namespace
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::size_t kMaxCredentialBytes = 64;
    constexpr std::size_t kMaxTokenBytes = 64;
    constexpr std::size_t kMaxApplicationMessageBytes = 64 * 1024;
    constexpr std::size_t kMaxReliableQueueBytes = 256 * 1024;
    constexpr std::size_t kMaxCapturedWireBytes = 2 * 1024 * 1024;
    constexpr std::uint32_t kLoopback = 0x7f000001;

    class Failure : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    void check(bool condition, std::string_view message)
    {
        if (!condition)
            throw Failure(std::string(message));
    }

    enum class Category
    {
        connected_encrypted,
        auth_required,
        auth_rejected,
        auth_rate_limited,
        input_oversized,
        resume_rejected,
        stale_generation,
        queue_full,
        transport_closed,
    };

    constexpr std::string_view categoryName(Category value)
    {
        switch (value)
        {
            case Category::connected_encrypted:
                return "connected_encrypted";
            case Category::auth_required:
                return "auth_required";
            case Category::auth_rejected:
                return "auth_rejected";
            case Category::auth_rate_limited:
                return "auth_rate_limited";
            case Category::input_oversized:
                return "input_oversized";
            case Category::resume_rejected:
                return "resume_rejected";
            case Category::stale_generation:
                return "stale_generation";
            case Category::queue_full:
                return "queue_full";
            case Category::transport_closed:
                return "transport_closed";
        }
        return "unknown";
    }

    struct Diagnostics
    {
        void emit(Category value) { entries.emplace_back(categoryName(value)); }

        bool contains(std::string_view canary) const
        {
            return std::any_of(entries.begin(), entries.end(), [&](const std::string& entry) {
                return entry.find(canary) != std::string::npos;
            });
        }

        std::vector<std::string> entries;
    };

    bool constantTimeEqual(std::string_view left, std::string_view right)
    {
        const std::size_t count = std::max(left.size(), right.size());
        unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            const unsigned char leftByte = index < left.size() ? static_cast<unsigned char>(left[index]) : 0;
            const unsigned char rightByte = index < right.size() ? static_cast<unsigned char>(right[index]) : 0;
            difference |= static_cast<unsigned int>(leftByte ^ rightByte);
        }
        return difference == 0;
    }

    enum class GateState
    {
        connecting,
        encrypted,
        negotiated,
        authenticated,
        closed,
    };

    class AuthenticationGate
    {
    public:
        bool encrypted()
        {
            if (mState != GateState::connecting)
                return false;
            mState = GateState::encrypted;
            return true;
        }

        bool negotiate(bool compatible)
        {
            if (mState != GateState::encrypted || !compatible)
                return false;
            mState = GateState::negotiated;
            return true;
        }

        bool maySendSecret() const { return mState == GateState::negotiated; }

        bool authenticate()
        {
            if (!maySendSecret())
                return false;
            mState = GateState::authenticated;
            return true;
        }

        void close() { mState = GateState::closed; }

    private:
        GateState mState = GateState::connecting;
    };

    SteamNetworkingConfigValue_t productionEncryption(bool requestUnencrypted)
    {
        check(!requestUnencrypted, "unencrypted production transport rejected");
        SteamNetworkingConfigValue_t encryption;
        encryption.SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
        return encryption;
    }

    enum class AuthResult
    {
        accepted,
        rejected,
        oversized,
        rate_limited,
        timed_out,
        cancelled,
    };

    class PasswordAuthenticator
    {
    public:
        explicit PasswordAuthenticator(std::optional<std::string> expected)
            : mExpected(std::move(expected))
        {
        }

        AuthResult authenticate(std::optional<std::string_view> supplied, std::int64_t now, bool timedOut = false,
            bool cancelled = false)
        {
            if (cancelled)
                return AuthResult::cancelled;
            if (timedOut)
                return AuthResult::timed_out;
            if (supplied && supplied->size() > kMaxCredentialBytes)
                return AuthResult::oversized;
            if (now < mBlockedUntil)
                return AuthResult::rate_limited;
            const bool accepted = !mExpected ? !supplied : supplied && constantTimeEqual(*mExpected, *supplied);
            if (accepted)
            {
                mFailures = 0;
                return AuthResult::accepted;
            }
            ++mFailures;
            if (mFailures >= 3)
            {
                mFailures = 0;
                mBlockedUntil = now + 10;
            }
            return AuthResult::rejected;
        }

    private:
        std::optional<std::string> mExpected;
        int mFailures = 0;
        std::int64_t mBlockedUntil = std::numeric_limits<std::int64_t>::min();
    };

    struct ResumeRecord
    {
        std::string principal;
        std::string session;
        std::string context;
        std::uint64_t generation = 0;
        std::int64_t expiresAt = 0;
    };

    struct ResumeResult
    {
        bool accepted = false;
        std::string replacement;
    };

    class ResumeStore
    {
    public:
        using Generator = std::function<std::optional<std::string>()>;

        void insert(std::string token, ResumeRecord record)
        {
            check(token.size() <= kMaxTokenBytes, "resume token exceeds proof budget");
            std::scoped_lock lock(mMutex);
            mRecords.emplace(std::move(token), std::move(record));
        }

        ResumeResult consume(std::string_view token, const ResumeRecord& expected, std::int64_t now, Generator generator)
        {
            if (token.size() > kMaxTokenBytes)
                return {};
            std::scoped_lock lock(mMutex);
            const auto found = mRecords.find(std::string(token));
            if (found == mRecords.end())
                return {};
            const ResumeRecord& actual = found->second;
            if (actual.expiresAt <= now || actual.principal != expected.principal || actual.session != expected.session
                || actual.context != expected.context || actual.generation != expected.generation)
                return {};
            const std::optional<std::string> replacement = generator();
            if (!replacement || replacement->empty() || replacement->size() > kMaxTokenBytes
                || mRecords.contains(*replacement))
                return {};
            ResumeRecord rotated = actual;
            ++rotated.generation;
            mRecords.emplace(*replacement, std::move(rotated));
            mRecords.erase(found);
            return { true, *replacement };
        }

        void invalidateAll()
        {
            std::scoped_lock lock(mMutex);
            mRecords.clear();
        }

    private:
        std::mutex mMutex;
        std::map<std::string, ResumeRecord, std::less<>> mRecords;
    };

    class GenerationGate
    {
    public:
        bool accept(std::uint64_t generation) const { return mAlive && generation == mGeneration; }
        void replace() { ++mGeneration; }
        void destroy() { mAlive = false; }
        std::uint64_t current() const { return mGeneration; }

    private:
        std::uint64_t mGeneration = 1;
        bool mAlive = true;
    };

    class LatestWinsQueue
    {
    public:
        bool push(std::string value)
        {
            if (value.size() > kMaxApplicationMessageBytes)
                return false;
            mValue = std::move(value);
            ++mAccepted;
            return true;
        }

        std::optional<std::string> pop()
        {
            std::optional<std::string> result = std::move(mValue);
            mValue.reset();
            return result;
        }

        std::size_t size() const { return mValue ? 1 : 0; }
        std::size_t accepted() const { return mAccepted; }

    private:
        std::optional<std::string> mValue;
        std::size_t mAccepted = 0;
    };

    class ReliableQueue
    {
    public:
        bool push(std::string value)
        {
            if (value.size() > kMaxApplicationMessageBytes || mBytes + value.size() > kMaxReliableQueueBytes)
                return false;
            mBytes += value.size();
            mValues.push_back(std::move(value));
            return true;
        }

        std::size_t bytes() const { return mBytes; }
        std::size_t size() const { return mValues.size(); }

    private:
        std::deque<std::string> mValues;
        std::size_t mBytes = 0;
    };

#ifdef _WIN32
    using Socket = SOCKET;
    constexpr Socket kInvalidSocket = INVALID_SOCKET;
    void closeSocket(Socket value) { closesocket(value); }
#else
    using Socket = int;
    constexpr Socket kInvalidSocket = -1;
    void closeSocket(Socket value) { close(value); }
#endif

    std::uint16_t findAvailableUdpPort()
    {
        const Socket probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        check(probe != kInvalidSocket, "cannot create UDP port probe");
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local.sin_port = 0;
        if (bind(probe, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
        {
            closeSocket(probe);
            throw Failure("cannot bind UDP port probe");
        }
#ifdef _WIN32
        int size = sizeof(local);
#else
        socklen_t size = sizeof(local);
#endif
        if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) != 0)
        {
            closeSocket(probe);
            throw Failure("cannot query UDP port probe");
        }
        closeSocket(probe);
        return ntohs(local.sin_port);
    }

    class DatagramProxy
    {
    public:
        explicit DatagramProxy(std::uint16_t serverPort)
        {
            mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            check(mSocket != kInvalidSocket, "cannot create capture proxy socket");
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = 0;
            check(bind(mSocket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0,
                "cannot bind capture proxy");
            socklen_type size = static_cast<socklen_type>(sizeof(local));
            check(getsockname(mSocket, reinterpret_cast<sockaddr*>(&local), &size) == 0,
                "cannot query capture proxy port");
            mPort = ntohs(local.sin_port);
            mServer.sin_family = AF_INET;
            mServer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            mServer.sin_port = htons(serverPort);
            mThread = std::thread([this] { run(); });
        }

        DatagramProxy(const DatagramProxy&) = delete;
        DatagramProxy& operator=(const DatagramProxy&) = delete;

        ~DatagramProxy()
        {
            mStop = true;
            if (mThread.joinable())
                mThread.join();
            if (mSocket != kInvalidSocket)
                closeSocket(mSocket);
        }

        std::uint16_t port() const { return mPort; }

        bool contains(std::string_view value) const
        {
            const std::vector<std::uint8_t> needle(value.begin(), value.end());
            std::scoped_lock lock(mMutex);
            return std::search(mCapture.begin(), mCapture.end(), needle.begin(), needle.end()) != mCapture.end();
        }

        std::size_t capturedBytes() const
        {
            std::scoped_lock lock(mMutex);
            return mCapture.size();
        }

    private:
#ifdef _WIN32
        using socklen_type = int;
#else
        using socklen_type = socklen_t;
#endif

        void run()
        {
            std::array<std::uint8_t, 65536> buffer{};
            while (!mStop)
            {
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(mSocket, &readSet);
                timeval timeout{ 0, 50'000 };
                const int ready = select(static_cast<int>(mSocket + 1), &readSet, nullptr, nullptr, &timeout);
                if (ready <= 0)
                    continue;
                sockaddr_in source{};
                socklen_type sourceSize = static_cast<socklen_type>(sizeof(source));
                const int received = recvfrom(mSocket, reinterpret_cast<char*>(buffer.data()),
                    static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &sourceSize);
                if (received <= 0)
                    continue;
                {
                    std::scoped_lock lock(mMutex);
                    const std::size_t remaining = kMaxCapturedWireBytes - mCapture.size();
                    const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(received), remaining);
                    mCapture.insert(mCapture.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
                }
                const bool fromServer = source.sin_port == mServer.sin_port;
                if (!fromServer)
                {
                    mClient = source;
                    mHaveClient = true;
                }
                const sockaddr_in& destination = fromServer ? mClient : mServer;
                if (fromServer && !mHaveClient)
                    continue;
                sendto(mSocket, reinterpret_cast<const char*>(buffer.data()), received, 0,
                    reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
            }
        }

        Socket mSocket = kInvalidSocket;
        std::uint16_t mPort = 0;
        sockaddr_in mServer{};
        sockaddr_in mClient{};
        bool mHaveClient = false;
        std::atomic_bool mStop = false;
        std::thread mThread;
        mutable std::mutex mMutex;
        std::vector<std::uint8_t> mCapture;
    };

    class NetworkHarness
    {
    public:
        NetworkHarness()
        {
            SteamDatagramErrMsg error{};
            check(GameNetworkingSockets_Init(nullptr, error), "GameNetworkingSockets initialization failed");
            sCurrent = this;
            SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&onStatusChanged);

            SteamNetworkingIPAddr listenAddress;
            listenAddress.SetIPv4(kLoopback, findAvailableUdpPort());
            mListen = sockets()->CreateListenSocketIP(listenAddress, 0, nullptr);
            check(mListen != k_HSteamListenSocket_Invalid, "cannot create loopback listen socket");
            check(sockets()->GetListenSocketAddress(mListen, &listenAddress), "cannot query listen address");
            mProxy.emplace(listenAddress.m_port);

            SteamNetworkingIPAddr proxyAddress;
            proxyAddress.SetIPv4(kLoopback, mProxy->port());
            SteamNetworkingConfigValue_t encryption = productionEncryption(false);
            mClient = sockets()->ConnectByIPAddress(proxyAddress, 1, &encryption);
            check(mClient != k_HSteamNetConnection_Invalid, "cannot create loopback client connection");
            pumpUntil([&] { return mClientConnected && mServerConnected; }, 8s, "encrypted connection timeout");

            SteamNetConnectionInfo_t clientInfo{};
            SteamNetConnectionInfo_t serverInfo{};
            check(sockets()->GetConnectionInfo(mClient, &clientInfo), "cannot inspect client connection");
            check(sockets()->GetConnectionInfo(mServer, &serverInfo), "cannot inspect server connection");
            check((clientInfo.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted) == 0,
                "client connection unexpectedly negotiated plaintext");
            check((serverInfo.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted) == 0,
                "server connection unexpectedly negotiated plaintext");
        }

        NetworkHarness(const NetworkHarness&) = delete;
        NetworkHarness& operator=(const NetworkHarness&) = delete;

        ~NetworkHarness()
        {
            if (mClient != k_HSteamNetConnection_Invalid)
                sockets()->CloseConnection(mClient, 0, nullptr, false);
            if (mServer != k_HSteamNetConnection_Invalid)
                sockets()->CloseConnection(mServer, 0, nullptr, false);
            if (mListen != k_HSteamListenSocket_Invalid)
                sockets()->CloseListenSocket(mListen);
            for (int index = 0; index < 5; ++index)
            {
                sockets()->RunCallbacks();
                std::this_thread::sleep_for(5ms);
            }
            SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(nullptr);
            sCurrent = nullptr;
            mProxy.reset();
            GameNetworkingSockets_Kill();
        }

        void sendClient(
            std::string_view value, int flags = k_nSteamNetworkingSend_Reliable, std::uint16_t lane = 0)
        {
            check(value.size() <= kMaxApplicationMessageBytes, "application message exceeds proof budget");
            SteamNetworkingMessage_t* message = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(value.size()));
            check(message != nullptr, "client message allocation failed");
            std::memcpy(message->m_pData, value.data(), value.size());
            message->m_conn = mClient;
            message->m_nFlags = flags;
            message->m_idxLane = lane;
            std::int64_t result = 0;
            sockets()->SendMessages(1, &message, &result, true);
            check(result > 0, "client send failed");
        }

        std::vector<std::string> receiveServer(std::size_t minimum, std::chrono::milliseconds timeout)
        {
            std::vector<std::string> result;
            pumpUntil(
                [&] {
                    ISteamNetworkingMessage* message = nullptr;
                    while (sockets()->ReceiveMessagesOnConnection(mServer, &message, 1) == 1)
                    {
                        result.emplace_back(static_cast<const char*>(message->m_pData), message->m_cbSize);
                        message->Release();
                    }
                    return result.size() >= minimum;
                },
                timeout, "message receive timeout");
            return result;
        }

        bool wireContains(std::string_view value) const { return mProxy->contains(value); }
        std::size_t capturedBytes() const { return mProxy->capturedBytes(); }
        HSteamNetConnection client() const { return mClient; }

        void pumpFor(std::chrono::milliseconds duration)
        {
            const auto deadline = Clock::now() + duration;
            while (Clock::now() < deadline)
            {
                sockets()->RunCallbacks();
                std::this_thread::sleep_for(2ms);
            }
        }

        static ISteamNetworkingSockets* sockets() { return SteamNetworkingSockets(); }

    private:
        static void onStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
        {
            if (sCurrent)
                sCurrent->statusChanged(*info);
        }

        void statusChanged(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting
                && info.m_info.m_hListenSocket == mListen)
            {
                mServer = info.m_hConn;
                check(sockets()->AcceptConnection(mServer) == k_EResultOK, "server accept failed");
            }
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
            {
                if (info.m_hConn == mClient)
                    mClientConnected = true;
                if (info.m_hConn == mServer)
                    mServerConnected = true;
            }
        }

        template <class Predicate>
        void pumpUntil(Predicate predicate, std::chrono::milliseconds timeout, std::string_view error)
        {
            const auto deadline = Clock::now() + timeout;
            while (Clock::now() < deadline)
            {
                sockets()->RunCallbacks();
                if (predicate())
                    return;
                std::this_thread::sleep_for(2ms);
            }
            throw Failure(std::string(error));
        }

        inline static NetworkHarness* sCurrent = nullptr;
        HSteamListenSocket mListen = k_HSteamListenSocket_Invalid;
        HSteamNetConnection mClient = k_HSteamNetConnection_Invalid;
        HSteamNetConnection mServer = k_HSteamNetConnection_Invalid;
        bool mClientConnected = false;
        bool mServerConnected = false;
        std::optional<DatagramProxy> mProxy;
    };

    void testAuthenticationOrderingAndRedaction()
    {
        Diagnostics diagnostics;
        AuthenticationGate gate;
        check(!gate.maySendSecret(), "secret allowed before encrypted transport");
        bool plaintextRejected = false;
        try
        {
            (void)productionEncryption(true);
        }
        catch (const Failure&)
        {
            plaintextRejected = true;
        }
        check(plaintextRejected, "unencrypted production request did not fail closed");
        check(!gate.negotiate(true), "negotiation allowed before encryption");
        check(gate.encrypted(), "encrypted transition rejected");
        check(!gate.maySendSecret(), "secret allowed before negotiation");
        check(gate.negotiate(true), "compatible negotiation rejected");
        check(gate.maySendSecret(), "secret rejected after negotiation");

        const std::string passwordCanary = "pw-canary-" + std::to_string(0x5a17u);
        const std::string tokenCanary = "resume-canary-" + std::to_string(0xc041u);
        PasswordAuthenticator openServer(std::nullopt);
        check(openServer.authenticate(std::nullopt, 0) == AuthResult::accepted, "open server rejected absent password");
        PasswordAuthenticator protectedServer(passwordCanary);
        check(protectedServer.authenticate(passwordCanary, 0) == AuthResult::accepted, "correct password rejected");
        check(protectedServer.authenticate("incorrect", 1) == AuthResult::rejected, "incorrect password accepted");
        check(protectedServer.authenticate(std::string(kMaxCredentialBytes + 1, 'x'), 2) == AuthResult::oversized,
            "oversized password accepted");
        check(protectedServer.authenticate(passwordCanary, 3, true) == AuthResult::timed_out, "timeout ignored");
        check(protectedServer.authenticate(passwordCanary, 3, false, true) == AuthResult::cancelled,
            "cancellation ignored");
        protectedServer.authenticate("bad-1", 4);
        protectedServer.authenticate("bad-2", 4);
        protectedServer.authenticate("bad-3", 4);
        check(protectedServer.authenticate(passwordCanary, 5) == AuthResult::rate_limited,
            "repeated failures were not rate limited");

        NetworkHarness network;
        diagnostics.emit(Category::connected_encrypted);
        network.sendClient("password:" + passwordCanary);
        network.sendClient("resume:" + tokenCanary);
        const auto received = network.receiveServer(2, 5s);
        check(received[0] == "password:" + passwordCanary, "password payload changed in transit");
        check(received[1] == "resume:" + tokenCanary, "resume payload changed in transit");
        network.pumpFor(100ms);
        check(network.capturedBytes() > 0, "capture proxy observed no wire traffic");
        check(!network.wireContains(passwordCanary), "password canary appeared in captured wire bytes");
        check(!network.wireContains(tokenCanary), "resume canary appeared in captured wire bytes");
        check(!diagnostics.contains(passwordCanary) && !diagnostics.contains(tokenCanary),
            "secret canary appeared in stable diagnostics");
        check(gate.authenticate(), "authentication transition rejected");
    }

    void testResumeAtomicityAndGeneration()
    {
        ResumeStore store;
        const ResumeRecord initial{ "principal", "session", "content-v1", 7, 100 };
        store.insert("token-a", initial);
        auto first = store.consume("token-a", initial, 10, [] { return std::optional<std::string>("token-b"); });
        check(first.accepted && first.replacement == "token-b", "valid resume did not rotate token");
        check(!store.consume("token-a", initial, 10, [] { return std::optional<std::string>("unused"); }).accepted,
            "consumed token was accepted twice");

        ResumeRecord rotated = initial;
        ++rotated.generation;
        std::atomic_int winners = 0;
        auto compete = [&](std::string replacement) {
            if (store.consume("token-b", rotated, 11,
                    [replacement = std::move(replacement)] { return std::optional<std::string>(replacement); })
                    .accepted)
                ++winners;
        };
        std::thread left(compete, "token-c");
        std::thread right(compete, "token-d");
        left.join();
        right.join();
        check(winners == 1, "concurrent resume did not have exactly one winner");

        ResumeStore failures;
        failures.insert("expiring", initial);
        check(!failures.consume("expiring", initial, 100, [] { return std::optional<std::string>("new"); }).accepted,
            "expired token accepted");
        ResumeRecord wrongContext = initial;
        wrongContext.context = "other";
        check(!failures.consume("expiring", wrongContext, 10, [] { return std::optional<std::string>("new"); }).accepted,
            "wrong-context token accepted");
        ResumeRecord oldGeneration = initial;
        --oldGeneration.generation;
        check(!failures.consume("expiring", oldGeneration, 10,
                  [] { return std::optional<std::string>("new"); })
                   .accepted,
            "old-generation token accepted");
        check(!failures.consume("expiring", initial, 10, [] { return std::optional<std::string>(); }).accepted,
            "interrupted rotation partially attached");
        check(failures.consume("expiring", initial, 10, [] { return std::optional<std::string>("replacement"); })
                  .accepted,
            "interrupted rotation consumed original token");
        failures.invalidateAll();
        check(!failures.consume("replacement", ResumeRecord{ "principal", "session", "content-v1", 8, 100 }, 10,
                  [] { return std::optional<std::string>("next"); })
                   .accepted,
            "server invalidation retained a resume token");

        GenerationGate gate;
        const auto old = gate.current();
        check(gate.accept(old), "current generation rejected");
        gate.replace();
        check(!gate.accept(old), "delayed old-connection callback accepted");
        check(gate.accept(gate.current()), "replacement generation rejected");
        gate.destroy();
        check(!gate.accept(gate.current()), "callback accepted after teardown");
    }

    void testBoundedQueuesAndFloods()
    {
        LatestWinsQueue latest;
        for (int index = 0; index < 10'000; ++index)
            check(latest.push("snapshot-" + std::to_string(index)), "bounded snapshot unexpectedly rejected");
        check(latest.size() == 1 && latest.accepted() == 10'000, "latest-wins queue grew or lost accounting");
        check(latest.pop() == "snapshot-9999", "latest-wins queue did not retain newest sample");
        check(!latest.push(std::string(kMaxApplicationMessageBytes + 1, 'x')), "oversized sample accepted");

        ReliableQueue reliable;
        const std::string chunk(32 * 1024, 'r');
        for (int index = 0; index < 8; ++index)
            check(reliable.push(chunk), "reliable queue rejected within byte budget");
        check(reliable.bytes() == kMaxReliableQueueBytes && reliable.size() == 8,
            "reliable queue accounting mismatch");
        check(!reliable.push("overflow"), "reliable queue exceeded byte budget");

        PasswordAuthenticator flood(std::string("join"));
        std::size_t limited = 0;
        for (int index = 0; index < 10'000; ++index)
        {
            if (flood.authenticate("bad", 0) == AuthResult::rate_limited)
                ++limited;
        }
        check(limited > 9'900, "authentication flood was not bounded by admission limiter");
    }

    void testDeliveryClassesUnderFaults()
    {
        NetworkHarness network;
        int priorities[2] = { 1, 0 };
        std::uint16_t weights[2] = { 1, 1 };
        check(NetworkHarness::sockets()->ConfigureConnectionLanes(network.client(), 2, priorities, weights)
                == k_EResultOK,
            "cannot configure proof lanes");
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, 8.0F);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send, 20.0F);
        SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time, 20);

        for (int index = 0; index < 24; ++index)
        {
            const std::string value = "reliable-" + std::to_string(index);
            network.sendClient(value, k_nSteamNetworkingSend_Reliable);
        }
        const auto reliable = network.receiveServer(24, 15s);
        for (int index = 0; index < 24; ++index)
            check(reliable[static_cast<std::size_t>(index)] == "reliable-" + std::to_string(index),
                "reliable lane was not ordered under loss/reordering");

        LatestWinsQueue latest;
        for (int index = 0; index < 1000; ++index)
            latest.push("latest-" + std::to_string(index));
        const auto newest = latest.pop();
        check(newest.has_value(), "latest-wins queue unexpectedly empty");

        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, 100.0F);
        const std::string delayedReliable(kMaxApplicationMessageBytes, 'd');
        network.sendClient(delayedReliable, k_nSteamNetworkingSend_Reliable, 0);
        network.pumpFor(50ms);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, 0.0F);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send, 0.0F);
        SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time, 0);
        network.sendClient(*newest, k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle, 1);
        const auto sample = network.receiveServer(1, 5s);
        check(sample.front() == "latest-999", "delayed reliable lane head-of-line blocked the latest sample");
        const auto delayed = network.receiveServer(1, 10s);
        check(delayed.front() == delayedReliable, "delayed reliable operation was lost");
    }

    void testStableCategories()
    {
        constexpr std::array values{ Category::connected_encrypted, Category::auth_required, Category::auth_rejected,
            Category::auth_rate_limited, Category::input_oversized, Category::resume_rejected,
            Category::stale_generation, Category::queue_full, Category::transport_closed };
        for (const Category value : values)
        {
            const std::string_view name = categoryName(value);
            check(!name.empty() && name != "unknown", "stable category missing");
            check(name.find(' ') == std::string_view::npos, "stable category contains raw diagnostic text");
        }
    }
}

int main()
{
    const std::array tests{
        std::pair{ "authentication_ordering_encryption_capture_and_redaction", &testAuthenticationOrderingAndRedaction },
        std::pair{ "resume_single_use_rotation_contention_and_generation", &testResumeAtomicityAndGeneration },
        std::pair{ "bounded_latest_reliable_authentication_and_flood_queues", &testBoundedQueuesAndFloods },
        std::pair{ "reliable_and_unreliable_delivery_classes_under_faults", &testDeliveryClassesUnderFaults },
        std::pair{ "stable_owned_telemetry_categories", &testStableCategories },
    };

    try
    {
        for (const auto& [name, test] : tests)
        {
            test();
            std::cout << "PASS " << name << '\n';
        }
        std::cout << "PASS all GameNetworkingSockets selection-proof scenarios\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL selection proof: " << error.what() << '\n';
        return 1;
    }
}
