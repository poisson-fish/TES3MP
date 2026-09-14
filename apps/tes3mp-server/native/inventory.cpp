#include "loadout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/localscripts.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    void Loadout::writeInventoryProbe(std::ostream& output, std::string_view itemId)
    {
        // Fixed counts, no externally supplied live Ptrs, and only MISC in this
        // slice. Other item classes can invoke further global gameplay services.
        if (itemId.empty() || itemId.size() > 256)
            throw std::runtime_error("Inventory probe expects a 1..256 byte item ID");
        for (unsigned char ch : itemId)
            if (ch < 32 || ch == 127)
                throw std::runtime_error("Inventory probe item ID contains a control character");
        const auto id = ESM::RefId::stringRefId(itemId);
        mStore.get<ESM::Miscellaneous>().find(id); // Reject other item classes before constructing a reference.

        MWClass::registerClasses();
        // The real registry and its lifetime bookkeeping, with a bounded cache.
        // No settings singleton, World, Environment, cell loading or scene setup.
        MWWorld::WorldModel worldModel(mStore, mReaders, 1);
        // Only ScriptManager::getLocals (the engine QuickFileParser) is used.
        // CompilerContext's live-world queries and opcode execution remain unused.
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions);
        MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
        compilerContext.setExtensions(&extensions);
        MWScript::ScriptManager scriptManager(mStore, compilerContext, 1);
        std::array<ESM::RefNum, 2> assigned;
        int notifications = 0;
        int stacks = 0;
        int scriptsRegistered = 0;
        int onPCAddAssigned = 0;
        std::array<std::size_t, 3> localCounts{};
        float weight = 0;
        std::string winningId;
        {
            // Explicit identity only: no NPC custom data or InventoryStore is
            // initialized. This is a disposable base store, not live actor state.
            MWWorld::ManualRef player(mStore, ESM::RefId::stringRefId("player"));
            MWWorld::ManualRef source(mStore, id, 2);
            const auto effectiveId
                = source.getPtr().getClass().isGold(source.getPtr()) ? MWWorld::ContainerStore::sGoldId : id;
            const auto* base = mStore.get<ESM::Miscellaneous>().find(effectiveId);
            if (!std::isfinite(base->mData.mWeight) || base->mData.mWeight < 0
                || base->mData.mWeight > std::numeric_limits<float>::max() / 3)
                throw std::runtime_error("Inventory probe item weight is invalid");

            const ESM::Script* script = nullptr;
            const Compiler::Locals* declarations = nullptr;
            if (!base->mScript.empty())
            {
                script = mStore.get<ESM::Script>().find(base->mScript);
                // Bound the offline declaration scan before the compiler copies text.
                // These are probe limits, not a restriction on engine script content.
                if (script->mScriptText.size() > 64 * 1024)
                    throw std::runtime_error("Inventory probe script text exceeds 64 KiB limit");
                declarations = &scriptManager.getLocals(script->mId);
                localCounts
                    = { declarations->get('s').size(), declarations->get('l').size(), declarations->get('f').size() };
                if (localCounts[0] + localCounts[1] + localCounts[2] > 256)
                    throw std::runtime_error("Inventory probe script exceeds 256 locals limit");
            }
            MWWorld::ContainerStore container;
            // Destroy the list of script Ptrs before the container, also on failure.
            MWWorld::LocalScripts localScripts(mStore);
            const MWWorld::ContainerStoreAddContext context{ mStore, worldModel, player.getPtr(), player.getPtr(),
                &localScripts, &scriptManager, [&](const MWWorld::Ptr& owner) {
                    if (owner != player.getPtr())
                        throw std::runtime_error("Inventory presentation request has the wrong owner");
                    ++notifications;
                } };
            const auto first = container.add(source.getPtr(), 2, context);
            assigned[0] = first->getCellRef().getRefNum();
            const auto second = container.add(source.getPtr(), 1, context);
            assigned[1] = second->getCellRef().getRefNum();
            const bool separateStacks = script && effectiveId != MWWorld::ContainerStore::sGoldId;
            stacks = separateStacks ? 2 : 1;
            if ((first == second) == separateStacks || first->getCellRef().getCount() != (separateStacks ? 2 : 3)
                || (separateStacks && second->getCellRef().getCount() != 1)
                || std::distance(container.begin(), container.end()) != stacks
                || worldModel.getPtr(assigned[0]) != *first || worldModel.getPtr(assigned[1]) != *second
                || first->getContainerStore() != &container || second->getContainerStore() != &container
                || source.getPtr().getCellRef().getCount() != 2 || !source.getPtr().getRefData().getLocals().isEmpty()
                || notifications != 2)
                throw std::runtime_error("Native inventory insertion/stacking/registration invariant failed");

            localScripts.startIteration();
            std::pair<ESM::RefId, MWWorld::Ptr> entry;
            while (localScripts.getNext(entry))
            {
                if (!script || entry.first != script->mId || (entry.second != *first && entry.second != *second)
                    || entry.second.mCell != nullptr || ++scriptsRegistered > stacks)
                    throw std::runtime_error("Native inventory local script registration invariant failed");
                const auto& locals = entry.second.getRefData().getLocals();
                if (locals.getScriptId() != script->mId || locals.mShorts.size() != localCounts[0]
                    || locals.mLongs.size() != localCounts[1] || locals.mFloats.size() != localCounts[2])
                    throw std::runtime_error("Native inventory compiler-derived locals invariant failed");
                const auto verify = [&](char type, const auto& values) {
                    for (std::size_t i = 0; i < values.size(); ++i)
                    {
                        const bool onPCAdd = declarations->get(type)[i] == "onpcadd";
                        if (values[i] != (onPCAdd ? 1 : 0))
                            throw std::runtime_error("Native inventory local initialization/OnPCAdd invariant failed");
                        if (onPCAdd)
                            ++onPCAddAssigned;
                    }
                };
                verify('s', locals.mShorts);
                verify('l', locals.mLongs);
                verify('f', locals.mFloats);
            }
            if (scriptsRegistered != (script ? stacks : 0))
                throw std::runtime_error("Native inventory missing local script registration");
            for (const auto& item : container)
                localScripts.remove(item);
            localScripts.startIteration();
            if (localScripts.getNext(entry))
                throw std::runtime_error("Native inventory local script cleanup failed");
            weight = container.getWeight();
            if (!std::isfinite(weight))
                throw std::runtime_error("Native inventory total weight is not finite");
            const auto refId = first->getCellRef().getRefId().getRefIdString();
            if (refId.size() > 256)
                throw std::runtime_error("Inventory winning ID exceeds diagnostic limit");
            winningId = Misc::StringUtils::lowerCase(refId);
        }
        if (std::ranges::any_of(assigned, [&](const auto& ref) { return !worldModel.getPtr(ref).isEmpty(); }))
            throw std::runtime_error("Native inventory destruction left a registered reference");

        // Staged owned output only; fixed fields plus one bounded quoted ID.
        // Device write failure itself cannot be rolled back.
        std::ostringstream report;
        report.imbue(std::locale::classic());
        report << std::setprecision(std::numeric_limits<float>::max_digits10) << "native-inventory\t2\nitem\t"
               << std::quoted(winningId) << "\ncount\t3\nstacks\t" << stacks << "\nweight\t" << weight
               << "\npresentation-requests\t" << notifications << "\nregistered\t" << stacks << "\nderegistered\t"
               << stacks << "\nlocal-shorts\t" << localCounts[0] << "\nlocal-longs\t" << localCounts[1]
               << "\nlocal-floats\t" << localCounts[2] << "\nscripts-registered\t" << scriptsRegistered
               << "\nscripts-removed\t" << scriptsRegistered << "\nonpcadd-assigned\t" << onPCAddAssigned
               << "\nscript-executed\t0\ncomplete\n";
        const auto text = report.str();
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
            throw std::runtime_error("Failed writing native inventory report");
    }
}
