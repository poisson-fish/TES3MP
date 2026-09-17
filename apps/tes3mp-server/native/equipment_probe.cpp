#include "loadout.hpp"
#include "equipment_runtime.hpp"

#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <components/files/hash.hpp>
#include <components/files/openfile.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace TES3MP::Native
{
    void Loadout::writeEquipmentProbe(std::ostream& output)
    {
        const auto id = [](std::string_view value) {
            if (value.empty() || value.size() > 256)
                throw std::invalid_argument("Equipment probe expects 1..256 byte record IDs");
            for (unsigned char c : value)
                if (c < 32 || c == 127)
                    throw std::invalid_argument("Equipment probe record ID contains a control character");
            return ESM::RefId::stringRefId(value);
        };
        if (mOptions.mEquipmentActors.size() != 2 || mOptions.mEquipmentSaveDirectory.empty())
            throw std::invalid_argument("Equipment probe requires explicit actors and save directory");
        const auto shirtId = id(mOptions.mEquipment);
        const auto* shirt = mStore.get<ESM::Clothing>().find(shirtId);
        const bool stats = !shirt->mEnchant.empty() || !shirt->mScript.empty();
        const std::array<EquipmentActorBinding, 2> actors{{
            { id(mOptions.mEquipmentActors[0]), shirtId, shirt->mScript.empty() ? 3 : 1, stats },
            { id(mOptions.mEquipmentActors[1]), shirtId, shirt->mScript.empty() ? -5 : -1, stats }
        }};

        // Reuse OpenMW's file fingerprint for this local diagnostic composition.
        // Bind ordered bytes, encoding and startup actor/item roles. This is not
        // an authenticated multiplayer pack manifest. Content stays immutable.
        std::ostringstream identity;
        identity << "equipment-probe-1\n" << mOptions.mEncoding << '\n';
        for (const auto& actor : actors)
            identity << actor.mBase << '\n' << actor.mShirt << '\n' << actor.mCount << '\n';
        for (size_t i = 0; i < mFiles.size(); ++i)
        {
            auto stream = Files::openBinaryInputFileStream(mFiles[i]);
            const auto hash = Files::getHash(mOptions.mContent[i], *stream);
            identity << mOptions.mContent[i].size() << ':' << mOptions.mContent[i] << '\n'
                     << hash[0] << ':' << hash[1] << '\n';
        }
        std::istringstream identityStream(identity.str());
        const auto hash = Files::getHash("equipment-loadout", identityStream);
        std::array<unsigned char, 32> contentIdentity{};
        for (size_t i = 0; i < 16; ++i)
            contentIdentity[i] = static_cast<unsigned char>(hash[i / 8] >> ((i % 8) * 8));

        MWScript::CompilerContext compiler(MWScript::CompilerContext::Type_Full);
        MWScript::ScriptManager declarations(mStore, compiler, 1);
        std::shared_ptr<const MWWorld::EquipmentScriptLocals> locals;
        if (!shirt->mScript.empty())
            locals = std::make_shared<const MWWorld::EquipmentScriptLocals>(mStore, shirt->mScript, declarations);

        if (!std::filesystem::create_directory(mOptions.mEquipmentSaveDirectory))
            throw std::invalid_argument("Equipment save directory must be new (existing files are never overwritten)");
        std::ostringstream report;
        report << "native-equipment-runtime\t1\ncontent-fingerprint\t";
        for (auto byte : contentIdentity)
            report << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
        report << std::dec << "\nshirt\t" << shirtId << "\nnpc-stats\t" << stats << '\n';
        for (size_t actor = 0; actor < actors.size(); ++actor)
        {
            const auto path = mOptions.mEquipmentSaveDirectory / ("actor-" + std::to_string(actor) + ".equipment");
            EquipmentBytes bytes;
            std::unique_ptr<const EquipmentSuccess> success;
            FileFaults faults;
            std::vector<ESM::RefId> ids{ shirtId, actors[actor].mBase };
            {
                MWWorld::WorldModel world(mStore, mReaders, 1);
                MWWorld::LocalScripts scripts(mStore);
                EquipmentRuntime runtime(mStore, world, scripts, "native-equipment-probe-1", contentIdentity,
                    actors, locals, &declarations);
                const auto command = runtime.command(actor, true);
                EquipmentFileSink file(path);
                if (runtime.execute({ command.mActor }, command, file, success, bytes, faults) != PersistenceResult::Accepted)
                    throw std::runtime_error("Equipment probe equip was not durably accepted");
                // Only trusted content-derived initial spells enter the decode allowlist.
                if (stats)
                {
                    MWWorld::ManualRef base(mStore, actors[actor].mBase);
                    base.getPtr().getCellRef().setRefNum({ command.mActor.mIndex, command.mActor.mContentFile });
                    const MWWorld::EquipmentNpcStats initial(base.getPtr(), mStore);
                    for (auto spell : initial.values().mSpells)
                        if (!spell.empty()) ids.push_back(spell);
                }
                report << "actor\t" << actor << "\tbase=" << actors[actor].mBase
                       << "\tequipped=" << success->mShirt.mIndex << "\trevision=" << success->mRevision << '\n';
            }
            // Every actor node, store, stat context and registry from execution
            // has died. Recovery cannot borrow the old runtime or replay effects.
            MWWorld::WorldModel world(mStore, mReaders, 1);
            MWWorld::LocalScripts scripts(mStore);
            EquipmentRuntime fresh(mStore, world, scripts, "native-equipment-probe-1", contentIdentity,
                actors, locals, &declarations, actor);
            std::unique_ptr<const MWWorld::PlainEquipmentValues> restored;
            EquipmentBytes accepted;
            if (fresh.restart(actor, path, ids, restored, accepted, faults) != FileReadResult::Read
                || accepted != bytes || restored->mShirt.mIndex != success->mShirt.mIndex)
                throw std::runtime_error("Equipment probe fresh recovery mismatch");
            const auto command = fresh.command(actor, false);
            EquipmentFileSink file(path);
            if (fresh.execute({ command.mActor }, command, file, success, bytes, faults) != PersistenceResult::Accepted
                || success->mShirt.mIndex != 0)
                throw std::runtime_error("Equipment probe recovered continuation failed");
            report << "restored\t" << actor << "\tcontinued-unequipped\tbytes=" << bytes.size() << '\n';
        }
        report << "complete\n";
        output << report.str();
        if (!output) throw std::runtime_error("Equipment probe report write failed");
    }
}
