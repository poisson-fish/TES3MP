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
        Archive,
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

    // Post-publication archival/diagnostic observation only. Durable commits
    // use CanonicalDurabilityPort and cannot be acknowledged here.
    class CanonicalArchiveSink
    {
    public:
        virtual ~CanonicalArchiveSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept = 0;
    };

    class CanonicalReplaySink
    {
    public:
        virtual ~CanonicalReplaySink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept = 0;
    };

    class CanonicalScriptSink
    {
    public:
        virtual ~CanonicalScriptSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept = 0;
    };

    class CanonicalMetricsSink
    {
    public:
        virtual ~CanonicalMetricsSink() = default;
        virtual CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept = 0;
    };

    class CanonicalSinkBundle
    {
    public:
        constexpr CanonicalSinkBundle() noexcept = default;
        constexpr CanonicalSinkBundle(CanonicalArchiveSink* archive, CanonicalReplaySink* replay,
            CanonicalScriptSink* script, CanonicalMetricsSink* metrics) noexcept
            : mArchive(archive)
            , mReplay(replay)
            , mScript(script)
            , mMetrics(metrics)
        {
        }

        constexpr CanonicalArchiveSink* archive() const noexcept { return mArchive; }
        constexpr CanonicalReplaySink* replay() const noexcept { return mReplay; }
        constexpr CanonicalScriptSink* script() const noexcept { return mScript; }
        constexpr CanonicalMetricsSink* metrics() const noexcept { return mMetrics; }

    private:
        CanonicalArchiveSink* mArchive = nullptr;
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
