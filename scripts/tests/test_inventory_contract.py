import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class InventoryContractTests(unittest.TestCase):
    def test_inventory_headers_and_sources_exist_in_engine_independent_component(self):
        catalog_header = ROOT / "components/tes3mp/include/tes3mp/item_catalog.hpp"
        world_header = ROOT / "components/tes3mp/include/tes3mp/inventory_world.hpp"
        catalog_src = ROOT / "components/tes3mp/protocol/item_catalog.cpp"
        world_src = ROOT / "components/tes3mp/server_core/inventory_world.cpp"

        self.assertTrue(catalog_header.exists(), "item_catalog.hpp must exist")
        self.assertTrue(world_header.exists(), "inventory_world.hpp must exist")
        self.assertTrue(catalog_src.exists(), "item_catalog.cpp must exist")
        self.assertTrue(world_src.exists(), "inventory_world.cpp must exist")

    def test_inventory_sources_have_no_openmw_or_engine_dependencies(self):
        catalog_src = (ROOT / "components/tes3mp/protocol/item_catalog.cpp").read_text(encoding="utf-8")
        world_src = (ROOT / "components/tes3mp/server_core/inventory_world.cpp").read_text(encoding="utf-8")
        catalog_hdr = (ROOT / "components/tes3mp/include/tes3mp/item_catalog.hpp").read_text(encoding="utf-8")
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/inventory_world.hpp").read_text(encoding="utf-8")

        all_content = catalog_src + world_src + catalog_hdr + world_hdr
        for forbidden in (
            "openmw", "mwclass", "mwmechanics", "mwworld", "components/esm", "components/sceneutil",
            "osg", "RakNet", "PacketPlayerInventory", "PacketContainer", "PacketPlayerEquipment",
        ):
            self.assertNotIn(forbidden, all_content, f"Forbidden reference '{forbidden}' found in inventory core")

    def test_strong_value_types_defined_for_inventory(self):
        val_types = (ROOT / "components/tes3mp/include/tes3mp/value_types.hpp").read_text(encoding="utf-8")
        for type_name in (
            "ItemPrototypeId",
            "ItemStackId",
            "InventoryRevision",
            "ContainerRevision",
            "WorldItemRevision",
            "ContainerId",
        ):
            self.assertIn(type_name, val_types, f"{type_name} must be declared in value_types.hpp")

    def test_inventory_world_enforces_authoritative_state_model_and_acid_transfer(self):
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/inventory_world.hpp").read_text(encoding="utf-8")
        world_src = (ROOT / "components/tes3mp/server_core/inventory_world.cpp").read_text(encoding="utf-8")

        # Canonical data models
        self.assertIn("CanonicalItemStack", world_hdr)
        self.assertIn("CanonicalPlayerInventoryState", world_hdr)
        self.assertIn("CanonicalContainerInventoryState", world_hdr)
        self.assertIn("CanonicalWorldItemState", world_hdr)
        self.assertIn("CanonicalInventoryWorld", world_hdr)
        self.assertIn("EquipmentSlot", world_hdr)
        self.assertIn("InventoryTransactionCommand", world_hdr)
        self.assertIn("InventoryTransactionKind", world_hdr)
        self.assertIn("InventoryTransactionResultCode", world_hdr)
        self.assertIn("applyInventoryTransaction", world_hdr)
        self.assertIn("collectVerifiedKeys", world_hdr)

        # Atomic transaction checks in implementation
        self.assertIn("StaleInventoryRevision", world_src)
        self.assertIn("StaleContainerRevision", world_src)
        self.assertIn("PlayerOutOfReach", world_src)
        self.assertIn("CellMismatch", world_src)
        self.assertIn("ContainerCapacityExceeded", world_src)
        self.assertIn("SlotNotCompatible", world_src)
        self.assertIn("InsufficientCount", world_src)
        self.assertIn("positionsWithinReach", world_src)
        self.assertIn("MissingExpectedRevision", world_src)
        self.assertIn("StackIdExhausted", world_src)
        self.assertIn("WorldItemNotFound", world_src)
        self.assertIn("const CanonicalServerState& players", world_hdr)
        self.assertIn("players.findPlayer(command.player)", world_src)
        self.assertIn("playerState->lastSpatialChangeTick()", world_src)
        self.assertNotIn("CellId playerCell", world_hdr)
        self.assertNotIn("Position3 playerRoot", world_hdr)

    def test_inventory_world_does_not_expose_mutable_canonical_state(self):
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/inventory_world.hpp").read_text(encoding="utf-8")

        public_section, private_section = world_hdr.split("    private:", maxsplit=1)
        self.assertNotIn("\n        CanonicalPlayerInventoryState* findPlayer", public_section)
        self.assertNotIn("\n        CanonicalContainerInventoryState* findContainer", public_section)
        self.assertNotIn("\n        CanonicalWorldItemState* findWorldItem", public_section)
        self.assertIn("findMutablePlayer", private_section)
        self.assertIn("findMutableContainer", private_section)

    def test_key_verification_bridge_links_inventory_to_object_interaction(self):
        world_hdr = (ROOT / "components/tes3mp/include/tes3mp/inventory_world.hpp").read_text(encoding="utf-8")
        world_src = (ROOT / "components/tes3mp/server_core/inventory_world.cpp").read_text(encoding="utf-8")
        tests_src = (ROOT / "components/tes3mp/tests/inventory_world_tests.cpp").read_text(encoding="utf-8")

        self.assertIn("collectVerifiedKeys", world_hdr)
        self.assertIn("KeyPrototypeId", world_hdr)
        self.assertIn("verifiedPlayerKeys", tests_src)
        self.assertIn("applyObjectInteraction", tests_src)

    def test_inventory_targets_registered_in_cmake_and_test_runner(self):
        cmake = (ROOT / "components/tes3mp/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("add_executable(tes3mp_item_catalog_tests", cmake)
        self.assertIn("add_executable(tes3mp_inventory_world_tests", cmake)
        self.assertIn("tes3mp_verify_target_dependencies(tes3mp_item_catalog_tests", cmake)
        self.assertIn("tes3mp_verify_target_dependencies(tes3mp_inventory_world_tests", cmake)
        self.assertIn("COMMAND $<TARGET_FILE:tes3mp_item_catalog_tests>", cmake)
        self.assertIn("COMMAND $<TARGET_FILE:tes3mp_inventory_world_tests>", cmake)
        self.assertIn("add_executable(tes3mp_inventory_replication_tests", cmake)
        self.assertIn("COMMAND $<TARGET_FILE:tes3mp_inventory_replication_tests>", cmake)

    def test_inventory_replication_and_server_composition_are_bounded_and_private(self):
        replication = (ROOT / "components/tes3mp/include/tes3mp/inventory_replication.hpp").read_text(
            encoding="utf-8")
        projection = (ROOT / "apps/tes3mp-server/inventory_interest_projection.cpp").read_text(encoding="utf-8")
        coordinator = (ROOT / "apps/tes3mp-server/connection_session_coordinator.cpp").read_text(encoding="utf-8")
        application = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")

        self.assertIn("MaximumInventoryBaselineChunkStacks", replication)
        self.assertIn("MaximumGroundItemBaselineChunkItems", replication)
        self.assertIn("MaximumEquipmentSnapshotPlayers", replication)
        self.assertIn("playerInventory->stacks", projection)
        self.assertNotIn("otherInventory.stacks", projection)
        self.assertIn("decodeClientInventoryTransactionCommand", coordinator)
        self.assertIn("InventoryCommandProposal", coordinator)
        self.assertIn("admitCombinedInterestTickAtomically", application)

    def test_inventory_wire_schemas_are_registered_with_unique_identifiers(self):
        schema_dir = ROOT / "components/tes3mp/protocol/schema"
        expected = {
            "client_inventory_transaction_command.fbs": "T3IT",
            "latest_wins_equipment_snapshot.fbs": "T3EQ",
            "reliable_container_inventory_baseline.fbs": "T3CI",
            "reliable_ground_item_baseline.fbs": "T3GI",
            "reliable_player_inventory_baseline.fbs": "T3PI",
        }
        for filename, identifier in expected.items():
            text = (schema_dir / filename).read_text(encoding="utf-8")
            self.assertIn(f'file_identifier "{identifier}"', text)
            self.assertRegex(text, r"\(id: \d+\)")

    def test_cpp_inventory_assertions_remain_active_in_release_builds(self):
        for source_name in ("inventory_catalog_tests.cpp", "inventory_world_tests.cpp"):
            source = (ROOT / "components/tes3mp/tests" / source_name).read_text(encoding="utf-8")
            self.assertIn("#undef assert", source)
            self.assertIn("#define assert(condition) require", source)

    def test_openmw_client_inventory_path_is_authoritative_and_complete(self):
        session = (ROOT / "components/tes3mp/include/tes3mp/client_session.hpp").read_text(encoding="utf-8")
        session_source = (ROOT / "components/tes3mp/client_session/client_session.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "components/tes3mp/client_session/client_session_runtime.cpp").read_text(encoding="utf-8")
        providers = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")
        adapter = (ROOT / "apps/openmw/tes3mp/adapter.cpp").read_text(encoding="utf-8")

        for symbol in (
            "receiveReliablePlayerInventoryBaseline",
            "receiveReliableContainerInventoryBaseline",
            "receiveReliableGroundItemBaseline",
            "receiveLatestWinsEquipmentSnapshot",
            "inventoryReplicationComplete",
        ):
            self.assertIn(symbol, session)
        self.assertIn("decodeReliablePlayerInventoryBaseline", runtime)
        self.assertIn("queueInventoryTransaction", runtime)
        self.assertIn("PlayerInventoryChunkLimit", session_source)
        self.assertIn("ContainerInventoryChunkLimit", session_source)
        self.assertIn("GroundItemChunkLimit", session_source)
        self.assertIn("captureInventoryTransaction", adapter)
        self.assertIn("applyInventory", adapter)
        self.assertIn("!mAwaitingResync || mResyncInventory", adapter)
        self.assertIn("inventory.unequipAll()", providers)
        self.assertIn("inventory.clear()", providers)
        self.assertIn("applyPublicEquipment", providers)
        self.assertIn("world->placeObject", providers)

    def test_openmw_inventory_ui_intercepts_before_local_mutation(self):
        item_model = (ROOT / "apps/openmw/mwgui/itemmodel.cpp").read_text(encoding="utf-8")
        inventory_window = (ROOT / "apps/openmw/mwgui/inventorywindow.cpp").read_text(encoding="utf-8")
        container_model = (ROOT / "apps/openmw/mwgui/containeritemmodel.cpp").read_text(encoding="utf-8")
        move = item_model.index("MWWorld::Ptr ItemModel::moveItem")
        intercepted = item_model.index("interceptTransfer(item, count", move)
        remove = item_model.index("removeItem(item, count);", move)
        self.assertLess(intercepted, remove)
        use = inventory_window.index("void InventoryWindow::useItem")
        intercepted_use = inventory_window.index("sUseItemInterceptor", use)
        stock_use = inventory_window.index("ptr.getClass().use", use)
        self.assertLess(intercepted_use, stock_use)
        take = container_model.index("bool ContainerItemModel::onTakeItem")
        intercepted_take = container_model.index("interceptTransfer", take)
        stock_take = container_model.index("itemTaken", take)
        self.assertLess(intercepted_take, stock_take)


if __name__ == "__main__":
    unittest.main()
