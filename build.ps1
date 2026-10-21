param([switch]$Tests)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$setup = Join-Path $PSScriptRoot 'tools/msvc/setup_x64.bat'
if (!(Test-Path -LiteralPath $setup) -and !(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC not found. Run this script in an x64 Visual Studio developer shell, or use CMake with Visual Studio 2022.'
}
New-Item -ItemType Directory -Force build, dist | Out-Null
$commands = @(
    '@echo off',
    'if exist tools\msvc\setup_x64.bat call tools\msvc\setup_x64.bat',
    'rc /nologo /fo build\plugin.res src\plugin.rc',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++17 /EHsc /utf-8 /W4 /wd4100 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Iinclude /Ithird_party /Isrc /LD src\plugin.cpp src\quota.cpp src\platform.cpp build\plugin.res /Fobuild\ /Fedist\CPAMPMonitor.dll /link /INCREMENTAL:NO /IMPLIB:build\CPAMPMonitor.lib winhttp.lib crypt32.lib shell32.lib ole32.lib user32.lib gdi32.lib',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++17 /EHsc /utf-8 /W4 /wd4100 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Iinclude /Ithird_party /Isrc tests\tests.cpp src\quota.cpp src\platform.cpp /Fobuild\ /Febuild\CPAMPTests.exe /link /INCREMENTAL:NO winhttp.lib crypt32.lib shell32.lib ole32.lib user32.lib',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++17 /EHsc /utf-8 /W4 /wd4100 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Iinclude tests\plugin_probe.cpp /Fobuild\ /Febuild\PluginProbe.exe /link /INCREMENTAL:NO user32.lib gdi32.lib',
    'exit /b %errorlevel%'
)
$commands | Set-Content build/compile.cmd -Encoding ascii
& cmd.exe /d /c build\compile.cmd
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
if ($Tests) {
    & ./build/CPAMPTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
Write-Host "Built: $PSScriptRoot\dist\CPAMPMonitor.dll"
