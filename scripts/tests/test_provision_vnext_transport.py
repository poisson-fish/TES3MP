from __future__ import annotations

import unittest
from unittest import mock

from scripts import provision_vnext_transport as provision


class TransportProvisionTests(unittest.TestCase):
    def test_missing_inputs_run_proofs_before_writing_manifest(self) -> None:
        manifest = {"schema_version": 1}
        with (
            mock.patch.object(provision, "_inputs_are_present", return_value=False),
            mock.patch.object(provision, "_run_proofs") as run_proofs,
            mock.patch.object(provision, "build_manifest", return_value=manifest),
            mock.patch.object(provision.pathlib.Path, "mkdir"),
            mock.patch.object(provision.pathlib.Path, "write_text") as write_text,
        ):
            self.assertEqual(provision.main([]), 0)
        run_proofs.assert_called_once_with()
        self.assertIn('"schema_version": 1', write_text.call_args.args[0])

    def test_present_inputs_are_reused_without_rerunning_proofs(self) -> None:
        with (
            mock.patch.object(provision, "_inputs_are_present", return_value=True),
            mock.patch.object(provision, "_run_proofs") as run_proofs,
            mock.patch.object(provision, "build_manifest", return_value={"schema_version": 1}),
            mock.patch.object(provision.pathlib.Path, "mkdir"),
            mock.patch.object(provision.pathlib.Path, "write_text"),
        ):
            self.assertEqual(provision.main([]), 0)
        run_proofs.assert_not_called()


if __name__ == "__main__":
    unittest.main()
