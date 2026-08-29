#include "tes3mp/transport_gns.hpp"
#include "transport_gns_detail.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
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
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingsockets_flat.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace
{
    using NumericAddress = TES3MP::Detail::NumericAddress;
    using NumericAddressFamily = TES3MP::Detail::NumericAddressFamily;

    constexpr std::uint16_t ReliableOrderedLane = 0;
    constexpr std::uint16_t LatestWinsLane = 1;
    constexpr int TransportLaneCount = 2;

    std::optional<std::uint16_t> laneFor(TES3MP::TransportChannel channel)
    {
        switch (channel)
        {
            case TES3MP::TransportChannel::ReliableOrdered:
                return ReliableOrderedLane;
            case TES3MP::TransportChannel::LatestWins:
                return LatestWinsLane;
        }
        return std::nullopt;
    }

    std::optional<TES3MP::TransportChannel> channelFor(std::uint16_t lane)
    {
        if (lane == ReliableOrderedLane)
            return TES3MP::TransportChannel::ReliableOrdered;
        if (lane == LatestWinsLane)
            return TES3MP::TransportChannel::LatestWins;
        return std::nullopt;
    }

    int sendFlags(TES3MP::TransportChannel channel)
    {
        return channel == TES3MP::TransportChannel::ReliableOrdered ? k_nSteamNetworkingSend_Reliable
                                                                    : k_nSteamNetworkingSend_UnreliableNoDelay;
    }

    TES3MP::Detail::HappyEyeballsAttempt::TimePoint monotonicNow()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }

