#!/usr/bin/env python3
"""Filter a private bridge config backup without extracting its contents."""

import argparse
import copy
import io
import os
from pathlib import Path, PurePosixPath
import posixpath
import tarfile
import tempfile
import zlib


REQUIRED = frozenset({
    "etc/config/network", "etc/config/system", "etc/config/firewall",
    "etc/shadow", "etc/wemo_ctrl.conf", "etc/fw_env.config",
    "etc/wemo-matter-bridge/matter-onboarding.json",
    "etc/uci-defaults/10_disable_services",
})
HOST_KEYS = frozenset("etc/dropbear/dropbear_" + kind + "_host_key"
                      for kind in ("rsa", "ecdsa", "ed25519"))
ADDITIONS = frozenset({"etc/wemo_ctrl.conf", "etc/fw_env.config",
                       "etc/dropbear/authorized_keys"})
HOLD_PATH = "etc/uci-defaults/09-netcam-hold-services"
HOLD_SCRIPT = b"""#!/bin/sh
/etc/init.d/wemo_ctrl disable || exit 1
/etc/init.d/wemo-matter-bridge disable || exit 1
/etc/init.d/wemo-matter-bridge-watchdog disable || exit 1
exit 0
"""


class UnsafeBackup(ValueError):
    """An archive cannot safely be used for this deployment."""


def normalize(name):
    if not name or name.startswith("/") or "\\" in name or "\x00" in name:
        raise UnsafeBackup("Unsafe archive path")
    parts = PurePosixPath(name).parts
    if ".." in parts:
        raise UnsafeBackup("Unsafe archive path")
    normalized = posixpath.normpath(name)
    if normalized in (".", ".."):
        raise UnsafeBackup("Unsafe archive path")
    return normalized


def validate_member(member):
    name = normalize(member.name)
    if member.type not in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE,
                           tarfile.SYMTYPE, tarfile.LNKTYPE):
        raise UnsafeBackup("Unsupported archive node type")
    if member.issym() or member.islnk():
        target = member.linkname
        if not target or target.startswith("/") or "\\" in target or "\x00" in target:
            raise UnsafeBackup("Unsafe archive link")
        target = (posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                  if member.issym() else normalize(target))
        if target != "etc" and not target.startswith("etc/"):
            raise UnsafeBackup("Archive link escapes configuration")
    return name


def load_archive(path, additions_only=False):
    entries = {}
    discarded = 0
    with tarfile.open(path, "r:gz") as source:
        seen = set()
        for member in source:
            # Root directory entries are harmless, but never restored.
            if member.name in (".", "./") and member.isdir():
                continue
            name = validate_member(member)
            if name in seen:
                raise UnsafeBackup("Duplicate normalized archive path")
            seen.add(name)
            if name == "data" or name.startswith("data/"):
                discarded += 1
                continue
            if name != "etc" and not name.startswith("etc/"):
                raise UnsafeBackup("Unexpected non-configuration archive path")
            if additions_only and name not in ADDITIONS:
                continue
            record = copy.copy(member)
            record.name = name
            record.pax_headers = {}
            # Numeric ownership is authoritative; tar extractors may prefer names.
            record.uname = record.gname = ""
            if record.islnk():
                record.linkname = normalize(record.linkname)
            stream = source.extractfile(member) if member.isfile() else None
            content = stream.read() if stream is not None else None
            if content is not None and len(content) != member.size:
                raise UnsafeBackup("Truncated archive member")
            entries[name] = (record, content)
    return entries, discarded


def validate_entries(entries):
    for name, (member, _) in entries.items():
        if member.isdir() and (member.uid != 0 or member.gid != 0 or member.mode & 0o022):
            raise UnsafeBackup("Configuration directories must be protected and root-owned")
        for parent in PurePosixPath(name).parents:
            ancestor = entries.get(str(parent))
            if ancestor and not ancestor[0].isdir():
                raise UnsafeBackup("Archive member has non-directory ancestor")
        if member.islnk():
            target = entries.get(normalize(member.linkname))
            if target is None or not target[0].isfile():
                raise UnsafeBackup("Hard link requires a regular archive target")
        if member.issym():
            # Resolve components before '..': lexical normpath alone misses link chains.
            parts = list(PurePosixPath(name).parent.parts) + member.linkname.split("/")
            stack = []
            for part in parts:
                if part in ("", "."):
                    continue
                if part == "..":
                    if len(stack) <= 1:
                        raise UnsafeBackup("Archive symlink escapes configuration")
                    stack.pop()
                else:
                    stack.append(part)
                traversed = entries.get("/".join(stack))
                if traversed and (traversed[0].issym() or traversed[0].islnk()):
                    raise UnsafeBackup("Archive symlink traverses another link")
    for name in REQUIRED:
        member = entries.get(name)
        if member is None or not member[0].isfile() or not member[1]:
            raise UnsafeBackup("Missing required regular configuration file")
        if member[0].uid != 0 or member[0].gid != 0:
            raise UnsafeBackup("Required configuration must retain root ownership")
    keys = [entries[name] for name in HOST_KEYS if name in entries]
    if not keys or not all(m.isfile() and m.uid == 0 and m.gid == 0 and data
                           and not (m.mode & 0o077) for m, data in keys):
        raise UnsafeBackup("Missing protected root-owned SSH host key")
    auth = entries.get("etc/dropbear/authorized_keys")
    if auth is not None:
        member, content = auth
        if (not member.isfile() or not content or member.uid != 0 or member.gid != 0
                or member.mode & 0o022):
            raise UnsafeBackup("SSH authorized keys must be protected and root-owned")


def curate(primary, output, additional=None):
    output = Path(output)
    if os.path.lexists(output):
        raise UnsafeBackup("Output already exists; refusing overwrite")
    output.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if output.parent.is_symlink() or output.parent.stat().st_mode & 0o077:
        raise UnsafeBackup("Output directory must be private (0700)")
    entries, discarded = load_archive(primary)
    if additional is not None:
        extra, _ = load_archive(additional, additions_only=True)
        for name, record in extra.items():
            entries.setdefault(name, record)
    if HOLD_PATH in entries:
        raise UnsafeBackup("Input already contains the deployment hold script")
    validate_entries(entries)
    hold = tarfile.TarInfo(HOLD_PATH)
    hold.mode = 0o755
    hold.uid = hold.gid = 0
    hold.uname = hold.gname = "root"
    hold.size = len(HOLD_SCRIPT)
    entries[HOLD_PATH] = (hold, HOLD_SCRIPT)
    fd, temp = tempfile.mkstemp(prefix=".bridge-config-", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            os.fchmod(handle.fileno(), 0o600)
            with tarfile.open(fileobj=handle, mode="w:gz", format=tarfile.USTAR_FORMAT) as dest:
                # A hard link may only reference a regular file already restored.
                for member, content in sorted(entries.values(), key=lambda entry: entry[0].islnk()):
                    dest.addfile(member, io.BytesIO(content) if content is not None else None)
            handle.flush()
            os.fsync(handle.fileno())
        # Atomic publication with exclusive destination creation, including races.
        os.link(temp, output)
        directory = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        os.unlink(temp)
    return {"retained_members": len(entries), "excluded_data_members": discarded,
            "hold_scripts": 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--additional-etc", type=Path)
    args = parser.parse_args()
    try:
        result = curate(args.input, args.output, args.additional_etc)
    except (OSError, ValueError, EOFError, zlib.error, tarfile.TarError):
        parser.exit(1, "Configuration backup rejected; no credential contents printed.\n")
    print(" ".join(f"{key}={value}" for key, value in result.items()))


if __name__ == "__main__":
    main()
