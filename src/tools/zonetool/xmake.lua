-- iw8r-zt: the x64-zt fork's zonetool, only IW8 and the IW3 map converter (UPSTREAM.txt), built as the fork builds it
-- (premake5.lua there): C++23, static CRT, warnings off here, /O1 except the IW8 game and the converter (/O2), no
-- whole-program optimization, the converter's fixed image base. The project-wide /utf-8, /Zc:__cplusplus and
-- NOMINMAX / WIN32_LEAN_AND_MEAN are not used for it: the fork's sources are read in the system code page and define
-- their own Windows macros. tools/patches/zt_sync_fork.py replaces everything here except this file.

local zt = "$(projectdir)/src/tools/zonetool"
local tp = "$(projectdir)/third_party"

local function zt_flags(optimize)
    set_languages("c11", "cxxlatest")
    set_runtimes("MT")
    set_warnings("none")
    set_optimize(optimize or "smallest")
    set_policy("build.optimization.lto", false)
    add_defines("_WINDOWS", "WIN32")
    if is_mode("release") then add_defines("NDEBUG") end
    add_cxflags("/bigobj", "/MP")
    on_load(function (target)
        local function strip(name, drop)
            local out = {}
            for _, v in ipairs(table.wrap(target:get(name))) do
                if not drop[v] then table.insert(out, v) end
            end
            target:set(name, out)
        end
        strip("cxflags", {["/utf-8"] = true, ["/Zc:__cplusplus"] = true, ["/permissive-"] = true})
        strip("defines", {NOMINMAX = true, WIN32_LEAN_AND_MEAN = true, _CRT_SECURE_NO_WARNINGS = true})
    end)
end

-- Library includes and defines the fork's core.includes() gives every project.
local function zt_lib_includes()
    add_includedirs(tp .. "/gsl/include", tp .. "/nlohmann_json/single_include/nlohmann", tp .. "/glaze/include",
                    tp .. "/zlib", tp .. "/lz4/lib", tp .. "/zstd/lib", tp .. "/zstd/lib/common",
                    tp .. "/libtomcrypt/src/headers", tp .. "/libtommath", tp .. "/directxtex/DirectXTex",
                    tp .. "/directxtex/Common")
    add_defines("ZLIB_CONST", "LTC_NO_FAST", "LTC_NO_PROTOTYPES", "LTC_NO_RSA_BLINDING", "LTM_DESC", "__STDC_IEC_559__",
                "MP_NO_DEV_URANDOM")
end

local function zt_includes(...)
    for _, d in ipairs({...}) do add_includedirs(zt .. "/src/" .. d) end
    add_includedirs(zt .. "/src/core", zt .. "/src/common", zt .. "/generated")
    zt_lib_includes()
end

-- Libraries (third_party/<lib>, each with its licence), compiled with the fork's settings.
target("zt_zlib")
    set_kind("static")
    zt_flags()
    add_defines("ZLIB_CONST", "_CRT_SECURE_NO_DEPRECATE")
    add_files(tp .. "/zlib/*.c")
    add_includedirs(tp .. "/zlib")

target("zt_minizip")
    set_kind("static")
    zt_flags()
    add_defines("ZLIB_CONST", "_CRT_SECURE_NO_DEPRECATE")
    add_files(tp .. "/minizip/*.c")
    add_includedirs(tp .. "/minizip", tp .. "/zlib")

target("zt_lz4")
    set_kind("static")
    zt_flags()
    add_defines("ZLIB_CONST", "_CRT_SECURE_NO_DEPRECATE")
    add_files(tp .. "/lz4/lib/*.c")
    add_includedirs(tp .. "/lz4/lib")

target("zt_zstd")
    set_kind("static")
    zt_flags()
    add_files(tp .. "/zstd/lib/common/*.c", tp .. "/zstd/lib/compress/*.c", tp .. "/zstd/lib/decompress/*.c",
              tp .. "/zstd/lib/dictBuilder/*.c")
    add_includedirs(tp .. "/zstd/lib", tp .. "/zstd/lib/common")

target("zt_libtommath")
    set_kind("static")
    zt_flags("fastest")  -- libtommath relies on dead code elimination (MP_HAS)
    add_defines("LTM_DESC", "__STDC_IEC_559__", "MP_NO_DEV_URANDOM", "_LIB")
    add_files(tp .. "/libtommath/*.c")
    add_includedirs(tp .. "/libtommath")

target("zt_libtomcrypt")
    set_kind("static")
    zt_flags()
    add_defines("LTC_NO_FAST", "LTC_NO_PROTOTYPES", "LTC_NO_RSA_BLINDING", "LTM_DESC", "__STDC_IEC_559__",
                "MP_NO_DEV_URANDOM", "_CRT_SECURE_NO_WARNINGS", "LTC_SOURCE", "_LIB", "USE_LTM")
    add_files(tp .. "/libtomcrypt/src/**.c|**tab.c")  -- the *tab.c tables are #included by the ciphers
    add_includedirs(tp .. "/libtomcrypt/src/headers", tp .. "/libtommath")

target("zt_directxtex")
    set_kind("static")
    zt_flags()
    add_files(tp .. "/directxtex/DirectXTex/*.cpp")
    add_includedirs(tp .. "/directxtex/DirectXTex", tp .. "/directxtex/Common")

-- The fork's projects: common, core, game-iw3, game-iw8, convert-iw3-iw8, zonetool.
target("zt_common")
    set_kind("static")
    zt_flags()
    add_files(zt .. "/src/common/**.cpp")
    add_includedirs(zt .. "/src/common", tp .. "/minizip")
    zt_lib_includes()
    add_deps("zt_zlib", "zt_minizip", "zt_libtomcrypt", "zt_libtommath")

target("zt_core")
    set_kind("static")
    zt_flags()
    add_files(zt .. "/src/core/**.cpp")
    zt_includes()
    add_deps("zt_common", "zt_lz4", "zt_zstd", "zt_directxtex")

target("zt_game_iw3")
    set_kind("static")
    zt_flags()
    add_files(zt .. "/src/games/iw3/**.cpp")
    zt_includes("games/iw3", "games", "")
    add_deps("zt_core")

target("zt_game_iw8")
    set_kind("static")
    zt_flags("fastest")  -- the IW8 map baking (sun shadows, Havok shapes, zone writing) is compute bound
    add_files(zt .. "/src/games/iw8/**.cpp")
    zt_includes("games/iw8", "games", "")
    add_deps("zt_core")

target("zt_convert_iw3_iw8")
    set_kind("static")
    zt_flags("fastest")  -- map conversion is compute bound (image encoding, light, shadow and collision baking)
    add_files(zt .. "/src/convert/iw3_iw8/**.cpp")
    zt_includes("convert/iw3_iw8", "games", "")
    add_deps("zt_game_iw3", "zt_game_iw8")

target("iw8r-zt")
    set_kind("binary")
    zt_flags()
    add_files(zt .. "/src/cli/**.cpp", zt .. "/src/cli/resource.rc", zt .. "/src/convert/iw3_iw8/resources.rc")
    zt_includes("cli", "games", "")
    add_includedirs(zt .. "/src/convert/iw3_iw8")
    add_deps("zt_convert_iw3_iw8")
    add_syslinks("bcrypt", "d3dcompiler", "d3d11", "mfplat", "mfreadwrite", "mfuuid", "shlwapi", "user32", "advapi32",
                 "ole32", "shell32", "dbghelp")
    -- The Havok baker maps the Replay executable at its own image base until native Havok replaces it.
    add_ldflags("/BASE:0x180000000", "/DYNAMICBASE:NO", "/IGNORE:4702", {force = true})
