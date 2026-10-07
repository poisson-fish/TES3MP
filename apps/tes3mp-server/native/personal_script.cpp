#include "personal_script.hpp"
#include <algorithm>
#include <limits>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t Magic = 0x3154535054335354;
        constexpr uint64_t ScheduledMagic = 0x3254535054335354;
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
        bool scheduled)
        : mCampaign(campaign), mDefinitions(std::move(definitions)), mScheduled(scheduled)
    {
        require(campaign && !mDefinitions.empty() && mDefinitions.size() <= MaximumInstances);
        for (size_t i = 0; i < mDefinitions.size(); ++i)
        {
            const auto& entry = mDefinitions[i];
            require(entry.character && entry.story && entry.script);
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
            state.push_back({{MWScript::ExecutionContext::Ownership::Character, mCampaign, entry.story, 1,
                entry.character, 0, 0, 0, 0}, 0, entry.script->initial()});
        return state;
    }

    void PersonalScripts::validate(const Instance& state, size_t index) const
    {
        const auto& expected = mDefinitions.at(index);
        const auto& last = state.last;
        require(last.ownership == MWScript::ExecutionContext::Ownership::Character && last.campaign == mCampaign
            && last.character == expected.character && last.story == expected.story && last.physicalScope == 1
            && !last.sourceReference && !last.sourceLife && last.invocation == state.runtime.tick
            && last.causalEvent <= state.eventCursor
            && (!last.causalEvent || last.causalEvent == state.eventCursor)
            && (last.invocation || (!last.causalEvent && !state.eventCursor && !state.scheduledTick))
            && (mScheduled || !state.scheduledTick));
        if (last.invocation) last.validate();
    }

    std::vector<char> PersonalScripts::encode(const State& state) const
    {
        require(state.size() == mDefinitions.size());
        std::vector<char> bytes;
        word(bytes, mScheduled ? ScheduledMagic : Magic);
        for (auto value : mIdentity.bytes) bytes.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
        word(bytes, state.size());
        for (size_t i = 0; i < state.size(); ++i)
        {
            validate(state[i], i);
            const auto image = mDefinitions[i].script->encode(state[i].runtime);
            context(bytes, state[i].last); word(bytes, state[i].eventCursor); word(bytes, image.size());
            if (mScheduled) word(bytes, state[i].scheduledTick);
            bytes.insert(bytes.end(), image.begin(), image.end());
        }
        require(bytes.size() <= MaximumImageBytes);
        return bytes;
    }

    PersonalScripts::State PersonalScripts::restore(std::span<const char> bytes) const
    {
        require(bytes.size() <= MaximumImageBytes);
        Cursor input{bytes};
        require(input.word() == (mScheduled ? ScheduledMagic : Magic));
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
        }
        require(preflight.bytes.empty());
        State state;
        state.reserve(size_t(count));
        for (size_t i = 0; i < count; ++i)
        {
            auto last = context(input); const auto event = input.word(), size = input.word();
            const auto scheduledTick = mScheduled ? input.word() : 0;
            Instance entry{last, event, mDefinitions[i].script->restore(input.take(size)), scheduledTick};
            validate(entry, i); state.push_back(std::move(entry));
        }
        require(encode(state) == std::vector<char>(bytes.begin(), bytes.end()));
        return state;
    }

    PersonalScripts::Prepared PersonalScripts::prepare(const State& before, const MWScript::ExecutionContext& execution,
        const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver) const
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
            require(current.last.invocation != std::numeric_limits<uint64_t>::max()
                && execution.invocation == current.last.invocation + 1
                && (!execution.causalEvent || execution.causalEvent > current.eventCursor));
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
            for (size_t i = 0; i < before.size(); ++i)
            {
                const auto& current = before[i];
                if (current.scheduledTick == worldTick || std::ranges::find(activeCharacters, current.last.character)
                    == activeCharacters.end()) continue;
                require(prepared.invocations < MaximumInvocationsPerTransaction
                    && current.last.invocation != std::numeric_limits<uint64_t>::max());
                auto execution = current.last;
                ++execution.invocation; execution.causalEvent = 0;
                auto invocation = prepare(prepared.state, execution, content, resolver);
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
                ++prepared.invocations;
            }
            result.outcome = SharedWorldScript::Outcome::Committed;
            result.executed = prepared.invocations != 0;
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
