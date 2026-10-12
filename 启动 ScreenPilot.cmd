@echo off
set "SCREENPILOT_EXE=%~dp0build\desktop\portable\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" (
    echo Build the app first: powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Deploy
    pause
    exit /b 1
)
start "" "%SCREENPILOT_EXE%"
