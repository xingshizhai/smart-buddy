@echo off
setlocal

call "%~dp0idf-env.bat"

if not defined IDF_PATH (
    echo ERROR: IDF_PATH not set
    exit /b 1
)

set MSYSTEM=
set MINGW_PREFIX=
set MINGW_CHOST=
set MSYSTEM_CARCH=
set MSYSTEM_CHOST=
set MSYSTEM_PREFIX=
set MINGW_PACKAGE_PREFIX=
set MSYS2_PATH_TYPE=

set "ESP_IDF_VERSION=6.0.1"
rem Add esptool and openocd to PATH
set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_TOOLS_PATH%\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\bin;%PATH%"

cd /d "%~dp0"

echo ================================
echo Flashing to COM3...
echo ================================

"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p COM3 flash 2>&1
echo Exit code: %ERRORLEVEL%
