"""Evidence packets + agent roster for the structs replay_layouts.py could not prove (action "report").

Each packet (out/ida_types/packets/<struct>.json) holds: the game-test layout, the current IDA layout, the builder's
Replay layout with per-field confidence, the access anchors (game-test offset -> Replay offset, votes), the unresolved
residuals, why it was not applied, the Replay accesses past the end, and the Replay / game-test functions that reach
the struct through a typed argument (with the offsets they touch). Structs are ranked by Replay accesses that land on
a wrong member and grouped by family into agents.

  python tools/ida_types/make_report_packets.py --agents 30 --first-id 101
"""
import argparse, collections, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay_layouts as RL

S = RL.S
PAD = RL.PAD


def family(n):
    for p, f in (("BgVehicle", "vehicle physics"), ("HelicopterDynamics", "vehicle physics"),
                 ("FlightDynamics", "vehicle physics"), ("CgVehicle", "vehicle physics"), ("GVehicle", "vehicle physics"),
                 ("bot_", "bots / AI"), ("AI", "bots / AI"), ("ai_", "bots / AI"), ("bfx", "bots / AI"),
                 ("hk", "Havok"), ("Havok", "Havok"), ("bd", "Demonware"), ("DW", "Demonware"),
                 ("Online", "online"), ("Party", "online"), ("MatchMaker", "online"), ("Lobby", "online"),
                 ("Gfx", "renderer"), ("R_", "renderer"), ("RB", "renderer"), ("Particle", "effects"), ("Fx", "effects"),
                 ("Cg", "client game"), ("cg", "client game"), ("Cl", "client"), ("Sv", "server"), ("G", "server game"),
                 ("scr", "script VM"), ("DLog", "telemetry"), ("LUI", "UI"), ("snd", "sound"), ("Snd", "sound")):
        if n.startswith(p):
            return f
    return "misc"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--agents", type=int, default=30)
    ap.add_argument("--first-id", type=int, default=101)
    a = ap.parse_args()
    gt = json.load(open(S + "gt_types_all.json"))
    ida = json.load(open(S + "ida_udts.json"))
    al = json.load(open(S + "all_align.json"))
    lay = json.load(open(S + "replay_layouts.json"))["structs"]
    mx = json.load(open(S + "replay_maxacc.json"))
    agent_set = RL.agent_set()
    w = RL.World(gt, ida, {k: dict(v) for k, v in al["pairs"].items()}, collections.defaultdict(collections.Counter),
                 agent_set)
    order = w.order()
    for g in order:
        w.build(g)
    # functions reaching each struct (Replay by name, game-test by start)
    rnames = {s: n for s, e, n in json.load(open(S + "functions.json"))}
    racc = collections.defaultdict(lambda: collections.defaultdict(set))
    for line in open(S + "replay_types.jsonl"):
        r = json.loads(line)
        for addr, st, off, wd, kind in r["acc"]:
            racc[st][rnames.get(r["s"], hex(r["s"]))].add(off)
    os.makedirs("out/ida_types/packets", exist_ok=True)
    rows = []
    for n, v in lay.items():
        if v["action"] != "report" or ida.get(n, {}).get("union"):
            continue
        g = v["gt"]
        ok, bad = RL.score(w, g, ida[n]["members"])
        funcs = sorted(racc.get(g, {}).items(), key=lambda kv: -len(kv[1]))[:25]
        pairs = sorted(((int(k.split(":")[0]), int(k.split(":")[1]), c) for k, c in w.pairs.get(g, {}).items()))
        pk = {
            "struct": n, "game_test_name": g, "ida_size": v["ida_size"], "game_test_size": v["gt_size"],
            "builder_size": v["size"], "builder_size_proof": v["size_proof"], "why_not_applied": v["extra"].get("why"),
            "unresolved_residuals": v["unresolved"], "replay_max_access_end": mx.get(g, 0),
            "score_now": [ok, bad], "agent_verified_before": n in agent_set,
            "game_test_layout": gt[g]["fields"], "game_test_bases": gt[g]["bases"],
            "ida_layout": ida[n]["members"],
            "builder_layout": [[s["roff"], s["name"], s["rsize"], s["conf"], s["off"]] for s in w.res[g]["slots"]],
            "anchors_gt_to_replay": pairs[:400],
            "replay_functions": [[f, sorted(o)[:40]] for f, o in funcs],
        }
        json.dump(pk, open("out/ida_types/packets/%s.json" % n, "w"), indent=0)
        rows.append((bad, ok, n, family(n), v["ida_size"]))
    rows.sort(reverse=True)
    # roster: the worst structs get an agent each, the rest are grouped by family
    k = a.agents
    solo = [r for r in rows if r[0] >= 60][: k // 2]
    rest = [r for r in rows if r not in solo]
    fams = collections.defaultdict(list)
    for r in rest:
        fams[r[3]].append(r)
    slots = k - len(solo)
    total = sum(len(v) for v in fams.values())
    roster = [[r] for r in solo]
    for f, lst in sorted(fams.items(), key=lambda kv: -len(kv[1])):
        share = max(1, round(slots * len(lst) / total)) if total else 1
        per = -(-len(lst) // share)
        for i in range(0, len(lst), per):
            roster.append(lst[i:i + per])
    roster = roster[:k] if len(roster) <= k else roster
    out = []
    for i, grp in enumerate(roster):
        out.append({"id": "A%03d" % (a.first_id + i), "family": grp[0][3],
                    "structs": [r[2] for r in grp], "wrong_member_hits": sum(r[0] for r in grp)})
    json.dump(out, open("docs/ida_types/report_agents.json", "w"), indent=1)
    print(len(rows), "report structs,", len(out), "agents")
    for x in out:
        print(x["id"], x["family"], x["wrong_member_hits"], x["structs"][:6], "+%d" % max(0, len(x["structs"]) - 6))


if __name__ == "__main__":
    main()
