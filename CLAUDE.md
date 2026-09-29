# Simutrans 122.0 — personal feature fork

Fork of Simutrans at the 122.0 release (SVN r9274). History starts fresh: the root commit (tag
`122.0`) is the pristine upstream source, everything after it is this fork's work on branch `main`.
Goal: add and update game features for the owner's own Windows install. Not intended for upstream
unless explicitly asked.

Remotes: `origin` is github.com/vojtechzicha/simutrans-122.0 (push here). `upstream` is the official
github.com/simutrans/simutrans clone, kept locally for reference only, never pushed to or merged:
`git log upstream/master -- <path>` shows how a file evolved after 122.0, and
`git show b4089f540:<path>` is the upstream 122.0 version of a file.

## Layout (only the parts that matter for feature work)

- `simworld.*` (`karte_t`) — the map and main game loop; `simmain.cc` — startup, args, dialogs.
- `obj/` — everything placed on a tile (`obj_t` subclasses: vehicles, buildings, trees, signals).
- `vehicle/`, `simconvoi.*`, `simline.*`, `simhalt.*` — convoys, lines, stops.
- `gui/` — dialogs (`gui_frame_t`), `gui/components/` — widgets. Read `documentation/about-the-code.txt`
  before touching the UI: use `add_component`, `add_listener(this)`, `action_triggered`, and the
  spacings from `gui/gui_theme.h`, never fixed pixel numbers.
- `dataobj/` — settings, translator, loadsave, schedules. `descriptor/` — pak object descriptors.
- `bauer/` — builders (ways, bridges, buildings, vehicles). `player/` — players and AIs.
- `sys/` — platform backends. `simsys_s2.cc` is SDL2 (used on macOS), `simsys_w.cc` is GDI
  (the usual Windows build). `display/` — software renderer.
- `script/`, `squirrel/` — scenario/AI scripting. `simutrans/` — runtime data dir (config, text, pak).
- Build files: `Makefile` + `config.default` (untracked, ignored), `Simutrans.sln` and the
  `*.vcxproj` files for Windows, `nsis/` for the installer.

## Build and run on macOS (test only)

```
make -j12 && rm -f simutrans/simutrans && cp build/default/sim simutrans/simutrans
cd simutrans && ./simutrans -use_workdir -objects pak
```

- Remove the old binary before copying: overwriting it in place keeps the stale code-signature cache
  and macOS kills the new binary with SIGKILL (exit 137) at start.

- `config.default` uses `BACKEND=sdl2`, `OSTYPE=mac`, `AV_FOUNDATION=1`, `USE_FREETYPE=1` and
  `FLAGS = -I/opt/homebrew/include` (the code includes `SDL2/SDL.h`; `sdl2-config` alone is not enough).
- Homebrew `sdl2` is sdl2-compat over SDL3. With `SDL_WINDOW_ALLOW_HIGHDPI` it reports mouse
  coordinates in Retina pixels, so every click missed. The flag is commented out in
  `sys/simsys_s2.cc`; keep it that way. Rendering is unchanged (the game draws a 1x texture).
- pak64 for 122.0 lives in `simutrans/pak` (ignored by git). No `revision.h` is checked in; the
  build generates it.
- There is no test suite. Verification means building, launching, and exercising the change in-game.
  `tools/mac-test/` has helpers for that: `shot.sh` screenshots the game window and records its
  geometry, `wclick.sh X Y [left|middle|right]` clicks at content-relative coordinates, `click.swift`
  is the CGEvent tool behind it (build once with `swiftc`, binary is git-ignored). Read the
  screenshot with the image reader to navigate. See `tools/mac-test/README.md` for the SDL quirks
  (first click after focus is swallowed; a press needs a real cursor move before it).
- `simutrans/save/schedtest.sve` (ignored) is a saved game with one road line and a two-stop
  schedule (`testline.sve` has only an empty line); start with `-load schedtest` to test the
  schedule editor without building anything in-game. Saves also restore their open dialogs, so a
  schedule window that comes back with the save may hold an unsaved edited copy of the schedule.
- `-load NAME` looks in `<user dir>/save/`, which on macOS is `~/Library/Simutrans/save/` unless you
  pass `-singleuser` (then it is `simutrans/save/`). A save must match its pakset: the owner's
  pak128.cs saves need the Windows Steam pakset plus add-ons, so they do not load with the
  pak128.cs copies in OneDrive (the loader segfaults on missing objects, that is stock behaviour).
- `./cleanup_code.sh` also rewrites include guards and trailing whitespace in files upstream never
  cleaned. Run it, then `git checkout --` every file you did not touch, so commits stay focused.

## Save export and web viewer

`simutrans -load NAME -export FILE.json` loads a game with the normal loader, writes a JSON dump
(`dataobj/savegame_export.cc`, format described in `tools/saveviewer/README.md`) and quits; add
`SDL_VIDEODRIVER=dummy -nosound -nomidi` to run it headless on macOS. `tools/saveviewer/index.html`
is a standalone page that opens such a file (lines, stops, convoys, map, settings). Keep the JSON
keys stable: the exporter and the viewer are the two halves of one format.

## Windows is the real target

- The owner plays on Windows, through Steam, and Steam runs this fork (see the Windows section
  below). Anything touching input, windowing, sound or fonts must be reasoned about for
  `sys/simsys_w.cc` (GDI) too, not only the SDL2 path, and should be re-verified on Windows.
- Keep Windows build files in sync: a new `.cc` file must be added to the `Makefile` SOURCES list
  and to `Simutrans-Main.vcxitems`, or the Windows build silently lacks it.

## Conventions

- Tabs for C++ indentation (`.editorconfig`). Run `./cleanup_code.sh` before committing to fix
  trailing whitespace and include guards. Keep the two-space style inside `if(  cond  )` parentheses
  that the codebase uses.
- Names: classes end in `_t`, methods are `snake_case` verbs with `get_`/`set_`/`is_` prefixes.
  Many identifiers are German (`karte_t`, `grund_t`, `planquadrat_t`, `bauer`); use the existing
  name for an existing concept, English for new ones. See `documentation/coding_styles.txt`.
- Use the project's fixed-width types (`sint32`, `uint8`, ...) and containers from `tpl/`
  (`vector_tpl`, `slist_tpl`, `hashtable_tpl`), not std containers, in game code.
- User-visible strings go through `translator::translate("...")`; add the English key to
  `simutrans/text/en.tab` (and other languages only if you can translate them).
- Debug output goes through `dbg->message/warning/error` and the `DBG_*` macros, not `printf`.

## Things that break silently

- **Savegame format.** Anything serialised in an `rdwr()` method changes the save format. Guard new
  fields with a version check (`file->is_version_atleast(...)` / `get_version()`), bump the
  version in `simversion.h` only deliberately, and keep old saves loading.
- **Network games** require identical versions and settings on all clients; changes to
  `simversion.h`, `settings.cc` or step/sync_step logic affect that.
- **`settings.cc` / `simuconf.tab`.** New settings must be parsed in `parse_simuconf`, saved in
  `settings_t::rdwr`, and given a sane default so old configs keep working.
- **Descriptors (`descriptor/`) and `makeobj/`** define the pak file format. Changing them means
  regenerating or breaking every pak set; avoid unless the feature truly needs new pak data.
