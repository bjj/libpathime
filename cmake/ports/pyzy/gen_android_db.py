#!/usr/bin/env python3
"""Build pyzy's android.db without needing the sqlite3 CLI.

Upstream does `create_db.py rawdict | sqlite3 android.db`. add_custom_command
cannot portably shell-pipe, so this helper runs create_db.py (which prints SQL
to stdout) and feeds the SQL into a fresh database via Python's stdlib sqlite3,
then applies the index script. Cross-platform: no external sqlite3 binary.

Usage: gen_android_db.py <android_src_dir> <rawdict> <out_db> <create_index.sql>
"""
import os
import sqlite3
import subprocess
import sys


def main():
    android_dir, rawdict, out_db, index_sql = sys.argv[1:5]

    # create_db.py does `from pydict import *` etc., so run it from its own dir.
    proc = subprocess.run(
        [sys.executable, os.path.join(android_dir, "create_db.py"), rawdict],
        cwd=android_dir, capture_output=True, text=True, check=True,
    )

    if os.path.exists(out_db):
        os.remove(out_db)

    con = sqlite3.connect(out_db)
    try:
        con.executescript(proc.stdout)
        with open(index_sql, encoding="utf-8") as f:
            con.executescript(f.read())
        con.commit()
    finally:
        con.close()


if __name__ == "__main__":
    main()
