// The fastfile command of iw8r-acts. From the ACTS fork's tools/fastfile/fastfile_handlers.cpp (ACTS by ate47, GPL-3
// or MIT, used here under MIT: src/tools/acts/LICENSE.md), trimmed to reading Replay 1.20 zones with the mw19replay
// handler: no CASC storage, linkers, compressors, asset-pool workflow or REPL, GSC formatter, translation keys, game
// ids or hardware breakpoints. What is kept is the fork's code.

#include <includes.hpp>
#include <atomic>
#include <regex>
#include <chrono>
#include <thread>
#include <tools/fastfile/fastfile_handlers.hpp>

namespace fastfile {
    std::vector<FFDecompressor*>& GetDecompressors() {
        static std::vector<FFDecompressor*> handlers{};
        return handlers;
    }

    std::vector<FFHandler*>& GetHandlers() {
        static std::vector<FFHandler*> handlers{};
        return handlers;
    }

    bool FFDecompressor::MatchFile(const std::filesystem::path& path, core::bytebuffer::ByteBuffer& data) const {
        uint64_t magic{ *data.Ptr<uint64_t>() };
        return (magic & mask) == (this->magic & mask);
    }

    FFDecompressor* FindDecompressor(const std::filesystem::path& path, core::bytebuffer::ByteBuffer& data) {
        for (FFDecompressor* handler : GetDecompressors()) {
            if (handler->MatchFile(path, data)) {
                return handler;
            }
        }
        return nullptr;
    }

    FFHandler* FindHandler(const char* name) {
        for (FFHandler* handler : GetHandlers()) {
            if (!_strcmpi(name, handler->name)) {
                return handler;
            }
        }
        return nullptr;
    }

    namespace {
        void RegisterCrypts() {
            int r;
            if ((r = register_hash(&sha256_desc)) != CRYPT_OK)
                throw std::runtime_error(std::format("Can't register sha256_desc {}", error_to_string(r)));
            if ((r = register_cipher(&aes_desc)) != CRYPT_OK)
                throw std::runtime_error(std::format("Can't register aes_desc {}", error_to_string(r)));
            if ((r = crypt_mp_init("ltm")) != CRYPT_OK)
                throw std::runtime_error(std::format("Can't init ltm {}", error_to_string(r)));
        }

        fastfile::FastFileContext* currentCtx{};
        fastfile::FastFileOption* currentOpt{};
    } // namespace

    fastfile::FastFileContext& GetCurrentContext() { return *currentCtx; }

    fastfile::FastFileOption& GetCurrentOptions() { return *currentOpt; }

    FastFileOption::FastFileOption() : ActsAPIFastFile_FastFileOption() {}

    FastFileOption::~FastFileOption() {}

