#include "interpretercontext.hpp"
#include "referencestate.hpp"

#include <cmath>
#include <sstream>

#include <components/compiler/locals.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/records.hpp>

#include "../mwworld/esmstore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/scene.hpp"

#include "../mwmechanics/npcstats.hpp"

#include "globalscripts.hpp"
#include "locals.hpp"
#include "dialoguestate.hpp"

namespace MWScript
{
    namespace
    {
        void scriptStateName(std::string_view name)
        {
            if (name.empty() || name.size() > ScriptState::MaximumNameBytes
                || name.find('\0') != std::string_view::npos)
                throw std::invalid_argument("Script state name exceeds bounds");
        }
    }

    const MWWorld::Ptr InterpreterContext::getReferenceImp(const ESM::RefId& id, bool activeOnly, bool doThrow) const
    {
        if (mExecution)
        {
            if (!id.empty() && (!id.is<ESM::StringRefId>()
                || id.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
                || id.getRefIdString().find('\0') != std::string_view::npos))
                throw std::invalid_argument("Script reference name exceeds bounds");
            if (id == "player" && mExecution->ownership != ExecutionContext::Ownership::Character)
                throw std::runtime_error("Shared script has no character context");
            if (id.empty() && !mExecution->sourceReference)
            {
                if (doThrow) throw MissingImplicitRefError();
                return {};
            }
            const auto ptr = mResolver->resolve(*mExecution, id, activeOnly);
            if (!ptr.isEmpty() && !ptr.hasLiveReference())
                throw std::runtime_error("Stale script reference context");
            if (ptr.isEmpty() && doThrow)
                throw std::runtime_error("Script reference absent from scope");
            return ptr;
        }
        if (!id.empty())
        {
            if (mExplicitPlayer && id == "player")
                return getPlayer();
            // Detached migration callers have no authority for other lookups.
            if (mExplicitPlayer)
                throw std::runtime_error("Script reference lacks scoped resolver");
            const auto world = MWBase::Environment::get().getWorld();
            return doThrow ? world->getPtr(id, activeOnly) : world->searchPtr(id, activeOnly);
        }
        else
        {
            if (mReference.isEmpty() && mGlobalScriptDesc)
                mReference = mGlobalScriptDesc->getPtr();

            if (mReference.isEmpty() && doThrow)
                throw MissingImplicitRefError();

            return mReference;
        }
    }

    const Locals& InterpreterContext::getMemberLocals(bool global, ESM::RefId& id) const
    {
        requireUnscopedService();
        if (global)
        {
            return MWBase::Environment::get().getScriptManager()->getGlobalScripts().getLocals(id);
        }
        else
        {
            const MWWorld::Ptr ptr = getReferenceImp(id, false);

            id = ptr.getClass().getScript(ptr);

            ptr.getRefData().setLocals(*MWBase::Environment::get().getESMStore()->get<ESM::Script>().find(id));

            return ptr.getRefData().getLocals();
        }
    }

    Locals& InterpreterContext::getMemberLocals(bool global, ESM::RefId& id)
    {
        requireUnscopedService();
        if (global)
        {
            return MWBase::Environment::get().getScriptManager()->getGlobalScripts().getLocals(id);
        }
        else
        {
            const MWWorld::Ptr ptr = getReferenceImp(id, false);

            id = ptr.getClass().getScript(ptr);

            ptr.getRefData().setLocals(*MWBase::Environment::get().getESMStore()->get<ESM::Script>().find(id));

            return ptr.getRefData().getLocals();
        }
    }

    MissingImplicitRefError::MissingImplicitRefError()
        : std::runtime_error("no implicit reference")
    {
    }

    int InterpreterContext::findLocalVariableIndex(const ESM::RefId& scriptId, std::string_view name, char type) const
    {
        requireUnscopedService();
        int index = MWBase::Environment::get().getScriptManager()->getLocals(scriptId).searchIndex(type, name);

        if (index != -1)
            return index;

        std::ostringstream stream;

        stream << "Failed to access ";

        switch (type)
        {
            case 's':
                stream << "short";
                break;
            case 'l':
                stream << "long";
                break;
            case 'f':
                stream << "float";
                break;
        }

        stream << " member variable " << name << " in script " << scriptId;

        throw std::runtime_error(stream.str().c_str());
    }

    InterpreterContext::InterpreterContext(MWScript::Locals* locals, const MWWorld::Ptr& reference)
        : mLocals(locals)
        , mReference(reference)
    {
    }

    InterpreterContext::InterpreterContext(MWScript::Locals* locals, const MWWorld::Ptr& reference,
        const MWWorld::Ptr& player, const MWWorld::ESMStore& content,
        MWMechanics::NpcStats* detachedPlayerStats)
        : mLocals(locals)
        , mReference(reference)
        , mPlayer(player)
        , mPlayerStats(detachedPlayerStats)
        , mContent(&content)
        , mExplicitPlayer(true)
    {
        if (player.isEmpty())
            throw std::invalid_argument("script player context is empty");
    }

