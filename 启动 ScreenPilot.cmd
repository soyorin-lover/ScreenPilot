@echo off
set "SCREENPILOT_EXE=%~dp0build\desktop\portable-playback\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" set "SCREENPILOT_EXE=%~dp0build\desktop\portable-canvas\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" set "SCREENPILOT_EXE=%~dp0build\desktop\portable-navigation\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" set "SCREENPILOT_EXE=%~dp0build\desktop\portable-current\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" set "SCREENPILOT_EXE=%~dp0build\desktop\portable-editor\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" set "SCREENPILOT_EXE=%~dp0build\desktop\portable\ScreenPilot.exe"
if not exist "%SCREENPILOT_EXE%" (
    echo Build the app first: powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Deploy
    pause
    exit /b 1
)
start "" "%SCREENPILOT_EXE%"
