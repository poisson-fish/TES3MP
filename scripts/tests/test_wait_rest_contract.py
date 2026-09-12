import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class WaitRestContractTests(unittest.TestCase):
    def test_openmw_intercepts_before_stock_time_and_resource_mutation(self):
        source = (ROOT / "apps/openmw/mwgui/waitdialog.cpp").read_text(encoding="utf-8")
        start = source.index("void WaitDialog::startWaiting")
        end = source.index("void WaitDialog::onCancelButtonClicked", start)
        body = source[start:end]
        submit = body.index("submitWaitRest")
        authority_return = body.index("return;", submit)
        self.assertLess(submit, authority_return)
        self.assertLess(authority_return, body.index("quickSave"))
        self.assertNotIn("advanceTime", body[:authority_return])
        self.assertNotIn("getMechanicsManager()->rest", body[:authority_return])

    def test_server_requires_exact_active_session_consensus_and_atomic_domains(self):
        source = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")
        consent = source.index("waitRestConsentsCandidate.size() == prepared.candidateState().activeSessions().size()")
        exact = source.index("found->second.request == first", consent)
        recovery = source.index("applyAuthoritativeWaitRestRecovery", exact)
        time = source.index("advanceCanonicalWorldTimeByHours", recovery)
        stage = source.index("stageSimulationCandidates", time)
        commit = source.index("reducer.commit", stage)
        self.assertLess(consent, exact)
        self.assertLess(exact, recovery)
        self.assertLess(recovery, time)
        self.assertLess(time, stage)
        self.assertLess(stage, commit)

    def test_bounds_combat_gate_and_session_generation_are_explicit(self):
        primitive = (ROOT / "components/tes3mp/include/tes3mp/wait_rest.hpp").read_text(encoding="utf-8")
        combat = (ROOT / "components/tes3mp/server_core/combat_world.cpp").read_text(encoding="utf-8")
        application = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")
        self.assertIn("MaximumWaitRestHours = 24", primitive)
        self.assertIn("WaitRestRecoveryError::ActiveCombat", combat)
        self.assertIn("active->sessionGeneration() != entry.second.generation", application)


if __name__ == "__main__":
    unittest.main()
