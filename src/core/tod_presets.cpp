#include "tod_presets.h"

#include "atmosphere.h"
#include "document.h"

namespace iw8r {

const std::vector<TodPreset>& todPresets() {
    // sundirection "pitch yaw 0" (negative pitch: the sun is above the horizon, as the stock maps write it),
    // suncolor linear 0..1, iw8r_fog "start halfway r g b", iw8r_film "contrast brightness desaturation invert
    // light rgb dark rgb", iw8r_exposure_adjust in EV.
    static const std::vector<TodPreset> presets = {
        {"clear_day", "Clear Day (high sun, light haze)",
         {{"sundirection", "-55 235 0"},
          {"suncolor", "1 0.96 0.88"},
          {"skyname", "sky_mp_rust"},
          {kFogKey, "1500 9000 0.62 0.7 0.8"},
          {kFilmKey, ""},
          {"iw8r_exposure_adjust", ""}}},
        {"overcast", "Overcast (soft grey light, close fog)",
         {{"sundirection", "-70 200 0"},
          {"suncolor", "0.55 0.58 0.62"},
          {"skyname", "sky_mp_donetsk"},
          {kFogKey, "400 3000 0.55 0.58 0.62"},
          {kFilmKey, "1 0 0.15 0 1 1 1 1 1 1"},
          {"iw8r_exposure_adjust", "0.25"}}},
        {"dusk", "Dusk (low warm sun, long shadows)",
         {{"sundirection", "-12 250 0"},
          {"suncolor", "1 0.58 0.32"},
          {"skyname", "sky_mp_euphrates"},
          {kFogKey, "800 5000 0.75 0.55 0.45"},
          {kFilmKey, "1.05 0 0 0 1.05 0.95 0.85 0.95 0.9 1"},
          {"iw8r_exposure_adjust", ""}}},
        {"night", "Night (dim cool moonlight)",
         {{"sundirection", "-40 120 0"},
          {"suncolor", "0.18 0.22 0.35"},
          {"skyname", "sky_mp_emporium"},
          {kFogKey, "300 2500 0.08 0.09 0.13"},
          {kFilmKey, "1.1 -0.05 0.25 0 0.9 0.95 1.1 0.85 0.9 1.1"},
          {"iw8r_exposure_adjust", "0.75"}}},
        {"interior", "Interior (faint sun, no fog)",
         {{"sundirection", "-60 235 0"},
          {"suncolor", "0.35 0.34 0.32"},
          {"skyname", "sky_mp_shipment"},
          {kFogKey, ""},
          {kFilmKey, ""},
          {"iw8r_exposure_adjust", "0.5"}}},
    };
    return presets;
}

const TodPreset* findTodPreset(const std::string& id) {
    for (const TodPreset& p : todPresets())
        if (id == p.id) return &p;
    return nullptr;
}

bool applyTodPreset(Document& doc, const std::string& id, bool keepSky) {
    const TodPreset* p = findTodPreset(id);
    Entity* w = doc.worldspawn();
    if (!p || !w) return false;
    const Id wid = w->id;
    auto wanted = [&](const std::string& k) { return !(keepSky && k == "skyname"); };
    bool changed = false;
    for (const auto& [k, v] : p->keys) {
        if (!wanted(k)) continue;
        const std::string* cur = w->value(k);
        changed |= v.empty() ? cur != nullptr : (!cur || *cur != v);
    }
    if (!changed) return false;
    doc.beginGroup(std::string("Time of Day: ") + p->title);
    for (const auto& [k, v] : p->keys) {
        if (!wanted(k)) continue;
        const std::string* cur = doc.findEntity(wid)->value(k);
        if (v.empty()) {
            if (cur) doc.removeKey(wid, k);
        } else if (!cur || *cur != v) {
            doc.setKeyValue(wid, k, v);
        }
    }
    doc.endGroup();
    return true;
}

}  // namespace iw8r
