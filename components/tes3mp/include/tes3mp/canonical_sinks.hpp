#ifndef TES3MP_CANONICAL_SINKS_HPP
#define TES3MP_CANONICAL_SINKS_HPP

#include "canonical_publication.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace TES3MP
{
    inline constexpr std::size_t MaximumCanonicalSinkAttempts = 4;

    enum class CanonicalSinkRole : std::uint8_t
    {
        Persistence,
        Replay,
        Script,
        Metrics,
    };

    enum class CanonicalSinkDeliveryResult : std::uint8_t
    {
        NotConfigured,
        Accepted,
        Backpressured,
        Failed,
    };

    class CanonicalPersistenceSink
    {
    public:
        virtual ~CanonicalPersistenceSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
            = 0;
    };

    class CanonicalReplaySink
    {
    public:
        virtual ~CanonicalReplaySink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
            = 0;
    };

    class CanonicalScriptSink
    {
    public:
        virtual ~CanonicalScriptSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
            = 0;
    };

    class CanonicalMetricsSink
    {
    public:
        virtual ~CanonicalMetricsSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
            = 0;
    };

    class CanonicalSinkBundle
    {
    public:
        constexpr CanonicalSinkBundle() noexcept = default;
        constexpr CanonicalSinkBundle(CanonicalPersistenceSink* persistence, CanonicalReplaySink* replay,
            CanonicalScriptSink* script, CanonicalMetricsSink* metrics) noexcept
            : mPersistence(persistence)
            , mReplay(replay)
            , mScript(script)
            , mMetrics(metrics)
        {
        }

        constexpr CanonicalPersistenceSink* persistence() const noexcept { return mPersistence; }
        constexpr CanonicalReplaySink* replay() const noexcept { return mReplay; }
        constexpr CanonicalScriptSink* script() const noexcept { return mScript; }
        constexpr CanonicalMetricsSink* metrics() const noexcept { return mMetrics; }

    private:
        CanonicalPersistenceSink* mPersistence = nullptr;
        CanonicalReplaySink* mReplay = nullptr;
        CanonicalScriptSink* mScript = nullptr;
        CanonicalMetricsSink* mMetrics = nullptr;
    };

    class CanonicalSinkDeliveryReport
    {
    public:
        constexpr bool publicationOffered() const noexcept { return mPublicationOffered; }
        constexpr CanonicalSinkDeliveryResult result(CanonicalSinkRole role) const noexcept
        {
            return mResults[static_cast<std::size_t>(role)];
        }

        friend constexpr bool operator==(CanonicalSinkDeliveryReport, CanonicalSinkDeliveryReport) noexcept = default;

    private:
        friend class CanonicalCommandReducer;

        constexpr void markPublicationOffered() noexcept { mPublicationOffered = true; }
        constexpr void setResult(CanonicalSinkRole role, CanonicalSinkDeliveryResult result) noexcept
        {
            mResults[static_cast<std::size_t>(role)] = result;
        }

        bool mPublicationOffered = false;
        std::array<CanonicalSinkDeliveryResult, MaximumCanonicalSinkAttempts> mResults{
            CanonicalSinkDeliveryResult::NotConfigured,
            CanonicalSinkDeliveryResult::NotConfigured,
            CanonicalSinkDeliveryResult::NotConfigured,
            CanonicalSinkDeliveryResult::NotConfigured,
        };
    };
}

#endif