    MWWorld::Ptr InterpreterContext::getPlayer() const
    {
        if (mExecution)
            return resolveReference(ESM::RefId::stringRefId("player"));
        if (mExplicitPlayer)
        {
            if (!mPlayer.hasLiveReference())
                throw std::runtime_error("Stale script player context");
            return mPlayer;
        }
        return MWBase::Environment::get().getWorld()->getPlayerPtr();
    }

    MWMechanics::NpcStats& InterpreterContext::getPlayerStats() const
    {
        // Validate ownership and the current character even with staged stats.
        const MWWorld::Ptr player = getPlayer();
        if (mPlayerStats)
            return *mPlayerStats;
        if (mExecution)
            throw std::runtime_error("Script character stats are not staged");
        return player.getClass().getNpcStats(player);
    }

    const MWWorld::ESMStore& InterpreterContext::getContent() const
    {
        return mContent ? *mContent : *MWBase::Environment::get().getESMStore();
    }

    InterpreterContext::InterpreterContext(MWScript::Locals* locals, ExecutionContext execution,
        const MWWorld::ESMStore& content, const ReferenceResolver& resolver,
        MWMechanics::NpcStats* detachedPlayerStats, ScriptState* scriptState, DialogueState* dialogueState,
        MWMechanics::NpcStats* sourceNpcStats, ReferenceState* referenceState)
        : mLocals(locals)
        , mPlayerStats(detachedPlayerStats)
        , mSourceNpcStats(sourceNpcStats)
        , mContent(&content)
        , mExplicitPlayer(execution.ownership == ExecutionContext::Ownership::Character)
        , mExecution(execution)
        , mResolver(&resolver)
        , mScriptState(scriptState)
        , mDialogueState(dialogueState)
        , mReferenceState(referenceState)
    {
        execution.validate();
        if (dialogueState) dialogueState->validateExecution(execution);
        if (detachedPlayerStats && execution.ownership != ExecutionContext::Ownership::Character)
            throw std::invalid_argument("Shared script cannot own character stats");
        if (sourceNpcStats && (execution.ownership != ExecutionContext::Ownership::Character
            || !execution.sourceReference || !dialogueState))
            throw std::invalid_argument("NPC result lacks character/story dialogue ownership");
    }

    void InterpreterContext::modDisposition(const MWWorld::Ptr& actor, int value) const
    {
        MWMechanics::NpcStats* stats = nullptr;
        if (mExecution)
        {
            if (!mSourceNpcStats || !actor.hasLiveReference() || actor.getType() != ESM::NPC::sRecordId
                || actor != resolveReference({}))
                throw std::invalid_argument("NPC result lacks staged source ownership");
            stats = mSourceNpcStats;
        }
        else if (actor.getClass().isNpc())
            stats = &actor.getClass().getNpcStats(actor);
        // Stock ModDisposition is a no-op for creatures.
        if (!stats) return;
        const int64_t disposition = int64_t(stats->getBaseDisposition()) + value;
        if (disposition < INT32_MIN || disposition > INT32_MAX)
            throw std::invalid_argument("NPC disposition exceeds stock integer range");
        stats->setBaseDisposition(int(disposition));
    }

    void InterpreterContext::modDisposition(std::string_view name, int value) const
    {
        if (!mExecution)
        {
            modDisposition(resolveReferenceName(name), value);
            return;
        }
        scriptStateName(name);
        const ESM::RefId id = ESM::RefId::stringRefId(name);
        if (!mSourceNpcStats || !mDialogueState || id == "player")
            throw std::invalid_argument("NPC result lacks staged source ownership");
        const auto source = resolveReference({});
        if (!source.hasLiveReference() || source.getType() != ESM::NPC::sRecordId)
            throw std::invalid_argument("NPC result lacks staged source ownership");
        if (id == source.getCellRef().getRefId())
        {
            modDisposition(source, value);
            return;
        }
        const auto player = resolveReference(ESM::RefId::stringRefId("player"), false);
        if (!player.isEmpty() && player.getCellRef().getRefId() == id)
            throw std::invalid_argument("NPC result lacks staged source ownership");
        const auto* record = getContent().get<ESM::NPC>().search(id);
        if (!record)
            throw std::invalid_argument("Foreign NPC result lacks NPC record");
        mDialogueState->modNpcDisposition(record->mId, record->mNpdt.mDisposition, value);
    }

    void InterpreterContext::requireUnscopedService() const
    {
        if (mExecution)
            throw std::runtime_error("Script service lacks scoped transaction");
    }

    void InterpreterContext::aiTravel(const MWWorld::Ptr& reference, const std::array<float, 3>& destination,
        bool repeat, const ESM::RefId& follower, float duration) const
    {
        if (!mExecution || !mReferenceState) throw std::invalid_argument("AI package lacks staged activity scope");
        mReferenceState->aiTravel(*mExecution, reference, destination, repeat, follower, duration);
    }

