#include "tes3mp/transport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <utility>

namespace
{
    bool parseDecimalByte(std::string_view value)
    {
        if (value.empty() || value.size() > 3)
            return false;
        unsigned int parsed = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        return error == std::errc{} && end == value.data() + value.size() && parsed <= 255;
    }

    bool isIpv4(std::string_view host)
    {
        std::size_t begin = 0;
        std::size_t parts = 0;
        while (begin <= host.size())
        {
            const std::size_t end = host.find('.', begin);
            const std::string_view part
                = host.substr(begin, end == std::string_view::npos ? host.size() - begin : end - begin);
            if (!parseDecimalByte(part))
                return false;
            ++parts;
            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        return parts == 4;
    }

    bool isHexGroup(std::string_view value)
    {
        return !value.empty() && value.size() <= 4
            && std::all_of(value.begin(), value.end(), [](unsigned char byte) { return std::isxdigit(byte) != 0; });
    }

    bool isIpv6(std::string_view host)
    {
        if (host.empty() || host.find(':') == std::string_view::npos)
            return false;
        if (host.front() == '[' || host.back() == ']')
            return false;

        const std::size_t compression = host.find("::");
        if (compression != std::string_view::npos && host.find("::", compression + 2) != std::string_view::npos)
            return false;

        std::size_t groups = 0;
        bool embeddedIpv4 = false;
        std::size_t begin = 0;
        while (begin <= host.size())
        {
            const std::size_t end = host.find(':', begin);
            const std::string_view group
                = host.substr(begin, end == std::string_view::npos ? host.size() - begin : end - begin);
            if (!group.empty())
            {
                if (group.find('.') != std::string_view::npos)
                {
                    if (end != std::string_view::npos || !isIpv4(group))
                        return false;
                    embeddedIpv4 = true;
                    groups += 2;
                }
                else if (!isHexGroup(group))
                    return false;
                else
                    ++groups;
            }
            else if (compression == std::string_view::npos)
                return false;

            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        if (groups > 8 || embeddedIpv4 && groups > 8)
            return false;
        return compression == std::string_view::npos ? groups == 8 : groups < 8;
    }

    bool isDnsLabel(std::string_view label)
    {
        if (label.empty() || label.size() > TES3MP::ConnectionEndpoint::MaxLabelBytes)
            return false;
        const auto isAlphaNumeric = [](unsigned char byte) { return std::isalnum(byte) != 0; };
        if (!isAlphaNumeric(static_cast<unsigned char>(label.front()))
            || !isAlphaNumeric(static_cast<unsigned char>(label.back())))
            return false;
        return std::all_of(
            label.begin(), label.end(), [&](unsigned char byte) { return isAlphaNumeric(byte) || byte == '-'; });
    }

    std::optional<std::string> normalizeDns(std::string_view host)
    {
        if (!host.empty() && host.back() == '.')
            host.remove_suffix(1);
        if (host.empty())
            return std::nullopt;

        std::string normalized;
        normalized.reserve(host.size());
        std::size_t begin = 0;
        while (begin <= host.size())
        {
            const std::size_t end = host.find('.', begin);
            const std::string_view label
                = host.substr(begin, end == std::string_view::npos ? host.size() - begin : end - begin);
            if (!isDnsLabel(label))
                return std::nullopt;
            for (unsigned char byte : label)
                normalized.push_back(static_cast<char>(std::tolower(byte)));
            if (end == std::string_view::npos)
                break;
            normalized.push_back('.');
            begin = end + 1;
        }
        return normalized;
    }

    std::optional<std::pair<std::string, TES3MP::EndpointHostKind>> parseHost(std::string_view host)
    {
        if (host.empty() || host.size() > TES3MP::ConnectionEndpoint::MaxHostBytes)
            return std::nullopt;
        if (std::any_of(
                host.begin(), host.end(), [](unsigned char byte) { return byte >= 0x80 || std::isspace(byte); }))
            return std::nullopt;
        if (isIpv4(host))
            return std::pair{ std::string(host), TES3MP::EndpointHostKind::Ipv4 };
        if (isIpv6(host))
        {
            std::string normalized(host);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
            return std::pair{ std::move(normalized), TES3MP::EndpointHostKind::Ipv6 };
        }
        auto normalized = normalizeDns(host);
        if (!normalized)
            return std::nullopt;
        return std::pair{ std::move(*normalized), TES3MP::EndpointHostKind::DnsName };
    }
}

namespace TES3MP
{
    std::optional<TransportChannel> transportChannelFor(MessageClass messageClass) noexcept
    {
        switch (messageClass)
        {
            case MessageClass::SessionControl:
            case MessageClass::ReliableOperation:
                return TransportChannel::ReliableOrdered;
            case MessageClass::LatestWinsSnapshot:
                return TransportChannel::LatestWins;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> maximumTransportMessageBytes(TransportChannel channel) noexcept
    {
        switch (channel)
        {
            case TransportChannel::ReliableOrdered:
                return ReliableOrderedMaximumMessageBytes;
            case TransportChannel::LatestWins:
                return LatestWinsMaximumMessageBytes;
        }
        return std::nullopt;
    }

    bool isMessageClassAllowedOnTransportChannel(MessageClass messageClass, TransportChannel channel) noexcept
    {
        const auto expected = transportChannelFor(messageClass);
        return expected && *expected == channel;
    }

    std::optional<ConnectionEndpoint> ConnectionEndpoint::create(std::string_view host, std::uint16_t port)
    {
        if (port == 0)
            return std::nullopt;
        auto parsed = parseHost(host);
        if (!parsed)
            return std::nullopt;
        return ConnectionEndpoint(std::move(parsed->first), port, parsed->second);
    }

    std::optional<ListenerEndpoint> ListenerEndpoint::create(std::string_view numericAddress, std::uint16_t port)
    {
        auto parsed = parseHost(numericAddress);
        if (!parsed || parsed->second == EndpointHostKind::DnsName)
            return std::nullopt;
        return ListenerEndpoint(std::move(parsed->first), port, parsed->second);
    }

    std::optional<TransportLimits> TransportLimits::create(std::size_t listenersValue, std::size_t pendingAttemptsValue,
        std::size_t connectionsValue, std::size_t retainedEventsValue) noexcept
    {
        if (listenersValue == 0 || listenersValue > MaxListeners || pendingAttemptsValue == 0
            || pendingAttemptsValue > MaxPendingAttempts || connectionsValue == 0 || connectionsValue > MaxConnections
            || retainedEventsValue == 0 || retainedEventsValue > MaxRetainedEvents)
            return std::nullopt;
        return TransportLimits{ listenersValue, pendingAttemptsValue, connectionsValue, retainedEventsValue };
    }

    std::optional<OutboundQueuePolicy> OutboundQueuePolicy::create(std::size_t reliableMessages,
        std::size_t reliableBytes, std::size_t sendAttemptsPerPump, std::size_t reliableBurstBeforeLatest,
        std::size_t reliableRateBurst, std::uint64_t reliableRefillMilliseconds, std::size_t latestRateBurst,
        std::uint64_t latestRefillMilliseconds, std::size_t consecutiveBlockLimit,
        std::uint64_t blockedDeadlineMilliseconds) noexcept
    {
        if (reliableMessages == 0 || reliableMessages > MaxReliableMessages || reliableBytes == 0
            || reliableBytes > MaxReliableBytes || sendAttemptsPerPump == 0
            || sendAttemptsPerPump > MaxSendAttemptsPerPump || reliableBurstBeforeLatest == 0
            || reliableBurstBeforeLatest > sendAttemptsPerPump || reliableRateBurst == 0
            || reliableRateBurst > MaxSendAttemptsPerPump || reliableRefillMilliseconds == 0 || latestRateBurst == 0
            || latestRateBurst > MaxSendAttemptsPerPump || latestRefillMilliseconds == 0 || consecutiveBlockLimit == 0
            || blockedDeadlineMilliseconds == 0)
            return std::nullopt;
        return OutboundQueuePolicy{ reliableMessages, reliableBytes, sendAttemptsPerPump, reliableBurstBeforeLatest,
            reliableRateBurst, reliableRefillMilliseconds, latestRateBurst, latestRefillMilliseconds,
            consecutiveBlockLimit, blockedDeadlineMilliseconds };
    }

    OutboundTransportQueue::OutboundTransportQueue(OutboundQueuePolicy policy)
        : mPolicy(policy)
    {
        mReliableRate.tokens = policy.reliableRateBurst;
        mLatestRate.tokens = policy.latestRateBurst;
    }

    TransportResult OutboundTransportQueue::enqueue(TransportChannel channel, std::span<const std::byte> message)
    {
        const auto maximum = maximumTransportMessageBytes(channel);
        if (!maximum || message.empty())
            return TransportResult::InvalidInput;
        if (message.size() > *maximum)
            return TransportResult::MessageTooLarge;
        if (channel == TransportChannel::LatestWins)
        {
            mLatest.emplace(message.begin(), message.end());
            return TransportResult::Accepted;
        }
        if (mReliable.size() >= mPolicy.reliableMessages || message.size() > mPolicy.reliableBytes - mReliableBytes)
            return TransportResult::WouldBlock;
        mReliable.emplace_back(message.begin(), message.end());
        mReliableBytes += message.size();
        return TransportResult::Accepted;
    }

    void OutboundTransportQueue::refill(
        RateBucket& bucket, std::size_t burst, std::uint64_t interval, std::uint64_t now) noexcept
    {
        if (!bucket.initialized)
        {
            bucket.lastRefill = now;
            bucket.initialized = true;
            return;
        }
        const std::uint64_t elapsed = now - bucket.lastRefill;
        const std::uint64_t gained = elapsed / interval;
        if (gained == 0)
            return;
        const std::size_t missing = burst - bucket.tokens;
        bucket.tokens = gained >= missing ? burst : bucket.tokens + static_cast<std::size_t>(gained);
        bucket.lastRefill += gained * interval;
    }

    bool OutboundTransportQueue::shouldEvict(std::uint64_t now) const noexcept
    {
        return mFirstReliableBlock
            && (mConsecutiveReliableBlocks >= mPolicy.consecutiveBlockLimit
                || now - *mFirstReliableBlock >= mPolicy.blockedDeadlineMilliseconds);
    }

    OutboundPumpResult OutboundTransportQueue::pump(
        TransportRuntime& runtime, TransportConnectionId connection, std::uint64_t nowMilliseconds)
    {
        if (mLastPumpTime && nowMilliseconds < *mLastPumpTime)
            return OutboundPumpResult::InvalidTime;
        mLastPumpTime = nowMilliseconds;
        refill(mReliableRate, mPolicy.reliableRateBurst, mPolicy.reliableRefillMilliseconds, nowMilliseconds);
        refill(mLatestRate, mPolicy.latestRateBurst, mPolicy.latestRefillMilliseconds, nowMilliseconds);

        std::size_t attempts = 0;
        std::size_t reliableAttempts = 0;
        bool progressed = false;
        bool reliableBlocked = false;
        while (!mReliable.empty() && attempts < mPolicy.sendAttemptsPerPump
            && reliableAttempts < mPolicy.reliableBurstBeforeLatest && mReliableRate.tokens != 0)
        {
            const TransportResult result
                = runtime.send(connection, TransportChannel::ReliableOrdered, mReliable.front());
            ++attempts;
            ++reliableAttempts;
            --mReliableRate.tokens;
            if (result == TransportResult::Accepted)
            {
                mReliableBytes -= mReliable.front().size();
                mReliable.pop_front();
                progressed = true;
                continue;
            }
            if (result == TransportResult::WouldBlock)
            {
                reliableBlocked = true;
                break;
            }
            return OutboundPumpResult::TransportFailed;
        }

        if (mLatest && attempts < mPolicy.sendAttemptsPerPump && mLatestRate.tokens != 0)
        {
            const TransportResult result = runtime.send(connection, TransportChannel::LatestWins, *mLatest);
            ++attempts;
            --mLatestRate.tokens;
            if (result == TransportResult::Accepted)
            {
                mLatest.reset();
                progressed = true;
            }
            else if (result != TransportResult::WouldBlock)
                return OutboundPumpResult::TransportFailed;
        }

        if (reliableBlocked)
        {
            if (!mFirstReliableBlock)
                mFirstReliableBlock = nowMilliseconds;
            ++mConsecutiveReliableBlocks;
            if (shouldEvict(nowMilliseconds))
            {
                runtime.close(connection, TransportCloseMode::Abort);
                clear();
                return OutboundPumpResult::SlowPeerEvicted;
            }
            return progressed ? OutboundPumpResult::Progress : OutboundPumpResult::Blocked;
        }
        if (progressed || mReliable.empty())
        {
            mFirstReliableBlock.reset();
            mConsecutiveReliableBlocks = 0;
        }
        if (progressed)
            return OutboundPumpResult::Progress;
        return mReliable.empty() && !mLatest ? OutboundPumpResult::Idle : OutboundPumpResult::Blocked;
    }

    void OutboundTransportQueue::clear() noexcept
    {
        mReliable.clear();
        mLatest.reset();
        mReliableBytes = 0;
        mFirstReliableBlock.reset();
        mConsecutiveReliableBlocks = 0;
    }

    std::optional<OutboundQueueSet> OutboundQueueSet::create(OutboundQueuePolicy policy, std::size_t connections)
    {
        if (connections == 0 || connections > OutboundQueuePolicy::MaxConnections)
            return std::nullopt;
        return OutboundQueueSet(policy, connections);
    }

    TransportResult OutboundQueueSet::attach(TransportConnectionId connection)
    {
        if (mQueues.contains(connection))
            return TransportResult::AlreadyFinalized;
        if (mQueues.size() >= mConnectionLimit)
            return TransportResult::AtCapacity;
        mQueues.emplace(connection, mPolicy);
        return TransportResult::Accepted;
    }

    TransportResult OutboundQueueSet::detach(TransportConnectionId connection) noexcept
    {
        return mQueues.erase(connection) == 1 ? TransportResult::Accepted : TransportResult::UnknownId;
    }

    TransportResult OutboundQueueSet::enqueue(
        TransportConnectionId connection, TransportChannel channel, std::span<const std::byte> message)
    {
        const auto found = mQueues.find(connection);
        return found == mQueues.end() ? TransportResult::UnknownId : found->second.enqueue(channel, message);
    }

    std::optional<OutboundPumpResult> OutboundQueueSet::pump(
        TransportRuntime& runtime, TransportConnectionId connection, std::uint64_t nowMilliseconds)
    {
        const auto found = mQueues.find(connection);
        if (found == mQueues.end())
            return std::nullopt;
        const OutboundPumpResult result = found->second.pump(runtime, connection, nowMilliseconds);
        if (result == OutboundPumpResult::SlowPeerEvicted)
            mQueues.erase(found);
        return result;
    }
}
