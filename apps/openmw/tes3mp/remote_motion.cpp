#include "remote_motion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        constexpr std::uint64_t NanosecondsPerSecond = 1'000'000'000;
        constexpr std::uint64_t ServerTicksPerSecond = 30;

        RemoteMotionPose poseFrom(const SpatialEntitySnapshot& sample, double x, double y, double z) noexcept
        {
            return { sample.transform().cell(), x, y, z, sample.transform().orientation() };
        }

        RemoteMotionPose exactPose(const SpatialEntitySnapshot& sample) noexcept
        {
            const auto position = sample.transform().position();
            return poseFrom(sample, static_cast<double>(position.x()), static_cast<double>(position.y()),
                static_cast<double>(position.z()));
        }

        std::uint64_t roundedDistance(double distance) noexcept
        {
            if (!std::isfinite(distance) || distance >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
                return std::numeric_limits<std::uint64_t>::max();
            return static_cast<std::uint64_t>(std::floor(distance + 0.5));
        }

        std::uint64_t saturatingAdd(std::uint64_t left, std::uint64_t right) noexcept
        {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
                return std::numeric_limits<std::uint64_t>::max();
            return left + right;
        }
    }

    RemoteMotionBuffer::RemoteMotionBuffer(RemoteMotionMetricSink& metrics) noexcept
        : mMetrics(metrics)
    {
    }

    void RemoteMotionBuffer::record(RemoteMotionMetricKey key, std::uint64_t value) noexcept
    {
        (void)mMetrics.tryRecord({ key, value });
    }

    void RemoteMotionBuffer::resetTo(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept
    {
        for (auto& value : mSamples)
            value.reset();
        mSamples[0].emplace(Sample{ sample, receivedAt });
        mSampleCount = 1;
        mStarted = false;
        mCursorTick = sample.serverTick().value();
        mCursorFraction = 0;
        mLastAdvance = receivedAt;
        mLastSnapshot = receivedAt;
        mCorrection = {};
        mCorrectionEnds.reset();
        record(RemoteMotionMetricKey::BufferDepth, 1);
        record(RemoteMotionMetricKey::HardSnaps, 1);
    }

    void RemoteMotionBuffer::advanceCursor(MonotonicInstant now) noexcept
    {
        if (!mLastAdvance)
        {
            mLastAdvance = now;
            return;
        }
        if (now < *mLastAdvance)
        {
            mLastAdvance = now;
            return;
        }
        const std::uint64_t elapsed = now.nanoseconds() - mLastAdvance->nanoseconds();
        mLastAdvance = now;
        if (!mStarted || mSampleCount == 0)
            return;

        const std::uint64_t seconds = elapsed / NanosecondsPerSecond;
        const std::uint64_t remainder = elapsed % NanosecondsPerSecond;
        if (seconds > (std::numeric_limits<std::uint64_t>::max() - mCursorTick) / ServerTicksPerSecond)
        {
            mCursorTick = std::numeric_limits<std::uint64_t>::max();
            mCursorFraction = 0;
        }
        else
        {
            mCursorTick += seconds * ServerTicksPerSecond;
            const std::uint64_t scaled = remainder * ServerTicksPerSecond + mCursorFraction;
            const std::uint64_t whole = scaled / NanosecondsPerSecond;
            mCursorFraction = scaled % NanosecondsPerSecond;
            if (whole > std::numeric_limits<std::uint64_t>::max() - mCursorTick)
            {
                mCursorTick = std::numeric_limits<std::uint64_t>::max();
                mCursorFraction = 0;
            }
            else
                mCursorTick += whole;
        }

        const std::uint64_t newest = mSamples[mSampleCount - 1]->snapshot.serverTick().value();
        const std::uint64_t maximum = saturatingAdd(newest, MaximumRemoteExtrapolationTicks);
        if (mCursorTick > maximum || (mCursorTick == maximum && mCursorFraction != 0))
        {
            mCursorTick = maximum;
            mCursorFraction = 0;
        }
    }

    std::optional<RemoteMotionBuffer::ResolvedPose> RemoteMotionBuffer::resolve() const noexcept
    {
        if (mSampleCount == 0)
            return std::nullopt;
        if (!mStarted)
            return ResolvedPose{ exactPose(mSamples[0]->snapshot), 0 };

        const auto& first = mSamples[0]->snapshot;
        if (mCursorTick < first.serverTick().value())
            return ResolvedPose{ exactPose(first), 0 };

        std::size_t lowerIndex = 0;
        for (std::size_t index = 1; index < mSampleCount; ++index)
        {
            if (mSamples[index]->snapshot.serverTick().value() > mCursorTick
                || (mSamples[index]->snapshot.serverTick().value() == mCursorTick && mCursorFraction == 0))
                break;
            lowerIndex = index;
        }

        const auto& lower = mSamples[lowerIndex]->snapshot;
        if (lowerIndex + 1 < mSampleCount)
        {
            const auto& upper = mSamples[lowerIndex + 1]->snapshot;
            const std::uint64_t tickSpan = upper.serverTick().value() - lower.serverTick().value();
            if (tickSpan != 0)
            {
                const long double cursorDistance = static_cast<long double>(mCursorTick - lower.serverTick().value())
                    + static_cast<long double>(mCursorFraction) / NanosecondsPerSecond;
                const double ratio = static_cast<double>(cursorDistance / tickSpan);
                const auto from = lower.transform().position();
                const auto to = upper.transform().position();
                const auto interpolate = [ratio](std::int64_t a, std::int64_t b) {
                    return static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * ratio;
                };
                return ResolvedPose{ poseFrom(lower, interpolate(from.x(), to.x()), interpolate(from.y(), to.y()),
                                         interpolate(from.z(), to.z())),
                    0 };
            }
        }

        const auto& newest = mSamples[mSampleCount - 1]->snapshot;
        const long double ahead = static_cast<long double>(mCursorTick - newest.serverTick().value())
            + static_cast<long double>(mCursorFraction) / NanosecondsPerSecond;
        const auto position = newest.transform().position();
        const auto velocity = newest.linearVelocity();
        const double ticks = static_cast<double>(std::max<long double>(0, ahead));
        const std::uint64_t aheadNumerator
            = (mCursorTick - newest.serverTick().value()) * NanosecondsPerSecond + mCursorFraction;
        return ResolvedPose{ poseFrom(newest,
                                 static_cast<double>(position.x()) + static_cast<double>(velocity.x()) * ticks,
                                 static_cast<double>(position.y()) + static_cast<double>(velocity.y()) * ticks,
                                 static_cast<double>(position.z()) + static_cast<double>(velocity.z()) * ticks),
            aheadNumerator / ServerTicksPerSecond };
    }

    RemoteMotionPose RemoteMotionBuffer::applyCorrection(RemoteMotionPose pose, MonotonicInstant now) noexcept
    {
        if (!mCorrectionEnds)
            return pose;
        if (now >= *mCorrectionEnds)
        {
            mCorrection = {};
            mCorrectionEnds.reset();
            return pose;
        }
        const double remaining = std::clamp(static_cast<double>(mCorrectionEnds->nanoseconds() - now.nanoseconds())
                / static_cast<double>(RemoteCorrectionBlendNanoseconds),
            0.0, 1.0);
        pose.x += mCorrection[0] * remaining;
        pose.y += mCorrection[1] * remaining;
        pose.z += mCorrection[2] * remaining;
        return pose;
    }

    bool RemoteMotionBuffer::observe(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept
    {
        if (mSampleCount == 0)
        {
            resetTo(sample, receivedAt);
            return true;
        }

        const auto& newest = mSamples[mSampleCount - 1]->snapshot;
        if (sample.playerId() != newest.playerId() || sample.entityId() != newest.entityId())
            return false;
        if (sample.entityRevision() < newest.entityRevision())
            return false;
        if (sample.entityRevision() == newest.entityRevision())
        {
            if (sample != newest)
                return false;
            mLastSnapshot = receivedAt;
            return true;
        }
        if (sample.transform().cell() != newest.transform().cell() || sample.authorityEpoch() != newest.authorityEpoch()
            || sample.serverTick() < newest.serverTick())
        {
            resetTo(sample, receivedAt);
            return true;
        }

        advanceCursor(receivedAt);
        const auto oldResolved = resolve();
        const auto oldPose = oldResolved
            ? std::optional<RemoteMotionPose>(applyCorrection(oldResolved->pose, receivedAt))
            : std::nullopt;

        if (sample.serverTick() == newest.serverTick())
            mSamples[mSampleCount - 1].emplace(Sample{ sample, receivedAt });
        else if (mSampleCount < MaximumRemoteMotionSamples)
            mSamples[mSampleCount++].emplace(Sample{ sample, receivedAt });
        else
        {
            for (std::size_t index = 1; index < MaximumRemoteMotionSamples; ++index)
                mSamples[index - 1] = std::move(mSamples[index]);
            mSamples[MaximumRemoteMotionSamples - 1].emplace(Sample{ sample, receivedAt });
        }
        mLastSnapshot = receivedAt;

        const std::uint64_t oldestTick = mSamples[0]->snapshot.serverTick().value();
        const std::uint64_t newestTick = mSamples[mSampleCount - 1]->snapshot.serverTick().value();
        if (!mStarted && newestTick - oldestTick >= RemotePlaybackDelayTicks)
        {
            mStarted = true;
            mCursorTick = newestTick - RemotePlaybackDelayTicks;
            if (mCursorTick < oldestTick)
                mCursorTick = oldestTick;
            mCursorFraction = 0;
            mLastAdvance = receivedAt;
        }
        else if (mStarted && mCursorTick < oldestTick)
        {
            mCursorTick = oldestTick;
            mCursorFraction = 0;
        }

        const auto newResolved = resolve();
        if (oldResolved && oldPose && newResolved)
        {
            const double x = newResolved->pose.x - oldResolved->pose.x;
            const double y = newResolved->pose.y - oldResolved->pose.y;
            const double z = newResolved->pose.z - oldResolved->pose.z;
            const double rawDistance = std::hypot(x, y, z);
            const std::uint64_t distance = roundedDistance(rawDistance);
            if (distance != 0)
            {
                record(RemoteMotionMetricKey::CorrectionDistanceQuanta, distance);
                if (!std::isfinite(rawDistance) || rawDistance > RemoteHardSnapDistanceQuanta)
                {
                    mCorrection = {};
                    mCorrectionEnds.reset();
                    record(RemoteMotionMetricKey::HardSnaps, 1);
                }
                else
                {
                    mCorrection = { oldPose->x - newResolved->pose.x, oldPose->y - newResolved->pose.y,
                        oldPose->z - newResolved->pose.z };
                    mCorrectionEnds = MonotonicInstant::fromNanoseconds(
                        saturatingAdd(receivedAt.nanoseconds(), RemoteCorrectionBlendNanoseconds));
                }
            }
        }
        record(RemoteMotionMetricKey::BufferDepth, mSampleCount);
        return true;
    }

    std::optional<RemoteMotionPose> RemoteMotionBuffer::advance(MonotonicInstant now) noexcept
    {
        advanceCursor(now);
        const auto resolved = resolve();
        if (!resolved)
            return std::nullopt;
        const std::uint64_t age
            = mLastSnapshot && now >= *mLastSnapshot ? now.nanoseconds() - mLastSnapshot->nanoseconds() : 0;
        record(RemoteMotionMetricKey::SnapshotAgeNanoseconds, age);
        record(RemoteMotionMetricKey::BufferDepth, mSampleCount);
        if (resolved->extrapolationNanoseconds != 0)
            record(RemoteMotionMetricKey::ExtrapolationNanoseconds, resolved->extrapolationNanoseconds);
        return applyCorrection(resolved->pose, now);
    }

    void RemoteMotionBuffer::clear() noexcept
    {
        for (auto& value : mSamples)
            value.reset();
        mSampleCount = 0;
        mStarted = false;
        mCursorTick = 0;
        mCursorFraction = 0;
        mLastAdvance.reset();
        mLastSnapshot.reset();
        mCorrection = {};
        mCorrectionEnds.reset();
        record(RemoteMotionMetricKey::BufferDepth, 0);
    }
}
