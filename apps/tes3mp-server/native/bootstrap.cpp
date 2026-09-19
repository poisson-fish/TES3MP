#include "loadout.hpp"
#include <components/misc/strings/lower.hpp>
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <tes3mp/server_authentication.hpp>
#include <charconv>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace
{
    int number(std::string_view text)
    {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument("Invalid coordinate/radius");
        return value;
    }
    ESM::RefId startCell(std::string_view text)
    {
        if (!text.starts_with("exterior:")) return TES3MP::Native::interiorCell(text);
        const auto split = text.find(':', 9);
        if (split == std::string_view::npos) throw std::invalid_argument("Use exterior:X:Y");
        return ESM::RefId::esm3ExteriorCell(number(text.substr(9, split - 9)), number(text.substr(split + 1)));
    }
    std::string hex(std::span<const std::byte> bytes)
    {
        std::ostringstream result;
        for (auto byte : bytes) result << std::hex << std::setw(2) << std::setfill('0') << unsigned(std::to_integer<uint8_t>(byte));
        return result.str();
    }
    std::string selector(ESM::RefId cell)
    {
        std::ostringstream result;
        if (const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>())
            result << "exterior " << exterior->getX() << ' ' << exterior->getY();
        else result << "interior " << std::quoted(cell.getRefIdString());
        return result.str();
    }
    void write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.write(text.data(), text.size()) || !output.flush()) throw std::runtime_error("Bootstrap output failed");
    }
}

