$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "build_native_tests\native_core_test.dll"
$outputDirectory = Split-Path -Parent $output
$clang = "C:\Program Files\LLVM\bin\clang.exe"

if (-not (Test-Path -LiteralPath $clang)) {
    throw "Clang not found at $clang"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& $clang `
    --target=x86_64-pc-windows-msvc `
    -std=c11 `
    -Wall `
    -Wextra `
    -Werror `
    -fno-builtin `
    -fno-stack-protector `
    -fuse-ld=lld `
    -nostdlib `
    -shared `
    -I (Join-Path $root "native\include") `
    (Join-Path $root "tests\native_core_exports.c") `
    (Join-Path $root "native\src\core.c") `
    (Join-Path $root "native\src\game.c") `
    (Join-Path $root "native\src\input.c") `
    (Join-Path $root "native\src\pixel.c") `
    "-Wl,/entry:DllMain" `
    "-Wl,/subsystem:windows" `
    "-Wl,/nodefaultlib" `
    -o $output

if ($LASTEXITCODE -ne 0) {
    throw "Native core compilation failed with exit code $LASTEXITCODE"
}

Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;
public static class NativeCoreTest {
    [DllImport(@"$output")] public static extern int hmbtn_test_performance_contract();
    [DllImport(@"$output")] public static extern int hmbtn_test_widescreen_viewport();
    [DllImport(@"$output")] public static extern int hmbtn_test_fixed_step();
    [DllImport(@"$output")] public static extern int hmbtn_test_gameplay_core();
    [DllImport(@"$output")] public static extern int hmbtn_test_cheat_chord();
    [DllImport(@"$output")] public static extern int hmbtn_test_pixels();
}
"@

$checks = @(
    [NativeCoreTest]::hmbtn_test_performance_contract(),
    [NativeCoreTest]::hmbtn_test_widescreen_viewport(),
    [NativeCoreTest]::hmbtn_test_fixed_step(),
    [NativeCoreTest]::hmbtn_test_gameplay_core(),
    [NativeCoreTest]::hmbtn_test_cheat_chord(),
    [NativeCoreTest]::hmbtn_test_pixels()
)

if ($checks -contains 0) {
    throw "Native core tests failed"
}

Write-Host "native_core_test: PASS"
