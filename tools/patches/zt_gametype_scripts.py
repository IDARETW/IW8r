import io, sys
root = sys.argv[1]


def patch(rel, pairs):
    p = root + '/' + rel
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (rel, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    std::string mapScript;
''', '''    std::string mapScript;
    // New game type scripts the map carries (IW8 Radiant research 91 / 92): IW8 GSC files
    // extracted/scripts/mp/gametypes/<name>.gsc, compiled as they are (no IW3 translation) into the map zone as
    // scripts/mp/gametypes/<name>.gsc. A stock name cannot be replaced from a map zone (research 91), so these are new
    // names; the match is started with that game type.
    std::vector<std::pair<std::string, std::string>> gameTypeScripts;
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , mapScript(std::move(other.mapScript))
''', '''    , mapScript(std::move(other.mapScript))
    , gameTypeScripts(std::move(other.gameTypeScripts))
'''),
 ('''        mapScript = std::move(other.mapScript);
''', '''        mapScript = std::move(other.mapScript);
        gameTypeScripts = std::move(other.gameTypeScripts);
'''),
 ('''        // Chyron unit names: stock IW8's most common pair (Shipment's), whatever''', '''        // New game type scripts (IW8 GSC, compiled as they are): extracted/scripts/mp/gametypes/<name>.gsc.
        {
            const auto gameTypeDirectory = exportRoot / "scripts" / "mp" / "gametypes";
            std::error_code gameTypeError;
            if (std::filesystem::is_directory(gameTypeDirectory, gameTypeError))
                for (const auto &entry : std::filesystem::directory_iterator(gameTypeDirectory))
                {
                    if (!entry.is_regular_file() || Lowercase(entry.path().extension().string()) != ".gsc")
                        continue;
                    const std::string name = Lowercase(entry.path().stem().string());
                    bool valid = !name.empty() && name.size() <= 11;
                    for (const char c : name)
                        valid = valid && (std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_');
                    std::string text;
                    if (!valid)
                        zt::warn("iw3: game type script %s left out: the name must be 1 to 11 of a-z, 0-9 and _",
                                 entry.path().filename().string().c_str());
                    else if (zt::read_file_str(entry.path().string(), text))
                        result.gameTypeScripts.push_back({name, std::move(text)});
                }
            if (!result.gameTypeScripts.empty())
                zt::info("iw3: %zu game type script(s) of the map", result.gameTypeScripts.size());
        }
        // Chyron unit names: stock IW8's most common pair (Shipment's), whatever'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''        else if (prepared && !prepared->mapScript.empty())
            warn("scripts: pass --gsc-tool (or set IW8_GSC_TOOL) to compile the converted IW3 map "
                 "scripts; using the minimap-only startup script");''', '''        else if (prepared && !prepared->mapScript.empty())
            warn("scripts: pass --gsc-tool (or set IW8_GSC_TOOL) to compile the converted IW3 map "
                 "scripts; using the minimap-only startup script");
        // New game type scripts of the map (IW8 Radiant research 91 / 92), compiled as they are.
        if (prepared && !prepared->gameTypeScripts.empty())
        {
            if (gscTool.empty())
                throw std::runtime_error("the map carries game type scripts; pass --gsc-tool (or set IW8_GSC_TOOL) to compile them");
            const std::filesystem::path work = std::filesystem::path(path_dir(outputDirectory)) / ".iw3-scripts" / map / "gametypes";
            for (const auto &[name, source] : prepared->gameTypeScripts)
            {
                // A script that does not compile stops the build: a match started with this game type would drop.
                const auto gscbin = iw3::scripts::Compile(source, gscTool, work, name);
                const std::string scriptName = "scripts/mp/gametypes/" + name + ".gsc";
                writer.add(ASSET_TYPE_SCRIPTFILE, scriptName,
                           [scriptName, gscbin](iw8::ZoneWriter &output) {
                               iw8::replay_script::emitCompiledScript(output, scriptName, gscbin);
                           });
                info("scripts: compiled game type script %s (%zu bytes gscbin)", scriptName.c_str(), gscbin.size());
            }
        }'''),
])
print('ok')
