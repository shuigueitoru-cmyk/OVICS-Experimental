@echo off
cd /d "%~dp0"
py -3 host\wikipedia_relay.py
if errorlevel 1 python host\wikipedia_relay.py
pause
