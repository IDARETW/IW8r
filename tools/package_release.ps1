<#
  IW8 Radiant - release package (roadmap 9.6).
  Copies the runtime files of bin\release (editor, CLI, Qt runtime, ADS) plus licences and docs into
  out\package\IW8Radiant, then zips it to out\package\IW8Radiant-<date>.zip.
  Left out: tests, PDBs, import libs, renamed running copies (*.running-*, *.prev-*), user layout/ini files, and any
  game data (no GPL or game assets are shipped).
  Usage: powershell -ExecutionPolicy Bypass -File tools\package_release.ps1 [-NoZip]
#>
param([switch]$NoZip)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'bin\release'
$outRoot = Join-Path $root 'out\package'
$dest = Join-Path $outRoot 'IW8Radiant'
if (-not (Test-Path (Join-Path $bin 'IW8Radiant.exe'))) { throw "build first: $bin\IW8Radiant.exe not found" }
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null

$files = @('IW8Radiant.exe', 'iw8r.exe', 'iw8r-gsc.exe', 'iw8r-zt.exe', 'iw8r-acts.exe', 'qtadvanceddocking.dll') +
         (Get-ChildItem $bin -Filter 'Qt6*.dll' | ForEach-Object Name)
foreach ($f in $files) { Copy-Item (Join-Path $bin $f) $dest }
# Qt plugin folders written by windeployqt.
foreach ($d in 'platforms', 'styles', 'imageformats', 'iconengines', 'generic', 'tls', 'networkinformation') {
    $src = Join-Path $bin $d
    if (Test-Path $src) {
        Copy-Item -Recurse $src (Join-Path $dest $d)
        Get-ChildItem (Join-Path $dest $d) -Recurse -Include '*.pdb' | Remove-Item -Force
    }
}

# Editor data the editor finds beside itself: the browser client (Tools > Web Server), the stock asset catalogs and
# availability lists, the canonical key table and the entity definitions.
$dataDest = Join-Path $dest 'data'
foreach ($d in 'web', 'asset_catalog', 'generated', 'prefabs', 'ies', 'stock_assets', 'fx', 'shaders', 'volume_masks', 'mw19') {
    $src = Join-Path $root "data\$d"
    if (Test-Path $src) { Copy-Item -Recurse $src (Join-Path $dataDest $d) }
}
foreach ($f in 'iw8_entity_defs.json', 'iw8_entity_classes.json', 'hotkey_presets.json') {
    $src = Join-Path $root "data\$f"
    if (Test-Path $src) { Copy-Item $src $dataDest }
}

