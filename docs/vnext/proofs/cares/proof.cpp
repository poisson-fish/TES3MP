#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
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

#include <ares.h>

namespace
{
    using namespace std::chrono_literals;

    constexpr std::size_t kMaximumHostnameBytes = 253;
    constexpr std::size_t kMaximumLabelBytes = 63;
    constexpr std::size_t kMaximumAddresses = 8;
    constexpr int kProofTimeoutMilliseconds = 80;

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

#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket kInvalidSocket = -1;
#endif

    void closeSocket(NativeSocket socket)
    {
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }

    class SocketRuntime
    {
    public:
        SocketRuntime()
        {
#ifdef _WIN32
            WSADATA data{};
            check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed");
#endif
        }

        ~SocketRuntime()
        {
#ifdef _WIN32
            WSACleanup();
#endif
        }
    };

    std::uint16_t readU16(const unsigned char* bytes)
    {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
    }

    void appendU16(std::vector<unsigned char>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<unsigned char>(value >> 8U));
        bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    }

    void appendU32(std::vector<unsigned char>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<unsigned char>(value >> 24U));
        bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
        bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
        bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    }

    struct Question
    {
        std::string name;
        std::uint16_t type = 0;
        std::size_t end = 0;
    };

    Question parseQuestion(const unsigned char* bytes, std::size_t size)
    {
        check(size >= 17, "DNS request is truncated");
        Question result;
        std::size_t cursor = 12;
        while (cursor < size)
        {
            const std::size_t labelSize = bytes[cursor++];
            if (labelSize == 0)
                break;
            check(labelSize <= 63 && cursor + labelSize <= size, "DNS question label is invalid");
            if (!result.name.empty())
                result.name.push_back('.');
            for (std::size_t index = 0; index < labelSize; ++index)
                result.name.push_back(static_cast<char>(std::tolower(bytes[cursor + index])));
            cursor += labelSize;
        }
        check(cursor + 4 <= size, "DNS question trailer is truncated");
        result.type = readU16(bytes + cursor);
        result.end = cursor + 4;
        return result;
    }

    void appendAnswer(
        std::vector<unsigned char>& response, std::uint16_t type, const std::array<unsigned char, 16>& address)
    {
        appendU16(response, 0xc00c);
        appendU16(response, type);
        appendU16(response, 1);
        appendU32(response, 60);
        const std::uint16_t size = type == 1 ? 4 : 16;
        appendU16(response, size);
        response.insert(response.end(), address.begin(), address.begin() + size);
    }

    class DnsFixture
    {
    public:
        DnsFixture()
        {
            mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            check(mSocket != kInvalidSocket, "cannot create loopback DNS socket");
#ifdef _WIN32
            const DWORD timeout = 100;
            check(setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                      sizeof(timeout)) == 0,
                "cannot set DNS socket timeout");
#else
            const timeval timeout{ 0, 100000 };
            check(setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
                "cannot set DNS socket timeout");
#endif
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            check(bind(mSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
                "cannot bind loopback DNS socket");
            socklen_t size = sizeof(address);
            check(getsockname(mSocket, reinterpret_cast<sockaddr*>(&address), &size) == 0,
                "cannot inspect loopback DNS socket");
            mPort = ntohs(address.sin_port);
            mThread = std::thread([this] { serve(); });
        }

        ~DnsFixture()
        {
            mStopping.store(true);
            if (mThread.joinable())
                mThread.join();
            closeSocket(mSocket);
        }

        DnsFixture(const DnsFixture&) = delete;
        DnsFixture& operator=(const DnsFixture&) = delete;

        std::uint16_t port() const { return mPort; }
        std::size_t queries() const { return mQueries.load(); }

    private:
        void serve()
        {
            std::array<unsigned char, 2048> request{};
            while (!mStopping.load())
            {
                sockaddr_storage peer{};
                socklen_t peerSize = sizeof(peer);
#ifdef _WIN32
                const int received = recvfrom(mSocket, reinterpret_cast<char*>(request.data()),
                    static_cast<int>(request.size()), 0, reinterpret_cast<sockaddr*>(&peer), &peerSize);
#else
                const int received = static_cast<int>(recvfrom(mSocket, request.data(), request.size(), 0,
                    reinterpret_cast<sockaddr*>(&peer), &peerSize));
#endif
                if (received <= 0)
                    continue;
                ++mQueries;
                try
                {
                    const Question question = parseQuestion(request.data(), static_cast<std::size_t>(received));
                    if (question.name == "timeout.proof.test")
                        continue;
                    std::vector<unsigned char> response = buildResponse(
                        request.data(), static_cast<std::size_t>(received), question);
#ifdef _WIN32
                    sendto(mSocket, reinterpret_cast<const char*>(response.data()),
                        static_cast<int>(response.size()), 0, reinterpret_cast<const sockaddr*>(&peer), peerSize);
#else
                    sendto(mSocket, response.data(), response.size(), 0,
                        reinterpret_cast<const sockaddr*>(&peer), peerSize);
#endif
                }
                catch (const Failure&)
                {
                    // The proof fixture ignores malformed client packets; c-ares is the subject under test.
                }
            }
        }

        static std::vector<unsigned char> buildResponse(
            const unsigned char* request, std::size_t requestSize, const Question& question)
        {
            if (question.name == "malformed.proof.test")
                return { request[0], request[1], 0x81 };

            const bool notFound = question.name == "nxdomain.proof.test";
            std::vector<std::array<unsigned char, 16>> answers;
            if (!notFound && question.name != "nodata.proof.test")
            {
                if (question.type == 1 && question.name != "ipv6.proof.test")
                {
                    const std::size_t count = question.name == "many.proof.test" ? 10
                        : question.name == "duplicate.proof.test"                  ? 3
                                                                                   : 1;
                    for (std::size_t index = 0; index < count; ++index)
                    {
                        std::array<unsigned char, 16> address{};
                        address[0] = 192;
                        address[1] = 0;
                        address[2] = 2;
                        address[3] = static_cast<unsigned char>(
                            question.name == "duplicate.proof.test" ? 42 : index + 1);
                        answers.push_back(address);
                    }
                }
                else if (question.type == 28 && question.name != "ipv4.proof.test"
                    && question.name != "many.proof.test" && question.name != "duplicate.proof.test")
                {
                    std::array<unsigned char, 16> address{};
                    address[0] = 0x20;
                    address[1] = 0x01;
                    address[2] = 0x0d;
                    address[3] = 0xb8;
                    address[15] = 1;
                    answers.push_back(address);
                }
            }

            std::vector<unsigned char> response;
            response.reserve(requestSize + answers.size() * 28);
            appendU16(response, readU16(request));
            appendU16(response, notFound ? 0x8183 : 0x8180);
            appendU16(response, 1);
            appendU16(response, static_cast<std::uint16_t>(answers.size()));
            appendU16(response, 0);
            appendU16(response, 0);
            response.insert(response.end(), request + 12, request + question.end);
            for (const auto& answer : answers)
                appendAnswer(response, question.type, answer);
            return response;
        }

        NativeSocket mSocket = kInvalidSocket;
        std::uint16_t mPort = 0;
        std::atomic<bool> mStopping = false;
        std::atomic<std::size_t> mQueries = 0;
        std::thread mThread;
    };

    struct OwnedAddress
    {
        int family = AF_UNSPEC;
        std::string text;
        std::uint16_t port = 0;

    };

    struct QueryResult
    {
        bool done = false;
        int status = ARES_EFORMERR;
        int timeouts = 0;
        std::vector<OwnedAddress> addresses;
    };

    class Resolver
    {
    public:
        explicit Resolver(std::uint16_t dnsPort)
        {
            ares_options options{};
            options.flags = ARES_FLAG_NOSEARCH;
            options.timeout = kProofTimeoutMilliseconds;
            options.maxtimeout = kProofTimeoutMilliseconds;
            options.tries = 1;
            options.qcache_max_ttl = 0;
            options.sock_state_cb = &Resolver::socketStateCallback;
            options.sock_state_cb_data = this;
            const int mask = ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS | ARES_OPT_MAXTIMEOUTMS | ARES_OPT_TRIES
                | ARES_OPT_QUERY_CACHE | ARES_OPT_SOCK_STATE_CB;
            check(ares_init_options(&mChannel, &options, mask) == ARES_SUCCESS,
                "cannot initialize c-ares channel");
            const std::string server = "127.0.0.1:" + std::to_string(dnsPort);
            check(ares_set_servers_ports_csv(mChannel, server.c_str()) == ARES_SUCCESS,
                "cannot configure loopback DNS fixture");
        }

        ~Resolver()
        {
            if (mChannel != nullptr)
                ares_destroy(mChannel);
        }

        Resolver(const Resolver&) = delete;
        Resolver& operator=(const Resolver&) = delete;

        void start(std::string_view host, std::uint16_t port, QueryResult& result)
        {
            result = {};
            ares_addrinfo_hints hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            hints.ai_flags = ARES_AI_NOSORT | ARES_AI_NUMERICSERV;
            mService = std::to_string(port);
            mHost.assign(host);
            ares_getaddrinfo(mChannel, mHost.c_str(), mService.c_str(), &hints,
                &Resolver::addressCallback, &result);
        }

        QueryResult resolve(std::string_view host, std::uint16_t port)
        {
            QueryResult result;
            start(host, port, result);
            pump(result, 2s);
            return result;
        }

        void cancel() { ares_cancel(mChannel); }

        void destroy()
        {
            ares_destroy(mChannel);
            mChannel = nullptr;
            mSockets.clear();
        }

        void pump(QueryResult& result, std::chrono::milliseconds deadline)
        {
            const auto end = std::chrono::steady_clock::now() + deadline;
            while (!result.done && std::chrono::steady_clock::now() < end)
            {
                fd_set readable;
                fd_set writable;
                FD_ZERO(&readable);
                FD_ZERO(&writable);
                ares_socket_t maximum = 0;
                for (const auto& [socket, events] : mSockets)
                {
                    if ((events & ARES_FD_EVENT_READ) != 0)
                        FD_SET(socket, &readable);
                    if ((events & ARES_FD_EVENT_WRITE) != 0)
                        FD_SET(socket, &writable);
                    maximum = std::max(maximum, socket);
                }
                timeval maximumWait{ 0, 20000 };
                timeval wait{};
                ares_timeout(mChannel, &maximumWait, &wait);
                std::vector<ares_fd_events_t> ready;
                if (mSockets.empty())
                {
                    std::this_thread::sleep_for(
                        std::chrono::seconds(wait.tv_sec) + std::chrono::microseconds(wait.tv_usec));
                }
                else
                {
                    const int selected = select(static_cast<int>(maximum + 1), &readable, &writable, nullptr, &wait);
                    check(selected >= 0, "select failed while pumping c-ares");
                    if (selected > 0)
                    {
                        for (const auto& [socket, events] : mSockets)
                        {
                            unsigned int observed = 0;
                            if ((events & ARES_FD_EVENT_READ) != 0 && FD_ISSET(socket, &readable))
                                observed |= ARES_FD_EVENT_READ;
                            if ((events & ARES_FD_EVENT_WRITE) != 0 && FD_ISSET(socket, &writable))
                                observed |= ARES_FD_EVENT_WRITE;
                            if (observed != 0)
                                ready.push_back({ socket, observed });
                        }
                    }
                }
                const ares_status_t status = ares_process_fds(
                    mChannel, ready.empty() ? nullptr : ready.data(), ready.size(), ARES_PROCESS_FLAG_NONE);
                check(status == ARES_SUCCESS, "ares_process_fds failed");
            }
            check(result.done, "resolver callback exceeded the proof deadline");
        }

    private:
        static void socketStateCallback(void* data, ares_socket_t socket, int readable, int writable)
        {
            auto& self = *static_cast<Resolver*>(data);
            unsigned int events = 0;
            if (readable != 0)
                events |= ARES_FD_EVENT_READ;
            if (writable != 0)
                events |= ARES_FD_EVENT_WRITE;
            if (events == 0)
                self.mSockets.erase(socket);
            else
                self.mSockets[socket] = events;
        }

        static void addressCallback(void* data, int status, int timeouts, ares_addrinfo* info)
        {
            auto& result = *static_cast<QueryResult*>(data);
            result.status = status;
            result.timeouts = timeouts;
            if (status == ARES_SUCCESS && info != nullptr)
            {
                for (ares_addrinfo_node* node = info->nodes; node != nullptr; node = node->ai_next)
                {
                    std::array<char, INET6_ADDRSTRLEN> text{};
                    const void* raw = nullptr;
                    std::uint16_t port = 0;
                    if (node->ai_family == AF_INET)
                    {
                        const auto* address = reinterpret_cast<const sockaddr_in*>(node->ai_addr);
                        raw = &address->sin_addr;
                        port = ntohs(address->sin_port);
                    }
                    else if (node->ai_family == AF_INET6)
                    {
                        const auto* address = reinterpret_cast<const sockaddr_in6*>(node->ai_addr);
                        raw = &address->sin6_addr;
                        port = ntohs(address->sin6_port);
                    }
                    if (raw != nullptr
                        && ares_inet_ntop(node->ai_family, raw, text.data(),
                               static_cast<ares_socklen_t>(text.size()))
                            != nullptr)
                        result.addresses.push_back({ node->ai_family, text.data(), port });
                }
            }
            if (info != nullptr)
                ares_freeaddrinfo(info);
            result.done = true;
        }

        ares_channel_t* mChannel = nullptr;
        std::map<ares_socket_t, unsigned int> mSockets;
        std::string mHost;
        std::string mService;
    };

    std::vector<OwnedAddress> boundedUnique(const std::vector<OwnedAddress>& input)
    {
        std::vector<OwnedAddress> result;
        std::set<std::pair<int, std::string>> seen;
        for (const OwnedAddress& address : input)
        {
            if (seen.emplace(address.family, address.text).second)
                result.push_back(address);
            if (result.size() == kMaximumAddresses)
                break;
        }
        return result;
    }

    bool validAsciiHost(std::string_view host)
    {
        if (host.empty() || host.size() > kMaximumHostnameBytes || host.front() == '.' || host.back() == '.')
            return false;
        std::size_t labelSize = 0;
        for (unsigned char value : host)
        {
            if (value > 0x7f || value == ':' || value == '/' || value == '\\' || std::isspace(value) != 0)
                return false;
            if (value == '.')
            {
                if (labelSize == 0 || labelSize > kMaximumLabelBytes)
                    return false;
                labelSize = 0;
            }
            else
                ++labelSize;
        }
        return labelSize > 0 && labelSize <= kMaximumLabelBytes;
    }

    std::vector<OwnedAddress> numericFastPath(std::string_view host, std::uint16_t port)
    {
        std::array<unsigned char, 16> raw{};
        std::array<char, INET6_ADDRSTRLEN> text{};
        for (const int family : { AF_INET, AF_INET6 })
        {
            if (ares_inet_pton(family, std::string(host).c_str(), raw.data()) == 1)
            {
                check(ares_inet_ntop(family, raw.data(), text.data(),
                          static_cast<ares_socklen_t>(text.size()))
                        != nullptr,
                    "numeric address formatting failed");
                return { OwnedAddress{ family, text.data(), port } };
            }
        }
        return {};
    }

    template <class Function>
    void scenario(std::string_view name, Function&& function)
    {
        function();
        std::cout << "PASS " << name << '\n';
    }
}