#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket kInvalidSocket = -1;
#endif

    void closeNativeSocket(NativeSocket socket)
    {
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }

    std::optional<std::uint16_t> findAvailableUdpPort(const TES3MP::ListenerEndpoint& endpoint)
    {
        const int family = endpoint.hostKind() == TES3MP::EndpointHostKind::Ipv4 ? AF_INET : AF_INET6;
        const NativeSocket probe = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (probe == kInvalidSocket)
            return std::nullopt;
        bool bound = false;
        std::uint16_t port = 0;
        if (family == AF_INET)
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = 0;
            if (inet_pton(AF_INET, endpoint.address().c_str(), &address.sin_addr) == 1
                && bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
            {
#ifdef _WIN32
                int size = sizeof(address);
#else
                socklen_t size = sizeof(address);
#endif
                bound = getsockname(probe, reinterpret_cast<sockaddr*>(&address), &size) == 0;
                port = ntohs(address.sin_port);
            }
        }
        else
        {
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_port = 0;
            if (inet_pton(AF_INET6, endpoint.address().c_str(), &address.sin6_addr) == 1
                && bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
            {
#ifdef _WIN32
                int size = sizeof(address);
#else
                socklen_t size = sizeof(address);
#endif
                bound = getsockname(probe, reinterpret_cast<sockaddr*>(&address), &size) == 0;
                port = ntohs(address.sin6_port);
            }
        }
        closeNativeSocket(probe);
        return bound && port != 0 ? std::optional<std::uint16_t>(port) : std::nullopt;
    }

    class Resolver
    {
    public:
        using Completion = void (*)(void*, int, std::vector<NumericAddress>, bool);

        Resolver(void* context, Completion completion)
            : mContext(context)
            , mCompletion(completion)
        {
            mIpv4 = { this, AF_INET };
            mIpv6 = { this, AF_INET6 };
            ares_options options{};
            options.flags = ARES_FLAG_NOSEARCH;
            options.qcache_max_ttl = 0;
            options.sock_state_cb = &Resolver::socketStateChanged;
            options.sock_state_cb_data = this;
            const int mask = ARES_OPT_FLAGS | ARES_OPT_QUERY_CACHE | ARES_OPT_SOCK_STATE_CB;
            mValid = ares_init_options(&mChannel, &options, mask) == ARES_SUCCESS;
        }

        Resolver(const Resolver&) = delete;
        Resolver& operator=(const Resolver&) = delete;

        ~Resolver()
        {
            if (mChannel != nullptr)
                ares_destroy(mChannel);
        }

        bool valid() const noexcept { return mValid; }

        void start(const TES3MP::ConnectionEndpoint& endpoint)
        {
            mHost = endpoint.host();
            mService = std::to_string(endpoint.port());
            startFamily(mIpv6);
            startFamily(mIpv4);
        }

        void cancel()
        {
            if (mChannel != nullptr)
                ares_cancel(mChannel);
        }

        bool pump()
        {
            if (mChannel == nullptr || mCompleted)
                return true;

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

            timeval wait{};
            std::vector<ares_fd_events_t> ready;
            if (!mSockets.empty())
            {
                const int selected = select(static_cast<int>(maximum + 1), &readable, &writable, nullptr, &wait);
                if (selected < 0)
                    return false;
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
            return ares_process_fds(
                       mChannel, ready.empty() ? nullptr : ready.data(), ready.size(), ARES_PROCESS_FLAG_NONE)
                == ARES_SUCCESS;
        }

    private:
        struct Query
        {
            Resolver* owner = nullptr;
            int family = AF_UNSPEC;
            int status = ARES_EDESTRUCTION;
            bool completed = false;
        };

        void startFamily(Query& query)
        {
            ares_addrinfo_hints hints{};
            hints.ai_family = query.family;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            hints.ai_flags = ARES_AI_NUMERICSERV;
            ares_getaddrinfo(mChannel, mHost.c_str(), mService.c_str(), &hints, &Resolver::addressComplete, &query);
        }

        static void socketStateChanged(void* data, ares_socket_t socket, int readable, int writable)
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

        static void addressComplete(void* data, int status, int, ares_addrinfo* info)
        {
            auto& query = *static_cast<Query*>(data);
            Resolver& self = *query.owner;
            std::vector<NumericAddress> addresses;
            std::set<std::string> unique;
            if (status == ARES_SUCCESS && info != nullptr)
            {
                for (ares_addrinfo_node* node = info->nodes;
                    node != nullptr && addresses.size() < TES3MP::TransportLimits::MaxResolvedAddresses;
                    node = node->ai_next)
                {
                    std::array<char, INET6_ADDRSTRLEN> text{};
                    const void* raw = nullptr;
                    std::uint16_t port = 0;
                    NumericAddressFamily family = NumericAddressFamily::Ipv4;
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
                        family = NumericAddressFamily::Ipv6;
                    }
                    if (raw == nullptr
                        || ares_inet_ntop(node->ai_family, raw, text.data(), static_cast<ares_socklen_t>(text.size()))
                            == nullptr)
                        continue;
                    std::string key(text.data());
                    if (unique.insert(key).second)
                        addresses.push_back({ std::move(key), port, family });
                }
            }
            if (info != nullptr)
                ares_freeaddrinfo(info);
            query.status = status;
            query.completed = true;
            self.mAnyAddress |= !addresses.empty();
            self.mCompleted = self.mIpv4.completed && self.mIpv6.completed;
            if (!self.mCompleted)
            {
                if (!addresses.empty())
                    self.mCompletion(self.mContext, ARES_SUCCESS, std::move(addresses), false);
                return;
            }

            int finalStatus = ARES_SUCCESS;
            if (!self.mAnyAddress)
            {
                const auto noData = [](int value) { return value == ARES_ENODATA || value == ARES_ENOTFOUND; };
                finalStatus = noData(self.mIpv4.status) && noData(self.mIpv6.status) ? ARES_ENODATA
                    : !noData(self.mIpv6.status)                                     ? self.mIpv6.status
                                                                                     : self.mIpv4.status;
            }
            self.mCompletion(self.mContext, finalStatus, std::move(addresses), true);
        }

        void* mContext = nullptr;
        Completion mCompletion = nullptr;
        ares_channel_t* mChannel = nullptr;
        std::map<ares_socket_t, unsigned int> mSockets;
        std::string mHost;
        std::string mService;
        Query mIpv4;
        Query mIpv6;
        bool mValid = false;
        bool mCompleted = false;
        bool mAnyAddress = false;
    };

    class GameNetworkingSocketsRuntime final : public TES3MP::TransportRuntime
    {
    public:
        GameNetworkingSocketsRuntime(TES3MP::TransportLimits limits, std::span<const std::byte> admissionScopeKey)
            : mLimits(limits)
        {
            if (admissionScopeKey.size() != mAdmissionScopeKey.size())
                return;
            std::ranges::copy(admissionScopeKey, mAdmissionScopeKey.begin());
            mAdmissionScopeKeyInitialized = true;
            SteamDatagramErrMsg error{};
            if (sActive != nullptr)
            {
                mFactoryFailure = TES3MP::TransportFactoryFailure::RuntimeAlreadyActive;
                return;
            }
            if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS)
                return;
            mCaresInitialized = true;
            if (!GameNetworkingSockets_Init(nullptr, error))
                return;
            mGnsInitialized = true;
            sActive = this;
            if (!SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetConnectionStatusChanged(
                    SteamNetworkingUtils(), &statusChangedCallback))
                return;
            mValid = true;
        }

        ~GameNetworkingSocketsRuntime() override
        {
            shutdown();
            if (mGnsInitialized)
            {
                SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetConnectionStatusChanged(
                    SteamNetworkingUtils(), nullptr);
                if (sActive == this)
                    sActive = nullptr;
                GameNetworkingSockets_Kill();
            }
            if (mCaresInitialized)
                ares_library_cleanup();
            OPENSSL_cleanse(mAdmissionScopeKey.data(), mAdmissionScopeKey.size());
        }

        bool valid() const noexcept { return mValid; }
        TES3MP::TransportFactoryFailure factoryFailure() const noexcept { return mFactoryFailure; }

        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint& endpoint) override
        {
            if (!available())
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            if (mListeners.size() >= mLimits.listeners)
                return { TES3MP::TransportResult::AtCapacity, std::nullopt };
            const auto id = allocate(mNextListener);
            if (!id)
                return exhausted<TES3MP::ListenerId>();
            const auto generation = allocateCallbackGeneration();
            if (!generation)
                return { TES3MP::TransportResult::CounterExhausted, std::nullopt };

            SteamNetworkingIPAddr address;
            address.Clear();
            if (!address.ParseString(endpoint.address().c_str()))
                return { TES3MP::TransportResult::InvalidInput, std::nullopt };
            const auto selectedPort
                = endpoint.port() == 0 ? findAvailableUdpPort(endpoint) : std::optional(endpoint.port());
            if (!selectedPort)
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            address.m_port = *selectedPort;
            std::array<SteamNetworkingConfigValue_t, 4> options;
            options[0].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
            options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, static_cast<std::int64_t>(*generation));
            options[2].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,
                static_cast<std::int32_t>(TES3MP::LatestWinsMaximumMessageBytes));
            options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,
                static_cast<std::int32_t>(TES3MP::TransportRuntime::MaxMessagesPerReceive));
            const HSteamListenSocket handle = SteamAPI_ISteamNetworkingSockets_CreateListenSocketIP(
                sockets(), address, static_cast<int>(options.size()), options.data());
            if (handle == k_HSteamListenSocket_Invalid)
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            if (!SteamAPI_ISteamNetworkingSockets_GetListenSocketAddress(sockets(), handle, &address))
            {
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), handle);
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            }

            std::array<char, 128> text{};
            address.ToString(text.data(), text.size(), false);
            auto bound = TES3MP::ListenerEndpoint::create(text.data(), address.m_port);
            if (!bound)
            {
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), handle);
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            }
            if (!mListenerHandles.bind(handle, *id, *generation))
            {
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), handle);
                failRuntime(TES3MP::TransportFailure::DependencyFailure);
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            }
            mListeners.emplace(*id, Listener{ handle, *bound, *generation });
            if (!queue({ TES3MP::TransportEventKind::ListenerStarted, TES3MP::TransportFailure::None, *id, std::nullopt,
                    std::nullopt, *bound }))
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            return { TES3MP::TransportResult::Accepted, *id };
        }

        TES3MP::TransportResult stopListener(TES3MP::ListenerId listener) override
        {
            if (mFailed)
                return TES3MP::TransportResult::RuntimeFailed;
            auto found = mListeners.find(listener);
            if (found == mListeners.end())
                return finalized(listener, mNextListener);
            SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), found->second.handle);
            mListenerHandles.erase(found->second.handle);
            mListeners.erase(found);

            std::vector<TES3MP::TransportConnectionId> pending;
            for (const auto& [connection, pendingListener] : mIncomingListeners)
            {
                const auto owned = mConnections.find(connection);
                if (pendingListener == listener && owned != mConnections.end() && owned->second.incomingPending)
                    pending.push_back(connection);
            }
            for (const auto connection : pending)
                closeIncomingPending(connection, TES3MP::TransportFailure::LocalClose);

            if (!queue({ TES3MP::TransportEventKind::ListenerStopped, TES3MP::TransportFailure::LocalClose, listener }))
                return TES3MP::TransportResult::RuntimeFailed;
            return TES3MP::TransportResult::Accepted;
        }

        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(
            const TES3MP::ConnectionEndpoint& endpoint) override
        {
            if (!available())
                return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
            if (mAttempts.size() + mIncomingPending >= mLimits.pendingAttempts)
                return { TES3MP::TransportResult::AtCapacity, std::nullopt };
            const auto id = allocate(mNextAttempt);
            if (!id)
                return exhausted<TES3MP::ConnectAttemptId>();

            auto attempt = std::make_unique<Attempt>();
            attempt->id = *id;
            attempt->endpoint = endpoint;
            if (endpoint.requiresDns())
            {
                attempt->resolver = std::make_unique<Resolver>(attempt.get(), &resolverComplete);
                if (!attempt->resolver->valid())
                    return { TES3MP::TransportResult::RuntimeFailed, std::nullopt };
                attempt->resolver->start(endpoint);
            }
            else
            {
                const NumericAddress address{ endpoint.host(), endpoint.port(),
                    endpoint.hostKind() == TES3MP::EndpointHostKind::Ipv6 ? NumericAddressFamily::Ipv6
                                                                          : NumericAddressFamily::Ipv4 };
                attempt->race.addResolution(std::span<const NumericAddress>(&address, 1),
                    TES3MP::Detail::ResolutionCompletion::Success, monotonicNow());
            }
            mAttempts.emplace(*id, std::move(attempt));
            if (!endpoint.requiresDns())
                launchAvailable(*id, true);
            return { TES3MP::TransportResult::Accepted, *id };
        }

        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId attempt) override
        {
            if (mFailed)
                return TES3MP::TransportResult::RuntimeFailed;
            auto found = mAttempts.find(attempt);
            if (found == mAttempts.end())
                return finalized(attempt, mNextAttempt);
            found->second->cancelled = true;
            if (found->second->resolver)
                found->second->resolver->cancel();
            found->second->race.cancel();
            closeCandidates(*found->second);
            if (!queue({ TES3MP::TransportEventKind::ConnectCancelled, TES3MP::TransportFailure::LocalClose,
                    std::nullopt, attempt }))
                return TES3MP::TransportResult::RuntimeFailed;
            mAttempts.erase(found);
            return TES3MP::TransportResult::Accepted;
        }

        TES3MP::TransportResult send(TES3MP::TransportConnectionId connection, TES3MP::TransportChannel channel,
            std::span<const std::byte> message) override
        {
            if (mFailed)
                return TES3MP::TransportResult::RuntimeFailed;
            const auto lane = laneFor(channel);
            const auto maximumBytes = TES3MP::maximumTransportMessageBytes(channel);
            if (!lane || !maximumBytes || message.empty())
                return TES3MP::TransportResult::InvalidInput;
            if (message.size() > *maximumBytes)
                return TES3MP::TransportResult::MessageTooLarge;
            const auto found = mConnections.find(connection);
            if (found == mConnections.end())
                return finalized(connection, mNextConnection);
            if (found->second.incomingPending)
                return TES3MP::TransportResult::NotReady;

            SteamNetworkingMessage_t* outgoing = SteamAPI_ISteamNetworkingUtils_AllocateMessage(
                SteamNetworkingUtils(), static_cast<int>(message.size()));
            if (outgoing == nullptr)
                return TES3MP::TransportResult::RuntimeFailed;
            std::memcpy(outgoing->m_pData, message.data(), message.size());
            outgoing->m_conn = found->second.handle;
            outgoing->m_nFlags = sendFlags(channel);
            outgoing->m_idxLane = *lane;
            std::int64_t result = 0;
            SteamAPI_ISteamNetworkingSockets_SendMessages(sockets(), 1, &outgoing, &result, true);
            if (result > 0)
                return TES3MP::TransportResult::Accepted;
            const auto failure = static_cast<EResult>(-result);
            if (failure == k_EResultLimitExceeded || failure == k_EResultIgnored)
                return TES3MP::TransportResult::WouldBlock;
            if (failure == k_EResultInvalidParam)
                return TES3MP::TransportResult::InvalidInput;
            if (failure == k_EResultInvalidState || failure == k_EResultNoConnection)
                return TES3MP::TransportResult::NotReady;
            return TES3MP::TransportResult::RuntimeFailed;
        }

        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId connection, std::span<TES3MP::TransportMessage> output) override
        {
            if (mFailed)
                return { TES3MP::TransportResult::RuntimeFailed, 0 };
            if (output.size() > TES3MP::TransportRuntime::MaxMessagesPerReceive)
                return { TES3MP::TransportResult::InvalidInput, 0 };
            const auto found = mConnections.find(connection);
            if (found == mConnections.end())
                return { finalized(connection, mNextConnection), 0 };
            if (found->second.incomingPending)
                return { TES3MP::TransportResult::NotReady, 0 };
            if (output.empty())
                return { TES3MP::TransportResult::Accepted, 0 };

            std::array<SteamNetworkingMessage_t*, TES3MP::TransportRuntime::MaxMessagesPerReceive> incoming{};
            const int received = SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection(
                sockets(), found->second.handle, incoming.data(), static_cast<int>(output.size()));
            if (received < 0)
            {
                failRuntime(TES3MP::TransportFailure::DependencyFailure);
                return { TES3MP::TransportResult::RuntimeFailed, 0 };
            }

            std::vector<TES3MP::TransportMessage> validated;
            validated.reserve(static_cast<std::size_t>(received));
            bool invalid = false;
            for (int index = 0; index < received; ++index)
            {
                SteamNetworkingMessage_t* value = incoming[static_cast<std::size_t>(index)];
                const auto channel = value == nullptr ? std::nullopt : channelFor(value->m_idxLane);
                const auto maximumBytes
                    = channel ? TES3MP::maximumTransportMessageBytes(*channel) : std::optional<std::size_t>{};
                const bool isReliable = value != nullptr && (value->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
                const bool expectedReliability
                    = channel && ((*channel == TES3MP::TransportChannel::ReliableOrdered) == isReliable);
                if (value == nullptr || value->m_cbSize <= 0 || value->m_pData == nullptr || !maximumBytes
                    || static_cast<std::size_t>(value->m_cbSize) > *maximumBytes || !expectedReliability)
                {
                    invalid = true;
                }
                else if (!invalid)
                {
                    const auto* begin = static_cast<const std::byte*>(value->m_pData);
                    validated.push_back(
                        { *channel, std::vector<std::byte>(begin, begin + static_cast<std::size_t>(value->m_cbSize)) });
                }
            }
            for (int index = 0; index < received; ++index)
            {
                if (incoming[static_cast<std::size_t>(index)] != nullptr)
                    incoming[static_cast<std::size_t>(index)]->Release();
            }
            if (invalid)
            {
                closeInvalidMessage(connection);
                return { mFailed ? TES3MP::TransportResult::RuntimeFailed : TES3MP::TransportResult::ProtocolViolation,
                    0 };
            }
            for (std::size_t index = 0; index < validated.size(); ++index)
                output[index] = std::move(validated[index]);
            return { TES3MP::TransportResult::Accepted, validated.size() };
        }

        TES3MP::TransportResult close(
            TES3MP::TransportConnectionId connection, TES3MP::TransportCloseMode mode) override
        {
            if (mFailed)
                return TES3MP::TransportResult::RuntimeFailed;
            auto found = mConnections.find(connection);
            if (found == mConnections.end())
                return finalized(connection, mNextConnection);
            const HSteamNetConnection handle = found->second.handle;
            SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), handle, 0,
                mode == TES3MP::TransportCloseMode::Graceful ? "graceful" : nullptr,
                mode == TES3MP::TransportCloseMode::Graceful);
            if (found->second.incomingPending && mIncomingPending > 0)
                --mIncomingPending;
            mConnectionHandles.erase(handle);
            mIncomingListeners.erase(connection);
            mConnections.erase(found);
            if (!queue({ TES3MP::TransportEventKind::ConnectionClosed, TES3MP::TransportFailure::LocalClose,
                    std::nullopt, std::nullopt, connection }))
                return TES3MP::TransportResult::RuntimeFailed;
            return TES3MP::TransportResult::Accepted;
        }

        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            if (!mShutdown && !mFailed)
            {
                std::vector<TES3MP::ConnectAttemptId> attempts;
                attempts.reserve(mAttempts.size());
                for (const auto& [id, _] : mAttempts)
                    attempts.push_back(id);
                for (const auto id : attempts)
                {
                    auto found = mAttempts.find(id);
                    if (found != mAttempts.end() && found->second->resolver && !found->second->resolver->pump())
                    {
                        failAttempt(id, TES3MP::TransportFailure::DependencyFailure);
                        continue;
                    }
                }
                SteamAPI_ISteamNetworkingSockets_RunCallbacks(sockets());
                attempts.clear();
                for (const auto& [id, _] : mAttempts)
                    attempts.push_back(id);
                for (const auto id : attempts)
                    launchAvailable(id, false);
            }

            const std::size_t count = std::min(output.size(), mEvents.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                output[index] = std::move(mEvents.front());
                mEvents.pop_front();
            }
            return { mFailed ? TES3MP::TransportResult::RuntimeFailed : TES3MP::TransportResult::Accepted, count };
        }

        TES3MP::TransportResult shutdown() override
        {
            if (mShutdown)
                return TES3MP::TransportResult::AlreadyFinalized;
            if (mFailed)
            {
                mShutdown = true;
                return TES3MP::TransportResult::RuntimeFailed;
            }
            mShutdown = true;
            while (!mAttempts.empty())
            {
                auto found = mAttempts.begin();
                auto attempt = std::move(found->second);
                mAttempts.erase(found);
                if (attempt->resolver)
                    attempt->resolver->cancel();
                attempt->race.cancel();
                closeCandidates(*attempt);
                queue({ TES3MP::TransportEventKind::ConnectCancelled, TES3MP::TransportFailure::Shutdown, std::nullopt,
                    attempt->id });
                if (mFailed)
                    return TES3MP::TransportResult::RuntimeFailed;
            }
            while (!mConnections.empty())
            {
                const auto found = mConnections.begin();
                const auto id = found->first;
                const auto connection = found->second;
                mConnections.erase(found);
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), connection.handle, 0, nullptr, false);
                mConnectionHandles.erase(connection.handle);
                mIncomingListeners.erase(id);
                queue({ TES3MP::TransportEventKind::ConnectionClosed, TES3MP::TransportFailure::Shutdown, std::nullopt,
                    std::nullopt, id });
                if (mFailed)
                    return TES3MP::TransportResult::RuntimeFailed;
            }
            while (!mListeners.empty())
            {
                const auto found = mListeners.begin();
                const auto id = found->first;
                const auto listener = found->second;
                mListeners.erase(found);
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), listener.handle);
                mListenerHandles.erase(listener.handle);
                queue({ TES3MP::TransportEventKind::ListenerStopped, TES3MP::TransportFailure::Shutdown, id });
                if (mFailed)
                    return TES3MP::TransportResult::RuntimeFailed;
            }
            return TES3MP::TransportResult::Accepted;
        }

    private:
        struct Listener
        {
            HSteamListenSocket handle = k_HSteamListenSocket_Invalid;
            TES3MP::ListenerEndpoint endpoint;
            std::uint64_t generation = 0;
        };

        struct Attempt
        {
            struct Candidate
            {
                HSteamNetConnection handle = k_HSteamNetConnection_Invalid;
                std::uint64_t ordinal = 0;
                std::uint64_t generation = 0;
            };

            TES3MP::ConnectAttemptId id = TES3MP::ConnectAttemptId::initial();
            TES3MP::ConnectionEndpoint endpoint = *TES3MP::ConnectionEndpoint::create("127.0.0.1", 1);
            std::unique_ptr<Resolver> resolver;
            TES3MP::Detail::HappyEyeballsAttempt race;
            std::vector<Candidate> candidates;
            bool cancelled = false;
        };

        struct Connection
        {
            HSteamNetConnection handle = k_HSteamNetConnection_Invalid;
            bool incomingPending = false;
            std::uint64_t generation = 0;
            std::optional<TES3MP::AdmissionScopeId> admissionScope;
        };

        static ISteamNetworkingSockets* sockets() { return SteamNetworkingSockets(); }

        bool available() const noexcept { return mValid && !mShutdown && !mFailed; }

        std::optional<std::uint64_t> allocateCallbackGeneration()
        {
            const auto generation = mCallbackGenerations.allocate();
            if (!generation || *generation > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                failRuntime(TES3MP::TransportFailure::CounterExhausted);
                return std::nullopt;
            }
            return generation;
        }

        static std::optional<std::uint64_t> callbackGeneration(const SteamNetConnectionInfo_t& info) noexcept
        {
            if (info.m_nUserData <= 0)
                return std::nullopt;
            return static_cast<std::uint64_t>(info.m_nUserData);
        }

        std::optional<TES3MP::AdmissionScopeId> admissionScope(const SteamNetworkingIPAddr& address) const noexcept
        {
            if (!mAdmissionScopeKeyInitialized)
                return std::nullopt;
            if (address.IsIPv4())
            {
                const std::uint32_t value = address.GetIPv4();
                std::array source{ static_cast<std::byte>(value >> 24), static_cast<std::byte>(value >> 16),
                    static_cast<std::byte>(value >> 8), static_cast<std::byte>(value) };
                const auto result = TES3MP::Detail::deriveAdmissionScope(
                    mAdmissionScopeKey, TES3MP::Detail::NumericAddressFamily::Ipv4, source);
                OPENSSL_cleanse(source.data(), source.size());
                return result;
            }
            std::array<std::byte, 16> source{};
            for (std::size_t index = 0; index < source.size(); ++index)
                source[index] = static_cast<std::byte>(address.m_ipv6[index]);
            const auto result = TES3MP::Detail::deriveAdmissionScope(
                mAdmissionScopeKey, TES3MP::Detail::NumericAddressFamily::Ipv6, source);
            OPENSSL_cleanse(source.data(), source.size());
            return result;
        }

        template <class Id>
        static std::optional<Id> allocate(std::optional<Id>& next)
        {
            if (!next)
                return std::nullopt;
            const Id result = *next;
            next = next->next();
            return result;
        }

        template <class Id>
        TES3MP::TransportAdmission<Id> exhausted()
        {
            failRuntime(TES3MP::TransportFailure::CounterExhausted);
            return { TES3MP::TransportResult::CounterExhausted, std::nullopt };
        }

        template <class Id>
        static TES3MP::TransportResult finalized(Id id, const std::optional<Id>& next)
        {
            if (!next || id.value() < next->value())
                return TES3MP::TransportResult::AlreadyFinalized;
            return TES3MP::TransportResult::UnknownId;
        }

        bool queue(TES3MP::TransportEvent event)
        {
            if (mFailed && event.kind != TES3MP::TransportEventKind::RuntimeFailed)
                return false;
            if (mEvents.size() >= mLimits.retainedEvents)
            {
                failRuntime(TES3MP::TransportFailure::EventCapacityExceeded);
                return false;
            }
            mEvents.push_back(std::move(event));
            return true;
        }

        void failRuntime(TES3MP::TransportFailure failure)
        {
            if (mFailed)
                return;
            mFailed = true;
            for (auto& [_, attempt] : mAttempts)
            {
                if (attempt->resolver)
                    attempt->resolver->cancel();
                closeCandidates(*attempt);
            }
            for (const auto& [_, connection] : mConnections)
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), connection.handle, 0, nullptr, false);
            for (const auto& [_, listener] : mListeners)
                SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets(), listener.handle);
            mAttempts.clear();
            mConnections.clear();
            mConnectionHandles.clear();
            mIncomingListeners.clear();
            mIncomingPending = 0;
            mListeners.clear();
            mListenerHandles.clear();
            mEvents.clear();
            mEvents.push_back({ TES3MP::TransportEventKind::RuntimeFailed, failure });
        }

        static void resolverComplete(void* context, int status, std::vector<NumericAddress> addresses, bool completed)
        {
            auto& attempt = *static_cast<Attempt*>(context);
            if (attempt.cancelled)
                return;
            TES3MP::Detail::ResolutionCompletion completion = TES3MP::Detail::ResolutionCompletion::Pending;
            if (completed && status == ARES_SUCCESS)
                completion = TES3MP::Detail::ResolutionCompletion::Success;
            else if (completed && (status == ARES_ENODATA || status == ARES_ENOTFOUND))
                completion = TES3MP::Detail::ResolutionCompletion::NoData;
            else if (completed)
                completion = TES3MP::Detail::ResolutionCompletion::Failed;
            attempt.race.addResolution(std::span<const NumericAddress>(addresses), completion, monotonicNow());
        }

        bool startCandidate(Attempt& attempt, const TES3MP::Detail::CandidateLaunch& launch)
        {
            const auto generation = allocateCallbackGeneration();
            if (!generation)
                return false;
            SteamNetworkingIPAddr address;
            address.Clear();
            if (!address.ParseString(launch.address.host.c_str()))
                return false;
            address.m_port = launch.address.port;
            std::array<SteamNetworkingConfigValue_t, 4> options;
            options[0].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
            options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, static_cast<std::int64_t>(*generation));
            options[2].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,
                static_cast<std::int32_t>(TES3MP::LatestWinsMaximumMessageBytes));
            options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,
                static_cast<std::int32_t>(TES3MP::TransportRuntime::MaxMessagesPerReceive));
            const HSteamNetConnection handle = SteamAPI_ISteamNetworkingSockets_ConnectByIPAddress(
                sockets(), address, static_cast<int>(options.size()), options.data());
            if (handle == k_HSteamNetConnection_Invalid)
                return false;
            if (!mCandidateHandles.bind(handle, attempt.id, *generation))
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), handle, 0, nullptr, false);
                failRuntime(TES3MP::TransportFailure::DependencyFailure);
                return false;
            }
            attempt.candidates.push_back({ handle, launch.ordinal, *generation });
            return true;
        }

        void launchAvailable(TES3MP::ConnectAttemptId id, bool immediate)
        {
            auto found = mAttempts.find(id);
            if (found == mAttempts.end())
                return;
            Attempt& attempt = *found->second;
            while (const auto launch = attempt.race.nextLaunch(monotonicNow(), immediate))
            {
                if (startCandidate(attempt, *launch))
                    break;
                attempt.race.candidateFailed(launch->ordinal);
                if (mFailed)
                    return;
                immediate = true;
            }
            if (attempt.race.shouldFail())
            {
                const auto resolution = attempt.race.resolution();
                failAttempt(id,
                    resolution == TES3MP::Detail::ResolutionCompletion::NoData
                        ? TES3MP::TransportFailure::ResolutionNoData
                        : resolution == TES3MP::Detail::ResolutionCompletion::Failed
                        ? TES3MP::TransportFailure::ResolutionFailed
                        : TES3MP::TransportFailure::ConnectionFailed);
            }
        }

        void closeCandidates(Attempt& attempt, HSteamNetConnection except = k_HSteamNetConnection_Invalid)
        {
            for (const Attempt::Candidate& candidate : attempt.candidates)
            {
                mCandidateHandles.erase(candidate.handle);
                if (candidate.handle != except)
                    SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), candidate.handle, 0, nullptr, false);
            }
            attempt.candidates.clear();
        }

        void failAttempt(TES3MP::ConnectAttemptId id, TES3MP::TransportFailure failure)
        {
            auto found = mAttempts.find(id);
            if (found == mAttempts.end())
                return;
            closeCandidates(*found->second);
            mAttempts.erase(found);
            queue({ TES3MP::TransportEventKind::ConnectFailed, failure, std::nullopt, id });
        }

        void closeIncomingPending(TES3MP::TransportConnectionId id, TES3MP::TransportFailure failure)
        {
            const auto found = mConnections.find(id);
            if (found == mConnections.end() || !found->second.incomingPending)
                return;
            const HSteamNetConnection handle = found->second.handle;
            SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), handle, 0, nullptr, false);
            if (mIncomingPending > 0)
                --mIncomingPending;
            mConnectionHandles.erase(handle);
            mIncomingListeners.erase(id);
            mConnections.erase(found);
            queue({ TES3MP::TransportEventKind::ConnectionClosed, failure, std::nullopt, std::nullopt, id });
        }

        void closeInvalidMessage(TES3MP::TransportConnectionId id)
        {
            const auto found = mConnections.find(id);
            if (found == mConnections.end())
                return;
            const HSteamNetConnection handle = found->second.handle;
            SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), handle, 0, nullptr, false);
            if (found->second.incomingPending && mIncomingPending > 0)
                --mIncomingPending;
            mConnectionHandles.erase(handle);
            mIncomingListeners.erase(id);
            mConnections.erase(found);
            queue({ TES3MP::TransportEventKind::ConnectionClosed, TES3MP::TransportFailure::InvalidMessage,
                std::nullopt, std::nullopt, id });
        }

        bool configureChannels(HSteamNetConnection handle)
        {
            const std::array<int, TransportLaneCount> priorities{ 0, 0 };
            const std::array<std::uint16_t, TransportLaneCount> weights{ 1, 1 };
            return SteamAPI_ISteamNetworkingSockets_ConfigureConnectionLanes(
                       sockets(), handle, TransportLaneCount, priorities.data(), weights.data())
                == k_EResultOK;
        }

        static void statusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info)
        {
            if (sActive != nullptr && info != nullptr)
                sActive->statusChanged(*info);
        }

        void statusChanged(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            const auto generation = callbackGeneration(info.m_info);
            if (!generation)
                return;
            if (const auto* candidate = mCandidateHandles.find(info.m_hConn, *generation))
            {
                outgoingStatus(*candidate, info);
                return;
            }
            if (const auto* connection = mConnectionHandles.find(info.m_hConn, *generation))
            {
                connectionStatus(*connection, info);
                return;
            }
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
            {
                if (const auto* listener = mListenerHandles.find(info.m_info.m_hListenSocket, *generation))
                    incomingStatus(*listener, *generation, info);
            }
        }

        void outgoingStatus(TES3MP::ConnectAttemptId id, const SteamNetConnectionStatusChangedCallback_t& info)
        {
            auto found = mAttempts.find(id);
            if (found == mAttempts.end())
                return;
            Attempt& attempt = *found->second;
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
            {
                if (mConnections.size() >= mLimits.connections)
                {
                    failAttempt(id, TES3MP::TransportFailure::ConnectionFailed);
                    return;
                }
                const auto connection = allocate(mNextConnection);
                if (!connection)
                {
                    failRuntime(TES3MP::TransportFailure::CounterExhausted);
                    return;
                }
                const auto winningCandidate = std::ranges::find_if(
                    attempt.candidates, [&](const Attempt::Candidate& value) { return value.handle == info.m_hConn; });
                if (winningCandidate == attempt.candidates.end()
                    || !attempt.race.candidateSucceeded(winningCandidate->ordinal))
                    return;
                if (!configureChannels(info.m_hConn))
                {
                    failAttempt(id, TES3MP::TransportFailure::DependencyFailure);
                    return;
                }
                const std::uint64_t winningGeneration = winningCandidate->generation;
                closeCandidates(attempt, info.m_hConn);
                mConnections.emplace(*connection, Connection{ info.m_hConn, false, winningGeneration, std::nullopt });
                if (!mConnectionHandles.bind(info.m_hConn, *connection, winningGeneration))
                {
                    failRuntime(TES3MP::TransportFailure::DependencyFailure);
                    return;
                }
                mAttempts.erase(found);
                queue({ TES3MP::TransportEventKind::ConnectSucceeded, TES3MP::TransportFailure::None, std::nullopt, id,
                    *connection });
                return;
            }
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
                || info.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                mCandidateHandles.erase(info.m_hConn);
                const auto failedCandidate = std::ranges::find_if(
                    attempt.candidates, [&](const Attempt::Candidate& value) { return value.handle == info.m_hConn; });
                if (failedCandidate == attempt.candidates.end())
                    return;
                attempt.race.candidateFailed(failedCandidate->ordinal);
                attempt.candidates.erase(failedCandidate);
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                launchAvailable(id, true);
            }
        }

        void incomingStatus(TES3MP::ListenerId listenerId, std::uint64_t listenerGeneration,
            const SteamNetConnectionStatusChangedCallback_t& info)
        {
            if (mIncomingPending + mAttempts.size() >= mLimits.pendingAttempts
                || mConnections.size() >= mLimits.connections)
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(
                    sockets(), info.m_hConn, 0, "admission-bounded", false);
                return;
            }
            const auto connection = allocate(mNextConnection);
            if (!connection)
            {
                failRuntime(TES3MP::TransportFailure::CounterExhausted);
                return;
            }
            const auto generation = allocateCallbackGeneration();
            if (!generation)
                return;
            const auto scope = admissionScope(info.m_info.m_addrRemote);
            if (!scope)
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                failRuntime(TES3MP::TransportFailure::DependencyFailure);
                return;
            }
            if (SteamAPI_ISteamNetworkingSockets_AcceptConnection(sockets(), info.m_hConn) != k_EResultOK)
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                return;
            }
            if (!configureChannels(info.m_hConn))
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                return;
            }
            if (!SteamAPI_ISteamNetworkingSockets_SetConnectionUserData(
                    sockets(), info.m_hConn, static_cast<std::int64_t>(*generation))
                || !mConnectionHandles.bind(info.m_hConn, *connection, *generation, listenerGeneration))
            {
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                failRuntime(TES3MP::TransportFailure::DependencyFailure);
                return;
            }
            ++mIncomingPending;
            mConnections.emplace(*connection, Connection{ info.m_hConn, true, *generation, *scope });
            mIncomingListeners.emplace(*connection, listenerId);
        }

        void connectionStatus(TES3MP::TransportConnectionId id, const SteamNetConnectionStatusChangedCallback_t& info)
        {
            auto found = mConnections.find(id);
            if (found == mConnections.end())
                return;
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected && found->second.incomingPending)
            {
                found->second.incomingPending = false;
                mConnectionHandles.retireInherited(info.m_hConn);
                if (mIncomingPending > 0)
                    --mIncomingPending;
                const auto listener = mIncomingListeners.find(id);
                queue({ TES3MP::TransportEventKind::ConnectionAccepted, TES3MP::TransportFailure::None,
                    listener == mIncomingListeners.end() ? std::nullopt
                                                         : std::optional<TES3MP::ListenerId>(listener->second),
                    std::nullopt, id, std::nullopt, TES3MP::TransportSecurity::EncryptedUnauthenticated,
                    found->second.admissionScope });
                return;
            }
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
                || info.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                if (found->second.incomingPending && mIncomingPending > 0)
                    --mIncomingPending;
                mConnectionHandles.erase(info.m_hConn);
                mIncomingListeners.erase(id);
                mConnections.erase(found);
                SteamAPI_ISteamNetworkingSockets_CloseConnection(sockets(), info.m_hConn, 0, nullptr, false);
                queue({ TES3MP::TransportEventKind::ConnectionClosed,
                    info.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
                        ? TES3MP::TransportFailure::PeerClosed
                        : TES3MP::TransportFailure::ConnectionFailed,
                    std::nullopt, std::nullopt, id });
            }
        }

        TES3MP::TransportLimits mLimits;
        std::optional<TES3MP::ListenerId> mNextListener = TES3MP::ListenerId::initial();
        std::optional<TES3MP::ConnectAttemptId> mNextAttempt = TES3MP::ConnectAttemptId::initial();
        std::optional<TES3MP::TransportConnectionId> mNextConnection = TES3MP::TransportConnectionId::initial();
        std::map<TES3MP::ListenerId, Listener> mListeners;
        TES3MP::Detail::GenerationBindingTable<HSteamListenSocket, TES3MP::ListenerId> mListenerHandles;
        std::map<TES3MP::ConnectAttemptId, std::unique_ptr<Attempt>> mAttempts;
        TES3MP::Detail::GenerationBindingTable<HSteamNetConnection, TES3MP::ConnectAttemptId> mCandidateHandles;
        std::map<TES3MP::TransportConnectionId, Connection> mConnections;
        TES3MP::Detail::GenerationBindingTable<HSteamNetConnection, TES3MP::TransportConnectionId> mConnectionHandles;
        std::map<TES3MP::TransportConnectionId, TES3MP::ListenerId> mIncomingListeners;
        std::deque<TES3MP::TransportEvent> mEvents;
        std::size_t mIncomingPending = 0;
        TES3MP::Detail::GenerationCounter mCallbackGenerations;
        std::array<std::byte, TES3MP::AdmissionScopeIdBytes> mAdmissionScopeKey{};
        bool mAdmissionScopeKeyInitialized = false;
        bool mCaresInitialized = false;
        bool mGnsInitialized = false;
        bool mValid = false;
        bool mShutdown = false;
        bool mFailed = false;
        TES3MP::TransportFactoryFailure mFactoryFailure = TES3MP::TransportFactoryFailure::DependencyInitialization;

        static inline GameNetworkingSocketsRuntime* sActive = nullptr;
    };
}

