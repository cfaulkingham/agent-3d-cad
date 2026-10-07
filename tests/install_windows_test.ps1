$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('cad-install-test-' + [Guid]::NewGuid())
$name = 'agent-3d-cad-desktop-0.1.0-preview.1-Windows-x64'
$downloads = Join-Path $root 'downloads'
$prefix = Join-Path $root 'installed versions'
try {
  New-Item -ItemType Directory -Path (Join-Path $root "$name/bin"), $downloads | Out-Null
  Set-Content -LiteralPath (Join-Path $root "$name/bin/agent-3d-cad.exe") -Value 'fixture binary'
  $archive = Join-Path $downloads "$name.zip"
  Compress-Archive -LiteralPath (Join-Path $root $name) -DestinationPath $archive
  $hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  Set-Content -LiteralPath (Join-Path $downloads 'SHA256SUMS') -Value "$hash  $name.zip"
  function Invoke-WebRequest {
    param([switch]$UseBasicParsing, [Parameter(Position=0)][string]$Uri, [string]$OutFile)
    if (-not $Uri.StartsWith('https://github.com/cfaulkingham/agent-3d-cad/releases/download/v0.1.0-preview.1/')) { throw 'Unexpected URL' }
    Copy-Item -LiteralPath (Join-Path $downloads ($Uri.Split('/')[-1])) -Destination $OutFile
  }
  $installer = Join-Path $PSScriptRoot '../install.ps1'
  & $installer -Version '0.1.0-preview.1' -Prefix $prefix
  $installed = Join-Path $prefix "$name/bin/agent-3d-cad.exe"
  if (-not (Test-Path -LiteralPath $installed)) { throw 'Nothing installed' }
  $rejected = $false
  try { & $installer -Version '0.1.0-preview.1' -Prefix $prefix } catch { $rejected = $true }
  if (-not $rejected) { throw 'Installer overwrote an existing version' }
  Set-Content -LiteralPath (Join-Path $downloads 'SHA256SUMS') -Value (('0' * 64) + "  $name.zip")
  $rejected = $false
  try { & $installer -Version '0.1.0-preview.1' -Prefix (Join-Path $root 'corrupt') } catch { $rejected = $true }
  if (-not $rejected -or (Test-Path (Join-Path $root 'corrupt'))) { throw 'Checksum failure published an install' }
  if ((Get-Content -LiteralPath $installed) -ne 'fixture binary') { throw 'Existing installation changed' }
  Write-Output 'Windows installer: verified install, spaces, no overwrite, checksum rejection passed.'
} finally { if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force } }
