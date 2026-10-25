@echo off
rem Gathering Map setup: adds the gathering icons to your game's map icons (needs Python 3).
rem Run it once after installing, and again after updating a UI mod (e.g. HD UI).
cd /d "%~dp0"
set ROOT=%~dp0..\..
py -3 --version >nul 2>nul && (py -3 -I setup\make_data.py "%ROOT%" & goto done)
python --version >nul 2>nul && (python -I setup\make_data.py "%ROOT%" & goto done)
echo.
echo Python 3 is needed for the setup: https://www.python.org/downloads/
echo Install it (tick "Add python.exe to PATH"), then run setup.bat again.
:done
echo.
pause
