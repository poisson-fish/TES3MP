#include "personal_script.hpp"
#include <algorithm>
#include <apps/openmw/mwdialogue/filter.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/disposition.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/scriptstate.hpp>
#include <apps/openmw/mwscript/referencestate.hpp>
#include <apps/openmw/mwscript/miscextensions.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryitem.hpp>
#include <bit>
#include <cmath>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/interpreter/defines.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <limits>
#include <source_location>
#include <sstream>

namespace TES3MP::Native
{
    namespace
    {
        struct EncounterPreparationRequired {};
        constexpr uint64_t Magic = 0x3154535054335354;
        constexpr uint64_t ScheduledMagic = 0x3254535054335354;
        constexpr uint64_t BankMagic = 0x3354535054335354;
        constexpr uint64_t DialogueMagic = 0x3454535054335354;
        constexpr uint64_t EncounterMagic = 0x3854535054335354;
        constexpr uint64_t EncounterReturnMagic = 0x3954535054335354;
        constexpr uint64_t EncounterEnableMagic = 0x4154535054335354;
        constexpr uint64_t EncounterRelocationMagic = 0x4254535054335354;
        constexpr uint64_t EncounterRelocationReturnMagic = 0x4354535054335354;
        constexpr uint64_t IntentMagic = 0x3754535054335354;
        constexpr uint64_t SourceWaitMagic = 0x4454535054335354;
        constexpr uint64_t LifeEventMagic = 0x4554535054335354;
        constexpr uint64_t ActivityMagic = 0x4754535054335354;
        constexpr uint64_t OpportunityMagic = 0x4654535054335354;
        constexpr uint64_t NpcBankMagic = 0x4854535054335354;
        void require(bool value, std::source_location location = std::source_location::current())
        {
            if (!value)
                throw std::invalid_argument(
                    "Invalid or incompatible personal script state at line " + std::to_string(location.line()));
        }
        void word(std::vector<char>& bytes, uint64_t value)
        { for (size_t i = 0; i < 8; ++i) bytes.push_back(static_cast<char>(value >> (8 * i))); }
        struct Cursor
        {
            std::span<const char> bytes;
            std::span<const char> take(size_t count)
            { require(count <= bytes.size()); auto result = bytes.first(count); bytes = bytes.subspan(count); return result; }
            uint64_t word()
            {
                uint64_t result = 0; auto value = take(8);
                for (size_t i = 0; i < 8; ++i) result |= uint64_t(static_cast<unsigned char>(value[i])) << (8 * i);
                return result;
            }
        };
        void ref(std::vector<char>& bytes, const ESM::RefId& id)
        {
            const std::string_view name = id.empty() ? std::string_view{} : id.getRefIdString();
            require(name.size() <= MWScript::WorldScript::MaximumNameBytes && name.find('\0') == std::string_view::npos);
            word(bytes, name.size()); bytes.insert(bytes.end(), name.begin(), name.end());
        }
        ESM::RefId ref(Cursor& cursor)
        {
            const auto count = cursor.word(); require(count <= MWScript::WorldScript::MaximumNameBytes);
            const auto bytes = cursor.take(size_t(count));
            require(std::ranges::find(bytes, '\0') == bytes.end());
            return ESM::RefId::stringRefId(std::string_view(bytes.data(), bytes.size()));
        }
        void progression(std::vector<char>& bytes, const PersonalScripts::Instance& state, bool journalText)
        {
            ref(bytes, state.lastTopic); ref(bytes, state.lastInfo); word(bytes, std::bit_cast<uint32_t>(state.lastChoice));
            word(bytes, state.dialogue.quests.size());
            for (const auto& quest : state.dialogue.quests)
            { ref(bytes, quest.mTopic); word(bytes, std::bit_cast<uint32_t>(quest.mState)); word(bytes, quest.mFinished); }
            word(bytes, state.dialogue.entries.size());
            for (const auto& entry : state.dialogue.entries)
            {
                ref(bytes, entry.topic); ref(bytes, entry.info);
                if (journalText)
                {
                    word(bytes, entry.text.size()); bytes.insert(bytes.end(), entry.text.begin(), entry.text.end());
                    word(bytes, entry.stamp.day); word(bytes, entry.stamp.month); word(bytes, entry.stamp.dayOfMonth);
                }
            }
            word(bytes, state.dialogue.topics.size());
            for (const auto& topic : state.dialogue.topics) ref(bytes, topic);
        }
        void progression(Cursor& cursor, PersonalScripts::Instance* state, bool journalText)
        {
            const auto topic = ref(cursor), lastInfo = ref(cursor); const auto choice = cursor.word(); require(choice <= UINT32_MAX);
            if (state) { state->lastTopic = topic; state->lastInfo = lastInfo; state->lastChoice = std::bit_cast<int32_t>(uint32_t(choice)); }
            auto count = cursor.word(); require(count <= MWScript::DialogueState::MaximumRecords);
            for (size_t i = 0; i < count; ++i)
            {
                auto id = ref(cursor); const auto index = cursor.word(), finished = cursor.word();
                require(index <= UINT32_MAX && finished <= 1);
                if (state) state->dialogue.quests.push_back({id, std::bit_cast<int32_t>(uint32_t(index)), static_cast<unsigned char>(finished)});
            }
            count = cursor.word(); require(count <= MWScript::DialogueState::MaximumEntries);
            size_t textBytes = 0;
            for (size_t i = 0; i < count; ++i)
            {
                auto id = ref(cursor), info = ref(cursor);
                MWScript::DialogueState::Entry entry{id, info};
                if (journalText)
                {
                    const auto size = cursor.word(); require(size <= MWScript::DialogueState::MaximumTextBytes);
                    textBytes += size; require(textBytes <= MWScript::DialogueState::MaximumJournalTextBytes);
                    const auto text = cursor.take(size); require(std::ranges::find(text, '\0') == text.end());
                    const auto day = cursor.word(), month = cursor.word(), dayOfMonth = cursor.word();
                    require(day <= INT32_MAX && month < 12 && dayOfMonth <= 31);
                    entry.stamp = {int(day), int(month), int(dayOfMonth)}; require(entry.stamp.valid());
                    if (state) entry.text.assign(text.data(), text.size());
                }
                if (state) state->dialogue.entries.push_back(std::move(entry));
            }
            count = cursor.word(); require(count <= MWScript::DialogueState::MaximumRecords);
            for (size_t i = 0; i < count; ++i) { auto id = ref(cursor); if (state) state->dialogue.topics.push_back(id); }
        }
        void conversation(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, state.dialogueRevision);
            word(bytes, state.dialogueCommand);
            const auto& request = state.dialogueRequest;
            for (auto value : { request.story, request.speaker, request.life, request.revision })
                word(bytes, value);
            ref(bytes, ESM::RefId::stringRefId(request.topic));
            word(bytes, std::bit_cast<uint32_t>(request.choice));
            word(bytes, state.choices.size());
            for (const auto& choice : state.choices)
            {
                word(bytes, choice.text.size());
                bytes.insert(bytes.end(), choice.text.begin(), choice.text.end());
                word(bytes, std::bit_cast<uint32_t>(choice.id));
            }
            word(bytes, state.talkedSpeakers.size());
            for (auto speaker : state.talkedSpeakers)
                word(bytes, speaker);
        }
        void conversation(Cursor& cursor, PersonalScripts::Instance* state)
        {
            const auto revision = cursor.word(), command = cursor.word();
            DialogueIntent request{ cursor.word(), cursor.word(), cursor.word(), cursor.word(), ref(cursor).toString(),
                -1 };
            const auto choice = cursor.word();
            require(choice <= UINT32_MAX);
            request.choice = std::bit_cast<int32_t>(uint32_t(choice));
            if (state)
            {
                state->dialogueRevision = revision;
                state->dialogueCommand = command;
                state->dialogueRequest = request;
            }
            auto count = cursor.word();
            require(count <= 8);
            for (size_t i = 0; i < count; ++i)
            {
                const auto size = cursor.word();
                require(size && size <= 256);
                const auto text = cursor.take(size);
                require(std::ranges::find(text, '\0') == text.end());
                const auto id = cursor.word();
                require(id <= INT32_MAX);
                if (state)
                    state->choices.push_back({ std::string(text.data(), text.size()), int(id) });
            }
            count = cursor.word();
            require(count <= 64);
            for (size_t i = 0; i < count; ++i)
            {
                const auto id = cursor.word();
                require(id);
                if (state)
                    state->talkedSpeakers.push_back(id);
            }
        }
        void npcs(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, state.npcs.size());
            for (const auto& npc : state.npcs)
            {
                word(bytes, npc.reference); word(bytes, npc.life); ref(bytes, npc.record);
                word(bytes, std::bit_cast<uint32_t>(npc.disposition));
            }
        }
        void npcs(Cursor& cursor, PersonalScripts::Instance* state)
        {
            const auto count = cursor.word(); require(count <= PersonalScripts::MaximumNpcsPerStory);
            for (size_t i = 0; i < count; ++i)
            {
                PersonalScripts::Instance::Npc npc{cursor.word(), cursor.word(), ref(cursor)};
                const auto disposition = cursor.word(); require(disposition <= UINT32_MAX);
                npc.disposition = std::bit_cast<int32_t>(uint32_t(disposition));
                require(bool(npc.reference) == bool(npc.life) && !npc.record.empty());
                if (state) state->npcs.push_back(std::move(npc));
            }
        }
        struct Bank final : MWScript::ScriptState
        {
            const MWScript::ExecutionContext& expected;
            std::vector<const SharedWorldScript*> scripts;
            std::vector<MWScript::GlobalScriptDesc> instances;
            Bank(const MWScript::ExecutionContext& execution, const PersonalScripts::Definition& definition,
                const PersonalScripts::Instance& state) : expected(execution), scripts{definition.script.get()}
            {
                for (const auto& member : definition.members) scripts.push_back(member.script.get());
                instances.reserve(scripts.size());
                for (size_t i = 0; i < scripts.size(); ++i)
                {
                    const auto& member = i ? state.members[i - 1] : state.runtime;
                    auto& descriptor = instances.emplace_back();
                    descriptor.mLocals = MWScript::Locals::restore(member.runtime.script.mLocals,
                        member.runtime.script.mId, scripts[i]->declarations());
                    descriptor.mRunning = member.runtime.script.mRunning;
                }
            }
            Binding resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id) override
            {
                require(execution == expected);
                for (size_t i = 0; i < instances.size(); ++i)
                    if (instances[i].mLocals.getScriptId() == id) return {instances[i], scripts[i]->declarations()};
                throw std::invalid_argument("Script lacks character/story bank ownership");
            }
            void write(PersonalScripts::Instance& state) const
            {
                for (size_t i = 0; i < scripts.size(); ++i)
                {
                    auto& member = i ? state.members[i - 1] : state.runtime;
                    member.runtime.script.mRunning = instances[i].mRunning;
                    member.runtime.script.mLocals.mVariables.clear();
                    require(instances[i].mLocals.write(member.runtime.script.mLocals, scripts[i]->declarations()));
                }
            }
        };
        struct FilterContext final : MWScript::InterpreterContext
        {
            const std::vector<ESM::Global>& globals;
            const std::string_view cellName;
            const std::optional<std::string_view> customClassName;
            const Interpreter::Context* journalGlobals = nullptr;
            FilterContext(const MWScript::ExecutionContext& execution, const MWWorld::ESMStore& content,
                const MWScript::ReferenceResolver& resolver, Bank& bank, MWScript::DialogueState& dialogue,
                const std::vector<ESM::Global>& values, MWMechanics::NpcStats* playerStats, std::string_view cell,
                std::optional<std::string_view> customClass, MWScript::ReferenceState* referenceState = nullptr)
                : InterpreterContext(nullptr, execution, content, resolver, playerStats, &bank, &dialogue, nullptr, referenceState)
                , globals(values), cellName(cell), customClassName(customClass) {}
            std::string_view getPCClass() const override
            {
                if (!customClassName) return InterpreterContext::getPCClass();
                require(getPlayer().get<ESM::NPC>()->mBase->mClass.empty());
                return *customClassName;
            }
            std::string_view getCurrentCellName() const override { return cellName; }
            std::vector<std::string> getGlobals() const override
            {
                if (journalGlobals) return journalGlobals->getGlobals();
                std::vector<std::string> names;
                for (const auto& value : globals) names.emplace_back(value.mId.getRefIdString());
                return names;
            }
            const ESM::Variant& global(std::string_view id) const
            {
                auto found = std::ranges::find(globals, ESM::RefId::stringRefId(id), &ESM::Global::mId);
                require(found != globals.end()); return found->mValue;
            }
            char getGlobalType(std::string_view id) const override
            {
                if (journalGlobals) return journalGlobals->getGlobalType(id);
                auto found = std::ranges::find(globals, ESM::RefId::stringRefId(id), &ESM::Global::mId);
                if (found == globals.end()) return ' ';
                return found->mValue.getType() == ESM::VT_Float ? 'f' : found->mValue.getType() == ESM::VT_Short ? 's' : 'l';
            }
            int getGlobalShort(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalShort(id) : global(id).getInteger(); }
            int getGlobalLong(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalLong(id) : global(id).getInteger(); }
            float getGlobalFloat(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalFloat(id) : global(id).getFloat(); }
        };
        struct JournalText final : MWScript::DialogueState::JournalText
        {
            const FilterContext& snapshot;
            explicit JournalText(const FilterContext& value, MWScript::DialogueState::Stamp date) : snapshot(value)
            { stamp = date; }
            std::string expand(std::string_view source, Interpreter::Context& globals) const override
            {
                auto context = snapshot;
                context.journalGlobals = &globals;
                return Interpreter::fixDefinesDialog(source, context, MWScript::DialogueState::MaximumTextBytes);
            }
        };
        void context(std::vector<char>& bytes, const MWScript::ExecutionContext& value)
        {
            for (const auto field : {value.campaign, value.story, value.physicalScope, value.character,
                value.sourceReference, value.sourceLife, value.invocation, value.causalEvent}) word(bytes, field);
        }
        MWScript::ExecutionContext context(Cursor& cursor)
        {
            using Context = MWScript::ExecutionContext;
            return {Context::Ownership::Character, cursor.word(), cursor.word(), cursor.word(), cursor.word(),
                cursor.word(), cursor.word(), cursor.word(), cursor.word()};
        }
        void sourceWait(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, state.clockTicks); word(bytes, bool(state.sourceWait));
            if (!state.sourceWait) return;
            const auto& wait = *state.sourceWait;
            context(bytes, wait.invocation); ref(bytes, wait.script);
            word(bytes, std::bit_cast<uint32_t>(wait.instruction));
            word(bytes, wait.sinceTick); word(bytes, wait.respawnTick); word(bytes, wait.reason);
        }
        void sourceWait(Cursor& cursor, PersonalScripts::Instance* state)
        {
            const auto clock = cursor.word(), present = cursor.word(); require(present <= 1);
            if (state) state->clockTicks = clock;
            if (!present) return;
            PersonalScripts::Instance::SourceWait wait;
            wait.invocation = context(cursor); wait.script = ref(cursor);
            const auto instruction = cursor.word(); require(instruction <= INT32_MAX);
            wait.instruction = int(instruction); wait.sinceTick = cursor.word();
            wait.respawnTick = cursor.word(); wait.reason = cursor.word();
            require(wait.reason == 1 && wait.sinceTick && wait.invocation.sourceReference && wait.invocation.sourceLife);
            wait.invocation.validate();
            if (state) state->sourceWait = std::move(wait);
        }
    }

    namespace
    {
        void activity(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, bool(state.activity));
            if (!state.activity) return;
            const auto& a = *state.activity;
            word(bytes, a.escort); word(bytes, a.completed);
            for (float v : a.destination) word(bytes, std::bit_cast<uint32_t>(v));
            word(bytes, std::bit_cast<uint32_t>(a.destinationTimer));
            word(bytes, std::bit_cast<uint32_t>(a.followDistance));
            word(bytes, std::bit_cast<uint32_t>(a.walkSpeed));
            word(bytes, a.tick); word(bytes, a.steps); word(bytes, a.actorImage.size());
            bytes.insert(bytes.end(), a.actorImage.begin(), a.actorImage.end());
        }
        void activity(Cursor& input, PersonalScripts::Instance* state)
        {
            const auto present = input.word(); require(present <= 1);
            if (!present) return;
            PersonalScripts::Instance::Activity a;
            const auto escort = input.word(), complete = input.word(); require(escort <= 1 && complete <= 1);
            a.escort = escort; a.completed = complete;
            const auto real = [&] { const auto bits = input.word(); require(bits <= UINT32_MAX); return std::bit_cast<float>(uint32_t(bits)); };
            for (auto& v : a.destination) v = real();
            a.destinationTimer = real(); a.followDistance = real(); a.walkSpeed = real(); a.tick = input.word(); a.steps = input.word();
            const auto size = input.word(); require(size <= 65536);
            const auto image = input.take(size);
            require(a.steps <= a.tick && (!a.steps || size) && (!a.completed || a.steps));
            // Validate scalars on the preflight pass without allocating the physics image.
            const auto steps = a.steps; const auto completed = a.completed;
            a.steps = 0; a.completed = false; a.validate(); a.steps = steps; a.completed = completed;
            if (state) { a.actorImage.assign(image.begin(), image.end()); state->activity = std::move(a); }
        }
        void items(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            const auto writeBalances = [&](const std::vector<PersonalScripts::ItemBalance>& balances) {
                word(bytes, balances.size());
                for (const auto& entry : balances)
                {
                    ref(bytes, entry.item);
                    word(bytes, std::bit_cast<uint32_t>(entry.count));
                }
            };
            writeBalances(state.playerItems);
            writeBalances(state.npcItems);
            word(bytes, state.itemReceipts.size());
            for (const auto& receipt : state.itemReceipts)
            {
                word(bytes, receipt.scope);
                word(bytes, receipt.sourceKind);
                word(bytes, receipt.sourceId);
                word(bytes, receipt.step);
                word(bytes, receipt.playerTarget);
                word(bytes, receipt.addition);
                ref(bytes, receipt.item);
                word(bytes, std::bit_cast<uint32_t>(receipt.count));
            }
        }
        void items(Cursor& input, PersonalScripts::Instance* state)
        {
            const auto readBalances = [&](std::vector<PersonalScripts::ItemBalance>* target) {
                const auto count = input.word();
                require(count <= PersonalScripts::MaximumTrackedItemsPerStory);
                for (size_t i = 0; i < count; ++i)
                {
                    const auto id = ref(input);
                    const auto rawCount = input.word();
                    require(!id.empty() && rawCount <= uint64_t(PersonalScripts::MaximumItemTransferCount));
                    if (target) target->push_back({id, int(rawCount)});
                }
            };
            readBalances(state ? &state->playerItems : nullptr);
            readBalances(state ? &state->npcItems : nullptr);
            const auto receiptCount = input.word();
            require(receiptCount <= PersonalScripts::MaximumItemReceiptsPerStory);
            for (size_t i = 0; i < receiptCount; ++i)
            {
                PersonalScripts::ItemReceipt receipt;
                receipt.scope = input.word();
                receipt.sourceKind = input.word();
                receipt.sourceId = input.word();
                receipt.step = input.word();
                const auto playerTarget = input.word(), addition = input.word();
                require(playerTarget <= 1 && addition <= 1);
                receipt.playerTarget = playerTarget;
                receipt.addition = addition;
                receipt.item = ref(input);
                const auto rawCount = input.word();
                require(!receipt.item.empty() && rawCount >= 1
                    && rawCount <= uint64_t(PersonalScripts::MaximumItemTransferCount));
                receipt.count = int(rawCount);
                if (state) state->itemReceipts.push_back(std::move(receipt));
            }
        }
        void validateInventoryItem(const MWWorld::ESMStore& content, const ESM::RefId& item)
        {
            require(item.is<ESM::StringRefId>() && !item.empty()
                && item.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                && item.getRefIdString().find('\0') == std::string_view::npos);
            const auto record = MWWorld::inventoryItemRecord(content, item);
            require(record.mScript.empty());
            MWWorld::visitInventoryRecord(content, item, [](const auto& base) {
                require(std::isfinite(base.mData.mWeight) && base.mData.mWeight >= 0.f && base.mData.mValue >= 0);
            });
        }
        void seedPlayerItems(const MWWorld::ESMStore& content, std::vector<PersonalScripts::ItemBalance>& target,
            std::span<const PersonalScripts::ItemBalance> input)
        {
            require(input.size() <= PersonalScripts::MaximumTrackedItemsPerStory);
            std::vector<PersonalScripts::ItemBalance> seeded;
            seeded.reserve(input.size());
            for (size_t i = 0; i < input.size(); ++i)
            {
                const auto& entry = input[i];
                require(entry.count > 0 && entry.count <= PersonalScripts::MaximumItemTransferCount
                    && std::ranges::none_of(
                        std::span(input).first(i), [&](const auto& prev) { return prev.item == entry.item; }));
                validateInventoryItem(content, entry.item);
                seeded.push_back(entry);
            }
            target = std::move(seeded);
        }
        struct StoryItemTransaction
        {
            const MWWorld::ESMStore* content = nullptr;
            const MWScript::ReferenceResolver* resolver = nullptr;
            ESM::RefId sourceRecord;
            uint64_t character = 0, story = 0, sourceReference = 0, sourceKind = 0;
            MWWorld::Ptr sourceActor;
            std::vector<PersonalScripts::ItemBalance>* playerItems = nullptr;
            std::vector<PersonalScripts::ItemBalance>* npcItems = nullptr;
            std::vector<PersonalScripts::ItemReceipt>* receipts = nullptr;
            std::vector<PersonalScripts::Prepared::StagedItemTransfer>* transfers = nullptr;
            bool isPlayer(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const
            {
                require(content && resolver && playerItems && npcItems && receipts && transfers
                    && !ptr.isEmpty() && ptr.hasLiveReference());
                if (!sourceActor.isEmpty() && ptr == sourceActor)
                    return false;
                const auto player = resolver->resolve(execution, ESM::RefId::stringRefId("player"), false);
                require(!player.isEmpty() && player.hasLiveReference() && ptr == player);
                return true;
            }
            int baseNpcCount(const ESM::RefId& item) const
            {
                const auto* npc = content->get<ESM::NPC>().search(sourceRecord);
                require(npc != nullptr);
                int64_t total = 0;
                for (const auto& entry : npc->mInventory.mList)
                    if (entry.mItem == item)
                    {
                        require(entry.mCount > 0 && entry.mCount <= PersonalScripts::MaximumItemTransferCount);
                        total += entry.mCount;
                        require(total <= PersonalScripts::MaximumItemTransferCount);
                    }
                return int(total);
            }
            int itemCount(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item) const
            {
                validateInventoryItem(*content, item);
                if (isPlayer(execution, ptr))
                {
                    const auto found = std::ranges::find(*playerItems, item, &PersonalScripts::ItemBalance::item);
                    return found == playerItems->end() ? 0 : found->count;
                }
                const auto found = std::ranges::find(*npcItems, item, &PersonalScripts::ItemBalance::item);
                return found == npcItems->end() ? baseNpcCount(item) : found->count;
            }
            void addItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count)
            {
                validateInventoryItem(*content, item);
                require(count > 0 && count <= PersonalScripts::MaximumItemTransferCount
                    && receipts->size() < PersonalScripts::MaximumItemReceiptsPerStory);
                const bool playerTarget = isPlayer(execution, ptr);
                int64_t priorGrantNet = 0;
                bool priorStepGrant = false;
                for (const auto& r : *receipts)
                    if (r.sourceKind == sourceKind && r.sourceId == sourceReference && r.playerTarget == playerTarget
                        && r.item == item)
                    {
                        if (r.step < execution.invocation && r.addition) priorStepGrant = true;
                        priorGrantNet += r.addition ? r.count : -r.count;
                    }
                require(!priorStepGrant || priorGrantNet <= 0);
                auto& balances = playerTarget ? *playerItems : *npcItems;
                auto found = std::ranges::find(balances, item, &PersonalScripts::ItemBalance::item);
                if (found == balances.end())
                {
                    require(balances.size() < PersonalScripts::MaximumTrackedItemsPerStory);
                    balances.push_back({item, playerTarget ? 0 : baseNpcCount(item)});
                    found = std::prev(balances.end());
                }
                require(found->count <= PersonalScripts::MaximumItemTransferCount - count);
                found->count += count;
                const PersonalScripts::ItemReceipt receipt{execution.physicalScope, sourceKind, sourceReference,
                    execution.invocation, playerTarget, true, item, count};
                receipts->push_back(receipt);
                transfers->push_back({character, story, receipt});
            }
            void removeItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count)
            {
                validateInventoryItem(*content, item);
                require(count > 0 && count <= PersonalScripts::MaximumItemTransferCount
                    && receipts->size() < PersonalScripts::MaximumItemReceiptsPerStory);
                const bool playerTarget = isPlayer(execution, ptr);
                auto& balances = playerTarget ? *playerItems : *npcItems;
                auto found = std::ranges::find(balances, item, &PersonalScripts::ItemBalance::item);
                if (found == balances.end())
                {
                    require(!playerTarget && balances.size() < PersonalScripts::MaximumTrackedItemsPerStory);
                    balances.push_back({item, baseNpcCount(item)});
                    found = std::prev(balances.end());
                }
                require(found->count >= count);
                found->count -= count;
                if (playerTarget && found->count == 0)
                    balances.erase(found);
                const PersonalScripts::ItemReceipt receipt{execution.physicalScope, sourceKind, sourceReference,
                    execution.invocation, playerTarget, false, item, count};
                receipts->push_back(receipt);
                transfers->push_back({character, story, receipt});
            }
        };
    }

    PersonalScripts::PersonalScripts(uint64_t campaign, std::vector<Definition> definitions, CredentialCrypto& crypto,
        bool scheduled, bool banks, bool intents, bool returns, bool enables, bool relocations, bool sourceWaits,
        bool lifeEvents, bool fairOpportunities, bool activities, bool npcBanks)
        : mCampaign(campaign)
        , mDefinitions(std::move(definitions))
        , mScheduled(scheduled)
        , mBanks(banks)
        , mIntents(intents)
        , mReturns(returns)
        , mEnables(enables)
        , mRelocations(relocations)
        , mSourceWaits(sourceWaits)
        , mLifeEvents(lifeEvents)
        , mFairOpportunities(fairOpportunities)
        , mActivities(activities)
        , mNpcBanks(npcBanks)
    {
        require(campaign && !mDefinitions.empty() && mDefinitions.size() <= MaximumInstances);
        size_t scripts = 0;
        require(!mBanks || mScheduled);
        mDialogues = std::ranges::any_of(mDefinitions, [](const auto& entry) { return !entry.dialogues.empty(); });
        require(!mDialogues || mBanks);
        require(!mIntents || mDialogues);
        mEncounters = std::ranges::any_of(mDefinitions, [](const auto& entry) { return bool(entry.encounter); });
        require(!mEncounters || mIntents);
        require(!mReturns || mEncounters);
        require(!mEnables || mReturns);
        require(!mRelocations || (mEncounters && (!mEnables || mNpcBanks)));
        require(!mNpcBanks || (mRelocations && mSourceWaits && mLifeEvents && mFairOpportunities && mActivities && mReturns && mEnables));
        require(!mSourceWaits || (mEncounters && mBanks && (!mReturns || mNpcBanks) && (!mRelocations || mNpcBanks)));
        require(!mLifeEvents || mSourceWaits);
        require(!mFairOpportunities || mLifeEvents);
        require(!mActivities || (mRelocations && (!mSourceWaits || mNpcBanks) && (!mReturns || mNpcBanks)));
        mResults.resize(mDefinitions.size());
        for (size_t i = 0; i < mDefinitions.size(); ++i)
        {
            const auto& entry = mDefinitions[i];
            require(entry.character && entry.story && entry.script);
            require(!mSourceWaits || entry.encounter);
            require(entry.script->sourceRecord() == (mSourceWaits ? entry.encounter->record : ESM::RefId{}));
            require(!mEncounters || (entry.encounter == mDefinitions.front().encounter && entry.encounter && entry.encounter->reference
                && entry.encounter->record.is<ESM::StringRefId>() && !entry.encounter->record.empty()
                && entry.encounter->cell.is<ESM::StringRefId>() && !entry.encounter->cell.empty()
                && entry.encounter->record.getRefIdString().size() <= 128
                && entry.encounter->cell.getRefIdString().size() <= 128));
            if (mEnables)
                mRestorations.push_back(entry.script->compileResult("Enable", true, entry.encounter->record, true));
            require(entry.members.size() < MaximumScriptsPerBank
                && (mBanks || (entry.members.empty() && entry.running))
                && entry.script->bankCapable() == mBanks);
            scripts += 1 + entry.members.size(); require(scripts <= MaximumInstances);
            std::vector<ESM::RefId> names{entry.script->initial().runtime.script.mId};
            for (const auto& member : entry.members)
            {
                require(member.script && member.script->bankCapable());
                require(member.script->sourceRecord() == entry.script->sourceRecord());
                const auto id = member.script->initial().runtime.script.mId;
                require(std::ranges::find(names, id) == names.end()); names.push_back(id);
            }
            for (size_t j = 0; j < i; ++j)
                require(entry.character != mDefinitions[j].character || entry.story != mDefinitions[j].story);
            require(!mDialogues || (!entry.dialogues.empty() && entry.dialogues.size() <= MWScript::DialogueState::MaximumRecords));
            size_t infos = 0;
            for (size_t d = 0; d < entry.dialogues.size(); ++d)
            {
                const auto& dialogue = entry.dialogues[d];
                require(dialogue.mId.is<ESM::StringRefId>() && !dialogue.mId.empty()
                    && dialogue.mId.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                    && dialogue.mId.getRefIdString().find('\0') == std::string_view::npos
                    && (dialogue.mType == ESM::Dialogue::Topic || dialogue.mType == ESM::Dialogue::Journal));
                for (size_t j = 0; j < d; ++j) require(entry.dialogues[j].mId != dialogue.mId);
                infos += dialogue.mInfo.size(); require(infos <= MWScript::DialogueState::MaximumInfos);
                std::vector<ESM::RefId> identities;
                for (const auto& info : dialogue.mInfo)
                {
                    require(!info.mId.empty() && info.mId.is<ESM::StringRefId>()
                        && info.mId.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                        && info.mId.getRefIdString().find('\0') == std::string_view::npos
                        && info.mQuestStatus >= ESM::DialInfo::QS_None && info.mQuestStatus <= ESM::DialInfo::QS_Restart
                        && (!mIntents || dialogue.mType != ESM::Dialogue::Topic
                            || (info.mData.mDisposition >= 0 && info.mData.mDisposition <= 100))
                        && info.mResponse.size() <= 4096
                        && info.mResponse.find('\0') == std::string::npos
                        && info.mResultScript.size() <= MWScript::WorldScript::MaximumSourceBytes
                        && info.mSelects.size() <= 16 && std::ranges::find(identities, info.mId) == identities.end());
                    for (const auto& name : {info.mActor, info.mRace, info.mClass, info.mFaction, info.mPcFaction, info.mCell})
                        require(name.empty() || (name.is<ESM::StringRefId>()
                            && name.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                            && name.getRefIdString().find('\0') == std::string_view::npos));
                    for (const auto& select : info.mSelects)
                    {
                        require(select.mVariable.size() <= MWScript::WorldScript::MaximumNameBytes
                            && select.mVariable.find('\0') == std::string::npos);
                        if (const auto* value = std::get_if<float>(&select.mValue)) require(std::isfinite(*value));
                    }
                    identities.push_back(info.mId);
                    if (dialogue.mType == ESM::Dialogue::Topic)
                        mResults[i].push_back({dialogue.mId, info.mId, entry.script->compileResult(info.mResultScript,
                            mEncounters && (!mLifeEvents || mNpcBanks),
                            mEncounters && (!mLifeEvents || mNpcBanks) ? entry.encounter->record : ESM::RefId{},
                            false, mRelocations && (!mActivities || mNpcBanks), mActivities)});
                }
            }
        }
        // Reserve two authenticated dialogue calls, including encounter discovery/retry.
        require(!mIntents || scripts <= MaximumInstances - (mEncounters ? 4 : 2));
        // Includes every script's content/source/defaults/limits digest and the
        // complete ordered ownership policy. No session identities or pointers.
        auto binding = encode(initial());
        if (mEncounters) for (const auto& entry : mDefinitions)
        {
            word(binding, entry.encounter->reference); ref(binding, entry.encounter->record); ref(binding, entry.encounter->cell);
        }
        if (mEncounters)
        {
            constexpr std::string_view limits = "terminal-source-disable-1;one-npc;one-cell;static-geometry;12-banks;4-calls;no-summons";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mEnables)
        {
            constexpr std::string_view limits = "safe-return-source-enable-1;frozen-story;living-origin-life;16-instructions";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mRelocations)
        {
            constexpr std::string_view limits = "terminal-source-position-1;inert;one-interior;4096-displacement;1000000-coordinates;frozen-scope";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mSourceWaits)
        {
            constexpr std::string_view limits = "durable-source-wait-1;one-dependency-per-story;foreign-death;newer-living-life;30hz-clock;rollback-bank;no-death-credit";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mRelocations && mReturns)
        {
            constexpr std::string_view limits = "safe-relocation-return-1;origin-and-destination-hulls;living-origin-life;retain-dormant-pose";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mLifeEvents)
        {
            constexpr std::string_view limits = "attributed-life-cursor-1;256;prequest;causal-owner;no-helper-credit;commit-only;no-terminal-scenes";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mFairOpportunities)
        {
            constexpr std::string_view limits = "next-source-opportunity-1;oldest-wait-character-story;retain-until-credit;one-canonical-life";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mNpcBanks)
        {
            constexpr std::string_view limits = "npc-bank-continuation-1;private-pose-and-activity;timer-health-life-cursor;public-scope-untouched";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mDialogues)
        {
            constexpr std::string_view limits = "scoped-dialogue-results-1;16;64;16;8192;4096;64;128;1048576";
            binding.insert(binding.end(), limits.begin(), limits.end());
            if (mIntents)
            {
                constexpr std::string_view intentLimits
                    = "authenticated-dialogue-5;14;2;8;256;4096;32768;64;source-npc-disposition-64;moddisposition;journal-text-16384;game-calendar;root-first;bounded-substitutions;stock-disposition;unscripted-"
                      "speaker;primary-bank";
                binding.insert(binding.end(), intentLimits.begin(), intentLimits.end());
            }
            std::ostringstream output(std::ios::binary); ESM::ESMWriter writer;
            writer.setVersion(ESM::VER_130); writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion); writer.save(output);
            for (const auto& entry : mDefinitions)
                for (const auto& dialogue : entry.dialogues)
                {
                    writer.startRecord("DIAL"); dialogue.save(writer); writer.endRecord("DIAL");
                    for (const auto& info : dialogue.mInfo)
                    { writer.startRecord("INFO"); info.save(writer); writer.endRecord("INFO"); }
                }
            writer.close(); const auto policy = output.str(); binding.insert(binding.end(), policy.begin(), policy.end());
        }
        require(crypto.sha256(std::as_bytes(std::span(binding)), mIdentity));
    }

    PersonalScripts::State PersonalScripts::initial() const
    {
        State state;
        for (const auto& entry : mDefinitions)
        {
            state.push_back({{MWScript::ExecutionContext::Ownership::Character, mCampaign, entry.story, 1,
                entry.character, 0, 0, 0, 0}, 0, entry.script->initial()});
            auto& instance = state.back(); instance.runtime.runtime.script.mRunning = entry.running;
            if (mIntents)
                for (const auto& dialogue : entry.dialogues)
                    if (dialogue.mType == ESM::Dialogue::Topic)
                    {
                        instance.dialogue.topics.push_back(dialogue.mId);
                        break;
                    }
            for (const auto& member : entry.members)
            {
                auto value = member.script->initial(); value.runtime.script.mRunning = member.running;
                value.runtime.globals.clear(); instance.members.push_back(std::move(value));
            }
        }
        return state;
    }

    ReliablePersonalDialogue PersonalScripts::view(const Instance& state, SessionId target,
        SessionGeneration generation, ServerTick tick, CanonicalRevision revision)
    {
        ReliablePersonalDialogue value{target, generation, tick, revision};
        value.story = state.last.story; value.revision = state.dialogueRevision;
        value.command = state.dialogueCommand; value.speaker = state.dialogueRequest.speaker;
        value.life = state.dialogueRequest.life; value.topic = state.dialogueRequest.topic;
        for (const auto& c : state.choices) value.choices.push_back({c.text, c.id});
        for (const auto& q : state.dialogue.quests)
            value.quests.push_back({q.mTopic.toString(), q.mState, bool(q.mFinished)});
        for (const auto& e : state.dialogue.entries)
            value.entries.push_back({e.topic.toString(), e.info.toString(), e.text,
                e.stamp.day, e.stamp.month, e.stamp.dayOfMonth});
        for (const auto& t : state.dialogue.topics) value.topics.push_back(t.toString());
        if (state.sourceWait || state.reservedLife)
        {
            const auto& context = state.sourceWait ? state.sourceWait->invocation : state.last;
            value.encounter = DialogueEncounterStatus{context.sourceReference, context.sourceLife,
                state.sourceWait ? state.sourceWait->sinceTick : 0,
                state.sourceWait ? state.sourceWait->respawnTick : 0, state.reservedLife};
        }
        return value;
    }

    void PersonalScripts::validate(const Instance& state, size_t index) const
    {
        const auto& expected = mDefinitions.at(index);
        const auto& last = state.last;
        require(!mLifeEvents || mNpcBanks || !state.encounterLife); // Mixing terminal scenes and life opportunities requires npcBanks.
        require(!mSourceWaits || !last.sourceReference
            || (last.sourceReference == expected.encounter->reference && last.sourceLife));
        require(mSourceWaits ? state.clockTicks <= state.scheduledTick : !state.clockTicks && !state.sourceWait);
        require(mLifeEvents ? state.deathCursor <= MaximumLifeEvents && state.deathCursor <= last.sourceLife
            : !state.deathCursor);
        require(mFairOpportunities ? (bool(state.reservedLife) == bool(state.reservationTick)
                && (!state.reservedLife || (!state.encounterLife && state.reservedLife > state.deathCursor
                    && state.reservedLife <= MaximumLifeEvents && state.reservationTick <= state.scheduledTick)))
            : !state.reservedLife && !state.reservationTick);
        if (state.sourceWait)
        {
            const auto& wait = *state.sourceWait;
            auto expectedInvocation = last;
            require(last.invocation != UINT64_MAX);
            ++expectedInvocation.invocation; expectedInvocation.causalEvent = 0;
            expectedInvocation.sourceReference = expected.encounter->reference;
            expectedInvocation.sourceLife = wait.invocation.sourceLife;
            require(mSourceWaits && !state.encounterLife && wait.invocation == expectedInvocation
                && wait.invocation.sourceLife && wait.reason == 1 && wait.sinceTick
                && wait.sinceTick <= state.scheduledTick && wait.instruction >= 0
                && wait.instruction < MWScript::WorldScript::MaximumInstructions
                && wait.respawnTick
                && ((wait.script == state.runtime.runtime.script.mId && state.runtime.runtime.script.mRunning)
                    || std::ranges::any_of(state.members, [&](const auto& member) {
                        return wait.script == member.runtime.script.mId && member.runtime.script.mRunning;
                    })));
        }
        require(!state.encounterLife || (mEncounters && last.sourceReference == expected.encounter->reference
            && last.sourceLife == state.encounterLife && state.choices.empty()));
        require(!state.encounterReturned || (mReturns && state.encounterLife));
        require(!state.encounterEnabled || (mEnables && state.encounterLife && state.encounterReturned && !state.relocation));
        require(!mEnables || state.encounterEnabled == (state.encounterReturned && !state.relocation));
        require(!state.relocation || (mRelocations && state.encounterLife && !state.encounterEnabled));
        if (state.relocation) state.relocation->validate();
        require(!state.activity || (mActivities && state.relocation && state.encounterLife
            && (!state.encounterReturned || (mNpcBanks && state.activity->completed))));
        require(!mActivities || mNpcBanks || bool(state.activity) == bool(state.encounterLife));
        if (state.activity)
        {
            state.activity->validate();
            Instance::Relocation target{state.relocation->origin, state.activity->destination, 0}; target.validate();
        }
        if (state.encounterLife)
            require(std::ranges::any_of(state.npcs, [&](const auto& npc) {
                return npc.reference == expected.encounter->reference && npc.life == state.encounterLife
                    && npc.record == expected.encounter->record;
            }));
        require(last.ownership == MWScript::ExecutionContext::Ownership::Character && last.campaign == mCampaign
            && last.character == expected.character && last.story == expected.story && last.physicalScope == (state.encounterLife ? index + 2 : 1)
            && (mDialogues || (!last.sourceReference && !last.sourceLife))
            && (mBanks ? last.invocation >= state.runtime.tick : last.invocation == state.runtime.tick)
            && last.causalEvent <= state.eventCursor
            && (!last.causalEvent || last.causalEvent == state.eventCursor)
            && (last.invocation || (!last.causalEvent && !state.eventCursor && (!state.scheduledTick || state.sourceWait)))
            && (mScheduled || !state.scheduledTick));
        if (mIntents)
        {
            require(bool(state.dialogueRevision) == bool(state.dialogueCommand) && state.choices.size() <= 8
                && state.talkedSpeakers.size() <= 64);
            if (state.dialogueRevision)
            {
                const auto& request = state.dialogueRequest;
                require(validDialogueIntent(request) && request.story == expected.story
                    && request.revision + 1 == state.dialogueRevision);
                auto execution = last;
                if (!execution.invocation)
                    execution.invocation = 1;
                auto copy = state.dialogue;
                MWScript::DialogueState dialogue(execution, copy, expected.dialogues, mIntents);
                (void)dialogue.record(ESM::RefId::stringRefId(request.topic), ESM::Dialogue::Topic);
                require(std::ranges::find(state.talkedSpeakers, request.speaker) != state.talkedSpeakers.end());
            }
            else
                require(
                    state.dialogueRequest == DialogueIntent{} && state.choices.empty() && state.talkedSpeakers.empty());
            for (size_t i = 0; i < state.choices.size(); ++i)
            {
                const auto& choice = state.choices[i];
                require(choice.id >= 0 && !choice.text.empty() && choice.text.size() <= 256
                    && choice.text.find('\0') == choice.text.npos
                    && std::ranges::none_of(
                        std::span(state.choices).first(i), [&](const auto& c) { return c.id == choice.id; }));
            }
            for (size_t i = 0; i < state.talkedSpeakers.size(); ++i)
                require(state.talkedSpeakers[i]
                    && std::find(
                           state.talkedSpeakers.begin(), state.talkedSpeakers.begin() + i, state.talkedSpeakers[i])
                        == state.talkedSpeakers.begin() + i);
            require(state.npcs.size() <= MaximumNpcsPerStory);
            for (size_t i = 0; i < state.npcs.size(); ++i)
            {
                const auto& npc = state.npcs[i];
                require(bool(npc.reference) == bool(npc.life) && npc.record.is<ESM::StringRefId>() && !npc.record.empty()
                    && npc.record.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                    && npc.record.getRefIdString().find('\0') == std::string_view::npos
                    && (!npc.reference || (state.encounterLife && npc.reference == expected.encounter->reference)
                        || std::ranges::find(state.talkedSpeakers, npc.reference) != state.talkedSpeakers.end())
                    && std::ranges::none_of(std::span(state.npcs).first(i), [&](const auto& previous) {
                        return (npc.reference && previous.reference == npc.reference)
                            || ((!npc.reference || !previous.reference) && previous.record == npc.record);
                    }));
            }
        }
        else
            require(!state.dialogueRevision && !state.dialogueCommand && state.dialogueRequest == DialogueIntent{}
                && state.choices.empty() && state.talkedSpeakers.empty() && state.npcs.empty());
        require(state.members.size() == expected.members.size());
        for (const auto& member : state.members)
            require(member.tick <= last.invocation && member.runtime.globals.empty());
        if (last.invocation) last.validate();
        if (mDialogues)
        {
            auto execution = last; if (!execution.invocation) execution.invocation = 1;
            auto progress = state.dialogue;
            MWScript::DialogueState dialogue(execution, progress, expected.dialogues, mIntents);
            require((mSourceWaits || state.lastTopic.empty() == !last.sourceReference)
                && state.lastTopic.empty() == state.lastInfo.empty());
            if (!state.lastTopic.empty())
            {
                const auto& topic = dialogue.record(state.lastTopic, ESM::Dialogue::Topic);
                require(last.causalEvent && state.lastChoice >= -1
                    && std::ranges::any_of(topic.mInfo, [&](const auto& info) { return info.mId == state.lastInfo; }));
            }
            else require(state.lastChoice == -1);
        }
        else require(state.dialogue.quests.empty() && state.dialogue.entries.empty() && state.dialogue.topics.empty()
            && state.lastTopic.empty() && state.lastInfo.empty() && state.lastChoice == -1);
        if (mIntents)
            require(!encodeReliablePersonalDialogue(view(state, *SessionId::fromValue(1), SessionGeneration::initial(),
                ServerTick::initial(), CanonicalRevision::initial())).empty());
        if (mNpcBanks)
        {
            const auto validateBalances = [&](const std::vector<ItemBalance>& balances, bool nonZero) {
                require(balances.size() <= MaximumTrackedItemsPerStory);
                for (size_t i = 0; i < balances.size(); ++i)
                {
                    const auto& entry = balances[i];
                    require(entry.item.is<ESM::StringRefId>() && !entry.item.empty()
                        && entry.item.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                        && entry.item.getRefIdString().find('\0') == std::string_view::npos
                        && entry.count >= (nonZero ? 1 : 0) && entry.count <= MaximumItemTransferCount
                        && std::ranges::none_of(
                            std::span(balances).first(i), [&](const auto& prev) { return prev.item == entry.item; }));
                }
            };
            validateBalances(state.playerItems, true);
            validateBalances(state.npcItems, false);
            require(state.itemReceipts.size() <= MaximumItemReceiptsPerStory);
            uint64_t previousStep = 0;
            for (size_t i = 0; i < state.itemReceipts.size(); ++i)
            {
                const auto& r = state.itemReceipts[i];
                require((r.scope == 1 || r.scope == index + 2) && (r.sourceKind == 1 || r.sourceKind == 2)
                    && expected.encounter && r.sourceId == expected.encounter->reference && r.step >= 1
                    && r.step <= last.invocation && r.step >= previousStep && r.item.is<ESM::StringRefId>()
                    && !r.item.empty() && r.item.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                    && r.item.getRefIdString().find('\0') == std::string_view::npos && r.count >= 1
                    && r.count <= MaximumItemTransferCount);
                if (r.addition)
                {
                    int64_t priorNet = 0;
                    bool priorGrant = false;
                    for (size_t j = 0; j < i; ++j)
                    {
                        const auto& prev = state.itemReceipts[j];
                        if (prev.sourceKind == r.sourceKind && prev.sourceId == r.sourceId
                            && prev.playerTarget == r.playerTarget && prev.item == r.item)
                        {
                            if (prev.step < r.step && prev.addition) priorGrant = true;
                            priorNet += prev.addition ? prev.count : -prev.count;
                        }
                    }
                    require(!priorGrant || priorNet <= 0);
                }
                previousStep = r.step;
            }
        }
        else
            require(state.playerItems.empty() && state.npcItems.empty() && state.itemReceipts.empty());
    }

    void PersonalScripts::Instance::Activity::validate() const
    {
        require(std::ranges::all_of(destination, [](float v) { return std::isfinite(v) && std::abs(v) <= 1e6f; })
            && std::isfinite(destinationTimer) && destinationTimer >= -1.f/30 && destinationTimer <= 2
            && std::isfinite(followDistance) && followDistance >= 250 && followDistance <= 4096
            && std::isfinite(walkSpeed) && walkSpeed >= 0 && walkSpeed <= 4096
            && actorImage.size() <= 65536 && steps <= tick && (!steps || !actorImage.empty())
            && (!completed || steps));
    }

    std::vector<char> PersonalScripts::encode(const State& state) const
    {
        require(state.size() == mDefinitions.size());
        for (size_t i = 0; i < state.size(); ++i) if (state[i].encounterLife)
            require(std::ranges::none_of(std::span(state).first(i), [&](const auto& previous) {
                return previous.encounterLife && previous.last.character == state[i].last.character;
            }));
        std::vector<char> bytes;
        word(bytes,
            mNpcBanks ? NpcBankMagic : mActivities ? ActivityMagic : mFairOpportunities ? OpportunityMagic : mLifeEvents ? LifeEventMagic : mSourceWaits ? SourceWaitMagic
                : mRelocations ? (mReturns ? EncounterRelocationReturnMagic : EncounterRelocationMagic)
                : mEnables   ? EncounterEnableMagic
                : mReturns   ? EncounterReturnMagic
                : mEncounters ? EncounterMagic
                : mIntents   ? IntentMagic
                : mDialogues ? DialogueMagic
                : mBanks     ? BankMagic
                : mScheduled ? ScheduledMagic
                             : Magic);
        for (auto value : mIdentity.bytes) bytes.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
        word(bytes, state.size());
        for (size_t i = 0; i < state.size(); ++i)
        {
            validate(state[i], i);
            const auto image = mDefinitions[i].script->encode(state[i].runtime);
            context(bytes, state[i].last); word(bytes, state[i].eventCursor); word(bytes, image.size());
            if (mScheduled) word(bytes, state[i].scheduledTick);
            bytes.insert(bytes.end(), image.begin(), image.end());
            if (mBanks)
            {
                word(bytes, state[i].members.size());
                for (size_t j = 0; j < state[i].members.size(); ++j)
                {
                    auto member = state[i].members[j]; member.runtime.globals = state[i].runtime.runtime.globals;
                    const auto encoded = mDefinitions[i].members[j].script->encode(member);
                    word(bytes, encoded.size()); bytes.insert(bytes.end(), encoded.begin(), encoded.end());
                }
            }
            if (mDialogues) progression(bytes, state[i], mIntents);
            if (mSourceWaits) sourceWait(bytes, state[i]);
            if (mLifeEvents) word(bytes, state[i].deathCursor);
            if (mFairOpportunities) { word(bytes, state[i].reservedLife); word(bytes, state[i].reservationTick); }
            if (mIntents)
            {
                conversation(bytes, state[i]);
                npcs(bytes, state[i]);
                if (mEncounters) word(bytes, state[i].encounterLife);
                if (mReturns) word(bytes, state[i].encounterReturned);
                if (mEnables) word(bytes, state[i].encounterEnabled);
                if (mRelocations)
                {
                    word(bytes, bool(state[i].relocation));
                    if (state[i].relocation)
                    {
                        for (float value : state[i].relocation->origin) word(bytes, std::bit_cast<uint32_t>(value));
                        for (float value : state[i].relocation->position) word(bytes, std::bit_cast<uint32_t>(value));
                        word(bytes, std::bit_cast<uint32_t>(state[i].relocation->yaw));
                    }
                }
                if (mActivities) activity(bytes, state[i]);
                if (mNpcBanks) items(bytes, state[i]);
            }
        }
        require(bytes.size() <= MaximumImageBytes);
        return bytes;
    }

    PersonalScripts::State PersonalScripts::restore(std::span<const char> bytes) const
    {
        require(bytes.size() <= MaximumImageBytes);
        Cursor input{bytes};
        require(input.word()
            == (mNpcBanks ? NpcBankMagic : mActivities ? ActivityMagic : mFairOpportunities ? OpportunityMagic : mLifeEvents ? LifeEventMagic : mSourceWaits ? SourceWaitMagic
                : mRelocations ? (mReturns ? EncounterRelocationReturnMagic : EncounterRelocationMagic)
                : mEnables      ? EncounterEnableMagic
                : mReturns      ? EncounterReturnMagic
                : mEncounters    ? EncounterMagic
                : mIntents   ? IntentMagic
                    : mDialogues ? DialogueMagic
                    : mBanks     ? BankMagic
                    : mScheduled ? ScheduledMagic
                                 : Magic));
        const auto identity = input.take(32);
        require(std::equal(identity.begin(), identity.end(), reinterpret_cast<const char*>(mIdentity.bytes.data())));
        const auto count = input.word(); require(count == mDefinitions.size());
        // Complete the outer structural pass before allocating any state.
        Cursor preflight = input;
        for (size_t i = 0; i < count; ++i)
        {
            preflight.take(72);
            const auto size = preflight.word(); require(size && size <= SharedWorldScript::MaximumImageBytes);
            if (mScheduled) preflight.take(8);
            preflight.take(size);
            if (mBanks)
            {
                require(preflight.word() == mDefinitions[i].members.size());
                for (size_t j = 0; j < mDefinitions[i].members.size(); ++j)
                {
                    const auto memberSize = preflight.word();
                    require(memberSize && memberSize <= SharedWorldScript::MaximumImageBytes); preflight.take(memberSize);
                }
            }
            if (mDialogues) progression(preflight, nullptr, mIntents);
            if (mSourceWaits) sourceWait(preflight, nullptr);
            if (mLifeEvents) require(preflight.word() <= MaximumLifeEvents);
            if (mFairOpportunities) { require(preflight.word() <= MaximumLifeEvents); preflight.take(8); }
            if (mIntents)
            {
                conversation(preflight, nullptr);
                npcs(preflight, nullptr);
                if (mEncounters) preflight.take(8);
                if (mReturns) require(preflight.word() <= 1);
                if (mEnables) require(preflight.word() <= 1);
                if (mRelocations)
                {
                    const auto present = preflight.word(); require(present <= 1);
                    if (present)
                    {
                        Instance::Relocation pose;
                        const auto real = [&] {
                            const auto bits = preflight.word(); require(bits <= UINT32_MAX);
                            return std::bit_cast<float>(uint32_t(bits));
                        };
                        for (auto& value : pose.origin) value = real();
                        for (auto& value : pose.position) value = real();
                        pose.yaw = real(); pose.validate();
                    }
                }
                if (mActivities) activity(preflight, nullptr);
                if (mNpcBanks) items(preflight, nullptr);
            }
        }
        require(preflight.bytes.empty());
        State state;
        state.reserve(size_t(count));
        for (size_t i = 0; i < count; ++i)
        {
            auto last = context(input); const auto event = input.word(), size = input.word();
            const auto scheduledTick = mScheduled ? input.word() : 0;
            Instance entry{last, event, mDefinitions[i].script->restore(input.take(size)), scheduledTick};
            if (mBanks)
            {
                require(input.word() == mDefinitions[i].members.size());
                for (const auto& definition : mDefinitions[i].members)
                {
                    const auto memberSize = input.word();
                    auto member = definition.script->restore(input.take(memberSize));
                    member.runtime.globals.clear(); entry.members.push_back(std::move(member));
                }
            }
            if (mDialogues) progression(input, &entry, mIntents);
            if (mSourceWaits) sourceWait(input, &entry);
            if (mLifeEvents) entry.deathCursor = input.word();
            if (mFairOpportunities) { entry.reservedLife = input.word(); entry.reservationTick = input.word(); }
            if (mIntents)
            {
                conversation(input, &entry);
                npcs(input, &entry);
                if (mEncounters) entry.encounterLife = input.word();
                if (mReturns) { const auto returned = input.word(); require(returned <= 1); entry.encounterReturned = returned; }
                if (mEnables) { const auto enabled = input.word(); require(enabled <= 1); entry.encounterEnabled = enabled; }
                if (mRelocations && input.word())
                {
                    entry.relocation.emplace();
                    for (auto& value : entry.relocation->origin) value = std::bit_cast<float>(uint32_t(input.word()));
                    for (auto& value : entry.relocation->position) value = std::bit_cast<float>(uint32_t(input.word()));
                    entry.relocation->yaw = std::bit_cast<float>(uint32_t(input.word()));
                }
                if (mActivities) activity(input, &entry);
                if (mNpcBanks) items(input, &entry);
            }
            validate(entry, i); state.push_back(std::move(entry));
        }
        require(encode(state) == std::vector<char>(bytes.begin(), bytes.end()));
        return state;
    }

    PersonalScripts::Prepared PersonalScripts::prepare(const State& before, const MWScript::ExecutionContext& execution,
        const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver, size_t budget,
        MWScript::ReferenceState* referenceState) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = execution.invocation;
        try
        {
            execution.validate();
            require(before.size() == mDefinitions.size());
            const auto found = std::ranges::find_if(mDefinitions, [&](const auto& entry) {
                return entry.character == execution.character && entry.story == execution.story;
            });
            require(found != mDefinitions.end());
            const auto index = size_t(found - mDefinitions.begin());
            const auto& current = before[index]; validate(current, index);
            require(execution.ownership == current.last.ownership && execution.campaign == mCampaign
                && execution.physicalScope == (mNpcBanks && current.encounterLife && current.relocation && !current.encounterReturned ? index + 2 : 1)
                && (mSourceWaits ? execution.sourceReference == found->encounter->reference && execution.sourceLife
                    && (!current.encounterLife || execution.sourceLife == current.encounterLife)
                    && !execution.causalEvent : !execution.sourceReference && !execution.sourceLife));
            result.script = current.runtime.runtime.script.mId;
            if (execution == current.last)
            { result.outcome = SharedWorldScript::Outcome::Committed; return prepared; }
            require(budget && budget <= MaximumInvocationsPerTransaction);
            require(current.last.invocation != std::numeric_limits<uint64_t>::max()
                && execution.invocation == current.last.invocation + 1
                && (!execution.causalEvent || execution.causalEvent > current.eventCursor));
            if (mBanks) return prepareBank(before, index, execution, content, resolver, budget, referenceState);
            auto invocation = found->script->prepare(current.runtime, execution, content, resolver);
            result = std::move(invocation.result);
            if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed) return prepared;
            prepared.state = before;
            prepared.state[index] = {execution, std::max(current.eventCursor, execution.causalEvent),
                std::move(invocation.state), current.scheduledTick};
            prepared.image = encode(prepared.state);
            prepared.invocations = 1;
        }
        catch (const std::exception& error)
        {
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareBank(const State& before, size_t index,
        const MWScript::ExecutionContext& execution, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget, MWScript::ReferenceState* referenceState) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = execution.invocation;
        try
        {
            (void)encode(before);
            prepared.state = before;
            auto& state = prepared.state[index];
            const auto& definition = mDefinitions[index];
            std::optional<MWScript::DialogueState> dialogue;
            if (mDialogues) dialogue.emplace(execution, state.dialogue, definition.dialogues, mIntents);
            Bank bank(execution, definition, state);
            const auto& scripts = bank.scripts;
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                auto& descriptor = bank.instances[i];
                if (!descriptor.mRunning) continue;
                require(prepared.invocations < budget);
                auto& member = i ? state.members[i - 1] : state.runtime;
                auto input = member;
                input.runtime.globals = state.runtime.runtime.globals;
                input.runtime.script.mRunning = descriptor.mRunning;
                input.runtime.script.mLocals.mVariables.clear();
                require(descriptor.mLocals.write(input.runtime.script.mLocals, scripts[i]->declarations()));
                ++prepared.invocations;
                auto invocation = scripts[i]->prepareBank(input, execution, content, resolver, bank, descriptor,
                    referenceState, dialogue ? &*dialogue : nullptr);
                result = std::move(invocation.result); result.presentation.clear();
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                { prepared.state.clear(); if (!result.sourceWait) prepared.invocations = 0; result.executed = false; return prepared; }
                state.runtime.runtime.globals = std::move(invocation.state.runtime.globals);
                member.tick = invocation.state.tick;
            }
            bank.write(state);
            state.sourceWait.reset();
            state.lastTopic = {}; state.lastInfo = {}; state.lastChoice = -1;
            state.last = execution; state.eventCursor = std::max(state.eventCursor, execution.causalEvent);
            prepared.image = encode(prepared.state);
            prepared.invocations = std::max(size_t(1), prepared.invocations);
            result.outcome = SharedWorldScript::Outcome::Committed;
            result.executed = true; // The bank occurrence/marker commits even when all members are stopped.
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    void PersonalScripts::Instance::Relocation::validate() const
    {
        double distance = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            require(std::isfinite(origin[i]) && std::isfinite(position[i])
                && std::abs(origin[i]) <= 1000000 && std::abs(position[i]) <= 1000000);
            const double delta = double(position[i]) - origin[i]; distance += delta * delta;
        }
        require(distance <= 4096. * 4096 && std::isfinite(yaw) && std::abs(yaw) <= 6.283186f);
    }

    const PersonalScripts::Instance::Relocation* PersonalScripts::relocation(const State& state, uint64_t character)
    {
        for (const auto& instance : state)
            if (instance.last.character == character && instance.relocation && !instance.encounterReturned)
                return &*instance.relocation;
        return nullptr;
    }

    uint64_t PersonalScripts::physicalScope(const State& state, uint64_t character)
    {
        for (const auto& instance : state)
            if (instance.last.character == character && instance.encounterLife && !instance.encounterReturned)
                return instance.last.physicalScope;
        return 1;
    }

    PersonalScripts::Prepared PersonalScripts::prepareReturn(const State& before, uint64_t character,
        const MWWorld::ESMStore* content, uint64_t sourceLife) const
    {
        Prepared prepared;
        require(mReturns && character);
        (void)encode(before);
        const auto found = std::ranges::find_if(before, [&](const auto& instance) {
            return instance.last.character == character && instance.encounterLife;
        });
        require(found != before.end());
        prepared.result.outcome = SharedWorldScript::Outcome::Committed;
        if (found->encounterReturned) return prepared;
        const auto index = size_t(found - before.begin());
        require(!found->activity || (mNpcBanks && found->activity->completed));
        if (found->relocation) require(sourceLife == found->encounterLife);
        const bool enable = mEnables && !found->relocation;
        if (enable)
        {
            require(content && sourceLife == found->encounterLife);
            const auto& binding = *mDefinitions[index].encounter;
            const auto* npc = content->get<ESM::NPC>().find(binding.record);
            require(npc->mScript.empty());
            MWWorld::ManualRef restored(*content, binding.record);
            restored.getPtr().getRefData().disable();
            struct Restoration final : MWScript::ReferenceState, MWScript::ReferenceResolver
            {
                MWScript::ExecutionContext expected;
                MWWorld::Ptr source;
                void enable(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    require(execution == expected && ptr.hasLiveReference() && ptr == source
                        && ptr.getType() == ESM::NPC::sRecordId);
                    source.getRefData().enable();
                }
                void disable(const MWScript::ExecutionContext&, const MWWorld::Ptr&) override
                { throw std::invalid_argument("Safe return admits only source Enable"); }
                bool disabled(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                { require(execution == expected && ptr == source); return !source.getRefData().isEnabled(); }
                MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id, bool activeOnly) const override
                {
                    require(execution == expected && (id.empty() || id == source.getCellRef().getRefId()));
                    return activeOnly && !source.getRefData().isEnabled() ? MWWorld::Ptr{} : source;
                }
            } scene;
            scene.expected = found->last; scene.source = restored.getPtr();
            MWScript::InterpreterContext context(nullptr, scene.expected, *content, scene,
                nullptr, nullptr, nullptr, nullptr, &scene);
            Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
            MWScript::Misc::installEnableOpcodes(interpreter);
            interpreter.run(mRestorations[index], context, 16, &prepared.result.trace);
            require(restored.getPtr().getRefData().isEnabled());
            prepared.invocations = 1;
        }
        prepared.state = before;
        prepared.state[index].encounterReturned = true;
        prepared.state[index].encounterEnabled = enable;
        prepared.image = encode(prepared.state);
        prepared.result.executed = true;
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareDialogue(const State& before,
        const MWScript::ExecutionContext& execution, const ESM::RefId& topic, int choice, int disposition,
        bool talkedToPlayer, std::string_view cellName, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget, const MWMechanics::NpcStats* playerStats,
        std::optional<std::string_view> customClassName, const MWMechanics::NpcStats* speakerStats,
        std::optional<MWScript::DialogueState::Stamp> journalStamp, bool encounterPrepared,
        std::optional<std::span<const ItemBalance>> playerInventory) const
    {
        Prepared prepared; auto& result = prepared.result; result.invocationTick = execution.invocation;
        try
        {
            execution.validate(); (void)encode(before);
            require(mDialogues && execution.campaign == mCampaign && execution.physicalScope == 1
                && physicalScope(before, execution.character) == 1
                && execution.ownership == MWScript::ExecutionContext::Ownership::Character
                && execution.sourceReference && execution.sourceLife && execution.causalEvent
                && choice >= -1 && disposition >= 0 && disposition <= 100 && cellName.size() <= 256);
            const auto found = std::ranges::find_if(mDefinitions, [&](const auto& entry) {
                return entry.character == execution.character && entry.story == execution.story; });
            require(found != mDefinitions.end());
            const auto index = size_t(found - mDefinitions.begin()); const auto& current = before[index];
            require(!current.encounterLife); // A dormant removal never reopens this story's source.
            if (current.sourceWait) throw MWScript::ScriptContextUnavailable();
            result.script = current.runtime.runtime.script.mId;
            if (execution == current.last)
            {
                require(topic == current.lastTopic && choice == current.lastChoice);
                result.outcome = SharedWorldScript::Outcome::Committed; return prepared;
            }
            require(budget && budget <= MaximumInvocationsPerTransaction
                && current.last.invocation != UINT64_MAX && execution.invocation == current.last.invocation + 1
                && execution.causalEvent > current.eventCursor && current.runtime.tick != UINT64_MAX);
            prepared.state = before; auto& state = prepared.state[index];
            if (playerInventory)
            {
                require(mNpcBanks);
                seedPlayerItems(content, state.playerItems, *playerInventory);
            }
            auto runningExecution = execution;
            if (encounterPrepared) runningExecution.physicalScope = index + 2;
            const auto origin = mEncounters ? resolver.resolve(execution, {}, false) : MWWorld::Ptr{};
            if (mEncounters) require(origin.hasLiveReference() && origin.getType() == ESM::NPC::sRecordId);
            struct Scene final : MWScript::ReferenceState
            {
                MWScript::ExecutionContext expected;
                MWWorld::Ptr source;
                bool prepared = false, removed = false, reservedByOther = false;
                bool relocations = false, activities = false, npcBanks = false;
                std::optional<Instance::Activity> activity;
                ESM::RefId cell;
                std::array<float, 3> origin{};
                std::optional<Instance::Relocation> relocation;
                StoryItemTransaction items;
                int itemCount(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item) const override
                {
                    require(npcBanks && execution == expected);
                    return items.itemCount(execution, ptr, item);
                }
                void addItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count) override
                {
                    require(npcBanks && execution == expected);
                    items.addItem(execution, ptr, item, count);
                }
                void removeItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count) override
                {
                    require(npcBanks && execution == expected);
                    items.removeItem(execution, ptr, item, count);
                }
                const ESM::Position& position(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                { validate(execution, ptr); require(relocations); return source.getRefData().getPosition(); }
                void position(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr,
                    const std::array<float, 3>& xyz, float yaw, const ESM::RefId& destination) override
                {
                    validate(execution, ptr);
                    require(relocations && (!activities || npcBanks) && !removed && !activity
                        && !reservedByOther && (destination.empty() || destination == cell));
                    Instance::Relocation pose{origin, xyz, yaw}; pose.validate();
                    if (!prepared) throw EncounterPreparationRequired{};
                    relocation = pose;
                    auto p = source.getRefData().getPosition();
                    std::copy(xyz.begin(), xyz.end(), p.pos); p.rot[2] = yaw;
                    source.getRefData().setPosition(p);
                }
                void aiTravel(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr,
                    const std::array<float, 3>& destination, bool repeat, const ESM::RefId& follower, float duration) override
                {
                    validate(execution, ptr);
                    require(activities && !repeat && duration == 0 && !removed && !relocation && !activity
                        && !reservedByOther && (follower.empty() || follower == ESM::RefId::stringRefId("player")));
                    Instance::Relocation target{origin, destination, 0}; target.validate();
                    if (!prepared) throw EncounterPreparationRequired{};
                    // The stock opcode supplies package arguments; stock completion/range
                    // policy advances the durable continuation with detached navigation.
                    activity.emplace(); activity->escort = !follower.empty(); activity->destination = destination;
                    const auto& p = source.getRefData().getPosition();
                    relocation = Instance::Relocation{origin, origin, p.rot[2]};
                }
                void enable(const MWScript::ExecutionContext&, const MWWorld::Ptr&) override
                { throw std::invalid_argument("Enable requires safe-return restoration admission"); }
                void validate(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const
                {
                    require(execution == expected && ptr.hasLiveReference() && ptr == source
                        && ptr.getType() == ESM::NPC::sRecordId);
                }
                void disable(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    validate(execution, ptr);
                    require((!activities || npcBanks) && !relocation && !activity && !reservedByOther); // Mixed physical outcomes need separate admission.
                    if (!prepared) throw EncounterPreparationRequired{};
                    removed = true;
                    source.getRefData().disable();
                }
                bool disabled(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                { validate(execution, ptr); return removed; }
            } scene;
            std::optional<MWWorld::ManualRef> privateSource;
            struct Resolver final : MWScript::ReferenceResolver
            {
                const MWScript::ReferenceResolver& upstream;
                Scene& scene;
                MWWorld::Ptr origin;
                MWScript::ExecutionContext originExecution;
                Resolver(const MWScript::ReferenceResolver& resolver, Scene& state, MWWorld::Ptr ptr,
                    MWScript::ExecutionContext execution)
                    : upstream(resolver), scene(state), origin(ptr), originExecution(execution) {}
                MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id, bool activeOnly) const override
                {
                    require(execution == scene.expected);
                    auto ptr = upstream.resolve(originExecution, id, activeOnly);
                    if (id != "player" && ptr == origin)
                        return activeOnly && scene.removed ? MWWorld::Ptr{} : scene.source;
                    return ptr;
                }
            } scopedResolver(resolver, scene, origin, execution);
            if (mEncounters)
            {
                require(execution.sourceReference == found->encounter->reference
                    && origin.getCellRef().getRefId() == found->encounter->record && origin.getRefData().isEnabled());
                privateSource.emplace(content, found->encounter->record);
                privateSource->getPtr().getCellRef() = origin.getCellRef();
                privateSource->getPtr().getRefData().setPosition(origin.getRefData().getPosition());
                const auto owner = mFairOpportunities ? reservationOwner(before, execution.sourceLife) : 0;
                scene.expected = runningExecution; scene.source = privateSource->getPtr(); scene.prepared = encounterPrepared;
                scene.reservedByOther = owner != 0 && owner != execution.character;
                scene.relocations = mRelocations; scene.activities = mActivities; scene.npcBanks = mNpcBanks;
                scene.cell = found->encounter->cell;
                std::copy(std::begin(origin.getRefData().getPosition().pos), std::end(origin.getRefData().getPosition().pos), scene.origin.begin());
                scene.items = {&content, &scopedResolver, found->encounter->record, execution.character, execution.story,
                    execution.sourceReference, 1, scene.source, &state.playerItems, &state.npcItems, &state.itemReceipts,
                    &prepared.itemTransfers};
            }

            const MWScript::ReferenceResolver& runningResolver = mEncounters ? scopedResolver : resolver;
            MWScript::DialogueState dialogue(runningExecution, state.dialogue, found->dialogues, mIntents);
            const auto& record = dialogue.record(topic, ESM::Dialogue::Topic);
            Bank bank(runningExecution, *found, state);
            // Player stats remain read-only. The source NPC snapshot has one
            // admitted result writer and restores this story's durable base.
            std::optional<MWMechanics::NpcStats> stats;
            if (playerStats) stats.emplace(*playerStats);
            FilterContext script(runningExecution, content, runningResolver, bank, dialogue, state.runtime.runtime.globals,
                stats ? &*stats : nullptr, cellName, customClassName, mEncounters ? &scene : nullptr);
            JournalText journalText(script, journalStamp.value_or(MWScript::DialogueState::Stamp{}));
            if (mIntents) dialogue.setJournalText(journalText);
            const auto actor = script.resolveReference({});
            require(actor.hasLiveReference()
                && (mIntents ? actor.getClass().getScript(actor).empty()
                             : actor.getClass().getScript(actor) == result.script));
            std::optional<MWMechanics::NpcStats> npcStats;
            auto ownedNpc = std::ranges::find(state.npcs, execution.sourceReference, &Instance::Npc::reference);
            if (ownedNpc == state.npcs.end())
                ownedNpc = std::ranges::find_if(state.npcs, [&](const auto& npc) {
                    return !npc.reference && npc.record == actor.getCellRef().getRefId();
                });
            if (mIntents && speakerStats)
            {
                require(actor.getType() == ESM::NPC::sRecordId);
                npcStats.emplace(*speakerStats);
                if (ownedNpc != state.npcs.end())
                {
                    require(ownedNpc->record == actor.getCellRef().getRefId() && ownedNpc->life <= execution.sourceLife);
                    npcStats->setBaseDisposition(ownedNpc->disposition);
                }
                for (const auto& npc : state.npcs)
                    if (npc.reference != execution.sourceReference && npc.record != actor.getCellRef().getRefId())
                        dialogue.seedNpcDisposition(npc.record, npc.disposition);
            }
            else require(ownedNpc == state.npcs.end());
            const int priorDisposition = npcStats ? npcStats->getBaseDisposition() : 0;
            if (mIntents)
            {
                disposition = 0;
                if (std::ranges::any_of(record.mInfo, [](const auto& info) { return info.mData.mDisposition != 0; }))
                {
                    require(stats && npcStats && actor.getType() == ESM::NPC::sRecordId);
                    const auto player = script.resolveReference(ESM::RefId::stringRefId("player"));
                    require(player.hasLiveReference() && player.getType() == ESM::NPC::sRecordId);
                    disposition = MWMechanics::derivedDisposition(*actor.get<ESM::NPC>()->mBase, *npcStats,
                        *player.get<ESM::NPC>()->mBase, *stats, content.get<ESM::GameSetting>(),
                        [&](const ESM::RefId& from, const ESM::RefId& to) {
                            // Faction-reaction result writes remain closed in this policy.
                            const auto& reactions = content.get<ESM::Faction>().find(from)->mReactions;
                            const auto found = reactions.find(to);
                            return found == reactions.end() ? 0 : found->second;
                        });
                }
            }
            MWDialogue::Filter::Scoped scoped{ script, dialogue, disposition, cellName,
                mIntents ? result.script : ESM::RefId{} };
            MWDialogue::Filter filter(actor, choice, talkedToPlayer, scoped);
            const auto response = filter.search(record, false);
            require(response.second != nullptr);
            const auto program = std::ranges::find_if(mResults[index], [&](const auto& value) {
                return value.topic == topic && value.info == response.second->mId; });
            require(program != mResults[index].end());
            // Stock dialogue expands the response before executing its result block.
            // Keep the private text local until every staged result succeeds.
            const auto text = mIntents ? Interpreter::fixDefinesDialog(response.second->mResponse, script, 4096) : std::string{};
            auto invocation = found->script->executeResult(state.runtime, runningExecution, content, runningResolver,
                bank, bank.instances.front(), dialogue, program->program, result.trace, npcStats ? &*npcStats : nullptr,
                mEncounters ? &scene : nullptr);
            if (scene.removed || scene.relocation)
            {
                require(dialogue.choices.empty()); // A removed speaker cannot own a pending choice.
                if (mFairOpportunities)
                {
                    const auto owner = reservationOwner(before, execution.sourceLife);
                    require(!owner || owner == execution.character);
                    if (state.reservedLife && execution.sourceLife >= state.reservedLife)
                    {
                        state.reservedLife = 0;
                        state.reservationTick = 0;
                    }
                }
                state.encounterLife = execution.sourceLife;
                state.relocation = scene.relocation;
                state.activity = std::move(scene.activity);
            }
            if (scene.removed || scene.relocation || (npcStats && (ownedNpc != state.npcs.end() || npcStats->getBaseDisposition() != priorDisposition)))
            {
                const Instance::Npc changed{execution.sourceReference, execution.sourceLife,
                    actor.getCellRef().getRefId(), npcStats ? npcStats->getBaseDisposition() : actor.get<ESM::NPC>()->mBase->mNpdt.mDisposition};
                if (ownedNpc != state.npcs.end()) *ownedNpc = changed;
                else
                {
                    require(state.npcs.size() < MaximumNpcsPerStory);
                    state.npcs.push_back(changed);
                }
            }
            for (const auto& foreign : dialogue.npcDispositions)
            {
                if (!foreign.modified) continue;
                const auto existing = std::ranges::find(state.npcs, foreign.record, &Instance::Npc::record);
                if (existing != state.npcs.end())
                    existing->disposition = foreign.disposition;
                else
                {
                    require(state.npcs.size() < MaximumNpcsPerStory);
                    state.npcs.push_back({0, 0, foreign.record, foreign.disposition});
                }
            }
            state.runtime.runtime.globals = std::move(invocation.state.globals);
            if (mIntents)
            {
                state.choices = std::move(dialogue.choices);
                prepared.response = text;
                result.presentation = std::move(invocation.presentation);
            }
            ++state.runtime.tick;
            bank.write(state);
            state.last = runningExecution; state.eventCursor = execution.causalEvent;
            if (state.encounterLife) state.last.physicalScope = index + 2;
            state.lastTopic = topic; state.lastInfo = response.second->mId; state.lastChoice = choice;
            if (!mIntents)
                prepared.image = encode(prepared.state);
            prepared.invocations = 1;
            result.outcome = SharedWorldScript::Outcome::Committed; result.executed = true;
        }
        catch (const EncounterPreparationRequired&)
        {
            // Neither locals, globals, progression nor output from discovery survives.
            // Admission already bound the sole cell/source; prepare a terminal private
            // reference and retry the identical causal occurrence from committed state.
            if (budget < 2)
            {
                prepared.state.clear(); prepared.image.clear(); prepared.response.clear(); result.presentation.clear();
                prepared.itemTransfers.clear();
                result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false;
                result.diagnostic = "Encounter discovery/retry exceeds invocation budget";
                return prepared;
            }
            auto retry = prepareDialogue(before, execution, topic, choice, disposition, talkedToPlayer, cellName,
                content, resolver, budget - 1, playerStats, customClassName, speakerStats, journalStamp, true, playerInventory);
            retry.result.trace.executed += result.trace.executed;
            if (retry.result.executed) ++retry.invocations;
            return retry;
        }
        catch (const MWScript::ScriptContextUnavailable& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            prepared.response.clear(); result.presentation.clear(); prepared.itemTransfers.clear(); result.executed = false;
            result.outcome = SharedWorldScript::Outcome::Deferred; result.diagnostic = error.what();
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            prepared.response.clear(); result.presentation.clear(); prepared.itemTransfers.clear();
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false;
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareIntent(const State& before, uint64_t character, uint64_t command,
        const DialogueIntent& request, std::string_view cellName, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget, const MWMechanics::NpcStats* playerStats,
        std::optional<std::string_view> customClassName, const MWMechanics::NpcStats* speakerStats,
        std::optional<MWScript::DialogueState::Stamp> journalStamp,
        std::optional<std::span<const ItemBalance>> playerInventory) const
    {
        Prepared rejected;
        try
        {
            require(mIntents && character && command && validDialogueIntent(request));
            (void)encode(before);
            const auto found = std::ranges::find_if(before, [&](const auto& state) {
                return state.last.character == character && state.last.story == request.story;
            });
            require(found != before.end());
            // Command IDs restart with a client process. The observed durable
            // revision distinguishes a new occurrence from an old receipt retry.
            if (command == found->dialogueCommand && request.revision != found->dialogueRevision)
            {
                require(request == found->dialogueRequest);
                rejected.result.outcome = SharedWorldScript::Outcome::Committed;
                return rejected;
            }
            require(request.revision == found->dialogueRevision && found->last.invocation != UINT64_MAX
                && found->eventCursor != UINT64_MAX);
            if (request.choice >= 0)
                require(request.speaker == found->dialogueRequest.speaker && request.life == found->dialogueRequest.life
                    && request.topic == found->dialogueRequest.topic
                    && std::ranges::any_of(
                        found->choices, [&](const auto& choice) { return choice.id == request.choice; }));
            else
            {
                require(found->choices.empty() || request.speaker != found->dialogueRequest.speaker
                    || request.life != found->dialogueRequest.life);
                require(std::ranges::find(found->dialogue.topics, ESM::RefId::stringRefId(request.topic))
                    != found->dialogue.topics.end());
            }
            auto execution = found->last;
            ++execution.invocation;
            execution.causalEvent = found->eventCursor + 1;
            execution.sourceReference = request.speaker;
            execution.sourceLife = request.life;
            const bool talked
                = std::ranges::find(found->talkedSpeakers, request.speaker) != found->talkedSpeakers.end();
            auto prepared = prepareDialogue(before, execution, ESM::RefId::stringRefId(request.topic), request.choice,
                0, talked, cellName, content, resolver, budget, playerStats, customClassName, speakerStats, journalStamp,
                false, playerInventory);
            if (prepared.result.outcome != SharedWorldScript::Outcome::Committed || !prepared.result.executed)
                return prepared;
            auto& state = prepared.state[size_t(found - before.begin())];
            state.dialogueRevision = request.revision + 1;
            state.dialogueCommand = command;
            state.dialogueRequest = request;
            if (!talked)
            {
                require(state.talkedSpeakers.size() < 64);
                state.talkedSpeakers.push_back(request.speaker);
            }
            auto output = view(state, *SessionId::fromValue(1), SessionGeneration::initial(),
                ServerTick::initial(), CanonicalRevision::initial());
            output.response = prepared.response; output.messages = prepared.result.presentation;
            require(!encodeReliablePersonalDialogue(output).empty());
            prepared.image = encode(prepared.state);
            return prepared;
        }
        catch (const std::exception& error)
        {
            rejected.result.diagnostic
                = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
            return rejected;
        }
    }

    uint64_t PersonalScripts::reservationOwner(const State& state, uint64_t life)
    {
        const Instance* first = nullptr;
        for (const auto& entry : state)
            if (entry.reservedLife && entry.reservedLife <= life
                && (!first || std::tuple(entry.reservationTick, entry.last.character, entry.last.story)
                    < std::tuple(first->reservationTick, first->last.character, first->last.story))) first = &entry;
        return first ? first->last.character : 0;
    }

    PersonalScripts::Prepared PersonalScripts::prepareScheduled(const State& before, uint64_t worldTick,
        std::span<const uint64_t> activeCharacters, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, const Source* source) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = worldTick;
        try
        {
            require(mScheduled && worldTick && before.size() == mDefinitions.size()
                && activeCharacters.size() <= MaximumInstances);
            for (size_t i = 0; i < activeCharacters.size(); ++i)
            {
                require(activeCharacters[i] && std::ranges::any_of(mDefinitions,
                    [&](const auto& entry) { return entry.character == activeCharacters[i]; }));
                require(std::find(activeCharacters.begin(), activeCharacters.begin() + i, activeCharacters[i])
                    == activeCharacters.begin() + i);
            }
            // Validate the complete candidate before executing any instance.
            (void)encode(before);
            require(!mSourceWaits || (source && source->reference == mDefinitions.front().encounter->reference
                && source->record == mDefinitions.front().encounter->record && source->life
                && (source->living ? !source->respawnTick : bool(source->respawnTick))));
            if (mNpcBanks)
            {
                require(std::ranges::all_of(source->position.pos, [](float v) { return std::isfinite(v); })
                    && std::ranges::all_of(source->position.rot, [](float v) { return std::isfinite(v); })
                    && source->playerInventories.size() <= MaximumInstances);
                for (size_t p = 0; p < source->playerInventories.size(); ++p)
                    require(source->playerInventories[p].character
                        && std::ranges::none_of(std::span(source->playerInventories).first(p), [&](const auto& prev) {
                               return prev.character == source->playerInventories[p].character;
                           }));
            }
            else
                require(!source || source->playerInventories.empty());
            if (mLifeEvents)
            {
                require(source->events.size() <= MaximumLifeEvents && source->life <= MaximumLifeEvents + 1
                    && source->events.size() == source->life - (source->living ? 1 : 0));
                uint64_t previousTick = 0;
                for (size_t e = 0; e < source->events.size(); ++e)
                {
                    const auto& event = source->events[e];
                    require(event.life == e + 1 && event.tick > previousTick && event.tick <= worldTick
                        && (event.killer ? event.killerLife && (event.killerKind == 1 || event.killerKind == 2)
                            : !event.killerKind && !event.killerLife && !event.owner && !event.ownerLife && !event.helper)
                        && bool(event.owner) == bool(event.ownerLife)
                        && (event.killerKind != 1 || (event.owner == event.killer && event.ownerLife == event.killerLife))
                        && (!event.helper || event.owner));
                    previousTick = event.tick;
                    if (mFairOpportunities)
                    {
                        const auto owner = reservationOwner(before, event.life);
                        require(!owner || event.killerKind != 1 || event.owner == owner);
                    }
                }
                if (!source->living) require(source->events.back().killer == source->killer
                    && source->events.back().killerKind == source->killerKind);
            }
            struct SourceResolver final : MWScript::ReferenceResolver, MWScript::ReferenceState
            {
                const Source& source;
                MWWorld::ManualRef reference;
                MWWorld::ManualRef playerReference;
                bool events, encounters;
                uint64_t cursor = 0, expectedScope = 1, expectedLife = 0;
                bool privateScope = false, reservedByOther = false, removed = false, activated = false;
                StoryItemTransaction items;
                std::vector<Prepared::StagedItemTransfer> transfers;
                SourceResolver(const Source& value, const MWWorld::ESMStore& store, bool lifeEvents, bool allowEncounters)
                    : source(value), reference(store, value.record), playerReference(store, value.record)
                    , events(lifeEvents), encounters(allowEncounters), expectedLife(value.life)
                {
                    reference.getPtr().getRefData().setPosition(value.position);
                }
                bool ownDeath(uint64_t character) const
                {
                    if (source.living) return false;
                    return events ? source.events.back().owner == character && !source.events.back().helper
                        : source.killerKind == 1 && source.killer == character;
                }
                bool eligible(uint64_t character) const
                {
                    return std::ranges::any_of(source.events, [&](const auto& event) {
                        return event.life > cursor && event.owner == character && !event.helper;
                    });
                }
                MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id, bool) const override
                {
                    require(execution.physicalScope == expectedScope && execution.sourceReference == source.reference
                        && execution.sourceLife == expectedLife
                        && (id.empty() || id == source.record || (encounters && id == "player")));
                    if (!privateScope && !source.living && !events)
                    {
                        require(source.killer && (source.killerKind == 1 || source.killerKind == 2));
                        // Own consequences remain queryable; a foreign death
                        // defers source reads. Death does not change the canonical
                        // enabled flag. Attributed OnDeath requires the event policy.
                        if (source.killerKind != 1 || source.killer != execution.character)
                            throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    }
                    if (id == "player")
                        return playerReference.getPtr();
                    return reference.getPtr();
                }
                const ESM::Position& position(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                {
                    require(ptr == resolve(execution, source.record, false));
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    return reference.getPtr().getRefData().getPosition();
                }
                void enable(const MWScript::ExecutionContext&, const MWWorld::Ptr&) override
                { throw std::invalid_argument("Scheduled source writes not admitted"); }
                void disable(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    require(encounters && ptr == resolve(execution, source.record, false)
                        && ptr.hasLiveReference() && ptr.getType() == ESM::NPC::sRecordId);
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    require((privateScope || source.living) && !reservedByOther);
                    removed = true;
                    reference.getPtr().getRefData().disable();
                }
                bool disabled(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                {
                    require(ptr == resolve(execution, source.record, false));
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    return removed || !ptr.getRefData().isEnabled();
                }
                bool onActivate(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    require(ptr == resolve(execution, source.record, false));
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    return !disabled(execution, ptr) && reference.getPtr().getRefData().onActivate();
                }
                void activate(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    require(ptr == resolve(execution, source.record, false));
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    require(!activated && !disabled(execution, ptr) && reference.getPtr().getRefData().activateByScript());
                    activated = true;
                }
                bool onDeath(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    require(events && ptr == resolve(execution, source.record, false));
                    for (const auto& event : source.events)
                        if (event.life > cursor && (!privateScope || event.life < expectedLife)
                            && event.owner == execution.character && !event.helper)
                        { cursor = event.life; return true; }
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    return false;
                }
                float health(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, bool ratio) const override
                {
                    const auto* activeStats = privateScope && source.privateStats ? source.privateStats : source.stats;
                    require(ptr == resolve(execution, source.record, false) && activeStats);
                    const auto& health = activeStats->getHealth();
                    require(std::isfinite(health.getCurrent()) && std::isfinite(health.getModified())
                        && (privateScope ? health.getCurrent() > 0 : source.living == (health.getCurrent() > 0)));
                    if (!privateScope && !source.living && !ownDeath(execution.character))
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    const float value = ratio ? health.getRatio(false) : health.getCurrent();
                    require(std::isfinite(value));
                    return value;
                }
                void validateScope(const MWScript::ExecutionContext& execution) const
                {
                    require(execution.physicalScope == expectedScope && execution.sourceReference == source.reference
                        && execution.sourceLife == expectedLife);
                    if (!privateScope && !source.living && !ownDeath(execution.character) && !eligible(execution.character))
                    {
                        require(events || (source.killer && (source.killerKind == 1 || source.killerKind == 2)));
                        throw MWScript::ScriptSourceWait(source.reference, source.life, source.respawnTick);
                    }
                }
                bool cellChanged(const MWScript::ExecutionContext& execution) const override
                {
                    validateScope(execution);
                    return source.cellChanged && (!source.cellChangedCharacter || source.cellChangedCharacter == execution.character);
                }
                bool menuMode(const MWScript::ExecutionContext& execution) const override
                {
                    validateScope(execution);
                    return false;
                }
                bool playerSleeping(const MWScript::ExecutionContext& execution) const override
                {
                    validateScope(execution);
                    return source.sleeping && (!source.sleepingCharacter || source.sleepingCharacter == execution.character);
                }
                int itemCount(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item) const override
                {
                    require(encounters);
                    validateScope(execution);
                    return items.itemCount(execution, ptr, item);
                }
                void addItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count) override
                {
                    require(encounters);
                    validateScope(execution);
                    items.addItem(execution, ptr, item, count);
                }
                void removeItem(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr, const ESM::RefId& item, int count) override
                {
                    require(encounters);
                    validateScope(execution);
                    items.removeItem(execution, ptr, item, count);
                }
            };
            std::unique_ptr<SourceResolver> sourceResolver;
            if (mSourceWaits)
                sourceResolver = std::make_unique<SourceResolver>(
                    *source, content, mLifeEvents, mEncounters && (!mLifeEvents || mNpcBanks) && (!mActivities || mNpcBanks));
            for (const auto& entry : before) require(entry.scheduledTick <= worldTick);
            prepared.state = before;
            bool processed = false;
            for (size_t i = 0; i < before.size(); ++i)
            {
                const auto& current = before[i];
                const auto scope = physicalScope(before, current.last.character);
                if (current.encounterReturned || (current.encounterLife ? (!mNpcBanks || !current.relocation || scope != i + 2) : scope != 1)
                    || current.scheduledTick == worldTick
                    || std::ranges::find(activeCharacters, current.last.character) == activeCharacters.end()) continue;
                require(prepared.invocations < MaximumInvocationsPerTransaction
                    && current.last.invocation != std::numeric_limits<uint64_t>::max());
                auto execution = current.last;
                ++execution.invocation; execution.causalEvent = 0; execution.sourceReference = execution.sourceLife = 0;
                if (mSourceWaits)
                {
                    sourceResolver->cursor = current.deathCursor;
                    sourceResolver->privateScope = mNpcBanks && current.encounterLife != 0;
                    sourceResolver->expectedScope = sourceResolver->privateScope ? i + 2 : 1;
                    sourceResolver->expectedLife = sourceResolver->privateScope ? current.encounterLife : source->life;
                    const auto owner = mFairOpportunities ? reservationOwner(before, sourceResolver->expectedLife) : 0;
                    sourceResolver->reservedByOther = owner != 0 && owner != execution.character;
                    sourceResolver->removed = false;
                    sourceResolver->activated = false;
                    sourceResolver->reference.getPtr().getRefData() = MWWorld::RefData();
                    (void)sourceResolver->reference.getPtr().getRefData().onActivate();
                    if (source->activated && (!source->activatedCharacter || source->activatedCharacter == execution.character))
                        require(!sourceResolver->reference.getPtr().getRefData().activate());
                    ESM::Position pose = source->position;
                    if (sourceResolver->privateScope)
                    {
                        require(bool(current.relocation));
                        std::copy(current.relocation->position.begin(), current.relocation->position.end(), pose.pos);
                        pose.rot[2] = current.relocation->yaw;
                    }
                    sourceResolver->reference.getPtr().getRefData().setPosition(pose);
                    if (mNpcBanks)
                    {
                        const auto inv = std::ranges::find(
                            source->playerInventories, execution.character, &PlayerInventory::character);
                        if (inv != source->playerInventories.end())
                            seedPlayerItems(content, prepared.state[i].playerItems, inv->items);
                        sourceResolver->transfers.clear();
                        sourceResolver->items = {&content, sourceResolver.get(), source->record, execution.character,
                            execution.story, source->reference, 2, sourceResolver->reference.getPtr(),
                            &prepared.state[i].playerItems, &prepared.state[i].npcItems,
                            &prepared.state[i].itemReceipts, &sourceResolver->transfers};
                    }
                    require((sourceResolver->privateScope ? current.encounterLife : source->life) >= current.last.sourceLife);
                    execution.sourceReference = source->reference;
                    execution.sourceLife = sourceResolver->expectedLife;
                    if (current.sourceWait)
                    {
                        const auto& wait = *current.sourceWait;
                        require(source->life >= wait.invocation.sourceLife);
                        const bool ownDeath = sourceResolver->ownDeath(execution.character);
                        if (!ownDeath && !sourceResolver->eligible(execution.character)
                            && (!source->living || source->life == wait.invocation.sourceLife))
                        {
                            auto& state = prepared.state[i];
                            if (!source->living)
                            {
                                require(mLifeEvents || (source->killer && (source->killerKind == 1 || source->killerKind == 2)
                                    && (source->killerKind != 1 || source->killer != execution.character)));
                                state.sourceWait->invocation.sourceLife = source->life;
                                state.sourceWait->respawnTick = source->respawnTick;
                            }
                            state.scheduledTick = worldTick; processed = true;
                            ++prepared.invocations; // Bounded wake probe, no bytecode or story clock step.
                            continue;
                        }
                    }
                }
                auto invocation = prepare(prepared.state, execution, content, sourceResolver ? *sourceResolver : resolver,
                    MaximumInvocationsPerTransaction - prepared.invocations, sourceResolver.get());
                result = std::move(invocation.result);
                // Personal presentation remains closed; never send it as shared output.
                result.presentation.clear();
                if (mSourceWaits && result.outcome == SharedWorldScript::Outcome::Deferred && result.sourceWait)
                {
                    const auto& dependency = *result.sourceWait;
                    require(dependency.reference == source->reference && dependency.life == source->life);
                    auto& state = prepared.state[i];
                    state.playerItems = current.playerItems;
                    state.npcItems = current.npcItems;
                    state.itemReceipts = current.itemReceipts;
                    const auto since = state.sourceWait ? state.sourceWait->sinceTick : worldTick;
                    state.sourceWait = Instance::SourceWait{execution, result.script, result.trace.instruction,
                        since, dependency.respawnTick};
                    if (mFairOpportunities && !state.reservedLife)
                    {
                        require(source->life < MaximumLifeEvents);
                        state.reservedLife = source->life + 1; state.reservationTick = worldTick;
                    }
                    state.scheduledTick = worldTick; processed = true;
                    prepared.invocations += invocation.invocations;
                    continue;
                }
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                {
                    result.diagnostic = "character=" + std::to_string(execution.character)
                        + " story=" + std::to_string(execution.story) + " scope=" + std::to_string(execution.physicalScope)
                        + " source="
                        + std::to_string(execution.sourceReference) + " life=" + std::to_string(execution.sourceLife)
                        + " " + result.diagnostic;
                    result.diagnostic.resize(std::min(result.diagnostic.size(), SharedWorldScript::MaximumDiagnosticBytes));
                    prepared.state.clear(); prepared.invocations = 0; prepared.itemTransfers.clear(); result.executed = false;
                    return prepared;
                }
                prepared.state.swap(invocation.state);
                prepared.state[i].scheduledTick = worldTick;
                if (mSourceWaits)
                {
                    if (mNpcBanks && !sourceResolver->transfers.empty())
                        prepared.itemTransfers.insert(prepared.itemTransfers.end(),
                            sourceResolver->transfers.begin(), sourceResolver->transfers.end());
                    if (mLifeEvents) prepared.state[i].deathCursor = sourceResolver->cursor;
                    if (mFairOpportunities && prepared.state[i].reservedLife
                        && prepared.state[i].deathCursor >= prepared.state[i].reservedLife)
                    { prepared.state[i].reservedLife = 0; prepared.state[i].reservationTick = 0; }
                    if (sourceResolver->removed)
                    {
                        auto& state = prepared.state[i];
                        state.choices.clear();
                        if (mFairOpportunities)
                        {
                            const auto owner = reservationOwner(before, execution.sourceLife);
                            require(!owner || owner == execution.character);
                            if (state.reservedLife && execution.sourceLife >= state.reservedLife)
                            {
                                state.reservedLife = 0;
                                state.reservationTick = 0;
                            }
                        }
                        state.encounterLife = execution.sourceLife;
                        state.relocation.reset();
                        state.activity.reset();
                        state.last.physicalScope = i + 2;
                        auto ownedNpc = std::ranges::find(state.npcs, execution.sourceReference, &Instance::Npc::reference);
                        if (ownedNpc == state.npcs.end())
                            ownedNpc = std::ranges::find_if(state.npcs, [&](const auto& npc) {
                                return !npc.reference && npc.record == source->record;
                            });
                        if (ownedNpc != state.npcs.end())
                        {
                            require(ownedNpc->record == source->record && ownedNpc->life <= execution.sourceLife);
                            ownedNpc->reference = execution.sourceReference;
                            ownedNpc->life = execution.sourceLife;
                        }
                        else
                        {
                            require(state.npcs.size() < MaximumNpcsPerStory);
                            const auto* npcRecord = content.get<ESM::NPC>().find(source->record);
                            state.npcs.push_back({execution.sourceReference, execution.sourceLife, source->record,
                                npcRecord->mNpdt.mDisposition});
                        }
                    }
                    require(prepared.state[i].clockTicks != UINT64_MAX);
                    ++prepared.state[i].clockTicks;
                }
                processed = true;
                prepared.invocations += invocation.invocations;
                require(prepared.invocations <= MaximumInvocationsPerTransaction);
            }
            result.outcome = SharedWorldScript::Outcome::Committed;
            result.executed = processed;
            if (result.executed) prepared.image = encode(prepared.state);
            else prepared.state.clear();
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0; prepared.itemTransfers.clear();
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }
}
