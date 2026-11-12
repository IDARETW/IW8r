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


patch('src/games/iw8/assets/replay_dynentity.h', [
 ('''struct MapLootPoint
{
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};
    bool cache = false;
};
''', '''struct MapLootPoint
{
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};
    bool cache = false;
};

// An authored loot box of a map (collision JSON "loot_boxes", IW8 Radiant research 89 section 6d): a cluster groups
// the points inside it, a zone is named (the loot tables pick the loot by the zone name).
struct MapLootBox
{
    std::array<float, 3> mid{};
    std::array<float, 3> half{};
    std::string name;  // zones only
};
'''),
])

patch('src/games/iw8/zone/map_zone.h', [
 ('''    std::string lootZoneName;
    uint32_t lootZoneString = 0;  // its script-string index
''', '''    std::string lootZoneName;
    uint32_t lootZoneString = 0;  // its script-string index
    // Authored loot clusters and zones (collision JSON "loot_boxes"). With zones given, lootZoneName is not used: each
    // point takes the first zone whose box holds it, and the first cluster whose box holds it ("first box", the rule
    // of every stock map, research 89 section 3).
    std::vector<MapLootBox> lootClusters, lootZones;
    std::vector<uint32_t> lootZoneStrings;  // script-string index of each zone name
'''),
 ('''        stamp16(me, 0x2D0, static_cast<uint16_t>(lootCount));
        stamp64(me, 0x2D8, PTR_FOLLOWS);
        if (!scriptables->lootZoneName.empty())
        {
            stamp16(me, 0x2F0, 1);           // SpawnGroup.zoneCount
            stamp64(me, 0x2F8, PTR_FOLLOWS); // SpawnGroup.zones
        }
''', '''        stamp16(me, 0x2D0, static_cast<uint16_t>(lootCount));
        stamp64(me, 0x2D8, PTR_FOLLOWS);
        if (!scriptables->lootClusters.empty())
        {
            stamp16(me, 0x2E0, static_cast<uint16_t>(scriptables->lootClusters.size())); // SpawnGroup.clusterCount
            stamp64(me, 0x2E8, PTR_FOLLOWS);                                             // SpawnGroup.clusters
        }
        if (!scriptables->lootZones.empty())
        {
            stamp16(me, 0x2F0, static_cast<uint16_t>(scriptables->lootZones.size())); // SpawnGroup.zoneCount
            stamp64(me, 0x2F8, PTR_FOLLOWS);
        }
        else if (!scriptables->lootZoneName.empty())
        {
            stamp16(me, 0x2F0, 1);           // SpawnGroup.zoneCount
            stamp64(me, 0x2F8, PTR_FOLLOWS); // SpawnGroup.zones
        }
'''),
 ('''    if (lootCount)
    {
        zb.align(3);
        for (const auto &point : scriptables->loot)
        {
            for (const float value : point.origin)
                zb.writeT(value);
            for (const float value : point.angles)
                zb.writeT(value);
            zb.writeT<uint16_t>(point.cache ? 0xFFFBu : 0xFFFDu);
            zb.writeT<uint16_t>(scriptables->lootZoneName.empty() ? 0 : 1);
        }
        if (!scriptables->lootZoneName.empty())
''', '''    if (lootCount && (!scriptables->lootClusters.empty() || !scriptables->lootZones.empty()))
    {
        // Authored boxes: each point takes the first cluster and the first zone whose box holds it. Lists are
        // ascending; a zone lists its points except the cache points, and the clusters whose first point is in it; a
        // cluster's zone is the zone of its first point (else the zone that holds its middle). Research 89 section 3.
        const auto &clusters = scriptables->lootClusters;
        const auto &zones = scriptables->lootZones;
        const auto holds = [](const MapLootBox &box, const std::array<float, 3> &p) {
            for (unsigned k = 0; k < 3; ++k)
                if (std::abs(p[k] - box.mid[k]) > box.half[k])
                    return false;
            return true;
        };
        std::vector<uint16_t> pointCluster(lootCount, 0xFFFDu), pointZone(lootCount, 0);
        std::vector<std::vector<uint16_t>> clusterPoints(clusters.size()), zonePoints(zones.size()), zoneClusters(zones.size());
        for (size_t i = 0; i < lootCount; ++i)
        {
            const auto &point = scriptables->loot[i];
            for (size_t z = 0; z < zones.size(); ++z)
                if (holds(zones[z], point.origin))
                {
                    pointZone[i] = static_cast<uint16_t>(z + 1);
                    break;
                }
            if (point.cache)
                pointCluster[i] = 0xFFFBu;
            else
                for (size_t c = 0; c < clusters.size(); ++c)
                    if (holds(clusters[c], point.origin))
                    {
                        pointCluster[i] = static_cast<uint16_t>(c);
                        clusterPoints[c].push_back(static_cast<uint16_t>(i));
                        break;
                    }
            if (pointZone[i] && !point.cache)
                zonePoints[pointZone[i] - 1u].push_back(static_cast<uint16_t>(i));
        }
        std::vector<uint16_t> clusterZone(clusters.size(), 0);
        for (size_t c = 0; c < clusters.size(); ++c)
        {
            if (!clusterPoints[c].empty())
                clusterZone[c] = pointZone[clusterPoints[c].front()];
            else
                for (size_t z = 0; z < zones.size(); ++z)
                    if (holds(zones[z], clusters[c].mid))
                    {
                        clusterZone[c] = static_cast<uint16_t>(z + 1);
                        break;
                    }
            if (!clusterPoints[c].empty() && clusterZone[c])
                zoneClusters[clusterZone[c] - 1u].push_back(static_cast<uint16_t>(c));
        }
        zb.align(3);
        for (size_t i = 0; i < lootCount; ++i)
        {
            const auto &point = scriptables->loot[i];
            for (const float value : point.origin)
                zb.writeT(value);
            for (const float value : point.angles)
                zb.writeT(value);
            zb.writeT<uint16_t>(pointCluster[i]);
            zb.writeT<uint16_t>(pointZone[i]);
        }
        const auto writeList = [&](const std::vector<uint16_t> &list) {
            if (list.empty())
                return;
            zb.align(1);
            for (const auto index : list)
                zb.writeT<uint16_t>(index);
        };
        if (!clusters.empty())
        {
            // SpawnGroupCluster (40 bytes, align 8): zone, pointCount, points, bounds; then each cluster's points.
            zb.align(7);
            for (size_t c = 0; c < clusters.size(); ++c)
            {
                uint8_t record[40]{};
                stamp16(record, 0, clusterZone[c]);
                stamp16(record, 2, static_cast<uint16_t>(clusterPoints[c].size()));
                if (!clusterPoints[c].empty())
                    stamp64(record, 8, PTR_FOLLOWS);
                for (unsigned k = 0; k < 3; ++k)
                {
                    stampf(record, 16 + k * 4, clusters[c].mid[k]);
                    stampf(record, 28 + k * 4, clusters[c].half[k]);
                }
                zb.write(record, sizeof(record));
            }
            for (size_t c = 0; c < clusters.size(); ++c)
                writeList(clusterPoints[c]);
        }
        if (!zones.empty())
        {
            // SpawnGroupZone (56 bytes, align 8): name, pointCount, points, clusterCount, clusters, bounds; then each
            // zone's points and clusters.
            zb.align(7);
            for (size_t z = 0; z < zones.size(); ++z)
            {
                uint8_t record[56]{};
                stamp32(record, 0, scriptables->lootZoneStrings.at(z));
                stamp16(record, 4, static_cast<uint16_t>(zonePoints[z].size()));
                if (!zonePoints[z].empty())
                    stamp64(record, 8, PTR_FOLLOWS);
                stamp16(record, 16, static_cast<uint16_t>(zoneClusters[z].size()));
                if (!zoneClusters[z].empty())
                    stamp64(record, 24, PTR_FOLLOWS);
                for (unsigned k = 0; k < 3; ++k)
                {
                    stampf(record, 32 + k * 4, zones[z].mid[k]);
                    stampf(record, 44 + k * 4, zones[z].half[k]);
                }
                zb.write(record, sizeof(record));
            }
            for (size_t z = 0; z < zones.size(); ++z)
            {
                writeList(zonePoints[z]);
                writeList(zoneClusters[z]);
            }
        }
    }
    else if (lootCount)
    {
        zb.align(3);
        for (const auto &point : scriptables->loot)
        {
            for (const float value : point.origin)
                zb.writeT(value);
            for (const float value : point.angles)
                zb.writeT(value);
            zb.writeT<uint16_t>(point.cache ? 0xFFFBu : 0xFFFDu);
            zb.writeT<uint16_t>(scriptables->lootZoneName.empty() ? 0 : 1);
        }
        if (!scriptables->lootZoneName.empty())
'''),
 ('''    const std::vector<MapLootPoint> *lootPoints = nullptr, const std::string &lootZoneName = {})
{''', '''    const std::vector<MapLootPoint> *lootPoints = nullptr, const std::string &lootZoneName = {},
    const std::vector<MapLootBox> *lootClusters = nullptr, const std::vector<MapLootBox> *lootZones = nullptr)
{'''),
 ('''            result.scriptables->lootZoneString = result.spawns->intern(lootZoneName);
        }
''', '''            result.scriptables->lootZoneString = result.spawns->intern(lootZoneName);
        }
        if (lootClusters)
            result.scriptables->lootClusters = *lootClusters;
        if (lootZones)
        {
            result.scriptables->lootZones = *lootZones;
            for (const auto &zone : *lootZones)
                result.scriptables->lootZoneStrings.push_back(result.spawns->intern(zone.name));
        }
        if (result.scriptables->lootClusters.size() >= 0xFFFB || result.scriptables->lootZones.size() >= 0xFFFF)
            throw std::runtime_error("too many loot clusters or zones");
'''),
])

patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    std::string lootZone;
''', '''    std::string lootZone;
    // Authored loot clusters and zones (collision JSON "loot_boxes": {"clusters": [{mid, half}], "zones": [{name,
    // mid, half}]}).
    std::vector<iw8::MapLootBox> lootClusters, lootZones;
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , lootZone(std::move(other.lootZone))
''', '''    , lootZone(std::move(other.lootZone))
    , lootClusters(std::move(other.lootClusters))
    , lootZones(std::move(other.lootZones))
'''),
 ('''        lootZone = std::move(other.lootZone);
''', '''        lootZone = std::move(other.lootZone);
        lootClusters = std::move(other.lootClusters);
        lootZones = std::move(other.lootZones);
'''),
 ('''    if (!result.lootPoints.empty())
        zt::info("iw3: %zu loot points", result.lootPoints.size());
''', '''    if (collision.contains("loot_boxes"))
    {
        const auto &boxes = collision.at("loot_boxes");
        if (!boxes.is_object())
            throw std::runtime_error("loot_boxes must be an object with \\"clusters\\" and \\"zones\\" lists");
        const auto read = [&](const char *key, std::vector<iw8::MapLootBox> &out, const bool named) {
            if (!boxes.contains(key))
                return;
            const auto &list = boxes.at(key);
            if (!list.is_array())
                throw std::runtime_error(std::string("loot_boxes.") + key + " is not a list");
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const auto &entry = list[index];
                const auto where = std::string("loot_boxes.") + key + "[" + std::to_string(index) + "]";
                iw8::MapLootBox box;
                for (const char *field : {"mid", "half"})
                {
                    if (!entry.is_object() || !entry.contains(field) || !entry.at(field).is_array() || entry.at(field).size() != 3)
                        throw std::runtime_error(where + ": " + field + " is not three numbers");
                    auto &v = std::string(field) == "mid" ? box.mid : box.half;
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        if (!entry.at(field)[axis].is_number())
                            throw std::runtime_error(where + ": " + field + " is not three numbers");
                        v[axis] = entry.at(field)[axis].get<float>();
                        if (!std::isfinite(v[axis]) || (std::string(field) == "half" && v[axis] <= 0.0f))
                            throw std::runtime_error(where + ": " + field + " is not finite (half: above 0)");
                    }
                }
                if (named)
                {
                    box.name = entry.value("name", std::string());
                    if (box.name.empty())
                        throw std::runtime_error(where + ": no name");
                    for (const char c : box.name)
                        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
                            throw std::runtime_error(where + ": '" + box.name + "' is not a lower-case name");
                }
                out.push_back(std::move(box));
            }
        };
        read("clusters", result.lootClusters, false);
        read("zones", result.lootZones, true);
    }
    if (!result.lootPoints.empty())
        zt::info("iw3: %zu loot points, %zu loot clusters, %zu loot zones", result.lootPoints.size(), result.lootClusters.size(),
                 result.lootZones.size());
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''            prepared ? &prepared->lootPoints : nullptr, prepared ? prepared->lootZone : std::string());''',
  '''            prepared ? &prepared->lootPoints : nullptr, prepared ? prepared->lootZone : std::string(),
            prepared ? &prepared->lootClusters : nullptr, prepared ? &prepared->lootZones : nullptr);'''),
])
print('ok')
