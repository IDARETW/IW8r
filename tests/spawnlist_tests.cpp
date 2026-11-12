// spawnList write-back (milestone 2.2): read the compiled spawns of a stock map_ents dump, write them back, and check that
// untouched records stay byte-identical. The stock dump and ACTS dump are read only; the tests skip when they are absent.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "iw8_dump.h"
#include "iw8_mapents.h"
#include "map_text.h"
#include "spawnlist.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const std::string kShipmentDump =
    "E:\\IW8-Radiant\\out\\m2-x64zt\\dump\\mp_shipment\\map_ents\\maps\\mp\\mp_shipment.d3dbsp.iw8";
const std::string kActsAsset =
    "E:\\IW8-Radiant\\out\\m2-script-check\\acts_ents\\mw19replay\\mp_shipment\\assets\\map_ents\\maps\\mp\\mp_shipment.d3dbsp.0.asset.json";
const std::string kActsStrings = "E:\\IW8-Radiant\\out\\m2-script-check\\acts_ents\\mw19replay\\mp_shipment\\script_strings.json";
constexpr uint32_t kShipmentSpawns = 611;

bool loadShipment(std::string& raw, Iw8Dump& d) {
    if (!readFile(kShipmentDump, raw)) return false;
    std::string err;
    CHECK(parseIw8Dump(raw, d, &err));
    return true;
}

bool sameRecord(const Iw8DumpRecord& a, const Iw8DumpRecord& b) {
    return a.kind == b.kind && a.value == b.value && a.delta == b.delta && a.bytes == b.bytes;
}

// Index of the spawn array data record in a stock dump (the first data record of 40 * count bytes), or 0 when absent.
size_t spawnBlock(const Iw8Dump& d, size_t count) {
    for (size_t s = 1; s < d.records.size(); ++s)
        if (d.records[s].kind == Iw8DumpRecord::Data && d.records[s].bytes.size() == 40 * count) return s;
    return 0;
}

}  // namespace

// The stock mp_shipment spawnList: 611 records, the same as the ACTS decode (index, names, origins, angles).
TEST(spawnlist_stock_mp_shipment_reads_611_spawns_matching_acts) {
    std::string raw, err;
    Iw8Dump d;
    if (!loadShipment(raw, d)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", kShipmentDump.c_str());
        return;
    }
    std::vector<SpawnRecord> spawns;
    CHECK(readSpawnList(d, spawns, &err));
    CHECK(spawns.size() == kShipmentSpawns);
    if (spawns.size() != kShipmentSpawns) return;
    CHECK(spawns[0].classname == "mp_dm_spawn_start");
    CHECK(spawns[0].origin == Vec3(-904, 2016, 36));
    CHECK(std::fabs(spawns[0].angles.y - 269.9989929199219) < 1e-6);
    for (size_t i = 0; i < spawns.size(); ++i) CHECK(spawns[i].index == i);

    if (!readFile(kActsAsset, raw)) {
        test::skipf("  note: ACTS dump not found, cross-check skipped\n");
        return;
    }
    Iw8MapEntsDump acts;
    if (!loadActsMapEnts(kActsAsset, kActsStrings, acts, &err)) {
        test::skipf("  note: ACTS dump not readable (%s), cross-check skipped\n", err.c_str());
        return;
    }
    CHECK(acts.spawns.size() == spawns.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < spawns.size() && i < acts.spawns.size(); ++i) {
        const Iw8SpawnRecord& a = acts.spawns[i];
        const SpawnRecord& s = spawns[i];
        const bool same = a.index == s.index && a.name == s.classname && a.target == s.target && a.noteworthy == s.script_noteworthy &&
                          std::fabs(a.origin.x - s.origin.x) < 1e-6 && std::fabs(a.origin.y - s.origin.y) < 1e-6 &&
                          std::fabs(a.origin.z - s.origin.z) < 1e-6 && std::fabs(a.angles.x - s.angles.x) < 1e-6 &&
                          std::fabs(a.angles.y - s.angles.y) < 1e-6 && std::fabs(a.angles.z - s.angles.z) < 1e-6;
        mismatches += !same;
    }
    CHECK(mismatches == 0);
    std::printf("  mp_shipment spawnList: %zu spawns, ACTS decode %zu, %zu mismatches\n", spawns.size(), acts.spawns.size(), mismatches);
}

