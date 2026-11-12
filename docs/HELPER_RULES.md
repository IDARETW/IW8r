# Rules for helper agents (IW8 Radiant Suite)

You are helping on a C++20 / Qt 6 map editor for MW2019 (Replay 1.20). Project root: `E:\IW8-Radiant`. Read this file
fully before you start. Your brief names the files you own.

## Layout

- `src/core/` : pure C++ (namespace `iw8r`), no Qt. Every `*.cpp` here is built into the core library.
- `src/qtapp/` : the Qt editor (namespace `iw8r::qt`). Every `*.cpp` and `*.h` here is built into the editor.
- `src/viewport/` : rendering helpers (namespace `iw8r::app`).
- `tests/` : core tests. Every `tests/*.cpp` is built into `iw8r_tests`. A test file looks like:

```cpp
#include "my_module.h"
#include "test_harness.h"

using namespace iw8r;

TEST(my_module_does_something) {
    CHECK(1 + 1 == 2);
}
```

- JSON in core: `src/core/json_lite.h` (`json::parse(text, &err)` returns `std::optional<json::Value>`; look at an existing
  user such as `src/core/fx_author.cpp` for how values are read).
- Reading and writing files in core: look for `readFile` / `writeFile` in `src/core/` headers.

## Hard rules

1. Create or change ONLY the files your brief names. Before creating a file, check the name is free (Glob). If it is
   taken, stop and report.
2. Never touch: `src/qtapp/main_window.*`, `src/core/compile_iw8.*`, `src/qtapp/compile_panel.*`, `xmake.lua`,
   `docs/ROADMAP.md`, `docs/PROGRESS.md`, `docs/TEST_CHECKLIST.md`, anything under `samples/`, anything under
   `E:\mw124\`, `E:\IW8\Builds\` (reading those is fine), and nothing under `E:\CoD4`. This project is IW8 only.
3. New Qt classes must NOT use `Q_OBJECT` (connect signals to lambdas instead).
4. Write files with the Write / Edit tools, never with shell heredocs (they destroy backslashes such as `\n`).
5. Do not start the editor window, do not close any window, do not run the game, do not send anything to a network.
6. Comments and messages in plain English, short. No em dashes.

## Building and testing (one build at a time for everyone)

Builds share one output folder, so take the lock first:

```bash
cd /e/IW8-Radiant
until mkdir out/build.lock 2>/dev/null; do sleep 20; done      # wait for the lock (give up after 15 minutes)
xmake build iw8r_tests 2>&1 | grep -i " error \|build ok"
bin/release/iw8r_tests.exe 2>&1 | grep "  FAIL\|checks,"
rmdir out/build.lock                                            # ALWAYS release it, also after a failure
```

For Qt files, inside the same lock, also run:

```bash
powershell -ExecutionPolicy Bypass -File tools/dev_rebuild_editor.ps1 -NoLaunch 2>&1 | grep -i "error\|build ok"
```

(`-NoLaunch` is required. Never run that script without it.)

If the build fails in a file that is not yours, another helper is mid-change: release the lock, wait 60 seconds, try
again (up to 5 times). Never edit someone else's file to get past it. Never hold the lock while you think or edit; take
it only to build and test.

## Your report

End with: the files you created or changed, the public functions (signatures) the main session should call to wire your
work into the menus, how many tests you added and the last "checks, failures, tests" line you saw, and anything you
could not verify. Do not claim something works unless you built and ran it.
