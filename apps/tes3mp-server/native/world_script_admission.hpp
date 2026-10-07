#ifndef TES3MP_NATIVE_WORLD_SCRIPT_ADMISSION_HPP
#define TES3MP_NATIVE_WORLD_SCRIPT_ADMISSION_HPP
#include "loadout.hpp"
#include "world_script.hpp"
#include <apps/openmw/mwworld/globals.hpp>
#include <components/esm3/loadscpt.hpp>
#include <algorithm>
#include <array>
#include <memory>

namespace TES3MP::Native
{
    inline std::shared_ptr<const SharedWorldScript> admitSharedWorldScript(const Loadout& loadout,
        ESM::RefId id, std::span<const ESM::RefId> owned, CredentialCrypto& crypto,
        std::span<const ESM::Script> bank = {})
    {
        if (owned.size() > MWScript::WorldScript::MaximumGlobals)
            throw std::invalid_argument("Shared global ownership budget exceeded");
        const auto* script = loadout.store().get<ESM::Script>().search(id);
        if (!script) throw std::invalid_argument("Shared script absent from winning content");
        std::vector<ESM::Global> globals;
        for (const auto& name : owned)
        {
            const auto calendar = std::array{MWWorld::Globals::sGameHour, MWWorld::Globals::sDay,
                MWWorld::Globals::sMonth, MWWorld::Globals::sYear, MWWorld::Globals::sDaysPassed,
                MWWorld::Globals::sTimeScale};
            if (std::ranges::any_of(calendar, [&](auto entry) { return name == entry.getValue(); }))
                throw std::invalid_argument("Shared script cannot own calendar globals");
            const auto* global = loadout.store().get<ESM::Global>().search(name);
            if (!global) throw std::invalid_argument("Shared global absent from winning content");
            globals.push_back(*global);
        }
        return std::make_shared<const SharedWorldScript>(*script, std::move(globals), loadout.contentFingerprint(), crypto, bank);
    }
}
#endif
