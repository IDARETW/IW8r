# Third-party code

IW8 Radiant is released under the GNU General Public License v3 ([LICENSE](LICENSE)). It contains, and its
installer ships, the code below; each part keeps its own licence file in its folder, and the folder's `UPSTREAM.txt`
says where it came from (project, commit) and what was left out.

## Tools built from this repository

| Part | Folder | From | Licence |
|---|---|---|---|
| GSC compiler (`iw8r-gsc.exe`) | `src/tools/gsc` | [gsc-tool](https://github.com/xensik/gsc-tool) 1.6.2 by xensik: the IW8 compiler only | GPL-3 (`src/tools/gsc/LICENSE`) |
| Zone writer and map converter (`iw8r-zt.exe`) | `src/tools/zonetool` | [x64-zt](https://github.com/Joelrau/x64-zt) by Joelrau and contributors, with the IW8 Radiant changes from the [IDARETW/x64-zt](https://github.com/IDARETW/x64-zt/tree/iw8radiant/build-extraction) fork (IW8 and the IW3 map converter only) | GPL-3 (`src/tools/zonetool/LICENSE`) |
| Replay zone reader (`iw8r-acts.exe`) | `src/tools/acts` | [atian-cod-tools (ACTS)](https://github.com/ate47/atian-cod-tools) by Antoine Willerval (ate47), with the IW8 Radiant changes from the [IDARETW/atian-cod-tools](https://github.com/IDARETW/atian-cod-tools/tree/codex/mw2019-complete) fork (the MW2019 Replay reader only) | GPL-3 or MIT, used here under MIT (`src/tools/acts/LICENSE.md`, `licenses/mit.md`) |

## Libraries

| Library | Folder | Used by | Licence |
|---|---|---|---|
| [zlib](https://github.com/madler/zlib) (Jean-loup Gailly, Mark Adler) | `third_party/zlib` | all three tools | zlib |
| [minizip](https://github.com/madler/zlib/tree/master/contrib/minizip) (Gilles Vollant, Mathias Svensson) | `third_party/minizip` | iw8r-zt | zlib-style (in the headers), Info-ZIP for `crypt.h` |
| [Zstandard](https://github.com/facebook/zstd) (Meta Platforms) | `third_party/zstd` | iw8r-zt, iw8r-acts | BSD-3-Clause (dual with GPL-2) |
| [LZ4](https://github.com/lz4/lz4) (Yann Collet) | `third_party/lz4` | iw8r-zt, iw8r-acts | BSD-2-Clause |
| [JSON for Modern C++](https://github.com/nlohmann/json) (Niels Lohmann) | `third_party/nlohmann_json` | iw8r-zt, iw8r-acts | MIT |
| [GSL](https://github.com/microsoft/GSL) (Microsoft) | `third_party/gsl` | iw8r-zt | MIT |
| [Glaze](https://github.com/stephenberry/glaze) (Stephen Berry) | `third_party/glaze` | iw8r-zt | MIT |
| [LibTomCrypt](https://github.com/libtom/libtomcrypt), [LibTomMath](https://github.com/libtom/libtommath) | `third_party/libtomcrypt`, `third_party/libtommath` | iw8r-zt, iw8r-acts | The LibTom licence (public domain) |
| [DirectXTex](https://github.com/microsoft/DirectXTex) (Microsoft) | `third_party/directxtex` | iw8r-zt | MIT |
| [hash-library](https://create.stephan-brumme.com/hash-library/) (Stephan Brumme): SHA-256 | `third_party/hash_library` | iw8r-acts | zlib |
| [stb_image](https://github.com/nothings/stb) (Sean Barrett) | `third_party/stb` | editor | MIT or public domain |
| [bcdec](https://github.com/iOrange/bcdec) (Sergii Kudlai), git submodule | `third_party/bcdec` | editor | MIT or public domain |
| [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System), git submodule | `third_party/qtads` | editor (`qtadvanceddocking.dll`) | LGPL-2.1 |
| [Qt](https://www.qt.io) 6.8 | not in the repository | editor (`Qt6*.dll`, deployed by windeployqt) | LGPL-3, dynamically linked |

## What is not included

No game files: the editor reads the game the user owns (Call of Duty: Modern Warfare 2019, Replay 1.20) from the
folder set in *Tools > Tool Paths*, and loads its Oodle DLL (`oo2core_7_win64.dll`) from there. The Replay reader maps
the user's game executable to run the game's own zone loader; nothing from the game is copied into the repository or
the installer.