// Read, write back unchanged: the container is byte-identical.
TEST(spawnlist_stock_write_back_unchanged_is_byte_identical) {
    std::string raw, err;
    Iw8Dump d;
    if (!loadShipment(raw, d)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", kShipmentDump.c_str());
        return;
    }
    std::vector<SpawnRecord> spawns;
    CHECK(readSpawnList(d, spawns, &err));
    CHECK(writeSpawnList(d, spawns, &err));
    CHECK(serializeIw8Dump(d) == raw);
}

// Move one spawn's origin: exactly that spawn's origin bytes change, every other record is identical.
TEST(spawnlist_move_one_origin_changes_only_that_record) {
    std::string raw, err;
    Iw8Dump d;
    if (!loadShipment(raw, d)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", kShipmentDump.c_str());
        return;
    }
    const Iw8Dump orig = d;
    std::vector<SpawnRecord> spawns;
    CHECK(readSpawnList(d, spawns, &err));
    const size_t moved = 5;
    const double oldX = spawns[moved].origin.x;
    spawns[moved].origin.x += 16;
    CHECK(writeSpawnList(d, spawns, &err));
    const std::string out = serializeIw8Dump(d);
    CHECK(out != raw);

    Iw8Dump back;
    CHECK(parseIw8Dump(out, back, &err));
    CHECK(back.records.size() == orig.records.size());
    const size_t block = spawnBlock(orig, kShipmentSpawns);
    CHECK(block != 0);
    size_t changedRecords = 0;
    for (size_t k = 0; k < orig.records.size() && k < back.records.size(); ++k) {
        if (sameRecord(orig.records[k], back.records[k])) continue;
        ++changedRecords;
        CHECK(k == block);
    }
    CHECK(changedRecords == 1);
    // Only the 4 bytes of origin.x inside that spawn's 40-byte entry differ.
    const std::string& ob = orig.records[block].bytes;
    const std::string& nb = back.records[block].bytes;
    size_t diffs = 0, first = 0, last = 0;
    for (size_t i = 0; i < ob.size() && i < nb.size(); ++i)
        if (ob[i] != nb[i]) {
            if (!diffs) first = i;
            last = i;
            ++diffs;
        }
    CHECK(diffs > 0);
    CHECK(first >= 40 * moved + 16 && last < 40 * moved + 20);
    std::vector<SpawnRecord> reread;
    CHECK(readSpawnList(back, reread, &err));
    CHECK(reread.size() == kShipmentSpawns);
    CHECK(std::fabs(reread[moved].origin.x - (oldX + 16)) < 1e-6);
    size_t others = 0;
    for (size_t i = 0; i < reread.size(); ++i)
        if (i != moved) others += reread[i].origin == spawns[i].origin && reread[i].classname == spawns[i].classname;
    CHECK(others == kShipmentSpawns - 1);
    std::printf("  moved spawn %zu origin.x %g -> %g: %zu bytes changed inside record %zu, other records identical\n", moved, oldX,
                reread[moved].origin.x, diffs, block);
}

