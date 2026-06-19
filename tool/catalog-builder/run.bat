@echo off
REM EQ Marketplace -- Catalog Builder launcher.
REM Uses the standard EQEmu DB defaults (127.0.0.1:3306, user eqemu, db peq).
REM If yours differ, edit the line below or pass flags, e.g.:
REM     run.bat --user myuser --password mypass --database peq
cd /d "%~dp0"
python -c "import pymysql" 2>nul || python -m pip install pymysql
python catalog_builder.py %*
pause
