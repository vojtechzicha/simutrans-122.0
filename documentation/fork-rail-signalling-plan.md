# Rail signalling: stations, single track and choice (fork)

Status: implemented (savegame 122.6) on top of PR #1 (overtaking at choose signals, platform types,
Hold stop, waiting for passing trains, Hold marker, savegame 122.5), tested headless on pak64 with
placeholder objects (section 7). Not yet seen in the Windows game with pak128.cs objects.

## 1. Problem and principles

- Stock Simutrans protects single track with long-block ("Full Stop") signals. They look ahead
  over the next schedule stops through halts without signals, up to the next signal, but they
  cannot pick a track at the station where that look-ahead ends. Choose signals can pick a track,
  but on single track they make the train wait at the station entrance, on the line (deadlock).
- A stock one-way signal makes its tile one-way (ribi mask). So a two-way station track needs
  two-way signals, and a two-way signal at the entry end of a track ends a passing train's
  reservation there, again on the line.
- Detecting stations from switches is not reliable (sidings, nakladiste on the line). Stations
  are marked explicitly instead.

Rules the design keeps:
1. A train waits only in a station track in front of a platform signal, or at a halt inside a
   section that is its own.
2. A train enters a single-track section only after it has claimed a track at the other end.
3. Every choice (choose signal or single-track arrival) picks only tracks from which the train
   can go on to its next stop.

## 2. New objects (pak add-on, owner makes the art)

| | Platform signal `P` | Station boundary `LT` (lichobeznikova tabulka) |
|---|---|---|
| Kind | Signal: `is_signal=1` plus new `is_platformsignal=1` | Sign: new `station_boundary=1` (like `end_of_choose`) |
| Placed | At both ends of every station track, facing out of the track | At every line entrance of a single-track station, outside the outermost switch, facing trains that enter |
| Applies to | Only trains leaving the track in its direction | Never stops a train; marks where a station begins and ends |
| Other direction | Passes its back as if it were not there; never makes the track one-way | Ignored |
| Stock exe | Plain one-way signal (would make the track one-way) | Decorative sign |

Dat files: copy the dat of an existing pak128.cs signal and of the end-of-choose sign, add the
flag line. The fork's makeobj learns the two keys. Flags: `PLATFORM_SIGNAL = 1<<9`,
`STATION_BOUNDARY = 1<<10`; the pak node version stays 5.

Notation used below:
- `═══` platform track, `───` plain track, `┬ ├ └ ┤ ┘ ┴` switches, `▌` buffer stop
- `S→` / `←S` normal signal (stock, one-way, makes the track one-way)
- `C→` / `←C` choose signal (stock, one-way)
- `E` end-of-choose sign (both directions)
- `P→` / `←P` platform signal for trains leaving to the east / west
- `LT→` / `←LT` station boundary facing trains running east / west (into a station)
- west is left, east is right

What goes where:
- Every station track, at every station: `←P` at its west end, `P→` at its east end. Placed on
  the first plain tile after the platform, before the switch (or the last platform tile).
- Double-track station: `C→`/`←C` on the main track before its first switch (the block behind
  must hold the longest train), `E` on the main track after its last switch (branch junctions and
  crossovers included). Between those and the platforms the mains are used in both directions,
  so no stock signal may stand there (it would make the track one-way); only `P`.
- Single-track station, and a single-track branch joining a double-track station: `LT` on the
  single track just before the station's outermost switch.
- Open double track: stock `S` block signals. Open single track: nothing.
- Halts: nothing. Full Stop signals are no longer used on these lines.

## 3. Logic

### 3.1 Direction-aware signals
A `P` that faces away from the train counts as no signal everywhere: reservation
(`block_reserver`), `can_enter_tile`, `is_signal_clear`, the choose walk and PR #1's helpers
(signal counting in `reserve_choose_detour` etc.). One helper decides "does the signal on this
tile apply to a train going this way". `P` and `LT` never set the ribi mask.

