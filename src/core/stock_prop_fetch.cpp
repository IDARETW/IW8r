#include "stock_prop_fetch.h"

#include <algorithm>

namespace iw8r {

std::vector<std::string> propFetchOrder(const std::map<std::string, size_t>& missingUse) {
    std::vector<std::pair<std::string, size_t>> v(missingUse.begin(), missingUse.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    std::vector<std::string> out;
    out.reserve(v.size());
    for (auto& [name, uses] : v) out.push_back(std::move(name));
    return out;
}

void PropFetchChain::reset() { *this = PropFetchChain(); }

PropFetchStep PropFetchChain::step(size_t missing, bool cacheBusy) {
    if (missing == 0) {
        asked_ = false;
        idleNoProgress_ = 0;
        stalled_ = false;
        return PropFetchStep::Done;
    }
    if (cacheBusy) {
        // Busy with these models: it reports when a batch has ended, and the request is judged when it is idle.
        // Busy with something else (models of the map's entities): the props are queued behind that.
        if (asked_) return PropFetchStep::Wait;
        asked_ = true;
        askedMissing_ = missing;
        ++requests_;
        return PropFetchStep::Request;
    }
    if (asked_) {
        // The cache is idle again. Fewer models missing than when it was asked: it worked.
        if (missing < askedMissing_) idleNoProgress_ = 0;
        else ++idleNoProgress_;
        asked_ = false;
    }
    if (idleNoProgress_ >= 2) {
        stalled_ = true;
        return PropFetchStep::Stalled;
    }
    stalled_ = false;
    asked_ = true;
    askedMissing_ = missing;
    ++requests_;
    return PropFetchStep::Request;
}

std::string propFailureReason(const std::string& error) {
    std::string s = error;
    // "<zone>: reason": the zone differs per model.
    if (const size_t colon = s.find(": "); colon != std::string::npos && colon > 0 && s.find(' ') > colon) s.erase(0, colon + 2);
    // "a long explanation: the name or log it is about": keep the explanation.
    if (const size_t colon = s.find(": "); colon != std::string::npos) {
        const std::string head = s.substr(0, colon);
        if (std::count(head.begin(), head.end(), ' ') >= 2) s = head;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == ':')) s.pop_back();
    size_t first = 0;
    while (first < s.size() && s[first] == ' ') ++first;
    s.erase(0, first);
    return s.empty() ? std::string("no reason recorded") : s;
}

std::vector<std::pair<std::string, size_t>> propFailureReasons(const std::vector<std::string>& errors) {
    std::map<std::string, size_t> counts;
    for (const std::string& e : errors) ++counts[propFailureReason(e)];
    std::vector<std::pair<std::string, size_t>> out(counts.begin(), counts.end());
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

std::string propStatusLine(const PropFetchCounts& c, bool fetching, bool stalled, const std::string& stallReason,
                           const std::vector<std::pair<std::string, size_t>>& reasons, size_t maxReasons) {
    if (c.models == 0) return "Stock props: none in this map";
    std::string s = "Stock props: " + std::to_string(c.ready) + " of " + std::to_string(c.models) + " models ready";
    if (c.missing > 0) {
        if (stalled || !fetching) {
            s += ", " + std::to_string(c.missing) + " not fetched";
            if (!stallReason.empty()) s += ": " + stallReason;
            else if (stalled) s += ": the model cache stopped without an answer";
        } else {
            s += ", fetching " + std::to_string(c.missing) + " (a first open can take several minutes)";
        }
    }
    if (c.failed > 0) {
        s += ", " + std::to_string(c.failed) + " failed";
        if (!reasons.empty()) {
            s += " (";
            size_t shown = 0, counted = 0;
            for (const auto& [reason, n] : reasons) {
                if (shown == maxReasons) break;
                if (shown) s += "; ";
                s += std::to_string(n) + ": " + reason;
                counted += n;
                ++shown;
            }
            if (counted < c.failed) s += "; " + std::to_string(c.failed - counted) + " for other reasons";
            s += ")";
        }
    }
    return s;
}

}  // namespace iw8r
