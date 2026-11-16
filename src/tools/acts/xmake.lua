-- iw8r-acts: reads Replay 1.20 zones with the game's own loader (the ACTS fork's mw19replay handler, UPSTREAM.txt),
-- built as ACTS builds it (CMakeLists.txt there): C++20, /source-charset:windows-1252, /O2, no LTO. The project-wide
-- /utf-8 and Windows macros are not used for it. The static CRT matches the zt_* libraries it shares (zlib, lz4, zstd,
-- libtomcrypt, libtommath: decompression and hashes only, so their versions do not change what it writes).
-- tools/sync/acts_sync_fork.py replaces src/, include/ and config/ here; iw8r/ and this file are ours.

local acts = "$(projectdir)/src/tools/acts"
local tp = "$(projectdir)/third_party"

target("iw8r-acts")
    set_kind("binary")
    set_languages("c11", "cxx20")
    set_runtimes("MT")
    set_warnings("none")
    set_optimize("fastest")
    set_policy("build.optimization.lto", false)
    if is_mode("release") then add_defines("NDEBUG") end
    add_cxflags("/source-charset:windows-1252", "/bigobj", "/MP")
    on_load(function (target)
        local function strip(name, drop)
            local out = {}
            for _, v in ipairs(table.wrap(target:get(name))) do
                if not drop[v] then table.insert(out, v) end
            end
            target:set(name, out)
        end
        strip("cxflags", {["/utf-8"] = true, ["/Zc:__cplusplus"] = true})
        strip("defines", {NOMINMAX = true, WIN32_LEAN_AND_MEAN = true, _CRT_SECURE_NO_WARNINGS = true})
    end)
    add_includedirs(acts .. "/iw8r", acts .. "/src/core/acts", acts .. "/src/core/shared", acts .. "/src/core/lib",
                    acts .. "/include")
    add_includedirs(tp .. "/zlib", tp .. "/lz4/lib", tp .. "/zstd/lib", tp .. "/nlohmann_json/single_include", tp .. "/hash_library",
                    tp .. "/libtomcrypt/src/headers", tp .. "/libtommath")
    add_defines("ACTS_COMMON_STATIC")  -- the ACTS API compiled in, not imported from acts-common.dll
    add_defines("LTC_NO_FAST", "LTC_NO_PROTOTYPES", "LTC_NO_RSA_BLINDING", "LTM_DESC", "__STDC_IEC_559__", "MP_NO_DEV_URANDOM")
    add_files(acts .. "/iw8r/*.cpp", tp .. "/hash_library/sha256.cpp")
    add_files(acts .. "/src/core/**.cpp|acts/tools/fastfile/fastfile_handlers.cpp|acts/hashutils.cpp|acts/tools/tools.cpp")
    add_deps("zt_zlib", "zt_lz4", "zt_zstd", "zt_libtomcrypt", "zt_libtommath")
    add_syslinks("user32", "advapi32", "shell32", "ole32", "dbghelp", "psapi", "bcrypt")
