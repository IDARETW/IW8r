#include "stock_prop_fetch.h"
#include "test_harness.h"

using namespace iw8r;

TEST(stock_prop_fetch_order_most_used_first) {
    const std::map<std::string, size_t> use = {{"tm/crate", 3}, {"tm/barrel", 40}, {"tm/apple", 3}, {"tm/fence", 12}};
    const std::vector<std::string> order = propFetchOrder(use);
    CHECK(order.size() == 4);
    CHECK(order[0] == "tm/barrel");
    CHECK(order[1] == "tm/fence");
    CHECK(order[2] == "tm/apple");  // equal use: by name
    CHECK(order[3] == "tm/crate");
    CHECK(propFetchOrder({}).empty());
}

// The ordinary first open: one request, the cache works through its batches, every pass in between waits.
TEST(stock_prop_fetch_chain_normal_run) {
    PropFetchChain chain;
    CHECK(chain.step(405, false) == PropFetchStep::Request);
    CHECK(chain.step(405, true) == PropFetchStep::Wait);
    CHECK(chain.step(255, true) == PropFetchStep::Wait);
    CHECK(chain.step(105, true) == PropFetchStep::Wait);
    CHECK(chain.step(0, false) == PropFetchStep::Done);
    CHECK(chain.requests() == 1);
    CHECK(!chain.stalled());
}

// A cache that goes idle with models still missing but fewer than before is asked again (it made progress).
TEST(stock_prop_fetch_chain_asks_again_after_progress) {
    PropFetchChain chain;
    CHECK(chain.step(300, false) == PropFetchStep::Request);
    CHECK(chain.step(150, false) == PropFetchStep::Request);
    CHECK(chain.step(20, false) == PropFetchStep::Request);
    CHECK(chain.step(0, false) == PropFetchStep::Done);
    CHECK(chain.requests() == 3);
    CHECK(!chain.stalled());
}

// A cache that delivers nothing (a missing tool, a request that never starts) must end the chain with a verdict:
// asked once more, then stalled. Never an endless wait and never an endless loop of requests.
TEST(stock_prop_fetch_chain_stalls_when_nothing_arrives) {
    PropFetchChain chain;
    CHECK(chain.step(405, false) == PropFetchStep::Request);
    CHECK(chain.step(405, false) == PropFetchStep::Request);  // one more try
    CHECK(chain.step(405, false) == PropFetchStep::Stalled);
    CHECK(chain.stalled());
    for (int i = 0; i < 20; ++i) CHECK(chain.step(405, false) == PropFetchStep::Stalled);
    CHECK(chain.requests() == 2);
    // Models arriving after all (the owner fixed the tool path and something else fetched them) end the stall.
    CHECK(chain.step(0, false) == PropFetchStep::Done);
    CHECK(!chain.stalled());
    // Another map starts over.
    chain.reset();
    CHECK(chain.requests() == 0);
    CHECK(chain.step(10, false) == PropFetchStep::Request);
}

// The cache is busy with other models (the map's entities) when the props are first seen: they are queued behind
// that work at once, not left until something else happens to wake the chain.
TEST(stock_prop_fetch_chain_requests_while_cache_busy_elsewhere) {
    PropFetchChain chain;
    CHECK(chain.step(405, true) == PropFetchStep::Request);
    CHECK(chain.step(405, true) == PropFetchStep::Wait);
    CHECK(chain.step(255, true) == PropFetchStep::Wait);
    CHECK(chain.step(0, false) == PropFetchStep::Done);
    CHECK(chain.requests() == 1);
}

// A batch in which every model fails still resolves them (failed is an answer): the chain goes on and ends.
TEST(stock_prop_fetch_chain_failed_batch_is_progress) {
    PropFetchChain chain;
    CHECK(chain.step(400, false) == PropFetchStep::Request);
    CHECK(chain.step(250, true) == PropFetchStep::Wait);   // 150 failed, next batch running
    CHECK(chain.step(100, true) == PropFetchStep::Wait);
    CHECK(chain.step(0, false) == PropFetchStep::Done);
    CHECK(!chain.stalled());
}

TEST(stock_prop_fetch_failure_reasons_group) {
    CHECK(propFailureReason("mp_rust: not in zone") == "not in zone");
    CHECK(propFailureReason("global_stream_mp: not in zone") == "not in zone");
    CHECK(propFailureReason("not listed in iw8_models.tsv") == "not listed in iw8_models.tsv");
    CHECK(propFailureReason("its LOD0 surface buffer is in another zone: ,uk_industrial_light_01_lod000v011") ==
          "its LOD0 surface buffer is in another zone");
    CHECK(propFailureReason("mp_rust: ACTS wrote no journal (exit 1): some log tail") == "ACTS wrote no journal (exit 1)");
    CHECK(propFailureReason("") == "no reason recorded");
    const auto reasons = propFailureReasons({"mp_rust: not in zone", "mp_hideout: not in zone", "cannot copy the GLB into the cache",
                                             "its LOD0 surface buffer is in another zone: ,a", "mp_backlot2: not in zone",
                                             "its LOD0 surface buffer is in another zone: ,b"});
    CHECK(reasons.size() == 3);
    CHECK(reasons[0].first == "not in zone" && reasons[0].second == 3);
    CHECK(reasons[1].first == "its LOD0 surface buffer is in another zone" && reasons[1].second == 2);
    CHECK(reasons[2].first == "cannot copy the GLB into the cache" && reasons[2].second == 1);
}

TEST(stock_prop_fetch_status_line) {
    PropFetchCounts c;
    CHECK(propStatusLine(c, false, false, "", {}) == "Stock props: none in this map");
    c.models = 514;
    c.ready = 108;
    c.missing = 406;
    CHECK(propStatusLine(c, true, false, "", {}) ==
          "Stock props: 108 of 514 models ready, fetching 406 (a first open can take several minutes)");
    CHECK(propStatusLine(c, false, true, "Missing tool, model cache idle: X:/acts.exe", {}) ==
          "Stock props: 108 of 514 models ready, 406 not fetched: Missing tool, model cache idle: X:/acts.exe");
    CHECK(propStatusLine(c, false, true, "", {}) ==
          "Stock props: 108 of 514 models ready, 406 not fetched: the model cache stopped without an answer");
    c.ready = 511;
    c.missing = 0;
    c.failed = 3;
    CHECK(propStatusLine(c, false, false, "", {{"its LOD0 surface buffer is in another zone", 3}}) ==
          "Stock props: 511 of 514 models ready, 3 failed (3: its LOD0 surface buffer is in another zone)");
    c.ready = 500;
    c.failed = 14;
    CHECK(propStatusLine(c, false, false, "", {{"not in zone", 8}, {"a", 3}, {"b", 2}, {"c", 1}}) ==
          "Stock props: 500 of 514 models ready, 14 failed (8: not in zone; 3: a; 3 for other reasons)");
    c.ready = 514;
    c.failed = 0;
    CHECK(propStatusLine(c, false, false, "", {}) == "Stock props: 514 of 514 models ready");
    // Still fetching with some already failed: both are said.
    c.ready = 300;
    c.missing = 200;
    c.failed = 14;
    const std::string both = propStatusLine(c, true, false, "", {{"not in zone", 14}});
    CHECK(both.find("fetching 200") != std::string::npos);
    CHECK(both.find("14 failed (14: not in zone)") != std::string::npos);
}
