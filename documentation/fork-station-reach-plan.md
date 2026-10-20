# Station reach: the nearest stop serves houses beyond the Reach (fork)

Status: implemented (savegame 122.15) with the owner's decisions: far reach 8, goods keep the stock
Reach, stops without a line do not count, no fallback from an overcrowded far stop, parking kept (the
removal tool of section 6 is not built), several stops in the Reach stay as stock. Added on the owner's
request: the display also shows the potential far reach (5.5). Numbers in sections 1-3 from
`FORK-CZR-VZ.sve` (2026-10-18), measured with a throwaway headless diagnostic on a copy of the save;
test results in section 8.

## 1. Problem

- The game runs with Reach (`station_coverage`) 2. A low Reach keeps neighbouring stops from
  sharing houses, so a trip between two close stops is a ride, not a walk, and each house belongs
  to its own stop.
- To reach every house anyway, stops are extended with `parking_(catchemnt_area_ext)1/2/3`: road
  stop objects that handle nothing (no passengers, mail or goods), joined to a stop. Each parking
  tile adds its own Reach square to the stop.
- In the save: 8,740 of 13,776 stops have parking, 26,757 parking tiles in all (77,090 stop tiles
  of every kind). 9 stops are made only of parking. Schedule entries point at 101 parking tiles
  (179 convoys have such an entry).

City building tiles by how stops reach them (weighted by passenger generation, `(level+6)>>2`;
140,309 tiles):

| Reached by | Passenger weight |
|---|---|
| Only stops with a non-parking tile in Reach | 44.1% |
| Only through parking | 47.0% |
| A stop in Reach plus another stop only through its parking (both serve) | 7.9% |
| Nothing | 1.0% |

Of the houses reached only through parking, 85% are served by one stop and 15% by two or more
(parking of neighbouring stops overlaps). Walking is rare today (5,102 walked against 1,043,371
transported passengers last month).

The parking network works, but every stop needs it placed by hand, and a house that no parking
reaches has no stop. The goal: the game picks the stop itself, so the parking tiles can go and the
network stays as close as possible to the one built with them.

## 2. Rule

For each house tile (also attraction tiles, and a factory as a whole for its workers and mail) and
each cargo class (passengers, mail; goods see 2.3):

1. **Reach (stock, unchanged):** every stop with any of its tiles within the Reach that handles the
   cargo serves the house. Parking tiles still count here.
2. **Far reach (new):** if no stop serves the house by rule 1, the nearest stop that handles the
   cargo, with a tile within the far reach `R` (a square, like the Reach), serves it alone.
   Nearest = straight-line (Euclidean) distance from the house to the stop's nearest tile. Stops at
   exactly the same distance all serve.

Consequences:
- Turning it on changes nothing for houses that are reached today; only unreached houses gain a
  stop. Parking keeps working as it does now and becomes the manual override: a parking tile gives
  houses a stop other than their nearest one.
- Without parking, each house beyond the Reach belongs to one stop, so the far areas of two stops
  never overlap. Walking keeps the stock rule (origin and destination share a serving stop), so
  passengers walk only within one stop's area.
- Passengers start at the stops that serve their house and the trip ends at the stops that serve
  the destination. Nothing else about route search changes.

### 2.1 Choices made by the measurements

- **Straight line, not walking along roads.** The parking chains follow streets, so a road
  distance looked promising. It is not better. For the parking-only weight, the straight-line
  nearest stop is exactly today's stop set for 61.9%, nearest by road steps (multi-source search
  over road and stop tiles) for 59.9% (4 neighbours) or 59.7% (8 neighbours). Counting any shared
  stop it is 77.1% against 77.7% / 79.5%, but road steps tie two to three times as often (9.5% /
  12.8% of the weight against 4.5%), and every tie is an overlap. The straight line is also
  simpler and cheap.
- **Ties.** With exact Euclidean distance 4.5% of the parking-only weight has a tie; all tied stops
  serve. Nearest by Chebyshev ring would tie for 20% and recreate the overlap.
