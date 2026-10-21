$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
& ./build.ps1 -Tests
$python = (Get-Command python).Source
$server = Start-Process -FilePath $python -ArgumentList @('tests/mock_server.py', '--port', '18766') -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput build/mock-test.log -RedirectStandardError build/mock-test-error.log
try {
    Start-Sleep -Milliseconds 800
    & ./build/CPAMPTests.exe http://127.0.0.1:18766
    if ($LASTEXITCODE -ne 0) { throw 'HTTP integration tests failed' }
    & ./build/PluginProbe.exe (Join-Path $root 'dist/CPAMPMonitor.dll')
    if ($LASTEXITCODE -ne 0) { throw 'DLL ABI test failed' }
    $config = Join-Path $env:APPDATA 'CPAMPTrafficMonitor/settings.json'
    $existed = Test-Path -LiteralPath $config
    $original = if ($existed) { [IO.File]::ReadAllBytes($config) } else { $null }
    try {
        & ./build/CPAMPTests.exe --write-fixture-config http://127.0.0.1:18766
        if ($LASTEXITCODE -ne 0) { throw 'Cannot create fixture configuration' }
        & ./build/PluginProbe.exe (Join-Path $root 'dist/CPAMPMonitor.dll') --expect-live
        if ($LASTEXITCODE -ne 0) { throw 'End-to-end plugin polling test failed' }
        & ./build/CPAMPTests.exe --write-lifecycle-config http://127.0.0.1:18766
        if ($LASTEXITCODE -ne 0) { throw 'Cannot create lifecycle configuration' }
        & ./build/PluginProbe.exe (Join-Path $root 'dist/CPAMPMonitor.dll') --expect-lifecycle
        if ($LASTEXITCODE -ne 0) { throw 'Plugin refresh lifecycle test failed' }
    } finally {
        if ($existed) { [IO.File]::WriteAllBytes($config, $original) }
        elseif (Test-Path -LiteralPath $config) { Remove-Item -LiteralPath $config }
    }
} finally {
    if (!$server.HasExited) { Stop-Process -Id $server.Id }
}
