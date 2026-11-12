# IW8 Radiant Suite installer

Windows installer for the editor package (roadmap 9.9). It is built from the package folder that
`tools\package_release.ps1` produces, using Inno Setup 6.

## Install

1. Run `IW8RadiantSuite-Setup-<version>.exe` from `out\installer\`.
2. Choose the install mode on the first page:
   - **Just me** (default): installs to `%LOCALAPPDATA%\Programs\IW8 Radiant Suite`, no administrator rights needed.
   - **All users**: installs to `C:\Program Files\IW8 Radiant Suite`. Setup warns about this, because the settings
     file then sits in a folder the editor cannot write without administrator rights. Prefer "Just me".
3. Optionally tick the desktop shortcut. The Start menu entry is always created.
4. The editor needs the **Microsoft Visual C++ 2015-2022 x64 Redistributable**. Setup checks for it and shows a
   note if it is missing. The installer does not bundle or download it: get it from Microsoft and install it, then
   start the editor.

Game data is not included. The editor asks for the paths to the game files on first use, and those paths are
stored in the settings file (see below).

## Settings and upgrades

The editor keeps its settings in `IW8Radiant.ini`, next to `IW8Radiant.exe`. The settings hold the tool paths,
the web server port and login hash, and the recent files. A default HTTPS certificate, when you create one, is in
the `web_tls` folder beside the editor.

- **Upgrade into the same folder** (the default; Setup remembers the previous folder): the installer replaces the
  program files only. `IW8Radiant.ini` and `web_tls\` are not part of the package and are never overwritten, so the
  settings are kept. Setup closes a running editor first (it asks you to close it).
- **Reinstall after an uninstall into the same folder**: the old `IW8Radiant.ini` is still there and is used.
- Installing to a different folder starts with fresh settings. Copy `IW8Radiant.ini` into the new folder to keep
  them.

## Uninstall

Uninstall from Windows Settings > Apps, or run `unins000.exe` in the install folder. The uninstaller removes the
program files, Start menu and desktop shortcuts. It then asks, with **No** as the default:

- **No** (keep): `IW8Radiant.ini`, `web_tls\` and the cache folders stay. A reinstall picks up your settings and
  caches.
- **Yes**: deletes `IW8Radiant.ini`, `web_tls\` and `%LOCALAPPDATA%\IW8 Radiant Suite` (see below). This cannot be undone.

Silent uninstalls (`/SILENT`, `/VERYSILENT`) keep the settings and caches.

### Cache and data folders

All under `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\`:

| Folder | Contents |
|---|---|
| `material_cache\` | converted material cache |
| `model_cache\` | converted model cache |
| `world_cache\` | world geometry cache |
| `stock_cache\` | stock map cache |
| `stock_rebuild\` | work folders from stock rebuilds |
| `thumbs\` | asset thumbnails |
| `image_browser\` | image browser cache |
| `custom_assets\` | custom asset folder for maps that are not saved yet |
| `x64zt_output\`, `builds\` | compile output (default locations) |
| `autosave\` | autosave snapshots: recover unsaved work from here |
| `crashes\` | crash reports |

Rebuildable caches (the first five and `thumbs\`) are safe to delete at any time. Keep `autosave\`,
`crashes\` and `custom_assets\` until you have checked them. Compile output may be needed to run a map that was
built with it.

The settings file is `IW8Radiant.ini` in the install folder, and the certificate folder is `web_tls\` next to
it. Certificates you pointed to elsewhere in the settings are not touched by the uninstaller.

## Building the installer

Requirements: the editor built in `bin\release` (see the main build instructions), and Inno Setup 6 installed
from https://jrsoftware.org/isinfo.php. The build does not install Inno Setup.

```
powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1
```

This runs `tools\package_release.ps1 -NoZip` to refresh `out\package\IW8RadiantSuite`, then compiles
`tools\installer\iw8radiant.iss` with ISCC into `out\installer\IW8RadiantSuite-Setup-<version>.exe`.

- `-Zip` also builds the zip (the package step without `-NoZip`).
- `-Iscc "<path>\ISCC.exe"` uses an Inno Setup installed in another location.
- Without Inno Setup the script stops after packaging and prints how to install it (exit code 2).

The version is set by `#define MyAppVersion` at the top of `iw8radiant.iss` (currently `0.1.0`, the version the
editor shows in its About box). Change it there when the release version changes. The `AppId` GUID must stay
the same in all versions, otherwise upgrades install beside the old version instead of replacing it.