# Licences: the suite (GPL-3), the tools built from src/tools (gsc-tool and x64-zt: GPL-3; ACTS: MIT), the libraries
# (THIRD_PARTY.md), Qt (LGPLv3, dynamic linking) and the Qt Advanced Docking System (LGPLv2.1).
$lic = Join-Path $dest 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $lic 'IW8Radiant-GPL-3.txt')
Copy-Item (Join-Path $root 'THIRD_PARTY.md') (Join-Path $lic 'THIRD_PARTY.md')
Copy-Item (Join-Path $root 'third_party\qtads\LICENSE') (Join-Path $lic 'QtADS-LGPL-2.1.txt')
$licenseFiles = [ordered]@{
    'gsc-tool-GPL-3.txt'      = 'src\tools\gsc\LICENSE'
    'x64-zt-GPL-3.txt'        = 'src\tools\zonetool\LICENSE'
    'ACTS-LICENSE.md'         = 'src\tools\acts\LICENSE.md'
    'ACTS-MIT.md'             = 'src\tools\acts\licenses\mit.md'
    'zlib.txt'                = 'third_party\zlib\LICENSE'
    'minizip-Info-ZIP.txt'    = 'third_party\minizip\LICENSE.Info-Zip'
    'zstd-BSD.txt'            = 'third_party\zstd\LICENSE'
    'lz4-BSD-2.txt'           = 'third_party\lz4\lib\LICENSE'
    'nlohmann-json-MIT.txt'   = 'third_party\nlohmann_json\LICENSE.MIT'
    'GSL-MIT.txt'             = 'third_party\gsl\LICENSE'
    'glaze-MIT.txt'           = 'third_party\glaze\LICENSE'
    'libtomcrypt.txt'         = 'third_party\libtomcrypt\LICENSE'
    'libtommath.txt'          = 'third_party\libtommath\LICENSE'
    'DirectXTex-MIT.txt'      = 'third_party\directxtex\LICENSE'
    'hash-library-zlib.txt'   = 'third_party\hash_library\LICENSE'
    'bcdec.txt'               = 'third_party\bcdec\LICENSE'
}
foreach ($k in $licenseFiles.Keys) { Copy-Item (Join-Path $root $licenseFiles[$k]) (Join-Path $lic $k) }
@"
IW8 Radiant: GNU General Public License v3 (IW8Radiant-GPL-3.txt). Source: https://github.com/IDARETW/IW8r
The tools beside the editor are built from that source: iw8r-gsc.exe (gsc-tool by xensik, GPL-3), iw8r-zt.exe (x64-zt,
GPL-3) and iw8r-acts.exe (atian-cod-tools by Antoine Willerval, MIT); the libraries are listed in THIRD_PARTY.md.
- Qt 6.8 (https://www.qt.io): LGPL v3, used as unmodified shared libraries (Qt6*.dll and the plugin folders).
  Source: https://download.qt.io/official_releases/qt/6.8/ . You may replace these DLLs with your own build.
- Qt Advanced Docking System (qtadvanceddocking.dll): LGPL v2.1, see QtADS-LGPL-2.1.txt.
  Source: https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System
Requires the Microsoft Visual C++ 2015-2022 x64 redistributable (vc_redist.x64.exe from Microsoft).
No game data or game executables are included: the editor reads the game you own from the folder set in Tool Paths.
"@ | Set-Content -Encoding utf8 (Join-Path $lic 'THIRD_PARTY.txt')

# Files the editor needs at run time: stop when one did not reach the package (the shaders folder was once left out).
foreach ($need in 'data\shaders\toon.iw8shader.hlsl', 'data\shaders\stock.iw8shader.hlsl', 'data\volume_masks\clouds_soft.png', 'data\web\index.html', 'data\web\app.js',
                  'data\web\viewlive.js', 'data\iw8_entity_defs.json', 'data\iw8_entity_classes.json', 'data\hotkey_presets.json',
                  'data\generated\iw8_canonical_keys.tsv') {
    if (-not (Test-Path (Join-Path $dest $need))) { throw "package is missing $need" }
}

# Docs for the mapper.
$docs = Join-Path $dest 'docs'
New-Item -ItemType Directory -Force $docs | Out-Null
foreach ($d in 'README.md', 'docs\USER_GUIDE.md', 'docs\TUTORIAL.md', 'docs\KEYS_REFERENCE.md') {
    $p = Join-Path $root $d
    if (Test-Path $p) { Copy-Item $p $docs }
}
Copy-Item (Join-Path $root 'tests\data\iw8_sample_room.map') (Join-Path $dest 'sample_room.map')
# Tutorial map and sample prefabs (docs/TUTORIAL.md).
$tut = Join-Path $dest 'tutorial'
New-Item -ItemType Directory -Force $tut | Out-Null
Copy-Item (Join-Path $root 'samples\tutorial\iw8r_tutorial.map') $tut
Copy-Item -Recurse (Join-Path $root 'samples\tutorial\prefabs') (Join-Path $tut 'prefabs')

$size = (Get-ChildItem $dest -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("package folder: {0} ({1:N1} MB)" -f $dest, ($size / 1MB))
if (-not $NoZip) {
    $zip = Join-Path $outRoot ("IW8Radiant-{0}.zip" -f (Get-Date -Format 'yyyyMMdd'))
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $dest '*') -DestinationPath $zip
    Write-Host ("zip: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}
