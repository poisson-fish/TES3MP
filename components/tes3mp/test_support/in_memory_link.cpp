#include <tes3mp/test_support/in_memory_link.hpp>

namespace TES3MP::TestSupport
{
    std::optional<InMemoryDuplexLink> InMemoryDuplexLink::create(LinkBudget aToB, LinkBudget bToA)
    {
        if (aToB.maximumMessages == 0 || aToB.maximumBytes == 0 || bToA.maximumMessages == 0 || bToA.maximumBytes == 0)
            return std::nullopt;
        return InMemoryDuplexLink(aToB, bToA);
    }

    InMemoryDuplexLink::InMemoryDuplexLink(LinkBudget aToB, LinkBudget bToA)
        : mAToB{ aToB }
        , mBToA{ bToA }
    {
    }

    InMemoryDuplexLink::DirectionState& InMemoryDuplexLink::state(LinkDirection direction) noexcept
    {
        return direction == LinkDirection::AtoB ? mAToB : mBToA;
    }

    const InMemoryDuplexLink::DirectionState& InMemoryDuplexLink::state(LinkDirection direction) const noexcept
    {
        return direction == LinkDirection::AtoB ? mAToB : mBToA;
    }

    LinkSendResult InMemoryDuplexLink::send(LinkDirection direction, std::span<const std::byte> message)
    {
        DirectionState& target = state(direction);
        if (target.closed)
            return LinkSendResult::Closed;
        if (message.size() > target.budget.maximumBytes)
            return LinkSendResult::MessageTooLarge;
        if (target.messages.size() >= target.budget.maximumMessages
            || message.size() > target.budget.maximumBytes - target.bytes)
            return LinkSendResult::WouldBlock;

        target.messages.emplace_back(message.begin(), message.end());
        target.bytes += message.size();
        return LinkSendResult::Accepted;
    }

    std::optional<std::vector<std::byte>> InMemoryDuplexLink::receive(LinkDirection direction)
    {
        DirectionState& source = state(direction);
        if (source.messages.empty())
            return std::nullopt;

        std::vector<std::byte> message = std::move(source.messages.front());
        source.messages.pop_front();
        source.bytes -= message.size();
        return message;
    }

    void InMemoryDuplexLink::closeSend(LinkDirection direction) noexcept
    {
        state(direction).closed = true;
    }

    bool InMemoryDuplexLink::isSendClosed(LinkDirection direction) const noexcept
    {
        return state(direction).closed;
    }

    std::size_t InMemoryDuplexLink::queuedMessages(LinkDirection direction) const noexcept
    {
        return state(direction).messages.size();
    }

    std::size_t InMemoryDuplexLink::queuedBytes(LinkDirection direction) const noexcept
    {
        return state(direction).bytes;
    }
}
