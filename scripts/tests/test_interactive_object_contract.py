"""Temporary guards for OpenMW seams outside the standalone C++ test targets.

Object authority, replication, and adapter behavior have executable tests. These
source guards cover the remaining player/desktop/CLI glue, not gameplay proof;
retire them when that glue has executable integration coverage.
"""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class InteractiveObjectContractTests(unittest.TestCase):
    def test_activation_intercepts_before_local_script_execution(self):
        source = (ROOT / "apps/openmw/mwworld/player.cpp").read_text(encoding="utf-8")
        start = source.index("void Player::activate()")
        self.assertLess(source.index("mActivationInterceptor(toActivate, player)", start),
                        source.index("objectActivated(toActivate, player)", start))

    def test_desktop_door_presentation_retains_validation_guards(self):
        source = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")
        start = source.index("ProviderResult applyInteractiveObjects(")
        end = source.index("std::optional<ObjectRevision> observedObjectRevision", start)
        body = source[start:end]
        for guard in (
            "member.objectId.value() > std::numeric_limits<std::uint32_t>::max()",
            "member.revision < found->second.lastRevision",
            "member.revision == found->second.lastRevision",
            "doorPtr.getCellRef().getTeleport()",
            "teleportDoor && member.doorState != TES3MP::DoorState::Closed",
            "!teleportDoor",
        ):
            self.assertTrue(guard in body, f"applyInteractiveObjects: missing guard {guard}")

    def test_object_mapping_rejects_numeric_suffixes(self):
        source = (ROOT / "apps/openmw/main.cpp").read_text(encoding="utf-8")
        for guard in ("parsedRef.ptr != refNumEnd", "parsedFile.ptr != refPart.data() + refPart.size()"):
            self.assertTrue(guard in source, f"OpenMW object mapping: missing full-number check {guard}")


if __name__ == "__main__":
    unittest.main()
