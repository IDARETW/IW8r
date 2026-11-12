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

patch('src/games/iw8/zone/map_zone.h', [
 ('''    std::vector<MapLootPoint> loot;
''', '''    std::vector<MapLootPoint> loot;
    // One loot zone over every loot point (the small stock BR maps have one: mp_br_tut "tutorial_scavenger"); the loot
    // tables pick the loot set by the zone name (IW8 Radiant research 87 section 2.4). Empty: no zone.
    std::string lootZoneName;
    uint32_t lootZoneString = 0;  // its script-string index
'''),
 ('''        stamp16(me, 0x2D0, static_cast<uint16_t>(lootCount));
        stamp64(me, 0x2D8, PTR_FOLLOWS);
''', '''        stamp16(me, 0x2D0, static_cast<uint16_t>(lootCount));
        stamp64(me, 0x2D8, PTR_FOLLOWS);
        if (!scriptables->lootZoneName.empty())
        {
            stamp16(me, 0x2F0, 1);           // SpawnGroup.zoneCount
            stamp64(me, 0x2F8, PTR_FOLLOWS); // SpawnGroup.zones
        }
'''),
 ('''            zb.writeT<uint16_t>(point.cache ? 0xFFFBu : 0xFFFDu);
            zb.writeT<uint16_t>(0);
        }
    }
''', '''            zb.writeT<uint16_t>(point.cache ? 0xFFFBu : 0xFFFDu);
            zb.writeT<uint16_t>(scriptables->lootZoneName.empty() ? 0 : 1);
        }
        if (!scriptables->lootZoneName.empty())
        {
            // SpawnGroupZone (56 bytes, align 8): name, pointCount, points, clusterCount, clusters, bounds. The zone
            // lists every point except the cache points (rule 3 of research 89: as every stock zone does).
            std::vector<uint16_t> listed;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (size_t i = 0; i < scriptables->loot.size(); ++i)
            {
                const auto &point = scriptables->loot[i];
                for (unsigned k = 0; k < 3; ++k)
                {
                    lo[k] = (std::min)(lo[k], point.origin[k]);
                    hi[k] = (std::max)(hi[k], point.origin[k]);
                }
                if (!point.cache)
                    listed.push_back(static_cast<uint16_t>(i));
            }
            zb.align(7);
            uint8_t zone[56]{};
            stamp32(zone, 0, scriptables->lootZoneString);
            stamp16(zone, 4, static_cast<uint16_t>(listed.size()));
            if (!listed.empty())
                stamp64(zone, 8, PTR_FOLLOWS);
            for (unsigned k = 0; k < 3; ++k)
            {
                // A margin of 64 units around the points, as the stock zone boxes reach past their points.
                stampf(zone, 32 + k * 4, (lo[k] + hi[k]) * 0.5f);
                stampf(zone, 44 + k * 4, (hi[k] - lo[k]) * 0.5f + 64.0f);
            }
            zb.write(zone, sizeof(zone));
            if (!listed.empty())
            {
                zb.align(1);
                for (const auto index : listed)
                    zb.writeT<uint16_t>(index);
            }
        }
    }
'''),
 ('''    const std::vector<MapLootPoint> *lootPoints = nullptr)
{
''', '''    const std::vector<MapLootPoint> *lootPoints = nullptr, const std::string &lootZoneName = {})
{
'''),
 ('''        result.scriptables->loot = *lootPoints;
''', '''        result.scriptables->loot = *lootPoints;
        if (!lootZoneName.empty())
        {
            result.scriptables->lootZoneName = lootZoneName;
            result.scriptables->lootZoneString = result.spawns->intern(lootZoneName);
        }
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    // Loot points of the map (collision JSON "loot_points").
''', '''    // The name of the one loot zone over the loot points (collision JSON "loot_zone"; empty: none).
    std::string lootZone;
    // Loot points of the map (collision JSON "loot_points").
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , lootPoints(std::move(other.lootPoints))
''', '''    , lootZone(std::move(other.lootZone))
    , lootPoints(std::move(other.lootPoints))
'''),
 ('''        lootPoints = std::move(other.lootPoints);
''', '''        lootPoints = std::move(other.lootPoints);
        lootZone = std::move(other.lootZone);
'''),
 ('''    result.lootPoints = ReadLootPoints(collision);
''', '''    result.lootPoints = ReadLootPoints(collision);
    if (collision.contains("loot_zone"))
    {
        if (!collision.at("loot_zone").is_string())
            throw std::runtime_error("loot_zone is not a name");
        result.lootZone = collision.at("loot_zone").get<std::string>();
        for (const char c : result.lootZone)
            if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
                throw std::runtime_error("loot_zone '" + result.lootZone + "' is not a lower-case name");
    }
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''            prepared ? &prepared->lootPoints : nullptr);
''', '''            prepared ? &prepared->lootPoints : nullptr, prepared ? prepared->lootZone : std::string());
'''),
])
print('ok')
