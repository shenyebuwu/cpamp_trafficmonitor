param([switch]$Rebuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ($Rebuild) { & (Join-Path $root 'build.ps1') -Tests }
$dll = Join-Path $root 'dist/CPAMPMonitor.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'Build first: ./build.ps1 -Tests' }
# A fresh staging folder and explicit allowlist prevent accidental credential packaging.
$stage = Join-Path $root ('build/release-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $stage 'plugins'),(Join-Path $stage 'licenses') -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'plugins/CPAMPMonitor.dll')
foreach ($name in 'README.md','THIRD_PARTY_NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'include/TrafficMonitor-LICENSE') -Destination (Join-Path $stage 'licenses/TrafficMonitor-LICENSE')
Copy-Item -LiteralPath (Join-Path $root 'third_party/nlohmann/LICENSE.MIT') -Destination (Join-Path $stage 'licenses/nlohmann-json-LICENSE.MIT')
$zip = Join-Path $root 'dist/CPAMPMonitor-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$checksum = Join-Path $root 'dist/SHA256SUMS.txt'
Set-Content -LiteralPath $checksum -Value "$hash  CPAMPMonitor-x64.zip" -Encoding ascii
Write-Host "Release archive: $zip"
Write-Host "Checksums: $checksum"