    bool FastFileOption::Compute(const char** args, size_t startIndex, size_t endIndex) {
        // default values
        for (size_t i = startIndex; i < endIndex; i++) {
            const char* arg = args[i];

            if (!strcmp("-?", arg) || !_strcmpi("--help", arg) || !strcmp("-h", arg)) {
                m_help = true;
            } else if (!strcmp("-o", arg) || !_strcmpi("--output", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                m_output = outputPath = args[++i];
            } else if (!strcmp("-g", arg) || !_strcmpi("--game", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                game = args[++i];
            } else if (!strcmp("-r", arg) || !_strcmpi("--handler", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                const char* id{ args[++i] };
                if (!(handler = FindHandler(id))) {
                    LOG_ERROR("Can't find handler for name '{}'", id);
                    return false;
                }
            } else if (!strcmp("-a", arg) || !_strcmpi("--assets", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                assetTypes = args[++i];
            } else if (!_strcmpi("--patch", arg) || !strcmp("-p", arg)) {
                m_fd = true;
            } else if (!_strcmpi("--no-auto-patch", arg)) {
                replayNoAutoPatch = true;
            } else if (!strcmp("-c", arg) || !_strcmpi("--fc", arg)) {
                m_fc = true;
            } else if (!strcmp("-R", arg) || !_strcmpi("--handlers", arg)) {
                print_handlers = true;
            } else if (!strcmp("-w", arg) || !_strcmpi("--wildcard", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                wildcard = args[++i];
            } else if (!strcmp("-W", arg) || !_strcmpi("--ignore-wildcard", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                ignoreWildcard = args[++i];
            } else if (!strcmp("-I", arg) || !_strcmpi("--ignore", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                ignore = args[++i];
            } else if (!strcmp("-n", arg) || !_strcmpi("--name", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                assets = args[++i];
            } else if (!strcmp("-D", arg) || !_strcmpi("--decompressors", arg)) {
                print_decompressors = true;
            } else if (!strcmp("-l", arg) || !_strcmpi("--reduced-logs", arg)) {
                reducedLogs = true;
            } else if (!_strcmpi("--noWorkerPreload", arg)) {
                noWorkerPreload = true;
            } else if (!strcmp("-d", arg) || !_strcmpi("--dump", arg)) {
                dump_decompressed = true;
            } else if (!_strcmpi("--header", arg) || !strcmp("-H", arg)) {
                m_header = true;
            } else if (!_strcmpi("--headerDump", arg)) {
                if (i + 1 == endIndex) {
                    LOG_ERROR("Missing value for param '{}'!", arg);
                    return false;
                }
                headerDump = args[++i];
            } else if (!_strcmpi("--noAssetDump", arg)) {
                noAssetDump = true;
            } else if (!_strcmpi("--trace-replay-streams", arg)) {
                replayStreamTrace = true;
            } else if (!_strcmpi("--test", arg)) {
                replayTest = true;
            } else if (!_strcmpi("--geometry", arg)) {
                replayGeometry = true;
            } else if (!_strcmpi("--limit-per-pool", arg)) {
                if (i + 1 == endIndex)
                    return false;
                std::string value{ args[++i] };
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos || value.size() > 2)
                    return false;
                replayLimit = std::stoul(value);
                if (!replayLimit || replayLimit > 32)
                    return false;
            } else if (!_strcmpi("--xpak", arg)) {
                if (i + 1 == endIndex || replayPackages.size() >= 64)
                    return false;
                replayPackages.emplace_back(args[++i]);
            } else if (!_strcmpi("--oodle", arg)) {
                if (i + 1 == endIndex)
                    return false;
                replayOodle = args[++i];
            } else if (!_strcmpi("--serve", arg)) {
                replayServe = true;
            } else if (!_strcmpi("--serve-cache-mb", arg)) {
                if (i + 1 == endIndex)
                    return false;
                std::string value{ args[++i] };
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos || value.size() > 6)
                    return false;
                replayServeCacheMb = std::stoul(value);
            } else if (!strcmp("-i", arg) || !_strcmpi("--fd-ignore", arg)) {
                m_fdIgnoreMissing = true;
            } else if (*arg == '-') {
                LOG_ERROR("Invalid argument '{}'!", arg);
                return false;
            } else {
                files.push_back(arg);
            }
        }
        if (!outputPath) {
            m_output = outputPath = "output_ff";
        }
        return true;
    }

    void FastFileOption::PrintHelp() {
        LOG_INFO("iw8r-acts fastfile -r mw19replay -g <Replay 1.20 exe> [options] <file.ff | folder>...");
        LOG_INFO("-h --help                 : Print help");
        LOG_INFO("-o --output [d]           : Output dir");
        LOG_INFO("-g --game [g]             : The Replay 1.20 executable");
        LOG_INFO("-r --handler [h]          : Fastfile handler (mw19replay)");
        LOG_INFO("-R --handlers             : Print handlers");
        LOG_INFO("-D --decompressors        : Print decompressors");
        LOG_INFO("-a --assets [a]           : Asset types to export, comma separated");
        LOG_INFO("-n --name [n]             : Asset names to export, comma separated");
        LOG_INFO("-w --wildcard [w]         : Only files matching this regex");
        LOG_INFO("-W --ignore-wildcard [w]  : Ignore files matching this regex");
        LOG_INFO("-I --ignore [d]           : Ignore files in this folder");
        LOG_INFO("-p --patch                : Apply the fastfile patches (.fp)");
        LOG_INFO("--no-auto-patch           : Do not apply a patch found beside the fastfile");
        LOG_INFO("-c --fc                   : Fastfile patch chain");
        LOG_INFO("-i --fd-ignore            : Ignore missing patch files");
        LOG_INFO("-l --reduced-logs         : Reduced logs");
        LOG_INFO("-d --dump                 : Dump the decompressed zone");
        LOG_INFO("-H --header               : Dump the fastfile header");
        LOG_INFO("--headerDump [f]          : Header dump file");
        LOG_INFO("--noAssetDump             : No asset dump");
        LOG_INFO("--noWorkerPreload         : Disable worker preload");
        LOG_INFO("--trace-replay-streams    : Trace the Replay load streams");
        LOG_INFO("--test                    : Replay report-only exporter checks, one sample per type");
        LOG_INFO("--limit-per-pool [1..32]  : Replay --test sample count per type");
        LOG_INFO("--geometry                : Replay geometry export");
        LOG_INFO("--xpak [file]             : Replay streamed payload archive (repeatable)");
        LOG_INFO("--oodle [absolute DLL]    : Replay XPak Oodle decoder");
        LOG_INFO("--serve                   : Replay request loop on stdin, loaded zones stay in memory");
        LOG_INFO("--serve-cache-mb [MiB]    : Replay --serve memory budget for retained zones (default 2048)");
    }

    hook::module_mapper::Module& FastFileOption::GetGameModule(
        bool crashError, bool* init, bool needDecrypt, const char* defaultName, const char* dumperName
    ) {
        if (init)
            *init = false;

        std::filesystem::path exe;
        if (game) {
            exe = game;
        } else if (defaultName && *defaultName) {
            exe = utils::GetProgDir() / "deps" / defaultName;
        } else {
            if (crashError) {
                throw std::runtime_error("No game module specified");
            }
            gameMod.Free();
            return gameMod;
        }

        if (!gameMod) {
            if (!gameMod.Load(exe)) {
                throw std::runtime_error(std::format("Can't load {}", exe.string()));
            }
            if (needDecrypt) {
                throw std::runtime_error(std::format("Can't load {}: string decryption is not part of iw8r-acts", exe.string()));
            }

            if (init)
                *init = true;
        }

        return gameMod;
    }
    hook::module_mapper::Module& FastFileOption::GetGameModule() {
        if (!gameMod) {
            throw std::runtime_error("no game module loaded for context");
        }
        return gameMod;
    }

    size_t CountDiv(const char* name) {
        size_t c{};
        for (; *name; name++) {
            if (*name == '_')
                c++;
        }
        return c;
    }

    const char* FastFileContext::GetFFType() { return ActsAPIFastFile_FastFileContext_GetType(this); }

    std::vector<std::string> FastFileOption::GetFileRecurse(const char* path) {
        std::vector<std::string> res{};
        if (gamePath) {
            std::filesystem::path root{ gamePath };

            std::vector<std::filesystem::path> files{};
            utils::GetFileRecurse(path[0] ? root / path : root, files);

            for (const std::filesystem::path& file : files) {
                res.push_back(file.string());
            }
        } else {
            std::vector<std::filesystem::path> files{};

            utils::GetFileRecurse(path, files);

            for (const std::filesystem::path& file : files) {
                res.push_back(file.string());
            }
        }

        return res;
    }

    bool FastFileOption::ReadFile(const char* path, std::vector<byte>& buff) {
        return utils::ReadFile(path, buff) && !buff.empty();
    }

    namespace {
        // True when the volume of the file reports no seek penalty (SSD): only then are parallel ranges a gain.
        bool HasNoSeekPenalty(const std::filesystem::path& file) {
            std::error_code ec;
            const std::wstring root{ std::filesystem::absolute(file, ec).root_name().wstring() };
            if (ec || root.size() != 2 || root[1] != L':')
                return false;
            const std::wstring volume{ L"\\\\.\\" + root };
            HANDLE h{ CreateFileW(
                volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr
            ) };
            if (h == INVALID_HANDLE_VALUE)
                return false;
            STORAGE_PROPERTY_QUERY query{};
            query.PropertyId = StorageDeviceSeekPenaltyProperty;
            query.QueryType = PropertyStandardQuery;
            DEVICE_SEEK_PENALTY_DESCRIPTOR result{};
            DWORD bytes{};
            const bool ok{ DeviceIoControl(
                               h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &result, sizeof(result),
                               &bytes, nullptr
                           ) &&
                           bytes >= sizeof(result) };
            CloseHandle(h);
            return ok && !result.IncursSeekPenalty;
        }

        bool ReadRange(const wchar_t* path, byte* dest, uint64_t offset, uint64_t length) {
            HANDLE h{ CreateFileW(
                path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr
            ) };
            if (h == INVALID_HANDLE_VALUE)
                return false;
            bool ok{ true };
            while (ok && length) {
                OVERLAPPED at{};
                at.Offset = (DWORD)offset;
                at.OffsetHigh = (DWORD)(offset >> 32);
                const DWORD want{ (DWORD)std::min<uint64_t>(length, 16ull << 20) };
                DWORD got{};
                ok = ::ReadFile(h, dest, want, &got, &at) && got;
                dest += got;
                offset += got;
                length -= got;
            }
            CloseHandle(h);
            return ok;
        }
    } // namespace

    bool FastFileOption::ReadFile(const char* path, std::unique_ptr<byte[]>& buff, size_t& size) {
        buff.reset();
        size = 0;
        const std::filesystem::path file{ path };
        std::error_code ec;
        const uint64_t length{ std::filesystem::file_size(file, ec) };
        if (ec || !length)
            return false;
        std::unique_ptr<byte[]> data{ std::make_unique_for_overwrite<byte[]>(length) };
        constexpr uint64_t minRange = 8ull << 20;
        size_t threads{ 1 };
        if (length >= 2 * minRange && HasNoSeekPenalty(file))
            threads = (size_t)std::min<uint64_t>({ length / minRange, 8, std::max(2u, std::thread::hardware_concurrency()) });
        const std::wstring wide{ file.wstring() };
        std::atomic<bool> ok{ true };
        const uint64_t part{ (length / threads + 0xFFFF) & ~uint64_t{ 0xFFFF } };
        auto readPart = [&](size_t i) {
            const uint64_t begin{ std::min<uint64_t>(part * i, length) };
            const uint64_t end{ i + 1 == threads ? length : std::min<uint64_t>(part * (i + 1), length) };
            if (end > begin && !ReadRange(wide.c_str(), data.get() + begin, begin, end - begin))
                ok.store(false);
        };
        {
            std::vector<std::thread> pool;
            pool.reserve(threads - 1);
            for (size_t i = 1; i < threads; i++)
                pool.emplace_back(readPart, i);
            readPart(0);
            for (std::thread& t : pool)
                t.join();
        }
        if (!ok.load())
            return false;
        buff = std::move(data);
        size = length;
        return true;
    }

    namespace {
        class FFLoadContext {
          public:
            FastFileOption& opt;
            std::unique_ptr<byte[]> buff{};
            size_t buffSize{};
            std::vector<byte> ffdata{};
            size_t count{};
            size_t completed{};
            FFDecompressor* lastDecompressor{};

            FFLoadContext(FastFileOption& opt) : opt(opt) {}

            ~FFLoadContext() {
                if (lastDecompressor)
                    lastDecompressor->Cleanup();
                if (opt.handler)
                    opt.handler->Cleanup();
            }

            void Init() {
                if ((opt.replayTest || opt.replayGeometry || opt.replayStreamTrace || opt.replayLimit != 1 ||
                     !opt.replayPackages.empty() || !opt.replayOodle.empty()) &&
                    (!opt.handler || std::strcmp(opt.handler->name, "mw19replay")))
                    throw std::runtime_error("Replay test and package options require the mw19replay fastfile handler");
                if (opt.replayLimit != 1 && !opt.replayTest)
                    throw std::runtime_error("--limit-per-pool requires --test");
                if (opt.replayServe && (!opt.handler || std::strcmp(opt.handler->name, "mw19replay")))
                    throw std::runtime_error("--serve requires the mw19replay fastfile handler");
                if (opt.replayServe && (opt.replayStreamTrace || opt.dump_decompressed))
                    throw std::runtime_error("--serve cannot be combined with stream traces or dumps");
                if (opt.replayGeometry && opt.noAssetDump)
                    throw std::runtime_error("--geometry requires asset export; remove --noAssetDump");
                if (opt.replayTest && (opt.dump_decompressed || opt.m_header || opt.headerDump))
                    throw std::runtime_error("Replay --test cannot be combined with file dumping");

                if (opt.handler) {
                    opt.handler->Init(opt);

                    if (!opt.files.empty() && !opt.m_fd && !opt.handler->noPatchOk) {
                        LOG_WARNING("-p option not specified, it might cause an error with patched fastfiles");
                    }
                }

                RegisterCrypts();
            }

            void UpdateDecompressor(FFDecompressor* decompressor) {
                if (lastDecompressor != decompressor) {
                    if (lastDecompressor) {
                        lastDecompressor->Cleanup();
                    }
                    lastDecompressor = decompressor;
                    decompressor->Init(opt);
                }
            }

            bool LoadFastFile(const char* f, const char* fWildcard = nullptr, const char* iWildcard = nullptr) {
                std::regex wildcard{ fWildcard ? fWildcard : ".*" };
                std::regex ignoreWildcard{ iWildcard ? iWildcard : "." };

                bool anyLoaded{};
                for (std::string& filename : opt.GetFileRecurse(f)) {
                    if (!filename.ends_with(".ff") && !filename.ends_with(".ff.zone") && !filename.ends_with(".ffd")) {
                        LOG_TRACE("Ignore {}", filename);
                        continue;
                    }
                    std::filesystem::path flpname{ filename };
                    flpname = flpname.filename();
                    std::string rfilename{ flpname.string() };
                    utils::MapString(rfilename.data(), [](char c) -> char { return c == '\\' ? '/' : c; });
                    if (fWildcard) {
                        std::sregex_iterator rbegin{ rfilename.begin(), rfilename.end(), wildcard };

                        if (rbegin == std::sregex_iterator() || rbegin->length() != rfilename.size()) {
                            continue; // doesn't match, we ignore it
                        }
                    }
                    if (iWildcard) {
                        std::sregex_iterator rbegin{ rfilename.begin(), rfilename.end(), ignoreWildcard };

                        if (rbegin != std::sregex_iterator() && rbegin->length() == rfilename.size()) {
                            continue; // match, we ignore it
                        }
                    }

                    if (opt.ignore) {
                        std::filesystem::path ffpath{ filename };
                        std::filesystem::path ignoreDir{ opt.ignore };

                        if (utils::IsSubDir(ignoreDir, ffpath)) {
                            continue; // ignore sub paths
                        }
                    }

                    count++;
                    anyLoaded = true;

                    const auto readStart{ std::chrono::steady_clock::now() };
                    if (!opt.ReadFile(filename.data(), buff, buffSize)) {
                        LOG_ERROR("Can't read file {}", filename);
                        continue;
                    }
                    opt.perfReadMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStart).count();
                    opt.perfDecompressMs = opt.perfPatchMs = 0;

                    core::bytebuffer::ByteBuffer reader{ buff.get(), buffSize };

                    if (!reader.CanRead(sizeof(uint64_t))) {
                        LOG_ERROR("Can't read file {}: too small", filename);
                        continue;
                    }

                    ffdata.clear();
                    uint64_t magic{ *reader.Ptr<uint64_t>() };

                    try {
                        // we use a temporary memory block for reader because we don't need the memory after reading
                        core::memory_allocator::MemoryAllocator ffMemory{};
                        fastfile::FastFileContext ctx{ .zoneMemory = ffMemory };
                        ctx.file = filename.c_str();
                        currentCtx = &ctx;
                        FFDecompressor* decompressor{ FindDecompressor(filename, reader) };

                        if (!decompressor) {
                            LOG_ERROR("Can't open {}: Can't find decompressor for magic 0x{:x}", filename, magic);
                            continue;
                        }
                        UpdateDecompressor(decompressor);

                        std::filesystem::path ffnamet{ filename };
                        ffnamet.replace_extension();
                        ffnamet = ffnamet.filename();
                        std::string ffnamets{ ffnamet.string() };
                        sprintf_s(ctx.ffname, "%s", ffnamets.data());

                        hashutils::Add(ctx.ffname, true, true);

                        LOG_INFO("Loading {}... ({})", filename, decompressor->name);

                        decompressor->LoadFastFile(opt, reader, ctx, ffdata);

                        // The decompressed stream: the context's buffer when the decompressor filled it, else the vector.
                        byte* const streamData{ ctx.decoded ? ctx.decoded.get() : ffdata.data() };
                        const size_t streamSize{ ctx.decoded ? ctx.decodedSize : ffdata.size() };
                        // The file image is not read again once the stream is decompressed.
                        buff.reset();
                        buffSize = 0;

                        LOG_TRACE("Decompressed 0x{:x} byte(s)", streamSize);

                        if (opt.dump_decompressed) {
                            std::filesystem::path of{ ctx.file };
                            std::filesystem::path decfile{ opt.m_output / ctx.ffname };

                            decfile.replace_extension(".ff.dec");

                            std::filesystem::create_directories(decfile.parent_path());
                            if (!utils::WriteFile(decfile, streamData, streamSize)) {
                                LOG_ERROR("Can't dump {}", decfile.string());
                            } else {
                                LOG_INFO("Dump into {}", decfile.string());
                            }
                        }

                        if (opt.handler) {
                            core::bytebuffer::ByteBuffer ffreader{ streamData, streamSize };
                            LOG_TRACE("Reading using {}", opt.handler->name);
                            opt.handler->Handle(opt, ffreader, ctx);
                        }
                        completed++;
                    } catch (std::runtime_error& err) {
                        LOG_ERROR("Can't read {}: {}", filename, err.what());
                    }
                    currentCtx = nullptr;
                }

                return anyLoaded;
            }
        };

        bool PrintAny(FastFileOption& opt) {
            if (!(opt.print_handlers || opt.print_decompressors)) {
                return false;
            }

            if (opt.print_handlers) {
                LOG_INFO("Handlers:");
                for (FFHandler* handler : GetHandlers()) {
                    LOG_INFO("{:10} - {}", handler->name, handler->description);
                }
            }

            if (opt.print_decompressors) {
                LOG_INFO("Decompressors:");
                for (FFDecompressor* decompr : GetDecompressors()) {
                    LOG_INFO("{:20}", decompr->name);
                }
            }

            return true;
        }
    } // namespace

    // argv[0] the exe, argv[1] "fastfile", the options from argv[2].
    int FastfileReplay(int argc, const char* argv[]) {
        FastFileOption opt{};
        opt.workflow = FFW_READER;
        currentOpt = &opt;

        bool anyPrint{};
        if (!opt.Compute(argv, 2, argc)) {
            opt.PrintHelp();
            return tool::BASIC_ERROR;
        }
        if (opt.m_help || (opt.files.empty() && !opt.replayServe && !(anyPrint = PrintAny(opt)))) {
            opt.PrintHelp();
            if (opt.files.empty() && !opt.handler) {
                LOG_ERROR("Missing entry");
                return tool::BASIC_ERROR;
            } else {
                return tool::OK;
            }
        } else if (anyPrint) {
            return tool::OK;
        }

        FFLoadContext cc{ opt };

        cc.Init();

        if (opt.replayServe) {
            // Requests name their own fastfile; files given on the command line are loaded (and retained) first.
            const bool served{ opt.handler->Serve(opt, [&cc](const char* file) {
                const size_t before{ cc.completed };
                cc.LoadFastFile(file);
                // The file image and the decompressed stream are only needed while loading.
                cc.buff.reset();
                cc.buffSize = 0;
                std::vector<byte>{}.swap(cc.ffdata);
                return cc.completed != before;
            }) };
            currentOpt = nullptr;
            if (!served) {
                LOG_ERROR("The {} handler has no --serve mode", opt.handler->name);
                return tool::BASIC_ERROR;
            }
            return tool::OK;
        }

        for (const char* f : opt.files) {
            cc.LoadFastFile(f, opt.wildcard, opt.ignoreWildcard);
        }

        currentOpt = nullptr;

        size_t errors{ cc.count - cc.completed };
        if (errors) {
            LOG_ERROR("Parsed {} (0x{:x}) file(s) with {} (0x{:x}) error(s)", cc.count, cc.count, errors, errors);
            return tool::BASIC_ERROR;
        }
        LOG_INFO("Parsed {} (0x{:x}) file(s)", cc.count, cc.count);

        return tool::OK;
    }
} // namespace fastfile

const char* ActsAPIFastFile_FastFileContext_GetType(ActsAPIFastFile_FastFileContext* context) {
    if (!context->fftype[0]) {
        std::string_view ffv{ context->ffname };
        size_t div{ fastfile::CountDiv(context->ffname) };
        size_t sub{ div > 6 ? 2ull : 1ull };
        size_t fdd{};
        for (size_t i = 0; i < sub; i++) {
            fdd = ffv.find('_', fdd ? fdd + 1 : fdd);
            if (fdd == std::string::npos) {
                fdd = ffv.size();
            }
        }

        std::memcpy(context->fftype, context->ffname, fdd);
        for (size_t i = 0; i < fdd; i++) {
            if (context->fftype[i] == '_') {
                context->fftype[i] = '/';
            }
        }
    }
    return context->fftype;
}
