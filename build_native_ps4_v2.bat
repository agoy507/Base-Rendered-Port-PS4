@echo off
setlocal

set "ROOT=E:\PS4\hmbtn"
set "TOOLCHAIN=E:\PS4\OpenOrbis\OpenOrbis\PS4Toolchain"
set "SOURCE=%ROOT%\native\piglet_upstream"
set "SOURCE_PACKAGE=%SOURCE%\UP9000-HMBT00002_00-HMBTNNATIVEV2000.pkg"
set "RELEASE=%ROOT%\releases\HarvestMoonBTN-Native-V2-Pad-Module-Fix.pkg"
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
set "OO_PS4_TOOLCHAIN=%TOOLCHAIN%"
set "DOTNET_ROLL_FORWARD=Major"

echo [1/3] Running upstream baseline contract test...
node "%ROOT%\tests\native_ps4_upstream_baseline.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\native_ps4_v2_telemetry.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\native_ps4_v2_project_asset.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\native_ps4_v2_playable_farm.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\native_ps4_v2_crash_diag.test.js"
if errorlevel 1 goto :error

echo [2/3] Building upstream Piglet baseline...
if exist "%SOURCE_PACKAGE%" del /Q "%SOURCE_PACKAGE%"
if exist "%SOURCE_PACKAGE%" goto :error
pushd "%SOURCE%\piglet"
call build.bat .\x64\Debug piglet "%SOURCE%"
if errorlevel 1 (popd & goto :error)
popd
if not exist "%SOURCE_PACKAGE%" goto :error

echo [3/3] Publishing HMBT00002 baseline package...
copy /Y "%SOURCE_PACKAGE%" "%RELEASE%" >nul
if errorlevel 1 goto :error

echo [SUCCESS] %RELEASE%
exit /b 0

:error
if "%CD%"=="%SOURCE%\piglet" popd
echo [ERROR] Native V2 baseline build failed with code %errorlevel%.
exit /b 1
