@echo off
rem Double-click after typing fidelity_report in the game's console: packs fidelity_report.zip to send.
rem After sending: report.bat -Clear
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0report.ps1" %*
pause
