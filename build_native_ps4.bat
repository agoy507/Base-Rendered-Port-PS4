@echo off
setlocal

set ROOT=E:\PS4\hmbtn
set TOOLCHAIN=E:\PS4\OpenOrbis\OpenOrbis\PS4Toolchain
set LLVM=C:\Program Files\LLVM\bin
set FAKE_PKG=E:\PS4\PS4_Fake_PKG_Tools_3.87_V7
set BUILD=%ROOT%\build_native_ps4
set STAGE=%BUILD%\stage
set RELEASE=%ROOT%\releases\HarvestMoonBTN-Native-GPU-Probe.pkg
set AUTHINFO=000000000000000000000000001C004000FF000000000080000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000

set CLANG="%LLVM%\clang.exe"
set LLD="%LLVM%\ld.lld.exe"
set FSELF="%TOOLCHAIN%\bin\windows\create-fself.exe"
set CREATE_GP4="%TOOLCHAIN%\bin\windows\create-gp4.exe"
set PKGTOOL="%TOOLCHAIN%\bin\windows\PkgTool.Core.exe"
set PUBCMD="%FAKE_PKG%\orbis-pub-cmd.exe"
set CFLAGS=--target=x86_64-pc-freebsd12-elf -std=c11 -fPIC -O3 -DNDEBUG -Wall -Wextra -Werror -Wno-visibility -I"%TOOLCHAIN%\include" -I"%ROOT%\native\include"

if not exist "%BUILD%" mkdir "%BUILD%"

echo [1/6] Running native regression tests...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tests\run_native_tests.ps1"
if errorlevel 1 goto :error
node "%ROOT%\tests\native_ps4_probe.test.js"
if errorlevel 1 goto :error

echo [2/6] Compiling native core and Piglet GPU probe...
%CLANG% %CFLAGS% -c "%ROOT%\native\src\core.c" -o "%BUILD%\core.o"
if errorlevel 1 goto :error
%CLANG% %CFLAGS% -c "%ROOT%\native\platform\ps4\piglet_probe.c" -o "%BUILD%\piglet_probe.o"
if errorlevel 1 goto :error

echo [3/6] Linking native PS4 ELF...
%LLD% -m elf_x86_64 -pie --script "%TOOLCHAIN%\link.x" --eh-frame-hdr -o "%BUILD%\hmbtn_native_probe.elf" -L"%TOOLCHAIN%\lib" "%TOOLCHAIN%\lib\crt1.o" "%BUILD%\core.o" "%BUILD%\piglet_probe.o" --start-group -lc -lkernel -lSceShellCoreUtil -lSceSysmodule -lSceSystemService -lScePigletv2VSH -lScePrecompiledShaders -lScePad -lSceUserService --end-group
if errorlevel 1 goto :error

echo [4/6] Signing eboot.bin with Piglet auth info...
set OO_PS4_TOOLCHAIN=%TOOLCHAIN%
%FSELF% -in "%BUILD%\hmbtn_native_probe.elf" -out "%BUILD%\hmbtn_native_probe.oelf" --eboot "%BUILD%\eboot.bin" --paid 0x3800000000000035 --authinfo %AUTHINFO%
if errorlevel 1 goto :error

echo [5/6] Generating package metadata...
if not exist "%STAGE%\sce_module" mkdir "%STAGE%\sce_module"
if not exist "%STAGE%\sce_sys\about" mkdir "%STAGE%\sce_sys\about"
copy /Y "%BUILD%\eboot.bin" "%STAGE%\eboot.bin" >nul
copy /Y "%ROOT%\pkg_staging\sce_module\libc.prx" "%STAGE%\sce_module\libc.prx" >nul
copy /Y "%ROOT%\pkg_staging\sce_module\libSceFios2.prx" "%STAGE%\sce_module\libSceFios2.prx" >nul
copy /Y "%TOOLCHAIN%\samples\piglet\sce_sys\about\right.sprx" "%STAGE%\sce_sys\about\right.sprx" >nul
copy /Y "%ROOT%\pkg_staging\sce_sys\icon0.png" "%STAGE%\sce_sys\icon0.png" >nul
copy /Y "%ROOT%\pkg_staging\sce_sys\pic0.png" "%STAGE%\sce_sys\pic0.png" >nul
%PUBCMD% sfo_create "%ROOT%\native\packaging\param.sfx" "%STAGE%\sce_sys\param.sfo"
if errorlevel 1 goto :error

echo [6/6] Building GPU probe PKG...
pushd "%STAGE%"
%CREATE_GP4% -out project.gp4 -content-id UP9000-HMBT00001_00-HMBTNNATIVEGPU01 -files "eboot.bin sce_module/libc.prx sce_module/libSceFios2.prx sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic0.png"
if errorlevel 1 (popd & goto :error)
set DOTNET_ROLL_FORWARD=Major
%PKGTOOL% pkg_build project.gp4 .
if errorlevel 1 (popd & goto :error)
copy /Y "UP9000-HMBT00001_00-HMBTNNATIVEGPU01.pkg" "%RELEASE%" >nul
if errorlevel 1 (popd & goto :error)
popd
if errorlevel 1 goto :error

echo [SUCCESS] %RELEASE%
exit /b 0

:error
if "%CD%"=="%STAGE%" popd
echo [ERROR] Native build failed with code %errorlevel%.
exit /b 1
