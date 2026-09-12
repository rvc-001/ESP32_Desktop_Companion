@echo off
REM Deskoo Desktop Companion Startup Script
REM This script launches the companion in the background.
REM You can place a shortcut to this file in your Windows Startup folder:
REM shell:startup

cd /d "%~dp0"
start "" pythonw desktop_companion.py
