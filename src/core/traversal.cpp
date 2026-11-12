#include "traversal.h"

#include <cmath>

#include "map_text.h"

namespace iw8r {

const std::vector<TraversalAnim>& stockTraversalAnims() {
    // The end offsets follow the names: a drop of N, a climb of N, a gap of N; a ladder and a window ledge.
    static const std::vector<TraversalAnim> anims = {
        {"jump_down_80", "Jump down 80", {48, 0, -80}},
        {"jump_down_40", "Jump down 40", {40, 0, -40}},
        {"jump_up_40", "Climb up 40", {32, 0, 40}},
        {"jump_down_136", "Jump down 136", {64, 0, -136}},
        {"jump_across_100", "Jump across a 100 gap", {100, 0, 0}},
        {"jump_up_80", "Climb up 80", {32, 0, 80}},
        {"jump_down_160", "Jump down 160", {64, 0, -160}},
        {"ladder_up", "Ladder up (128)", {16, 0, 128}},
        {"window_over_40", "Over a window ledge of 40", {48, 0, 0}},
    };
    return anims;
}

const TraversalAnim* findTraversalAnim(const std::string& name) {
    for (const TraversalAnim& a : stockTraversalAnims())
        if (name == a.name) return &a;
    return nullptr;
}

std::pair<std::vector<std::pair<std::string, std::string>>, std::vector<std::pair<std::string, std::string>>>
traversalPairKeys(const TraversalAnim& anim, const Vec3& origin, double yaw, const std::string& name) {
    const double r = yaw * 3.14159265358979323846 / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    const Vec3 end{origin.x + anim.end.x * c - anim.end.y * s, origin.y + anim.end.x * s + anim.end.y * c, origin.z + anim.end.z};
    const auto vec = [](const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); };
    const std::string angles = "0 " + formatNumber(yaw) + " 0";
    std::vector<std::pair<std::string, std::string>> begin = {{"classname", "node_negotiation_begin"},
                                                               {"origin", vec(origin)},
                                                               {"angles", angles},
                                                               {"animscript", anim.name},
                                                               {"targetname", name},
                                                               {"target", name + "_end"}};
    std::vector<std::pair<std::string, std::string>> endKeys = {
        {"classname", "node_negotiation_end"}, {"origin", vec(end)}, {"angles", angles}, {"targetname", name + "_end"}};
    return {begin, endKeys};
}

}  // namespace iw8r
