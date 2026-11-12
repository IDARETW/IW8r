#include "audio_zones.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

#include "brush_ops.h"

namespace iw8r {
namespace {

struct OcclusionRow {
    const char* channel;
    double cutoff[4];
    double attn[4];
};
struct FilterRow {
    const char* channel;
    int band, type;
    double freq, gain, q;
};
#include "audio_presets.inc"

std::string lowerCopy(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c | 0x20);
    return s;
}

std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

std::string vec(const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; }

bool validZoneName(const std::string& n) {
    if (n.empty() || n.size() > 60) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// "<room> [dry wet fade]" into the zone; false (and a warning) for an unknown room type.
bool readReverb(const std::string& value, AudioZone& z, const std::string& where, std::vector<std::string>* warn) {
    std::istringstream in(value);
    std::string room;
    in >> room;
    room = lowerCopy(room);
    if (room.empty() || room == "none") return true;
    const ReverbRoom* r = findReverbRoom(room);
    if (!r) {
        if (warn) warn->push_back(where + ": reverb room type '" + room + "' is not one of the stock room types; no reverb");
        return false;
    }
    z.roomType = r->roomType;
    z.dry = r->dry, z.wet = r->wet, z.fade = r->fade;
    double d, w, f;
    if (in >> d >> w >> f) {
        z.dry = std::clamp(d, 0.0, 1.0);
        z.wet = std::clamp(w, 0.0, 1.0);
        z.fade = std::clamp(f, 0.0, 30.0);
    }
    return true;
}

std::string reverbNameOf(const AudioZone& z) { return z.name + "_reverb"; }

}  // namespace

uint32_t soundHashName(const std::string& name) {
    uint32_t hash = 5381;
    for (char c : name) {
        const char lower = (c >= 'A' && c <= 'Z') ? char(c | 0x20) : c;
        hash = 65599u * hash + uint32_t(int32_t(static_cast<signed char>(lower)));
    }
    return hash ? hash : 1;
}

const std::vector<ReverbRoom>& reverbRooms() {
    // The room types of the stock bank mp_shipment.all (ACTS export, 2026-11-11) with one stock dry / wet / fade each.
    static const std::vector<ReverbRoom> kRooms = {
    {"deathsdoor_verb", 0.5, 1, 2},
    {"generic", 0.9, 0.1, 1},
    {"iw8_bus_interior", 1, 0.75, 0.66},
    {"iw8_ext_courtyard_village", 1, 0.6, 0.66},
    {"iw8_ext_lg_valley", 0.8, 0.3, 2},
    {"iw8_ext_open_space_lg_1", 1, 0.6, 0.66},
    {"iw8_ext_open_space_lg_2", 1, 0.6, 0.66},
    {"iw8_ext_open_space_lg_3", 1, 0.6, 0.66},
    {"iw8_ext_open_space_lg_4", 1, 0.6, 0.66},
    {"iw8_ext_open_space_med_1", 1, 0.6, 0.66},
    {"iw8_ext_open_space_med_2", 1, 0.6, 0.66},
    {"iw8_ext_open_space_med_3", 1, 0.6, 0.66},
    {"iw8_ext_open_space_med_4", 1, 0.6, 0.66},
    {"iw8_ext_rocky_forest", 1, 0.6, 0.66},
    {"iw8_ext_street_village", 1, 0.6, 0.66},
    {"iw8_lg_bank_vault", 1, 0.75, 0.66},
    {"iw8_lg_partcover", 1, 0.75, 0.66},
    {"iw8_lg_room", 1, 0.75, 0.66},
    {"iw8_lg_room_dark", 1, 0.75, 0.66},
    {"iw8_lg_room_tile", 1, 0.75, 0.66},
    {"iw8_lg_room_warehouse", 1, 0.75, 0.66},
    {"iw8_lg_room_wood", 1, 0.75, 0.66},
    {"iw8_lg_swimming_pool_room", 1, 0.7, 1},
    {"iw8_lg_tunnel", 1, 0.75, 0.66},
    {"iw8_md_cave", 1, 0.6, 0.66},
    {"iw8_md_ext_alley", 1, 0.6, 0.66},
    {"iw8_md_ext_alley_2", 1, 0.6, 0.66},
    {"iw8_md_hallway", 1, 0.75, 0.66},
    {"iw8_md_hallway_wood", 1, 0.75, 0.66},
    {"iw8_md_partcover", 1, 0.75, 0.66},
    {"iw8_md_room", 1, 0.75, 0.66},
    {"iw8_md_room_dark", 1, 0.75, 0.66},
    {"iw8_md_room_wood", 1, 0.75, 0.66},
    {"iw8_no_wet_all_dry", 0.9, 0.4, 6},
    {"iw8_sm_bathroom", 1, 0.75, 0.66},
    {"iw8_sm_bathroom_dark", 1, 0.75, 0.66},
    {"iw8_sm_container", 1, 0.75, 0.66},
    {"iw8_sm_container_metal", 1, 0.75, 0.66},
    {"iw8_sm_ext_alley", 1, 0.75, 0.66},
    {"iw8_sm_hallway", 1, 0.75, 0.66},
    {"iw8_sm_hallway_dark", 1, 0.75, 0.66},
    {"iw8_sm_hallway_wood", 1, 0.75, 0.66},
    {"iw8_sm_partcover", 1, 0.75, 0.66},
    {"iw8_sm_pipe", 1, 0.75, 0.66},
    {"iw8_sm_room", 1, 0.75, 0.66},
    {"iw8_sm_room_dark", 1, 0.75, 0.66},
    {"iw8_sm_room_wood", 1, 0.75, 0.66},
    {"iw8_stairwell", 1, 0.75, 0.66},
    {"iw8_stairwell_concrete", 1, 0.75, 0.66},
    {"iw8_th_dining_room", 1, 0.75, 0.66},
    {"iw8_th_ext_alley", 1, 0.3, 0.66},
    {"iw8_th_kitchen", 1, 0.75, 0.66},
    {"iw8_th_stairwell", 1, 0.75, 0.66},
    {"iw8_th_upper_rooms", 1, 0.75, 0.66},
    {"iw8_th_urban_street", 1, 0.3, 0.66},
    {"iw8_urban_ext", 1, 0.6, 0.66},
    {"iw8_urban_street", 1, 0.6, 0.66},
    {"iw8_xlg_room_tile", 1, 0.75, 0.66},
    {"underwater", 0.7, 0.5, 0.66},
    };
    return kRooms;
}

const ReverbRoom* findReverbRoom(const std::string& roomType) {
    const std::string want = lowerCopy(roomType);
    for (const ReverbRoom& r : reverbRooms())
        if (want == r.roomType) return &r;
    return nullptr;
}

AudioSetup readAudioSetup(const MapFile& map, const std::string& mapId, const AABB& worldBounds, std::vector<std::string>* warn,
                          const std::function<bool(const std::string&)>& isCustomSound) {
    const auto ambientOf = [&](const std::string& value, AudioZone& z, const std::string& where) {
        const std::string name = lowerCopy(value);
        if (name.empty()) return;
        if (isCustomSound && isCustomSound(name)) {
            z.ambient = name;
            return;
        }
        if (warn)
            warn->push_back(where + ": iw8r_ambient '" + name + "' is not one of the map's own sounds (sounds folder); a stock alias "
                            "would be silent in the map bank, so it is not built (a looping sound_emitter plays stock ambience)");
    };
    const auto oneShotsOf = [&](const std::string& value, AudioZone& z, const std::string& where) {
        std::istringstream in(value);
        double lo = 0, hi = 0;
        if (!(in >> lo >> hi) || lo < 0 || hi < lo) {
            if (warn) warn->push_back(where + ": iw8r_ambient_oneshots needs \"<min s> <max s> <sound>[:weight] ...\"; skipped");
            return;
        }
        z.oneShotMin = lo, z.oneShotMax = hi;
        std::string tok;
        while (in >> tok) {
            const size_t colon = tok.find(':');
            const std::string name = lowerCopy(tok.substr(0, colon));
            const double weight = colon == std::string::npos ? 1.0 : std::max(0.0, std::atof(tok.c_str() + colon + 1));
            if (isCustomSound && isCustomSound(name))
                z.oneShots.push_back({name, weight});
            else if (warn)
                warn->push_back(where + ": ambient one-shot '" + name + "' is not one of the map's own sounds; left out");
        }
    };
    AudioSetup s;
    s.mapId = lowerCopy(mapId);
    s.mapZone.name = s.mapId;
    s.mapZone.priority = 0;
    s.mapZone.bounds = worldBounds;
    std::set<std::string> names{s.mapId};
    for (const Entity& e : map.entities) {
        if (e.isWorldspawn()) {
            if (const std::string* v = e.value("iw8r_reverb")) readReverb(*v, s.mapZone, "worldspawn", warn);
            if (const std::string* v = e.value("iw8r_audio_interior")) s.mapZone.interior = std::atoi(v->c_str()) != 0;
            if (const std::string* v = e.value("iw8r_ambient")) ambientOf(*v, s.mapZone, "worldspawn");
            if (const std::string* v = e.value("iw8r_ambient_oneshots")) oneShotsOf(*v, s.mapZone, "worldspawn");
            continue;
        }
        const std::string* zn = e.value("iw8r_audio_zone");
        if (!zn || zn->empty()) continue;
        AudioZone z;
        z.name = lowerCopy(*zn);
        const std::string where = e.classname() + " '" + z.name + "'";
        if (lowerCopy(e.classname()).rfind("trigger_", 0) != 0) {
            if (warn) warn->push_back(where + ": iw8r_audio_zone only works on a trigger brush; skipped");
            continue;
        }
        if (!validZoneName(z.name)) {
            if (warn) warn->push_back(where + ": a zone name is lower-case letters, digits and _; skipped");
            continue;
        }
        if (!names.insert(z.name).second) {
            if (warn) warn->push_back(where + ": the zone name is used twice (or is the map's name); skipped");
            continue;
        }
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) z.bounds.add(brushBounds(brushFromPrimitive(*p)));
        if (!z.bounds.valid()) {
            if (warn) warn->push_back(where + ": the trigger has no brush; skipped");
            continue;
        }
        if (const std::string* v = e.value("iw8r_reverb")) readReverb(*v, z, where, warn);
        if (const std::string* v = e.value("iw8r_ambient")) ambientOf(*v, z, where);
        if (const std::string* v = e.value("iw8r_ambient_oneshots")) oneShotsOf(*v, z, where);
        if (const std::string* v = e.value("iw8r_audio_priority")) z.priority = std::clamp(std::atof(v->c_str()), 1.0, 1000.0);
        if (const std::string* v = e.value("iw8r_audio_interior")) z.interior = std::atoi(v->c_str()) != 0;
        s.zones.push_back(z);
    }
    return s;
}

