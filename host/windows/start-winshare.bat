@echo off
rem GnuBox Windows sharing: the PC's internet for the Nokia 9300 over Bluetooth.
rem Usage: start-winshare.bat COM5   (or run it and type the port)
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
  echo Python not found. Install it from https://www.python.org and run this again.
  pause
  exit /b 1
)
python -c "import serial" 2>nul || python -m pip install pyserial
set PORT=%1
if "%PORT%"=="" (
  python gnubox_winshare.py --list
  set /p PORT=Incoming Bluetooth COM port, e.g. COM5: 
)
python gnubox_winshare.py %PORT%
pause
