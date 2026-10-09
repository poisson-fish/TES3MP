#ifndef GAME_SCRIPT_INTERPRETERCONTEXT_H
#define GAME_SCRIPT_INTERPRETERCONTEXT_H

#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>

#include <components/esm/refid.hpp>
#include <components/interpreter/context.hpp>

#include "globalscripts.hpp"
#include "executioncontext.hpp"
#include "scriptstate.hpp"

#include "../mwworld/ptr.hpp"

namespace MWWorld { class ESMStore; }
namespace MWMechanics { class NpcStats; }

namespace MWScript
{
    class Locals;
    class DialogueState;
    class ReferenceState;

    class MissingImplicitRefError : public std::runtime_error
    {
    public:
        MissingImplicitRefError();
    };

    class InterpreterContext : public Interpreter::Context
    {
        Locals* mLocals;
        mutable MWWorld::Ptr mReference;
        MWWorld::Ptr mPlayer;
        MWMechanics::NpcStats* mPlayerStats = nullptr;
        MWMechanics::NpcStats* mSourceNpcStats = nullptr;
        const MWWorld::ESMStore* mContent = nullptr;
        bool mExplicitPlayer = false;
        std::optional<ExecutionContext> mExecution;
        const ReferenceResolver* mResolver = nullptr;
        ScriptState* mScriptState = nullptr;
        DialogueState* mDialogueState = nullptr;
        ReferenceState* mReferenceState = nullptr;
        std::shared_ptr<GlobalScriptDesc> mGlobalScriptDesc;

        void requireUnscopedService() const;
        ScriptState::Binding getScopedScript(const ESM::RefId& id) const;
        struct ScopedMember
        {
            Locals& locals;
            size_t index;
        };
        ScopedMember getScopedMember(const ESM::RefId& id, std::string_view name, char type, bool global) const;

        /// If \a id is empty, a reference the script is run from is returned or in case
        /// of a non-local script the reference derived from the target ID.
        const MWWorld::Ptr getReferenceImp(
            const ESM::RefId& id = ESM::RefId(), bool activeOnly = false, bool doThrow = true) const;

        const Locals& getMemberLocals(bool global, ESM::RefId& id) const;
        ///< \a id is changed to the respective script ID, if \a id wasn't a script ID before

        Locals& getMemberLocals(bool global, ESM::RefId& id);
        ///< \a id is changed to the respective script ID, if \a id wasn't a script ID before

        /// Throws an exception if local variable can't be found.
        int findLocalVariableIndex(const ESM::RefId& scriptId, std::string_view name, char type) const;

    public:
        InterpreterContext(std::shared_ptr<GlobalScriptDesc> globalScriptDesc);

        InterpreterContext(MWScript::Locals* locals, const MWWorld::Ptr& reference);
        ///< The ownership of \a locals is not transferred. 0-pointer allowed.

        InterpreterContext(MWScript::Locals* locals, const MWWorld::Ptr& reference,
            const MWWorld::Ptr& player, const MWWorld::ESMStore& content,
            MWMechanics::NpcStats* detachedPlayerStats = nullptr);
        ///< Execute player-facing opcodes for this character. An empty player is invalid.

        InterpreterContext(MWScript::Locals* locals, ExecutionContext execution,
            const MWWorld::ESMStore& content, const ReferenceResolver& resolver,
            MWMechanics::NpcStats* detachedPlayerStats = nullptr, ScriptState* scriptState = nullptr,
            DialogueState* dialogueState = nullptr, MWMechanics::NpcStats* sourceNpcStats = nullptr,
            ReferenceState* referenceState = nullptr);
        ///< Scoped callers supply staged locals/stats. Unprovided services reject;
        ///< no fallback to the stock protagonist, world globals or script manager.

