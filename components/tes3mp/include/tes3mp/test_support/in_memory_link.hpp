#ifndef TES3MP_TEST_SUPPORT_IN_MEMORY_LINK_HPP
#define TES3MP_TEST_SUPPORT_IN_MEMORY_LINK_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace TES3MP::TestSupport
{
    enum class LinkDirection : std::uint8_t
    {
        AtoB,
        BtoA,
    };

    enum class LinkSendResult : std::uint8_t
    {
        Accepted,
        WouldBlock,
        MessageTooLarge,
        Closed,
    };

    struct LinkBudget
    {
        std::size_t maximumMessages;
        std::size_t maximumBytes;
    };

    class InMemoryDuplexLink
    {
    public:
        static std::optional<InMemoryDuplexLink> create(LinkBudget aToB, LinkBudget bToA);

        LinkSendResult send(LinkDirection direction, std::span<const std::byte> message);
        std::optional<std::vector<std::byte>> receive(LinkDirection direction);
        void closeSend(LinkDirection direction) noexcept;

        bool isSendClosed(LinkDirection direction) const noexcept;
        std::size_t queuedMessages(LinkDirection direction) const noexcept;
        std::size_t queuedBytes(LinkDirection direction) const noexcept;

    private:
        struct DirectionState
        {
            LinkBudget budget;
            std::deque<std::vector<std::byte>> messages;
            std::size_t bytes = 0;
            bool closed = false;
        };

        InMemoryDuplexLink(LinkBudget aToB, LinkBudget bToA);

        DirectionState& state(LinkDirection direction) noexcept;
        const DirectionState& state(LinkDirection direction) const noexcept;

        DirectionState mAToB;
        DirectionState mBToA;
    };
}

#endif
