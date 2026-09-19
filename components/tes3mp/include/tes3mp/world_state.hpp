#ifndef TES3MP_WORLD_STATE_HPP
#define TES3MP_WORLD_STATE_HPP

#include "content_identity.hpp"
#include "deterministic_random.hpp"
#include "wait_rest.hpp"

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
    inline constexpr std::size_t MaximumFactionCatalogEntries = 4'096;
    inline constexpr std::size_t MaximumFactionRanksPerFaction = 256;
    inline constexpr std::size_t MaximumFactionCatalogRanks = 16'384;
    inline constexpr std::size_t MaximumDialogueChoiceCatalogEntries = 16'384;
    inline constexpr std::size_t MaximumPlayerFactionStates = 256;
    inline constexpr std::size_t MaximumCanonicalFactionStates = 16'384;
    inline constexpr std::size_t MaximumNativeEnvironmentBytes = 512 * 1024;
    inline constexpr std::size_t MaximumWeatherIdentities = 256;
    inline constexpr std::size_t MaximumWeatherRegions = 4'096;
    inline constexpr std::size_t MaximumWeatherEligibilityEntries = 65'536;
    inline constexpr std::uint64_t MaximumWeatherTimingTicks = 1'000'000'000;
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

    struct FactionCatalogEntry
    {
        FactionId id;
        std::vector<FactionRank> ranks;
        friend bool operator==(const FactionCatalogEntry&, const FactionCatalogEntry&) noexcept = default;
    };

    struct DialogueChoiceCatalogEntry
    {
        DialogueChoiceId id;
        std::optional<FactionId> requiredFaction;
        FactionRank minimumRank = FactionRank::initial();
        std::int32_t minimumReputation = 0;
        friend constexpr bool operator==(DialogueChoiceCatalogEntry, DialogueChoiceCatalogEntry) noexcept = default;
    };

    class FactionDialogueCatalog
    {
    public:
        static std::optional<FactionDialogueCatalog> create(ContentManifestId manifest,
            std::span<const FactionCatalogEntry> factions,
            std::span<const DialogueChoiceCatalogEntry> dialogueChoices) noexcept;
        constexpr ContentManifestId manifest() const noexcept { return mManifest; }
        std::span<const FactionCatalogEntry> factions() const noexcept { return mFactions; }
        std::span<const DialogueChoiceCatalogEntry> dialogueChoices() const noexcept { return mDialogueChoices; }
        const FactionCatalogEntry* findFaction(FactionId id) const noexcept;
        const DialogueChoiceCatalogEntry* findDialogueChoice(DialogueChoiceId id) const noexcept;
        friend bool operator==(const FactionDialogueCatalog&, const FactionDialogueCatalog&) noexcept = default;

    private:
        FactionDialogueCatalog(ContentManifestId manifest, std::vector<FactionCatalogEntry> factions,
            std::vector<DialogueChoiceCatalogEntry> dialogueChoices) noexcept
            : mManifest(manifest)
            , mFactions(std::move(factions))
            , mDialogueChoices(std::move(dialogueChoices))
        {
        }
        ContentManifestId mManifest;
        std::vector<FactionCatalogEntry> mFactions;
        std::vector<DialogueChoiceCatalogEntry> mDialogueChoices;
    };

    struct WeatherRegionCatalogEntry
    {
        WeatherRegionId id;
        WeatherId initialWeather;
        std::uint64_t selectionIntervalTicks = 0;
        std::uint64_t transitionDurationTicks = 0;
        std::vector<WeatherId> eligibleWeather;
        friend bool operator==(const WeatherRegionCatalogEntry&, const WeatherRegionCatalogEntry&) noexcept = default;
    };

    class WeatherCatalog
    {
    public:
        static std::optional<WeatherCatalog> create(ContentManifestId manifest,
            std::span<const WeatherId> weather, std::span<const WeatherRegionCatalogEntry> regions) noexcept;
        constexpr ContentManifestId manifest() const noexcept { return mManifest; }
        std::span<const WeatherId> weather() const noexcept { return mWeather; }
        std::span<const WeatherRegionCatalogEntry> regions() const noexcept { return mRegions; }
        bool contains(WeatherId id) const noexcept;
        const WeatherRegionCatalogEntry* findRegion(WeatherRegionId id) const noexcept;
        friend bool operator==(const WeatherCatalog&, const WeatherCatalog&) noexcept = default;

    private:
        WeatherCatalog(ContentManifestId manifest, std::vector<WeatherId> weather,
            std::vector<WeatherRegionCatalogEntry> regions) noexcept
            : mManifest(manifest)
            , mWeather(std::move(weather))
            , mRegions(std::move(regions))
        {
        }
        ContentManifestId mManifest;
        std::vector<WeatherId> mWeather;
        std::vector<WeatherRegionCatalogEntry> mRegions;
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

    struct CanonicalFactionState
    {
        FactionId id;
        std::optional<FactionRank> rank;
        FactionMembershipRevision membershipRevision = FactionMembershipRevision::initial();
        ServerTick lastMembershipChangeTick = ServerTick::initial();
        std::int32_t reputation = 0;
        FactionReputationRevision reputationRevision = FactionReputationRevision::initial();
        ServerTick lastReputationChangeTick = ServerTick::initial();
        friend constexpr bool operator==(CanonicalFactionState, CanonicalFactionState) noexcept = default;
    };

    struct CanonicalPlayerFactionState
    {
        PlayerId player;
        std::vector<CanonicalFactionState> factions;
        friend bool operator==(const CanonicalPlayerFactionState&, const CanonicalPlayerFactionState&) noexcept
            = default;
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
        // Engine elapsed days are independent of the epoch date.
        std::optional<std::uint32_t> daysPassed;

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

    struct CanonicalWeatherRegionState
    {
        WeatherRegionId region;
        WeatherId currentWeather;
        WeatherId targetWeather;
        ServerTick transitionStartTick = ServerTick::initial();
        ServerTick transitionEndTick = ServerTick::initial();
        ServerTick nextSelectionTick = ServerTick::initial();
        WeatherRevision revision = WeatherRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        friend constexpr bool operator==(CanonicalWeatherRegionState, CanonicalWeatherRegionState) noexcept = default;
    };

    struct CanonicalWeatherState
    {
        std::vector<CanonicalWeatherRegionState> regions;
        RandomStateV1 randomState;
        ServerTick lastAdvanceTick = ServerTick::initial();
        // Opaque app-owned engine state. Time/weather above are committed wire
        // projections; only the native environment service may advance this domain.
        std::vector<std::byte> nativeEnvironment;

        friend bool operator==(const CanonicalWeatherState&, const CanonicalWeatherState&) noexcept = default;
    };

    class CanonicalWorldState
    {
    public:
        static std::optional<CanonicalWorldState> create(
            CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
        static std::optional<CanonicalWorldState> create(CanonicalWorldTimeState time,
            std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
            std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept;
        static std::optional<CanonicalWorldState> create(CanonicalWorldTimeState time,
            std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
            FactionDialogueCatalog factionDialogueCatalog,
            std::span<const CanonicalPlayerQuestJournalState> questJournal,
            std::span<const CanonicalPlayerFactionState> factions) noexcept;
        static std::optional<CanonicalWorldState> create(CanonicalWorldTimeState time,
            std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
            FactionDialogueCatalog factionDialogueCatalog, WeatherCatalog weatherCatalog,
            std::span<const CanonicalPlayerQuestJournalState> questJournal,
            std::span<const CanonicalPlayerFactionState> factions, CanonicalWeatherState weather) noexcept;
        static std::optional<CanonicalWorldState> initial(
            CanonicalWorldTimeState time, const GlobalVariableCatalog& catalog) noexcept;
        static std::optional<CanonicalWorldState> initial(CanonicalWorldTimeState time,
            const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog) noexcept;
        static std::optional<CanonicalWorldState> initial(CanonicalWorldTimeState time,
            const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog,
            FactionDialogueCatalog factionDialogueCatalog) noexcept;
        static std::optional<CanonicalWorldState> initial(CanonicalWorldTimeState time,
            const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog,
            FactionDialogueCatalog factionDialogueCatalog, WeatherCatalog weatherCatalog,
            RandomStateV1 weatherRandomState) noexcept;

        constexpr const CanonicalWorldTimeState& time() const noexcept { return mTime; }
        std::span<const CanonicalGlobalVariableState> globals() const noexcept { return mGlobals; }
        const CanonicalGlobalVariableState* find(GlobalVariableId id) const noexcept;
        const std::optional<QuestJournalCatalog>& questJournalCatalog() const noexcept { return mQuestJournalCatalog; }
        std::span<const CanonicalPlayerQuestJournalState> questJournal() const noexcept { return mQuestJournal; }
        const CanonicalPlayerQuestJournalState* findQuestJournal(PlayerId player) const noexcept;
        const std::optional<FactionDialogueCatalog>& factionDialogueCatalog() const noexcept
        {
            return mFactionDialogueCatalog;
        }
        std::span<const CanonicalPlayerFactionState> factionStates() const noexcept { return mFactionStates; }
        const CanonicalPlayerFactionState* findFactionState(PlayerId player) const noexcept;
        const std::optional<WeatherCatalog>& weatherCatalog() const noexcept { return mWeatherCatalog; }
        const std::optional<CanonicalWeatherState>& weather() const noexcept { return mWeather; }
        const CanonicalWeatherRegionState* findWeather(WeatherRegionId region) const noexcept;
        friend bool operator==(const CanonicalWorldState&, const CanonicalWorldState&) noexcept = default;

    private:
        CanonicalWorldState(CanonicalWorldTimeState time, std::vector<CanonicalGlobalVariableState> globals,
            std::optional<QuestJournalCatalog> questJournalCatalog = std::nullopt,
            std::vector<CanonicalPlayerQuestJournalState> questJournal = {},
            std::optional<FactionDialogueCatalog> factionDialogueCatalog = std::nullopt,
            std::vector<CanonicalPlayerFactionState> factionStates = {},
            std::optional<WeatherCatalog> weatherCatalog = std::nullopt,
            std::optional<CanonicalWeatherState> weather = std::nullopt) noexcept
            : mTime(time)
            , mGlobals(std::move(globals))
            , mQuestJournalCatalog(std::move(questJournalCatalog))
            , mQuestJournal(std::move(questJournal))
            , mFactionDialogueCatalog(std::move(factionDialogueCatalog))
            , mFactionStates(std::move(factionStates))
            , mWeatherCatalog(std::move(weatherCatalog))
            , mWeather(std::move(weather))
        {
        }
        CanonicalWorldTimeState mTime;
        std::vector<CanonicalGlobalVariableState> mGlobals;
        std::optional<QuestJournalCatalog> mQuestJournalCatalog;
        std::vector<CanonicalPlayerQuestJournalState> mQuestJournal;
        std::optional<FactionDialogueCatalog> mFactionDialogueCatalog;
        std::vector<CanonicalPlayerFactionState> mFactionStates;
        std::optional<WeatherCatalog> mWeatherCatalog;
        std::optional<CanonicalWeatherState> mWeather;
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
        UnknownFaction,
        UnknownFactionRank,
        FactionMembershipRevisionMismatch,
        FactionReputationRevisionMismatch,
        UnknownDialogueChoice,
        DialogueChoiceIneligible,
        UnknownWeatherRegion,
        UnknownWeather,
        WeatherIneligible,
        WeatherRevisionMismatch,
    };

    using CanonicalWorldMutationResult = std::variant<CanonicalWorldState, CanonicalWorldMutationError>;

    CanonicalWorldMutationResult advanceCanonicalWorldTime(
        const CanonicalWorldState& state, ServerTick tick, std::uint64_t tickIntervalMilliseconds) noexcept;
    CanonicalWorldMutationResult advanceCanonicalWorldTimeByHours(
        const CanonicalWorldState& state, ServerTick tick, std::uint8_t hours) noexcept;
    CanonicalWorldMutationResult setCanonicalWorldTime(const CanonicalWorldState& state,
        WorldTimeRevision expectedRevision, CanonicalWorldTimeState replacement, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalGlobal(const CanonicalWorldState& state,
        const GlobalVariableCatalog& catalog, GlobalVariableId id, GlobalVariableRevision expectedRevision,
        GlobalVariableValue value, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalQuestStage(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, QuestRevision expectedRevision, QuestStage stage, ServerTick tick) noexcept;
    CanonicalWorldMutationResult addCanonicalJournalEntry(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, JournalRevision expectedRevision, JournalEntryId entry, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalFactionRank(const CanonicalWorldState& state, PlayerId player,
        FactionId faction, FactionMembershipRevision expectedRevision, FactionRank rank, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalFactionReputation(const CanonicalWorldState& state, PlayerId player,
        FactionId faction, FactionReputationRevision expectedRevision, std::int32_t reputation,
        ServerTick tick) noexcept;
    std::optional<CanonicalWorldMutationError> validateCanonicalDialogueChoice(
        const CanonicalWorldState& state, PlayerId player, DialogueChoiceId choice) noexcept;
    CanonicalWorldMutationResult setCanonicalWeather(const CanonicalWorldState& state, WeatherRegionId region,
        WeatherRevision expectedRevision, WeatherId target, ServerTick tick) noexcept;
    CanonicalWorldMutationResult advanceCanonicalWeather(const CanonicalWorldState& state, ServerTick tick) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& catalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog,
        const FactionDialogueCatalog& expectedFactionDialogueCatalog, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        const FactionDialogueCatalog& restoredFactionDialogueCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog,
        const FactionDialogueCatalog& expectedFactionDialogueCatalog, const WeatherCatalog& expectedWeatherCatalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        const FactionDialogueCatalog& restoredFactionDialogueCatalog, const WeatherCatalog& restoredWeatherCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions, CanonicalWeatherState weather) noexcept;
}

#endif
