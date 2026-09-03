#include "options.hpp"

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/rng.hpp>

namespace
{
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;
}

namespace OpenMW
{
    bpo::options_description makeOptionsDescription()
    {
        bpo::options_description desc("Syntax: openmw <options>\nAllowed options");
        Files::ConfigurationManager::addCommonOptions(desc);

        auto addOption = desc.add_options();
        addOption("help", "print help message");
        addOption("version", "print version information and quit");

        addOption("tes3mp-enable", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "enable TES3MP multiplayer");
        addOption("tes3mp-host", bpo::value<std::string>()->default_value(""), "TES3MP server host");
        addOption("tes3mp-port", bpo::value<unsigned>()->default_value(0), "TES3MP server port");
        addOption("tes3mp-timeout-ms", bpo::value<unsigned>()->default_value(0),
            "TES3MP connection timeout in milliseconds (1-60000; required when enabled)");
        addOption("tes3mp-password-file", bpo::value<Files::MaybeQuotedPath>()->default_value({}, ""),
            "file containing optional TES3MP join password");
        addOption("tes3mp-fixture-interior", bpo::value<std::string>()->default_value(""),
            "OpenMW interior cell mapped to the TES3MP fixture interior");
        addOption("tes3mp-fixture-worldspace", bpo::value<std::string>()->default_value(""),
            "OpenMW worldspace mapped to the TES3MP fixture exterior");
        addOption("tes3mp-fixture-avatar", bpo::value<std::string>()->default_value(""),
            "OpenMW NPC record used for transient remote-player presentation");
#ifdef TES3MP_OPENMW_DESKTOP_AUTOMATION
        addOption("tes3mp-automation-role", bpo::value<std::string>()->default_value(""),
            "test-only fixed TES3MP desktop automation role");
        addOption("tes3mp-automation-output", bpo::value<Files::MaybeQuotedPath>()->default_value({}, ""),
            "test-only TES3MP desktop automation evidence path");
#endif

        addOption("data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        addOption("data-local", bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "set local data directory (highest priority)");

        addOption("fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        addOption("start", bpo::value<std::string>()->default_value(""), "set initial cell");

        addOption("content", bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

        addOption("groundcover",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "groundcover content file(s): esm/esp, or omwgame/omwaddon");

        addOption("no-sound", bpo::value<bool>()->implicit_value(true)->default_value(false), "disable all sounds");

        addOption("script-all", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all scripts (excluding dialogue scripts) at startup");

        addOption("script-all-dialogue", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all dialogue scripts at startup");

        addOption("script-console", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "enable console-only script functionality");

        addOption("script-run", bpo::value<std::string>()->default_value(""),
            "select a file containing a list of console commands that is executed on startup");

        addOption("script-warn", bpo::value<int>()->implicit_value(1)->default_value(1),
            "handling of warnings when compiling scripts\n"
            "\t0 - ignore warnings\n"
            "\t1 - show warnings but consider script as correctly compiled anyway\n"
            "\t2 - treat warnings as errors");

        addOption("load-savegame", bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "load a save game file on game startup (specify an absolute filename or a filename relative to the current "
            "working directory)");

        addOption("skip-menu", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "skip main menu on game startup");

        addOption("new-game", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "run new game sequence (ignored if skip-menu=0)");

        addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding used in OpenMW game messages:\n"
            "\n\twin1250 - Central and Eastern European such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, "
            "Croatian, Serbian (Latin script), Romanian and Albanian languages\n"
            "\n\twin1251 - Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages\n"
            "\n\twin1252 - Western European (Latin) alphabet, used by default");

        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");

        addOption("no-grab", bpo::value<bool>()->implicit_value(true)->default_value(false), "Don't grab mouse cursor");

        addOption("export-fonts", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "Export Morrowind .fnt fonts to PNG image and XML file in current directory");

        addOption("activate-dist", bpo::value<int>()->default_value(-1), "activation distance override");

        addOption("random-seed", bpo::value<unsigned int>()->default_value(Misc::Rng::generateDefaultSeed()),
            "seed value for random number generator");

        return desc;
    }
}
