-- IW8 Radiant Suite build.
-- Requires: Visual Studio 2022 Build Tools (C++ workload), xmake >= 2.8, Qt 6.8.3 msvc2022_64.
--   xmake f -m release --qt=E:\Qt\6.8.3\msvc2022_64 -y
--   xmake build -y iw8r_tests && xmake build -y iw8r && xmake build -y IW8Radiant
-- Outputs: bin/<mode>/IW8Radiant.exe (+ Qt DLLs via windeployqt), bin/<mode>/iw8r.exe, bin/<mode>/iw8r_tests.exe
set_project("iw8-radiant-suite")
set_version("0.1.0")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")
set_warnings("all")
set_targetdir("$(projectdir)/bin/$(mode)")

if is_plat("windows") then
    add_cxflags("/utf-8", "/permissive-", "/Zc:__cplusplus", "/bigobj")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    if is_mode("release") then
        set_symbols("debug") -- keep PDBs for crash triage
    end
end

-- Editor core: lossless .map / IW8 entity-string model, geometry, selection, undo/redo, IW8 MapEnts reader.
-- No UI code: every shell and tool links this.
target("iw8r_core")
    set_kind("static")
    add_files("src/core/*.cpp")
    add_includedirs("src/core", {public = true})
    -- stb_image (third_party/stb, public domain / MIT): included only by src/core/stb_image_impl.cpp.
    add_includedirs("third_party/stb")

-- Command line tool used by mappers' scripts and by the milestone tests.
target("iw8r")
    set_kind("binary")
    add_deps("iw8r_core")
    add_files("src/cli/*.cpp")

-- Unit + fixture tests (synthetic, IW8 sample room, stock IW8 MapEnts, optional grammar corpus).
target("iw8r_tests")
    set_kind("binary")
    add_deps("iw8r_core")
    add_files("tests/*.cpp")

-- Shared viewport code: D3D11 scene renderer, render/pick cache, camera math.
target("iw8r_viewport")
    set_kind("static")
    add_deps("iw8r_core")
    add_files("src/viewport/*.cpp")
    add_includedirs("src/viewport", {public = true})
    add_syslinks("d3d11", "dxgi", "d3dcompiler", {public = true})

-- Qt Advanced Docking System 4.4.1 (LGPL-2.1), built as a DLL from third_party/qtads so the editor
-- links it dynamically (same LGPL posture as Qt itself).
target("qtads")
    set_kind("shared")
    add_rules("qt.shared")
    set_basename("qtadvanceddocking")
    add_frameworks("QtCore", "QtGui", "QtWidgets")
    add_defines("ADS_SHARED_EXPORT")
    add_includedirs("third_party/qtads/src", {public = true})
    -- moc does not see MSVC's predefined macros; without _WIN32 it takes ADS's Linux (QDockWidget) branch.
    on_load(function (target) target:set("qt.moc.flags", "-D_WIN32", "-DWIN32", "-D_WIN64", "-D_MSC_VER=1944") end)
    add_files("third_party/qtads/src/*.cpp")
    add_files("third_party/qtads/src/ads.qrc")
    for _, h in ipairs({"AutoHideDockContainer", "AutoHideSideBar", "AutoHideTab", "DockAreaTabBar", "DockAreaTitleBar_p",
                        "DockAreaTitleBar", "DockAreaWidget", "DockContainerWidget", "DockFocusController", "DockManager",
                        "DockOverlay", "DockSplitter", "DockWidget", "DockWidgetTab", "ElidingLabel", "FloatingDockContainer",
                        "FloatingDragPreview", "PushButton", "ResizeHandle"}) do
        add_files("third_party/qtads/src/" .. h .. ".h")
    end
    add_syslinks("user32", "dwmapi")

-- The editor: Qt 6 Widgets + Qt Advanced Docking System, Direct3D 11 camera view.
target("IW8Radiant")
    add_rules("qt.widgetapp")
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtTest", "QtNetwork")
    add_deps("iw8r_core", "iw8r_viewport", "qtads")
    add_files("src/qtapp/*.cpp")
    add_files("src/qtapp/*.h")
    on_load(function (target) target:set("qt.moc.flags", "-D_WIN32", "-DWIN32", "-D_WIN64", "-D_MSC_VER=1944") end)
    add_syslinks("d3d11", "dxgi", "d3dcompiler", "user32", "dbghelp", "psapi")
    after_build(function (target)
        -- Copy the Qt runtime next to the exe so it runs from bin/<mode> without a Qt install on PATH.
        local qt = target:data("qt")
        local bindir = qt and qt.bindir
        if bindir and os.isfile(path.join(bindir, "windeployqt.exe")) then
            os.vrunv(path.join(bindir, "windeployqt.exe"), {"--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw",
                     "--no-compiler-runtime", target:targetfile()})
            -- The offscreen platform runs --gui-test without a visible window.
            local offscreen = path.join(qt.sdkdir, "plugins", "platforms", "qoffscreen.dll")
            if os.isfile(offscreen) then
                os.cp(offscreen, path.join(target:targetdir(), "platforms", "qoffscreen.dll"))
            end
        end
    end)
