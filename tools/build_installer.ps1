<#
  IW8 Radiant - installer build (roadmap 9.9).
  1. Runs tools\package_release.ps1 -NoZip (builds out\package\IW8Radiant from bin\release).
  2. Finds the Inno Setup 6 compiler (ISCC.exe) and compiles tools\installer\iw8radiant.iss into out\installer\.
  If ISCC.exe is not found, step 2 is skipped and the install instructions are printed (exit code 2).
  Usage: powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1 [-Zip] [-Iscc <path to ISCC.exe>]
    -Zip   also build the zip (package_release.ps1 without -NoZip)
    -Iscc  use this ISCC.exe instead of searching
#>
param([switch]$Zip, [string]$Iscc)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pkgScript = Join-Path $root 'tools\package_release.ps1'
$issFile = Join-Path $root 'tools\installer\iw8radiant.iss'
$pkgDir = Join-Path $root 'out\package\IW8Radiant'
$outDir = Join-Path $root 'out\installer'

# 1. Package the runtime folder.
$pkgArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $pkgScript)
if (-not $Zip) { $pkgArgs += '-NoZip' }
& powershell @pkgArgs
if ($LASTEXITCODE -ne 0) { throw "package_release.ps1 failed (exit $LASTEXITCODE)" }
if (-not (Test-Path (Join-Path $pkgDir 'IW8Radiant.exe'))) { throw "package folder missing IW8Radiant.exe: $pkgDir" }

# 2. Find ISCC.exe.
if (-not $Iscc) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $Iscc = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $Iscc) {
        $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($cmd) { $Iscc = $cmd.Source }
    }
}
if (-not $Iscc -or -not (Test-Path $Iscc)) {
    Write-Host ''
    Write-Host 'Inno Setup 6 (ISCC.exe) was not found, so the installer was not built.'
    Write-Host "The package folder is ready: $pkgDir"
    Write-Host 'To build the installer:'
    Write-Host '  1. Install Inno Setup 6 from https://jrsoftware.org/isinfo.php (the default options are fine).'
    Write-Host '  2. Re-run: powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1'
    Write-Host '     (or pass -Iscc "<path to ISCC.exe>" if it is installed elsewhere).'
    exit 2
}

# 3. Compile.
New-Item -ItemType Directory -Force $outDir | Out-Null
Write-Host "compiling with $Iscc"
& $Iscc "/DSourceDir=$pkgDir" "/O$outDir" $issFile
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

$setup = Get-ChildItem $outDir -Filter 'IW8Radiant-Setup-*.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($setup) {
    Write-Host ("installer: {0} ({1:N1} MB)" -f $setup.FullName, ($setup.Length / 1MB))
}
