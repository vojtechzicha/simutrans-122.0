#!/usr/bin/env python3
"""Generate a realistic sample export for the Simutrans save viewer.

The real data comes from `simutrans -load <save> -export out.json`; this script
produces a file with the same shape (see export-schema) so the viewer can be
developed and tested without a game build.

Usage:
    python3 sample_data.py [out.json]     # default: sample.json next to this file
"""

import json
import os
import random
import sys

random.seed(19301)

MAP_W = 512
MAP_H = 512
MONTHS = 12

CITY_DEFS = [
    ("Praha", 118, 156, 148000),
    ("Brno", 302, 268, 76000),
    ("Plzen", 62, 244, 41000),
    ("Ostrava", 404, 118, 58000),
    ("Ceske Budejovice", 176, 372, 29000),
]
# with diacritics for display; ascii kept above only to keep the source readable
CITY_NAMES = {
    "Praha": "Praha",
    "Brno": "Brno",
    "Plzen": "Plzeň",
    "Ostrava": "Ostrava",
    "Ceske Budejovice": "České Budějovice",
}

STOP_SUFFIXES = [
    "hl.n.", "hlavní nádraží", "západní nádraží",
    "Jižní Město", "Holešovice", "Smíchov", "Vršovice",
    "Libeň", "Dejvická", "Náměstí Míru", "Florenc ÚAN",
    "Zastávka u lesa", "Přístaviště", "letiště",
    "Přívoz", "Staré Město", "Nové Sady", "U cukrovaru",
    "Dolní nádraží", "Horní náměstí", "Tovární",
    "Koupaliště", "Sídliště", "Nemocnice", "Pod Špičákem",
]

GOODS = [
    {"index": 0, "name": "Cestující", "catg_index": 0, "catg_name": "Passagiere",
     "speed_bonus": 18, "weight_per_unit": 0, "value": 32},
    {"index": 1, "name": "Pošta", "catg_index": 1, "catg_name": "Post",
     "speed_bonus": 12, "weight_per_unit": 5, "value": 42},
    {"index": 2, "name": "Uhlí", "catg_index": 2, "catg_name": "Bulk",
     "speed_bonus": 0, "weight_per_unit": 1000, "value": 12},
    {"index": 3, "name": "Ocel", "catg_index": 3, "catg_name": "Piece goods",
     "speed_bonus": 5, "weight_per_unit": 1000, "value": 28},
    {"index": 4, "name": "Dřevo", "catg_index": 2, "catg_name": "Bulk",
     "speed_bonus": 0, "weight_per_unit": 800, "value": 14},
    {"index": 5, "name": "Ropa", "catg_index": 4, "catg_name": "Liquid",
     "speed_bonus": 0, "weight_per_unit": 1000, "value": 30},
]

LINE_TYPES = ["rail", "road", "tram", "ship", "air", "monorail", "narrowgauge", "maglev"]
LINE_STATES = ["ok", "ok", "ok", "ok", "loss", "overcrowded", "no_convoys",
               "missing_scheduled_slots", "obsolete", "nothing_moved"]

CONVOY_STATES = ["driving", "loading", "waiting_for_clearance", "in_depot",
                 "no_route", "self_destruct", "leaving_depot"]

VEHICLES = {
    "rail": [("Br 754", 0, 0, 120, 1400), ("Bdmtee", 64, 0, 120, 0),
             ("Bmz", 72, 0, 140, 0), ("Aby", 48, 0, 140, 0),
             ("Es slide", 0, 2, 80, 0), ("Falls", 0, 2, 90, 0)],
    "road": [("Karosa B732", 22, 0, 80, 180), ("Škoda 21Ab", 30, 0, 65, 175),
             ("Tatra 815 sklápěč", 0, 2, 90, 270)],
    "tram": [("Tatra T3", 24, 0, 65, 160), ("Škoda 15T", 60, 0, 70, 480)],
    "ship": [("Nákladní člun", 0, 2, 20, 400),
             ("Osobní parník", 120, 0, 25, 300)],
    "air": [("ATR 72", 68, 0, 480, 2000), ("Boeing 737", 140, 0, 840, 9000)],
    "monorail": [("Monorail vůz", 80, 0, 160, 900)],
    "narrowgauge": [("M 27.0", 40, 0, 50, 120)],
    "maglev": [("Transrapid", 90, 0, 400, 4000)],
}

