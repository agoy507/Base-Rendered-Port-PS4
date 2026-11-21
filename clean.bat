@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo   PS4 Native Render Port Base - Build Cache Cleaner
echo ======================================================================
echo.

set BUILD_DIR=%~dp0build_ps4_port
set SRC_DIR=%~dp0native

echo [*] Membersihkan intermediate object files (*.o)...
if exist "%BUILD_DIR%\*.o" del /F /Q "%BUILD_DIR%\*.o"
if exist "%SRC_DIR%\*.o" del /F /Q "%SRC_DIR%\*.o"

echo [*] Membersihkan ELF binaries sementara (*.elf, *.oelf)...
if exist "%BUILD_DIR%\*.elf" del /F /Q "%BUILD_DIR%\*.elf"
if exist "%BUILD_DIR%\*.oelf" del /F /Q "%BUILD_DIR%\*.oelf"

echo [*] Membersihkan temporary files (*.tmp)...
if exist "%BUILD_DIR%\*.tmp" del /F /Q "%BUILD_DIR%\*.tmp"
if exist "%~dp0*.tmp" del /F /Q "%~dp0*.tmp"

echo [*] Membersihkan backup files (*.bak)...
if exist "%~dp0*.bak" del /F /Q "%~dp0*.bak"
if exist "%~dp0releases\*.bak" del /F /Q "%~dp0releases\*.bak"
if exist "%BUILD_DIR%\*.bak" del /F /Q "%BUILD_DIR%\*.bak"

echo.
echo ======================================================================
echo [OK] Cache build berhasil dibersihkan!
echo      File konfigurasi, skrip JS, SFO, GP4, dan PKG tetap aman.
echo      Jalankan update_rebuild.bat kapan saja untuk rebuild dari awal.
echo ======================================================================
echo.

endlocal
exit /b 0
