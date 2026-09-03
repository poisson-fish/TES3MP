import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class OpenMWPatchRegistryTests(unittest.TestCase):
    def test_repository_registry_passes(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_openmw_patch_registry.py")],
            cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_registry_has_required_semantic_fields(self):
        data = json.loads((ROOT / "docs/vnext/OPENMW_PATCH_REGISTRY.json").read_text(encoding="utf-8"))
        required = {"id", "slice", "owner", "purpose", "paths", "tests", "disposition", "removal_condition"}
        self.assertTrue(data["patches"])
        self.assertTrue(all(set(entry) == required for entry in data["patches"]))

    def test_engine_hook_order_and_shutdown_are_exact(self):
        source = (ROOT / "apps/openmw/engine.cpp").read_text(encoding="utf-8")
        frame = source.index("bool OMW::Engine::frame")
        input_update = source.index("mInputManager->update(frametime, false);", frame)
        coordinator = source.index("mMultiplayerCoordinator->frame(frametime);", input_update)
        lua = source.index("mLuaManager->synchronizedUpdate();", coordinator)
        self.assertLess(input_update, coordinator)
        self.assertLess(coordinator, lua)
        destructor = source.index("OMW::Engine::~Engine")
        reset = source.index("mMultiplayerCoordinator.reset();", destructor)
        first_dependency_reset = source.index("mMechanicsManager = nullptr;", destructor)
        self.assertLess(reset, first_dependency_reset)


if __name__ == "__main__":
    unittest.main()