    void InterpreterContext::aiPackage(const MWWorld::Ptr& reference, int packageType,
        const std::array<float, 3>& destination, bool repeat, const ESM::RefId& targetActor, float duration,
        std::string_view cellName) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || cellName.size() > ExecutionContext::MaximumReferenceNameBytes
            || cellName.find('\0') != std::string_view::npos)
            throw std::invalid_argument("AI package lacks staged activity scope");
        mReferenceState->aiPackage(*mExecution, reference, packageType, destination, repeat, targetActor, duration, cellName);
    }

    bool InterpreterContext::aiPackageDone(const MWWorld::Ptr& reference) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference())
            throw std::invalid_argument("GetAiPackageDone lacks staged activity scope");
        return mReferenceState->aiPackageDone(*mExecution, reference);
    }

    int InterpreterContext::currentAiPackage(const MWWorld::Ptr& reference) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference())
            throw std::invalid_argument("GetCurrentAIPackage lacks staged activity scope");
        return mReferenceState->currentAiPackage(*mExecution, reference);
    }

    bool InterpreterContext::pcCell(std::string_view name) const
    {
        if (!mExecution || !mReferenceState || name.empty()
            || name.size() > ExecutionContext::MaximumReferenceNameBytes
            || name.find('\0') != std::string_view::npos)
            throw std::invalid_argument("GetPCCell lacks staged cell scope");
        return mReferenceState->pcCell(*mExecution, name);
    }

    float InterpreterContext::distance(const MWWorld::Ptr& from, const ESM::RefId& toId) const
    {
        if (!mExecution || !mReferenceState || from.isEmpty() || !from.hasLiveReference()
            || !toId.is<ESM::StringRefId>() || toId.empty()
            || toId.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || toId.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("GetDistance lacks staged position scope");
        return mReferenceState->distance(*mExecution, from, toId);
    }

    void InterpreterContext::forceGreeting(const MWWorld::Ptr& reference) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference())
            throw std::invalid_argument("ForceGreeting lacks staged dialogue scope");
        mReferenceState->forceGreeting(*mExecution, reference);
    }

    void InterpreterContext::goodbye() const
    {
        if (!mExecution || !mReferenceState)
            throw std::invalid_argument("Goodbye lacks staged dialogue scope");
        mReferenceState->goodbye(*mExecution);
    }

    const ESM::Position& InterpreterContext::position(const MWWorld::Ptr& reference) const
    {
        if (!mExecution) return reference.getRefData().getPosition();
        if (!mReferenceState) throw std::invalid_argument("Position read lacks staged physical scope");
        return mReferenceState->position(*mExecution, reference);
    }

    bool InterpreterContext::cellChanged() const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("CellChanged lacks staged physical scope");
            return mReferenceState->cellChanged(*mExecution);
        }
        return MWBase::Environment::get().getWorldScene()->hasCellChanged();
    }

    bool InterpreterContext::menuMode() const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("MenuMode lacks staged physical scope");
            return mReferenceState->menuMode(*mExecution);
        }
        return MWBase::Environment::get().getWindowManager()->isGuiMode();
    }

    bool InterpreterContext::playerSleeping() const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("GetPCSleep lacks staged physical scope");
            return mReferenceState->playerSleeping(*mExecution);
        }
        return MWBase::Environment::get().getWindowManager()->getPlayerSleeping();
    }

    bool InterpreterContext::onActivate(const MWWorld::Ptr& reference) const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("OnActivate lacks staged physical scope");
            return mReferenceState->onActivate(*mExecution, reference);
        }
        return reference.getRefData().onActivate();
    }

    void InterpreterContext::activate(const MWWorld::Ptr& reference) const
    {
        if (!mExecution || !mReferenceState) throw std::invalid_argument("Activate lacks staged physical scope");
        mReferenceState->activate(*mExecution, reference);
    }

    int InterpreterContext::itemCount(const MWWorld::Ptr& reference, const ESM::RefId& item) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !item.is<ESM::StringRefId>() || item.empty()
            || item.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || item.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("GetItemCount lacks scoped inventory transaction");
        return mReferenceState->itemCount(*mExecution, reference, item);
    }

    void InterpreterContext::addItem(const MWWorld::Ptr& reference, const ESM::RefId& item, int count) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !item.is<ESM::StringRefId>() || item.empty()
            || item.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || item.getRefIdString().find('\0') != std::string_view::npos || count <= 0)
            throw std::invalid_argument("AddItem lacks scoped inventory transaction");
        mReferenceState->addItem(*mExecution, reference, item, count);
    }

    void InterpreterContext::removeItem(const MWWorld::Ptr& reference, const ESM::RefId& item, int count) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !item.is<ESM::StringRefId>() || item.empty()
            || item.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || item.getRefIdString().find('\0') != std::string_view::npos || count <= 0)
            throw std::invalid_argument("RemoveItem lacks scoped inventory transaction");
        mReferenceState->removeItem(*mExecution, reference, item, count);
    }

    bool InterpreterContext::hasSpell(const MWWorld::Ptr& reference, const ESM::RefId& spell) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !spell.is<ESM::StringRefId>() || spell.empty()
            || spell.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || spell.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("GetSpell lacks scoped spellbook transaction");
        return mReferenceState->hasSpell(*mExecution, reference, spell);
    }

    void InterpreterContext::addSpell(const MWWorld::Ptr& reference, const ESM::RefId& spell) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !spell.is<ESM::StringRefId>() || spell.empty()
            || spell.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || spell.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("AddSpell lacks scoped spellbook transaction");
        mReferenceState->addSpell(*mExecution, reference, spell);
    }

    void InterpreterContext::removeSpell(const MWWorld::Ptr& reference, const ESM::RefId& spell) const
    {
        if (!mExecution || !mReferenceState || reference.isEmpty() || !reference.hasLiveReference()
            || !spell.is<ESM::StringRefId>() || spell.empty()
            || spell.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || spell.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("RemoveSpell lacks scoped spellbook transaction");
        mReferenceState->removeSpell(*mExecution, reference, spell);
    }

    void InterpreterContext::startCombat(const MWWorld::Ptr& actor, const ESM::RefId& target) const
    {
        if (!mExecution || !mReferenceState || actor.isEmpty() || !actor.hasLiveReference()
            || !target.is<ESM::StringRefId>() || target.empty()
            || target.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || target.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("StartCombat lacks staged combat admission");
        mReferenceState->startCombat(*mExecution, actor, target);
    }

    void InterpreterContext::stopCombat(const MWWorld::Ptr& actor) const
    {
        if (!mExecution || !mReferenceState || actor.isEmpty() || !actor.hasLiveReference())
            throw std::invalid_argument("StopCombat lacks staged combat admission");
        mReferenceState->stopCombat(*mExecution, actor);
    }

    void InterpreterContext::setAiSetting(const MWWorld::Ptr& actor, int index, int value) const
    {
        if (!mExecution || !mReferenceState || actor.isEmpty() || !actor.hasLiveReference())
            throw std::invalid_argument("AI setting write lacks staged combat admission");
        mReferenceState->setAiSetting(*mExecution, actor, index, value);
    }

    void InterpreterContext::cast(const MWWorld::Ptr& caster, const ESM::RefId& spell, const ESM::RefId& target) const
    {
        if (!mExecution || !mReferenceState || caster.isEmpty() || !caster.hasLiveReference()
            || !spell.is<ESM::StringRefId>() || spell.empty()
            || spell.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || spell.getRefIdString().find('\0') != std::string_view::npos
            || !target.is<ESM::StringRefId>() || target.empty()
            || target.getRefIdString().size() > ExecutionContext::MaximumReferenceNameBytes
            || target.getRefIdString().find('\0') != std::string_view::npos)
            throw std::invalid_argument("Cast lacks staged spellcast admission");
        mReferenceState->cast(*mExecution, caster, spell, target);
    }

    void InterpreterContext::position(const MWWorld::Ptr& reference, const std::array<float, 3>& xyz,
        float yaw, const ESM::RefId& cell) const
    {
        if (!mExecution || !mReferenceState) throw std::invalid_argument("Position lacks staged physical scope");
        mReferenceState->position(*mExecution, reference, xyz, yaw, cell);
    }

    void InterpreterContext::enable(const MWWorld::Ptr& reference) const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("Enable lacks staged physical scope");
            mReferenceState->enable(*mExecution, reference);
        }
        else MWBase::Environment::get().getWorld()->enable(reference);
    }

    void InterpreterContext::disable(const MWWorld::Ptr& reference) const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("Disable lacks staged physical scope");
            mReferenceState->disable(*mExecution, reference);
        }
        else MWBase::Environment::get().getWorld()->disable(reference);
    }

    bool InterpreterContext::onDeath(const MWWorld::Ptr& reference) const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("OnDeath lacks staged life events");
            return mReferenceState->onDeath(*mExecution, reference);
        }
        if (!reference.getClass().isActor()) return false;
        auto& stats = reference.getClass().getCreatureStats(reference);
        const bool died = stats.hasDied();
        if (died) stats.clearHasDied();
        return died;
    }

    float InterpreterContext::health(const MWWorld::Ptr& reference, bool ratio) const
    {
        if (!mExecution || !mReferenceState)
            throw std::invalid_argument("Health read lacks staged actor state");
        return mReferenceState->health(*mExecution, reference, ratio);
    }

    bool InterpreterContext::disabled(const MWWorld::Ptr& reference) const
    {
        if (mExecution)
        {
            if (!mReferenceState) throw std::invalid_argument("GetDisabled lacks staged physical scope");
            return mReferenceState->disabled(*mExecution, reference);
        }
        return !reference.getRefData().isEnabled();
    }

    DialogueState* InterpreterContext::getDialogueState() const
    {
        if (mExecution && !mDialogueState)
            throw std::runtime_error("Dialogue lacks scoped transaction");
        if (mDialogueState) mDialogueState->validateExecution(*mExecution);
        return mDialogueState;
    }

    std::optional<std::variant<int, float>> InterpreterContext::getDialogueLocal(const ESM::RefId& id, std::string_view name) const
    {
        auto binding = getScopedScript(id);
        const char kind = binding.declarations.getType(std::string(name));
        if (kind == ' ') return {};
        const auto member = getScopedMember(id, name, kind, true);
        switch (kind)
        {
            case 's': return int(member.locals.mShorts.at(member.index));
            case 'l': return member.locals.mLongs.at(member.index);
            case 'f': return member.locals.mFloats.at(member.index);
            default: throw std::invalid_argument("Invalid dialogue local type");
        }
    }

    ScriptState::Binding InterpreterContext::getScopedScript(const ESM::RefId& id) const
    {
        if (!mExecution || !mScriptState)
            throw std::runtime_error("Script state lacks scoped transaction");
        if (!id.is<ESM::StringRefId>())
            throw std::invalid_argument("Script state requires a content identity");
        scriptStateName(id.getRefIdString());
        auto binding = mScriptState->resolve(*mExecution, id);
        const auto& locals = binding.instance.mLocals;
        const auto* target = std::get_if<MWWorld::Ptr>(&binding.instance.mTarget);
        if (locals.getScriptId() != id || !target || !target->isEmpty())
            throw std::invalid_argument("Script state identity/target mismatch");
        size_t remaining = ScriptState::MaximumLocals;
        for (const char type : {'s', 'l', 'f'})
        {
            const auto& names = binding.declarations.get(type);
            const auto count = type == 's' ? locals.mShorts.size()
                : type == 'l' ? locals.mLongs.size() : locals.mFloats.size();
            if (names.size() > remaining || names.size() != count)
                throw std::invalid_argument("Script state locals exceed bounds or shape");
            remaining -= names.size();
            for (size_t i = 0; i < names.size(); ++i)
            {
                scriptStateName(names[i]);
                if (binding.declarations.getType(names[i]) != type
                    || binding.declarations.getIndex(names[i]) != static_cast<int>(i))
                    throw std::invalid_argument("Script state declarations mismatch");
            }
        }
        for (const auto value : locals.mFloats)
            if (!std::isfinite(value))
                throw std::invalid_argument("Nonfinite scoped script state");
        return binding;
    }

    InterpreterContext::ScopedMember InterpreterContext::getScopedMember(
        const ESM::RefId& id, std::string_view name, char type, bool global) const
    {
        if (!global)
            throw std::runtime_error("Reference script state is not staged");
        scriptStateName(name);
        const auto binding = getScopedScript(id);
        const auto index = binding.declarations.searchIndex(type, name);
        if (index < 0)
            throw std::invalid_argument("Missing or mistyped scoped script member");
        return {binding.instance.mLocals, static_cast<size_t>(index)};
    }

    bool InterpreterContext::scriptRunning(std::string_view name) const
    {
        if (mExecution) scriptStateName(name); // Before interning an external name.
        const auto id = ESM::RefId::stringRefId(name);
        if (mExecution) return getScopedScript(id).instance.mRunning;
        return MWBase::Environment::get().getScriptManager()->getGlobalScripts().isRunning(id);
    }

    void InterpreterContext::startScript(std::string_view name, const MWWorld::Ptr& target)
    {
        if (mExecution)
        {
            scriptStateName(name);
            if (!target.isEmpty())
                throw std::runtime_error("Targeted script start is not staged");
            getScopedScript(ESM::RefId::stringRefId(name)).instance.mRunning = true;
            return;
        }
        const auto id = ESM::RefId::stringRefId(name);
        if (!MWBase::Environment::get().getESMStore()->get<ESM::Script>().search(id))
        {
            report("Failed to start global script '" + id.getRefIdString() + "': script record not found");
            return;
        }
        MWBase::Environment::get().getScriptManager()->getGlobalScripts().addScript(id, target);
    }

    void InterpreterContext::stopScript(std::string_view name)
    {
        if (mExecution)
        {
            scriptStateName(name);
            getScopedScript(ESM::RefId::stringRefId(name)).instance.mRunning = false;
            return;
        }
        const auto id = ESM::RefId::stringRefId(name);
        if (!MWBase::Environment::get().getESMStore()->get<ESM::Script>().search(id))
        {
            report("Failed to stop global script '" + id.getRefIdString() + "': script record not found");
            return;
        }
        MWBase::Environment::get().getScriptManager()->getGlobalScripts().removeScript(id);
    }

    MWWorld::Ptr InterpreterContext::resolveReference(const ESM::RefId& id, bool required, bool activeOnly) const
    {
        return getReferenceImp(id, activeOnly, required);
    }

    MWWorld::Ptr InterpreterContext::resolveReferenceName(std::string_view name, bool required, bool activeOnly) const
    {
        // Bound compiled external names before interning a RefId. An explicit
        // empty name must never become the invocation's implicit source.
        if (mExecution && (name.size() > ExecutionContext::MaximumReferenceNameBytes
            || name.find('\0') != std::string_view::npos))
            throw std::invalid_argument("Script reference name exceeds bounds");
        if (name.empty())
        {
            if (required) throw std::runtime_error("Empty explicit script reference");
            return {};
        }
        return resolveReference(ESM::RefId::stringRefId(name), required, activeOnly);
    }

    InterpreterContext::InterpreterContext(std::shared_ptr<GlobalScriptDesc> globalScriptDesc)
        : mLocals(&(globalScriptDesc->mLocals))
    {
        const MWWorld::Ptr* ptr = globalScriptDesc->getPtrIfPresent();
        // A nullptr here signifies that the script's target has not yet been resolved after loading the game.
        // Script targets are lazily resolved to MWWorld::Ptrs (which can, upon resolution, be empty)
        // because scripts started through dialogue often don't use their implicit target.
        if (ptr)
            mReference = *ptr;
        else
            mGlobalScriptDesc = std::move(globalScriptDesc);
    }

    ESM::RefId InterpreterContext::getTarget() const
    {
        if (mExecution)
        {
            const auto ptr = getReference(false);
            return ptr.isEmpty() ? ESM::RefId() : ptr.getCellRef().getRefId();
        }
        if (!mReference.isEmpty())
            return mReference.mRef->mRef.getRefId();
        else if (mGlobalScriptDesc)
            return mGlobalScriptDesc->getId();
        return ESM::RefId();
    }

    int InterpreterContext::getLocalShort(int index) const
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        return mLocals->mShorts.at(index);
    }

    int InterpreterContext::getLocalLong(int index) const
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        return mLocals->mLongs.at(index);
    }

    float InterpreterContext::getLocalFloat(int index) const
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        return mLocals->mFloats.at(index);
    }

    void InterpreterContext::setLocalShort(int index, int value)
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        mLocals->mShorts.at(index) = static_cast<Interpreter::Type_Short>(value);
    }

    void InterpreterContext::setLocalLong(int index, int value)
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        mLocals->mLongs.at(index) = value;
    }

    void InterpreterContext::setLocalFloat(int index, float value)
    {
        if (!mLocals)
            throw std::runtime_error("local variables not available in this context");

        mLocals->mFloats.at(index) = value;
    }

    void InterpreterContext::messageBox(std::string_view message, const std::vector<std::string>& buttons)
    {
        requireUnscopedService();
        if (buttons.empty())
            MWBase::Environment::get().getWindowManager()->messageBox(message);
        else
            MWBase::Environment::get().getWindowManager()->interactiveMessageBox(message, buttons);
    }

    void InterpreterContext::report(const std::string& message) { requireUnscopedService(); }

    int InterpreterContext::getGlobalShort(std::string_view name) const
    {
        requireUnscopedService();
        return MWBase::Environment::get().getWorld()->getGlobalInt(name);
    }

    int InterpreterContext::getGlobalLong(std::string_view name) const
    {
        requireUnscopedService();
        // a global long is internally a float.
        return MWBase::Environment::get().getWorld()->getGlobalInt(name);
    }

    float InterpreterContext::getGlobalFloat(std::string_view name) const
    {
        requireUnscopedService();
        return MWBase::Environment::get().getWorld()->getGlobalFloat(name);
    }

    void InterpreterContext::setGlobalShort(std::string_view name, int value)
    {
        requireUnscopedService();
        MWBase::Environment::get().getWorld()->setGlobalInt(name, value);
    }

    void InterpreterContext::setGlobalLong(std::string_view name, int value)
    {
        requireUnscopedService();
        MWBase::Environment::get().getWorld()->setGlobalInt(name, value);
    }

    void InterpreterContext::setGlobalFloat(std::string_view name, float value)
    {
        requireUnscopedService();
        MWBase::Environment::get().getWorld()->setGlobalFloat(name, value);
    }

    std::vector<std::string> InterpreterContext::getGlobals() const
    {
        requireUnscopedService();
        const MWWorld::Store<ESM::Global>& globals = MWBase::Environment::get().getESMStore()->get<ESM::Global>();

        std::vector<std::string> ids;
        for (const auto& globalVariable : globals)
        {
            ids.emplace_back(globalVariable.mId.getRefIdString());
        }

        return ids;
    }

    char InterpreterContext::getGlobalType(std::string_view name) const
    {
        requireUnscopedService();
        MWBase::World* world = MWBase::Environment::get().getWorld();
        return world->getGlobalVariableType(name);
    }

    std::string InterpreterContext::getActionBinding(std::string_view targetAction) const
    {
        requireUnscopedService();
        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        const auto& actions = input->getActionKeySorting();
        for (const int action : actions)
        {
            std::string_view desc = input->getActionDescription(action);
            if (desc.empty())
                continue;

            if (desc == targetAction)
            {
                if (input->joystickLastUsed())
                    return input->getActionControllerBindingName(action);
                else
                    return input->getActionKeyBindingName(action);
            }
        }

        return "None";
    }

    std::string_view InterpreterContext::getActorName() const
    {
        const MWWorld::Ptr& ptr = getReferenceImp();
        if (ptr.getClass().isNpc())
        {
            const ESM::NPC* npc = ptr.get<ESM::NPC>()->mBase;
            return npc->mName;
        }

        const ESM::Creature* creature = ptr.get<ESM::Creature>()->mBase;
        return creature->mName;
    }

    std::string_view InterpreterContext::getNPCRace() const
    {
        const ESM::NPC* npc = getReferenceImp().get<ESM::NPC>()->mBase;
        const ESM::Race* race = getContent().get<ESM::Race>().find(npc->mRace);
        return race->mName;
    }

    std::string_view InterpreterContext::getNPCClass() const
    {
        const ESM::NPC* npc = getReferenceImp().get<ESM::NPC>()->mBase;
        const ESM::Class* npcClass = getContent().get<ESM::Class>().find(npc->mClass);
        return npcClass->mName;
    }

    std::string_view InterpreterContext::getNPCFaction() const
    {
        const MWWorld::Ptr& ptr = getReferenceImp();
        const ESM::RefId& factionId = ptr.getClass().getPrimaryFaction(ptr);
        if (factionId.empty())
        {
            Log(Debug::Warning) << "getNPCFaction(): NPC " << ptr.getCellRef().getRefId() << " has no primary faction";
            return "%";
        }

        const MWWorld::ESMStore& store = getContent();
        const ESM::Faction* faction = store.get<ESM::Faction>().find(factionId);
        return faction->mName;
    }

    std::string_view InterpreterContext::getNPCRank() const
    {
        const MWWorld::Ptr& ptr = getReferenceImp();
        const MWWorld::Class& ptrClass = ptr.getClass();
        const ESM::RefId& factionId = ptrClass.getPrimaryFaction(ptr);
        if (factionId.empty())
        {
            Log(Debug::Warning) << "getNPCRank(): NPC " << ptr.getCellRef().getRefId() << " has no primary faction";
            return "%";
        }

        const MWWorld::ESMStore& store = getContent();
        const ESM::Faction* faction = store.get<ESM::Faction>().find(factionId);

        int rank = ptrClass.getPrimaryFactionRank(ptr);
        if (rank < 0 || rank > 9)
        {
            Log(Debug::Warning) << "getNPCRank(): NPC " << ptr.getCellRef().getRefId() << " has invalid rank " << rank
                                << " in faction " << factionId;
            return "%";
        }
        return faction->mRanks[rank];
    }

    std::string_view InterpreterContext::getPCName() const
    {
        return getPlayer().get<ESM::NPC>()->mBase->mName;
    }

    std::string_view InterpreterContext::getPCRace() const
    {
        const ESM::RefId& race = getPlayer().get<ESM::NPC>()->mBase->mRace;
        return getContent().get<ESM::Race>().find(race)->mName;
    }

    std::string_view InterpreterContext::getPCClass() const
    {
        const ESM::RefId& playerClass = getPlayer().get<ESM::NPC>()->mBase->mClass;
        return getContent().get<ESM::Class>().find(playerClass)->mName;
    }

    std::string_view InterpreterContext::getPCRank() const
    {
        if (mExecution) getPlayerStats();
        const MWWorld::Ptr& ptr = getReferenceImp();
        const ESM::RefId& factionId = ptr.getClass().getPrimaryFaction(ptr);
        if (factionId.empty())
        {
            Log(Debug::Warning) << "getPCRank(): NPC " << ptr.getCellRef().getRefId() << " has no primary faction";
            return "%";
        }

        const auto& ranks = getPlayerStats().getFactionRanks();
        auto it = ranks.find(factionId);
        int rank = -1;
        if (it != ranks.end())
            rank = it->second;

        // If you are not in the faction, PcRank returns the first rank, for whatever reason.
        // This is used by the dialogue when joining the Thieves Guild in Balmora.
        if (rank == -1)
            rank = 0;

        const MWWorld::ESMStore& store = getContent();
        const ESM::Faction* faction = store.get<ESM::Faction>().find(factionId);

        if (rank < 0 || rank > 9) // there are only 10 ranks
            return {};

        return faction->mRanks[rank];
    }

    std::string_view InterpreterContext::getPCNextRank() const
    {
        if (mExecution) getPlayerStats();
        const MWWorld::Ptr& ptr = getReferenceImp();
        const ESM::RefId& factionId = ptr.getClass().getPrimaryFaction(ptr);
        if (factionId.empty())
        {
            Log(Debug::Warning) << "getPCNextRank(): NPC " << ptr.getCellRef().getRefId() << " has no primary faction";
            return "%";
        }

        const auto& ranks = getPlayerStats().getFactionRanks();
        auto it = ranks.find(factionId);
        int rank = -1;
        if (it != ranks.end())
            rank = it->second;

        ++rank; // Next rank

        // if we are already at max rank, there is no next rank
        if (rank > 9)
            rank = 9;

        const MWWorld::ESMStore& store = getContent();
        const ESM::Faction* faction = store.get<ESM::Faction>().find(factionId);

        if (rank < 0)
            return {};

        return faction->mRanks[rank];
    }

    int InterpreterContext::getPCBounty() const
    {
        return getPlayerStats().getBounty();
    }

    float InterpreterContext::getSecondsPassed() const
    {
        requireUnscopedService();
        return MWBase::Environment::get().getFrameDuration();
    }

    std::string_view InterpreterContext::getCurrentCellName() const
    {
        requireUnscopedService();
        return MWBase::Environment::get().getWorld()->getCellName();
    }

    void InterpreterContext::executeActivation(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor)
    {
        requireUnscopedService();
        // MWScripted activations don't go through Lua because 1-frame delay can brake mwscripts.
#if 0
        MWBase::Environment::get().getLuaManager()->objectActivated(ptr, actor);

        // TODO: Enable this branch after implementing one of the options:
        // 1) Pause this mwscript (or maybe all mwscripts) for one frame and continue from the same
        //     command when the activation is processed by Lua script.
        // 2) Force Lua scripts to handle a zero-length extra frame right now, so when control
        //     returns to the mwscript, the activation is already processed.
#else
        std::unique_ptr<MWWorld::Action> action = (ptr.getClass().activate(ptr, actor));
        action->execute(actor);
        if (action->getTarget() != MWWorld::Ptr() && action->getTarget() != ptr)
        {
            updatePtr(ptr, action->getTarget());
        }
#endif
    }

    ESM::RefId InterpreterContext::getMemberId(std::string_view name) const
    {
        if (mExecution) scriptStateName(name);
        return ESM::RefId::stringRefId(name);
    }

    int InterpreterContext::getMemberShort(ESM::RefId id, std::string_view name, bool global) const
    {
        if (mExecution)
        {
            const auto member = getScopedMember(id, name, 's', global);
            return member.locals.mShorts.at(member.index);
        }
        const Locals& locals = getMemberLocals(global, id);

        return locals.mShorts[findLocalVariableIndex(id, name, 's')];
    }

    int InterpreterContext::getMemberLong(ESM::RefId id, std::string_view name, bool global) const
    {
        if (mExecution)
        {
            const auto member = getScopedMember(id, name, 'l', global);
            return member.locals.mLongs.at(member.index);
        }
        const Locals& locals = getMemberLocals(global, id);

        return locals.mLongs[findLocalVariableIndex(id, name, 'l')];
    }

    float InterpreterContext::getMemberFloat(ESM::RefId id, std::string_view name, bool global) const
    {
        if (mExecution)
        {
            const auto member = getScopedMember(id, name, 'f', global);
            return member.locals.mFloats.at(member.index);
        }
        const Locals& locals = getMemberLocals(global, id);

        return locals.mFloats[findLocalVariableIndex(id, name, 'f')];
    }

    void InterpreterContext::setMemberShort(ESM::RefId id, std::string_view name, int value, bool global)
    {
        if (mExecution)
        {
            const auto member = getScopedMember(id, name, 's', global);
            member.locals.mShorts.at(member.index) = static_cast<Interpreter::Type_Short>(value);
            return;
        }
        Locals& locals = getMemberLocals(global, id);

        locals.mShorts[findLocalVariableIndex(id, name, 's')] = static_cast<Interpreter::Type_Short>(value);
    }

    void InterpreterContext::setMemberLong(ESM::RefId id, std::string_view name, int value, bool global)
    {
        if (mExecution)
        {
            const auto member = getScopedMember(id, name, 'l', global);
            member.locals.mLongs.at(member.index) = value;
            return;
        }
        Locals& locals = getMemberLocals(global, id);

        locals.mLongs[findLocalVariableIndex(id, name, 'l')] = value;
    }

    void InterpreterContext::setMemberFloat(ESM::RefId id, std::string_view name, float value, bool global)
    {
        if (mExecution)
        {
            if (!std::isfinite(value))
                throw std::invalid_argument("Nonfinite scoped script member");
            const auto member = getScopedMember(id, name, 'f', global);
            member.locals.mFloats.at(member.index) = value;
            return;
        }
        Locals& locals = getMemberLocals(global, id);

        locals.mFloats[findLocalVariableIndex(id, name, 'f')] = value;
    }

    MWWorld::Ptr InterpreterContext::getReference(bool required) const
    {
        return getReferenceImp({}, true, required);
    }

    void InterpreterContext::updatePtr(const MWWorld::Ptr& base, const MWWorld::Ptr& updated)
    {
        if (!mReference.isEmpty() && base == mReference)
        {
            mReference = updated;
            if (mLocals == &base.getRefData().getLocals())
                mLocals = &mReference.getRefData().getLocals();
        }
    }
}
