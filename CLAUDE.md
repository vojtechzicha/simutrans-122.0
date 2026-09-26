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
`schedule_entry_t::get_departure_offsets` gives the sorted list). The dialog keeps the single
offset input and adds an "Also at (min)" text field for the extras. A convoy leaves only when its loading rules are met (minimum load or
maximum wait, unchanged) and a slot is open that no other convoy of the line used at that entry
(`simline_t::take_departure_slot`, `last_departure_slot` saved with the line and cleared when the
schedule changes); among ready convoys the earliest arrival goes first. Convoys without a line and
convoys with `no_load` ignore the timetable. The schedule dialog shows the two inputs only with the
calendar on and greys them out for line-less convoys; the entry list appends "(every N min, +M)".
A convoy that arrived after another convoy of its line at the same entry only unloads until
that one has left (`count_earlier_waiting`), so passengers board the train that leaves first;
the convoy window shows "Departure: HH:MM (in N min), K ahead" from `get_planned_departure`.
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
  primary's direction (`lay_out_on_route`, the stock reversal code), all tiles reserved for the
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
  (`follow_to_stop` on arrival, `advance` on departure, its open timetable slot is marked used too, unless a train of that line that came first can take it;
  the joined line's timetable never holds the coupled train).
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
  train's schedule window is refused while coupled; the convoy window shows "Coupled to/with",
  "Uncoupled, waiting for the platform", waiting for the partner, "Running late"; the departure
  board lists the joined train's destination with the primary's time; JSON export `coupling`,
  `coupled_with`, `running_late` per convoy (each lists only its own vehicles) and `couple_line_id`,
  `couple_max_wait` per schedule entry.
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

## Windows: the fork is the Steam game (since 2026-09-12)

The owner plays the fork through Steam. `tools/windows/steam-fork.sh` (run from Git Bash) builds
with MSYS2 MinGW64 and installs the result as `simutrans.exe` in the Steam game folder
(`D:\SteamLibrary\steamapps\common\Simutrans`); the exe Steam shipped is kept next to it as
`simutrans-stock.exe`, and a Steam update that replaces `simutrans.exe` is detected and backed up
again on the next install. Commands: `build`, `install`, `update` (both), `restore` (stock exe
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

GUI checks on Windows: `tools/win-test/win.ps1` screenshots the GDI window and sends clicks and
keys (`pwsh tools/win-test/win.ps1 shot out.png`, `click X Y [left|right|middle]`, `keys "{ESC}"`).
Coordinates are client-relative like the macOS helpers.

Saves and autosaves from the fork carry the fork's save version, which a stock exe refuses, so
`downgrade` the saves you need before `restore`. `settings.xml` is written with the stock version
string (`SETTINGS_SAVE_VER_NR`) on purpose: an exe that finds a newer `settings.xml` deletes it,
and the next start then asks for the language (the fork now picks English silently anyway).
