@echo off
rem GnuBox Windows sharing in a console window (the GUI is "GnuBox Share.bat").
rem   start-winshare.bat          direct Bluetooth, no COM port needed
rem   start-winshare.bat COM5     an incoming Bluetooth COM port instead
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
  echo Python not found. Install it from https://www.python.org and run this again.
  pause
  exit /b 1
)
if not "%1"=="" python -c "import serial" 2>nul || python -m pip install pyserial
python gnubox_winshare.py %*
pause
