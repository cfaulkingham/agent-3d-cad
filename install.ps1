param(
  [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+(-preview\.[1-9]\d*)?$')][string]$Version,
  [switch]$Core,
  [string]$Prefix = (Join-Path $env:LOCALAPPDATA 'Programs\Agent CAD')
)
$ErrorActionPreference = 'Stop'
$architecture = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($architecture -ne 'AMD64') { throw 'Windows x64 is the supported Windows architecture.' }
$package = if ($Core) { 'agent-3d-cad' } else { 'agent-3d-cad-desktop' }
$name = "$package-$Version-Windows-x64"
$archive = "$name.zip"
$base = "https://github.com/cfaulkingham/agent-3d-cad/releases/download/v$Version"
$stage = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
$lock = $null
$stagingInstall = $null
try {
  New-Item -ItemType Directory -Path $stage | Out-Null
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  Invoke-WebRequest -UseBasicParsing "$base/SHA256SUMS" -OutFile (Join-Path $stage 'SHA256SUMS')
  Invoke-WebRequest -UseBasicParsing "$base/$archive" -OutFile (Join-Path $stage $archive)
  $matchesForArchive = @(Get-Content (Join-Path $stage 'SHA256SUMS') | Where-Object { $_ -match ('^[a-fA-F0-9]{64}  ' + [regex]::Escape($archive) + '$') })
  if ($matchesForArchive.Count -ne 1) { throw 'Missing or duplicate release checksum.' }
  $expected = $matchesForArchive[0].Substring(0, 64)
  if ((Get-FileHash (Join-Path $stage $archive) -Algorithm SHA256).Hash -ne $expected) { throw 'Archive checksum mismatch; nothing installed.' }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $stage $archive))
  try {
    foreach ($entry in $zip.Entries) {
      $relative = $entry.FullName.Replace('\', '/')
      if (-not $relative.StartsWith("$name/") -or $relative -match '(^|/)\.\.(/|$)|:') { throw 'Unsafe archive path.' }
    }
  } finally { $zip.Dispose() }
  Expand-Archive -LiteralPath (Join-Path $stage $archive) -DestinationPath $stage
  if (-not (Test-Path -LiteralPath (Join-Path $stage "$name/bin/agent-3d-cad.exe"))) { throw 'Archive has no native executable.' }
  New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
  $Prefix = (Resolve-Path -LiteralPath $Prefix).Path
  $lock = [IO.File]::Open((Join-Path $Prefix '.install-lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
  $destination = Join-Path $Prefix $name
  if (Test-Path -LiteralPath $destination) { throw "Already installed: $destination" }
  $stagingInstall = Join-Path $Prefix ('.staging-' + [Guid]::NewGuid().ToString())
  Copy-Item -LiteralPath (Join-Path $stage $name) -Destination $stagingInstall -Recurse
  Move-Item -LiteralPath $stagingInstall -Destination $destination
  $stagingInstall = $null
  $exe = Join-Path $destination 'bin/agent-3d-cad.exe'
  Write-Output "Installed: $exe"
  Write-Output 'Keep your CAD workspace outside this application directory.'
  Write-Output "Generate settings: & '$($exe.Replace("'", "''"))' config --client codex --workspace `"`$HOME\Documents\Agent CAD`""
  if (-not $Core) { Write-Output "Open viewer: & '$($exe.Replace("'", "''"))' viewer --workspace `"`$HOME\Documents\Agent CAD`"" }
} finally {
  if ($lock) { $lock.Dispose() }
  if ($stagingInstall -and (Test-Path -LiteralPath $stagingInstall)) { Remove-Item -LiteralPath $stagingInstall -Recurse -Force }
  if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