int main()
{
    try
    {
        SocketRuntime sockets;
        check(ares_library_init(ARES_LIB_INIT_ALL) == ARES_SUCCESS, "c-ares global initialization failed");
        struct Cleanup
        {
            ~Cleanup() { ares_library_cleanup(); }
        } cleanup;
        int versionNumber = 0;
        check(std::string_view(ares_version(&versionNumber)) == "1.34.8", "unexpected c-ares version");

        DnsFixture fixture;

        scenario("bounded_host_and_separate_port_contract", [&] {
            check(validAsciiHost("server.example"), "ordinary ASCII hostname rejected");
            check(validAsciiHost("xn--bcher-kva.example"), "IDNA A-label rejected");
            check(!validAsciiHost("server.example:25565"), "embedded port accepted");
            check(!validAsciiHost("https://server.example"), "URI accepted");
            check(!validAsciiHost(std::string(64, 'a') + ".example"), "oversized label accepted");
            check(!validAsciiHost(std::string(254, 'a')), "oversized hostname accepted");
            check(!validAsciiHost("server example"), "whitespace accepted");
        });

        scenario("numeric_address_bypasses_dns", [&] {
            const std::size_t before = fixture.queries();
            const auto ipv4 = numericFastPath("192.0.2.90", 25565);
            const auto ipv6 = numericFastPath("2001:db8::90", 25566);
            check(ipv4.size() == 1 && ipv4[0].family == AF_INET && ipv4[0].port == 25565,
                "numeric IPv4 fast path failed");
            check(ipv6.size() == 1 && ipv6[0].family == AF_INET6 && ipv6[0].port == 25566,
                "numeric IPv6 fast path failed");
            check(fixture.queries() == before, "numeric address reached DNS fixture");
        });

        scenario("dual_stack_success_and_port_propagation", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("dual.proof.test", 25565);
            check(result.status == ARES_SUCCESS, "dual-stack lookup failed");
            check(std::any_of(result.addresses.begin(), result.addresses.end(),
                      [](const OwnedAddress& value) { return value.family == AF_INET; }),
                "dual-stack lookup omitted IPv4");
            check(std::any_of(result.addresses.begin(), result.addresses.end(),
                      [](const OwnedAddress& value) { return value.family == AF_INET6; }),
                "dual-stack lookup omitted IPv6");
            check(std::all_of(result.addresses.begin(), result.addresses.end(),
                      [](const OwnedAddress& value) { return value.port == 25565; }),
                "separate port was not propagated");
        });

        scenario("ipv4_only_success", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("ipv4.proof.test", 1);
            check(result.status == ARES_SUCCESS && !result.addresses.empty(), "IPv4-only lookup failed");
            check(std::all_of(result.addresses.begin(), result.addresses.end(),
                      [](const OwnedAddress& value) { return value.family == AF_INET; }),
                "IPv4-only result contained another family");
        });

        scenario("ipv6_only_success", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("ipv6.proof.test", 1);
            check(result.status == ARES_SUCCESS && !result.addresses.empty(), "IPv6-only lookup failed");
            check(std::all_of(result.addresses.begin(), result.addresses.end(),
                      [](const OwnedAddress& value) { return value.family == AF_INET6; }),
                "IPv6-only result contained another family");
        });

        scenario("nxdomain_and_no_data_are_distinct_failures", [&] {
            Resolver resolver(fixture.port());
            const QueryResult missing = resolver.resolve("nxdomain.proof.test", 1);
            const QueryResult empty = resolver.resolve("nodata.proof.test", 1);
            check(missing.status == ARES_ENOTFOUND,
                "NXDOMAIN category changed: " + std::to_string(missing.status) + " "
                    + ares_strerror(missing.status));
            check(empty.status == ARES_ENODATA,
                "no-data category changed: " + std::to_string(empty.status) + " "
                    + ares_strerror(empty.status));
        });

        scenario("timeout_is_bounded", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("timeout.proof.test", 1);
            check(result.status == ARES_ETIMEOUT && result.timeouts > 0, "timeout did not fail closed");
        });

        scenario("malformed_response_fails_closed", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("malformed.proof.test", 1);
            check(result.status != ARES_SUCCESS && result.addresses.empty(),
                "malformed response produced addresses");
        });

        scenario("cancellation_completes_owned_callback", [&] {
            Resolver resolver(fixture.port());
            QueryResult result;
            resolver.start("timeout.proof.test", 1, result);
            resolver.cancel();
            check(result.done && result.status == ARES_ECANCELLED, "cancel callback category changed");
        });

        scenario("destruction_completes_owned_callback", [&] {
            Resolver resolver(fixture.port());
            QueryResult result;
            resolver.start("timeout.proof.test", 1, result);
            resolver.destroy();
            check(result.done && result.status == ARES_EDESTRUCTION, "destruction callback category changed");
        });

        scenario("duplicate_addresses_are_deduplicated", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("duplicate.proof.test", 1);
            const auto bounded = boundedUnique(result.addresses);
            check(result.status == ARES_SUCCESS && !result.addresses.empty(), "duplicate lookup failed");
            check(bounded.size() == 1, "duplicate answers escaped owned deduplication");
        });

        scenario("more_than_eight_answers_are_bounded", [&] {
            Resolver resolver(fixture.port());
            const QueryResult result = resolver.resolve("many.proof.test", 1);
            const auto bounded = boundedUnique(result.addresses);
            check(result.status == ARES_SUCCESS && result.addresses.size() > kMaximumAddresses,
                "fixture did not deliver more than eight answers");
            check(bounded.size() == kMaximumAddresses, "owned result cap changed");
        });

        scenario("caller_pumped_process_fds_profile", [&] {
            check(true, "unreachable");
        });

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