FACTORY_NAMES = [
    "Uhelný důl", "Ocelárna", "Rafinerie", "Pila", "Cukrovar",
    "Elektrárna", "Automobilka", "Pivovar", "Papírna", "Cementárna",
]


def money(lo, hi):
    return round(random.uniform(lo, hi), 2)


def series(n, lo, hi, kind="int", zero_chance=0.0):
    out = []
    for _ in range(n):
        if zero_chance and random.random() < zero_chance:
            out.append(0 if kind == "int" else 0.0)
        elif kind == "int":
            out.append(random.randint(lo, hi))
        else:
            out.append(round(random.uniform(lo, hi), 2))
    return out


def build():
    cities = []
    for name, x, y, pop in CITY_DEFS:
        cities.append({
            "name": CITY_NAMES[name],
            "pos": {"x": x, "y": y},
            "population": pop + random.randint(-3000, 9000),
            "buildings": pop // 45,
            "growth": random.randint(-4, 90),
        })

    players = [
        {
            "id": 0, "name": "České dráhy", "is_human": True, "ai_type": None,
            "active": True, "color1": 3, "color2": 7,
            "cash": money(1.2e6, 4.5e6), "net_wealth": money(9e6, 2.4e7),
            "headquarters": {"x": 120, "y": 158},
            "finance": {"revenue_this_month": money(1e5, 4e5), "profit_this_month": money(-2e4, 2e5),
                        "revenue_last_year": money(2e6, 5e6), "profit_last_year": money(3e5, 1.6e6)},
            "convoy_count": 0, "line_count": 0, "halt_count": 0,
        },
        {
            "id": 1, "name": "Dopravní podnik Města", "is_human": False, "ai_type": "goods_ai",
            "active": True, "color1": 12, "color2": 2,
            "cash": money(2e5, 1.1e6), "net_wealth": money(1e6, 6e6),
            "headquarters": None,
            "finance": {"revenue_this_month": money(2e4, 9e4), "profit_this_month": money(-4e4, 4e4),
                        "revenue_last_year": money(3e5, 1.1e6), "profit_last_year": money(-8e4, 3e5)},
            "convoy_count": 0, "line_count": 0, "halt_count": 0,
        },
    ]

    # ---- stops -------------------------------------------------------------
    stops = []
    sid = 1
    for ci, (name, cx, cy, _pop) in enumerate(CITY_DEFS):
        czname = CITY_NAMES[name]
        count = 10 if ci == 0 else 7 if ci < 3 else 6
        for k in range(count):
            suffix = STOP_SUFFIXES[(ci * 5 + k) % len(STOP_SUFFIXES)]
            x = max(2, min(MAP_W - 3, cx + random.randint(-26, 26)))
            y = max(2, min(MAP_H - 3, cy + random.randint(-26, 26)))
            owner = 0 if random.random() < 0.75 else 1
            types = random.choice([
                ["rail"], ["rail", "road"], ["road"], ["road", "truck"],
                ["tram"], ["tram", "road"], ["ship"], ["air"], ["monorail"],
            ])
            cap_p = random.choice([0, 120, 240, 320, 640])
            cap_m = random.choice([0, 40, 80, 160])
            cap_g = random.choice([0, 0, 100, 250])
            waiting = []
            for g in GOODS:
                if g["catg_index"] == 0 and cap_p == 0:
                    continue
                if g["catg_index"] == 1 and cap_m == 0:
                    continue
                if g["catg_index"] > 1 and cap_g == 0:
                    continue
                if random.random() < 0.55:
                    waiting.append({"goods_index": g["index"], "goods_name": g["name"],
                                    "amount": random.randint(1, 260)})
            wt = {"passengers": 0, "mail": 0, "goods": 0}
            for w in waiting:
                gi = GOODS[w["goods_index"]]["catg_index"]
                key = "passengers" if gi == 0 else "mail" if gi == 1 else "goods"
                wt[key] += w["amount"]
            stops.append({
                "id": sid,
                "name": "%s %s" % (czname, suffix),
                "owner_id": owner,
                "pos": {"x": x, "y": y, "z": 0},
                "tiles": [{"x": x + dx, "y": y + dy, "z": 0}
                          for dx in range(random.randint(1, 3)) for dy in range(random.randint(1, 2))],
                "station_types": types,
                "enabled": {"passengers": cap_p > 0, "mail": cap_m > 0, "goods": cap_g > 0},
                "capacity": {"passengers": cap_p, "mail": cap_m, "goods": cap_g},
                "waiting": waiting,
                "waiting_total": wt,
                "pax": {"happy": random.randint(0, 2400), "unhappy": random.randint(0, 300),
                        "no_route": random.randint(0, 90)},
                "line_ids": [], "convoy_ids": [],
                "connections": [],
                "stats": {
                    "arrived": series(MONTHS, 0, 3200),
                    "departed": series(MONTHS, 0, 3200),
                    "waiting": series(MONTHS, 0, 900),
                    "happy": series(MONTHS, 0, 2400),
                    "unhappy": series(MONTHS, 0, 300),
                    "no_route": series(MONTHS, 0, 90),
                    "convoys_arrived": series(MONTHS, 0, 160),
                },
                "_city": ci,
            })
            sid += 1

    by_city = {}
    for s in stops:
        by_city.setdefault(s["_city"], []).append(s)

    # ---- lines -------------------------------------------------------------
    line_specs = [
        ("R1 Praha - Kolín", "rail", [0, 1]),
        ("R2 Praha - Plzeň", "rail", [0, 2]),
        ("Ex 3 Praha - Ostrava", "rail", [0, 3]),
        ("Nákladní uhlí Ostrava", "rail", [3, 3]),
        ("Os 9 Brno okruh", "rail", [1, 1]),
        ("Autobus 101", "road", [0, 0]),
        ("Autobus 205 Plzeň", "road", [2, 2]),
        ("Kamiony s ocelí", "road", [3, 1]),
        ("Tramvaj 22", "tram", [0, 0]),
        ("Tramvaj 4 Brno", "tram", [1, 1]),
        ("Lodní linka Vltava", "ship", [0, 4]),
        ("Letecká linka PRG-OSR", "air", [0, 3]),
        ("Monorail Jižní Město", "monorail", [0, 0]),
        ("Úzkokolejka Jindřichův Hradec", "narrowgauge", [4, 4]),
    ]

    lines = []
    convoys = []
    stop_by_id = {s["id"]: s for s in stops}
    next_convoy = 1

    for i, (lname, ltype, cities_used) in enumerate(line_specs):
        lid = 10 + i * 3
        owner = 1 if ltype in ("tram", "road") and random.random() < 0.5 else 0
        pool = []
        for c in set(cities_used):
            pool += by_city[c]
        random.shuffle(pool)
        nstops = random.randint(3, 8)
        chosen = pool[:nstops]
        entries = []
        for s in chosen:
            entries.append({
                "pos": dict(s["pos"]),
                "halt_id": s["id"],
                "minimum_loading": random.choice([0, 0, 0, 20, 50, 100]),
                "waiting_time": random.choice([0, 0, 0, 4, 8, 16]),
            })
            # occasional waypoint between stops
            if random.random() < 0.25:
                entries.append({
                    "pos": {"x": s["pos"]["x"] + random.randint(-14, 14),
                            "y": s["pos"]["y"] + random.randint(-14, 14), "z": 0},
                    "halt_id": None, "minimum_loading": 0, "waiting_time": 0,
                })
        state = random.choice(LINE_STATES)
        catgs = sorted({0} if ltype in ("tram", "monorail") else
                       {random.choice([0, 0, 1, 2, 3])} | ({1} if random.random() < 0.4 else set()))
        line = {
            "id": lid,
            "name": lname,
            "owner_id": owner,
            "type": ltype,
            "state": state,
            "goods_categories": catgs,
            "convoy_ids": [],
            "schedule": {
                "current_stop": random.randint(0, max(0, len(entries) - 1)),
                "bidirectional": random.random() < 0.4,
                "mirrored": random.random() < 0.2,
                "entries": entries,
            },
            "stats": {
                "capacity": series(MONTHS, 200, 9000),
                "transported": series(MONTHS, 0, 8000),
                "revenue": series(MONTHS, 0, 180000, "float"),
                "operations": series(MONTHS, 0, 120000, "float"),
                "profit": series(MONTHS, -40000, 120000, "float"),
                "convoys": series(MONTHS, 0, 6),
                "distance": series(MONTHS, 0, 30000),
                "maxspeed": series(MONTHS, 40, 200),
            },
        }
        lines.append(line)

        ncv = 0 if state == "no_convoys" else random.randint(1, 4)
        for _ in range(ncv):
            cid = next_convoy
            next_convoy += 1
            vdefs = VEHICLES.get(ltype, VEHICLES["rail"])
            nveh = random.randint(1, 6) if ltype in ("rail", "road") else random.randint(1, 3)
            vehicles = []
            for vi in range(nveh):
                vn, cap, catg, mspd, power = random.choice(vdefs)
                vehicles.append({
                    "name": vn, "capacity": cap, "goods_catg": catg,
                    "loaded": random.randint(0, cap) if cap else 0,
                    "max_speed": mspd, "power": power,
                })
            tot_cap = sum(v["capacity"] for v in vehicles)
            tot_load = sum(v["loaded"] for v in vehicles)
            here = random.choice(chosen)
            convoys.append({
                "id": cid,
                "name": "%s %d" % (lname, len(line["convoy_ids"]) + 1),
                "owner_id": owner,
                "line_id": lid,
                "state": random.choice(CONVOY_STATES),
                "pos": {"x": here["pos"]["x"] + random.randint(-6, 6),
                        "y": here["pos"]["y"] + random.randint(-6, 6), "z": 0},
                "vehicle_count": len(vehicles),
                "vehicles": vehicles,
                "max_speed": min(v["max_speed"] for v in vehicles),
                "loading_level": int(100 * tot_load / tot_cap) if tot_cap else 0,
                "loading_limit": random.choice([0, 0, 50, 100]),
                "total_capacity": tot_cap,
                "total_loaded": tot_load,
                "home_depot": {"x": here["pos"]["x"], "y": here["pos"]["y"], "z": 0},
                "profit_this_month": money(-9000, 42000),
                "profit_last_year": money(-40000, 380000),
                "revenue_this_month": money(0, 90000),
                "schedule": None,
            })
            line["convoy_ids"].append(cid)
            for s in chosen:
                if cid not in s["convoy_ids"]:
                    s["convoy_ids"].append(cid)

        for s in chosen:
            if lid not in s["line_ids"]:
                s["line_ids"].append(lid)
        # connections between consecutive halts of the line
        halt_seq = [e["halt_id"] for e in entries if e["halt_id"] is not None]
        for a, b in zip(halt_seq, halt_seq[1:]):
            for catg in catgs:
                for src, dst in ((a, b), (b, a)):
                    conns = stop_by_id[src]["connections"]
                    if not any(c["halt_id"] == dst and c["catg_index"] == catg for c in conns):
                        conns.append({"halt_id": dst, "catg_index": catg})

    # a handful of line-less convoys with their own schedule
    for _ in range(4):
        cid = next_convoy
        next_convoy += 1
        chosen = random.sample(stops, 3)
        vdefs = VEHICLES["road"]
        vehicles = []
        for _vi in range(random.randint(1, 3)):
            vn, cap, catg, mspd, power = random.choice(vdefs)
            vehicles.append({"name": vn, "capacity": cap, "goods_catg": catg,
                             "loaded": random.randint(0, cap) if cap else 0,
                             "max_speed": mspd, "power": power})
        tot_cap = sum(v["capacity"] for v in vehicles)
        tot_load = sum(v["loaded"] for v in vehicles)
        convoys.append({
            "id": cid,
            "name": "Volný spoj %d" % cid,
            "owner_id": random.choice([0, 1]),
            "line_id": None,
            "state": random.choice(CONVOY_STATES),
            "pos": {"x": chosen[0]["pos"]["x"], "y": chosen[0]["pos"]["y"], "z": 0},
            "vehicle_count": len(vehicles),
            "vehicles": vehicles,
            "max_speed": min(v["max_speed"] for v in vehicles),
            "loading_level": int(100 * tot_load / tot_cap) if tot_cap else 0,
            "loading_limit": 0,
            "total_capacity": tot_cap,
            "total_loaded": tot_load,
            "home_depot": None,
            "profit_this_month": money(-4000, 9000),
            "profit_last_year": money(-20000, 60000),
            "revenue_this_month": money(0, 20000),
            "schedule": {
                "current_stop": 0, "bidirectional": True, "mirrored": False,
                "entries": [{"pos": dict(s["pos"]), "halt_id": s["id"],
                             "minimum_loading": 0, "waiting_time": 0} for s in chosen],
            },
        })
        for s in chosen:
            s["convoy_ids"].append(cid)

    for s in stops:
        del s["_city"]

    factories = []
    for i, fn in enumerate(FACTORY_NAMES):
        cx, cy = CITY_DEFS[i % len(CITY_DEFS)][1:3]
        factories.append({
            "name": fn,
            "pos": {"x": max(1, cx + random.randint(-60, 60)),
                    "y": max(1, cy + random.randint(-60, 60)), "z": 0},
            "owner_id": None if random.random() < 0.8 else 0,
            "production": random.randint(20, 900),
        })

    for p in players:
        p["line_count"] = sum(1 for l in lines if l["owner_id"] == p["id"])
        p["halt_count"] = sum(1 for s in stops if s["owner_id"] == p["id"])
        p["convoy_count"] = sum(1 for c in convoys if c["owner_id"] == p["id"])

    settings = {
        "starting_year": 1930, "starting_month": 0, "bits_per_month": 20,
        "use_timeline": 2, "freeplay": False, "starting_money": 15000000,
        "pay_for_total_distance": 0, "factory_worker_percentage": 33,
        "passenger_factor": 16, "max_route_steps": 1000000, "max_transfers": 7,
        "max_hops": 2000, "traffic_level": 5, "separate_halt_capacities": False,
        "avoid_overcrowding": False, "no_routing_over_overcrowding": False,
        "station_coverage": 2, "allow_buying_obsolete_vehicles": 1,
        "just_in_time": 1, "industry_increase_every": 0,
        "minimum_city_distance": 16, "groundwater": -2,
        "way_toll_runningcost_percentage": 0, "way_toll_waycost_percentage": 0,
        "way_toll_revenue_percentage": 0, "maintenance_building": 5000,
        "cst_multiply_dock": -50000, "cst_multiply_station": -60000,
        "cst_multiply_roadstop": -40000, "cst_multiply_airterminal": -300000,
        "cst_multiply_post": -30000, "cst_multiply_headquarter": -100000,
        "cst_depot_rail": -100000, "cst_depot_road": -130000,
        "cst_buy_land": -10000, "cst_alter_land": -100000,
        "cst_set_slope": -250000, "cst_found_city": -5000000,
        "numbered_stations": True, "language": "cs",
        "pak_name": "pak128.cs", "allow_player_change": True,
        "city_isolation_factor": 1, "crossconnect_factories": False,
        "electric_promille": 330, "show_pax": True,
        "beginner_mode": False, "advance_speedbonus_year": 0,
    }

    return {
        "format": 1,
        "meta": {
            "save_file": "CZR.sve",
            "game_version": "0.122.0",
            "pakset": "pak128.cs",
            "exported_at": "2026-09-11T18:00:00Z",
            "map": {"width": MAP_W, "height": MAP_H},
            "date": {"year": 2031, "month": 5, "ticks": 123456},
            "player_count": len(players),
        },
        "settings": settings,
        "goods": GOODS,
        "players": players,
        "lines": lines,
        "stops": stops,
        "convoys": convoys,
        "cities": cities,
        "factories": factories,
    }


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "sample.json")
    data = build()
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=1)
    print("wrote %s (%d stops, %d lines, %d convoys, %.0f kB)" % (
        out, len(data["stops"]), len(data["lines"]), len(data["convoys"]),
        os.path.getsize(out) / 1024.0))


if __name__ == "__main__":
    main()