// Add a spawn, check that the records behind the block still resolve to the same content, then remove it: the container
// is byte-identical to the stock dump again.
TEST(spawnlist_add_then_remove_restores_bytes) {
    std::string raw, err;
    Iw8Dump d;
    if (!loadShipment(raw, d)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", kShipmentDump.c_str());
        return;
    }
    const Iw8Dump orig = d;
    const size_t block = spawnBlock(orig, kShipmentSpawns);
    CHECK(block != 0);
    if (!block) return;
    const size_t oldEnd = block + 1 + 3 * kShipmentSpawns;
    std::vector<SpawnRecord> spawns;
    CHECK(readSpawnList(d, spawns, &err));
    SpawnRecord added = spawns[0];
    added.classname = "mp_dm_spawn_test_added";
    added.origin.z += 50;
    spawns.push_back(added);
    CHECK(writeSpawnList(d, spawns, &err));
    const std::string grown = serializeIw8Dump(d);

    Iw8Dump g;
    CHECK(parseIw8Dump(grown, g, &err));
    CHECK(g.records.size() == orig.records.size() + 3);
    std::vector<SpawnRecord> reread;
    CHECK(readSpawnList(g, reread, &err));
    CHECK(reread.size() == kShipmentSpawns + 1);
    if (reread.size() == kShipmentSpawns + 1) {
        CHECK(reread.back().classname == "mp_dm_spawn_test_added");
        CHECK(reread.back().origin == added.origin);
        CHECK(reread.back().index == kShipmentSpawns);
    }
    // Every offset behind the block still points at the same content.
    size_t offsetsChecked = 0, offsetsWrong = 0;
    for (size_t k = oldEnd; k < orig.records.size(); ++k) {
        if (orig.records[k].kind != Iw8DumpRecord::Offset) continue;
        ++offsetsChecked;
        const size_t target = size_t(orig.records[k].value);
        const size_t newK = k + 3, newTarget = target >= oldEnd ? target + 3 : target;
        if (newK >= g.records.size() || newTarget >= g.records.size() || g.records[newK].kind != Iw8DumpRecord::Offset ||
            !sameRecord(orig.records[target], g.records[newTarget]) || g.records[newK].delta != orig.records[k].delta)
            ++offsetsWrong;
    }
    CHECK(offsetsChecked > 0);
    CHECK(offsetsWrong == 0);

    // Remove the added spawn again.
    spawns.pop_back();
    CHECK(writeSpawnList(g, spawns, &err));
    CHECK(serializeIw8Dump(g) == raw);
    std::printf("  added one spawn: %zu offsets behind the block re-pointed, %zu wrong; removing it restores the stock bytes\n",
                offsetsChecked, offsetsWrong);
}

// Entity text round trip (the spawns.ents format used by the CLI) writes back the same container.
TEST(spawnlist_entity_text_round_trip_is_byte_identical) {
    std::string raw, err;
    Iw8Dump d;
    if (!loadShipment(raw, d)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", kShipmentDump.c_str());
        return;
    }
    std::vector<SpawnRecord> spawns;
    CHECK(readSpawnList(d, spawns, &err));
    const std::string text = spawnsToEntityText(spawns);
    std::vector<SpawnRecord> parsed;
    CHECK(spawnsFromEntityText(text, parsed, &err));
    CHECK(parsed.size() == spawns.size());
    CHECK(writeSpawnList(d, parsed, &err));
    CHECK(serializeIw8Dump(d) == raw);
}

// Synthetic dumps: an empty spawnList cannot take new spawns (no block to insert next to), and a count with no block is an error.
TEST(spawnlist_refuses_insert_without_block_and_bad_count) {
    Iw8Dump d;
    d.type = kIw8MapEntsType;
    Iw8DumpRecord header;
    header.kind = Iw8DumpRecord::Data;
    header.bytes.assign(1032, '\0');
    d.records.push_back(header);
    std::vector<SpawnRecord> spawns;
    std::string err;
    CHECK(readSpawnList(d, spawns, &err));
    CHECK(spawns.empty());
    const std::string before = serializeIw8Dump(d);
    CHECK(writeSpawnList(d, {}, &err));
    CHECK(serializeIw8Dump(d) == before);
    SpawnRecord s;
    s.classname = "mp_dm_spawn_start";
    CHECK(!writeSpawnList(d, {s}, &err));
    CHECK(serializeIw8Dump(d) == before);

    // The count says 2 but no block follows: reading fails and nothing is written.
    const uint32_t two = 2;
    std::memcpy(&d.records[0].bytes[0x128], &two, sizeof(two));
    CHECK(!readSpawnList(d, spawns, &err));
    const std::string bad = serializeIw8Dump(d);
    CHECK(!writeSpawnList(d, {s, s}, &err));
    CHECK(serializeIw8Dump(d) == bad);

    // Not a map_ents dump.
    d.type = 3;
    CHECK(!readSpawnList(d, spawns, &err));
}
