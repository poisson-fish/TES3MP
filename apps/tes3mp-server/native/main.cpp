#include "loadout.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "tes3mp_native_loadout_probe [OpenMW --config DIR --data DIR --data-local DIR\n"
                     "  --content FILE --encoding win1252 --replace SETTING ...]\n"
                     "  [--sample | --inventory ID | --enchantment ID]\n"
                     "  [--equipment SHIRT --equipment-actors NPC_A NPC_B --equipment-save-dir NEW_DIR]\n"
                     "  [--equipment-container CONT] (empty diagnostic container for plain-shirt drop/take)\n"
                     "Loads configured TES3 records through OpenMW; writes diagnostic TSV to stdout.\n"
                     "Record IDs use OpenMW's ASCII case folding; display names retain their spelling.\n"
                     "Uses the engine's local/global openmw.cfg and its config chain.\n"
                     "--sample stages up to four winning IDs per category as owned diagnostic values,\n"
                     "  with 60 records, 32 effects/record, 4096 bytes/string and 64 KiB report limits.\n"
                     "--inventory adds 2 then 1 copies of a MISC record to a disposable ContainerStore,\n"
                     "  verifies stacks/WorldModel and local script registration, and records presentation requests.\n"
                     "  Script locals use OpenMW declarations; declared OnPCAdd is assigned without execution.\n"
                     "  Script probe limits: 64 KiB source text, 256 locals.\n"
                     "--enchantment stages native cast cost (before skill adjustment) and maximum charge.\n"
                     "  Limits: 256 bytes/ID, 32 effects, nonnegative effect fields/base cost/multiplier <= 1e6,\n"
                     "  and representable rounded cost/charge. This does not execute enchanted effects.\n"
                     "--equipment transfers two plain shirts, equips both actors in one owner, then recovers the pair.\n"
                     "  With --equipment-container CONT, A drops, B takes/equips, then all three owners recover.\n"
                     "  Both actors continue; A takes the remaining shirt. One coherent session file.\n"
                     "  Without a container, bounded constant/scripted shirts retain pair equipment/recovery.\n"
                     "No World, Environment, rendering, UI, or script instruction execution.\n";
        return 0;
    }
    // OpenMW Log writes to cout. Keep diagnostics off the record report even
    // when the engine logs during validation or throws during loading.
    std::ostream report(std::cout.rdbuf());
    auto* previous = std::cout.rdbuf(std::cerr.rdbuf());
    const auto start = std::chrono::steady_clock::now();
    int result = 0;
    try
    {
        TES3MP::Native::probe(argc, argv, report);
        std::cerr << "Native loadout completed in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native loadout failed: " << error.what() << '\n';
        result = 1;
    }
    std::cout.rdbuf(previous);
    return result;
}