### 3.2 Train at a `P` that applies to it
Look ahead along the route and the following schedule stops, as long-block does today:
- A signal that applies comes first: `P` acts as a normal signal. So `P` is also the ordinary
  exit signal of double-track stations; PR #1 waiting and the Hold marker keep working (they need
  `E` ahead and no choose signal in between, and `P` is no choose signal).
- The route passes an `LT` from behind (leaving the station) first: section mode.

### 3.3 Section mode (all or nothing, else the `P` stays red)
1. Section: keep looking ahead through halts and sidings until the route passes an `LT` from the
   front (enters station B). For a train that turns at a halt or siding on the line, the
   return leg is followed, so B can be its home station.
   If the line ends in a station without `LT`, the section ends at the first signal that applies
   (it must stand a train length past the junction). If neither is found within a whole schedule
   cycle (no route on, or the schedule never leaves the line) the `P` stays red.
2. Every tile of the line up to B's `LT` must be free.
3. Choose a track in B (3.4).
4. Claim that track (3.5).
5. Reserve the way to the first stop, or up to B's `LT` if there is no halt before it. B's
   throat is not reserved yet: at a junction station like Zabreh other trains use it, and it
   would be blocked for the whole trip.
While the train is in the section, opposite and following trains are kept out because their own
look-ahead must pass through this train or its reserved way; the line itself is not held beyond
the current route.

