<#
.SYNOPSIS
  Rebuilds IW8Radiant.exe while a copy is open, then opens the new build.

.DESCRIPTION
  Windows locks a running exe/DLL but allows renaming it, so any locked editor binaries in bin\release are
  renamed to *.prev-<time> first (the open editor keeps running from them), the editor is rebuilt, and the
  new build is launched with -Open (default: the IW8 sample room). Previous editor windows are closed
  gracefully first (an unsaved map keeps its save prompt; nothing is killed).
  Leftover *.prev-* files are deleted once nothing holds them.
#>
param([string]$Open = 'samples\iw8\iw8_sample_room.map', [switch]$NoLaunch)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'bin\release'
Set-Location $root

# Remove previous iterations that are no longer in use.
Get-ChildItem $bin -Filter '*.prev-*' -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch { }
}

# Move aside binaries held by a running editor.
$stamp = Get-Date -Format 'HHmmss'
foreach ($name in 'IW8Radiant.exe', 'IW8Radiant.pdb', 'qtadvanceddocking.dll', 'qtadvanceddocking.pdb') {
    $f = Join-Path $bin $name
    if (-not (Test-Path $f)) { continue }
    try {
        $stream = [IO.File]::Open($f, 'Open', 'ReadWrite', 'None'); $stream.Close()
    } catch {
        Rename-Item $f ("$name.prev-$stamp") -Force
        Write-Host "moved aside (in use): $name"
    }
}

xmake build -y IW8Radiant
if ($LASTEXITCODE -ne 0) { throw 'editor build failed' }
if (-not $NoLaunch) {
    # Close the previous editor windows (owner request 2026-11-09). A graceful close: an editor with unsaved changes
    # shows its save prompt and stays open until the owner answers; it is never killed. Test runs (no window) are left.
    $old = Get-Process IW8Radiant -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 }
    foreach ($o in $old) { [void]$o.CloseMainWindow() }
    foreach ($o in $old) {
        if (-not $o.WaitForExit(5000)) { Write-Host "still open (unsaved changes prompt?): pid $($o.Id)" }
        else { Write-Host "closed previous editor: pid $($o.Id)" }
    }
    $p = Start-Process -FilePath (Join-Path $bin 'IW8Radiant.exe') -ArgumentList $Open -WorkingDirectory $root -PassThru
    Write-Host "opened new iteration: pid $($p.Id)"
}
