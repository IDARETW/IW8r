// IW8 Radiant - IW3-style brush contents and surface types (see iw8_contents.h).
#include "iw8_contents.h"

#include <cctype>
#include <cstddef>
#include <vector>

namespace iw8r {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Last path component of a material, lower case: "tm2co/clip" -> "clip".
std::string materialKey(const std::string& material) {
    const size_t p = material.find_last_of("/\\");
    return lower(p == std::string::npos ? material : material.substr(p + 1));
}

// Alphanumeric tokens of a lower-case key: "me_curb_stone_01" -> {me, curb, stone, 01}.
std::vector<std::string> nameTokens(const std::string& key) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : key) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------- face classes

struct ToolRule {
    const char* name;
    uint32_t contents;
    bool collides;
    bool rendered;
    const char* why;
};

// Order does not matter (exact match on the last path component).
// All bits verified directly in Replay 1.20 PhysicsScript_CreateContents (0x1410A5850).
const ToolRule kToolRules[] = {
    {"clip", kIw3ContentsSolid, true, false, "solid clip (physicscontents_solid 0x1)"},
    {"clip_player", kIw3ContentsPlayerClip, true, false, "player clip (physicscontents_playerclip 0x10000)"},
    {"clip_weapon", kIw3ContentsClipShot, true, false, "weapon/bullet clip (physicscontents_clipshot 0x2000)"},
    {"clip_item", kIw3ContentsItemClip, true, false, "item clip (physicscontents_itemclip 0x400)"},
    {"clip_nosight", kIw3ContentsAiNoSight, true, false, "AI no-sight (physicscontents_ainosight 0x1000)"},
    {"clip_playernosight", kIw3ContentsPlayerNoSight, true, false, "player no-sight (physicscontents_playernosight 0x8000)"},
    {"clip_missile", kIw3ContentsMissileClip, true, false, "missile clip (physicscontents_missileclip 0x80)"},
    {"clip_vehicle", kIw3ContentsVehicleClip, true, false, "vehicle clip (physicscontents_vehicleclip 0x200)"},
    {"clip_ai", kIw3ContentsAiClip, true, false, "AI clip (physicscontents_aiclip 0x20000)"},
    {"clip_explosion", kIw3ContentsExplosionClip, true, false, "explosion clip (physicscontents_explosionclip 0x100000)"},
    {"clip_sound", kIw3ContentsSoundClip, true, false, "sound clip (physicscontents_soundclip 0x200000)"},
    {"clip_use", kIw3ContentsUseClip, true, false, "use clip (physicscontents_useclip 0x20000000)"},
    {"clip_mantle", kIw3ContentsMantle, true, false, "mantle clip (physicscontents_mantle 0x1000000)"},
    {"clip_nav", 0, false, false, "navigation blocker: no physics collision, emits NavMeshData obstacleBounds"},
    {"nav_blocker", 0, false, false, "navigation blocker: no physics collision, emits NavMeshData obstacleBounds"},
    {"ladder", kIw3ContentsSolid, true, false, "solid; climbable faces come from ladder_planes (doc 15 §2.3)"},
    {"water", kIw3ContentsWater, true, true, "water volume (physicscontents_water 0x20), drawn with water shading"},
    {"nodraw", kIw3ContentsSolid, true, false, "solid, not drawn"},
    {"caulk", kIw3ContentsSolid, true, false, "solid, not drawn"},
    {"trigger", kIw3TriggerContents, false, false, "trigger: no world collision; trigger hull contents"},
    {"sky", kIw3ContentsSolid, true, false,
     "sky: solid collision, not drawn; the 0x800 sky bit is dropped by ConvertContents"},
    {"portal", 0, false, false, "portal: no collision, not drawn"},
    {"hint", 0, false, false, "hint: no collision, not drawn"},
    {"skip", 0, false, false, "skip: no collision, not drawn"},
    {"origin", 0, false, false, "origin: no collision, not drawn"},
};

const ToolRule* findTool(const std::string& key) {
    for (const ToolRule& r : kToolRules)
        if (key == r.name) return &r;
    return nullptr;
}

// Contents line of a brush body: "contents detail;" -> returns the lower-case word, empty when none.
std::string contentsWord(const TextLine& line) {
    const std::string t = trim(line.text);
    const std::string kw = "contents";
    if (t.size() <= kw.size() || lower(t.substr(0, kw.size())) != kw) return {};
    if (!std::isspace(static_cast<unsigned char>(t[kw.size()]))) return {};
    std::string rest = trim(t.substr(kw.size()));
    if (!rest.empty() && rest.back() == ';') rest.pop_back();
    return lower(trim(rest));
}

// ---------------------------------------------------------------- surface types

