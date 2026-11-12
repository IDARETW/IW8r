import io, sys
root = sys.argv[1]
p = root + '/src/convert/iw3_iw8/convert.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('ScriptableCarry CarryScriptableDefinitions(')
end = s.index('// `prepare` (optional) runs on the loaded zone')
new = '''ScriptableCarry CarryScriptableDefinitions(const std::vector<iw8::ScriptableInstance> &instances,
                                           const std::string &replayExecutable)
{
    ScriptableCarry carry;
    std::map<std::string, std::set<std::string>> wanted; // stock zone -> definitions
    for (const auto &instance : instances)
        if (!instance.carryZone.empty())
            wanted[instance.carryZone].insert(instance.definition);
    if (wanted.empty())
        return carry;
    if (replayExecutable.empty())
        throw std::runtime_error("carrying a stock scriptable definition needs --replay (the game folder)");
    const auto zoneFolder = std::filesystem::path(replayExecutable).parent_path() / "zone";

    // One stock zone in memory with its full definitions by type and name.
    struct Source
    {
        std::unique_ptr<zonetool::iw8::loaded_zone> zone;
        std::map<AssetKey, zonetool::iw8::zone_asset> full;
        std::vector<zonetool::iw8::zone_asset> roots;
        std::set<AssetKey> rootKeys;
        std::vector<zonetool::iw8::zone_asset> closure;
    };
    const auto open = [&](const std::string &name, const bool required) {
        Source source;
        const auto path = zoneFolder / (name + ".ff");
        if (!std::filesystem::exists(path))
        {
            if (required)
                throw std::runtime_error("stock zone " + path.string() + " (scriptable definitions) was not found");
            return source;
        }
        source.zone = zonetool::iw8::load_zone(path, {.keep_temp_data = true});
        for (const auto &asset : source.zone->assets)
            if (!asset.referenced)
                source.full.try_emplace(AssetKey{asset.type, zonetool::iw8::zone_asset_name(asset.type, asset.header)},
                                        zonetool::iw8::zone_asset{asset.type, asset.header, false});
        return source;
    };

    for (const auto &[zoneName, names] : wanted)
    {
        info("scriptables: reading stock zone %s for %zu definition(s)", zoneName.c_str(), names.size());
        // The map zone holds the definition, its models and the images; its techsets companion holds the materials
        // of those models with their technique sets and shaders. Each names the other's assets by reference, so the
        // two closures are followed until neither names anything the other holds.
        std::array<Source, 2> sources{open(zoneName, true), open("techsets_" + zoneName, false)};
        for (const auto &name : names)
        {
            const AssetKey key{zonetool::iw8::ASSET_TYPE_SCRIPTABLE, name};
            const auto found = sources[0].full.find(key);
            if (found == sources[0].full.end())
                throw std::runtime_error("stock zone " + zoneName + " does not hold the scriptable definition " + name);
            if (carry.definitions.emplace(name, found->second.header).second && sources[0].rootKeys.insert(key).second)
                sources[0].roots.push_back(found->second);
        }
        std::set<AssetKey> unresolved;
        for (int round = 0; round < 8; ++round)
        {
            bool grew = false;
            unresolved.clear();
            std::vector<AssetKey> references;
            for (auto &source : sources)
            {
                if (!source.zone || source.roots.empty())
                    continue;
                std::vector<AssetKey> named;
                source.closure = zonetool::iw8::asset_closure(*source.zone, source.roots, &named);
                references.insert(references.end(), named.begin(), named.end());
            }
            for (const auto &key : references)
            {
                bool held = false;
                for (auto &source : sources)
                {
                    const auto found = source.full.find(key);
                    if (found == source.full.end())
                        continue;
                    held = true;
                    if (source.rootKeys.insert(key).second)
                    {
                        source.roots.push_back(found->second);
                        grew = true;
                    }
                }
                if (!held)
                    unresolved.insert(key);
            }
            if (!grew)
                break;
        }
        const auto summary = [](const std::vector<zonetool::iw8::zone_asset> &assets) {
            std::map<std::string, std::size_t> byType;
            for (const auto &asset : assets)
            {
                const auto format = zonetool::iw8::find_asset_format(asset.type);
                ++byType[format ? format->name : "?"];
            }
            std::string text;
            for (const auto &[type, count] : byType)
                text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + type;
            return text;
        };
        info("scriptables: %s gives %zu asset(s): %s", zoneName.c_str(), sources[0].closure.size(),
             summary(sources[0].closure).c_str());
        if (sources[1].zone)
            info("scriptables: techsets_%s gives %zu asset(s): %s", zoneName.c_str(), sources[1].closure.size(),
                 summary(sources[1].closure).c_str());
        {
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
        carry.assets.insert(carry.assets.end(), sources[0].closure.begin(), sources[0].closure.end());
        carry.techsets.insert(carry.techsets.end(), sources[1].closure.begin(), sources[1].closure.end());
        for (auto &source : sources)
            if (source.zone)
                carry.zones.push_back(std::move(source.zone));
    }
    return carry;
}

'''.replace('\n', nl)
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
