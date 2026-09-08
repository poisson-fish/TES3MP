import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class CombatBootstrapContractTests(unittest.TestCase):
    def test_production_loads_and_wires_combat_without_advertising_it(self):
        main = (ROOT / "apps/tes3mp-server/main.cpp").read_text(encoding="utf-8")
        config = (ROOT / "apps/tes3mp-server/server_config.cpp").read_text(encoding="utf-8")
        application = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")
        cmake = (ROOT / "apps/tes3mp-server/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('"combat_content_file"', config)
        self.assertIn("loadCombatContent(", main)
        self.assertIn("combatContent ? &combatContent->world : nullptr", main)
        self.assertIn("combatContent ? &combatContent->weapons : nullptr", main)
        self.assertIn("combatContent ? &combatContent->playerTemplate : nullptr", main)
        self.assertIn("combatContent ? &combatContent->settings : nullptr", main)
        self.assertIn("UnavailableMeleeContactQuery", main)
        self.assertNotIn("combatReplicationCapability()", main)
        self.assertIn('mFailure = "combat composition incomplete"', application)
        self.assertIn("combat_content.cpp", cmake)

    def test_attack_resolution_uses_inventory_and_weapon_catalog_inputs(self):
        header = (ROOT / "components/tes3mp/include/tes3mp/combat_world.hpp").read_text(encoding="utf-8")
        source = (ROOT / "components/tes3mp/server_core/combat_world.cpp").read_text(encoding="utf-8")

        declaration = header[header.index("PreparedMeleeAttack prepareAuthoritativeMeleeAttack") :]
        declaration = declaration[: declaration.index(";")]
        self.assertIn("const CanonicalInventoryWorld& inventory", declaration)
        self.assertIn("const MeleeWeaponCatalog& weapons", declaration)
        self.assertNotIn("OpenMwMeleeWeapon", declaration)
        self.assertIn("EquipmentSlot::CarriedRight", source)
        self.assertIn("setEquippedItemCondition", source)


if __name__ == "__main__":
    unittest.main()