// Keyword table in priority order: the first entry with any matching name token wins.
// Types are the IW3 1-based surface type numbers (bit = type - 1, x64-zt:6956-6983).
struct SurfaceRule {
    int type;
    const char* name;
    std::vector<std::string> keys;
};

const std::vector<SurfaceRule>& surfaceRules() {
    static const std::vector<SurfaceRule> rules = {
        {22, "asphalt", {"asphalt", "pavement", "tarmac", "road", "blacktop"}},
        {23, "ceramic", {"ceramic", "tile", "porcelain"}},
        {24, "plastic", {"plastic", "vinyl"}},
        {25, "rubber", {"rubber", "tire", "tyre"}},
        {26, "cushion", {"cushion", "pillow", "couch", "sofa", "upholstery", "mattress", "leather"}},
        {27, "fruit", {"fruit"}},
        {1, "bark", {"bark"}},
        {2, "brick", {"brick", "bricks"}},
        {3, "carpet", {"carpet", "rug"}},
        {4, "cloth", {"cloth", "fabric", "canvas", "curtain", "tarp"}},
        {5, "concrete", {"concrete", "cement", "cinderblock", "cinder"}},
        {6, "dirt", {"dirt", "soil", "earth"}},
        {7, "flesh", {"flesh", "skin"}},
        {8, "foliage", {"foliage", "leaf", "leaves", "bush", "hedge", "plant", "shrub"}},
        {9, "glass", {"glass"}},
        {10, "grass", {"grass", "lawn", "turf"}},
        {11, "gravel", {"gravel", "pebble"}},
        {12, "ice", {"ice", "frost"}},
        {13, "metal", {"metal", "steel", "iron", "chrome", "tin"}},
        {14, "mud", {"mud", "mire"}},
        {15, "paper", {"paper", "cardboard"}},
        {16, "plaster", {"plaster", "drywall", "stucco"}},
        {17, "rock", {"rock", "rocks", "stone", "boulder", "cliff", "curb"}},
        {18, "sand", {"sand"}},
        {19, "snow", {"snow"}},
        {20, "water", {"water"}},
        {21, "wood", {"wood", "wooden", "timber", "plank", "planks", "log", "plywood", "lumber", "pallet"}},
    };
    return rules;
}

}  // namespace

bool brushIsDetail(const Primitive& brush) {
    for (const TextLine& l : brush.lines)
        if (contentsWord(l) == "detail") return true;
    return false;
}

