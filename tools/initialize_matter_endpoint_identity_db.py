#!/usr/bin/env python3
"""Explicit offline first-provisioning only; never run automatically at boot.

Creates an empty strict SQLite DB at a fresh private ignored path. Copy it to
/data/wemo-matter/endpoint_identity.db before starting a new, uncommissioned
installation. An existing commissioned bridge needs exact identity migration
seeds instead. Never initialize a missing/damaged operational database to hide
data loss: recover its preserved DB/seed first.
"""
import argparse
import os
import sqlite3
import sys

from read_matter_endpoint_baseline import output_path

SCHEMA = (
    'CREATE TABLE endpoint_identity ('
    'udn TEXT NOT NULL PRIMARY KEY CHECK(length(CAST(udn AS BLOB)) BETWEEN 6 AND 256),'
    'unique_id TEXT NOT NULL UNIQUE CHECK(length(CAST(unique_id AS BLOB)) BETWEEN 1 AND 32)'
    ')'
)


def initialize(path):
    path = output_path(path)
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
    os.close(fd)
    db = None
    try:
        db = sqlite3.connect(path)
        db.execute('PRAGMA synchronous=EXTRA')
        db.execute(SCHEMA)
        db.commit()
        if db.execute('PRAGMA integrity_check').fetchone() != ('ok',):
            raise ValueError('Initialized schema did not pass integrity check')
    except Exception:
        path.unlink()  # Our exclusively created offline provisioning file only.
        raise
    finally:
        if db is not None:
            db.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    os.umask(0o077)
    try:
        initialize(args.output)
    except (ValueError, OSError, sqlite3.Error) as error:
        print('Offline initialization failed: ' + type(error).__name__, file=sys.stderr)
        return 1
    print('Empty identity database initialized; device_count=0')
    return 0


if __name__ == '__main__':
    sys.exit(main())
