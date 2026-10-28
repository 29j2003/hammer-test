@echo off
rem Double-click: copies the fidelity shaders and cfg into steamapps\sourcemods\mymod.
rem To remove them again: deploy.bat -Remove
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" %*
pause