- **No special case for parking in the engine.** The rule uses every stop tile. Parking needs no
  detection by name or flag: while it exists it works as today, once removed the far reach takes
  over.

### 2.2 Stops without a line, overcrowded stops

- A stop that no line or lineless convoy serves for the cargo does not count for the far reach,
  and does not stop it either: a house whose stops in Reach all have no such line gets its far stop
  (0.7% of the weight is in that state today). Otherwise a new stop built before its line would take
  over its nearest houses at once (1.3% of the parking-only weight has a stop without a passenger
  line as its nearest; 963 stops handle passengers but have none). Rule 1 keeps the stock behaviour
  for those stops: they stay in the house's list.
- It also keeps mail out while no line carries it (0 letters transported last month): houses then
  get no far mail stop. Counting unserved stops would give a far mail stop to every house within `R`
  of a stop that handles mail, and turn today's skipped mail generation (no mail stop, no route
  search) into a route search per house that finds nothing.
- An overcrowded far stop leaves its passengers unhappy, as stock does when all stops of a house
  are overcrowded. They do not fall back to a farther stop.

### 2.3 Goods

Goods keep the stock Reach. 736 of 838 factories have no freight stop at all and 13 reach one only
through parking; a far reach for goods would connect many factories to stations up to `R` tiles away
and change the goods flows. The far reach applies to factories only as passenger and mail
destinations (workers): 144 factories reach a passenger stop only through parking. A separate
`station_far_reach_goods` could follow later.

## 3. What it means for this game

Houses reached today only through parking, by the Chebyshev distance of their nearest stop tile
(not counting parking), and whether that nearest stop is one serving them now (cumulative shares
of all parking-only weight; last column: of the 1% reached by nothing today):

| `R` | Parking-only weight within `R` | ... and its nearest serves it now | Unreached houses newly served |
|---|---|---|---|
| 3 | 38.3% | 29.7% | 64.0% |
| 4 | 66.6% | 52.1% | 81.0% |
| 5 | 82.1% | 64.2% | 88.8% |
| 6 | 90.5% | 70.5% | 93.3% |
| 8 | 97.2% | 75.3% | 97.1% |
| 10 | 99.1% | 76.5% | 97.9% |
| 12 | 99.6% | 76.9% | 98.8% |

All parking removed, with the rule on, for all city building tiles (passenger weight):

| `R` | Same stops | Fewer stops, one of today's | Another stop | No stop | Newly served |
|---|---|---|---|---|---|
| 6 | 70.5% | 14.8% | 9.6% | 4.2% | 0.9% |
| 8 | 72.4% | 15.0% | 10.4% | 1.2% | 0.9% |
| 10 | 73.0% | 15.1% | 10.6% | 0.4% | 0.9% |

"Fewer stops" is the overlap going away: the 7.9% reached both by a stop in Reach and by another
stop's parking keep only the first, and houses served by two parking stops keep the nearer one.

Where the parking gives another stop than the nearest, the stop serving it now is farther by
(Euclidean, rounded): 0-1 tiles for 54% of that weight, 2 tiles for 21%, 3 or more for 25%. The
first two are borders drawn differently; the last looks deliberate (a station over a closer bus
stop, a stop on the far side of a barrier). Those are the parking tiles worth keeping (section 6).

Recommended `R` = 8: 97% of today's parking-only weight is within it, and with all parking removed
1.2% of all passenger weight would lose its stop (0.4% with `R` = 10).

## 4. Setting

