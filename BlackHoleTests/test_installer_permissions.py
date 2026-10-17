#!/usr/bin/env python3
"""Exercise installer permission repair without root or a live audio install.

Run from the repository root:
    python3 -m unittest discover -s BlackHoleTests -p 'test_installer_*.py' -v

The scripts operate on temporary target volumes using Installer's third argument.
Only chown is mocked: mkdir, chmod, and find run against real fixture files.
"""

import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
SCRIPTS = REPOSITORY / "Installer" / "scripts"


class InstallerPermissionsTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory(
            prefix="BlackHole installer tests "
        )
        self.addCleanup(self.temporary_directory.cleanup)
        self.workspace = Path(self.temporary_directory.name)
        self.volume = self.workspace / "Target Volume"
        self.volume.mkdir()
        self.hal = self.volume / "Library" / "Audio" / "Plug-Ins" / "HAL"
        self.mock_bin = self.workspace / "bin"
        self.mock_bin.mkdir()
        self.chown_log = self.workspace / "chown.jsonl"
        mock_chown = self.mock_bin / "chown"
        mock_chown.write_text(
            f"#!{sys.executable}\n"
            "import json, os, sys\n"
            "with open(os.environ['CHOWN_LOG'], 'a') as log:\n"
            "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            "sys.exit(int(os.environ.get('CHOWN_EXIT_STATUS', '0')))\n"
        )
        mock_chown.chmod(0o755)

    def run_script(self, name, *, chown_exit_status=0, trailing_slash=False):
        environment = os.environ.copy()
        environment.update(
            PATH=str(self.mock_bin) + os.pathsep + environment.get("PATH", os.defpath),
            CHOWN_LOG=str(self.chown_log),
            CHOWN_EXIT_STATUS=str(chown_exit_status),
        )
        volume = str(self.volume) + ("/" if trailing_slash else "")
        return subprocess.run(
            ["/bin/sh", str(SCRIPTS / name), "BlackHole.pkg", "/Library/Audio/Plug-Ins/HAL", volume],
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            preexec_fn=lambda: os.umask(0o077),
        )

    def assert_succeeded(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_mode(self, path, expected):
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), expected, str(path))

    def ownership_requests(self):
        if not self.chown_log.exists():
            return []
        return [json.loads(line) for line in self.chown_log.read_text().splitlines()]

    def create_driver(self, name="BlackHole2ch.driver"):
        driver = self.hal / name
        executable = driver / "Contents" / "MacOS" / "BlackHole"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"fixture executable\n")
        resources = driver / "Contents" / "Resources"
        resources.mkdir()
        resource = resources / "resource with spaces.txt"
        resource.write_bytes(b"fixture resource\n")
        info = driver / "Contents" / "Info.plist"
        info.write_bytes(b"fixture plist\n")
        for directory in [driver, *[p for p in driver.rglob("*") if p.is_dir()]]:
            directory.chmod(0o700)
        for file in [executable, resource, info]:
            file.chmod(0o600)
        return driver, executable, resource, info

    def assert_driver_permissions(self, fixture):
        driver, executable, resource, info = fixture
        for directory in [driver, *[p for p in driver.rglob("*") if p.is_dir()]]:
            self.assert_mode(directory, 0o755)
        self.assert_mode(executable, 0o755)
        self.assert_mode(resource, 0o644)
        self.assert_mode(info, 0o644)
        self.assertEqual(executable.read_bytes(), b"fixture executable\n")
        self.assertEqual(resource.read_bytes(), b"fixture resource\n")
        self.assertEqual(info.read_bytes(), b"fixture plist\n")

    def test_preinstall_creates_accessible_hal_with_restrictive_umask(self):
        self.assert_succeeded(self.run_script("preinstall", trailing_slash=True))

        self.assert_mode(self.hal, 0o755)
        self.assertEqual(self.ownership_requests(), [["root:wheel", str(self.hal)]])

    def test_preinstall_repairs_existing_restrictive_hal(self):
        self.hal.mkdir(parents=True)
        self.hal.chmod(0o700)

        self.assert_succeeded(self.run_script("preinstall"))

        self.assert_mode(self.hal, 0o755)

    def test_postinstall_repairs_hal_after_payload_and_all_blackhole_variants(self):
        fixtures = [self.create_driver("BlackHole2ch.driver"), self.create_driver("BlackHole16ch.driver")]
        # A package payload can reapply the staging root's mode after preinstall.
        self.hal.chmod(0o700)
        unrelated = self.create_driver("Other Audio.driver")
        unrelated_modes = {path: stat.S_IMODE(path.stat().st_mode) for path in unrelated}

        self.assert_succeeded(self.run_script("postinstall"))

        self.assert_mode(self.hal, 0o755)
        for fixture in fixtures:
            self.assert_driver_permissions(fixture)
        for path, mode in unrelated_modes.items():
            self.assert_mode(path, mode)
        self.assertEqual(unrelated[1].read_bytes(), b"fixture executable\n")
        requests = self.ownership_requests()
        self.assertIn(["root:wheel", str(self.hal)], requests)
        for fixture in fixtures:
            self.assertTrue(
                any(
                    request[:2] == ["-R", "root:wheel"] and str(fixture[0]) in request[2:]
                    for request in requests
                ),
                str(fixture[0]),
            )
        self.assertFalse(any(str(unrelated[0]) in request for request in requests))

    def test_postinstall_repair_is_idempotent(self):
        fixture = self.create_driver()
        self.assert_succeeded(self.run_script("postinstall"))
        self.assert_succeeded(self.run_script("postinstall", trailing_slash=True))

        self.assert_mode(self.hal, 0o755)
        self.assert_driver_permissions(fixture)

    def test_postinstall_reports_missing_payload(self):
        self.hal.mkdir(parents=True)
        self.hal.chmod(0o700)

        result = self.run_script("postinstall")

        self.assertNotEqual(result.returncode, 0)
        self.assert_mode(self.hal, 0o755)

    def test_preinstall_reports_ownership_failure(self):
        self.hal.mkdir(parents=True)
        self.hal.chmod(0o700)

        result = self.run_script("preinstall", chown_exit_status=19)

        self.assertNotEqual(result.returncode, 0)
        self.assert_mode(self.hal, 0o700)

    def test_postinstall_reports_ownership_failure_before_touching_bundles(self):
        fixture = self.create_driver()
        self.hal.chmod(0o700)

        result = self.run_script("postinstall", chown_exit_status=19)

        self.assertNotEqual(result.returncode, 0)
        self.assert_mode(self.hal, 0o700)
        self.assert_mode(fixture[1], 0o600)
        self.assertEqual(self.ownership_requests(), [["root:wheel", str(self.hal)]])


if __name__ == "__main__":
    unittest.main()
