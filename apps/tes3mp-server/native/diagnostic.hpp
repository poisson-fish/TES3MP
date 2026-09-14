#ifndef TES3MP_NATIVE_DIAGNOSTIC_H
#define TES3MP_NATIVE_DIAGNOSTIC_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace TES3MP::Native
{
    // Callers may lower these ceilings, never raise them. This deliberately
    // samples the first IDs in each engine store, not a complete gameplay catalog.
    struct DiagnosticLimits
    {
        std::size_t mRecordsPerType = 4;
        std::size_t mMaxRecords = 60;
        std::size_t mMaxStringBytes = 4096;
        std::size_t mMaxEffectsPerRecord = 32;
        std::size_t mMaxBytes = 64 * 1024;
    };

    struct DiagnosticEffect
    {
        std::string mId, mSkill, mAttribute;
        std::uint32_t mIndex;
        std::int32_t mRange, mArea, mDuration, mMagnitudeMin, mMagnitudeMax;
        bool operator==(const DiagnosticEffect&) const = default;
    };

    struct DiagnosticRecord
    {
        std::string mType, mId;
        std::optional<std::string> mName;
        std::optional<float> mWeight;
        std::optional<std::int32_t> mValue;
        std::vector<DiagnosticEffect> mEffects;
        // Disengaged for non-GMSTs; monostate preserves OpenMW's empty GMST.
        std::optional<std::variant<std::monostate, std::int32_t, float, std::string>> mSetting;
        bool operator==(const DiagnosticRecord&) const = default;
    };

    // Owns every value, with no engine types, views, pointers or catalog lookup.
    // Both aggregate string storage and the complete escaped TSV are bounded by
    // mMaxBytes; vector storage is separately bounded by record/effect ceilings.
    // This diagnostic format is not a wire protocol or a gameplay authority.
    struct DiagnosticSample
    {
        std::vector<DiagnosticRecord> mRecords;
        std::string mReport;
        bool operator==(const DiagnosticSample&) const = default;
    };
}

#endif
