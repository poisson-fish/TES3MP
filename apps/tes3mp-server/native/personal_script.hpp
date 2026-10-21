#ifndef TES3MP_NATIVE_PERSONAL_SCRIPT_HPP
#define TES3MP_NATIVE_PERSONAL_SCRIPT_HPP

#include "world_script.hpp"
#include <apps/openmw/mwscript/dialoguestate.hpp>
#include <apps/openmw/mwscript/executioncontext.hpp>
#include <memory>
#include <tes3mp/personal_dialogue.hpp>

namespace MWMechanics { class NpcStats; class CreatureStats; }

namespace TES3MP::Native
{
    // Explicitly admitted source-free banks and scoped dialogue results share
    // character/story state. Authenticated dialogue also owns bounded source-NPC
    // disposition and bounded terminal Disable/safe-return Enable. Scheduled policies suspend
    // offline characters and execute once per admitted host tick without catch-up.
    class PersonalScripts
    {
    public:
        static constexpr size_t MaximumInstances = 16;
        static constexpr size_t MaximumScriptsPerBank = 4;
        static constexpr size_t MaximumInvocationsPerTransaction = 16;
        static constexpr size_t MaximumImageBytes = 1048576;
        static constexpr size_t MaximumNpcsPerStory = 64;
        static constexpr size_t MaximumLifeEvents = 256;
        static constexpr size_t MaximumTrackedItemsPerStory = 64;
        static constexpr size_t MaximumItemReceiptsPerStory = 64;
        static constexpr size_t MaximumPartyMembers = 4;
        static constexpr size_t MaximumRewardReceiptsPerStory = 64;
        static constexpr int MaximumItemTransferCount = 100000;
        struct ItemBalance
        {
            ESM::RefId item;
            int count = 0;
            bool operator==(const ItemBalance&) const = default;
        };
        struct ItemReceipt
        {
            uint64_t scope = 1, sourceKind = 0, sourceId = 0, step = 0;
            bool playerTarget = true, addition = true;
            ESM::RefId item;
            int count = 0;
            bool operator==(const ItemReceipt&) const = default;
        };
        struct RewardReceipt
        {
            uint64_t occurrence = 0, effect = 0, recipient = 0, partyId = 0, sourceKind = 0, sourceId = 0;
            ESM::RefId topic, info, item;
            int count = 0;
            bool operator==(const RewardReceipt&) const = default;
        };
        struct PartyState
        {
            uint64_t partyId = 0, partyRevision = 0;
            uint64_t controller = 0, controllerRevision = 0;
            bool helper = false, suspended = false, separated = false;
            uint64_t sharedScope = 0, encounterReference = 0, encounterLife = 0;
            uint64_t stepPartyId = 0, stepPartyRevision = 0, stepController = 0, stepControllerRevision = 0;
            std::vector<uint64_t> stepParticipants;
            std::vector<uint64_t> helperLives;
            bool operator==(const PartyState&) const = default;
        };
        struct ParticipantContext
        {
            uint64_t character = 0;
            const MWMechanics::NpcStats* playerStats = nullptr;
            std::optional<std::string_view> customClassName = {};
            std::optional<std::span<const ItemBalance>> playerInventory = {};
            MWWorld::Ptr playerPtr = {};
        };
        struct EncounterBinding
        {
            uint64_t reference = 0;
            ESM::RefId record, cell;
            bool operator==(const EncounterBinding&) const = default;
        };
        struct Definition
        {
            struct Member { std::shared_ptr<const SharedWorldScript> script; bool running = false; };
            uint64_t character = 0, story = 0;
            std::shared_ptr<const SharedWorldScript> script;
            std::vector<Member> members;
            bool running = true;
            std::vector<ESM::Dialogue> dialogues;
            std::optional<EncounterBinding> encounter;
        };
        struct Instance
        {
            struct SourceWait
            {
                MWScript::ExecutionContext invocation;
                ESM::RefId script;
                int instruction = -1;
                uint64_t sinceTick = 0, respawnTick = 0;
                uint64_t reason = 1; // Foreign death; wake only on a newer living placement life.
                bool operator==(const SourceWait&) const = default;
            };
            struct Relocation
            {
                std::array<float, 3> origin{}, position{};
                float yaw = 0;
                bool operator==(const Relocation&) const = default;
                void validate(bool crossCell = false) const;
            };
            struct Npc
            {
                uint64_t reference = 0, life = 0;
                ESM::RefId record;
                int disposition = 0; // Stock raw base; derived disposition clamps later.
                bool operator==(const Npc&) const = default;
            };
            struct Activity
            {
                bool escort = false, completed = false;
                std::array<float, 3> destination{};
                float destinationTimer = 2, followDistance = 450, walkSpeed = 0;
                uint64_t tick = 0, steps = 0;
                std::vector<char> actorImage; // Same placement/origin life; no store or new actor identity.
                int packageType = -1; // -1 uses default (escort ? 2 : 1); 0=Wander, 1=Travel, 2=Escort, 3=Follow.
                ESM::RefId destinationCell;
                int effectivePackageType() const noexcept { return packageType >= 0 ? packageType : (escort ? 2 : 1); }
                bool operator==(const Activity&) const = default;
                void validate() const;
            };
            MWScript::ExecutionContext last;
            uint64_t eventCursor = 0;
            SharedWorldScript::State runtime;
            uint64_t scheduledTick = 0;
            // The first runtime owns globals; additional members own only GSCR
            // state/ordinals. Encoding lends them the canonical globals.
            std::vector<SharedWorldScript::State> members;
            MWScript::DialogueState::State dialogue;
            ESM::RefId lastTopic, lastInfo;
            int lastChoice = -1;
            uint64_t dialogueRevision = 0, dialogueCommand = 0;
            DialogueIntent dialogueRequest;
            std::vector<MWScript::DialogueState::Choice> choices;
            // Speakers personally met, bounded independently of shared NPC state.
            std::vector<uint64_t> talkedSpeakers;
            // Relationships belong to a character/story and placement, surviving
            // legitimate new lives. No public stats, inventories or loot are copied.
            std::vector<Npc> npcs;
            // One terminal source-only Disable encounter per character. The
            // origin life is frozen; no inventory, loot or alive NPC is cloned.
            uint64_t encounterLife = 0;
            bool encounterReturned = false; // Dormant outcome; origin and story receipts remain frozen.
            bool encounterEnabled = false; // Stock source Enable committed at safe return; never renews the story.
            std::optional<Relocation> relocation; // Retained private pose; V89 inert, V94 activity frame. Same origin life, no store copy.
            std::optional<Activity> activity;
            uint64_t clockTicks = 0; // Successful scheduled story steps only; no wait/offline/downtime catch-up.
            std::optional<SourceWait> sourceWait; // One bounded dependency; banks remain running and unchanged.
            uint64_t deathCursor = 0; // Source life, independent of dialogue/event ordinals.
            uint64_t reservedLife = 0, reservationTick = 0; // Durable next-opportunity right, retained after waking.
            std::vector<ItemBalance> playerItems, npcItems;
            std::vector<ItemReceipt> itemReceipts;
            PartyState party;
            std::vector<RewardReceipt> rewardReceipts;
        };
        struct LifeEvent
        {
            uint64_t life = 0, tick = 0, killer = 0, killerKind = 0, killerLife = 0;
            // Authority-supplied causal owner, including a summon chain's root.
            // Help-only enrollment cannot spend the helper's personal cursor.
            uint64_t owner = 0, ownerLife = 0;
            bool helper = false;
        };
        struct PlayerInventory
        {
            uint64_t character = 0;
            std::span<const ItemBalance> items;
        };
        struct PlayerLocation
        {
            uint64_t character = 0;
            std::array<float, 3> position{};
            std::string_view cellName;
        };
        struct Source
        {
            uint64_t reference = 0, life = 0, respawnTick = 0;
            ESM::RefId record;
            bool living = false;
            uint64_t killer = 0, killerKind = 0;
            std::span<const LifeEvent> events;
            const MWMechanics::CreatureStats* stats = nullptr; // Transaction-owned, never serialized.
            ESM::Position position{};
            const MWMechanics::CreatureStats* privateStats = nullptr; // Detached living stats for private scopes.
            bool cellChanged = false, sleeping = false;
            uint64_t cellChangedCharacter = 0, sleepingCharacter = 0;
            bool activated = false;
            uint64_t activatedCharacter = 0;
            std::span<const PlayerInventory> playerInventories = {};
            std::span<const PlayerLocation> playerLocations = {};
        };
        using State = std::vector<Instance>;
        struct Prepared
        {
            struct StagedItemTransfer
            {
                uint64_t character = 0, story = 0;
                ItemReceipt receipt;
                bool operator==(const StagedItemTransfer&) const = default;
            };
            State state;
            std::vector<char> image;
            SharedWorldScript::Result result;
            size_t invocations = 0; // Budget charge: member calls, or one stopped-bank receipt.
            std::string response;
            std::vector<StagedItemTransfer> itemTransfers;
        };
        struct DialogueCall
        {
            ESM::RefId topic;
            int choice = -1, disposition = 0;
            bool talkedToPlayer = false;
            std::string cellName;
        };
        PersonalScripts(uint64_t campaign, std::vector<Definition>, CredentialCrypto&, bool scheduled = false,
            bool banks = false, bool intents = false, bool returns = false, bool enables = false, bool relocations = false,
            bool sourceWaits = false, bool lifeEvents = false, bool fairOpportunities = false, bool activities = false,
            bool npcBanks = false, bool parties = false);
        uint64_t campaign() const { return mCampaign; }
        bool scheduled() const { return mScheduled; }
        bool banks() const { return mBanks; }
        bool dialogues() const { return mDialogues; }
        bool intents() const { return mIntents; }
        bool encounters() const { return mEncounters; }
        bool returns() const { return mReturns; }
        bool enables() const { return mEnables; }
        bool relocations() const { return mRelocations; }
        bool sourceWaits() const { return mSourceWaits; }
        bool lifeEvents() const { return mLifeEvents; }
        bool fairOpportunities() const { return mFairOpportunities; }
        bool activities() const { return mActivities; }
        bool npcBanks() const { return mNpcBanks; }
        bool parties() const { return mParties; }
        const std::vector<Definition>& definitions() const { return mDefinitions; }
        static uint64_t reservationOwner(const State&, uint64_t life, uint64_t reference = 0);
        static bool reservationAllows(const State&, uint64_t life, uint64_t character, uint64_t reference = 0);
        static const Instance::Relocation* relocation(const State&, uint64_t character);
        static uint64_t physicalScope(const State&, uint64_t character);
        Prepared preparePartyEnroll(const State&, uint64_t story, uint64_t partyId, uint64_t controller,
            uint64_t character, bool helper = false) const;
        Prepared preparePartyLeave(const State&, uint64_t story, uint64_t actorCharacter,
            uint64_t targetCharacter, bool dissolve = false) const;
        Prepared preparePartyDisconnect(const State&, uint64_t story, uint64_t character) const;
        Prepared preparePartyResume(const State&, uint64_t story, uint64_t character, bool asHelper = false) const;
        Prepared preparePartyController(const State&, uint64_t story, uint64_t newController) const;
        Prepared prepareQuestItemAcquisition(const State&, uint64_t story, uint64_t character, uint64_t sourceId,
            const ESM::RefId& item, int count, const MWWorld::ESMStore&) const;
        Prepared prepareReturn(const State&, uint64_t character, const MWWorld::ESMStore* = nullptr,
            uint64_t sourceLife = 0) const;
        State initial() const;
        std::vector<char> encode(const State&) const;
        State restore(std::span<const char>) const;
        static ReliablePersonalDialogue view(const Instance&, SessionId, SessionGeneration, ServerTick, CanonicalRevision);
        Prepared prepare(const State&, const MWScript::ExecutionContext&, const MWWorld::ESMStore&,
            const MWScript::ReferenceResolver&, size_t budget = MaximumInvocationsPerTransaction,
            MWScript::ReferenceState* = nullptr) const;
        Prepared prepareScheduled(const State&, uint64_t worldTick, std::span<const uint64_t> activeCharacters,
            const MWWorld::ESMStore&, const MWScript::ReferenceResolver&, const Source* = nullptr,
            std::span<const Source> sources = {}) const;
        Prepared prepareDialogue(const State&, const MWScript::ExecutionContext&, const ESM::RefId& topic,
            int choice, int disposition, bool talkedToPlayer, std::string_view cellName,
            const MWWorld::ESMStore&, const MWScript::ReferenceResolver&, size_t budget = MaximumInvocationsPerTransaction,
            const MWMechanics::NpcStats* playerStats = nullptr,
            std::optional<std::string_view> customClassName = {},
            const MWMechanics::NpcStats* speakerStats = nullptr,
            std::optional<MWScript::DialogueState::Stamp> journalStamp = {}, bool encounterPrepared = false,
            std::optional<std::span<const ItemBalance>> playerInventory = {}) const;
        Prepared prepareIntent(const State&, uint64_t character, uint64_t command, const DialogueIntent&,
            std::string_view cellName, const MWWorld::ESMStore&, const MWScript::ReferenceResolver&,
            size_t budget = MaximumInvocationsPerTransaction, const MWMechanics::NpcStats* playerStats = nullptr,
            std::optional<std::string_view> customClassName = {},
            const MWMechanics::NpcStats* speakerStats = nullptr,
            std::optional<MWScript::DialogueState::Stamp> journalStamp = {},
            std::optional<std::span<const ItemBalance>> playerInventory = {},
            std::span<const ParticipantContext> partyParticipants = {}) const;

    private:
        uint64_t mCampaign;
        std::vector<Definition> mDefinitions;
        CredentialDigest mIdentity;
        bool mScheduled;
        bool mBanks;
        bool mDialogues = false;
        bool mIntents = false;
        bool mEncounters = false;
        bool mReturns = false;
        bool mEnables = false;
        bool mRelocations = false;
        bool mSourceWaits = false;
        bool mLifeEvents = false, mFairOpportunities = false, mActivities = false, mNpcBanks = false, mParties = false;
        std::vector<Interpreter::Program> mRestorations;
        struct ResultProgram { ESM::RefId topic, info; Interpreter::Program program; };
        std::vector<std::vector<ResultProgram>> mResults;
        void validate(const Instance&, size_t) const;
        Prepared prepareBank(const State&, size_t, const MWScript::ExecutionContext&, const MWWorld::ESMStore&,
            const MWScript::ReferenceResolver&, size_t budget, MWScript::ReferenceState*) const;
    };
}
#endif
