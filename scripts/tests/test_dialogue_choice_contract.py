import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class DialogueChoiceContractTests(unittest.TestCase):
    def test_wire_contract_is_typed_bounded_and_presentation_free(self):
        command = (ROOT / "components/tes3mp/protocol/schema/client_dialogue_choice_command.fbs").read_text(
            encoding="utf-8")
        result = (ROOT / "components/tes3mp/protocol/schema/reliable_dialogue_choice_result.fbs").read_text(
            encoding="utf-8")
        for text in (command, result):
            self.assertIn("choice_id:ulong", text)
            self.assertNotIn("string", text)
        self.assertIn('file_identifier "T3DC"', command)
        self.assertIn('file_identifier "T3DR"', result)

    def test_server_admission_uses_authenticated_session_and_canonical_world(self):
        coordinator = (ROOT / "apps/tes3mp-server/connection_session_coordinator.cpp").read_text(encoding="utf-8")
        reducer = (ROOT / "components/tes3mp/server_core/server_command_reducer.cpp").read_text(encoding="utf-8")
        application = (ROOT / "apps/tes3mp-server/server_application.cpp").read_text(encoding="utf-8")
        self.assertIn("decodeClientDialogueChoiceCommand", coordinator)
        self.assertIn("DialogueChoiceCommandProposal", coordinator)
        self.assertIn("validateCanonicalDialogueChoice", reducer)
        admission = application.index("admitCombinedInterestTickAtomically")
        commit = application.index("mWiring->reducer.commit(std::move(prepared)", admission)
        pump = application.index("mWiring->queues.pump", commit)
        self.assertLess(admission, commit)
        self.assertLess(commit, pump)
        self.assertIn("MaximumRetainedDialogueChoiceResults", application)

    def test_openmw_intercepts_before_local_consequences_and_exposes_pending_rejected_states(self):
        source = (ROOT / "apps/openmw/mwgui/dialogue.cpp").read_text(encoding="utf-8")
        start = source.index("void DialogueWindow::onChoiceActivated")
        end = source.index("void DialogueWindow::onGoodbyeActivated", start)
        handler = source[start:end]
        submit = handler.index("submitDialogueChoice(id)")
        early_return = handler.index("return;", submit)
        local_effect = handler.index("questionAnswered(id", early_return)
        self.assertLess(submit, early_return)
        self.assertLess(early_return, local_effect)
        self.assertIn("waiting for server", source)
        self.assertIn("rejected by server", source)
        self.assertIn("resolved->committed() && sameActor", source)
        self.assertIn("typesetter->write(questionStyle, choice.first)", source)

    def test_disconnect_retry_preserves_command_identity(self):
        adapter = (ROOT / "apps/openmw/tes3mp/adapter.cpp").read_text(encoding="utf-8")
        tests = (ROOT / "apps/openmw/tes3mp/adapter_tests.cpp").read_text(encoding="utf-8")
        self.assertIn("preservesDialogueChoice", adapter)
        self.assertIn("mPendingDialogueChoice->commandId", adapter)
        self.assertIn("sentDialogueChoices.size() == 2", tests)
        self.assertIn("sentDialogueChoices.back().commandId == retainedDialogueCommandId", tests)

    def test_manifest_mapping_and_tests_are_registered(self):
        mappings = (ROOT / "files/data/tes3mp/vanilla-client-mappings.cfg").read_text(encoding="utf-8")
        cmake = (ROOT / "components/tes3mp/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("tes3mp-content-dialogue-choice-map=1=1", mappings)
        self.assertIn("tes3mp-content-dialogue-choice-map=2=2", mappings)
        self.assertIn("add_executable(tes3mp_dialogue_choice_protocol_tests", cmake)
        self.assertIn("COMMAND $<TARGET_FILE:tes3mp_dialogue_choice_protocol_tests>", cmake)


if __name__ == "__main__":
    unittest.main()
