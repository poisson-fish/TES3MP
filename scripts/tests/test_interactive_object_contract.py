import unittest
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


class InteractiveObjectContractTests(unittest.TestCase):
    def test_interactive_object_headers_exist_in_engine_independent_component(self):
        catalog_header = ROOT / "components/tes3mp/include/tes3mp/interactive_object_catalog.hpp"
        world_header = ROOT / "components/tes3mp/include/tes3mp/interactive_object_world.hpp"
        self.assertTrue(catalog_header.exists())
        self.assertTrue(world_header.exists())

    def test_interactive_object_sources_have_no_openmw_or_engine_dependencies(self):
        catalog_src = (ROOT / "components/tes3mp/protocol/interactive_object_catalog.cpp").read_text(encoding="utf-8")
        world_src = (ROOT / "components/tes3mp/server_core/interactive_object_world.cpp").read_text(encoding="utf-8")
        catalog_hdr = (ROOT / "components/tes3mp/include/tes3mp/interactive_object_catalog.hpp").read_text(encoding="utf-8")
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/interactive_object_world.hpp").read_text(encoding="utf-8")

        all_content = catalog_src + world_src + catalog_hdr + world_hdr
        for forbidden in (
            "openmw", "mwclass", "mwmechanics", "mwworld", "components/esm", "components/sceneutil",
            "osg", "RakNet", "PacketDoorState", "PacketObjectLock", "PacketObjectTrap",
        ):
            self.assertNotIn(forbidden, all_content)

    def test_interactive_object_world_enforces_authoritative_state_model(self):
        catalog_src = (ROOT / "components/tes3mp/protocol/interactive_object_catalog.cpp").read_text(encoding="utf-8")
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/interactive_object_world.hpp").read_text(encoding="utf-8")
        world_src = (ROOT / "components/tes3mp/server_core/interactive_object_world.cpp").read_text(encoding="utf-8")

        self.assertIn("CanonicalInteractiveObjectState", world_hdr)
        self.assertIn("ObjectRevision revision", world_hdr)
        self.assertIn("DoorState", world_hdr)
        self.assertIn("LockState", world_hdr)
        self.assertIn("TrapState", world_hdr)
        self.assertIn("applyObjectInteraction", world_hdr)

        # Ensure reach, cell mismatch, lock, and trap checks are present in implementation
        self.assertIn("PlayerOutOfReach", world_src)
        self.assertIn("CellMismatch", world_src)
        self.assertIn("StaleRevision", world_src)
        self.assertIn("TrapSprung", world_src)
        self.assertIn("verifiedPlayerKeys", world_src)
        self.assertIn("TickRegression", world_src)
        self.assertIn("RevisionExhausted", world_src)
        self.assertIn("coordinateDistance", world_src)
        self.assertNotIn("dx * dx + dy * dy + dz * dz", world_src)
        self.assertIn("entry.transform.cell() != entry.cell", catalog_src)
        self.assertIn("entry.destination->transform.cell() != entry.destination->cell", catalog_src)


if __name__ == "__main__":
    unittest.main()