BrushContents brushContents(const Primitive& brush) {
    BrushContents out;
    if (brush.kind != PrimKind::Brush) {
        out.contents = 0;
        out.collides = false;
        out.rendered = false;
        out.reason = "not a brush (patch): no brush contents";
        return out;
    }

    uint32_t collideBits = 0, allBits = 0;
    bool anyCollide = false, anyDrawn = false;
    std::vector<std::string> notes;
    for (const BrushFace& f : brush.faces) {
        const std::string key = materialKey(f.material());
        const ToolRule* r = findTool(key);
        if (!r) {  // structural face
            collideBits |= kIw3ContentsSolid;
            allBits |= kIw3ContentsSolid;
            anyCollide = true;
            anyDrawn = true;
            continue;
        }
        allBits |= r->contents;
        if (key == "ladder") out.ladder = true;
        if (key == "water") out.water = true;
        if (key == "clip_nav" || key == "nav_blocker") out.navBlocker = true;
        if (r->collides) {
            collideBits |= r->contents;
            anyCollide = true;
        }
        if (r->rendered) anyDrawn = true;
        notes.push_back(key + ": " + r->why);
    }
    if (brush.faces.empty()) {
        out.reason = "brush without faces";
        out.collides = false;
        out.contents = 0;
        out.rendered = false;
        return out;
    }

    out.collides = anyCollide;
    out.contents = anyCollide ? collideBits : allBits;
    out.rendered = anyDrawn;

    // Directives: `contents X;` lines inside the brush body.
    for (const TextLine& l : brush.lines) {
        const std::string word = contentsWord(l);
        if (word.empty()) continue;
        if (word == "detail") {
            notes.push_back("contents detail: collides and is drawn, does not seal a leak");
        } else if (word == "noncolliding") {
            out.collides = false;
            out.contents = 0;
            notes.push_back("contents nonColliding: no world collision");
        } else if (word == "weaponclip" || word == "clipshot") {
            out.contents |= kIw3ContentsClipShot;
            out.collides = true;
            notes.push_back("contents weaponClip: physicscontents_clipshot 0x2000");
        } else if (word == "missileclip") {
            out.contents |= kIw3ContentsMissileClip;
            out.collides = true;
            notes.push_back("contents missileClip: physicscontents_missileclip 0x80");
        } else if (word == "vehicleclip") {
            out.contents |= kIw3ContentsVehicleClip;
            out.collides = true;
            notes.push_back("contents vehicleClip: physicscontents_vehicleclip 0x200");
        } else if (word == "itemclip") {
            out.contents |= kIw3ContentsItemClip;
            out.collides = true;
            notes.push_back("contents itemClip: physicscontents_itemclip 0x400");
        } else if (word == "playerclip") {
            out.contents |= kIw3ContentsPlayerClip;
            out.collides = true;
            notes.push_back("contents playerClip: physicscontents_playerclip 0x10000");
        } else if (word == "aiclip") {
            out.contents |= kIw3ContentsAiClip;
            out.collides = true;
            notes.push_back("contents aiClip: physicscontents_aiclip 0x20000");
        } else if (word == "ainosight") {
            out.contents |= kIw3ContentsAiNoSight;
            out.collides = true;
            notes.push_back("contents aiNoSight: physicscontents_ainosight 0x1000");
        } else if (word == "playernosight") {
            out.contents |= kIw3ContentsPlayerNoSight;
            out.collides = true;
            notes.push_back("contents playerNoSight: physicscontents_playernosight 0x8000");
        } else if (word == "explosionclip") {
            out.contents |= kIw3ContentsExplosionClip;
            out.collides = true;
            notes.push_back("contents explosionClip: physicscontents_explosionclip 0x100000");
        } else if (word == "soundclip") {
            out.contents |= kIw3ContentsSoundClip;
            out.collides = true;
            notes.push_back("contents soundClip: physicscontents_soundclip 0x200000");
        } else if (word == "soundreflect") {
            out.contents |= kIw3ContentsSoundReflect;
            out.collides = true;
            notes.push_back("contents soundReflect: physicscontents_soundreflect 0x4000000");
        } else if (word == "mantle") {
            out.contents |= kIw3ContentsMantle;
            out.collides = true;
            notes.push_back("contents mantle: physicscontents_mantle 0x1000000");
        } else if (word == "useclip") {
            out.contents |= kIw3ContentsUseClip;
            out.collides = true;
            notes.push_back("contents useClip: physicscontents_useclip 0x20000000");
        } else if (word == "water") {
            out.contents = (out.contents & ~kIw3ContentsSolid) | kIw3ContentsWater;
            out.collides = true;
            out.water = true;
            notes.push_back("contents water: physicscontents_water 0x20");
        } else if (word == "foliage") {
            out.contents = (out.contents & ~kIw3ContentsSolid) | kIw3ContentsFoliage;
            out.collides = true;
            notes.push_back("contents foliage: physicscontents_foliage 0x2");
        } else if (word == "navblocker" || word == "clip_nav") {
            out.navBlocker = true;
            out.collides = false;
            out.contents = 0;
            notes.push_back("contents navBlocker: navigation obstacle volume");
        } else {
            notes.push_back("contents " + word + ": unknown directive, ignored");
        }
    }

    if (notes.empty()) notes.push_back("structural: solid, drawn");
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i) out.reason += "; ";
        out.reason += notes[i];
    }
    return out;
}

int iw3SurfaceTypeForMaterial(const std::string& material) {
    const std::string key = materialKey(material);
    if (key.empty()) return 0;
    const std::vector<std::string> tok = nameTokens(key);

    // "painted metal" is type 28 and must win over the plain metal keyword (x64-zt:6983).
    bool painted = false;
    for (size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] == "paintedmetal") painted = true;
        if (tok[i] == "painted" && i + 1 < tok.size() && tok[i + 1] == "metal") painted = true;
    }
    if (painted) return 28;

    // The last material word in the name wins: stock names go from the general to the specific
    // (ee_food_fruit_cardboard_box_01 is cardboard, not fruit; container_barrel_plastic_01 is plastic). Owner playtest
    // 2026-11-11: a cardboard box took the fruit surface because "fruit" came first in the rule list.
    for (size_t i = tok.size(); i-- > 0;)
        for (const SurfaceRule& r : surfaceRules())
            for (const std::string& k : r.keys)
                if (tok[i] == k) return r.type;
    return 0;
}

uint32_t iw3SurfaceFlagsForType(int type) {
    if (type < 1 || type > 28) return 0;
    return static_cast<uint32_t>(type) << 20;
}

uint32_t iw3SurfaceTypeBits(int type) {
    if (type < 1 || type > 28) return 0;
    return 1u << (type - 1);
}