namespace TES3MP
{
    TransportFactoryResult makeGameNetworkingSocketsTransport(TransportLimits limits) noexcept
    {
        if (limits.listeners == 0 || limits.listeners > TransportLimits::MaxListeners || limits.pendingAttempts == 0
            || limits.pendingAttempts > TransportLimits::MaxPendingAttempts || limits.connections == 0
            || limits.connections > TransportLimits::MaxConnections || limits.retainedEvents == 0
            || limits.retainedEvents > TransportLimits::MaxRetainedEvents)
            return { TransportFactoryFailure::InvalidLimits, nullptr };
        std::array<std::byte, AdmissionScopeIdBytes> key{};
        if (RAND_bytes(reinterpret_cast<unsigned char*>(key.data()), static_cast<int>(key.size())) != 1)
            return { TransportFactoryFailure::DependencyInitialization, nullptr };
        auto result = Detail::makeGameNetworkingSocketsTransportWithAdmissionScopeKey(limits, key);
        OPENSSL_cleanse(key.data(), key.size());
        return result;
    }
}

namespace TES3MP::Detail
{
    TransportFactoryResult makeGameNetworkingSocketsTransportWithAdmissionScopeKey(
        TransportLimits limits, std::span<const std::byte> key) noexcept
    {
        if (key.size() != AdmissionScopeIdBytes)
            return { TransportFactoryFailure::DependencyInitialization, nullptr };
        try
        {
            auto runtime = std::make_unique<GameNetworkingSocketsRuntime>(limits, key);
            if (!runtime->valid())
                return { runtime->factoryFailure(), nullptr };
            return { TransportFactoryFailure::None, std::move(runtime) };
        }
        catch (...)
        {
            return { TransportFactoryFailure::DependencyInitialization, nullptr };
        }
    }
}
