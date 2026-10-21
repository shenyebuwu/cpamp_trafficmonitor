param([Parameter(Mandatory = $true)][string]$TrafficMonitorDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $root 'dist/CPAMPMonitor.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the plugin first: ./build.ps1 -Tests' }
$exe = Join-Path $TrafficMonitorDir 'TrafficMonitor.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "TrafficMonitor.exe not found: $TrafficMonitorDir" }
$plugins = Join-Path $TrafficMonitorDir 'plugins'
New-Item -ItemType Directory -Force -Path $plugins | Out-Null
$target = Join-Path $plugins 'CPAMPMonitor.dll'
if (Test-Path -LiteralPath $target) {
    Copy-Item -LiteralPath $target -Destination ($target + '.bak') -Force
}
Copy-Item -LiteralPath $dll -Destination $target -Force
Write-Host "Installed: $target"
Write-Host 'Restart TrafficMonitor, then enable CPAMP 5H Remaining and CPAMP Reset Time in its taskbar display items.'
Write-Host 'Open Other Functions > Plugin Manager > CPAMP Quota Monitor > Options to configure the server and key.'
