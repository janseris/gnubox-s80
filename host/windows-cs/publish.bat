@echo off
rem Builds GnuBoxShare.exe (one small file, needs the .NET 10 Desktop Runtime) into .\publish
cd /d "%~dp0"
dotnet publish GnuBoxShare\GnuBoxShare.csproj -c Release -r win-x64 -p:SelfContained=false -p:PublishSingleFile=true -p:DebugType=none -o publish
if errorlevel 1 pause & exit /b 1
echo.
echo Done: %~dp0publish\GnuBoxShare.exe
pause
