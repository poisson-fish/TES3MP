import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
TES3MP = ROOT / "components" / "tes3mp"


class Phase9PoseProtocolTests(unittest.TestCase):
    def test_pose_values_are_presentation_only(self):
        header = (TES3MP / "include" / "tes3mp" / "protocol_pose.hpp").read_text(
            encoding="utf-8"
        )
        for forbidden in (
            "CanonicalRevision",
            "ServerTick",
            "CommandSequence",
            "Platform",
            "OpenXR",
            "Renderer",
            "Transport",
        ):
            self.assertNotIn(forbidden, header)

    def test_schemas_are_directional_bounded_and_collection_free(self):
        client = (TES3MP / "protocol" / "schema" / "client_vr_pose_sample.fbs").read_text(
            encoding="utf-8"
        )
        server = (TES3MP / "protocol" / "schema" / "server_vr_pose_snapshot.fbs").read_text(
            encoding="utf-8"
        )
        self.assertIn('file_identifier "T3VP";', client)
        self.assertIn('file_identifier "T3VR";', server)
        self.assertIn("root_type ClientVrPoseSample;", client)
        self.assertIn("root_type ServerVrPoseSnapshot;", server)
        self.assertNotIn("source_player_id", client)
        self.assertIn("source_player_id:ulong", server)
        self.assertNotIn("[", client + server)
        for forbidden in (
            "canonical_revision",
            "server_tick",
            "acknowledged",
            "platform",
            "timestamp",
            "opaque",
        ):
            self.assertNotIn(forbidden, (client + server).lower())

    def test_runtime_advertising_dispatch_and_transport_mapping_remain_disabled(self):
        production = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                ROOT / "apps" / "openmw" / "tes3mp" / "adapter.cpp",
                ROOT / "apps" / "openmw" / "tes3mp" / "client_connection.cpp",
                ROOT / "apps" / "tes3mp-headless-client" / "main.cpp",
                ROOT / "apps" / "tes3mp-server" / "main.cpp",
                TES3MP / "client_session" / "client_session_runtime.cpp",
                TES3MP / "transport" / "transport.cpp",
            )
        )
        self.assertNotIn("vrPoseCapability", production)
        self.assertNotIn("MessageKind::ClientVrPoseSample", production)
        self.assertNotIn("MessageKind::ServerVrPoseSnapshot", production)
        self.assertNotIn("case MessageClass::PresentationSample", production)

    def test_pose_codec_target_has_only_protocol_dependencies(self):
        cmake = (TES3MP / "CMakeLists.txt").read_text(encoding="utf-8")
        target = cmake.split("add_executable(tes3mp_protocol_pose_tests", 1)[1].split(
            "add_executable(tes3mp_session_state_tests", 1
        )[0]
        self.assertIn(
            "target_link_libraries(tes3mp_protocol_pose_tests PRIVATE tes3mp_protocol)",
            target,
        )
        self.assertIn(
            "tes3mp_verify_target_dependencies(tes3mp_protocol_pose_tests ALLOWED tes3mp_protocol)",
            target,
        )
        self.assertNotIn("openmw", target.lower())
        self.assertNotIn("openxr", target.lower())
        self.assertNotIn("tes3mp_transport", target)


if __name__ == "__main__":
    unittest.main()
