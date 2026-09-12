@echo off
echo Stopping Deskoo companion (releases COM port)...
wmic process where "name='pythonw.exe' and commandline like '%%desktop_companion.py%%'" delete >nul 2>&1
wmic process where "name='python.exe' and commandline like '%%desktop_companion.py%%'" delete >nul 2>&1
echo Done! You can now flash via Arduino IDE.
echo.
echo Press any key to close this window...
pause >nul
