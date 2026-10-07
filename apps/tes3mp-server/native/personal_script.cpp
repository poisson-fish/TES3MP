#include "personal_script.hpp"
#include <apps/openmw/mwscript/scriptstate.hpp>
#include <algorithm>
#include <limits>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t Magic = 0x3154535054335354;
        constexpr uint64_t ScheduledMagic = 0x3254535054335354;
        constexpr uint64_t BankMagic = 0x3354535054335354;
        void require(bool value)
        { if (!value) throw std::invalid_argument("Invalid or incompatible personal script state"); }
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
    }

    PersonalScripts::PersonalScripts(uint64_t campaign, std::vector<Definition> definitions, CredentialCrypto& crypto,
        bool scheduled, bool banks)
        : mCampaign(campaign), mDefinitions(std::move(definitions)), mScheduled(scheduled), mBanks(banks)
    {
        require(campaign && !mDefinitions.empty() && mDefinitions.size() <= MaximumInstances);
        size_t scripts = 0;
        require(!mBanks || mScheduled);
        for (size_t i = 0; i < mDefinitions.size(); ++i)
        {
            const auto& entry = mDefinitions[i];
            require(entry.character && entry.story && entry.script);
            require(entry.members.size() < MaximumScriptsPerBank
                && (mBanks || (entry.members.empty() && entry.running))
                && entry.script->bankCapable() == mBanks);
            scripts += 1 + entry.members.size(); require(scripts <= MaximumInstances);
            std::vector<ESM::RefId> names{entry.script->initial().runtime.script.mId};
            for (const auto& member : entry.members)
            {
                require(member.script && member.script->bankCapable());
                const auto id = member.script->initial().runtime.script.mId;
                require(std::ranges::find(names, id) == names.end()); names.push_back(id);
            }
            for (size_t j = 0; j < i; ++j)
                require(entry.character != mDefinitions[j].character || entry.story != mDefinitions[j].story);
        }
        // Includes every script's content/source/defaults/limits digest and the
        // complete ordered ownership policy. No session identities or pointers.
        auto binding = encode(initial());
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
            for (const auto& member : entry.members)
            {
                auto value = member.script->initial(); value.runtime.script.mRunning = member.running;
                value.runtime.globals.clear(); instance.members.push_back(std::move(value));
            }
        }
        return state;
    }

    void PersonalScripts::validate(const Instance& state, size_t index) const
    {
        const auto& expected = mDefinitions.at(index);
        const auto& last = state.last;
        require(last.ownership == MWScript::ExecutionContext::Ownership::Character && last.campaign == mCampaign
            && last.character == expected.character && last.story == expected.story && last.physicalScope == 1
            && !last.sourceReference && !last.sourceLife
            && (mBanks ? last.invocation >= state.runtime.tick : last.invocation == state.runtime.tick)
            && last.causalEvent <= state.eventCursor
            && (!last.causalEvent || last.causalEvent == state.eventCursor)
            && (last.invocation || (!last.causalEvent && !state.eventCursor && !state.scheduledTick))
            && (mScheduled || !state.scheduledTick));
        require(state.members.size() == expected.members.size());
        for (const auto& member : state.members)
            require(member.tick <= last.invocation && member.runtime.globals.empty());
        if (last.invocation) last.validate();
    }

    std::vector<char> PersonalScripts::encode(const State& state) const
    {
        require(state.size() == mDefinitions.size());
        std::vector<char> bytes;
        word(bytes, mBanks ? BankMagic : mScheduled ? ScheduledMagic : Magic);
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
        }
        require(bytes.size() <= MaximumImageBytes);
        return bytes;
    }

    PersonalScripts::State PersonalScripts::restore(std::span<const char> bytes) const
    {
        require(bytes.size() <= MaximumImageBytes);
        Cursor input{bytes};
        require(input.word() == (mBanks ? BankMagic : mScheduled ? ScheduledMagic : Magic));
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
            validate(entry, i); state.push_back(std::move(entry));
        }
        require(encode(state) == std::vector<char>(bytes.begin(), bytes.end()));
        return state;
    }

    PersonalScripts::Prepared PersonalScripts::prepare(const State& before, const MWScript::ExecutionContext& execution,
        const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver, size_t budget) const
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
                && execution.physicalScope == 1 && !execution.sourceReference && !execution.sourceLife);
            result.script = current.runtime.runtime.script.mId;
            if (execution == current.last)
            { result.outcome = SharedWorldScript::Outcome::Committed; return prepared; }
            require(budget && budget <= MaximumInvocationsPerTransaction);
            require(current.last.invocation != std::numeric_limits<uint64_t>::max()
                && execution.invocation == current.last.invocation + 1
                && (!execution.causalEvent || execution.causalEvent > current.eventCursor));
            if (mBanks) return prepareBank(before, index, execution, content, resolver, budget);
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
        const MWScript::ReferenceResolver& resolver, size_t budget) const
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
            std::vector<const SharedWorldScript*> scripts{definition.script.get()};
            for (const auto& member : definition.members) scripts.push_back(member.script.get());
            struct Bank final : MWScript::ScriptState
            {
                const MWScript::ExecutionContext& expected;
                const std::vector<const SharedWorldScript*>& scripts;
                std::vector<MWScript::GlobalScriptDesc> instances;
                Bank(const MWScript::ExecutionContext& context, const std::vector<const SharedWorldScript*>& policy)
                    : expected(context), scripts(policy) {}
                Binding resolve(const MWScript::ExecutionContext& context, const ESM::RefId& id) override
                {
                    require(context == expected);
                    for (size_t i = 0; i < instances.size(); ++i)
                        if (instances[i].mLocals.getScriptId() == id) return {instances[i], scripts[i]->declarations()};
                    throw std::invalid_argument("Script lacks character/story bank ownership");
                }
            } bank(execution, scripts);
            bank.instances.reserve(scripts.size());
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                const auto& member = i ? state.members[i - 1] : state.runtime;
                auto& descriptor = bank.instances.emplace_back();
                descriptor.mLocals = MWScript::Locals::restore(member.runtime.script.mLocals,
                    member.runtime.script.mId, scripts[i]->declarations());
                descriptor.mRunning = member.runtime.script.mRunning;
            }
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
                auto invocation = scripts[i]->prepareBank(input, execution, content, resolver, bank, descriptor);
                result = std::move(invocation.result); result.presentation.clear();
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                { prepared.state.clear(); prepared.invocations = 0; result.executed = false; return prepared; }
                state.runtime.runtime.globals = std::move(invocation.state.runtime.globals);
                member.tick = invocation.state.tick;
                ++prepared.invocations;
            }
            // Includes member writes to already visited/stopped scripts and self
            // aliases. No separate caller-local copy can overwrite those writes.
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                auto& member = i ? state.members[i - 1] : state.runtime;
                member.runtime.script.mRunning = bank.instances[i].mRunning;
                member.runtime.script.mLocals.mVariables.clear();
                require(bank.instances[i].mLocals.write(member.runtime.script.mLocals, scripts[i]->declarations()));
            }
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

    PersonalScripts::Prepared PersonalScripts::prepareScheduled(const State& before, uint64_t worldTick,
        std::span<const uint64_t> activeCharacters, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver) const
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
            for (const auto& entry : before) require(entry.scheduledTick <= worldTick);
            prepared.state = before;
            bool processed = false;
            for (size_t i = 0; i < before.size(); ++i)
            {
                const auto& current = before[i];
                if (current.scheduledTick == worldTick || std::ranges::find(activeCharacters, current.last.character)
                    == activeCharacters.end()) continue;
                require(prepared.invocations < MaximumInvocationsPerTransaction
                    && current.last.invocation != std::numeric_limits<uint64_t>::max());
                auto execution = current.last;
                ++execution.invocation; execution.causalEvent = 0;
                auto invocation = prepare(prepared.state, execution, content, resolver,
                    MaximumInvocationsPerTransaction - prepared.invocations);
                result = std::move(invocation.result);
                // Personal presentation remains closed; never send it as shared output.
                result.presentation.clear();
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                {
                    result.diagnostic = "character=" + std::to_string(execution.character)
                        + " story=" + std::to_string(execution.story) + " scope=1 source=0 life=0 " + result.diagnostic;
                    result.diagnostic.resize(std::min(result.diagnostic.size(), SharedWorldScript::MaximumDiagnosticBytes));
                    prepared.state.clear(); prepared.invocations = 0; result.executed = false;
                    return prepared;
                }
                prepared.state.swap(invocation.state);
                prepared.state[i].scheduledTick = worldTick;
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
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }
}
