@echo off
setlocal

rem === Load idf-env.bat ===
call "%~dp0idf-env.bat"

if not defined IDF_PATH (
    echo ERROR: IDF_PATH not set
    exit /b 1
)
if not defined IDF_PYTHON_ENV_PATH (
    echo ERROR: IDF_PYTHON_ENV_PATH not set
    exit /b 1
)

rem === Clean MSYS env ===
set MSYSTEM=
set MINGW_PREFIX=
set MINGW_CHOST=
set MSYSTEM_CARCH=
set MSYSTEM_CHOST=
set MSYSTEM_PREFIX=
set MINGW_PACKAGE_PREFIX=
set MSYS2_PATH_TYPE=

rem === Set ESP_IDF_VERSION (required by idf_component_manager) ===
set "ESP_IDF_VERSION=6.0.1"

rem === Set up PATH (all ESP-IDF tools + compiler toolchain) ===
set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_TOOLS_PATH%\ninja\1.12.1;%IDF_TOOLS_PATH%\idf-exe\1.0.3;%IDF_TOOLS_PATH%\ccache\4.12.1;%IDF_TOOLS_PATH%\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;%IDF_TOOLS_PATH%\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin;%PATH%"

cd /d "%~dp0"

echo ================================
echo ESP-IDF Build
echo ================================
echo IDF_PATH=%IDF_PATH%
echo PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe
echo ================================

rem === Run idf.py build ===
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build 2>&1
echo Exit code: %ERRORLEVEL%
