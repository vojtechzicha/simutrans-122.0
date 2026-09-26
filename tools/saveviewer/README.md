# Save viewer

A single HTML page that reads the JSON a Simutrans save exports and shows it as tables and a map.
No build step, no libraries, no network access. Open the file and drop the JSON on it.

## Exporting a save

```
simutrans -load CZR -export czr.json
```

The game loads the save, writes `czr.json` and exits. The file holds the map metadata, the settings,
the players, lines, stops, convoys, cities, factories and the goods table. All ids are the game's own
ids, so the viewer can link a line to its stops and convoys.

## Opening the viewer

Double-click `index.html`, then drop the JSON on the page or press "Choose file". That works from
`file://` in Chrome and Edge on Windows and macOS.

If you prefer a URL, serve the folder and pass the file name in the query string:

```
python3 -m http.server 8765 --directory tools/saveviewer
```

then open `http://localhost:8765/index.html?file=czr.json` with the JSON copied into this folder.
The page fetches that name on load and falls back to the file picker if it is not there.

## What the tabs show

Overview has the save name, pakset, game version, in-game date and map size, a row of counts, and
one line per player with cash, net wealth, monthly revenue and profit, and how many lines, stops and
convoys they own.

Settings lists every entry of the `settings` block as a setting and value pair, with a text filter.

Lines lists every line with its owner, type, state, convoy and stop counts, and this month's
transported amount, revenue and profit. Filter by text, owner or type, and sort by clicking a header.
Opening a line shows its schedule as an ordered list (waypoints are marked as such, stops link to the
stop page), the convoys running on it, and a table of the last twelve months.

Stops lists every stop with owner, station types, capacity and waiting amounts for passengers, mail
and goods, the happy, unhappy and no-route counters, and the number of lines and convoys. The detail
page adds the tile count, the waiting goods per type, the lines and convoys that serve the stop, its
direct connections, and the monthly statistics.

Convoys lists every convoy with its line, state, vehicle count, loading level, load against capacity
and this month's profit. The detail page lists the vehicles and, for a convoy that runs without a
line, its own schedule. A convoy with electric and other engines (mixed traction) has `traction`
(`under_wire_electric`, `under_wire_all` or `off_wire`, null for other convoys) and its vehicles
have `idle` when an engine is hauled without pulling; `max_speed` is the top speed of the engines
that pull at that moment. Coupled trains have `coupling` (`primary` for the train in front, `joined`
for the one running behind it, null otherwise) and `coupled_with` (the other convoy's id); each lists
only its own vehicles, load and money, and the states `coupled` and `uncoupling` are the joined train
while it rides along and right after it was uncoupled. `max_speed`, `sum_power`, `traction`,
`waiting_for` and `departure` of a joined train are those of the primary, which drives both.
`uncouples_at` is the stop where a coupled pair parts (null: not coupled, or they stay coupled).
`running_late` is set for a train that missed its coupling.
Schedule entries have `couple_line_id` (the line whose train this schedule's train joins there, or
null) and `couple_max_wait` in calendar minutes. Schedules have `no_standing` and `no_overcrowding`
(passenger vehicles take no standing or no overcrowded passengers; no standing implies no overcrowding).

What the convoy window shows under the destination is in `waiting_for` (null, or an object with
`reason`: `passing_train` with `convoy_id` and `convoy_name`, `no_free_track` / `last_free_track` with
`halt_id`, `station_entry`, `single_track`, `coupling_partner`, `platform_after_uncoupling`) and
`departure` (null, or `hour`, `minute`, `days_ahead`, `in_minutes`, `latest` when it is the end of the
maximum wait, and `ahead`, the trains of its line that leave first). `hold_marker` and `hold_divert`
are the Hold marker and a diversion to let a passing train by. Passenger vehicles have `seated`,
`standing`, `overcrowded`, `standing_max` and `overcrowded_max` (`capacity` is the seats), and the
convoy sums them in `crowd` (null without passenger vehicles), so `total_loaded` and `loading_level`
can pass the capacity and 100%. Stops give `missed` per waiting good and in `waiting_total`: waiting
passengers who missed a full vehicle and may board the next one overcrowded.

Schedule entries also have `depot` (true for a depot entry; like a waypoint it has no `halt_id`).

A line that runs coupled with another gets a card per partner line, the same on both lines: which
train runs in front, capacity together, where they couple, both timetables there, the shared stops
(with the turn at a terminus), where they uncouple and where each goes on, and the pairs coupled now.
The viewer plays the schedules forward like `convoi_t::get_uncouple_halt()`: the trains couple at an
entry of the front line at the same stop that goes on to the same next stop, and stay together while
both schedules go on to the same next stop (waypoints skipped, a depot ends it). A coupling point a
pair from another point passes already coupled is listed as a fallback. The schedule marks the
coupled stretch with a bar and notes where they couple and uncouple; the lines table has a
"Coupled with" column. The viewer also shows the stop type as the schedule dialog's letter badge,
a coupling card on convoy pages (the other train, where it parts, the whole train's vehicles front
first), the wait or departure text and badges in the convoy tables (with a filter for
coupled, waiting, late, hold and crowded convoys), and the calendar clock in the top bar.

Map draws the whole map extent on a canvas: cities as labeled dots, stops as small squares, and each
line schedule as a polyline in the color of its type. Drag to pan, use the wheel or the plus and
minus buttons to zoom, hover a stop or a line to see its name, click one to open it. The checkboxes
in the toolbar turn line types, stops and city labels on and off.

Cities, Factories and Goods are plain sortable tables.

Every table is filtered and sorted in the browser and rows are added in chunks, so a save with
thousands of stops still scrolls. Links between lines, stops and convoys go through the URL hash
(`#line/17`, `#stop/5`, `#convoy/3`), so the back button works.

## Sample data

`sample_data.py` writes a `sample.json` with the same shape as a real export: five cities, two
players, a few dozen stops, lines and convoys, Czech names with diacritics. Use it to try the viewer
without a save.

```
python3 sample_data.py           # writes sample.json next to the script
```

The viewer treats every field except the section arrays as optional, so an export that fills in less
than the schema describes still opens.
