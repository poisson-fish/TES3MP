import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ReplicatedActorContractTests(unittest.TestCase):
    def test_old_transient_npc_initializer_is_removed(self):
        self.assertFalse((ROOT / "apps/openmw/mwrender/transientactorpresentation.cpp").exists())
        self.assertFalse((ROOT / "apps/openmw/mwrender/transientactorpresentation.hpp").exists())
        npc = (ROOT / "apps/openmw/mwclass/npc.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/openmw/mwclass/npc.hpp").read_text(encoding="utf-8")
        self.assertNotIn("ensureTransientPresentationData", npc + header)
        self.assertNotIn("initializeCustomData", npc + header)

    def test_replica_build_has_no_gameplay_registration_or_state_access(self):
        source = (ROOT / "apps/openmw/mwrender/replicatedactor.cpp").read_text(encoding="utf-8")
        for forbidden in (
                "registerPtr", "getInventoryStore", "getContainerStore", "getCreatureStats",
                "getNpcStats", "getMechanicsManager", "getPhysics", "getNavigator", "getLuaManager",
                "getPrng", "PtrHolder"):
            self.assertNotIn(forbidden, source)
        self.assertIn("insertBegin(ptr, false)", source)
        self.assertIn("Context::ReplicatedActor", source)

    def test_visibility_role_is_rendered_but_not_intersected(self):
        vismask = (ROOT / "apps/openmw/mwrender/vismask.hpp").read_text(encoding="utf-8")
        rendering = (ROOT / "apps/openmw/mwrender/renderingmanager.cpp").read_text(encoding="utf-8")
        water = (ROOT / "apps/openmw/mwrender/water.cpp").read_text(encoding="utf-8")
        self.assertIn("Mask_ReplicatedActor", vismask)
        self.assertIn("Mask_Actor | Mask_ReplicatedActor", rendering)
        self.assertIn("Mask_Groundcover | Mask_ReplicatedActor", rendering)
        self.assertIn("Mask_Player | Mask_Actor | Mask_ReplicatedActor", water)

    def test_adapter_owns_opaque_handles_and_maps_typed_results(self):
        source = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")
        self.assertIn("std::unique_ptr<MWRender::ReplicatedActor>", source)
        self.assertIn("mapReplicatedActorResult", source)
        for result in (
                "InvalidAppearanceRecord", "MissingAppearanceDependency", "ResourceLoadFailed",
                "CapacityExceeded", "InvalidPose", "LifecycleViolation", "AnimationFallback"):
            self.assertIn(result, source)
        content_mapping = source.index("case Result::MissingAppearanceDependency:")
        resource_failure = source.index("case Result::ResourceLoadFailed:")
        presentation_mapping = source.index("case Result::CapacityExceeded:")
        self.assertLess(content_mapping, resource_failure)
        self.assertLess(resource_failure, presentation_mapping)

    def test_provider_reconciles_retimestamped_unchanged_revisions_idempotently(self):
        source = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")
        self.assertIn("sameReplicatedState", source)
        self.assertIn("entry->entityRevision() == found->second.lastObserved->entityRevision()", source)
        self.assertIn("contradictory same-revision observation", source)

    def test_provider_reconcile_scratch_space_is_capacity_bounded_without_heap_churn(self):
        source = (ROOT / "apps/openmw/tes3mp/desktop_providers.cpp").read_text(encoding="utf-8")
        self.assertIn("std::array<std::optional<EntityId>, MWRender::MaximumReplicatedActors> desired", source)
        self.assertNotIn("std::vector<EntityId> desired", source)


if __name__ == "__main__":
    unittest.main()