- `sync_step()` runs every frame with exclusive map access and must stay cheap; slower work
  belongs in `step()`.
- **`min()`/`max()` from `simtypes.h` take `int`.** The owner's main game has a tick counter past
  2^31, so `max( 1u, welt->get_ticks() )` returns 1 there. Compare `uint32` ticks by hand, and
  subtract ticks as `uint32` before widening (`(sint64)(now - since)`).

## Git

- Work on `main` (or feature branches off it). Do not commit `config.default`, `build/`,
  `simutrans/pak*`, `simutrans/save/` or the copied binary; they are ignored already.
- Never push tags or branches from `upstream` to `origin`; the fork's history is meant to stay small.
- One feature per commit with a `ADD:`/`FIX:`/`CHG:`/`CODE:` prefix, matching upstream style.

## World calendar (fork feature)

`minutes_per_month` in simuconf.tab (saved with the game, 0 = stock) puts a realistic clock on top
of the game months: `karte_t::get_calendar_minutes()` / `get_calendar_date()` in `simworld.cc` are
the single source for the status bar clock (`simintr.cc`), the day/night cycle (`display/simview.cc`),
the seasons (`recalc_season_snowline`, switchable with `calendar_seasons`) and the JSON export. The
economy still runs on game months; the status bar shows the game year next to the calendar date.
Schedule stops carry `waiting_time` in calendar minutes (savegame 122.1, `schedule_entry_t`), which
wins over the stock `waiting_time_shift` fraction when set. Saves written by the fork do not load in
the stock 122.0 exe; use `tools/windows/steam-fork.sh downgrade` for that.