std::string soundTableJson(const AudioSetup& s) {
    std::vector<const AudioZone*> all{&s.mapZone};
    for (const AudioZone& z : s.zones) all.push_back(&z);
    std::string reverbs, zones;
    int reverbCount = 0;
    // The presets the zones use, each written once: occlusion filters and filters with the stock values.
    bool useInterior = false;
    for (const AudioZone* z : all) useInterior |= z->interior;
    std::string occ, fil;
    int occCount = 0, filCount = 0;
    struct Range {
        int occStart, occNum, filStart, filNum;
    };
    const auto addPreset = [&](const char* preset, const OcclusionRow* o, size_t on, const FilterRow* f, size_t fn) {
        Range r{occCount, int(on), filCount, int(fn)};
        for (size_t i = 0; i < on; ++i, ++occCount)
            occ += std::string(occCount ? "," : "") + "{\"id\":" + std::to_string(soundHashName(std::string(preset) + o[i].channel)) +
                   ",\"entChannel\":" + quote(o[i].channel) + ",\"cutoffLevel1\":" + num(o[i].cutoff[0]) +
                   ",\"cutoffLevel2\":" + num(o[i].cutoff[1]) + ",\"cutoffLevel3\":" + num(o[i].cutoff[2]) +
                   ",\"cutoffLevel4\":" + num(o[i].cutoff[3]) + ",\"attnLevel1\":" + num(o[i].attn[0]) +
                   ",\"attnLevel2\":" + num(o[i].attn[1]) + ",\"attnLevel3\":" + num(o[i].attn[2]) +
                   ",\"attnLevel4\":" + num(o[i].attn[3]) + "}";
        for (size_t i = 0; i < fn; ++i, ++filCount)
            fil += std::string(filCount ? "," : "") + "{\"id\":" +
                   std::to_string(soundHashName(std::string(preset) + f[i].channel + std::to_string(f[i].band))) +
                   ",\"entChannel\":" + quote(f[i].channel) + ",\"band\":" + std::to_string(f[i].band) +
                   ",\"type\":" + std::to_string(f[i].type) + ",\"freq\":" + num(f[i].freq) + ",\"gain\":" + num(f[i].gain) +
                   ",\"q\":" + num(f[i].q) + "}";
        return r;
    };
    // Ambient one-shot definitions, one per zone that has sounds.
    std::string defs, events, elements;
    int defCount = 0, eventCount = 0, elementCount = 0;
    std::vector<int> defIndex(all.size(), -1);
    std::vector<std::string> elementNames;
    for (size_t i = 0; i < all.size(); ++i) {
        const AudioZone& z = *all[i];
        if (z.oneShots.empty()) continue;
        const std::string defName = z.name + "_elements";
        const int firstEvent = eventCount;
        double total = 0;
        for (const auto& [n, w] : z.oneShots) total += w;
        for (const auto& [n, w] : z.oneShots) {
            int el = -1;
            for (size_t k = 0; k < elementNames.size(); ++k)
                if (elementNames[k] == n) el = int(k);
            if (el < 0) {
                el = elementCount++;
                elementNames.push_back(n);
                elements += std::string(el ? "," : "") + "{\"id\":" + std::to_string(soundHashName(n)) + ",\"aliasName\":" + quote(n) +
                            ",\"rangeMin\":0,\"rangeMax\":0,\"coneMin\":0,\"coneMax\":0}";
            }
            events += std::string(eventCount ? "," : "") + "{\"id\":" + std::to_string(soundHashName(defName + n)) +
                      ",\"ambientElementIndex\":" + std::to_string(el) + ",\"weight\":" + num(total > 0 ? w / total : 0) + "}";
            ++eventCount;
        }
        defIndex[i] = defCount++;
        defs += std::string(defIndex[i] ? "," : "") + "{\"name\":" + quote(defName) + ",\"id\":" + std::to_string(soundHashName(defName)) +
                ",\"ambientEventIndex\":" + std::to_string(firstEvent) + ",\"numEvents\":" + std::to_string(eventCount - firstEvent) +
                ",\"timeMin\":" + num(z.oneShotMin) + ",\"timeMax\":" + num(z.oneShotMax) + "}";
    }
    const Range rDefault = addPreset("default", kOcclusion_default, std::size(kOcclusion_default), kFilter_default, std::size(kFilter_default));
    const Range rInterior = useInterior ? addPreset("interior", kOcclusion_interior, std::size(kOcclusion_interior), kFilter_interior,
                                                    std::size(kFilter_interior))
                                        : Range{0, 0, 0, 0};
    for (size_t i = 0; i < all.size(); ++i) {
        const AudioZone& z = *all[i];
        int reverbIndex = -1;
        if (!z.roomType.empty()) {
            reverbIndex = reverbCount++;
            reverbs += std::string(reverbIndex ? "," : "") + "{\"id\":" + std::to_string(soundHashName(reverbNameOf(z))) +
                       ",\"roomTypeId\":" + std::to_string(soundHashName(z.roomType)) + ",\"roomType\":" + quote(z.roomType) +
                       ",\"dryLevel\":" + num(z.dry) + ",\"wetLevel\":" + num(z.wet) + ",\"fadeTime\":" + num(z.fade) + "}";
        }
        zones += std::string(i ? "," : "") + "{\"id\":" + std::to_string(soundHashName(z.name)) + ",\"stateId\":0,\"zoneName\":" +
                 quote(z.name) + ",\"reverbIndex\":" + std::to_string(reverbIndex) + ",\"reverbName\":" +
                 quote(z.roomType.empty() ? std::string() : reverbNameOf(z)) + ",\"ambientStream\":" + quote(z.ambient) +
                 ",\"ambientDefIndex\":" + std::to_string(defIndex[i]) + ",\"ambientName\":" +
                 quote(defIndex[i] >= 0 ? z.name + "_elements" : std::string()) + ",\"occlusionName\":" + quote(z.interior ? "interior" : "default") +
                 ",\"filterName\":" + quote(z.interior ? "interior" : "default") + ",\"startOcclusionIndex\":" +
                 std::to_string((z.interior ? rInterior : rDefault).occStart) + ",\"numOcclusion\":" +
                 std::to_string((z.interior ? rInterior : rDefault).occNum) + ",\"startFilterIndex\":" +
                 std::to_string((z.interior ? rInterior : rDefault).filStart) + ",\"numFilter\":" +
                 std::to_string((z.interior ? rInterior : rDefault).filNum) + ",\"startMixIndex\":-1,\"numMix\":0,\"startAdsrZoneIndex\":-1,\"numAdsrZone\":0,"
                 "\"startNPCAdsrZoneIndex\":-1,\"numNPCAdsrZone\":0,\"startFullOccIndex\":-1,\"numDisableFullOcc\":0,"
                 "\"startPlayerBreathStateIndex\":-1,\"numPlayerBreathStates\":0,\"exterior\":0}";
    }
    return "{\"map\":" + quote(s.mapId) + ",\"send_effects\":[],\"reverbs\":[" + reverbs + "],\"occlusion_filters\":[" + occ +
           "],\"filters\":[" + fil + "],\"zones\":[" + zones + "],\"ambient_defs\":[" + defs + "],\"ambient_events\":[" + events + "],\"ambient_elements\":[" + elements + "]}";
}

