// Stock map props: what to fetch next, whether the fetch has stopped, and the text that says so.
//
// A stock map draws its props (static models) from the editor's model cache. A first open has to fetch hundreds of
// models through ACTS. This file holds the parts of that which need no Qt and no cache, so they can be tested:
//   propFetchOrder       the missing models, most used first
//   PropFetchChain       decides after every pass over the props: ask the cache, wait for it, done, or stopped
//   propFailureReasons   failed models grouped by reason, most common first
//   propStatusLine       the one line the editor shows while and after the props are fetched
// Research note: docs/research/84_stock_prop_loading.md.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace iw8r {

// Distinct prop models of one stock map by their state in the model cache, with the placements that use them.
struct PropFetchCounts {
    size_t models = 0;   // distinct models the map places
    size_t ready = 0;    // cached and drawable
    size_t failed = 0;   // the cache tried and could not get them (they stay placeholders)
    size_t missing = 0;  // not asked yet, or being fetched
    size_t placements = 0, placementsReady = 0, placementsFailed = 0, placementsMissing = 0;
};

// The missing models in the order to ask for them: most placements first, then by name (a stable order).
std::vector<std::string> propFetchOrder(const std::map<std::string, size_t>& missingUse);

enum class PropFetchStep {
    Done,     // nothing is missing
    Request,  // ask the cache for the missing models now
    Wait,     // the cache is working: it reports when a batch has ended
    Stalled,  // the cache is idle, models are missing, and asking again changed nothing
};

// Guards the chain "ask, wait for the cache, redraw, ask for the rest". The editor calls step() after every pass
// over the props. A cache that goes idle without resolving anything (a missing tool, a request that never started)
// is asked once more; when that changes nothing either the chain says Stalled instead of asking for ever or, worse,
// waiting for ever. reset() starts over (another map, or the owner asks again).
class PropFetchChain {
public:
    void reset();
    PropFetchStep step(size_t missing, bool cacheBusy);
    bool stalled() const { return stalled_; }
    int requests() const { return requests_; }

private:
    bool asked_ = false;       // a request is out and has not been judged yet
    size_t askedMissing_ = 0;  // models missing when it was sent
    int idleNoProgress_ = 0;   // requests in a row that left the cache idle with nothing resolved
    int requests_ = 0;
    bool stalled_ = false;
};

// A cache error without the parts that differ per model, so equal causes group together: a leading "<zone>: " is
// dropped and the text after the last ": " of a long explanation is kept only when it is short.
std::string propFailureReason(const std::string& error);

// (reason, models) for the failed models, most models first, then by reason.
std::vector<std::pair<std::string, size_t>> propFailureReasons(const std::vector<std::string>& errors);

// The status line. `fetching`: the cache is working or about to be asked. `stallReason`: why a stalled fetch
// stopped (may be empty). At most `maxReasons` failure reasons are named; the rest are counted.
//   "Stock props: 108 of 514 models ready, fetching 406 (a first open can take several minutes)"
//   "Stock props: 511 of 514 models ready, 3 failed (3: LOD0 surface is in another zone)"
//   "Stock props: 108 of 514 models ready, 406 not fetched: Missing tool ..."
std::string propStatusLine(const PropFetchCounts& counts, bool fetching, bool stalled, const std::string& stallReason,
                           const std::vector<std::pair<std::string, size_t>>& reasons, size_t maxReasons = 2);

}  // namespace iw8r
