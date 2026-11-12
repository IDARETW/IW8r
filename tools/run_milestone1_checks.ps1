<#
.SYNOPSIS
  Builds IW8 Radiant Suite and runs every milestone-1 check (edit -> save -> reopen).

.DESCRIPTION
  1. xmake release build of iw8r_tests, iw8r and IW8Radiant (Qt shell).
  2. Core tests: synthetic scenes, IW8 sample room, stock IW8 MapEnts (mp_hideout), optional grammar corpus.
  3. CLI: round trip of the IW8 sample room; scripted edit + diff; stock mp_hideout entity string round trip.
  4. GUI: scripted end-to-end UI test on Qt's offscreen platform (no visible window, no OS input).
  5. GUI: Direct3D 11 smoke test (off-monitor window) that counts drawn pixels and saves a screenshot.
  Nothing here starts the game or writes into E:\IW8\Builds\1.20-replay.

.PARAMETER Out
  Folder for reports and edited copies (default: .\out\milestone1-<timestamp>).
#>
param(
    [string]$Out = '',
    [string]$Qt = 'E:\Qt\6.8.3\msvc2022_64',
    [string]$StockMapEnts = 'E:\mw124-conversion-builds\_ents_stock\hideout\mw19replay\mp_hideout\assets\map_ents\maps\mp\mp_hideout.d3dbsp.0.asset.json',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $Out) { $Out = Join-Path $root ("out\milestone1-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$results = [ordered]@{}
function Step($name, [scriptblock]$body) {
    Write-Host "== $name" -ForegroundColor Cyan
    try { & $body; $results[$name] = 'PASS' } catch { $results[$name] = "FAIL: $($_.Exception.Message)"; Write-Host $_ -ForegroundColor Red }
}

if (-not $SkipBuild) {
    Step 'build' {
        xmake f -m release --qt=$Qt -y | Out-Null
        foreach ($t in 'iw8r_tests', 'iw8r', 'IW8Radiant') {
            xmake build -y $t
            if ($LASTEXITCODE -ne 0) { throw "xmake build $t failed" }
        }
    }
}

$bin = Join-Path $root 'bin\release'
# Tests use the pristine generated fixture; samples\iw8 is the mapper's working copy and may be edited.
$sample = Join-Path $root 'tests\data\iw8_sample_room.map'

Step 'core tests' {
    & "$bin\iw8r_tests.exe" --tmp $Out --iw8-ents $StockMapEnts > "$Out\core-tests.txt"
    $code = $LASTEXITCODE
    Get-Content "$Out\core-tests.txt" -Tail 1
    if ($code -ne 0) { throw 'iw8r_tests reported failures' }
}

Step 'cli round trip + scripted edit' {
    & "$bin\iw8r.exe" roundtrip $sample | Tee-Object -FilePath "$Out\cli.txt"
    if ($LASTEXITCODE -ne 0) { throw 'round trip differs' }
    $edited = Join-Path $Out 'iw8_sample_room_cli_edit.map'
    & "$bin\iw8r.exe" edit $sample -o $edited --grid 16 --select-brush 5 --move 30 -9 0 --select-entity mp_tdm_spawn_allies_start --set angles '0 90 0' --undo --undo --redo --redo | Tee-Object -FilePath "$Out\cli.txt" -Append
    & "$bin\iw8r.exe" diff $sample $edited | Tee-Object -FilePath "$Out\cli.txt" -Append
    if ($LASTEXITCODE -ne 3) { throw 'expected exactly the two edits in the diff' }
}

Step 'stock IW8 MapEnts (mp_hideout)' {
    if (-not (Test-Path $StockMapEnts)) { throw "missing $StockMapEnts" }
    & "$bin\iw8r.exe" iw8-ents $StockMapEnts -o (Join-Path $Out 'mp_hideout.ents') --spawns 3 > "$Out\iw8-ents.txt"
    if ($LASTEXITCODE -ne 0) { throw 'iw8-ents failed' }
    Get-Content "$Out\iw8-ents.txt" -TotalCount 4
    if (-not (Select-String -Path "$Out\iw8-ents.txt" -Pattern 'byte-identical' -Quiet)) { throw 'entity string round trip differs' }
}

Step 'gui end-to-end (offscreen)' {
    $p = Start-Process -FilePath "$bin\IW8Radiant.exe" -ArgumentList '--gui-test', $sample, '--out', $Out -PassThru
    if (-not $p.WaitForExit(120000)) { $p.Kill(); throw 'GUI test timed out' }
    Get-Content "$Out\gui-test-report.txt" | Select-Object -Last 1
    if ($p.ExitCode -ne 0) { throw "GUI test exit code $($p.ExitCode)" }
}

Step 'gui d3d11 smoke' {
    $p = Start-Process -FilePath "$bin\IW8Radiant.exe" -ArgumentList '--smoke-test', $sample, '--out', $Out -PassThru
    if (-not $p.WaitForExit(60000)) { $p.Kill(); throw 'smoke test timed out' }
    Get-Content "$Out\smoke-test.txt"
    if ($p.ExitCode -ne 0) { throw "smoke test exit code $($p.ExitCode)" }
}

Write-Host "`nResults ($Out):" -ForegroundColor Cyan
$results.GetEnumerator() | ForEach-Object { '{0,-34} {1}' -f $_.Key, $_.Value }
$results | ConvertTo-Json | Set-Content (Join-Path $Out 'summary.json')
if ($results.Values | Where-Object { $_ -ne 'PASS' }) { exit 1 }
