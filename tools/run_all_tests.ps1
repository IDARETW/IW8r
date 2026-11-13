<#
  IW8 Radiant Suite - local CI (roadmap 9.8): build everything, run the core tests, the offscreen GUI test and the
  D3D smoke test, and print one summary line per suite. Exit code 0 only when every suite passes.
  -Full also runs the GUI test with IW8R_FULL_COMPILE=1 (F7 + x64-zt + ACTS + scratch deploy, about a minute; needs
  the tool paths configured in the editor). Nothing is written to the game folder.
  -Watch keeps running: whenever a file under src\, tests\ or data\ changes (checked every -Interval seconds), the
  suites run again ("CI on every change" without a server). Each run writes out\ci\summary.txt.
  Usage: powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1 [-Full] [-Watch [-Interval 60]]
#>
param([switch]$Full, [switch]$Watch, [int]$Interval = 60)
if ($Watch) {
    $self = $MyInvocation.MyCommand.Path
    $last = $null
    while ($true) {
        $stamp = (Get-ChildItem -Recurse -File (Join-Path (Split-Path -Parent $PSScriptRoot) 'src'), (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests'),
                  (Join-Path (Split-Path -Parent $PSScriptRoot) 'data') -ErrorAction SilentlyContinue |
                  Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
        if ($stamp -ne $last) {
            $last = $stamp
            Write-Host ("[{0}] change seen, running the suites" -f (Get-Date -Format 'HH:mm:ss'))
            if ($Full) { & powershell -ExecutionPolicy Bypass -File $self -Full } else { & powershell -ExecutionPolicy Bypass -File $self }
        }
        Start-Sleep -Seconds $Interval
    }
}
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$bin = Join-Path $root 'bin\release'
$results = @()
function Add-Result($name, $ok, $detail) { $script:results += [pscustomobject]@{ Suite = $name; Ok = $ok; Detail = $detail } }

& xmake build -y iw8r_tests 2>&1 | Out-Null
$okCore = $LASTEXITCODE -eq 0
& xmake build -y iw8r 2>&1 | Out-Null
$okCore = $okCore -and ($LASTEXITCODE -eq 0)  # xmake builds one target per call
& powershell -ExecutionPolicy Bypass -File (Join-Path $root 'tools\dev_rebuild_editor.ps1') -NoLaunch 2>&1 | Out-Null
$okEditor = $LASTEXITCODE -eq 0
Add-Result 'build' ($okCore -and $okEditor) ("core={0} editor={1}" -f $okCore, $okEditor)

$core = & (Join-Path $bin 'iw8r_tests.exe') 2>&1
$line = ($core | Select-String -Pattern 'checks,' | Select-Object -Last 1).Line
Add-Result 'core' ($LASTEXITCODE -eq 0 -and $line -match ' 0 failures') $line

function Run-Gui($name, $outDir) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $p = Start-Process -FilePath (Join-Path $bin 'IW8Radiant.exe') -ArgumentList @('--gui-test', 'tests\data\iw8_sample_room.map', '--out', $outDir) -Wait -PassThru -NoNewWindow
    $rep = Join-Path $outDir 'gui-test-report.txt'
    $last = if (Test-Path $rep) { (Get-Content $rep | Select-Object -Last 1) } else { 'no report' }
    Add-Result $name ($p.ExitCode -eq 0) $last
}
Run-Gui 'gui' (Join-Path $root 'out\ci\gui')
if ($Full) {
    $env:IW8R_FULL_COMPILE = '1'
    Run-Gui 'gui-full' (Join-Path $root 'out\ci\gui-full')
    Remove-Item Env:\IW8R_FULL_COMPILE
}

$smokeDir = Join-Path $root 'out\ci\smoke'
New-Item -ItemType Directory -Force $smokeDir | Out-Null
$p = Start-Process -FilePath (Join-Path $bin 'IW8Radiant.exe') -ArgumentList @('--smoke-test', 'tests\data\iw8_sample_room.map', '--out', $smokeDir) -Wait -PassThru -NoNewWindow
$s = Join-Path $smokeDir 'smoke-test.txt'
Add-Result 'smoke' ($p.ExitCode -eq 0) $(if (Test-Path $s) { (Get-Content $s -First 1) } else { 'no report' })

$table = $results | Format-Table -AutoSize | Out-String
Write-Host $table
New-Item -ItemType Directory -Force (Join-Path $root 'out\ci') | Out-Null
$verdict = if ($results | Where-Object { -not $_.Ok }) { 'FAILED' } else { 'passed' }
Set-Content -Path (Join-Path $root 'out\ci\summary.txt') -Value ("CI {0} at {1}`r`n{2}" -f $verdict, (Get-Date -Format 's'), $table)
if ($results | Where-Object { -not $_.Ok }) { Write-Host 'CI: FAILED'; exit 1 }
Write-Host 'CI: all suites passed'
exit 0
