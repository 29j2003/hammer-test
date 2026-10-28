@echo off
rem Double-click: makes the weapon sounds fuller (copies in steamapps\sourcemods\mymod\sound).
rem Stronger: sound_tool.bat -Preset heavy   Other folders: sound_tool.bat -Folder weapons,physics
rem To remove them again: sound_tool.bat -Remove
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sound_tool.ps1" %*
pause
