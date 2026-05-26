@echo off
setlocal

call "%~dp0idf-env.bat"

cd /d "%~dp0\build"

echo Flashing to COM3...
"D:\Tools\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe" "-m" "esptool" "--chip" "esp32s3" "-p" "COM3" "-b" "460800" "--before" "default-reset" "--after" "hard-reset" "write-flash" "0x0" "bootloader\bootloader.bin" "0x8000" "partition_table\partition-table.bin" "0x10000" "smart_buddy.bin"

echo Exit code: %ERRORLEVEL%
