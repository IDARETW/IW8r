<#
.SYNOPSIS
  Writes an edited IW8 entity string back into a stock Replay 1.20 zone and validates the rebuilt fastfile.

.DESCRIPTION
  Milestone 2.1 pipeline. Everything happens under -Work; the Replay install is only read.
    1. x64-zt `dumpzone <Zone>` (reads the stock zone)
    2. iw8r iw8d-set-ents: replace only the map_ents entity string (+ numEntityChars) in the dump
       (-Spawns: iw8r iw8d-spawns --set also replaces the compiled spawnList, roadmap 6.1)
    3. x64-zt `buildzone <Zone>` into a scratch game folder (Oodle DLL copied in; no links to the install)
    4. x64-zt `verifyzone` on the rebuilt zone (every byte consumed)
    5. ACTS `--test` (Replay's own loaders) and ACTS `-a map_ents` read-back, diffed against the edit
  The result is <Work>\out\<Zone>.ff. Installing it into the game is a separate, manual step.

.EXAMPLE
  pwsh -File tools\m2_rebuild_mapents.ps1 -Zone mp_shipment -Ents out\shipment_edited.ents
#>
param(
    [Parameter(Mandatory)] [string]$Zone,
    [string]$Ents = '',
    [string]$Vision = '',  # KEY "value" lines applied to vision\<Zone>.vision (iw8r vision-set; roadmap 11.6)
    [string]$Spawns = '',
    [string]$Work = '',
    [string]$Game = 'E:\IW8\Builds\1.20-replay',
    [string]$ZoneTool = '',  # empty: bin\release\iw8r-zt.exe (src/tools/zonetool)
    [string]$Acts = ''  # empty: bin\release\iw8r-acts.exe (src/tools/acts)
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$iw8r = Join-Path $root 'bin\release\iw8r.exe'
if (-not $ZoneTool) { $ZoneTool = Join-Path $root 'bin\release\iw8r-zt.exe' }
if (-not $Acts) { $Acts = Join-Path $root 'bin\release\iw8r-acts.exe' }
if (-not $Work) { $Work = Join-Path $root "out\m2-$Zone-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
New-Item -ItemType Directory -Force $Work | Out-Null
$Work = (Resolve-Path $Work).Path  # absolute: the script changes into it
if (-not $Ents -and -not $Vision) { throw 'nothing to do: give -Ents and/or -Vision' }
if ($Ents) { $Ents = (Resolve-Path $Ents).Path }
if ($Vision) { $Vision = (Resolve-Path $Vision).Path }
if ($Spawns) { $Spawns = (Resolve-Path $Spawns).Path }
$stock = Join-Path $Game "zone\$Zone.ff"
$stamp = (Get-Item $stock).LastWriteTimeUtc
$scratch = Join-Path $Work 'scratch_game'
New-Item -ItemType Directory -Force "$scratch\zone", "$Work\build\zone_source", "$Work\build\zonetool", "$Work\out" | Out-Null
Copy-Item (Join-Path $Game 'oo2core_7_win64.dll') $scratch -Force

function Run($what, [scriptblock]$cmd) {
    Write-Host "== $what" -ForegroundColor Cyan
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

Push-Location $Work
try {
    Run 'dumpzone' { & $ZoneTool -game iw8 -path $Game dumpzone $Zone *> dumpzone.log }
    Copy-Item "dump\$Zone\$Zone.csv" "build\zone_source\" -Force
    robocopy "dump\$Zone" "build\zonetool\$Zone" /E /NFL /NDL /NJH /NJS /NP /XF "$Zone.csv" | Out-Null
    if ($Ents) {
        $iw8 = Get-ChildItem "build\zonetool\$Zone\map_ents" -Recurse -Filter *.iw8 | Select-Object -First 1
        if (-not $iw8) { throw "no map_ents dump in $Zone" }
        Run 'write entity string' { & $iw8r iw8d-set-ents $iw8.FullName $Ents -o $iw8.FullName }
    }
    if ($Vision) {
        $vis = "build\zonetool\$Zone\vision\$Zone.vision"
        if (-not (Test-Path $vis)) { throw "no vision rawfile in the $Zone dump" }
        Run 'write vision values' { & $iw8r vision-set $vis $Vision -o $vis }
    }
    if ($Spawns) { Run 'write spawnList' { & $iw8r iw8d-spawns $iw8.FullName --set $Spawns -o $iw8.FullName } }
    Push-Location build
    try { Run 'buildzone' { & $ZoneTool -game iw8 -path $scratch buildzone $Zone *> buildzone.log } } finally { Pop-Location }
    Run 'verifyzone' { & $ZoneTool -game iw8 -path $scratch verifyzone $Zone *> verifyzone.log }
    Move-Item "$scratch\zone\$Zone.ff" "out\$Zone.ff" -Force
    $ErrorActionPreference = 'Continue'
    & $Acts --noUpdater fastfile -r mw19replay -g "$Game\game_dx12_ship_replay.exe" --oodle "$Game\oo2core_7_win64.dll" --test -o acts_test "out\$Zone.ff" *> acts_test.log
    $ErrorActionPreference = 'Stop'
    $m = Get-Content (Get-ChildItem acts_test -Recurse -Filter manifest.json | Select-Object -First 1).FullName -Raw | ConvertFrom-Json
    Write-Host "ACTS --test: complete=$($m.complete) loaded=$($m.loaded_assets) tested=$($m.tested) failed=$($m.failed) unavailable=$($m.unavailable)"
    if (-not $m.complete -or $m.failed -ne 0) { throw 'ACTS rejected the rebuilt zone' }
    if ($m.unavailable -gt 0) { Write-Host "ACTS: $($m.unavailable) stock streamed payload(s) absent from the XPaks (not part of the edit)" }
    $ErrorActionPreference = 'Continue'
    if (-not $Ents) { return }  # vision only: ACTS --test above is the check
    & $Acts --noUpdater fastfile -r mw19replay -g "$Game\game_dx12_ship_replay.exe" --oodle "$Game\oo2core_7_win64.dll" -a map_ents -o acts_ents "out\$Zone.ff" *> acts_ents.log
    $ErrorActionPreference = 'Stop'
    $json = (Get-ChildItem acts_ents -Recurse -Filter *.asset.json | Where-Object { $_.FullName -match 'map_ents' } | Select-Object -First 1).FullName
    & $iw8r iw8-ents $json -o readback.ents --spawns 0 | Out-Null
    & $iw8r diff $Ents readback.ents
    if ($LASTEXITCODE -ne 0) { throw 'the rebuilt zone does not carry the edited entity string' }
    if ($Spawns) {
        # The spawnList as Replay's loader reads it (ACTS), against the edited spawn text: count and every origin.
        $listing = & $iw8r iw8-ents $json --spawns 65535
        $read = @($listing | Where-Object { $_ -match '^\s+\d+ \S+\s+origin ' })
        $want = @(Select-String -Path $Spawns -Pattern '^709 "([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value })
        if ($read.Count -ne $want.Count) { throw "spawnList read back $($read.Count) spawns, expected $($want.Count)" }
        for ($i = 0; $i -lt $want.Count; $i++) {
            $o = ($read[$i] -replace '^.*origin (\S+ \S+ \S+).*$', '$1') -split ' ' | ForEach-Object { [double]$_ }
            $w = $want[$i] -split ' ' | ForEach-Object { [double]$_ }
            for ($k = 0; $k -lt 3; $k++) { if ([math]::Abs($o[$k] - $w[$k]) -gt 0.01) { throw "spawn $i origin $($o -join ' ') != $($w -join ' ')" } }
        }
        Write-Host "spawnList read back: $($read.Count) spawns, every origin as edited"
    }
} finally {
    Pop-Location
}
if ((Get-Item $stock).LastWriteTimeUtc -ne $stamp) { throw 'the stock zone changed (should never happen)' }
Write-Host "`nRebuilt and validated: $Work\out\$Zone.ff" -ForegroundColor Green
Write-Host 'Not verified in game. To test, install it into a scratch copy of the Replay install, keeping a backup of the stock zone.'
