#ifndef TES3MP_WORLD_STATE_HPP
#define TES3MP_WORLD_STATE_HPP

#include "content_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumGlobalVariables = 65'536;
    inline constexpr std::size_t MaximumQuestCatalogEntries = 4'096;
    inline constexpr std::size_t MaximumQuestStagesPerQuest = 1'024;
    inline constexpr std::size_t MaximumQuestCatalogStages = 32'768;
    inline constexpr std::size_t MaximumJournalCatalogEntries = 16'384;
    inline constexpr std::size_t MaximumPlayerQuestJournalStates = 256;
    inline constexpr std::size_t MaximumCanonicalQuestStates = 16'384;
    inline constexpr std::size_t MaximumCanonicalJournalEntries = 16'384;
    inline constexpr std::uint32_t WorldMillisecondsPerHour = 3'600'000;
    inline constexpr std::uint32_t WorldMillisecondsPerDay = 24 * WorldMillisecondsPerHour;
    inline constexpr std::uint32_t WorldTimeScaleUnitsPerOne = 1'000;
    inline constexpr std::uint32_t MaximumWorldTimeScaleUnits = 1'000'000;

    enum class GlobalVariableType : std::uint8_t
    {
        Short,
        Long,
        Float,
    };

    using GlobalVariableValue = std::variant<std::int16_t, std::int32_t, float>;

    struct GlobalVariableCatalogEntry
    {
        GlobalVariableId id;
        GlobalVariableValue initialValue;

        constexpr GlobalVariableType type() const noexcept
        {
            return static_cast<GlobalVariableType>(initialValue.index());
        }
        friend constexpr bool operator==(const GlobalVariableCatalogEntry&, const GlobalVariableCatalogEntry&) noexcept
            = default;
    };

    class GlobalVariableCatalog
    {
    public:
        static std::optional<GlobalVariableCatalog> create(
            std::span<const GlobalVariableCatalogEntry> entries) noexcept;
        std::span<const GlobalVariableCatalogEntry> entries() const noexcept { return mEntries; }
        const GlobalVariableCatalogEntry* find(GlobalVariableId id) const noexcept;
        friend bool operator==(const GlobalVariableCatalog&, const GlobalVariableCatalog&) noexcept = default;

    private:
        explicit GlobalVariableCatalog(std::vector<GlobalVariableCatalogEntry> entries) noexcept
            : mEntries(std::move(entries))
        {
        }
        std::vector<GlobalVariableCatalogEntry> mEntries;
    };

    struct QuestCatalogEntry
    {
        QuestId id;
        QuestStage initialStage;
        std::vector<QuestStage> stages;
        friend bool operator==(const QuestCatalogEntry&, const QuestCatalogEntry&) noexcept = default;
    };

    struct JournalCatalogEntry
    {
        JournalEntryId id;
        QuestId quest;
        QuestStage stage;
        friend constexpr bool operator==(JournalCatalogEntry, JournalCatalogEntry) noexcept = default;
    };

    class QuestJournalCatalog
    {
    public:
        static std::optional<QuestJournalCatalog> create(ContentManifestId manifest,
            std::span<const QuestCatalogEntry> quests, std::span<const JournalCatalogEntry> journal) noexcept;
        constexpr ContentManifestId manifest() const noexcept { return mManifest; }
        std::span<const QuestCatalogEntry> quests() const noexcept { return mQuests; }
        std::span<const JournalCatalogEntry> journal() const noexcept { return mJournal; }
        const QuestCatalogEntry* findQuest(QuestId id) const noexcept;
        const JournalCatalogEntry* findJournalEntry(JournalEntryId id) const noexcept;
        friend bool operator==(const QuestJournalCatalog&, const QuestJournalCatalog&) noexcept = default;

    private:
        QuestJournalCatalog(ContentManifestId manifest, std::vector<QuestCatalogEntry> quests,
            std::vector<JournalCatalogEntry> journal) noexcept
            : mManifest(manifest)
            , mQuests(std::move(quests))
            , mJournal(std::move(journal))
        {
        }
        ContentManifestId mManifest;
        std::vector<QuestCatalogEntry> mQuests;
        std::vector<JournalCatalogEntry> mJournal;
    };

    struct CanonicalQuestState
    {
        QuestId id;
        QuestStage stage;
        QuestRevision revision = QuestRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        friend constexpr bool operator==(CanonicalQuestState, CanonicalQuestState) noexcept = default;
    };

    struct CanonicalJournalEntryState
    {
        JournalEntryId id;
        JournalRevision revision = JournalRevision::initial();
        ServerTick changeTick = ServerTick::initial();
        friend constexpr bool operator==(CanonicalJournalEntryState, CanonicalJournalEntryState) noexcept = default;
    };

    struct CanonicalPlayerQuestJournalState
    {
        PlayerId player;
        std::vector<CanonicalQuestState> quests;
        std::vector<CanonicalJournalEntryState> journal;
        JournalRevision journalRevision = JournalRevision::initial();
        ServerTick lastJournalChangeTick = ServerTick::initial();
        friend bool operator==(
            const CanonicalPlayerQuestJournalState&, const CanonicalPlayerQuestJournalState&) noexcept = default;
    };

    struct CanonicalWorldTimeState
    {
        std::uint8_t day = 1;
        std::uint8_t month = 0;
        std::int32_t year = 0;
        std::uint32_t millisecondsSinceMidnight = 0;
        std::uint32_t timeScaleUnits = 30 * WorldTimeScaleUnitsPerOne;
        std::uint16_t subMillisecondRemainder = 0;
        WorldTimeRevision revision = WorldTimeRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        ServerTick lastAdvanceTick = ServerTick::initial();

        double hour() const noexcept
        {
            return static_cast<double>(millisecondsSinceMidnight) / WorldMillisecondsPerHour;
        }
        double timeScale() const noexcept { return static_cast<double>(timeScaleUnits) / WorldTimeScaleUnitsPerOne; }
        friend constexpr bool operator==(const CanonicalWorldTimeState&, const CanonicalWorldTimeState&) noexcept
            = default;
    };

    struct CanonicalGlobalVariableState
    {
        GlobalVariableId id;
        GlobalVariableValue value;
        GlobalVariableRevision revision = GlobalVariableRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();

        constexpr GlobalVariableType type() const noexcept { return static_cast<GlobalVariableType>(value.index()); }
        friend constexpr bool operator==(
            const CanonicalGlobalVariableState&, const CanonicalGlobalVariableState&) noexcept = default;
    };

    class CanonicalWorldState
    {
    public:
        static std::optional<CanonicalWorldState> create(
            CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
        static std::optional<CanonicalWorldState> create(CanonicalWorldTimeState time,
            std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
            std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept;
        static std::optional<CanonicalWorldState> initial(
            CanonicalWorldTimeState time, const GlobalVariableCatalog& catalog) noexcept;
        static std::optional<CanonicalWorldState> initial(CanonicalWorldTimeState time,
            const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog) noexcept;

        constexpr const CanonicalWorldTimeState& time() const noexcept { return mTime; }
        std::span<const CanonicalGlobalVariableState> globals() const noexcept { return mGlobals; }
        const CanonicalGlobalVariableState* find(GlobalVariableId id) const noexcept;
        const std::optional<QuestJournalCatalog>& questJournalCatalog() const noexcept { return mQuestJournalCatalog; }
        std::span<const CanonicalPlayerQuestJournalState> questJournal() const noexcept { return mQuestJournal; }
        const CanonicalPlayerQuestJournalState* findQuestJournal(PlayerId player) const noexcept;
        friend bool operator==(const CanonicalWorldState&, const CanonicalWorldState&) noexcept = default;

    private:
        CanonicalWorldState(CanonicalWorldTimeState time, std::vector<CanonicalGlobalVariableState> globals,
            std::optional<QuestJournalCatalog> questJournalCatalog = std::nullopt,
            std::vector<CanonicalPlayerQuestJournalState> questJournal = {}) noexcept
            : mTime(time)
            , mGlobals(std::move(globals))
            , mQuestJournalCatalog(std::move(questJournalCatalog))
            , mQuestJournal(std::move(questJournal))
        {
        }
        CanonicalWorldTimeState mTime;
        std::vector<CanonicalGlobalVariableState> mGlobals;
        std::optional<QuestJournalCatalog> mQuestJournalCatalog;
        std::vector<CanonicalPlayerQuestJournalState> mQuestJournal;
    };

    enum class CanonicalWorldMutationError : std::uint8_t
    {
        InvalidState,
        TickRegression,
        RevisionExhausted,
        ArithmeticOverflow,
        UnknownGlobal,
        TypeMismatch,
        RevisionMismatch,
        CatalogMismatch,
        UnknownPlayer,
        UnknownQuest,
        UnknownQuestStage,
        QuestRevisionMismatch,
        UnknownJournalEntry,
        JournalEntryQuestMismatch,
        JournalRevisionMismatch,
        DuplicateJournalEntry,
    };

    using CanonicalWorldMutationResult = std::variant<CanonicalWorldState, CanonicalWorldMutationError>;

    CanonicalWorldMutationResult advanceCanonicalWorldTime(
        const CanonicalWorldState& state, ServerTick tick, std::uint64_t tickIntervalMilliseconds) noexcept;
    CanonicalWorldMutationResult setCanonicalWorldTime(const CanonicalWorldState& state,
        WorldTimeRevision expectedRevision, CanonicalWorldTimeState replacement, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalGlobal(const CanonicalWorldState& state,
        const GlobalVariableCatalog& catalog, GlobalVariableId id, GlobalVariableRevision expectedRevision,
        GlobalVariableValue value, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalQuestStage(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, QuestRevision expectedRevision, QuestStage stage, ServerTick tick) noexcept;
    CanonicalWorldMutationResult addCanonicalJournalEntry(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, JournalRevision expectedRevision, JournalEntryId entry, ServerTick tick) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& catalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept;
}

#endif
