#ifndef TES3MP_TEST_SUPPORT_SCRIPTED_FAKE_CLIENT_HPP
#define TES3MP_TEST_SUPPORT_SCRIPTED_FAKE_CLIENT_HPP

#include <tes3mp/headless_client_session.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>

namespace TES3MP::TestSupport
{
    enum class FakeClientStepKind : std::uint8_t { Connect, Pump, Close };
    enum class FakeClientTraceReason : std::uint8_t { Accepted, Rejected, TransportFailed, CapacityExceeded };

    struct FakeClientStep { FakeClientStepKind kind; std::optional<ConnectionEndpoint> endpoint; };
    struct FakeClientTimelineEntry
    {
        std::uint64_t sequence = 0;
        ServerTick tick = ServerTick::initial();
        FakeClientStepKind step = FakeClientStepKind::Pump;
        FakeClientTraceReason reason = FakeClientTraceReason::Accepted;
        friend bool operator==(const FakeClientTimelineEntry&, const FakeClientTimelineEntry&) = default;
    };

    class FakeClientScript
    {
    public:
        static constexpr std::size_t MaximumSteps = 64;
        bool addConnect(ConnectionEndpoint endpoint) noexcept;
        bool addPump() noexcept;
        bool addClose() noexcept;
        std::span<const FakeClientStep> steps() const noexcept { return { mSteps.data(), mSize }; }
    private:
        bool add(FakeClientStep step) noexcept;
        std::array<FakeClientStep, MaximumSteps> mSteps{};
        std::size_t mSize = 0;
    };

    class ScriptedFakeClient
    {
    public:
        static constexpr std::size_t MaximumTimelineEntries = 128;
        explicit ScriptedFakeClient(HeadlessClientSession& session) noexcept : mSession(session) {}
        bool execute(const FakeClientScript& script, ServerTick firstTick) noexcept;
        std::span<const FakeClientTimelineEntry> timeline() const noexcept
        { return { mTimeline.data(), mSize }; }
        std::optional<std::string> timelineNdjson() const;
    private:
        HeadlessClientSession& mSession;
        std::array<FakeClientTimelineEntry, MaximumTimelineEntries> mTimeline{};
        std::size_t mSize = 0;
    };
}

#endif
