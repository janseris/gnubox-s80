@echo off
rem Opens the GnuBox Share window (no console). Same as double-clicking gnubox_share.pyw.
cd /d "%~dp0"
where pythonw >nul 2>nul
if errorlevel 1 (
  echo Python not found. Install it from https://www.python.org ^(tick "Add python.exe to PATH"^) and run this again.
  pause
  exit /b 1
)
start "" pythonw gnubox_share.pyw
