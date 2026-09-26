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
never routes cargo from it). The rules live in the helpers on `schedule_entry_t` (`loads`,
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

## Mixed traction (fork feature)

A convoy with electric engines and other engines (diesel, steam, ...) needs no catenary
(`convoi_t::needs_electrification()` is true only when all engines are electric). The electric
engines pull only while every one of them is on a tile with catenary; otherwise only the other
engines pull. Under wires the other engines pull too only when that gives a higher top speed with
the current load (`traction_both_under_wire`, chosen in `recalc_traction(true)` from
`calc_speedbonus_kmh`, i.e. at start and at every departure). An idle engine adds no power, no
running cost, no top speed limit, no smoke (`vehicle_t::idle`) and no start sound
(`play_start_sound`); its fixed cost stays. `convoi_t::recalc_traction` sets `sum_gear_and_power`,
`min_top_speed`, `sum_running_costs` and `is_electric` for the current mode; `vehicle_t::hop` calls
it when an electric engine of a mixed convoy enters or leaves catenary (`on_wire`). The bonus speed
assumes the better choice under wires; the per-tile average of speed limits covers the rest.
Nothing is saved: the mode is rebuilt on load. The depot offers electric vehicles in a depot without
catenary once the convoy has another engine and shows the off-wire power and speed; the convoy
details show what pulls now and mark idle engines; the JSON export has `traction` per convoy and
`idle` per vehicle. Convoys with only one kind of engine behave as stock.

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
