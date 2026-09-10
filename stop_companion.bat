@echo off
echo Stopping Deskoo companion (releases COM port)...
taskkill /F /IM pythonw.exe /T 2>nul
taskkill /F /IM python.exe /FI "WINDOWTITLE eq Deskoo*" /T 2>nul
echo Done! You can now flash via Arduino IDE.
echo.
echo Press any key to close this window...
pause >nul
