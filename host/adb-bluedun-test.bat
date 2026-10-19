@echo off
rem GnuBox - check why BlueDUN++ stops when the Android screen is off
rem Writes adb-report.txt next to this file. Send that file back.
cd /d "%~dp0"
set OUT=adb-report.txt
set A=com.bluedunz
set B=com.stephentaylor.gpsnmeatether

echo 1. Start sharing in BlueDUN++ and the GPS app, and press Connect in GnuBox on the 9300.
echo    Keep the Android screen ON for now.
pause

echo ===== devices > %OUT%
adb devices >> %OUT%

for %%P in (%A% %B%) do (
  echo.>> %OUT%
  echo ==================== %%P ==================== >> %OUT%
  echo --- version / targetSdk >> %OUT%
  adb shell "dumpsys package %%P | grep -E 'versionName|targetSdk'" >> %OUT%
  echo --- standby bucket ^(5 exempt, 10 active, 20 working, 30 frequent, 40 rare, 45 restricted^) >> %OUT%
  adb shell am get-standby-bucket %%P >> %OUT%
  echo --- app ops >> %OUT%
  adb shell cmd appops get %%P RUN_ANY_IN_BACKGROUND >> %OUT%
  adb shell cmd appops get %%P RUN_IN_BACKGROUND >> %OUT%
  echo --- on battery-optimization allowlist? >> %OUT%
  adb shell "dumpsys deviceidle whitelist | grep %%P || echo NOT on allowlist" >> %OUT%
  echo --- running services ^(look for isForeground=true^) >> %OUT%
  adb shell dumpsys activity services %%P >> %OUT%
)

adb logcat -c
echo.
echo 2. Now turn the Android screen OFF and wait 3 minutes
echo    ^(until GnuBox on the 9300 shows the connection dropped^).
echo    Then turn the screen on and press a key here.
pause

echo.>> %OUT%
echo ==================== AFTER SCREEN OFF ==================== >> %OUT%
echo --- BlueDUN++ services now >> %OUT%
adb shell dumpsys activity services %A% >> %OUT%
echo --- log lines about the two apps / stopping services >> %OUT%
adb logcat -d -v time > adb-logcat-full.txt
findstr /i "bluedun gpsnmea idle Killing Stopping" adb-logcat-full.txt >> %OUT%

echo.
echo Done. Send adb-report.txt  (adb-logcat-full.txt is the full log, keep it private).
pause
