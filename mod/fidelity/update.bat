@echo off
rem Double-click after every pull: builds client, server, shader DLL and VRAD,
rem copies them where the game and the map compiler find them, runs deploy.
rem Without VRAD: update.bat -NoVrad   Old VRAD back: update.bat -RestoreVrad
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1" %*
pause