        const ExecutionContext* getExecutionContext() const
        { return mExecution ? &*mExecution : nullptr; }
        MWWorld::Ptr resolveReference(const ESM::RefId& id, bool required = true, bool activeOnly = false) const;
        MWWorld::Ptr resolveReferenceName(std::string_view name, bool required = true, bool activeOnly = false) const;

        MWWorld::Ptr getPlayer() const;
        MWMechanics::NpcStats& getPlayerStats() const;
        // Only the validated invocation source may change detached NPC state.
        // Stock callers retain the ordinary NPC writer.
        void modDisposition(const MWWorld::Ptr& actor, int value) const;
        void enable(const MWWorld::Ptr& reference) const;
        void disable(const MWWorld::Ptr& reference) const;
        bool disabled(const MWWorld::Ptr& reference) const;
        void position(const MWWorld::Ptr&, const std::array<float, 3>&, float, const ESM::RefId&) const;
        const ESM::Position& position(const MWWorld::Ptr&) const;
        const MWWorld::ESMStore& getContent() const;
        bool hasExplicitPlayer() const { return mExplicitPlayer; }
        DialogueState* getDialogueState() const;
        std::optional<std::variant<int, float>> getDialogueLocal(const ESM::RefId& script, std::string_view name) const;

        // Stock script lifecycle calls; scoped calls only change an admitted
        // source-free instance's staged running flag, never invoke it inline.
        bool scriptRunning(std::string_view name) const;
        void startScript(std::string_view name, const MWWorld::Ptr& target);
        void stopScript(std::string_view name);

        ESM::RefId getTarget() const override;

        int getLocalShort(int index) const override;

        int getLocalLong(int index) const override;

        float getLocalFloat(int index) const override;

        void setLocalShort(int index, int value) override;

        void setLocalLong(int index, int value) override;

        void setLocalFloat(int index, float value) override;

        using Interpreter::Context::messageBox;

        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override;

        void report(const std::string& message) override;
        ///< By default, do nothing.

        int getGlobalShort(std::string_view name) const override;

        int getGlobalLong(std::string_view name) const override;

        float getGlobalFloat(std::string_view name) const override;

        void setGlobalShort(std::string_view name, int value) override;

        void setGlobalLong(std::string_view name, int value) override;

        void setGlobalFloat(std::string_view name, float value) override;

        std::vector<std::string> getGlobals() const override;

        char getGlobalType(std::string_view name) const override;

        std::string getActionBinding(std::string_view action) const override;

        std::string_view getActorName() const override;

        std::string_view getNPCRace() const override;

        std::string_view getNPCClass() const override;

        std::string_view getNPCFaction() const override;

        std::string_view getNPCRank() const override;

        std::string_view getPCName() const override;

        std::string_view getPCRace() const override;

        std::string_view getPCClass() const override;

        std::string_view getPCRank() const override;

        std::string_view getPCNextRank() const override;

        int getPCBounty() const override;

        std::string_view getCurrentCellName() const override;
        virtual float getSecondsPassed() const;

        void executeActivation(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor);
        ///< Execute the activation action for this ptr. If ptr is mActivated, mark activation as handled.

        ESM::RefId getMemberId(std::string_view name) const override;
        int getMemberShort(ESM::RefId id, std::string_view name, bool global) const override;

        int getMemberLong(ESM::RefId id, std::string_view name, bool global) const override;

        float getMemberFloat(ESM::RefId id, std::string_view name, bool global) const override;

        void setMemberShort(ESM::RefId id, std::string_view name, int value, bool global) override;

        void setMemberLong(ESM::RefId id, std::string_view name, int value, bool global) override;

        void setMemberFloat(ESM::RefId id, std::string_view name, float value, bool global) override;

        MWWorld::Ptr getReference(bool required = true) const;
        ///< Reference, that the script is running from (can be empty)

        void updatePtr(const MWWorld::Ptr& base, const MWWorld::Ptr& updated);
        ///< Update the Ptr stored in mReference, if there is one stored there. Should be called after the reference has
        ///< been moved to a new cell.
    };
}

#endif