int replaySurfaceTypeForIw3(int type) {
    // x64-zt:6956-6983 (NativeSurfaceMaterial), indexed by IW3 type = bit + 1.
    switch (type) {
    case 1: return 1;    // bark
    case 2: return 2;    // brick
    case 3: return 3;    // carpet
    case 4: return 4;    // cloth
    case 5: return 5;    // concrete
    case 6: return 6;    // dirt
    case 7: return 7;    // flesh
    case 8: return 8;    // foliage
    case 9: return 9;    // glass
    case 10: return 10;  // grass
    case 11: return 11;  // gravel
    case 12: return 12;  // ice
    case 13: return 13;  // metal
    case 14: return 15;  // mud
    case 15: return 16;  // paper
    case 16: return 17;  // plaster
    case 17: return 18;  // rock
    case 18: return 19;  // sand
    case 19: return 20;  // snow
    case 20: return 21;  // water
    case 21: return 22;  // wood
    case 22: return 23;  // asphalt
    case 23: return 24;  // ceramic
    case 24: return 25;  // plastic
    case 25: return 26;  // rubber
    case 26: return 35;  // cushion
    case 27: return 27;  // fruit
    case 28: return 28;  // painted metal
    default: return 0;
    }
}

int iw3SurfaceTypeForStockSurfaceFlags(long long surfaceFlags) {
    if (surfaceFlags < 0) return 0;
    const int replay = int((surfaceFlags >> 19) & 0x3f);
    if (replay <= 0) return 0;
    for (int t = 1; t <= 28; ++t)
        if (replaySurfaceTypeForIw3(t) == replay) return t;
    return 0;
}

const char* iw3SurfaceTypeName(int type) {
    static const char* const kNames[] = {
        "default", "bark", "brick", "carpet", "cloth", "concrete", "dirt", "flesh", "foliage", "glass",
        "grass", "gravel", "ice", "metal", "mud", "paper", "plaster", "rock", "sand", "snow", "water",
        "wood", "asphalt", "ceramic", "plastic", "rubber", "cushion", "fruit", "paintedmetal"};
    if (type < 0 || type > 28) return "";
    return kNames[type];
}

uint32_t iw3ConvertContents(uint32_t source) {
    // x64-zt:6933-6945.
    uint32_t result = source & kIw3ConvertMask;
    if (source & kIw3ContentsGlass) result |= kIw3ContentsSolid;
    if (source & 0x20u) result |= 0x20u;  // water volumes (fork 0639335b): Replay physicscontents_water
    if (!(result & kIw3ContentsSolid) && (result & kIw3ContentsPlayerClip)) result &= 0x30202u;
    return result;
}

SurfaceFeedback surfaceFeedback(int t) {
    // Index = IW3 surface type (iw3SurfaceTypeName order). Families from the converter's impact effect table; a type
    // with no family of its own uses the closest one (brick -> concrete, carpet -> cloth, ceramic -> tile, ...).
    static const SurfaceFeedback kTable[] = {
        {"generic", ""},                          // 0 default
        {"bark", "Step_scrape_wood"},             // 1 bark
        {"concrete", "Step_scrape_concrete"},     // 2 brick
        {"cloth", ""},                            // 3 carpet
        {"cloth", ""},                            // 4 cloth
        {"concrete", "Step_scrape_concrete"},     // 5 concrete
        {"dirt", "Step_scrape_dirt"},             // 6 dirt
        {"flesh", ""},                            // 7 flesh
        {"foliage", "Step_scrape_grass"},         // 8 foliage
        {"glass", "Step_scrape_glass"},           // 9 glass
        {"grass", "Step_scrape_grass"},           // 10 grass
        {"gravel", "Step_scrape_dirt"},           // 11 gravel
        {"ice", ""},                              // 12 ice
        {"metal", "Step_scrape_metal"},           // 13 metal
        {"mud", "Step_scrape_dirt"},              // 14 mud
        {"paper", ""},                            // 15 paper
        {"plaster", "Step_scrape_concrete"},      // 16 plaster
        {"rock", "Step_scrape_concrete"},         // 17 rock
        {"sand", "Step_scrape_dirt"},             // 18 sand
        {"snow", ""},                             // 19 snow
        {"water", ""},                            // 20 water
        {"wood_solid", "Step_scrape_wood"},       // 21 wood
        {"asphalt", "Step_scrape_concrete"},      // 22 asphalt
        {"tile", "Step_scrape_concrete"},         // 23 ceramic
        {"plastic", ""},                          // 24 plastic
        {"rubber", ""},                           // 25 rubber
        {"cushion", ""},                          // 26 cushion
        {"generic", ""},                          // 27 fruit
        {"metal", "Step_scrape_paintedmetal"},    // 28 paintedmetal
    };
    if (t < 0 || t >= int(sizeof(kTable) / sizeof(kTable[0]))) return {"", ""};
    return kTable[t];
}

}  // namespace iw8r
