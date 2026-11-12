import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/compile_iw8.cpp', [
 ('''            const bool trigger = startsWith(lcls, "trigger_") && acceptedPointClass(lcls);
            if (!trigger && lcls != "script_brushmodel") {
                ++skippedBrushEntities[cls];
''', '''            const bool trigger = startsWith(lcls, "trigger_") && acceptedPointClass(lcls);
            if (isLootBoxClass(lcls)) continue;  // loot clusters and zones: boxes of the collision json (below)
            if (!trigger && lcls != "script_brushmodel") {
                ++skippedBrushEntities[cls];
'''),
 ('''                        warn.push_back("worldspawn iw8r_loot_zone '" + *z + "' is not a lower-case name; map_quarry is used");
                }
        }
    }
''', '''                        warn.push_back("worldspawn iw8r_loot_zone '" + *z + "' is not a lower-case name; map_quarry is used");
                }
        }
        // Authored loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section
        // 6d): with zones, each point takes the first box that holds it and iw8r_loot_zone only names unnamed zones.
        if (lootCount) {
            std::vector<std::string> boxNotes;
            const LootBoxEdits boxes = documentLootBoxes(map, lootZone, &boxNotes);
            for (const std::string& n : boxNotes) warn.push_back(n);
            if (!boxes.empty()) {
                lootBoxes = lootBoxesJson(boxes);
                warn.push_back(std::to_string(boxes.clusters.size()) + " loot cluster(s) and " + std::to_string(boxes.zones.size()) +
                               " loot zone(s) written (each point takes the first box that holds it; not seen in game)");
            }
        }
    }
'''),
 ('''    std::string lootJson, lootZone;
''', '''    std::string lootJson, lootZone, lootBoxes;
'''),
 ('''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "],\\"loot_zone\\":\\"" + lootZone + "\\"" : std::string()) +''',
  '''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "],\\"loot_zone\\":\\"" + lootZone + "\\"" : std::string()) +
           (lootBoxes.empty() ? std::string() : ",\\"loot_boxes\\":" + lootBoxes) +'''),
])
print('ok')
