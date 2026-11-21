@echo off
setlocal

set "ROOT=%~dp0"
set "PROJECT_CONFIG=%ROOT%config\project.local.bat"
if exist "%PROJECT_CONFIG%" call "%PROJECT_CONFIG%"
if not defined PORT_TITLE set "PORT_TITLE=PS4 Native Render Port"
if not defined PORT_TITLE_ID set "PORT_TITLE_ID=PORT00001"
if not defined PORT_CONTENT_ID set "PORT_CONTENT_ID=UP0000-PORT00001_00-PS4PORTBASE0001"
if not defined PORT_TOOLCHAIN set "PORT_TOOLCHAIN=E:\PS4\OpenOrbis\OpenOrbis\PS4Toolchain"

set "TOOLCHAIN=%PORT_TOOLCHAIN%"
set "SOURCE=%ROOT%\native\piglet_renderer"
set "SOURCE_PACKAGE=%SOURCE%\%PORT_CONTENT_ID%.pkg"
set "RELEASE=%ROOT%\releases\%PORT_TITLE_ID%.pkg"
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
set "OO_PS4_TOOLCHAIN=%TOOLCHAIN%"
set "DOTNET_ROLL_FORWARD=Major"

echo [1/3] Running upstream baseline contract test...
node "%ROOT%\tests\generic_base_identity.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\renderer_baseline.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\port_telemetry.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\placeholder_asset.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\template_scene.test.js"
if errorlevel 1 goto :error
node "%ROOT%\tests\input_initialization.test.js"
if errorlevel 1 goto :error

echo [2/3] Building upstream Piglet baseline...
if exist "%SOURCE_PACKAGE%" del /Q "%SOURCE_PACKAGE%"
if exist "%SOURCE_PACKAGE%" goto :error
pushd "%SOURCE%\piglet"
call build.bat .\x64\Debug piglet "%SOURCE%"
if errorlevel 1 (popd & goto :error)
popd
if not exist "%SOURCE_PACKAGE%" goto :error

echo [3/3] Publishing PORT00001 baseline package...
if not exist "%ROOT%\releases" mkdir "%ROOT%\releases"
if errorlevel 1 goto :error
copy /Y "%SOURCE_PACKAGE%" "%RELEASE%" >nul
if errorlevel 1 goto :error

echo [SUCCESS] %RELEASE%
exit /b 0

:error
if "%CD%"=="%SOURCE%\piglet" popd
echo [ERROR] PS4 port base build failed with code %errorlevel%.
exit /b 1
