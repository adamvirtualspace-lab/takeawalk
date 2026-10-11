@echo off
rem Installs Take a Walk into Euro Truck Simulator 2. See README.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
