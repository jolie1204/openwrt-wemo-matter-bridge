import pathlib
import sqlite3
import stat
import subprocess
import tempfile
import unittest

TOOL = pathlib.Path(__file__).parents[1] / 'initialize_matter_endpoint_identity_db.py'


class OfflineProvisioningTests(unittest.TestCase):
    def run_tool(self, output):
        return subprocess.run(['python3', str(TOOL), '--output', str(output)], capture_output=True)

    def test_fresh_private_schema_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'identity.db'
            self.assertEqual(self.run_tool(output).returncode, 0)
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
            db = sqlite3.connect(output)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM endpoint_identity').fetchone(), (0,))
            db.execute("INSERT INTO endpoint_identity VALUES ('uuid:fixture','GEN-private-fixture')")
            db.commit(); db.close()
            before = output.read_bytes()
            result = self.run_tool(output)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), before)
            self.assertNotIn(b'GEN-private-fixture', result.stdout + result.stderr)

    def test_public_directory_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = pathlib.Path(directory); parent.chmod(0o755)
            output = parent / 'identity.db'
            self.assertNotEqual(self.run_tool(output).returncode, 0)
            self.assertFalse(output.exists())

    def test_initializer_never_repairs_zero_byte_file(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'identity.db'
            output.touch(mode=0o600)
            self.assertNotEqual(self.run_tool(output).returncode, 0)
            self.assertEqual(output.stat().st_size, 0)


if __name__ == '__main__':
    unittest.main()
