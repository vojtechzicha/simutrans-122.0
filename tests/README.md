# Fork tests

Headless scenarios for the fork's rail and road features. Each scenario builds a small layout with the
game's own tools (or works on a loaded game), runs the simulation without a window and checks what the
trains and buses did. The harness is compiled into every build as the batch mode `-forktest SCRIPT`
(`tests/fork_test_harness.cc`), so it keeps compiling with the code it tests and cannot get lost.

Run them before committing a change to rail or road behaviour, and add a scenario for every fix of a
lock or jam: it must fail on the build without the fix and pass with it.

## Running

```
make -j20                       # or the MSYS2 build, see CLAUDE.md
tests/run.sh                    # every scenario, 4 at a time
tests/run.sh chained_lt_4       # some of them
tests/run.sh -e OTHER.exe       # another build (e.g. an older commit, to see a test fail without a fix)
tests/run.sh --clean            # remove the work dirs
```

Git Bash on Windows, bash on macOS (the SDL2 build runs with `SDL_VIDEODRIVER=dummy`). It prints one
line per scenario, PASS or FAIL, the `FAIL`/`ERROR` lines of the failed ones, and exits 1 if any failed.

- The scenarios use the owner's pak128.cs (vehicles and signals named in them): `PAKSET` points at it,
  default OneDrive's `Games/Simutrans/pak128.cs` (`D:\OneDrive\...` on Windows, `~/Library/CloudStorage/
  OneDrive-*` on a Mac).
- Each scenario runs in a work dir of its own under `TESTS_WORK` (default `%LOCALAPPDATA%\simutrans-fork-
  tests`, on a Mac `$TMPDIR/simutrans-fork-tests`): copies of `simutrans/config`, `text`, `font` and
  `themes`, `tests/simuconf.tab` (calendar at 480 minutes a month; nothing of the owner's simuconf), the
  scenarios and layouts, and a link (junction) to the pakset. The log is
  `TESTS_WORK/NAME/scenarios/NAME.txt.log`. The work dirs are kept out of the repo so that no
  `git clean` walks into the pakset through that link; remove them with `--clean`, which unlinks the
  pakset first. Never delete the pakset link with a tool that follows junctions.
- One scenario takes a few seconds: the worlds are tiny.

## Scenarios

| Scenario | Checks |
| --- | --- |
| `chained_lt_3`, `chained_lt_4` | Two station boundaries in a row (Vršovice -> ONJ, 2026-10-30): a train for the yard does not refuse it because the train on its planned track waits to leave. Fails before 0c1ad07. |
| `lt_entry_main_3`, `lt_entry_main_4` | A single-track stub between two station boundaries entered through a plain signal (Vršovice from Eden and ONJ, 2026-10-30): the signal acts as a platform signal, so no train waits on the stub for a track at the station. Fail before the fix. |
| `lt_entry_p_3` | The same stub entered through a P: the layout way to the same result, passes on older builds too. |
| `station_tracks_tram` | A tram stop in a train station's halt is no track of the station for the train rules (`expect_tracks`). Fails before the fix. |
| `coupling_r18` | Coupling and parting of two lines every hour (R18 at Staré Město). |
| `coupling_s4` | A unit coupled for part of the way only, left and taken back at the far station (S4 Kralupy). |
| `layover_cluster` | Timetabled buses in a cluster of dead-end layover bays: every bus parks, no bay errors. |
| `layover_side` | Layover tiles beside the lane with through buses driving past both ways. |
| `layover_gate` | A bus queued at the only way into a layover takes the bay of a bus behind it (Praha-Uhříněves, 2026-11-02): every bus parks. Fails before the fix. |
| `layover_gate_near` | A bus queued for one layover tile takes a free bay of the tile in front of the gate. Fails before the fix. |
| `layover_long_pair` | 24 m buses (line 59, Praha, 2026-11-07) park in a bay of two layover tiles in a row; short buses take the single tile beside it, never the pair. Fails before. |
| `layover_long_west` | The same heading west (the front stops halfway, the rear on the second tile), with save/load and rotation while parked. Fails before. |
| `layover_long_row` | A row of three layover tiles: long buses pull up to its end, a short bus takes an end tile, not the middle one. Fails before. |
| `layover_long_deadend` | A long bus at a layover of dead ends only stands in the lane and keeps running, short buses park. Passes before too. |

Layouts shared by several scenarios are in `layouts/` and read with `include`.

## Writing a scenario

A scenario is a text file of commands, one per line; `#` starts a comment. Start it with a comment that
says what it checks, which feature or fix it guards and what the expected outcome is. End it with
`expect` lines: the log's last line is `RESULT PASS` only when every expect held and no command failed
(`ERROR`). Keep it small and quick; make the trains frequent rather than the run long.

Building a world:

- `newworld W H` flat empty map; `waydesc NAME` the way for `track X1 Y1 X2 Y2` (a straight piece, or a
  route between the two tiles); `wire rail|tram X1 Y1 X2 Y2` catenary
- `stations NAME X0 X1 Y` a row of station tiles, built west to east (a row whose first tile touches no
  halt becomes a halt of its own: build the touching part first), `vstations NAME X Y0 Y1`, `station`
- `sign NAME X Y DIR` a signal or sign applying to trains heading DIR (N, S, E, W, B = both)
- `depot NAME X Y` at a track end; `list ways|depots|stations|signs|vehicles|road|tram` names in the pakset
- `line NAME` / `tramline` / `busline`, then `stop X Y MINLOAD WAIT TYPE` (TYPE R, T, A, L, U, H as in
  the schedule dialog), `timetable INTERVAL OFFSET` and `couple LINE MAXWAIT` for the last stop, `endline`