### 3.4 Choosing a track in the next station
The search starts at B's `LT` and never goes past a signal that applies to the train, so it stays
inside B (a few dozen tiles; `max_choose_route_steps` does not matter here).
- Stopping in B: a free platform of the right type and length (PR #1 rules), the one clicked in
  the schedule first.
- Passing B: a free track up to its `P` at the far end, the planned track first; at most 1.5x the
  planned length + 4 tiles (as the PR #1 detour).
- Either way the track must lead on (rule 3): from its end there is a way to the next stop that
  starts at a signal applying to the train (for a train that turns in B, the `P` at the other end).
  The planned track is held to the same rules: it must lead on, and a platform shorter than the
  train is skipped when the station has one long enough (where none is, the train stops sticking
  out, as in the stock game). For a stopping train, "leads on" also means not much further than
  from the planned platform (at most 1.5x its way on to the next stop + 8 tiles): a platform on the
  other side with no crossover after it only leads back through the throat it came from.
- Which tracks a search can reach: Simutrans crossovers and switches can be run in every direction
  (a route may take any exit of a switch tile except straight back), so any train can reach any
  track through the crossovers. Among the free tracks that lead on, the cheapest path wins (planned
  track first), so a train stays on its own side while a track there is free and crosses over only
  when its side is full, like a dispatcher would (owner's decision).

### 3.5 Claim
- The claim is the platform part of the chosen track (from its first platform tile after the last
  switch to the stop, or to the `P` at its far end), reserved for the convoy. The throat in front
  is not claimed (3.6). A path that ends on a track without platform (e.g. a branch train running
  on to the main line) claims nothing.
- Stored on the convoy and saved (savegame 122.6), re-reserved after loading.
- The route to the next stop in B, or through B, goes via the claimed track, not necessarily the
  one clicked in the schedule.
- Released when the train reaches the track, it goes to a depot or is sold, or it finds no route.
  Every new route re-checks it (the schedule may have been edited): released when the stop whose
  way enters that station is no longer in the schedule, or the way to it no longer passes the
  station boundary. A `P` or `LT` of another station drops a stale claim too. A train leaving its
  own track keeps a claim on it.
- Choose signals, PR #1 detours and Hold searches see claimed tiles as reserved.

### 3.6 At halts and on arrival
After dwelling at a halt the train reserves up to B's `LT` (or its next stop before it). At the
`LT` it reserves from there into its claimed track (to its stop, or to the far `P` when it
passes). If the throat is taken at that moment, it waits at the `LT`, like at the real
lichobeznikova tabulka. That wait is always short: nobody ever stands in a throat (trains only
stand at `C`, at `P` and beyond `E`), and a train that wants this section waits at its `P`
without reserving anything, so no one holds the throat while waiting for this train.
A train that reaches an `LT` without a claim (it entered the line some other way, e.g. from a
depot on the line or an old save) chooses and claims a track there; if none is free it waits at
the `LT`.

### 3.7 Choose signal additions (double-track stations)
- The choose walk also ends at a `P` that applies to the train (in addition to `E` and another
  choose signal).
- A stopping train reaches its halt before that `P`, so it chooses a platform as today, now with
  the lead-on check (rule 3, with the length bound of 3.4). This matters because `P` leaves the
  tracks two-way: with stock one-way exit signals the other side's platforms were closed to the
  train, with `P` they are reachable through the entry crossover.
- A passing train whose walk ends at such a `P`: through-choose, i.e. any free track up to its `P`
  at the far end that leads on to the planned route, planned track first, length limit as 3.4.
  If the next block after that `P` is taken, the train waits at the `P`, inside the station.
  This replaces PR #1's detour wherever exits are `P`; the detour code stays for stock exits.

### 3.7.1 `P` as the exit signal of every double-track station
Replacing the stock exit signals of a double-track station with `P` is fine, and that is how
Polom and Zabreh are drawn in 4.1 and 4.3:
- Put a `P` at both ends of every platform track, each facing out of the station. With `P` only at
  the end the stock signal was at, a train using the track the other way (other side's platform,
  a train turning there) leaves without any exit signal.
- Keep the choose signals at the entries, the `E` signs after the exits and the stock block
  signals on the open line; those keep the main lines one-way.
- `P` is a plain signal here (no `LT` behind it). Do not use it in place of a pre-signal,
  priority or long-block exit signal: it has none of their behaviour.
- The station tracks become two-way: a train turning at the station leaves the way it came, a
  stopping train takes the other side's platform when its own are full and it can go on from
  there (crossovers in both throats), and a passing train blocked on its own track takes any free
  track to its `P` (through-choose). Unlike the PR #1 detour, through-choose does not ask why its
  track is blocked, so a fast train may also get past a slow one that is still running through the
  station, if it reaches its `P` first.
- A station with a crossover in one throat only: a stopping train never takes a platform of the
  other side, since it could not go on (lead-on check); it waits at the choose signal instead.
- A downgraded save (`steam-fork.sh downgrade`) writes `P` as a two-way stock signal: the stock
  game drives, but its choose logic and the lead-on check are gone.

### 3.8 Why it cannot lock, and the one limit
A train only waits in front of a `P`, or at a halt inside a section nobody else can enter, and it
enters a section only with a claimed track at the far end. So trains never meet head-on on the
line and never arrive at a full station. The remaining lock is capacity: both stations of a
section full of trains that all want that section (four trains for two 2-track stations). The
timetable or one more track prevents it. The headless test reproduces it (scenario `lock`: four
trains on a line whose termini and loop have two tracks each lock within minutes, all standing at
`P` inside stations, none on the line). Possible later rule: a train may not take the last free
track of a station when every other train there wants the section it came over and the station at
the other end is full as well.

### 3.9 End-of-choose `E` in mixed layouts
- With `P` at every track end, `E` no longer ends the choice for passing trains: the `P` that
  applies comes first. `E` stays only at double-track stations, for PR #1's waiting for passing
  trains and the Hold marker, which need the point after the station where the local's and the
  passing train's routes merge.
- Place `E` on each main track after the last switch of the station on that side, branch
  junctions and crossovers included (Zabreh main 2: west of the branch junction). A train that leaves the
  main line before `E` is never held (Olomouc → branch).
- Never on single track: `E` applies to both directions. The section look-ahead and the `LT`
  search ignore `E`. Passing trains at single-track stations need no `E`: their track is chosen
  by the section logic (3.4). Waiting for a faster train at a single-track station, when added,
  will use the station's far `LT` in the role `E` has on double track.
- PR #1 change: the hold walk (`is_held_for_passing_train`) follows the whole route to the next
  stop, so in mixed layouts it can find a far `E`: a branch train at Postrelmov or at a halt,
  going to Olomouc, would see Zabreh's `E` and wait for a main-line express. Bound the walk: it
  stops at any `LT` and at the first signal that applies after the train's own exit signal.
- PR #1 option: also count trains that enter the station through its `LT` with a claim as
  passing trains (today only trains that entered through a choose signal count), so a local at
  Zabreh waits for a branch express to Olomouc too.

## 4. Stations

### 4.1 Polom (double track, 4 pax + 2 freight, most trains pass, some terminate)

Same layout as Zabreh (4.3) without the branch:

```
                                     ┌── ←P ═══ pass 1 ════════ P→ ──┐
to Přerov      ◄── ←S ── E ────┬─────┴── ←P ═══ pass 2 ════════ P→ ──┴───────┬──── ←C ── ←S ──◄ from Bohumín
from Přerov    ──► S→ ── C→ ───┴──┬────┬── ←P ═══ pass 3 ════════ P→ ─────┬──┴──── E ── S→ ──► to Bohumín
                                  │    └── ←P ═══ pass 4 ════════ P→ ──┬──┘
                                  └──┬──── ←P ═══ freight 1 ═════ P→ ──┤
                                     └──── ←P ═══ freight 2 ═════ P→ ──┘

 crossovers at the west and east throat (Simutrans crossovers work in every direction)
```

| Train | In | Can use | Out |
|---|---|---|---|
| Přerov → Bohumín, stop or pass | `C→` | 3, 4, freight (pass: through-choose) | `P→` normal → `E` |
| Bohumín → Přerov, stop or pass | `←C` | 1, 2; 3, 4, freight via the east crossover when full | `←P` normal (3/4 via the west crossover) → `E` |
| Terminates from Přerov | `C→` | 3, 4 (1, 2 when full) | reverse, `←P` normal (3/4 via the west crossover) → main 2 |
| Terminates from Bohumín | `←C` | 1, 2 (3, 4 when full) | reverse, `P→` normal (1/2 via the east crossover) → main 1 |
| Slow freight marked Hold | | steps onto a free track when a faster train comes (PR #1) | |

Locals click pass 3 (eastbound) or pass 1 (westbound) in the schedule so passing trains keep the
straight tracks. The column "Can use" lists the cheapest tracks first; any track is possible
through the crossovers when those are full.

### 4.2 Mnichovo Hradiste (single track, 3 pax + 2 freight, any train may terminate)

```
 from Bakov (west)                                                      to Turnov (east)

 ──────── LT→ ───┬── ←P ═══ 1 pax     ═══════════════════ P→ ──┬─── ←LT ────────
                 ├── ←P ═══ 2 pax     ═══════════════════ P→ ──┤
                 ├── ←P ═══ 3 pax     ═══════════════════ P→ ──┤
                 ├── ←P ═══ 4 freight ═══════════════════ P→ ──┤
                 └── ←P ═══ 5 freight ═══════════════════ P→ ──┘
```

| Train | Path |
|---|---|
| Local Bakov → MH → Turnov | at Bakov's `P→`: section Bakov–MH, claims a free pax track (say 2); passes `LT→` and the back of `←P` on 2, stops; `P→` of 2: section to the next station; passes `←LT` from behind |
| Express, no stop | at Bakov claims the planned through track (or any free); stops at its `P→` only when the next section is taken, which is where trains cross |
| Terminates at MH | claims e.g. 3, stops, reverses, `←P` on 3: section back to Bakov |
| Freight | stopping: 4/5 by platform type; passing: any free through track |

### 4.3 Zabreh na Morave (double track + branch to Postrelmov joining on the west side)

The owner's layout: the branch joins main 2 west of the station, crossovers in both throats.

```
from/to Postřelmov ── LT→ ──┐
                            │        ┌── ←P ═══ pass 1 ════════ P→ ──┐
to Č.Třebová   ◄── ←S ── E ─┴──┬─────┴── ←P ═══ pass 2 ════════ P→ ──┴───────┬──── ←C ── ←S ──◄ from Olomouc
from Č.Třebová ──► S→ ── C→ ───┴──┬────┬── ←P ═══ pass 3 ════════ P→ ─────┬──┴──── E ── S→ ──► to Olomouc
                                  │    └── ←P ═══ pass 4 ════════ P→ ──┬──┘
                                  └──┬──── ←P ═══ freight 1 ═════ P→ ──┤
                                     └──── ←P ═══ freight 2 ═════ P→ ──┘

 branch junction: on main 2, west of the west crossover; E west of the junction
 crossovers at the west and east throat (Simutrans crossovers work in every direction)
 main 2 between the junction and pass 1/2, and main 1 between the crossovers and pass 3/4, are
 used both ways: no stock signals there
```

Reachability: every train can reach every track through the crossovers; the cheapest are listed
first. `C→` trains prefer pass 3, 4 and freight, `←C` trains pass 1, 2; branch trains reach pass
1/2 along main 2 and the rest through the west crossover.

| Train | In | Can use | Out |
|---|---|---|---|
| Č.Třebová → Olomouc, stop or pass | `C→` | 3, 4, freight | `P→` normal → `E` |
| Olomouc → Č.Třebová, stop or pass | `←C` | 1, 2 (3, 4, freight when full) | `←P` normal → junction straight → `E` |
| Terminates from Č.Třebová | `C→` | 3, 4 | reverse, `←P` normal → west crossover → main 2 |
| Terminates from Olomouc | `←C` | 1, 2 (3, 4 when full) | reverse, `P→` normal (1/2 via the east crossover) → main 1 |
| Olomouc → branch, stop or pass | `←C` | 1, 2 (3, 4 when full) | `←P` section → main 2 → junction → `LT→` |
| Č.Třebová → branch | `C→` | 3, 4 | stop, reverse, `←P` section → west crossover → junction |
| Branch → Olomouc, stop or pass | claim at Postřelmov, enters at `LT→` | 1, 2, 3, 4 | `P→` normal (1/2 via the east crossover) → `E` |
| Branch → Č.Třebová | claim | any | stop, reverse, `←P` normal → `E` |
| Branch terminates | claim | any | stop, reverse, `←P` section |

- Only trains between Č.Třebová and the branch reverse; Olomouc–branch trains run through, with
  or without a stop.
- A branch train enters at `LT→` only when the junction, main 2 and the west crossover are free
  into its claimed track (3.6); it never stands on main 2.
- A westbound train that wants the branch waits at its `←P` until the branch section and a
  Postřelmov track are free; it never stands in the throat.
- PR #1 waiting works for trains joining a main line before `E`; trains to the branch leave
  before `E` and are not held.

### 4.4 Branch Postrelmov – Zabreh

```
 Sumperk                                                                                      Zabreh
 ... LT→ ─┬── ←P ═══ 1 pax ═════ P→ ──┬── ←LT ──────═ halt B ═──────┬──────═ halt A ═────── LT→ ──[K] ...
          └── ←P ═══ 2 pax ═════ P→ ──┘                             │
                   Postrelmov                                        └─ nakladiste
```

- Halts: nothing, any number. No `E` anywhere on the branch.
- Postrelmov as the end of the line: `←LT` only, tracks start at buffers `▌` on the west,
  `P→` at their east ends.
- Nakladiste, two options:
  - plain stop, no objects: a freight train serving it holds Postrelmov–Zabreh while loading;
  - own station: `LT` on the siding track just after the switch facing into the siding, `P` at the
    switch end of the siding platform facing out. The train waits there without holding the line.
    A one-track station, so the capacity limit of 3.8 applies to it.

## 5. Code changes

1. `descriptor/roadsign_desc.h`: the two flags, accessors; `is_simple_signal` excludes `P`.
2. `descriptor/writer/roadsign_writer.cc`: keys `is_platformsignal`, `station_boundary`.
3. `obj/roadsign.cc`, `simtool.cc`: `P` and `LT` set no ribi mask and are one-way only.
4. `vehicle/simvehicle.cc`: direction-aware signal helper everywhere (3.1);
   `is_platform_signal_clear` (3.2–3.6); section look-ahead; station search from `LT`; choose walk
   ends at an applicable `P`, through-choose for passing trains, lead-on check (3.7).
5. `simconvoi.*`: claim (tiles + halt), rdwr 122.6, re-reserve after load, release paths, route via
   the claim; convoy window texts ("Waiting for the line to X", "No free track at X").
6. `simversion.h` 122.6; en.tab strings; JSON export optional `claim` key (+ viewer).
7. Downgrade (`-saveversion 0.122.0`): write `P` as a two-way plain signal so the stock game still
   runs (without the protection).
8. `tools/windows/steam-fork.sh`: build makeobj and the add-on pak.
8a. PR #1 hold walk bounded at `LT` and at the first signal after the own exit signal (3.9).
9. CLAUDE.md: the design.

## 6. Decisions (defaults until the owner says otherwise)

1. Claim the track only, not the throat.
2. Trains that enter a single line without passing a `P` (depot on the line, junction without
   `P`, old saves) choose and claim a track at the station boundary (3.6); depots still belong in
   stations, since a train waiting at an `LT` stands on the line.
3. Main-line trains may use the tracks the branch uses at Zabreh; only the schedule click steers.
4. Zabreh and Polom use the owner's layout (4.1, 4.3). Trains may use the other side's tracks
   when their own are full (owner's decision).
5. Later: several trains following in one direction (direction lock per section, block signals);
   a local at a single-track station waiting for a faster train (the `LT` gives the area PR #1
   needed `E` for).

## 7. Tests

Done headless on pak64 with placeholder objects (`tools/fork-signals`) and a throwaway harness
that builds the layouts in code (not committed):
- `line`: terminus A (2 tracks) – halt – halt – loop B (2 tracks) – terminus C (2 tracks), a local
  stopping everywhere, an express A–C passing B, a shuttle terminating at B. All keep running; no
  train ever stood longer than a few seconds outside a station track. Claims are taken at every
  departure onto the line.
- `zabreh`: the layout of 4.3 with termini at both ends: express and local on the main line, a train
  terminating at Zabreh from Olomouc, a branch shuttle terminating at Zabreh, branch–Olomouc,
  Ceska Trebova–branch (reverses) and Olomouc–branch without stopping at Zabreh. All keep running;
  the only waits off station tracks were at main-line signals and up to 13 s at the branch `LT`
  while a main-line train crossed the throat.
- `saveload`: saved while a train between the halts held a claim; after loading the claim and its
  reserved tiles are back and all trains keep running; a 0.122.0 save writes every platform signal
  two-way and has no claim.
- `sides`: a double-track station with `P` at both ends of both tracks, the eastbound platform held
  by a train that waits for a full load. With crossovers in both throats a stopping eastbound train
  takes the westbound platform and goes on; with only the west crossover it waits at the choose
  signal and takes its own platform once that is free (before the fix it took the westbound
  platform and then stood at the `P` there, on the westbound main's track, until the blocker left).
- `lock`: the capacity limit of 3.8, see there.

Still to see in the Windows game with the pak128.cs objects:

- Placing the objects with the tool (one click one-way, a second click turns the direction), their
  images, and the convoy window texts.
- Nakladiste in both variants, freight platform types on a single-track station.
- PR #1 waiting at Zabreh with the calendar on, and a branch train at Postrelmov or a halt not
  being held for a main-line express at Zabreh (3.9).
- A downgraded save in the stock exe.