int main(int argc, char** argv)
try
{
    const bool inspectArea = argc >= 2 && std::string_view(argv[1]) == "--inspect-area";
    const bool listCells = argc >= 2 && std::string_view(argv[1]) == "--list-cells";
    if ((listCells && argc != 4) || (inspectArea && argc != 4 && argc != 5))
        throw std::invalid_argument("Invalid inspection arguments; see --help");
    if (listCells || inspectArea)
    {
        const std::string query = Misc::StringUtils::lowerCase(std::string(argv[3]));
        if (query.empty() || query.size() > 128) throw std::invalid_argument("Cell filter must contain 1..128 bytes");
        const char* arguments[]{"tes3mp_native_bootstrap", "--config", argv[2]};
        TES3MP::Native::Loadout loadout(TES3MP::Native::readLoadoutOptions(3, arguments));
        const auto& cells = loadout.store().get<ESM::Cell>();
        if (cells.getSize() > 65536) throw std::invalid_argument("Native cell catalog budget exceeded");
        std::vector<ESM::RefId> matches;
        if (inspectArea)
        {
            const int radius = argc == 5 ? number(argv[4]) : 1;
            if (radius < 0 || radius > 7) throw std::invalid_argument("Exterior radius outside bounds");
            matches = loadout.playerAreas(startCell(argv[3]), unsigned(radius), 256);
        }
        else
        {
            for (size_t i = 0; i < cells.getSize(); ++i)
            {
                const auto* cell = cells.at(i);
                if (Misc::StringUtils::lowerCase(cell->mName).find(query) != std::string::npos)
                    matches.push_back(cell->mId);
            }
            std::sort(matches.begin(), matches.end());
        }
        MWClass::registerClasses();
        MWWorld::WorldModel world(loadout.store(), loadout.readers(), 1);
        Misc::Rng::Generator rng{0};
        const size_t shown = std::min<size_t>(matches.size(), inspectArea ? 256 : 64);
        for (size_t i = 0; i < shown; ++i)
        {
            const auto* cell = world.getCell(matches[i]).getCell();
            std::cout << selector(matches[i]) << " name=" << std::quoted(std::string(cell->getDisplayName()))
                << " region=" << cell->getRegion();
            try
            {
                const auto containers = loadout.placedContainers(matches[i]);
                if (!inspectArea) rng = Misc::Rng::Generator{0};
                std::vector<TES3MP::Native::ActorSpawnSelection> spawns;
                const auto actors = loadout.resolveActors(matches[i], 128, 1, rng, spawns);
                const auto items = loadout.placedItems(matches[i], 192);
                const auto unsupported = std::ranges::count_if(containers, [](const auto& ref) {
                    return ref.mScripted || ref.mRef.mIsLocked || !ref.mRef.mTrap.empty();
                });
                std::cout << " containers=" << containers.size() << " unavailable=" << unsupported
                    << " actors=" << actors.size() << " leveled=" << spawns.size() << " ground=" << items.size();
                if (inspectArea)
                    std::cout << " doors=" << loadout.ordinaryDoors(matches[i], 128).size()
                        << " teleports=" << loadout.teleportDoors(matches[i], matches).size();
                size_t scripted = 0, references = 0;
                std::set<std::string> scripts;
                world.getCell(matches[i]).forEach([&](const MWWorld::Ptr& ptr) {
                    if (++references > 8192) throw std::invalid_argument("Scene reference budget exceeded");
                    if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
                    const auto script = ptr.getClass().getScript(ptr);
                    if (!script.empty())
                    {
                        ++scripted;
                        if (scripts.size() < 3) scripts.insert(script.toString());
                    }
                    return true;
                });
                std::cout << " scene_scripts=" << scripted;
                for (const auto& script : scripts) std::cout << ' ' << std::quoted(script);
            }
            catch (const std::exception& error) { std::cout << " unavailable=" << std::quoted(error.what()); }
            std::cout << '\n';
        }
        std::cout << "Matched " << matches.size() << " cells; " << shown << " shown\n";
        if (inspectArea) std::cout << "Read-only inspection at level 1, seed 0; runtime startup is still required.\n";
        return 0;
    }
    if (argc < 6 || argc > 8)
    {
        std::cout << "tes3mp_native_bootstrap OPENMW_CONFIG_DIR NEW_OUTPUT_DIR START NPC_A NPC_B [RADIUS] [SPAWN_X:SPAWN_Y:SPAWN_Z]\n"
            "tes3mp_native_bootstrap --list-cells OPENMW_CONFIG_DIR NAME_FILTER (at most 64 results)\n"
            "tes3mp_native_bootstrap --inspect-area OPENMW_CONFIG_DIR START [RADIUS] (all connected areas)\n"
            "START is an interior name or exterior:X:Y; exterior radius defaults to 1 (maximum 7).\n"
            "Use RADIUS=single for a one-cell connection/presentation check with travel disabled.\n"
            "OpenMW discovers the area and connected interiors. Outputs native-inventory-15 and matching\n"
            "server/client content settings, without fixture plugins or reference recipes.\n"
            "Spawn uses a discovered incoming door destination, or explicit OpenMW coordinates.\n"
            "Set the two established player IDs in native.txt; their registrations must bind manifest.txt.\n"
            "Add connection/password/player_identity_file settings to server-content.cfg before launch.\n";
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    const auto config = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    if (std::filesystem::exists(output)) throw std::invalid_argument("Bootstrap output directory already exists");
    const bool singleCell = argc >= 7 && std::string_view(argv[6]) == "single";
    const int radius = singleCell ? 0 : argc >= 7 ? number(argv[6]) : 1;
    if (radius < 0 || radius > 7) throw std::invalid_argument("Exterior radius outside bounds");
    const auto start = startCell(argv[3]);
    TES3MP::Native::validateCell(start);
    const std::string configArgument = config.string();
    const char* arguments[]{"tes3mp_native_bootstrap", "--config", configArgument.c_str()};
    TES3MP::Native::Loadout loadout(TES3MP::Native::readLoadoutOptions(3, arguments));
    const auto actorA = TES3MP::Native::interiorCell(argv[4]), actorB = TES3MP::Native::interiorCell(argv[5]);
    loadout.store().get<ESM::NPC>().find(actorA); loadout.store().get<ESM::NPC>().find(actorB);
    const auto cells = singleCell ? std::vector{start} : loadout.playerAreas(start, unsigned(radius), 256);
    std::optional<ESM::Position> spawnPosition;
    if (argc == 8)
    {
        std::string point = argv[7];
        std::ranges::replace(point, ':', ' ');
        std::istringstream input(point);
        ESM::Position position{};
        if (!(input >> position.pos[0] >> position.pos[1] >> position.pos[2]) || !(input >> std::ws).eof())
            throw std::invalid_argument("Spawn must contain three OpenMW coordinates");
        spawnPosition = position;
    }
    spawnPosition = loadout.playerSpawn(start, cells, spawnPosition);
    std::ostringstream material;
    material << "native-inventory-15\n" << loadout.contentFingerprint() << '\n' << actorA << '\n' << actorB << '\n';
    for (const auto& cell : cells) material << selector(cell) << '\n';
    auto crypto = TES3MP::makeProductionCredentialCrypto();
    TES3MP::CredentialDigest digest;
    const auto input = material.str();
    if (!crypto || !crypto->sha256(std::as_bytes(std::span(input)), digest)) throw std::runtime_error("Content identity unavailable");
    const auto manifest = hex(digest.bytes);
    std::ostringstream spaces, allowed, mappings, native;
    const bool exteriors = std::ranges::any_of(cells, [](const auto& cell) { return cell.template is<ESM::ESM3ExteriorCellRefId>(); });
    bool haveSpace = false;
    uint64_t nextInterior = exteriors ? 2 : 1;
    if (exteriors)
    {
        spaces << "exterior:1"; haveSpace = true;
        mappings << "tes3mp-content-cell-space-map=1=" << ESM::Cell::sDefaultWorldspaceId.getValue() << '\n';
    }
    native << "native-inventory-15\nmanifest " << manifest << "\nconfig " << std::quoted(config.string())
        << "\nplayers 1 2\nactors " << std::quoted(std::string(actorA.getRefIdString())) << ' '
        << std::quoted(std::string(actorB.getRefIdString())) << "\nloot 1 0\n";
    std::string spawn;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto& cell = cells[i];
        std::ostringstream wire;
        if (const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>())
            wire << "exterior:1:" << exterior->getX() << ':' << exterior->getY();
        else
        {
            if (haveSpace) spaces << ';';
            spaces << "interior:" << nextInterior; haveSpace = true;
            mappings << "tes3mp-content-cell-space-map=" << nextInterior << '=' << cell.getRefIdString() << '\n';
            wire << "interior:" << nextInterior++;
        }
        if (i) allowed << ';'; else spawn = wire.str();
        allowed << wire.str();
        native << selector(cell) << '\n';
        if (!i) native << "doors auto\n";
        native << "cell " << wire.str() << '\n';
        if (!i) native << "areas " << cells.size() << '\n';
    }
    std::ostringstream server, client;
    // Appearance IDs must be disjoint from every allocated cell-space ID.
    const auto appearance = nextInterior;
    server << "content_manifest_id=" << manifest << "\ncell_spaces=" << spaces.str() << "\nallowed_cells=" << allowed.str()
        << "\nspawn_cell=" << spawn << "\nspawn_positions=" << std::llround(double(spawnPosition->pos[0]) * 1024) << ':'
        << std::llround(double(spawnPosition->pos[1]) * 1024) << ':' << std::llround(double(spawnPosition->pos[2]) * 1024)
        << "\ndefault_appearance_id=" << appearance
        << "\nmovement_profile=sneak:1024;walk:4097;run:8192;jump:4096\nnative_inventory_file=native.txt\n";
    client << "tes3mp-content-manifest-id=" << manifest << "\ntes3mp-content-cell-spaces=" << spaces.str()
        << "\ntes3mp-content-allowed-cells=" << allowed.str() << '\n' << mappings.str()
        << "tes3mp-content-appearance-id=" << appearance << '\n'
        << "tes3mp-content-appearance-record=" << actorA.getRefIdString() << '\n';
    auto staging = output; staging += ".pending";
    if (!std::filesystem::create_directory(staging)) throw std::runtime_error("Bootstrap staging directory already exists");
    try
    {
        write(staging / "native.txt", native.str()); write(staging / "server-content.cfg", server.str());
        write(staging / "client-content.cfg", client.str()); write(staging / "manifest.txt", manifest + '\n');
        if (std::filesystem::exists(output)) throw std::runtime_error("Bootstrap output appeared during export");
        std::filesystem::rename(staging, output);
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored); // Only the directory created by this invocation.
        throw;
    }
    std::cout << "Exported " << cells.size() << " native areas to " << output.string() << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "Native bootstrap failed: " << error.what() << '\n';
    return 1;
}
