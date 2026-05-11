@echo off
rem Flash firmware to ESP32-S3. Usage: do_flash.bat [COM_PORT]
rem Default: COM8 (change as needed)
set "PORT=%~1"
if not defined PORT set "PORT=COM8"
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '.\build_esp32.ps1' -ActivateOnly; idf.py -p %PORT% flash"
echo EXIT_CODE: %ERRORLEVEL%