- `station_far_reach` in simuconf.tab, saved with the game (savegame 122.15), 0 = off (stock).
  Values up to the Reach do nothing. Routing tab of the settings dialog, next to
  `station_coverage` (`gui/settings_stats.cc`). A value set in a simuconf.tab wins over the saved one
  whenever a game is loaded (not in network games: all clients must load the server's value), so it can
  be changed for an existing game; otherwise a save keeps its own (an older save takes the default).
- `settings_t`: default 0, `parse_simuconf`, `rdwr` after the 122.10 block. JSON export:
  `station_far_reach` next to `station_coverage` (`dataobj/savegame_export.cc`).
- en.tab: the setting label and "(nearest stop)".

## 5. Implementation

### 5.1 Data: far entries in the tile's stop list

`planquadrat_t::halt_list` keeps the stock entries (every stop with a tile within the Reach,
sorted by distance) and gets the far entries appended after them, on customer tiles only (city
buildings, attractions, a factory's `pos` tile). The list stays an array of `halthandle_t`, so no
consumer of `get_haltlist()` changes type. The far data lives in the padding of `planquadrat_t`:
after `halt_list` (8 bytes) and the three `uint8` fields the `data` union is aligned to 8, which
leaves 5 unused bytes per tile on 64-bit. Four of them are used: `far_count` (the last `far_count`
entries are far entries), a `uint16` with a 3-bit class mask for each of up to 4 far entries (bit 0
passengers, bit 1 mail, bit 2 goods) and `potential_count` for the display-only potential entries of
5.5. Ties beyond 4 far halts keep the first 4. No memory per tile; a `static_assert` on
`sizeof(planquadrat_t)` keeps it that way. `add_to_haltlist` and `sort_haltlist` work on the stock
part only; `halt_list_remove` of a far entry also drops its mask and lowers `far_count`.

A far entry's halt is never also a stock entry: categories are per halt, so a stop in Reach that
handles a class serves it by rule 1.

New `planquadrat_t` helpers:
- `bool is_serving( uint8 i, uint8 catg_index ) const`: stock entry: `halt->is_enabled( catg )`;
  far entry: its class bit; potential entry: never.
- `bool is_connected( halthandle_t halt, uint8 catg_index ) const`: arrival check, true for a stock
  entry of the halt or a far entry with the class bit.

Rejected alternatives:
- Widening the stock lists to `R`: every stop tile would enter 289 lists (R = 8) instead of 25,
  i.e. up to 22 million insertions instead of 1.9 million for the 77,090 stop tiles on this
  7192 x 5403 map (before the squares of one stop overlap), all to serve 140,000 house tiles.
- A flag byte per entry in the list block: either spare handles used as bytes, or a new element
  type that every `get_haltlist()` consumer would have to follow.
- A separate cache keyed by tile: a second invalidation system, and the coverage overlay, info
  windows, AI and scripts would not see it.
- Weighting walking distance in route search: changes route choice for every passenger, which is
  not what the parking network does.

### 5.2 Computation (`dataobj/station_reach.cc/.h`, `station_reach_t`)

In the Makefile SOURCES and `Simutrans-Main.vcxitems`.

- `update_tile( koord pos )`: drops the far entries; if the tile is a customer, for each class
  (passengers, mail) that no stock entry serves (handled and served by a line, 2.2), searches rings
  r = 0..R around it for stop tiles (every ground of the tile, `gr->get_halt()`), keeps the halts
  that handle the class and are served, nearest by squared Euclidean distance, ties kept; stops when
  r*r exceeds the best distance. Writes one far entry per halt with its class bits, then the
  potential entries. A factory searches around its footprint (`get_factory_box`: the bounding box of
  the tiles the factory map gives it; `pos` is no corner after rotating), distance to that box.
- `update_area( koord pos )`: `update_tile` for the customer tiles (and tiles holding far entries)
  within `R` of `pos`, and the `pos` tile of every factory with a tile within `R`
  (`fabrik_t::sind_da_welche`, as `verbinde_fabriken` does): a factory's `pos` can lie outside the
  square while its footprint does not.
- `update_halt( halthandle_t )`: the same around the bounding box of the halt's tiles.
- `update_all( bool clear_first )`: every city's buildings, the attractions, every factory.
- The halt keeps its served classes (`haltestelle_t::reach_served`: passengers, mail, goods carried by
  a registered line or lineless convoy), set at the end of `rebuild_connections`; `update_halt` only
  when they change. `get_serving_classes` adds the handled check.
- `get_far_reach()`: `station_far_reach` when above `station_coverage`, else 0 (off: no far entries,
  stock behaviour).

### 5.3 Triggers

None of the hooks below runs while a game loads: `add_grund`, `add_gebaeude_to_stadt` and
`add_attraction` are called for every stop tile and building then (`add_gebaeude_to_stadt` even from
the loading threads), and the bulk pass after loading covers them (`suspend()`/`resume()`, set for the
whole of `karte_t::load`; the world's `is_destroying()` counts too).

| Event | Call |
|---|---|
| Halt tile added or removed (`haltestelle_t::add_grund` / `rem_grund`, after the stock loops) | `update_area( pos )` |
| A halt's handled cargo changes (`add_grund`, `recalc_station_type`: `enables` before and after) | `update_halt` |
| A halt's served classes change (end of `rebuild_connections`) | `update_halt` |
| A halt is deleted (destructor, after the stock list cleanup) | `halt_removed`: strip its far entries, recompute around it |
| City building added or removed (`stadt_t::add_gebaeude_to_stadt` / `remove_gebaeude_from_stadt`; a renovation keeps the tile a customer) | `update_tile` / `clear_tile` per tile |
| Attraction added or removed (`karte_t::add_attraction` / `remove_attraction`) | `update_tile` / `clear_tile` |
| Factory built (end of `factory_builder_t::build_factory`) or removed (`hausbauer_t::remove`, `karte_t::rem_fab`) | `update_tile( pos )` / `clear_tile` |
| Game loaded, map enlarged (outermost `resume()`), map rotated | `update_all` |

On load the bulk pass goes after the reconnect loop (`karte_t::load`, after `haltestelle_t::step_all`):
lines register their stops only in the players' `finish_rd`, after the convoys and `end_load_game`, and
that loop runs `rebuild_connections` for every halt, which sets the served classes.

Halt merges and removal go through `rem_grund`/`add_grund`. Stock `remove_from_haltlist` (re-adds a
halt still within the Reach) may drop a far entry; the `update_area` that follows restores it.

### 5.4 Consumers

| Where | Change |
|---|---|
| `simcity.cc` `step_passagiere` start halts | `is_serving( h, catg )` instead of `halt->is_enabled( wtyp )` |
| `simcity.cc` unhappy / no route booked at the first stop | same |
| `simhalt.cc` `search_route` end halts | same |
| `simhalt.cc` `search_route_resumable` | presence check, explored halts and end halts with the category |
| `simhalt.cc` `reroute_goods`, `liefere_an` (arrived?) | `is_connected( self, catg )` |
| `simfab.cc` `verteile_waren`, `is_within_players_network`, status, info | skip far entries for goods |
| `simworld.cc` `rem_fab` copied the list into `halthandle_t list[16]` with `assert(count<16)` | copy into a `vector_tpl` |
| `fabrikbauer.cc` linking a new factory | skip far entries |

Display, AI and scripts read the list unchanged and so see the far stops (not the potential ones). A
passenger routed while the house had one far stop and arriving after a new stop took it over is
rerouted from there, or dropped if no route: the same as stock when a stop's coverage changes.

### 5.5 Display

Three kinds of coverage, so a city can be planned before its lines exist:
- Reach: as stock (tint, or filled boxes in the owner's colour).
- Far reach: a box with a dark border and a light centre, right of the Reach boxes (in the box mode a
  far box on top of a Reach box of the same colour disappeared: a house in the Reach of a stop without
  a line has both).
- Potential far reach: only the dark corners of that box. A potential entry is the nearest passenger
  stop as if every stop had a line, where no passenger stop is in the Reach and it is not the far stop
  already (display only, hidden from `get_haltlist_count()`, so no game code sees it).
- Hovering a stop with coverage shown marks its Reach squares (stock) and every house it serves or
  would serve through the far reach. The build-stop cursor of a passenger stop marks, beyond its Reach
  square, the houses for which it would become the nearest stop (`zeiger_t::set_far_preview`).
- Factory info lists far stops with "(nearest stop)". Minimap passenger and mail coverage, the
  attraction list and "in network" statistics include the far stops.

### 5.6 Load, network, savegame

- Stop lists are rebuilt from the halts on every load; `update_all` adds the far entries (343 ms on
  this save).
- Deterministic from the saved state, so network games stay in sync.
- Only the setting is saved. Saves keep loading in older fork builds once written back with
  `-saveversion 0.122.14` (or 0.122.0 for stock), which leaves the setting out.

### 5.7 Cost

- Passenger generation and route search: one flag test per list entry, nothing else.
- A stop tile built or removed: at most 289 customer tiles, each a ring search of at most 289 tiles
  (R = 8) where a class is missing; milliseconds even for a large station.

## 6. Removing the parking (phase 2, optional, not built)

A batch command next to `-rename`: `simutrans -load NAME -strip-parking MODE [-saveas OUT.sve]`
(dry run without `-saveas`), wrapped by `steam-fork.sh strip-parking NAME MODE` that backs up the
save and writes a new file, never over an existing one, like `rename`. Parking objects are found by
name (a prefix list in the command, default `parking_(catchemnt_area_ext)`).

| Mode | Removes (R = 8) | Network after |
|---|---|---|
| `unchanged` | 14,074 tiles: every parking tile not within Reach of a house whose stops would change | same stops for every house |
| `deliberate` | 18,519 tiles: keeps only parking behind houses whose nearest stop is not one of today's, or that would lose their stop | 15% of the weight keeps fewer of today's stops (the overlap goes) |
| `all` | 26,757 tiles | section 3 table |

Edge cases the command would handle: parking tiles that a schedule entry points at are kept (101
tiles), stops made only of parking are kept (9), a stop's name on a parking tile moves to another tile
(`rem_grund` does it), tiles with a vehicle on them or reserved are skipped. It would write a report
(TSV) with the removed and kept tiles and, after removal, every house whose serving stops changed, so
the estimate above is checked on the real result.

## 7. Decisions (taken 2026-10-19)

1. `R` = 8 (section 3).
2. Goods: stock Reach only (2.3).
3. Stops without a line do not count for the far reach (2.2).
4. Overcrowded far stop: no fallback (2.2).
5. Parking: all kept; no removal in this pass.
6. Houses within the Reach of two or more stops (6.1% of the weight, no parking involved) keep all
   of them, as stock.
7. The display shows Reach, far reach and potential far reach (5.5).

## 8. Testing

Results (2026-10-20, copy of `FORK-CZR-VZ.sve`, Reach 2, far reach 8): a throwaway hook recomputed every
customer tile by brute force (each stop tile pushed to the houses within reach, an independent method
from the ring search) after each step. Load: 2,711 tiles (2.3% of the passenger weight) get a far stop,
16 a potential one, stock entries identical everywhere; the same after a 60-minute run, after the most
far-served stop (Poděbrady, 131 tiles) lost its lines and got them back (identical state), after removing
that stop (84 tiles), after building a bus stop by an unreached house in Kladno (6 potential tiles, the
build preview marks the same 6; pretending a line turns them into far entries and back), after rotating
(the first version measured a factory from its `pos` as a corner, which it is not after rotating; it now
takes the bounding box of the factory's tiles) and after saving and loading. Only city-list entries whose
tile holds no building (15 in that save) are not served, as there is nothing to serve. Rebuilding all
far entries takes 343 ms (480 ms after a rotation, which clears the whole map first). 120 game minutes
from the same state: transported passengers 308,571 with far reach 0 and 325,034 with 8 (+5.3%), no
route 150,130 and 139,785, unhappy 61,641 and 68,479, walked 1,236 and 1,288. The GDI window showed all
three kinds on screen in both coverage modes.

The plan's checks were:
- The diagnostic used for this plan (load headless, classify every house tile, simulate the rule)
  becomes the acceptance check: with the rule on, the stock entries of every tile are byte-identical
  to a build without it, and the far entries equal the diagnostic's nearest stops.
- Run the real save headless for a game month with `R` = 0 and `R` = 8 and compare the cities'
  generated, transported, walked and no-route passengers.
- Throwaway harness on a small layout: build and remove stops, station buildings and lines near
  houses, grow a city, build and remove a factory, rotate the map, save and load; check the far
  entries after each step.
- Load time and fps on the real save with the rule on.
- On Windows: the coverage overlay and the stop hover in the GDI build.
