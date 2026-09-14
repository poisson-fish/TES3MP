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
                     "Loads configured TES3 records through OpenMW; writes diagnostic TSV to stdout.\n"
                     "Uses the engine's local/global openmw.cfg and its config chain.\n"
                     "No World, rendering, UI, inventory operations, or script execution.\n";
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