- `vehicles NAME...` then `convoy DEPOT_X DEPOT_Y LINE COUNT` (named LINE#1, LINE#2, ...)

Running and looking:

- `run MINUTES [VERBOSE_EVERY]` calendar minutes; `run_until NAME STATE MAXMIN`
- `stuck` rail convoys that did not move since the last `stuck`; `summary` per convoy counters
- `events on|off`, `stops on`, `waits on` (log every wait text change), `baywatch on` (bus bays: a line per
  change with the bay's tiles and, parked, where the vehicles stand; two buses in one bay, or a long bus with a
  section off its bay, is an error)
- `view X Y` centres the view on a tile; a `save` keeps it, so the save opens there in the game window
- `diag NAME|ID` state, section, claim, schedule and route with reservations; `tiles X0 Y0 X1 Y1`,
  `dump X0 Y0 X1 Y1` an ASCII map, `res`, `signs`, `bays`, `roadjam`, `linelayout`, `stationmaps`
- `save NAME`, `load NAME`, `lineedit`, `stoptype`, `retarget`, `delcnv`, `todepot`, `rotate`, `remove`,
  `pax`, `loads`, `snap` (screenshot) and more: see `run_command` in `fork_test_harness.cc`. A convoy is named
  by part of its name (one word: arguments split at spaces) or as `id:ID`; `#` starts a comment anywhere
- `ltentries` lists every signal (no P or block post) that leads onto a single-track section through a
  station boundary, so it acts as a P (useful on a real save)
- `standwatch on` counts the minutes rail convoys stand (not loading) per front tile; `standreport` lists them
  (`STAND x,y,z minutes times`) with the total and the stops done, to compare two builds on a real save

Checking (each failed one logs `FAIL ...` and makes the result FAIL):

- `expect PATTERN FIELD OP N`: the convoys matching PATTERN together. PATTERN is `*`, part of a convoy
  name, or a line name. FIELD: `stops`, `couplings`, `claims`, `holds`, `follows`, `parks` (summed),
  `offstation` (longest stand outside a station, minutes) or `sectionwait` (longest wait at a platform
  signal or station boundary), which take the largest. OP: `>=`, `<=`, `==`, `>`, `<`.
- `expect_each PATTERN FIELD OP N`: every matching convoy.
- `expect_stuck OP N`: the count of the last `stuck` (call `stuck` before the run and after).
- `expect_parkedat PATTERN X Y OP N`: how often the matching convoys parked in a layover bay with their front at
  X,Y (`baywatch on`).
- `expect_state PATTERN STATE` (e.g. `LOADING`, `CAN_START`) and `expect_wait PATTERN TEXT...` (part of
  the wait text the convoy window shows): some matching convoy, now.
- `expect_tracks X Y OP N`: the tracks the single-track and keep-a-track rules count at the halt on that tile.

Vehicles and signals must exist in the owner's pak128.cs. Diesel trains need no catenary: e.g.
`VZ-CeskeDrahy-843-najbrt2-843 VZ-CeskeDrahy-843-najbrt2-043 VZ-CeskeDrahy-843-najbrt2-843` (3-car
railcar, 110 km/h, 3 tiles), `VZ-AZDPraha-810-modrozluta-810` (railcar, 80 km/h, 1 tile),
`1Benst162t Passagierwagen Passagierwagen` (150 km/h, 2 tiles). Buses: `Road_050`,
`CZR-Stops-ODIS_Bus-Empty-L`, `roaddepot_2000`, `CZ-ODIS-SOR_BN95-Green`.

On a copy of a real save: `sim.exe -use_workdir -singleuser -objects pak128.cs/ -nosound -nomidi
-load NAME -forktest SCRIPT` in a work dir that has the save in `save/` (pass `-load` on the command
line; `load` in the script does not find a bare name).

## To rebuild

Until 2026-10-30 the scenarios lived in Claude's temporary folders, and a cleanup deleted them: about
39 that every rail change was checked against, comparing the logs of the old and the new build. They
are described in CLAUDE.md, feature by feature; rebuild them here when the feature is next touched:

- Bay platforms: trains turning back take bays, through trains do not (`bay_t1`-`bay_t9`).
- `P`/`LT` single-track sections: claims, last free track, longest waiter first, the loop claim
  (Nepřevázka), track choice keeping waypoints.
- Far-side platform whose exit crosses another platform (Čachovice, `oc*`, `ocx`): `locks_with_others`.
- Block posts (`f1`-`f11`): following trains, turners, coupling in a section.
- Held at the platform and boarding (`pa`-`pg`), held-train slots (`h1`, `h2`).
- Stepping aside and overtaking at choose signals (`s1`-`s9`, 12 overtaking scenarios), single-track
  overtaking at `LT`/`P` stations (line 062).
- Platform length choice (shortest that fits, coupling pairs).
- Keeping a track for the other direction: a double-track pair of 3-track stations with locals and
  expresses. The recovered layout of 2026-10-13 locked at once with the vehicles of today's pakset (one
  express with no route), so it is not in the set yet: find out why first.
- Autoblock aspects (`abstale`), entry signal aspects, passengers changing trains, the road ring
  deadlock warning (`rg5`), timetable first slots.
