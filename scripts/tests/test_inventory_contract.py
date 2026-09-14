"""Temporary UI-hook guards; inventory behavior belongs to the C++ tests.

Dependency guards live in test_tes3mp_target_boundaries.py. Retire the source
ordering check when the OpenMW UI hooks have executable integration coverage.
"""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class InventoryContractTests(unittest.TestCase):
    def test_cpp_inventory_assertions_remain_active_in_release_builds(self):
        for name in ("inventory_catalog_tests.cpp", "inventory_world_tests.cpp"):
            source = (ROOT / "components/tes3mp/tests" / name).read_text(encoding="utf-8")
            for marker in ("#undef assert", "#define assert(condition) require"):
                self.assertTrue(marker in source, f"{name}: missing release assertion guard {marker}")

    def test_openmw_inventory_ui_intercepts_before_local_mutation(self):
        cases = (
            ("itemmodel.cpp", "MWWorld::Ptr ItemModel::moveItem",
             "interceptTransfer(item, count", "removeItem(item, count);"),
            ("inventorywindow.cpp", "void InventoryWindow::useItem",
             "sUseItemInterceptor", "ptr.getClass().use"),
            ("containeritemmodel.cpp", "bool ContainerItemModel::onTakeItem",
             "interceptTransfer", "itemTaken"),
        )
        for name, function, interceptor, mutation in cases:
            with self.subTest(source=name):
                source = (ROOT / "apps/openmw/mwgui" / name).read_text(encoding="utf-8")
                start = source.index(function)
                self.assertLess(source.index(interceptor, start), source.index(mutation, start),
                                f"{name}: multiplayer interception must precede local mutation")


if __name__ == "__main__":
    unittest.main()
