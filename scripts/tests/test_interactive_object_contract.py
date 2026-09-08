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
        spatial_hdr = (ROOT / "components/tes3mp/include/tes3mp/spatial_types.hpp").read_text(encoding="utf-8")
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
        self.assertIn("positionsWithinReach", world_src)
        self.assertIn("coordinateDistance", spatial_hdr)
        self.assertIn("positionsWithinReach", spatial_hdr)
        self.assertNotIn("dx * dx + dy * dy + dz * dz", world_src)
        self.assertIn("entry.transform.cell() != entry.cell", catalog_src)
        self.assertIn("entry.destination->transform.cell() != entry.destination->cell", catalog_src)

    def test_interactive_object_replication_and_projection_headers_and_sources_exist(self):
        repl_hdr = ROOT / "components/tes3mp/include/tes3mp/interactive_object_replication.hpp"
        repl_src = ROOT / "components/tes3mp/protocol/interactive_object_replication.cpp"
        proj_hdr = ROOT / "apps/tes3mp-server/interactive_object_interest_projection.hpp"
        proj_src = ROOT / "apps/tes3mp-server/interactive_object_interest_projection.cpp"
        schema_baseline = ROOT / "components/tes3mp/protocol/schema/reliable_interactive_object_interest_baseline.fbs"
        schema_cmd = ROOT / "components/tes3mp/protocol/schema/client_interact_object_command.fbs"
        gen_baseline = ROOT / "components/tes3mp/protocol/generated/reliable_interactive_object_interest_baseline_generated.h"
        gen_cmd = ROOT / "components/tes3mp/protocol/generated/client_interact_object_command_generated.h"

        for p in (repl_hdr, repl_src, proj_hdr, proj_src, schema_baseline, schema_cmd, gen_baseline, gen_cmd):
            self.assertTrue(p.exists(), f"{p} must exist")

    def test_interactive_object_replication_and_projection_have_no_forbidden_dependencies(self):
        repl_hdr = (ROOT / "components/tes3mp/include/tes3mp/interactive_object_replication.hpp").read_text(encoding="utf-8")
        repl_src = (ROOT / "components/tes3mp/protocol/interactive_object_replication.cpp").read_text(encoding="utf-8")
        proj_hdr = (ROOT / "apps/tes3mp-server/interactive_object_interest_projection.hpp").read_text(encoding="utf-8")
        proj_src = (ROOT / "apps/tes3mp-server/interactive_object_interest_projection.cpp").read_text(encoding="utf-8")

        all_content = repl_hdr + repl_src + proj_hdr + proj_src
        for forbidden in (
            "openmw", "mwclass", "mwmechanics", "mwworld", "components/esm", "components/sceneutil",
            "osg", "RakNet", "PacketDoorState", "PacketObjectLock", "PacketObjectTrap",
        ):
            self.assertNotIn(forbidden, all_content)

    def test_interactions_use_canonical_ordering_and_publish_authoritative_outcomes(self):
        coordinator = (ROOT / "apps/tes3mp-server/connection_session_coordinator.cpp").read_text(encoding="utf-8")
        application = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")
        intake = (ROOT / "components/tes3mp/include/tes3mp/server_command_intake.hpp").read_text(encoding="utf-8")
        publication = (ROOT / "components/tes3mp/include/tes3mp/canonical_publication.hpp").read_text(encoding="utf-8")
        reducer = (ROOT / "components/tes3mp/server_core/server_command_reducer.cpp").read_text(encoding="utf-8")

        self.assertIn("InteractiveObjectCommandProposal", intake)
        self.assertIn("intake.submit(std::move(proposal))", coordinator)
        self.assertNotIn("pendingInteractions", coordinator)
        self.assertIn("prepareTick(", application)
        self.assertIn("candidateInteractiveObjects", application)
        self.assertIn("objectInteractionOutcome", publication)
        self.assertIn("outcome.playerTeleport", reducer)
        self.assertIn("destination.transform", reducer)
        self.assertIn("mBaseInteractiveObjects", reducer)
        self.assertIn("applyObjectInteractionToCandidate", reducer)

    def test_production_server_loads_objects_and_defers_key_unlock_until_inventory_authority(self):
        content_header = ROOT / "apps/tes3mp-server/interactive_object_content.hpp"
        content_source = ROOT / "apps/tes3mp-server/interactive_object_content.cpp"
        main = (ROOT / "apps/tes3mp-server/main.cpp").read_text(encoding="utf-8")
        config = (ROOT / "apps/tes3mp-server/server_config.cpp").read_text(encoding="utf-8")
        coordinator = (ROOT / "apps/tes3mp-server/connection_session_coordinator.cpp").read_text(encoding="utf-8")

        self.assertTrue(content_header.exists())
        self.assertTrue(content_source.exists())
        self.assertIn("interactive_object_content_file", config)
        self.assertIn("loadInteractiveObjectContent", main)
        self.assertIn("interactiveObjectReplicationCapability", main)
        self.assertIn("interactiveObjectCatalog ? &*interactiveObjectCatalog : nullptr", main)
        self.assertIn("cmd->kind == ObjectInteractionKind::UnlockWithKey", coordinator)
        self.assertIn("return ConnectionSessionResult::ProtocolRejected", coordinator)

    def test_client_reception_and_openmw_presentation_contract(self):
        client_session_hdr = (ROOT / "components/tes3mp/include/tes3mp/client_session.hpp").read_text(encoding="utf-8")
        client_session_src = (ROOT / "components/tes3mp/client_session/client_session.cpp").read_text(encoding="utf-8")
        client_runtime_hdr = (ROOT / "components/tes3mp/include/tes3mp/client_session_runtime.hpp").read_text(encoding="utf-8")
        client_runtime_src = (ROOT / "components/tes3mp/client_session/client_session_runtime.cpp").read_text(encoding="utf-8")
        adapter_src = (ROOT / "apps/openmw/tes3mp/adapter.cpp").read_text(encoding="utf-8")
        connection_src = (ROOT / "apps/openmw/tes3mp/client_connection.cpp").read_text(encoding="utf-8")
        openmw_main = (ROOT / "apps/openmw/main.cpp").read_text(encoding="utf-8")
        providers_hdr = (ROOT / "apps/openmw/tes3mp/providers.hpp").read_text(encoding="utf-8")
        desktop_hdr = (ROOT / "apps/openmw/tes3mp/desktop_providers.hpp").read_text(encoding="utf-8")
        desktop_src = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")

        # Client session state machine
        self.assertIn("receiveReliableInteractiveObjectInterestBaseline", client_session_hdr)
        self.assertIn("observedInteractiveObjects", client_session_hdr)
        self.assertIn("confirmedInteractiveObjectInterestBaseline", client_session_hdr)
        self.assertIn("interactiveObjectInterestBaselineComplete", client_session_hdr)
        self.assertIn("receiveReliableInteractiveObjectInterestBaseline", client_session_src)

        # Client session runtime frame ingestion and resync tracking
        self.assertIn("interactiveObjectBaselineApplied", client_runtime_hdr)
        self.assertIn("interactiveObjectBaselineCompleted", client_runtime_hdr)
        self.assertIn("mResyncObjectBaselineObserved", client_runtime_hdr)
        self.assertIn("MessageKind::ReliableInteractiveObjectInterestBaseline", client_runtime_src)

        # Adapter coordinator capability negotiation, revision gating, and resync gating
        self.assertIn("interactiveObjectReplicationCapability", adapter_src)
        self.assertIn("interactiveObjectsNegotiated", adapter_src)
        self.assertIn("mMinimumObjectBaselineRevision", adapter_src)
        self.assertIn("mResyncObjectBaseline", adapter_src)
        self.assertIn("applyInteractiveObjects", adapter_src)
        production_capabilities = (
            "const std::array optional{ vrPoseCapability(), actorReplicationCapability(), "
            "interactiveObjectReplicationCapability() };"
        )
        old_capabilities = "const std::array optional{ vrPoseCapability(), actorReplicationCapability() };"
        for src in (adapter_src, connection_src):
            compact = " ".join(src.split())
            self.assertIn(production_capabilities, compact)
            self.assertNotIn(old_capabilities, compact)
            self.assertEqual(compact.count("const std::array optional{"), 1)

        # OpenMW Activation raycast interception, input capture, and desktop provider wiring
        player_hdr = (ROOT / "apps/openmw/mwworld/player.hpp").read_text(encoding="utf-8")
        player_src = (ROOT / "apps/openmw/mwworld/player.cpp").read_text(encoding="utf-8")
        self.assertIn("captureObjectInteraction", providers_hdr)
        self.assertIn("clearSessionState", providers_hdr)
        self.assertIn("observedObjectRevision", providers_hdr)
        self.assertIn("mActivationInterceptor", player_hdr)
        self.assertIn("setActivationInterceptor", player_hdr)
        self.assertIn("mActivationInterceptor(toActivate, player)", player_src)
        self.assertIn("queueObjectActivation", desktop_hdr)
        self.assertIn("captureObjectInteraction", desktop_hdr)
        self.assertIn("observedObjectRevision", desktop_hdr)
        self.assertIn("captureObjectInteraction", desktop_src)
        self.assertIn("clearSessionState", desktop_src)
        self.assertIn("observedObjectRevision", desktop_src)
        self.assertIn("queueInteractObject", adapter_src)

        # OpenMW Presentation provider and desktop implementation
        self.assertIn("applyInteractiveObjects", providers_hdr)
        self.assertIn("DesktopInteractiveObjectMapping", desktop_hdr)
        self.assertIn("applyInteractiveObjects", desktop_hdr)
        self.assertIn("applyInteractiveObjects", desktop_src)
        self.assertIn("activateDoor", desktop_src)
        self.assertIn("rotateObject", desktop_src)
        self.assertIn("observedDoors", desktop_src)
        self.assertIn("member.objectId.value() > std::numeric_limits<std::uint32_t>::max()", desktop_src)
        self.assertIn("member.revision < found->second.lastRevision", desktop_src)
        self.assertIn("member.revision == found->second.lastRevision", desktop_src)
        self.assertIn("doorPtr.getCellRef().getTeleport()", desktop_src)
        self.assertIn("!teleportDoor", desktop_src)

        # Mapping fields must be parsed in full, without numeric-prefix acceptance.
        self.assertIn("parsedRef.ptr != refNumEnd", openmw_main)
        self.assertIn("parsedFile.ptr != refPart.data() + refPart.size()", openmw_main)

        # Verify no forbidden legacy networking references
        for text in (client_session_src, client_runtime_src, adapter_src, desktop_src):
            for forbidden in ("PacketDoorState", "PacketObjectLock", "PacketObjectTrap", "RakNet"):
                self.assertNotIn(forbidden, text)


if __name__ == "__main__":
    unittest.main()