std::string clientTriggersJson(const AudioSetup& s) {
    // Stock audio triggers (mp_shipment): type 2, the box centred on the origin (hull midPoint 0, no slabs), script
    // delay 0.25, priority 0 for the map-wide zone and 30 to 50 for rooms.
    std::vector<const AudioZone*> all;
    if (s.mapZone.bounds.valid()) all.push_back(&s.mapZone);
    for (const AudioZone& z : s.zones) all.push_back(&z);
    std::string triggers;
    for (size_t i = 0; i < all.size(); ++i) {
        const AudioZone& z = *all[i];
        const Vec3 c = z.bounds.center();
        const Vec3 h = (z.bounds.maxs - z.bounds.mins) * 0.5;
        triggers += std::string(i ? "," : "") + "{\"type\":2,\"origin\":" + vec(c) + ",\"scriptDelay\":0.25,\"priority\":" +
                    num(z.priority) + ",\"audio\":" + quote(z.name) +
                    ",\"npc\":\"\",\"blend\":-1,\"audioState\":-1,\"hulls\":[{\"midPoint\":[0,0,0],\"halfSize\":" + vec(h) +
                    ",\"slabs\":[]}]}";
    }
    return "{\"map\":" + quote(s.mapId) + ",\"triggers\":[" + triggers + "],\"blend_nodes\":[],\"pa_speakers\":[]}";
}

}  // namespace iw8r
