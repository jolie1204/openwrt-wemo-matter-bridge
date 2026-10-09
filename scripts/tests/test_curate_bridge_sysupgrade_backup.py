"""Host-only tests with synthetic configuration values."""

import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import subprocess
import sys
import unittest
from unittest.mock import patch


MODULE = Path(__file__).resolve().parents[1] / "curate_bridge_sysupgrade_backup.py"
SPEC = importlib.util.spec_from_file_location("curator", MODULE)
curator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(curator)


def regular(name, data=b"synthetic fixture\n", mode=0o600, uid=0, gid=0):
    member = tarfile.TarInfo(name)
    member.mode, member.uid, member.gid = mode, uid, gid
    member.size = len(data)
    member.mtime = 123456789
    return member, data


def fixture():
    entries = [regular(name) for name in sorted(curator.REQUIRED)]
    entries.append(regular("etc/dropbear/dropbear_ed25519_host_key"))
    entries.append(regular("data/wemo-matter/chip/chip_kvs", b"private fixture"))
    return entries


def write_archive(path, entries):
    with tarfile.open(path, "w:gz") as archive:
        for member, data in entries:
            archive.addfile(member, io.BytesIO(data) if data is not None else None)


class CurateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.source = self.base / "input.tgz"
        self.output = self.base / "result.tgz"
        write_archive(self.source, fixture())

    def tearDown(self):
        self.temp.cleanup()

    def reject(self, entries):
        write_archive(self.source, entries)
        with self.assertRaises(curator.UnsafeBackup):
            curator.curate(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_preserves_metadata_and_disables_services_without_data(self):
        entries = fixture() + [regular("./etc/example", mode=0o640, uid=42, gid=43)]
        write_archive(self.source, entries)
        result = curator.curate(self.source, self.output)
        self.assertEqual(result["excluded_data_members"], 1)
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)
        with tarfile.open(self.output) as archive:
            self.assertFalse(any(m.name.startswith("data") for m in archive))
            member = archive.getmember("etc/example")
            self.assertEqual((member.uid, member.gid, member.mode, member.mtime),
                             (42, 43, 0o640, 123456789))
            hold = archive.getmember(curator.HOLD_PATH)
            self.assertEqual((hold.uid, hold.gid, hold.mode), (0, 0, 0o755))
            self.assertEqual(archive.extractfile(hold).read(), curator.HOLD_SCRIPT)
            old = "etc/uci-defaults/10_disable_services"
            self.assertEqual(archive.extractfile(old).read(), b"synthetic fixture\n")

    def test_explicit_missing_config_added_without_replacing_original(self):
        entries = [item for item in fixture() if item[0].name != "etc/wemo_ctrl.conf"]
        write_archive(self.source, entries)
        extra = self.base / "extra.tgz"
        write_archive(extra, [regular("etc/wemo_ctrl.conf", b"added fixture"),
                              regular("etc/config/network", b"must not replace"),
                              regular("etc/init.d/wemo_ctrl", b"must not restore")])
        curator.curate(self.source, self.output, extra)
        with tarfile.open(self.output) as archive:
            self.assertEqual(archive.extractfile("etc/wemo_ctrl.conf").read(), b"added fixture")
            self.assertEqual(archive.extractfile("etc/config/network").read(), b"synthetic fixture\n")
            self.assertNotIn("etc/init.d/wemo_ctrl", archive.getnames())

    def test_optional_authorized_keys_are_preserved_when_present(self):
        entries = fixture() + [regular("etc/dropbear/authorized_keys", b"synthetic public key")]
        write_archive(self.source, entries)
        curator.curate(self.source, self.output)
        with tarfile.open(self.output) as archive:
            self.assertEqual(archive.extractfile("etc/dropbear/authorized_keys").read(),
                             b"synthetic public key")

    def test_optional_authorized_keys_must_not_be_writable_or_non_root(self):
        self.reject(fixture() + [regular("etc/dropbear/authorized_keys", mode=0o666)])
        self.reject(fixture() + [regular("etc/dropbear/authorized_keys", uid=1000)])

    def test_rejects_absolute_parent_and_non_configuration_paths(self):
        for name in ["/etc/evil", "etc/../evil", "etc/a/../../evil", "tmp/evil", "etc\\evil"]:
            with self.subTest(name=name):
                self.reject(fixture() + [regular(name)])

    def test_rejects_duplicate_normalized_paths(self):
        self.reject(fixture() + [regular("./etc/config/network")])

    def test_rejects_escaping_symlinks_and_hardlinks(self):
        for kind in [tarfile.SYMTYPE, tarfile.LNKTYPE]:
            data_target = ("../data/wemo-matter/chip/chip_kvs" if kind == tarfile.SYMTYPE
                           else "data/wemo-matter/chip/chip_kvs")
            for target in ["/etc/shadow", "../../tmp/evil", data_target]:
                with self.subTest(kind=kind, target=target):
                    link = tarfile.TarInfo("etc/link")
                    link.type, link.linkname = kind, target
                    self.reject(fixture() + [(link, None)])

    def test_rejects_symlink_ancestor(self):
        link = tarfile.TarInfo("etc/redirect")
        link.type, link.linkname = tarfile.SYMTYPE, "config"
        self.reject(fixture() + [(link, None), regular("etc/redirect/file")])

    def test_rejects_symlink_chain_that_changes_parent_resolution(self):
        first = tarfile.TarInfo("etc/b")
        first.type, first.linkname = tarfile.SYMTYPE, "."
        second = tarfile.TarInfo("etc/a")
        second.type, second.linkname = tarfile.SYMTYPE, "b/../root/.ssh/x"
        self.reject(fixture() + [(first, None), (second, None)])

    def test_preserves_safe_links(self):
        symlink = tarfile.TarInfo("etc/safe-symbolic")
        symlink.type, symlink.linkname = tarfile.SYMTYPE, "config/network"
        hardlink = tarfile.TarInfo("etc/safe-hard")
        hardlink.type, hardlink.linkname = tarfile.LNKTYPE, "etc/config/network"
        write_archive(self.source, fixture() + [(symlink, None), (hardlink, None)])
        curator.curate(self.source, self.output)
        with tarfile.open(self.output) as archive:
            self.assertTrue(archive.getmember("etc/safe-symbolic").issym())
            self.assertTrue(archive.getmember("etc/safe-hard").islnk())

    def test_rejects_special_nodes(self):
        for kind in [tarfile.CHRTYPE, tarfile.BLKTYPE, tarfile.FIFOTYPE, tarfile.CONTTYPE]:
            with self.subTest(kind=kind):
                node = tarfile.TarInfo("etc/device")
                node.type = kind
                self.reject(fixture() + [(node, None)])

    def test_rejects_missing_required_config_and_host_key(self):
        for missing in list(curator.REQUIRED) + ["etc/dropbear/dropbear_ed25519_host_key"]:
            with self.subTest(missing=missing):
                self.reject([entry for entry in fixture() if entry[0].name != missing])

    def test_rejects_unprotected_required_config(self):
        for field, value in [("uid", 1000), ("gid", 1000)]:
            entries = fixture()
            setattr(entries[0][0], field, value)
            self.reject(entries)
        entries = fixture()
        next(m for m, _ in entries if m.name == "etc/dropbear/dropbear_ed25519_host_key").mode = 0o644
        self.reject(entries)

    def test_numeric_ownership_wins_over_conflicting_names(self):
        entries = fixture()
        for member, _ in entries:
            member.uname, member.gname = "nobody", "nogroup"
        write_archive(self.source, entries)
        curator.curate(self.source, self.output)
        with tarfile.open(self.output) as archive:
            member = archive.getmember("etc/shadow")
            self.assertEqual((member.uid, member.gid, member.uname, member.gname), (0, 0, "", ""))

    def test_rejects_unsafe_configuration_directories(self):
        for uid, mode in [(1000, 0o755), (0, 0o777)]:
            directory = tarfile.TarInfo("etc/dropbear")
            directory.type, directory.uid, directory.mode = tarfile.DIRTYPE, uid, mode
            self.reject(fixture() + [(directory, None)])

    def test_every_present_host_key_must_be_protected(self):
        self.reject(fixture() + [regular("etc/dropbear/dropbear_rsa_host_key", mode=0o644)])

    def test_corrupt_gzip_cli_has_fixed_error_without_traceback(self):
        self.source.write_bytes(b"\x1f\x8btruncated gzip")
        result = subprocess.run([sys.executable, str(MODULE), str(self.source), str(self.output)],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("Configuration backup rejected", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(self.output.exists())

    def test_output_cannot_overwrite_existing_file_or_symlink(self):
        self.output.write_bytes(b"existing")
        with self.assertRaises(curator.UnsafeBackup):
            curator.curate(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), b"existing")
        self.output.unlink()
        self.output.symlink_to(self.base / "absent")
        with self.assertRaises(curator.UnsafeBackup):
            curator.curate(self.source, self.output)
        self.assertFalse((self.base / "absent").exists())

    def test_rejects_existing_hold_and_non_private_parent(self):
        self.reject(fixture() + [regular(curator.HOLD_PATH)])
        write_archive(self.source, fixture())
        self.base.chmod(0o755)
        with self.assertRaises(curator.UnsafeBackup):
            curator.curate(self.source, self.output)

    def test_publication_race_preserves_winner_and_cleans_tempfile(self):
        original_link = curator.os.link

        def competing_writer(source, destination):
            Path(destination).write_bytes(b"competing result")
            return original_link(source, destination)

        with patch.object(curator.os, "link", side_effect=competing_writer):
            with self.assertRaises(FileExistsError):
                curator.curate(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), b"competing result")
        self.assertFalse(list(self.base.glob(".bridge-config-*")))


if __name__ == "__main__":
    unittest.main()
