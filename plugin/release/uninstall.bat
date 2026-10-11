@echo off
rem Removes Take a Walk from Euro Truck Simulator 2. See README.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
pause
