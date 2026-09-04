import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ADAPTER_DIR = ROOT / "apps" / "openmw" / "tes3mp"


class Phase9DualEngineCompositionTests(unittest.TestCase):
    def test_shared_adapter_and_desktop_providers_are_separate_targets(self):
        cmake = (ADAPTER_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        adapter = cmake.split("add_library(openmw_tes3mp_adapter STATIC", 1)[1].split(
            "add_library(openmw_tes3mp_desktop_providers STATIC", 1
        )[0]
        providers = cmake.split("add_library(openmw_tes3mp_desktop_providers STATIC", 1)[1]

        self.assertIn("client_connection.cpp", adapter)
        self.assertNotIn("desktop_providers.cpp", adapter)
        self.assertNotIn("openmw-lib", adapter)
        self.assertIn("desktop_providers.cpp", providers)
        self.assertIn("PRIVATE openmw-lib", providers)
        self.assertIn("PUBLIC TES3MP::OpenMWAdapter", providers)

    def test_both_executables_link_the_same_adapter_target(self):
        cmake = (ROOT / "apps" / "openmw" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "target_link_libraries(openmw TES3MP::OpenMWAdapter TES3MP::OpenMWDesktopProviders)", cmake
        )
        self.assertIn("target_link_libraries(openmw_vr TES3MP::OpenMWAdapter)", cmake)
        self.assertNotIn("TES3MP_OPENMW_DESKTOP_PROVIDERS", cmake)

    def test_shared_adapter_sources_have_no_vr_or_fork_headers(self):
        for name in (
            "adapter.cpp",
            "adapter.hpp",
            "client_connection.cpp",
            "client_connection.hpp",
            "engine_coordinator.hpp",
            "movement_mapping.cpp",
            "movement_mapping.hpp",
            "providers.hpp",
            "remote_motion.cpp",
            "remote_motion.hpp",
        ):
            source = (ADAPTER_DIR / name).read_text(encoding="utf-8").lower()
            self.assertNotIn("components/vr", source, name)
            self.assertNotIn("mwvr/", source, name)
            self.assertNotIn("openxr", source, name)

    def test_vr_composition_fails_closed_without_providers(self):
        source = (ROOT / "apps" / "openmw" / "main.cpp").read_text(encoding="utf-8")
        unavailable = source.index("const TES3MP::OpenMWAdapter::ClientProviders providers{};")
        create = source.index("TES3MP::OpenMWAdapter::makeClientCoordinator")
        self.assertLess(unavailable, create)
        self.assertIn('return "this build has no approved multiplayer providers";', source)
        self.assertNotIn("makeDesktopCoordinator", source)
        self.assertFalse((ADAPTER_DIR / "desktop_connection.cpp").exists())


if __name__ == "__main__":
    unittest.main()
