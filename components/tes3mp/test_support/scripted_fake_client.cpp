#include <tes3mp/test_support/scripted_fake_client.hpp>

namespace TES3MP::TestSupport
{
    bool FakeClientScript::add(FakeClientStep step) noexcept
    {
        if (mSize == mSteps.size()) return false;
        mSteps[mSize++] = std::move(step);
        return true;
    }
    bool FakeClientScript::addConnect(ConnectionEndpoint endpoint) noexcept
    { return add({ FakeClientStepKind::Connect, std::move(endpoint) }); }
    bool FakeClientScript::addPump() noexcept { return add({ FakeClientStepKind::Pump, std::nullopt }); }
    bool FakeClientScript::addClose() noexcept { return add({ FakeClientStepKind::Close, std::nullopt }); }

    bool ScriptedFakeClient::execute(const FakeClientScript& script, ServerTick firstTick) noexcept
    {
        if (script.steps().size() > mTimeline.size() - mSize) return false;
        auto tick = firstTick;
        for (const auto& step : script.steps())
        {
            HeadlessClientResult result = HeadlessClientResult::Accepted;
            if (step.kind == FakeClientStepKind::Connect)
            {
                if (!step.endpoint) return false;
                result = mSession.connect(*step.endpoint);
            }
            else if (step.kind == FakeClientStepKind::Pump) result = mSession.pump().result;
            else result = mSession.close();
            const auto reason = result == HeadlessClientResult::Accepted ? FakeClientTraceReason::Accepted
                : (result == HeadlessClientResult::TransportFailed ? FakeClientTraceReason::TransportFailed
                                                                    : FakeClientTraceReason::Rejected);
            mTimeline[mSize] = { mSize + 1, tick, step.kind, reason };
            ++mSize;
            if (const auto next = tick.next()) tick = *next;
        }
        return true;
    }

    std::optional<std::string> ScriptedFakeClient::timelineNdjson() const
    {
        std::string output;
        output.reserve(mSize * 64);
        for (const auto& entry : timeline())
        {
            output += "{\"sequence\":" + std::to_string(entry.sequence) + ",\"tick\":"
                + std::to_string(entry.tick.value()) + ",\"step\":"
                + std::to_string(static_cast<unsigned>(entry.step)) + ",\"reason\":"
                + std::to_string(static_cast<unsigned>(entry.reason)) + "}\n";
            if (output.size() > 8192) return std::nullopt;
        }
        return output;
    }
}
