#!/usr/bin/env bash
# EQ Marketplace -- Catalog Builder launcher.
# Uses the standard EQEmu DB defaults (127.0.0.1:3306, user eqemu, db peq).
# If yours differ, pass flags, e.g.:  ./run.sh --user myuser --password mypass --database peq
cd "$(dirname "$0")"
python3 -c "import pymysql" 2>/dev/null || python3 -m pip install pymysql
exec python3 catalog_builder.py "$@"
