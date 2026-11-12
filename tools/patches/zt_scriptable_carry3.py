import io, sys
root = sys.argv[1]
p = root + '/src/convert/iw3_iw8/convert.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)

rep('''        {
            // What stays a name reference: assets of zones the game always has loaded.
            std::map<std::string, std::pair<std::size_t, std::string>> byType;
            for (const auto &[type, name] : unresolved)
            {
                const auto format = zonetool::iw8::find_asset_format(type);
                auto &[count, listed] = byType[format ? format->name : "?"];
                ++count;
                if (listed.size() < 300)
                    listed += (listed.empty() ? "" : ", ") + name;
            }
            for (const auto &[type, entry] : byType)
                info("scriptables: %zu %s stay name references (not in %s or its techsets zone): %s", entry.first,
                     type.c_str(), zoneName.c_str(), entry.second.c_str());
        }
''', '''        {
            // What stays a name reference must be in a zone every multiplayer match has loaded, as it is for the
            // stock map. The zones are read smallest first and only while something is still unaccounted for.
            // "$..." images are engine-made.
            static const char *const kAlwaysLoaded[] = {
                "techsets_common",    "techsets_common_mp", "techsets_common_core_mp", "techsets_common_base_mp",
                "techsets_global_mp", "techsets_global_stream_mp", "code_pre_gfx", "global_core_mp", "common", "global",
                "common_core_mp",     "global_mp",          "global_stream_mp",        "code_post_gfx",
                "common_base_mp",     "common_mp"};
            const std::size_t referenced = unresolved.size();
            std::erase_if(unresolved, [](const AssetKey &key) { return !key.second.empty() && key.second.front() == '$'; });
            std::map<std::string, std::size_t> foundIn;
            for (const char *const name : kAlwaysLoaded)
            {
                if (unresolved.empty())
                    break;
                const auto path = zoneFolder / (std::string(name) + ".ff");
                if (!std::filesystem::exists(path))
                    continue;
                std::set<AssetKey> held;
                try
                {
                    const auto zone = zonetool::iw8::load_zone(path, {});
                    for (const auto &asset : zone->assets)
                        if (!asset.referenced)
                            held.emplace(asset.type, zonetool::iw8::zone_asset_name(asset.type, asset.header));
                }
                catch (const std::exception &e)
                {
                    warn("scriptables: cannot read %s to check name references: %s", name, e.what());
                    continue;
                }
                const auto before = unresolved.size();
                std::erase_if(unresolved, [&](const AssetKey &key) { return held.count(key) != 0; });
                if (before != unresolved.size())
                    foundIn[name] = before - unresolved.size();
            }
            std::string where;
            for (const auto &[name, count] : foundIn)
                where += (where.empty() ? "" : ", ") + std::to_string(count) + " in " + name;
            info("scriptables: %zu asset(s) stay name references to always-loaded zones (%s)", referenced - unresolved.size(),
                 where.empty() ? "engine-made" : where.c_str());
            std::map<std::string, std::pair<std::size_t, std::string>> byType;
            for (const auto &[type, name] : unresolved)
            {
                const auto format = zonetool::iw8::find_asset_format(type);
                auto &[count, listed] = byType[format ? format->name : "?"];
                ++count;
                if (listed.size() < 300)
                    listed += (listed.empty() ? "" : ", ") + name;
            }
            for (const auto &[type, entry] : byType)
                warn("scriptables: %zu %s named by the carried definitions are in no zone that is always loaded; "
                     "they will be missing in game: %s",
                     entry.first, type.c_str(), entry.second.c_str());
        }
        // Each carried material lives in the techsets zone and is named from the main zone by a reference entry, as
        // the map's own materials are.
        for (const auto &asset : sources[1].closure)
            if (asset.type == zonetool::iw8::ASSET_TYPE_MATERIAL)
                carry.materialNames.insert(zonetool::iw8::zone_asset_name(asset.type, asset.header));
''')
rep('''    std::map<std::string, void *> definitions;                      // definition name -> its header
''', '''    std::map<std::string, void *> definitions;                      // definition name -> its header
    std::set<std::string> materialNames;                            // carried materials (techsets zone)
''')
# register main-zone references for carried materials before the main zone is built
rep('''        const ScriptableCarry carry = CarryScriptableDefinitions(scriptableInstances, args.replayExecutable);
''', '''        const ScriptableCarry carry = CarryScriptableDefinitions(scriptableInstances, args.replayExecutable);
        for (const auto &name : carry.materialNames)
            if (!writer.has(ASSET_TYPE_MATERIAL, name))
                iw8::stock::RegisterNameReference(writer, ASSET_TYPE_MATERIAL, name);
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