Timetable (savegame 122.2, offsets list 122.4): a stop entry may have `departure_interval` and
`departure_offset` in calendar minutes, plus up to seven `extra_offsets` for lines that leave
several times per cycle (every 60 at 1, 11, 31, 41). Slots are `cycle * interval + offset` counted
from midnight, and stay open for half the gap to the next slot (`simline.cc` slot helpers,
`schedule_entry_t::get_departure_offsets` gives the sorted list), or for `departure_window` minutes
when set (savegame 122.9, `WINDOW_AUTO` = half the gap, `is_slot_open`; e.g. S6a and S6b every 60
but 30 apart need 15). The dialog keeps the single offset input and adds an "Also at (min)" text
field for the extras and a "Leave up to (min late)" row that shows the automatic value with "(half
the gap)" until set; changing the interval resets it to automatic. The entry list shows a set
window as `[1h+30' late 15']`, the export as `departure_window` (null = automatic).
The schedule dialog (convoy and line) keeps the current entry's settings in tabs above the always
visible entry list: Scheduling (timetable, calendar only, the default), Loading (full load, wait
time and the crowding checkboxes of the whole schedule), Coupling (rail only); the return ticket
button sits in the Add/Insert/Remove row. Every minute input has a "= 1h30" label (`show_minutes`). A convoy leaves only when its loading rules are met (minimum load or
maximum wait, unchanged) and a slot is open that no other convoy of the line used at that entry
(`simline_t::take_departure_slot`, `last_departure_slot` saved with the line; a schedule edit
keeps it, and the missed couplings, for the entries that stayed: `keep_slots_across_edit` matches
old and new entries as the longest run of the same stops in order, and resets an entry whose
interval or offsets changed); among ready convoys the earliest arrival goes first
(`convoi_t::arrived_before`, the same tick goes by convoy number). Fork saves keep `arrived_time`
also at stops without a waiting time (stock wrote a dummy there, so every train waiting at such a
stop came back with one tick); a slot already in `last_departure_slot` is never booked twice. Convoys without a line and
convoys with `no_load` ignore the timetable. The schedule dialog shows the two inputs only with the
calendar on and greys them out for line-less convoys; the entry list appends "(every N min, +M)".
A convoy that arrived after another convoy of its line at the same entry only unloads until
that one has left (`count_earlier_waiting`), so passengers board the train that leaves first;
the convoy window shows "Departure: HH:MM (in N min), K ahead" from `get_planned_departure`.
Below it, "Delay: N min" or "On time": how many minutes after its slot the convoy left the last
stop with a timetable (`convoi_t::departure_delay`, set where `hat_gehalten` books the slot, i.e. at
the end of loading; a coupled joined train counts from its own line's slot, or takes the primary's
delay when its entry has a timetable but no free slot). Stops without a timetable keep it; only a
new schedule with other entries or a line change clears it (`set_schedule`,
`check_pending_updates`). Since only an open slot can be booked, it stays within the departure
window except when running late after a missed coupling. Saved (122.11), export and viewer
`departure_delay`. The row is hidden while the departure row is shown. It shows up to 1 min as "On
time" (the value stays exact), and "Missed its slot" instead (on the departure row as ", missed its
slot" when that one is shown) while the convoy waits at a timetabled stop whose last slot has closed with no train
of the line leaving in it (`simline_t::is_last_slot_missed`, `convoi_t::has_missed_slot`; not for
a train running late or holding a slot for its partner, nor before the line's first departure there).
A train that came after the window cannot tell late for that slot from early for the next, so with
more slots than trains every unused slot counts, e.g. one train on a 20 minute timetable shows it
until the next slot opens. `book_departure_slot` only moves `last_departure_slot` forwards (a train
that held an older slot for its partner used to make a later, used slot look free).
The label over a train (`show_vehicle_states`, `vehicle_t::display_after`) shows the same texts:
loading plus coupling wait or late running plus departure, and at a signal the passing-train or
single-track reason instead of "Waiting for clearance" (`convoi_t::append_departure_text`,
`append_wait_reason`, shared with the convoy window).
Not done yet: the stop departure boards still estimate from arrival plus wait.

Stop types (savegame 122.3, `schedule_entry_t::stop_type`): Regular, Terminal (everything off,
then load; transfers allowed, nothing rides through), All off (everything off, no loading), Only
load (no unloading, the planner never routes cargo to it), Only unload (no loading, the planner
never routes cargo from it), Hold (122.5: no loading or unloading, the planner ignores it, cargo
rides through; a stop only to let passing trains overtake, see below). The rules live in the helpers on `schedule_entry_t` (`loads`,
`unloads`, `rides_through`, `plans_arrival`, `plans_departure`); `haltestelle_t::rebuild_connections`
applies them when it walks a schedule: a Terminal or All off entry blocks the walk, so no edges are
added until the next entry of the home halt, which is what stops the planner from routing anyone
through a terminus (A-B-C-E, then E-D-B-A: nobody boards at B for D, they change instead).
`convoi_t::hat_gehalten` applies them when stopping. The schedule dialog shows the type as a
lettered badge in front of each entry row; left click cycles forward, right click back. A single
terminus entry with a timetable is Terminal; a bus station with arrival, waiting and departure
tiles of one halt uses All off on the arrival tile and Regular on the rest.

## Passing standing buses (fork feature)

Road vehicles (convoys and city cars) pass a convoy standing at a stop (`convoi_t::is_standing()`:
LOADING, ROUTING_1 right after loading, NO_ROUTE) regardless of bends, junctions or rail crossings
before or after it. Only the tiles alongside it must be free and not junctions/crossings, and the route
must go on at least one tile beyond it (`get_tiles_to_pass_standing` in `simconvoi.cc` and
`simroadtraffic.cc`, `vehicle_base_t::is_free_for_passing`). The overtaking lane is used only alongside
it (`overtaker_t::set_tiles_passing_standing`); the tile after it is checked like normal driving
(`is_passing_standing_last_tile`), so junction rules and `request_crossing` still apply there. A vehicle
may enter a junction when the car blocking its exit is a standing convoy it can pass. Choose signs first
search for a free stop tile beyond standing convoys (`road_vehicle_t::choose_pass_standing` in
`is_target`), then fall back to the stock nearest free tile. Moving overtaking is unchanged. Not saved:
a game loaded mid-pass finishes it like a stock overtake.

## Overtaking at choose signals (fork feature)

Stock rail choose signal: a train whose next stop lies in the choose area picks a free platform; a train
that meets an end-of-choose sign (or a second choose signal) before its stop treats it as a plain signal.
The fork adds one case to the second: when the plain reservation fails, the train is not stopping in the
area and it meets an end-of-choose sign first (`rail_vehicle_t::get_choose_detour_end`), it may take any
free track through the area to that sign, if the train in its way is standing (`is_standing()` or
`is_waiting()`) or running but stopping at a station on its path through the area. A running train that
does not stop there is never overtaken. `reserve_choose_detour` searches from the signal tile over
unreserved track only (`detour_*` members switch `check_next_tile`/`is_target` into that mode), must
reach the end-of-choose tile from the planned side, at most 1.5x the planned length + 4 tiles. It then
reserves the detour through all its signals plus the block after the sign in one `block_reserver` call,
and only on success swaps it into the route (rest of the route unchanged); otherwise the signal stays
red. A schedule waypoint inside the area disables it, one beyond does not. The search runs in `step()`,
so a train approaching the signal brakes for it and pauses there for one step (`restart_speed = -1` keeps its speed).
No save format change: after loading, the train re-reserves along the saved detour route. Layout: choose
signal as the entry signal, end-of-choose sign after the exit switches where the tracks rejoin. Tested
headless on pak64 (standing, running-stopping, running-through, both tracks taken, exit blocked,
save/load mid-detour, waypoints) with a throwaway harness; re-verify in the Windows game.

Stepping aside (no save change): a stopping train at a choose signal whose planned platform lies on
the way of a train that will pass it there takes a free platform off that way instead, so the fast
train runs straight through on its main track (`get_overtaker_ways`, `is_choose_signal_clear`). Such
a train is running (a train standing at a stop has no route past it yet), has our signal tile and our
next tile ahead on its route, then an end of choose or the next signal that applies with its route
going on (it does not stop in the area), and reaches it before we would leave: our arrival plus the
maximum wait (with a minimum load; a month without one) or the timetable slot, plus `passing_hold_minutes`; needs the
calendar. Its way from the signal to there must miss every tile we would stand on. A candidate must
also be a track of our direction (`leads_back_to_overtaker`): every branch walked forward from its
stop position, never back towards the entry, runs back into that train's way or its next 128 route
tiles, or ends; a one-way signal or `LT` against us, another halt or 128 tiles means the other
direction's main track of a double-track station, which is never taken. Types, bays, the way on and
the shortest-fit rule apply as before. Nothing off its way free: exactly as before (planned platform,
then any), and the train overtakes through the other track. The search needs a step, so such a
train pauses at the signal for one. Tested headless (pak128.cs): main plus loop, a ladder with two
loops, double track with crossovers only (keeps its main), express too far behind, timetable dwell
alone, loop taken; the 33 rail regression and 12 overtaking scenarios unchanged.

## Platform types, Hold and waiting for passing trains (fork feature, savegame 122.5)

All rail only, in `vehicle/simvehicle.cc` unless noted.
- Platform types: a train stopping in a choose area only takes platforms whose station building
  enables what it carries (`get_platform_needs`: passengers or mail need PAX|POST, anything else WARE;
  no capacity or a Hold stop = any; a station without such a platform on that track long enough for
  the train, counted as suitable tiles in a row, = any). Every
  tile the train stands on must suit (`is_platform_suitable` in `is_stop_position`); an unsuitable
  planned platform is searched away even when free (`is_planned_platform_suitable`).
- Hold stop type (H badge): see Stop types. The train leaves right away unless it waits as below.
- Waiting for passing trains: in `can_enter_tile` for CAN_START, a train at a halt inside a choose
  area (an end-of-choose sign ahead on its route before any choose signal) waits while
  `get_passing_train` finds a train that runs past that end-of-choose sign in the same direction,
  entered the area through a choose signal, is not itself standing at a stop in the area, and would
  reach the sign within `passing_hold_minutes` calendar minutes at its top speed (the departure
  board's tiles<<20/speed estimate). Passenger or mail trains wait only for passenger or mail trains.
  It stops waiting when that train is through, after `passing_hold_max_minutes` at the stop, or at once
  when any train stands at red (speed 0) at a choose signal leading into this halt or through this
  area (it could not get past, so waiting would only block it). The state lives in `convoi_t`
  (`passing_hold_for/_since/_released`, not saved), the convoy window shows "Waiting for X to pass".
  The departure slot of a timetable is taken before (end of loading), so a held train leaves late but
  keeps its slot. Settings in simuconf.tab, saved with the game (defaults 5 and 20, needs the
  calendar: without it `calendar_minutes_to_ticks` is 0 and nobody waits).
  Mixed layouts (Blažovice: single-track lines in through `LT`s, double-track exit with an `E`): a
  train entering the area through a station boundary that applies to it counts as passing too, if it
  holds a claim there, has that `LT` tile reserved (close to it the claim turns into a reservation) or
  is past it, and its way to the `E` does not run over the tiles the waiting train stands on. A train
  standing with a section wait for a track at this halt (TRACK, LAST_TRACK) also ends the wait. A held
  train clears its section wait (else a train at the signal ahead yields to it: `yields_to_waiting`).
  The `E` must come before the second signal after the stop (the walk stops there: at Blažovice the
  dwarf signal at 4752,3427 stands in front of it). No waiting at a single-track exit (own `LT` as the
  merge point was tried: it hands the line to opposing trains and everyone loses in two-way traffic).
- Hold marker (convoy and line, `convoi_t::hold_marker` / `simline_t::hold_marker`, tool commands
  convoy `h` and line `h,id,0|1`, buttons in the convoy window and line management): at a choose
  signal of an area it passes without stopping, a marked train whose passing train is coming
  (same finder, which must still have our signal tile ahead) takes a free platform of any of our
  halts before the end-of-choose sign, first off its planned way (`reserve_hold_platform`,
  `hold_search` 1 then 2). The platform must lead on forward to that end-of-choose sign
  (`has_onward_path`: a second search over any track, reserved or not, since the passing train may
  already hold part of it); a bay or dead-end platform is excluded and the next one tried, up to four
  per pass. Then it sets `convoi_t::hold_divert` (saved). Arriving there
  (`ziel_erreicht`) is no schedule stop: it goes to ROUTING_1, gets a new route to its unchanged next
  stop, and waits as above. `drive_to` clears `hold_divert`. No diversion while a schedule waypoint
  is pending (the new route would skip it). It diverts only when a passing train is actually coming,
  not at every station.
Tested headless on pak64 with the throwaway harness (split platforms, Hold stop, local waits for an
express, local ignores fast freight, freight waits for freight, both tracks held and the express stuck
at red releases both, marked convoy and marked line divert, save/load mid-diversion, 20 minute limit,
no hold outside a choose area, plus the earlier overtaking cases).

## Mixed traction (fork feature)

A convoy with electric engines and other engines (diesel, steam, ...) needs no catenary
(`convoi_t::needs_electrification()` is true only when all engines are electric). The electric
engines pull only while every one of them is on a tile with catenary; otherwise only the other
engines pull. Under wires the other engines pull too only when that gives a higher top speed with
the current load (`traction_both_under_wire`, chosen in `recalc_traction(true)` from
`calc_speedbonus_kmh`, i.e. at start and at every departure). An idle engine adds no power, no
running cost, no top speed limit, no smoke (`vehicle_t::idle`) and no start sound
(`play_start_sound`); its fixed cost stays. `convoi_t::recalc_traction` sets `sum_gear_and_power`,
`min_top_speed`, `sum_running_costs`, `is_electric` and (road convoys) the overtaking speed
`max_power_speed` for the current mode; `vehicle_t::hop` calls
it when an electric engine of a mixed convoy enters or leaves catenary (`on_wire`). The bonus speed
assumes the better choice under wires; `add_running_cost` credits each tile with the speed the
pulling engines reach with the departure load (`traction_power_speed_*`), so slow unwired sections
lower the average. Minimum speed signs are checked per tile with `get_traction_top_speed(electrified)`.
Nothing is saved: the mode is rebuilt on load. The depot offers electric vehicles in a depot without
catenary once the convoy has another engine and shows the off-wire power and speed; the convoy
details show installed and pulling power and the mode and mark idle engines; the JSON export has `traction` per convoy and
`idle` per vehicle. Convoys with only one kind of engine behave as stock.

## Coupling trains (fork feature, savegame 122.6)

Two lines share a trunk with one train, like R12 Brno -> Zabreh -> Sumperk / Jesenik: R12a and R12b
keep their full schedules, and a schedule entry of R12b says "Couple with R12a, wait up to N min"
(`schedule_entry_t::couple_line_id` / `couple_max_wait`, rail schedules only; schedule dialog row
"Couple with", entry list shows `[+ R12a]`). R12a is the primary, R12b the joining train; the
primary line carries no setting, it learns from the stop's `registered_lines`. All logic is in
`simconvoi.cc` (block "Fork: coupling of trains") plus hooks in `vehicle/simvehicle.cc`.
- When: both trains stand at the stop of that entry and both schedules go on to the same next stop
  (`can_couple_here`, waypoints skipped). One partner per train. Whichever train is ready first waits
  up to the max wait (`expects_partner`, counted from when it would otherwise leave; the timetable
  slot is held in `couple_hold_slot`, so it leaves late but in its slot). Needs the calendar.
- Getting next to each other: `cut_route_before_partner`, called from `block_reserver`, cuts a
  train's route right before the tiles of its partner standing at its next stop, so a signal that is
  red only because of the partner turns green and the train stops behind it (or head to head). At a
  choose signal `reserve_to_partner` routes to the partner's platform (`couple_search` mode of
  `check_next_tile`/`is_target`); a partner still running in (route reserved into the stop) makes
  the train wait at red and try again; no way there falls back to the stock choice.
  Single-track stations (`P`/`LT`): at the `P` (only when the partner already stands at the stop
  the train goes to now, i.e. no schedule stop in between) and again at the `LT` (the partner may
  have come after the claim) `find_partner_track` claims the platform right behind the standing
  partner (`couple_in_station`: the search does not pass a signal or boundary that applies; no
  platform tile left behind it, or its track taken: the stock choice). At the `LT` a train also waits
  while its partner is DRIVING into the stop (at most 30 calendar minutes, `section_waited_long`).
- Different platforms of the same stop (no choose signal, or no way over): they couple anyway. The
  joining train's vehicles move to the track behind the primary, the way the primary came in
  (`couple`, needs those tiles free of other trains and long enough); otherwise they keep waiting.
  Not realistic, but it keeps a missed platform from breaking the pair.
- Every wait is bounded (max wait at the stop; red at a choose signal only while the partner's route
  already leads into the stop), so a pair that cannot get together runs separately. Two warnings in
  the message window: both stood at the stop but could not couple (no free track behind the
  primary), and a train that cannot reappear after uncoupling for 30 calendar minutes (1/8 month
  without the calendar) because its platform stays occupied (`uncouple_since`, not saved).
- Joining (`couple`, from `laden()` when both stand at the stop): the two rows of tiles
  become one, the primary's vehicles first, then the joining train's, laid out anew along it in the
  primary's direction (`lay_out_on_route( true )`: the front stays where the front train stood, halfway through its tile heading north or west as `hop()` stops it, the rest packed behind; the stock reversal code packs from the rear instead), all tiles reserved for the
  primary. Revenue for the trip in is booked before the move, and moving vehicles count as no
  trip (`last_stop_pos` is reset). The joining train goes to state COUPLED: out of the sync list, its `fahr` still points to
  its vehicles (for its window, finances, save) but the vehicles belong to the primary
  (`coupled_first`, `get_vehicle_owner`, `get_own_vehicle_count`). Fixed costs, goods categories,
  revenue, running costs (and their share of way tolls), distance, average speed and transported
  goods stay with each train and line; at every stop each part loads for its own schedule (portions
  in `hat_gehalten`), and the joined train's minimum load and maximum wait (counted from its own
  arrival) hold the whole train too.
  At most 255 vehicles together (the vehicle count is a uint8). A primary's load
  (`calc_loading`), sale value (`calc_restwert`) and purchase cost count only its own vehicles. The joined train's schedule follows
  (`follow_to_stop` on arrival, `advance` on departure and at each of its waypoints the pair
  passes (`vehicle_t::hop`), its open timetable slot is marked used too, unless a train of that line that came first can take it;
  the joined line's timetable never holds the coupled train). After a schedule change of either
  train while coupled (a line edit, or the primary's own schedule sending it elsewhere),
  `resync_coupled_schedule` points the joined train at its entry of the stop the primary is at or
  goes to that leads on to the same next stop, so only edits that really make the schedules differ
  part the pair.
- Line edits (any train): `check_pending_updates` keeps a train's target while that stop is still
  in the schedule in the same place (the stop before or after it unchanged, waypoints ignored),
  else goes to the first following stop still there (`find_matching_entry`). The stock scoring
  compared entries at fixed distances, so e.g. a waypoint inserted at the start while a train went
  to one of the last stops made it skip that stop.
- Parting (`uncouple_here`): at the first stop where the next stops differ (checked at departure),
  or on arrival at a stop the joined train's schedule skips. The joined train goes off the map
  (UNCOUPLING) and remembers the tiles of the whole train (`uncouple_span`); the primary leaves on
  green, hands each span tile over as its last vehicle leaves it (`handover_tile` from
  `rail_vehicle_t::leave_tile`, `move_to` keeps span tiles), and tiles it will not drive through are
  taken in `step_uncoupling`. When all are ours, the train appears at the rear of the span and loads
  there as a train of its own. Nothing else can take the platform in between, so single track with
  full stop signals behaves like two stock trains.
- Missed coupling: a primary that leaves alone after the max wait gives its slot to the joining line
  (`simline_t::add_missed_coupling`); the next train of that line arriving alone at that entry takes
  it (`late_slot`, `running_late`), does not wait for the gone partner, and leaves at once. While
  late it takes the oldest unused due slot at timetabled stops (`get_late_departure_slot`) until a
  slot lies ahead again or it couples; spare time at the branch terminus recovers it.
- Other: depot entry of a coupled primary takes both trains in as two convoys; deleting either
  train keeps the other (a deleted primary leaves the joined train standing where it is); the joined
  train's schedule window is refused while coupled; the convoy window shows
  "Uncoupled, waiting for the platform", waiting for the partner, "Running late"; the departure
  board lists the joined train's destination with the primary's time; JSON export `coupling`,
  `coupled_with`, `running_late` per convoy (each lists only its own vehicles) and `couple_line_id`,
  `couple_max_wait` per schedule entry.
- Convoy window of either train while coupled: a panel under the destination names the other
  train (in front/behind, button opens its window, names cut to 48 characters), its line, where
  the two part (`convoi_t::get_uncouple_halt`, the laden()/follow_to_stop() rules played forward
  over both schedules; "Stays coupled" if they never part) and its load. The Freight tab lists
  this train's freight, then the other's (`build_freight_info`, uncached, since the other train's
  own window uses its cache; `get_freight_info` counts own vehicles only). Vehicle Details lists
  the whole train front first with a heading per part ("This train:", "Coupled train:").
- Saved (122.6): the entry fields, both handles, `coupled_first`, `handover_to`, the span, the wait
  and late state, the line's missed slots. The primary saves its own vehicles and the joined train
  its own; `finish_rd` of the primary joins them again. `-saveversion 0.122.0` writes a joined
  train as a train of its own (state ROUTING_1) behind the primary; one waiting to reappear keeps
  its old tile positions (rare, short window).
Tested headless on pak64 with a throwaway harness (not committed): couple, run, split, rejoin,
terminus reversal while coupled, the primary turning back through the span, choose signals with the
partner on the other platform, coupling across platforms without choose signals, missed coupling with timetable and late running, save/load while
coupled, uncoupling and waiting, downgrade, deleting either train, depot entry. The GUI parts
(schedule dialog row, convoy window, departure board) are compiled but not seen on screen.

## Platform signals and station boundaries (fork feature, savegame 122.7)

Design, station layouts and test results: `documentation/fork-rail-signalling-plan.md`. Two new pak
objects (roadsign flags `PLATFORM_SIGNAL`, `STATION_BOUNDARY`; makeobj keys `is_platformsignal=1`
with `is_signal=1`, and `station_boundary=1`; art and pak64 placeholders in `tools/fork-signals`,
`steam-fork.sh signals FILE.dat` builds pak128 ones into the pakset folder). The owner's pak128.cs
objects come from the pakset repo (`VZ-Signals-rail.pak`, built with the fork's makeobj): `P` as
`VZ-Signals-D3-P` and `VZ-Signals-D1-{New,Old,Dwarf}-P`, `LT` as `VZ-Signals-D3-LT` and
`VZ-Signals-D1-{New,Old,Dwarf}-LT`; a `P` has 8 images (16 with a wired set), an `LT` exactly 4
(more would make it a traffic light). An `LT` applies to the direction its image shows, like a signal:
`roadsign_t::applies_to` reads its stored direction as the one it applies to (a stock one-way sign's
is the blocked one), so an `LT` that looks right is right; its art keeps the signal image layout
(see `tools/fork-signals/README.md`):
- Platform signal `P`: one-way exit signal at each end of a station track. It applies only to trains
  leaving in its direction (`roadsign_t::applies_to`) and sets no ribi mask, so the track stays
  two-way. Rail code asks `rail_vehicle_t::is_stop_point` (a signal that applies, or a station
  boundary entered) wherever stock code asked `has_signal()`: `block_reserver`, `can_enter_tile`,
  the PR #1 detour signal count. A `P` acts as a normal signal unless the train leaves the station
  through an `LT` before any other signal: then `is_platform_signal_clear` follows the route and the
  next schedule legs (halts without signals included) to the `LT` of the next station, checks all
  those tiles free (no end within a schedule cycle: stays red), picks a track there with `find_station_track` (stopping: a stop position of the
  right platform type that leads on to the next stop; passing: a free track up to a `P` from which
  the route goes on; planned track first; searches never pass a signal that applies) and claims it.
- Station boundary `LT`: sign outside the outermost switch where a single-track line enters a
  station, facing entering trains. A train reserves only up to it; at the `LT`
  (`is_station_boundary_clear`) it reserves the throat into its claimed track, or waits there
  briefly. Without a claim it chooses a track there.
- Claim (`convoi_t::claim_path`, `claim_first`, `claim_stops`, saved 122.7, reserved again in
  `finish_rd`): the path from the `LT` through the chosen track; only its platform tiles are
  reserved. `drive_to` routes through it (`route_via_claim`) and releases it when `claim_stop` (the
  schedule stop entering that station) left the schedule, its way misses the `LT`, or there is no
  route; `leave_tile` keeps it on a track the train is leaving, depot/destroy release it, a `P` or
  `LT` of another station drops a stale one.
- Choose signals: the walk also ends at an `LT` and at a `P` that applies before the stop; then a
  passing train takes any free track up to its `P` (through-choose) instead of the PR #1 detour.
  A stopping train's choice (stock search and `find_station_track`) takes a platform other than
  the planned one only if its way on to the next stop is at most 1.5x + 8 tiles of the planned
  one's (`leads_on_like_planned`): `P` leaves tracks two-way, so without it a train could take the
  other side's platform of a station with a crossover in one throat only and have to go back.
  A platform whose way on turns off at a switch and then runs over another platform of the same
  stop (`runs_over_other_platform`, `ONWARD_CROSSING`; a far-side track whose line on leaves only
  from the others, e.g. Olomouc hl.n. 5172 for trains turning back to Bystrovany) is taken only
  when no other is free: a second pass inside each bay phase of the bays-first order, so no new
  waits. Tram and road stops of a combined halt do not count as platforms there.
  Within each of those passes a stopping train takes the shortest free platform that fits it, not
  the nearest (`get_found_platform_length`: suitable halt tiles in a row back from the stop
  position; ties go to the first found), so long platforms stay free for long trains. "Fits" is
  `convoi_t::get_platform_length_needed`: its own length, plus its coupling partner's when it
  couples at that stop (the partner at the stop or on its way in, else the longest train of the
  lines it couples with there); if no platform holds both, the longest one. Each found platform
  goes to `track_search_taken` (no target any more, but the search still runs through it, unlike
  `track_search_excluded`) and the search runs again until none is left or one fits exactly; a
  platform no better than the best so far skips the way-on check. Only when the planned platform
  is not free or not suitable. Tested headless (base took the first 8-tile platform, new the 6-,
  3- or 2-tile one; a coupling pair of 2+2 tiles the 8-tile one, not the 3-tile one), the
  regression set unchanged.
  Both platform searches (stock choose, `find_station_track` for a stopping train) and the
  `can_enter_without_turning` probe first run with
  `stop_search_halt` set: `rail_vehicle_t::get_ribi` never lets them run on out of a platform's far
  end. `route_t::find_route` enters each tile only once, so a way through a goods platform, round the
  throat and back could reach a free platform from behind and close its stop tile before the way in
  from the front got there (Bylnice: a train red at a choose signal beside a free platform). If that
  finds nothing, the old search runs (a platform reached only through another one).
  Those searches judge a signal or boundary by the direction the train leaves its tile
  (`get_exit_dir`), as `is_stop_point` and `roadsign_t::applies_to` do; on a curve that is not the
  direction it came in (Holubice: a passing train took a `P` on a throat curve as its track's end,
  its way on ran into an occupied platform and it stood at the `LT` for good).
  `P` at both ends of every track is the normal exit signal of double-track stations too (plan 3.7.1).
  The PR #1 hold walk stops at an `LT` and after the first signal past the train's exit signal.
- Schedule waypoints: the route runs through the waypoints ahead up to the next stop (stock
  `drive_to`), and every track choice at a `P` or `LT` keeps them (`find_station_track`). A
  stopping train keeps the way up to the last waypoint (`get_last_waypoint_index`) and searches a
  free platform only from there. A passing train with a waypoint on its planned track takes only
  that track; from another track the way on goes through the waypoints beyond it
  (`convoi_t::calc_route_on`, also in `route_via_claim` and `route_through`). Platform type, next
  stop and `claim_stop` come from the stop the route ends at (`get_route_entry`), not from the
  waypoint entry. Before, a busy planned track sent the train the shortest way on and it skipped
  them (R8/R12 passing Blazovice skipped the three waypoints to Brno hl.n.).
- A save for an older version (0.122.6 for the previous fork build, 0.122.0 for the stock game)
  writes `P` two-way (older builds treat it as a plain signal, and a one-way one would make the
  track one-way) and leaves out claims. Such a file is for the older exe only: loaded back into
  this build the `P` stay two-way and trains stick at them, so keep the 0.122.7 save. Convoy window: "Waiting for the single track", "No free track at X", "Waiting to
  enter the station". Export: optional `claim_boundary` on convoys.
- Last free track (no save change, plan 3.8): at a `P`, a train about to claim the last free track
  of the next station X (`keeps_last_track`) first checks that some train at X could still leave
  it afterwards, the newcomer included: it leaves without a single-track section, already holds
  its claim elsewhere, or its next station (`get_section_after`, found through the station
  boundaries) has a free track or is the one the newcomer leaves; one station further (two-step
  look, `station_can_release` depth 1) the same for the trains there. Otherwise the `P` stays red
  ("Keeping the last track at X free", `SECTION_WAIT_LAST_TRACK`). A station's tracks
  (`get_station_tracks`) are its platform rows plus the plain track to the next switch; a track is
  free when no tile is reserved, and a train counts as leaving only if it is alone on a track (short
  trains share platforms). After 30 calendar minutes at the `P` (1/8 month without the calendar)
  the look goes through all stations, so the rule never holds everyone up by itself.
- Bay platforms (plan 3.10, no save change): a platform whose track ends in a buffer stop (or a
  depot) before any switch or station boundary (`is_bay_tile`) is used only by trains that turn back
  there (`reverses_at_stop`: the way on to the next stop starts back the way the train came, judged
  from the last switch before the stop when the stop itself is a bay, and only if the way on from there
  runs over another platform of the stop: a train out of a depot on a spur at that switch turns back).
  `is_platform_suitable` refuses bays to trains that run through (`bay_search` 1, also a clicked bay; a
  station with only bays is not restricted, and tram or road stops of the halt are no through
  platforms). A train turning back keeps its planned platform when free, else searches bays first
  (`bay_search` 2), then any track; stock choose search and `find_station_track` alike. In the last
  free track rule a free bay counts only at a station of bays only, or in `station_can_release` for a
  train that comes back from that station. Directed bays: no platform choice takes a way in that runs
  through the station the other way first (`turns_round_through`; a bay facing away, or a track from
  the far side, reached by U-turning over the far throat); a clicked stop reached that way is refused
  too unless the halt has no other way in, taken or not (`can_enter_without_turning`, `turn_probe`).
- Lock warning: a train waiting 30 minutes for a track (`check_section_lock`) checks the same way
  whether the stations are really locked and posts one message naming them ("Trains are locked
  up at ..."). What still locks: more trains than a group of stations can hold, e.g. trains coming
  out of a depot inside a station; timetable, fewer trains or more tracks.
- Block posts (hradlo, savegame 122.10, plan 3.11): signal flag `BLOCK_POST` (makeobj
  `is_blockpost=1`, owner's objects `VZ-Signals-D1-New-AH` and `VZ-Signals-D1-Old-Hradlo`, test
  placeholder `BlockPost` in `tools/fork-signals`), one-way like `P`, placed as a pair on the open
  single track. A train that gets green at a `P` in section mode keeps a section record
  (`convoi_t::section_from`/`section_to`, the boundaries it leaves and enters, saved, cleared at that
  `LT`, depot, deletion); only then does a post stop it (`is_stop_point`, `is_block_post_clear`:
  reserve the next block). `signal_applies` is false for posts, so every search ignores them, and
  trains without a record (stock/long-block/depot exits, old saves) hold the whole line as before.
  At a `P` the line up to the first post must be free; beyond it only trains `may_follow` accepts
  (same `section_to`, claim held at that boundary; a train turning back at a halt on the line only
  once it has turned, i.e. the rest of its route enters that boundary); the follower
  claims its own track (last free track rule unchanged). A train partly past the far `LT` is not
  followed. Trains from a junction on the line into the same `LT` follow each other too.
  `find_partner_at` does not wait for a partner behind us in the same section (it may wait at the
  post for us). The record is cleared at the `LT` before reserving into the station. Block 1 must
  hold the longest train.
- Longest waiter first (plan 3.12, every single-track line): a train about to enter at a `P` waits
  ("Letting a train from X through", `SECTION_WAIT_YIELD`) while another train still standing at its
  signal has waited `block_yield_minutes` (simuconf, default 10, saved) for the line itself
  (`SECTION_WAIT_LINE`, not for a track), started first, and would enter through our exit boundary or
  leave through it with the tile that stops it on our way (`section_wait_tile`, so not a train
  blocked on another branch) or into the same station boundary as we do (the tile may be in our
  station's throat, off our way, e.g. a train coming in over the other platform's switch). A tile off
  our way held by us or by a LOADING train does not count, at either end (it could not go first)
  (`yields_to_waiting`, loops over all convoys only when a train is about
  to get green or is yielding). A follower that cannot claim waits as `SECTION_WAIT_TRACK`. Not after it waited 4 hours more, except followers never go while one waits
  at the other end. `section_wait`, its start and boundaries are saved (122.10); `drive_to`, depot
  and deletion reset them. Saves for < 122.10 leave the posts out (`objlist_t::rdwr`; an older exe
  would make the pair one-way and close the line). `roadsign_t::rdwr` sets `dir` only when loading,
  so writing a 0.122.0 copy no longer turns the live `P` two-way.
  Tested headless (pak128.cs, placeholder post as an add-on): following, two posts, turners in
  either block, long dwell in block 2, coupling in both orders, a junction in block 2, save/load,
  downgrades; the 33 earlier rail scenarios unchanged except for ordering by the fairness rule.
  Export: `waiting_for.reason` `block_post`, `yielding` (with `halt_id`).
- Entry signal aspects (no save change, display only): an `LT` object with 8 images (red, green)
  or 12 (red, green, yellow; 16/24 add a set for catenary) is drawn as an entry signal
  (`roadsign_t::shows_aspects`, still a road sign, not a traffic light: `is_traffic_light` excludes
  station boundaries); a choose signal with exactly 12 images gets yellow too (`has_yellow_aspect`;
  pak128.cs's 20/24-image choose signals keep the stock layout). `rail_vehicle_t::update_boundary_aspect`
  sets an `LT` from the train that reserved its tile (let in: its reserved way; coming with a claim
  at this `LT` and not waiting: its claim), else from the nearest train holding a claim there, else
  red; called from `set_claim`/`release_claim`, after `is_station_boundary_clear`, when the last
  vehicle leaves the tile, on unreserving it and in `convoi_t::finish_rd`. So it turns green when
  the train gets its `P` at the previous station, red while it waits at the `LT` and after it.
  A choose signal gets its aspect after `is_choose_signal_clear`. Yellow (`get_way_aspect`):
  simuconf `yellow_aspect_rule` 0 (default, `env_t`, not saved) = a stopping train goes to another
  platform than its schedule stop (`reaches_stop_platform`); 1, and trains that do not stop at the
  end of the way = it takes a switch to the side (`takes_diverging_switch`, a turn at a tile with
  3+ directions, zigzags of a diagonal line excepted). Nothing reads the state for driving. The
  state byte is saved as for every road sign and kept on load for these signs. Art: the D1
  `VZ-Signals-D1-{New,Old,Dwarf}-LT` and `-Choose` objects of the pakset (two yellows on New/Old).
- Held at the platform (no save change): a train starting from a halt whose first stop point on the
  route is a `P` applying to it, reached over its own track (no switch, crossing, depot or other halt
  in between, `rail_vehicle_t::get_platform_exit_signal`; the head may stand on the `P` tile), asks
  that `P` (`is_signal_clear`) from its stop position in the CAN_START branch of `can_enter_tile`
  instead of creeping up to it. Red: it stays there in CAN_START with `convoi_t::platform_hold`
  (not saved, cleared at every poll, set again while red) and keeps boarding for the next stops it
  loaded for (`load_while_held`: `leaving_halts`, vehicles on halt tiles only, seats then standing if
  allowed, never overcrowded, capacity statistic corrected); if that filled it, `mark_missed_after_hold`
  flags who is left at the real start. Other trains leaving first may take over its boarded passengers
  (`take_boarded_passengers` also scans held trains, departure unknown = "we are sooner"). Not while
  leaving a depot or handing the platform over after uncoupling (`may_hold_at_platform`). Displays:
  "Held at the platform, boarding (62%): <section reason>" or "...: Exit signal red" over the train and
  in the convoy window (`append_wait_reason`), route bar "waiting", the stop's departure board lists
  it "(held at the platform)" (`add_held_departure`, it left the stop by its schedule), export
  `waiting_for.held_at_platform` and reason `exit_signal`. Trains whose first stop point is not such
  a `P` behave as before.

## Autoblock signals (fork feature, no save change)

Signal flag `AUTOBLOCK` (makeobj `is_autoblock=1` with `is_signal=1`, 8 or 16 images like a plain
signal); the owner's `VZ-Signals-D1-{New,Old,Dwarf}-Block` become autoblocks (new plain
`-Main` objects take their old art). Display only: it drives, reserves and makes the track one-way
exactly like a stock signal (`is_simple_signal` stays true), since trains stop when `block_reserver`
fails, never on a signal's state; the tool places it one-way only (like `P`). Aspect
(`signal_t::refresh_autoblock`, obj/signal.cc): green when the train holding the signal's own tile
has reserved on into the block (as stock sets it), else green unless a tile of its block, from the
signal in the direction it applies to up to and including the next signal or station boundary that
applies (every switch branch except legs against a one-way signal, at most 256 tiles), is reserved
by another train (so a train standing in a siding of the block keeps it red until one passes). Event-driven, nothing per step: the last vehicle leaving an autoblock refreshes it
and leaving any signal or `LT` walks back to the autoblocks whose block ends there
(`refresh_autoblocks_behind`, `rail_vehicle_t::leave_tile`); `block_reserver` refreshes the autoblocks
whose reservation it frees (instead of setting them red) and, after reserving over a switch, the
autoblocks on its other branches; a train entering a depot or deleted (`refresh_autoblocks_around`
from its front and rear tiles); placing or turning one; after loading, `refresh_loaded_autoblocks`
once the convoys reserved their routes again (`finish_rd` lists them). `signal_t::any_autoblock` keeps
all of it off until one exists. Stale until the next train: coupling, a train leaving a depot inside
a block, reservations made by `reserve_route`. No yellow (the next plain signal is red most of the
time). A stock exe reads them as plain signals. Tested headless (harness with an `abcheck` that
recomputes every aspect each step): chains, following trains, a branch joining, sidings, depot,
deletion, save/load, the real pak; driving identical to plain signals, about 2% slower with 110
autoblocks on a line, the rail regression set byte-identical.

## Standing and overcrowded passengers (fork feature, savegame 122.8)

A passenger vehicle (`vehicle_t::can_carry_crowd`) takes up to its seats (`get_cargo_max`, stock),
then standing passengers up to 2x (`get_standing_max`), then overcrowded ones up to 6.5x
(`get_overcrowded_max`, capped at 65535). `convoi_t::hat_gehalten` loads the seats of all vehicles
first (stock pass), then standing places of all vehicles once no seat of that part of the train
(coupling portion) is free, then overcrowded places once no standing place is free. Overcrowded
places take only waiting packets with `ware_t::missed_connection`: when a passenger train leaves
with no seat and no standing place free, `haltestelle_t::mark_missed_connection` flags every packet
still waiting for its next stops (one pass over the stop's passengers, only at departure of a full
train). The flag takes one bit from `menge` (packet limit now 2^22-1), is part of
`same_destination` (flagged and unflagged packets do not merge), is cleared on boarding and saved
with the packet (122.8). Revenue per hop (`vehicle_t::calc_revenue`) is scaled by the load during
that hop: seats x1, standing x0.75, overcrowded x0.25, shared by everybody aboard. Schedule flags
`no_standing` / `no_overcrowding` (`schedule_t`, saved 122.8, part of the schedule string as
`current|type,flags|`, copied to line convoys) are the two checkboxes on the Loading tab of the
schedule dialog; no standing implies no overcrowding. Unchanged: the full-load minimum means seats, loading
level can go past 100%, line capacity statistics count seats only, the planner ignores it.
JSON export: `no_standing`, `no_overcrowding` per schedule.
UI: the convoy window shows "Seats x/y, standing x/y, overcrowded x/y" (own vehicles only, "no
standing"/"no overcrowding" when the schedule forbids it), the vehicle details give each vehicle's
load as "350/163 passengers (163 standing, 24 overcrowded)" (`vehicle_t::get_crowd_split`), and a
stop's waiting list heads passengers with ", N missed a full vehicle" and marks flagged packets
"(missed)" except in the via-sum sort, which merges them (`freight_list_sorter.cc`).

## Passengers change to the train that leaves first (fork feature, save bit in 122.10)

Passengers and mail who boarded a train standing at a stop (`ware_t::boarded_here`, set in
`vehicle_t::load_cargo` before packets are joined, so they never merge with those riding through;
cleared for every vehicle in `ziel_erreicht` on arrival and on the packet handed to a halt in
`unload_cargo`) move to another train of the same owner that physically starts from that stop first
and takes them to their next stop (`zwischenziel` in its destination list, the stop-type-aware walk
now in `collect_destination_halts`). The departure block of `hat_gehalten` records the stop, both
parts' destination lists and the vehicles at the platform (`leaving_halt`, not saved); the move
happens where the train really starts (`vorfahren` and the CAN_START step, both to DRIVING), so a
train still held by a signal, a passing hold or a train in front never takes anybody.
`convoi_t::take_boarded_passengers` checks each LOADING train at that halt: a packet moves only if the
estimated arrival at its next stop (departure + tiles<<20/top speed + 2 calendar minutes per stop
before it; 1/256 month without the calendar) is no later than with its own train, whose departure
comes from `get_planned_departure`, is now when it is ready, or never when it waits for its load.
Seats first, then standing places if the receiving part allows standing, never overcrowded places;
a coupled train fills each part from its own list. Direct vehicle to vehicle (`take_boarded`,
`add_cargo`): no halt statistics, the free capacity booked at departure is corrected. Nothing moves
back: the receiving train is gone. The bit is saved as bit 1 of the 122.8 `missed_connection` byte
from 122.10 on (no version bump; `ware_t` grew from 12 to 16 bytes). Tested headless with the harness
(`pax` and `loads` commands): local waiting for its slot loses its passengers for X to an express,
keeps those for its own stops and those riding through; express leaving 1 min after a local keeps
them, 15 min after loses them; capacity limit; save/load while waiting; coupled pair filling its
joined part; the 34 rail regression scenarios unchanged.

## Convoy names (fork feature, no save change)

A convoy's name gets its id in front ("(4486) R12") only while another convoy of the same owner has
the same name (`convoi_t::refresh_name_ids`, called by `set_name` for the old and the new name and by
the destructor; `refresh_all_name_ids` once in `karte_t::load`). `name_and_id` holds the shown name,
`get_internal_name()` the name without the id, which is what is saved, as in stock. A leading
"(number) " is never part of a name (`skip_shown_ids` on rename and on load), which also cleans
names that kept another convoy's id from pasting a shown name. Stock exes show the id always.

## Windows: the fork is the Steam game (since 2026-09-12)

The owner plays the fork through Steam. `tools/windows/steam-fork.sh` (run from Git Bash) builds
with MSYS2 MinGW64 and installs the result as `simutrans.exe` in the Steam game folder
(`D:\SteamLibrary\steamapps\common\Simutrans`); the exe Steam shipped is kept next to it as
`simutrans-stock.exe`, and a Steam update that replaces `simutrans.exe` is detected and backed up
again on the next install. `install` also appends the fork's `simutrans/text/en.tab` pairs that
Steam's `text/en.tab` lacks (Steam's file kept as `en.tab-stock`) and copies the help pages the
fork changed (`text/en/*`, originals as `*-stock`); `restore` puts both back. Commands: `build`, `install`, `update` (both), `restore` (stock exe
back), `status`, `downgrade NAME [OUT.sve]`, `export NAME OUT.json`. After every feature, run
`tools/windows/steam-fork.sh update`.

The build is `STATIC = 1`, so the exe needs no MinGW DLLs and runs from Steam. `config.default`
for it (the script writes this if the file is missing):

```
BACKEND = gdi
OSTYPE = mingw
DEBUG = 0
OPTIMISE = 1
MULTI_THREAD = 1
USE_FREETYPE = 1
USE_ZSTD = 1
WITH_REVISION = 0
STATIC = 1
FREETYPE_CONFIG = pkg-config freetype2
```

MSYS2 is at `C:\msys64` (`winget install MSYS2.MSYS2`), packages: `make mingw-w64-x86_64-gcc
mingw-w64-x86_64-freetype mingw-w64-x86_64-zstd mingw-w64-x86_64-libpng mingw-w64-x86_64-brotli
mingw-w64-x86_64-bzip2 mingw-w64-x86_64-zlib mingw-w64-x86_64-pkg-config`. `FREETYPE_CONFIG` is
required or `display/font.cc` fails on `ft2build.h`. From Git Bash the build is
`MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc 'cd <repo> && make -j20'` (the login shell ignores
the caller's cwd). Visual Studio (`Simutrans.sln`, GDI Release) also works but needs the .lib files.

The user dir is the redirected Documents folder `D:\OneDrive\Documents\Simutrans` (saves in
`save/`, add-ons, `settings.xml`, autosave). Its `simuconf.tab` holds the owner's fork settings:
`minutes_per_month = 480`, `calendar_seasons = 1`, `pak_file_path = pak128.cs/` (no pakset dialog)
and `warn_doubled_objects = 0` (a fork key: the doubled-objects list is only logged). The
`addons/pak128.cs` folder there is empty, so "reading addon object data failed" at start is
expected; the add-ons live inside the pakset folder. The owner's main game is `save/FORK-CZR.sve` (fork
save format 0.122.1); `save/CZR.sve` is the last stock-format save of the same game.

Batch mode: `-load NAME -export FILE.json` dumps a game, `-load NAME -saveas FILE.sve
[-saveversion 0.122.0]` rewrites it and quits. Version 0.122.0 leaves out the fork's fields, so
the stock game (or the stock exe, `steam-fork.sh restore`) can open the file; that is what
`steam-fork.sh downgrade NAME` does (writes `save/NAME-122.0.sve`). Both must run from the Steam
folder with `-use_workdir -objects pak128.cs` so pakset and add-ons are found; the script does
that. Loading the 175 MB save takes about a minute. The pakset reports "doubled objects" and
missing producers for some goods; both are tolerated in batch mode.

Verified on the real save: export (65 MB JSON), the calendar clock, and the fork loading and
saving the game. To test the viewer with the real file, serve `tools/saveviewer/` and the JSON
over http (see `tools/saveviewer/README.md`); Playwright for Python is installed and drives the
installed Chrome (`channel="chrome"`).

Welcome screen build info: `tools/fork-build-info.sh` writes `fork_build_info.h` (ignored) with the
version (`122.0+N (hash)`, commits since the tag), build date and up to 12 commit subjects, which
`gui/banner.cc` shows under the version. `build` passes the commit installed in Steam
(`simutrans-fork.commit`, written by `install` from `build/default/sim.commit`) as `FORK_SINCE`, so
the list is what is new since the installed build (the latest commits when nothing is). The MSYS2
shell has no git, so the script runs from Git Bash before `make`; the Makefile runs it itself only
where git exists (macOS) and defines `FORK_BUILD_INFO` when the header is there.

GUI checks on Windows: `tools/win-test/win.ps1` screenshots the GDI window and sends clicks and
keys (`pwsh tools/win-test/win.ps1 shot out.png`, `click X Y [left|right|middle]`, `keys "{ESC}"`).
Coordinates are client-relative like the macOS helpers.

Saves and autosaves from the fork carry the fork's save version, which a stock exe refuses, so
`downgrade` the saves you need before `restore`. `settings.xml` is written with the stock version
string (`SETTINGS_SAVE_VER_NR`) on purpose: an exe that finds a newer `settings.xml` deletes it,
and the next start then asks for the language (the fork now picks English silently anyway).
