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
                void validate() const;
            };
            struct Npc
            {
                uint64_t reference = 0, life = 0;
                ESM::RefId record;
                int disposition = 0; // Stock raw base; derived disposition clamps later.
                bool operator==(const Npc&) const = default;
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
            std::optional<Relocation> relocation; // Terminal inert pose; original placement/life, no new loot or actor simulation.
            uint64_t clockTicks = 0; // Successful scheduled story steps only; no wait/offline/downtime catch-up.
            std::optional<SourceWait> sourceWait; // One bounded dependency; banks remain running and unchanged.
            uint64_t deathCursor = 0; // Source life, independent of dialogue/event ordinals.
            uint64_t reservedLife = 0, reservationTick = 0; // Durable next-opportunity right, retained after waking.
        };
        struct LifeEvent
        {
            uint64_t life = 0, tick = 0, killer = 0, killerKind = 0, killerLife = 0;
            // Authority-supplied causal owner, including a summon chain's root.
            // Help-only enrollment cannot spend the helper's personal cursor.
            uint64_t owner = 0, ownerLife = 0;
            bool helper = false;
        };
        struct Source
        {
            uint64_t reference = 0, life = 0, respawnTick = 0;
            ESM::RefId record;
            bool living = false;
            uint64_t killer = 0, killerKind = 0;
            std::span<const LifeEvent> events;
            const MWMechanics::CreatureStats* stats = nullptr; // Transaction-owned, never serialized.
        };
        using State = std::vector<Instance>;
        struct Prepared
        {
            State state;
            std::vector<char> image;
            SharedWorldScript::Result result;
            size_t invocations = 0; // Budget charge: member calls, or one stopped-bank receipt.
            std::string response;
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
            bool sourceWaits = false, bool lifeEvents = false, bool fairOpportunities = false);
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
        static uint64_t reservationOwner(const State&, uint64_t life);
        static const Instance::Relocation* relocation(const State&, uint64_t character);
        static uint64_t physicalScope(const State&, uint64_t character);
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
            const MWWorld::ESMStore&, const MWScript::ReferenceResolver&, const Source* = nullptr) const;
        Prepared prepareDialogue(const State&, const MWScript::ExecutionContext&, const ESM::RefId& topic,
            int choice, int disposition, bool talkedToPlayer, std::string_view cellName,
            const MWWorld::ESMStore&, const MWScript::ReferenceResolver&, size_t budget = MaximumInvocationsPerTransaction,
            const MWMechanics::NpcStats* playerStats = nullptr,
            std::optional<std::string_view> customClassName = {},
            const MWMechanics::NpcStats* speakerStats = nullptr,
            std::optional<MWScript::DialogueState::Stamp> journalStamp = {}, bool encounterPrepared = false) const;
        Prepared prepareIntent(const State&, uint64_t character, uint64_t command, const DialogueIntent&,
            std::string_view cellName, const MWWorld::ESMStore&, const MWScript::ReferenceResolver&,
            size_t budget = MaximumInvocationsPerTransaction, const MWMechanics::NpcStats* playerStats = nullptr,
            std::optional<std::string_view> customClassName = {},
            const MWMechanics::NpcStats* speakerStats = nullptr,
            std::optional<MWScript::DialogueState::Stamp> journalStamp = {}) const;

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
        bool mLifeEvents = false, mFairOpportunities = false;
        std::vector<Interpreter::Program> mRestorations;
        struct ResultProgram { ESM::RefId topic, info; Interpreter::Program program; };
        std::vector<std::vector<ResultProgram>> mResults;
        void validate(const Instance&, size_t) const;
        Prepared prepareBank(const State&, size_t, const MWScript::ExecutionContext&, const MWWorld::ESMStore&,
            const MWScript::ReferenceResolver&, size_t budget, MWScript::ReferenceState*) const;
    };
}
#endif
