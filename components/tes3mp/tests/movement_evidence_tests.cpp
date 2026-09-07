#include "movement_mapping.hpp"
#include "remote_motion.hpp"

#include <tes3mp/fixed_tick_scheduler.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::OpenMWAdapter;
    using namespace TES3MP::TestSupport;

    constexpr std::uint64_t Frame = 16'666'667;
    constexpr std::uint64_t Tick = 33'333'334;

    struct Profile
    {
        std::string_view name;
        std::uint64_t commandDelay;
        std::uint64_t localError;
        bool jitter;
        bool loss;
        bool stall;
    };

    struct Evidence
    {
        std::array<MovementMetricSummary, static_cast<std::size_t>(MovementMetricKey::Count)> metrics{};
        std::uint64_t tickLagMaximum = 0;
    };

    template <class T>
    T value(std::uint64_t raw)
    {
        return T::fromValue(raw).value();
    }

    SpatialEntitySnapshot routeSample(std::uint64_t tick)
    {
        std::int64_t x = 0;
        std::int64_t y = 0;
        LinearVelocity3 velocity(0, 0, 0);
        if (tick < 5)
        {
            x = static_cast<std::int64_t>(tick * DesktopFixtureSpeedQuantaPerTick);
            velocity = LinearVelocity3(DesktopFixtureSpeedQuantaPerTick, 0, 0);
        }
        else if (tick < 10)
        {
            x = 5 * DesktopFixtureSpeedQuantaPerTick;
            y = static_cast<std::int64_t>((tick - 5) * DesktopFixtureSpeedQuantaPerTick);
            velocity = LinearVelocity3(0, DesktopFixtureSpeedQuantaPerTick, 0);
        }
        else
        {
            x = 5 * DesktopFixtureSpeedQuantaPerTick;
            y = 5 * DesktopFixtureSpeedQuantaPerTick;
        }
        const auto zero = Turn32::fromValue(0);
        return SpatialEntitySnapshot(value<ServerTick>(tick + 1), value<PlayerId>(1), value<EntityId>(2),
            value<AppearanceId>(1), value<EntityRevision>(tick + 1), AuthorityEpoch::initial(),
            Transform(CellId::interior(value<CellSpaceId>(7)), Position3(x, y, 0), Orientation3(zero, zero, zero)),
            velocity);
    }

    std::uint64_t arrivalIncrement(const Profile& profile, std::uint64_t tick)
    {
        if (profile.stall && tick == 7)
            return 250'000'000;
        if (profile.jitter)
            return tick % 2 == 0 ? 18'000'000 : 52'000'000;
        return Tick;
    }

    bool dropped(const Profile& profile, std::uint64_t tick)
    {
        return profile.loss && (tick == 4 || tick == 8);
    }

    void captureCommands(const Profile& profile, BoundedMovementMetricSink& sink)
    {
        MotionIntentTracker tracker(&sink);
        const std::array intents{
            mapPlanarMovement(1, 0, 0),
            mapPlanarMovement(0, 1, 0),
            mapPlanarMovement(0, 0, 0),
        };
        LinearVelocity3 authoritative(0, 0, 0);
        std::uint64_t now = 0;
        for (std::size_t index = 0; index < intents.size(); ++index)
        {
            tracker.sample(intents[index], MonotonicInstant::fromNanoseconds(now));
            const auto next = tracker.next(authoritative);
            const auto sequence = value<CommandSequence>(index + 1);
            if (!next || !tracker.markQueued(sequence, *next, MonotonicInstant::fromNanoseconds(now)))
                std::abort();
            now += profile.commandDelay;
            tracker.observeAcknowledgement(sequence, MonotonicInstant::fromNanoseconds(now));
            authoritative = intents[index].desiredVelocity();
            now += Tick;
        }
    }

    void capturePresentation(const Profile& profile, bool vr, BoundedMovementMetricSink& sink)
    {
        RemoteMotionBuffer remote(sink);
        PoseEvidenceTracker pose(vr ? &sink : nullptr);
        std::uint64_t now = 0;
        std::uint64_t nextFrame = 0;
        for (std::uint64_t tick = 0; tick < 15; ++tick)
        {
            if (tick != 0)
                now += arrivalIncrement(profile, tick);
            while (nextFrame < now)
            {
                (void)remote.advance(MonotonicInstant::fromNanoseconds(nextFrame));
                pose.advance(MonotonicInstant::fromNanoseconds(nextFrame));
                nextFrame += Frame;
            }
            if (dropped(profile, tick))
                continue;
            const auto sample = routeSample(tick);
            if (!remote.observe(sample, MonotonicInstant::fromNanoseconds(now)))
                std::abort();
            const double error = static_cast<double>(profile.localError);
            (void)sink.tryRecord({ MovementMetricKey::LocalCorrectionDistanceQuanta,
                movementCorrectionDistanceQuanta(sample.transform().position(),
                    static_cast<double>(sample.transform().position().x()) + error,
                    static_cast<double>(sample.transform().position().y()), 0) });
            if (vr)
                pose.observe(sample.entityId(), sample.authorityEpoch(), value<PoseSampleSequence>(tick + 1),
                    MonotonicInstant::fromNanoseconds(now));
        }
        for (unsigned frame = 0; frame < 8; ++frame)
        {
            (void)remote.advance(MonotonicInstant::fromNanoseconds(nextFrame));
            pose.advance(MonotonicInstant::fromNanoseconds(nextFrame));
            nextFrame += Frame;
        }
    }

    std::uint64_t captureTickLag(const Profile& profile)
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        FixedTickScheduler scheduler(clock, clock.now(), ServerTick::initial());
        std::uint64_t maximum = 0;
        for (std::uint64_t tick = 1; tick < 15; ++tick)
        {
            clock.advance(arrivalIncrement(profile, tick));
            const auto pumped = scheduler.pump();
            if (!pumped)
                std::abort();
            maximum = std::max(maximum, pumped.dueTickLag());
        }
        return maximum;
    }

    Evidence capture(const Profile& profile, bool vr)
    {
        BoundedMovementMetricSink sink;
        captureCommands(profile, sink);
        capturePresentation(profile, vr, sink);
        Evidence result;
        for (std::size_t index = 0; index < result.metrics.size(); ++index)
            result.metrics[index] = sink.summary(static_cast<MovementMetricKey>(index));
        result.tickLagMaximum = captureTickLag(profile);
        if (sink.droppedCount() != 0)
            std::abort();
        return result;
    }

    std::uint64_t maximum(const Evidence& evidence, MovementMetricKey key)
    {
        return evidence.metrics[static_cast<std::size_t>(key)].maximum;
    }

    std::uint64_t total(const Evidence& evidence, MovementMetricKey key)
    {
        return evidence.metrics[static_cast<std::size_t>(key)].total;
    }

    void print(std::string_view platform, const Profile& profile, const Evidence& evidence)
    {
        std::cout << "{\"platform\":\"" << platform << "\",\"profile\":\"" << profile.name
                  << "\",\"command_ack_max_ns\":"
                  << maximum(evidence, MovementMetricKey::CommandAcknowledgementNanoseconds)
                  << ",\"stop_ack_max_ns\":" << maximum(evidence, MovementMetricKey::StopAcknowledgementNanoseconds)
                  << ",\"local_correction_max_quanta\":"
                  << maximum(evidence, MovementMetricKey::LocalCorrectionDistanceQuanta)
                  << ",\"remote_snapshot_age_max_ns\":" << maximum(evidence, MovementMetricKey::SnapshotAgeNanoseconds)
                  << ",\"remote_buffer_depth_max\":" << maximum(evidence, MovementMetricKey::BufferDepth)
                  << ",\"remote_extrapolation_max_ns\":"
                  << maximum(evidence, MovementMetricKey::ExtrapolationNanoseconds)
                  << ",\"remote_correction_max_quanta\":"
                  << maximum(evidence, MovementMetricKey::CorrectionDistanceQuanta)
                  << ",\"remote_hard_snaps\":" << total(evidence, MovementMetricKey::HardSnaps)
                  << ",\"server_tick_lag_max\":" << evidence.tickLagMaximum
                  << ",\"pose_age_max_ns\":" << maximum(evidence, MovementMetricKey::PoseAgeNanoseconds)
                  << ",\"pose_lost_samples\":" << total(evidence, MovementMetricKey::PoseLostSamples) << "}\n";
    }
}

int main()
{
    constexpr std::array profiles{
        Profile{ "direct", 20'000'000, 128, false, false, false },
        Profile{ "jitter", 85'000'000, 512, true, false, false },
        Profile{ "loss", 180'000'000, 1024, false, true, false },
        Profile{ "stall", 350'000'000, 4096, false, false, true },
    };
    for (const auto& profile : profiles)
    {
        const auto desktop = capture(profile, false);
        const auto vr = capture(profile, true);
        for (std::size_t index = 0; index < desktop.metrics.size(); ++index)
        {
            const auto key = static_cast<MovementMetricKey>(index);
            if (key != MovementMetricKey::PoseAgeNanoseconds && key != MovementMetricKey::PoseLostSamples
                && desktop.metrics[index] != vr.metrics[index])
                return 1;
        }
        if (desktop.tickLagMaximum != vr.tickLagMaximum || maximum(vr, MovementMetricKey::PoseAgeNanoseconds) == 0
            || (profile.loss && total(vr, MovementMetricKey::PoseLostSamples) == 0))
            return 1;
        print("desktop", profile, desktop);
        print("pc_vr", profile, vr);
    }
    return 0;
}
