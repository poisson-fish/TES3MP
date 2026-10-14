#include "worldscript.hpp"
#include "aiextensions.hpp"
#include "transformationextensions.hpp"

#include "interpretercontext.hpp"
#include "locals.hpp"
#include "miscextensions.hpp"
#include "scriptstate.hpp"
#include "dialogueextensions.hpp"
#include "statsextensions.hpp"

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/compiler/quickfileparser.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/tokenloc.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/misc/strings/algorithm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace MWScript
{
    namespace
    {
        void require(bool value, const char* message)
        {
            if (!value) throw std::invalid_argument(message);
        }
        void name(std::string_view value)
        {
            require(!value.empty() && value.size() <= WorldScript::MaximumNameBytes
                && value.find('\0') == std::string_view::npos, "Invalid world script name");
        }
        char type(const ESM::Variant& value)
        {
            switch (value.getType())
            {
                case ESM::VT_Short: return 's';
                case ESM::VT_Long: return 'l';
                case ESM::VT_Float: return 'f';
                default: throw std::invalid_argument("Invalid world global type");
            }
        }
        const ESM::Global* find(const std::vector<ESM::Global>& globals, std::string_view id)
        {
            const auto found = std::ranges::find_if(globals,
                [&](const auto& global) { return ::Misc::StringUtils::ciEqual(global.mId.getRefIdString(), id); });
            return found == globals.end() ? nullptr : &*found;
        }
        class Context final : public InterpreterContext
        {
            std::vector<ESM::Global>& mGlobals;
            std::vector<std::string>& mPresentation;
            bool mSourceClock = false;
            ESM::Variant& global(std::string_view id) const
            {
                const auto* found = find(mGlobals, id);
                require(found != nullptr, "World global lacks explicit ownership");
                return mGlobals[found - mGlobals.data()].mValue;
            }
            void present(std::string_view message)
            {
                require(message.size() <= WorldScript::MaximumMessageBytes
                    && mPresentation.size() < WorldScript::MaximumMessages, "World script presentation budget exceeded");
                mPresentation.emplace_back(message);
            }
            [[noreturn]] static void unavailable()
            {
                throw std::invalid_argument("World script requires unavailable player/reference context");
            }
        public:
            Context(Locals& locals, std::vector<ESM::Global>& globals, std::vector<std::string>& presentation)
                : InterpreterContext(&locals, {}), mGlobals(globals), mPresentation(presentation) {}
            Context(Locals& locals, std::vector<ESM::Global>& globals, std::vector<std::string>& presentation,
                const ExecutionContext& execution, const MWWorld::ESMStore& content, const ReferenceResolver& resolver,
                ScriptState* bank, DialogueState* dialogue, MWMechanics::NpcStats* sourceNpcStats, ReferenceState* referenceState,
                bool sourceClock)
                : InterpreterContext(&locals, execution, content, resolver, nullptr, bank, dialogue, sourceNpcStats, referenceState), mGlobals(globals), mPresentation(presentation), mSourceClock(sourceClock) {}

            float getSecondsPassed() const override
            { require(mSourceClock, "Story clock not admitted"); return 1.f / 30; }

            int getGlobalShort(std::string_view id) const override { return global(id).getInteger(); }
            int getGlobalLong(std::string_view id) const override { return global(id).getInteger(); }
            float getGlobalFloat(std::string_view id) const override { return global(id).getFloat(); }
            void setGlobalShort(std::string_view id, int value) override { global(id).setInteger(value); }
            void setGlobalLong(std::string_view id, int value) override { global(id).setInteger(value); }
            void setGlobalFloat(std::string_view id, float value) override { global(id).setFloat(value); }
            char getGlobalType(std::string_view id) const override
            { const auto* found = find(mGlobals, id); return found ? type(found->mValue) : ' '; }
            std::vector<std::string> getGlobals() const override
            {
                std::vector<std::string> result;
                for (const auto& global : mGlobals) result.emplace_back(global.mId.getRefIdString());
                return result;
            }
            void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
            { require(buttons.empty(), "Interactive world script presentation unavailable"); present(message); }
            void report(const std::string& message) override { present(message); }

            std::string getActionBinding(std::string_view) const override { unavailable(); }
            std::string_view getActorName() const override { unavailable(); }
            std::string_view getNPCRace() const override { unavailable(); }
            std::string_view getNPCClass() const override { unavailable(); }
            std::string_view getNPCFaction() const override { unavailable(); }
            std::string_view getNPCRank() const override { unavailable(); }
            std::string_view getPCName() const override { unavailable(); }
            std::string_view getPCRace() const override { unavailable(); }
            std::string_view getPCClass() const override { unavailable(); }
            std::string_view getPCRank() const override { unavailable(); }
            std::string_view getPCNextRank() const override { unavailable(); }
            int getPCBounty() const override { unavailable(); }
            std::string_view getCurrentCellName() const override { unavailable(); }
        };
        class CompilerContext final : public Compiler::Context
        {
            const std::vector<ESM::Global>& mGlobals;
            const std::vector<std::pair<ESM::RefId, Compiler::Locals>>& mMembers;
            ESM::RefId mReference;
            bool mAllowPlayer = false;
        public:
            CompilerContext(const std::vector<ESM::Global>& globals,
                const std::vector<std::pair<ESM::RefId, Compiler::Locals>>& members, const ESM::RefId& reference = {},
                bool allowPlayer = false)
                : mGlobals(globals), mMembers(members), mReference(reference), mAllowPlayer(allowPlayer) {}
            bool canDeclareLocals() const override { return true; }
            char getGlobalType(const std::string& id) const override
            { const auto* found = find(mGlobals, id); return found ? type(found->mValue) : ' '; }
            std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const override
            {
                for (const auto& [script, locals] : mMembers)
                    if (script == id) return {locals.getType(name), false};
                return {' ', false};
            }
            bool isId(const ESM::RefId& id) const override
            { return (!mReference.empty() && (id == mReference || (mAllowPlayer && id == "player")))
                || std::ranges::any_of(mMembers, [&](const auto& entry) { return entry.first == id; }); }
        };
        class Errors final : public Compiler::ErrorHandler
        {
            // Never log source text or let unsupported content expand diagnostics.
            void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override
            {
                if (first.empty())
                    first = "line " + std::to_string(loc.mLine + 1) + ": " + message.substr(0, 64);
            }
            void report(const std::string& message, Type type) override
            { if (first.empty()) first = message.substr(0, 64); }
        public:
            std::string first;
        };
        void numeric(const ESM::Variant& value)
        {
            const auto kind = type(value);
            const float number = value.getFloat();
            require(std::isfinite(number), "Nonfinite world global");
            if (kind != 'f')
            {
                require(double(number) >= std::numeric_limits<int32_t>::min()
                    && double(number) <= std::numeric_limits<int32_t>::max(), "World global integer exceeds range");
                // Stock GLOB stores integers as FLTV. Do not acknowledge a value
                // whose stock serialization would truncate or round on recovery.
                require(static_cast<int32_t>(number) == value.getInteger()
                    && (kind != 's' || (value.getInteger() >= std::numeric_limits<int16_t>::min()
                        && value.getInteger() <= std::numeric_limits<int16_t>::max())),
                    "World global cannot round-trip through stock serialization");
            }
        }
    }

    WorldScript::WorldScript(const ESM::Script& script, std::vector<ESM::Global> ownedGlobals,
        std::span<const ESM::Script> bank, const ESM::RefId& sourceRecord, bool lifeEvents)
        : mId(script.mId), mGlobals(std::move(ownedGlobals)), mBank(!bank.empty()), mSource(sourceRecord), mLifeEvents(lifeEvents)
    {
        require(mId.is<ESM::StringRefId>(), "World script ID must be a content string");
        name(mId.getRefIdString());
        require(!script.mScriptText.empty() && script.mScriptText.size() <= MaximumSourceBytes
            && script.mScriptText.find('\0') == std::string::npos, "World script source exceeds bounds");
        require(mGlobals.size() <= MaximumGlobals, "World global budget exceeded");
        for (std::size_t i = 0; i < mGlobals.size(); ++i)
        {
            const auto& global = mGlobals[i];
            require(global.mId.is<ESM::StringRefId>() && global.mRecordFlags == 0, "Invalid owned world global");
            name(global.mId.getRefIdString());
            numeric(global.mValue);
            for (std::size_t j = 0; j < i; ++j)
                require(global.mId != mGlobals[j].mId, "Duplicate world global ownership");
        }
        require(bank.size() <= 4, "Script bank exceeds bounds");
        std::vector<std::pair<ESM::RefId, Compiler::Locals>> members;
        require(mSource.empty() || (mBank && mSource.is<ESM::StringRefId>()), "Source query requires a script bank");
        if (!mSource.empty()) name(mSource.getRefIdString());
        CompilerContext context(mGlobals, members, mSource);
        for (const auto& member : bank)
        {
            require(member.mId.is<ESM::StringRefId>(), "Invalid bank script identity");
            name(member.mId.getRefIdString());
            require(!member.mScriptText.empty() && member.mScriptText.size() <= MaximumSourceBytes
                && member.mScriptText.find('\0') == std::string::npos, "Bank script source exceeds bounds");
            require(std::ranges::none_of(members, [&](const auto& entry) { return entry.first == member.mId; }),
                "Repeated bank script");
            Compiler::Locals locals;
            Errors declarations;
            Compiler::QuickFileParser parser(declarations, context, locals);
            std::istringstream source(member.mScriptText);
            Compiler::Scanner scanner(declarations, source, nullptr);
            scanner.scan(parser);
            require(declarations.isGood(), "Invalid bank declarations");
            size_t count = 0;
            for (const char kind : {'s', 'l', 'f'})
                for (const auto& local : std::as_const(locals).get(kind)) { name(local); ++count; }
            require(count <= MaximumLocals, "Bank locals exceed bounds");
            members.emplace_back(member.mId, std::move(locals));
        }
        require(!mBank || std::ranges::any_of(members, [&](const auto& entry) { return entry.first == mId; }),
            "Caller lacks bank ownership");
        mMembers = members;
        require(!mLifeEvents || (mBank && !mSource.empty()), "Life events require a source bank");
        Compiler::Extensions extensions;
        if (mBank)
        {
            Misc::registerScriptOpcodes(extensions);
            if (!mSource.empty()) Misc::registerSourceWaitOpcodes(extensions);
            if (!mSource.empty()) Stats::registerHealthOpcodes(extensions);
            if (!mSource.empty()) Transformation::registerPositionOpcodes(extensions, false);
            if (mLifeEvents) Stats::registerDeathOpcodes(extensions);
            context.setExtensions(&extensions);
        }
        Errors errors;
        // Stock compilation can warn and skip unknown instructions. Admission
        // must not turn an unsupported script into a successful no-op.
        errors.setWarningsMode(2);
        Compiler::FileParser parser(errors, context);
        std::istringstream source(script.mScriptText);
        Compiler::Scanner scanner(errors, source, context.getExtensions());
        bool compiled = true;
        try { scanner.scan(parser); }
        catch (const Compiler::SourceException&) { compiled = false; }
        if (!compiled || !errors.isGood() || errors.countWarnings() != 0)
            throw std::invalid_argument("World script " + std::string(mId.getRefIdString()) + ": " + errors.first);
        mDeclarations = parser.getLocals();
        mProgram = parser.getProgram();
        require(mProgram.mInstructions.size() <= MaximumInstructions,
            "World script program exceeds bounds");
        std::size_t count = 0;
        for (const char kind : {'s', 'l', 'f'})
            for (const auto& local : std::as_const(mDeclarations).get(kind)) { name(local); ++count; }
        require(count <= MaximumLocals, "World script locals exceed bounds");
        for (const auto& literal : mProgram.mStrings)
            require(literal.size() <= MaximumMessageBytes && literal.find_first_of("%^") == std::string::npos,
                "World script supports bounded literal presentation only");
    }

    WorldScript::State WorldScript::initial() const
    {
        State state{{}, mGlobals};
        state.script.mId = mId;
        state.script.mRunning = true;
        for (const char kind : {'s', 'l', 'f'})
            for (const auto& local : mDeclarations.get(kind))
            {
                ESM::Variant zero;
                zero.setType(kind == 'f' ? ESM::VT_Float : ESM::VT_Int);
                state.script.mLocals.mVariables.emplace_back(local, zero);
            }
        validate(state);
        return state;
    }

    void WorldScript::validate(const State& state) const
    {
        require(state.script.mId == mId && (mBank || state.script.mRunning) && state.script.mTargetId.empty()
            && !state.script.mTargetRef.isSet(), "World script state/context mismatch");
        require(state.script.mLocals.mVariables.size() <= MaximumLocals, "World script locals exceed bounds");
        Locals::validateRestore(state.script.mLocals, mDeclarations);
        require(state.globals.size() == mGlobals.size(), "World global ownership mismatch");
        for (std::size_t i = 0; i < mGlobals.size(); ++i)
        {
            require(state.globals[i].mId == mGlobals[i].mId && state.globals[i].mRecordFlags == 0
                && state.globals[i].mValue.getType() == mGlobals[i].mValue.getType(), "World global binding mismatch");
            numeric(state.globals[i].mValue);
        }
    }

    WorldScript::Invocation WorldScript::execute(const State& before, Interpreter::ExecutionTrace* trace) const
    { return executeImpl(before, trace, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }

    WorldScript::Invocation WorldScript::execute(const State& before, const ExecutionContext& execution,
        const MWWorld::ESMStore& content, const ReferenceResolver& resolver, Interpreter::ExecutionTrace* trace,
        ScriptState* bank, GlobalScriptDesc* caller, DialogueState* dialogue, const Interpreter::Program* result,
        MWMechanics::NpcStats* sourceNpcStats, ReferenceState* referenceState) const
    { return executeImpl(before, trace, &execution, &content, &resolver, bank, caller, dialogue, result, sourceNpcStats, referenceState); }

    WorldScript::Invocation WorldScript::executeImpl(const State& before, Interpreter::ExecutionTrace* trace,
        const ExecutionContext* execution, const MWWorld::ESMStore* content, const ReferenceResolver* resolver,
        ScriptState* bank, GlobalScriptDesc* caller, DialogueState* dialogue, const Interpreter::Program* program,
        MWMechanics::NpcStats* sourceNpcStats, ReferenceState* referenceState) const
    {
        validate(before);
        Invocation result{before, {}};
        require(mBank ? (execution && bank && caller) : (!bank && !caller), "Bank execution lacks staged ownership");
        if (mBank)
        {
            const auto binding = bank->resolve(*execution, mId);
            const auto* target = std::get_if<MWWorld::Ptr>(&caller->mTarget);
            require(&binding.instance == caller && &binding.declarations == &mDeclarations
                && caller->mLocals.getScriptId() == mId && (program || (caller->mRunning && before.script.mRunning))
                && target && target->isEmpty(), "Bank caller/target mismatch");
        }
        auto restored = Locals::restore(before.script.mLocals, mId, mDeclarations);
        auto& locals = caller ? caller->mLocals : restored;
        auto context = execution
            ? std::make_unique<Context>(locals, result.state.globals, result.presentation, *execution, *content, *resolver, bank, dialogue, sourceNpcStats, referenceState, !mSource.empty())
            : std::make_unique<Context>(locals, result.state.globals, result.presentation);
        Interpreter::Interpreter interpreter;
        Interpreter::installOpcodes(interpreter);
        if (mBank) Misc::installScriptOpcodes(interpreter);
        if (!mSource.empty()) Misc::installSourceWaitOpcodes(interpreter, !referenceState);
        if (mLifeEvents) Stats::installDeathOpcodes(interpreter);
        if (!mSource.empty()) Stats::installHealthOpcodes(interpreter);
        if (dialogue) Dialogue::installJournalOpcodes(interpreter);
        if (sourceNpcStats) Stats::installDispositionResultOpcode(interpreter);
        if (referenceState) Misc::installDisableOpcodes(interpreter);
        if (referenceState) Transformation::installPositionOpcodes(interpreter);
        if (referenceState) Ai::installActivityOpcodes(interpreter);
        // Every admitted opcode is stock core bytecode. At most one stack value
        // is allocated per executed instruction; loops and presentation are bounded.
        interpreter.run(program ? *program : mProgram, *context, MaximumExecutedInstructions, trace);
        result.state.script.mLocals.mVariables.clear();
        require(locals.write(result.state.script.mLocals, mDeclarations), "World script locals unavailable");
        if (caller) result.state.script.mRunning = caller->mRunning;
        validate(result.state);
        return result;
    }

    Interpreter::Program WorldScript::compileResult(std::string_view text, bool physical, const ESM::RefId& reference,
        bool restoration, bool relocation, bool activity) const
    {
        require(mBank && text.size() <= MaximumSourceBytes && text.find('\0') == std::string_view::npos,
            "Dialogue result source exceeds bounds or lacks bank");
        CompilerContext context(mGlobals, mMembers, physical ? reference : ESM::RefId{}, physical);
        Compiler::Extensions extensions;
        Misc::registerScriptOpcodes(extensions);
        Compiler::Dialogue::registerExtensions(extensions);
        Stats::registerDispositionResultOpcode(extensions);
        if (physical) Misc::registerDisableOpcodes(extensions);
        require(!restoration || (physical && !reference.empty()), "Restoration lacks bounded source");
        if (restoration) Misc::registerEnableOpcodes(extensions);
        require(!relocation || (physical && !reference.empty()), "Relocation lacks bounded source");
        if (relocation) Transformation::registerPositionOpcodes(extensions);
        require(!activity || (physical && !reference.empty()), "Activity lacks bounded source");
        if (activity) Ai::registerActivityOpcodes(extensions);
        context.setExtensions(&extensions);
        std::string source = "begin " + std::string(mId.getRefIdString()) + "\n";
        for (const auto kind : {'s', 'l', 'f'})
            for (const auto& local : mDeclarations.get(kind))
                source += std::string(kind == 's' ? "short " : kind == 'l' ? "long " : "float ") + local + "\n";
        source.append(text); source += "\nend\n";
        Errors errors; errors.setWarningsMode(2);
        Compiler::FileParser parser(errors, context);
        std::istringstream stream(source);
        Compiler::Scanner scanner(errors, stream, &extensions);
        try { scanner.scan(parser); }
        catch (const Compiler::SourceException&) { throw std::invalid_argument("Invalid dialogue result: " + errors.first); }
        if (!errors.isGood() || errors.countWarnings())
            throw std::invalid_argument("Invalid dialogue result: " + errors.first);
        // Result blocks cannot introduce or alter the actor's declarations.
        for (const char kind : {'s', 'l', 'f'})
            require(parser.getLocals().get(kind) == mDeclarations.get(kind), "Dialogue result changed declarations");
        auto program = parser.getProgram();
        require(program.mInstructions.size() <= MaximumInstructions, "Dialogue result program exceeds bounds");
        for (const auto& literal : program.mStrings)
            require(literal.size() <= MaximumMessageBytes && literal.find_first_of("%^") == std::string::npos,
                "Dialogue result literal exceeds bounds");
        return program;
    }
}
