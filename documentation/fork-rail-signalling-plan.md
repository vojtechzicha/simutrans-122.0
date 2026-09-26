# Rail signalling plan: stations, single track and choice (fork, planned)

Status: design only, nothing implemented yet. Builds on PR #1 (overtaking at choose signals,
platform types, Hold stop, waiting for passing trains, Hold marker, savegame 122.5).
Everything below is derived from reading the code; nothing of it has run yet.

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
- Double-track station: `C→`/`←C` before the entry switches (the block behind must hold the
  longest train), `E` on the main track after the exit switches. Crossovers for turning trains
  lie between `C` and the track fans.
- Single-track station: `LT` outside each throat.
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
   (it must stand a train length past the junction).
2. Every tile of the line up to B's `LT` must be free.
3. Choose a track in B (3.4).
4. Claim that track (3.5).
5. Reserve the way to the first stop, or into the claimed track if there is no halt before it.
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

### 3.5 Claim
- The claim is the chosen track's tiles (between its two `P`, or `P` and buffer), reserved for the
  convoy. The throat in front is not claimed: on single track only this section's trains use it.
- Stored on the convoy and saved (savegame 122.6), re-reserved after loading.
- The route to the next stop in B, or through B, goes via the claimed track, not necessarily the
  one clicked in the schedule.
- Released when the train reaches the track, or when its schedule changes, it goes to a depot or
  is sold, or its route becomes impossible. A train leaving its own track keeps a claim on it.
- Choose signals, PR #1 detours and Hold searches see claimed tiles as reserved.

### 3.6 At halts and on arrival
After dwelling at a halt the train reserves up to its next signal that applies, which is inside
its claimed track in B, through B's throat. It never stops at an `LT`.

### 3.7 Choose signal additions (double-track stations)
- The choose walk also ends at a `P` that applies to the train (in addition to `E` and another
  choose signal).
- A stopping train reaches its halt before that `P`, so it chooses a platform as today, now with
  the lead-on check (rule 3).
- A passing train whose walk ends at such a `P`: through-choose, i.e. any free track up to its `P`
  at the far end that leads on to the planned route, planned track first, length limit as 3.4.
  If the next block after that `P` is taken, the train waits at the `P`, inside the station.
  This replaces PR #1's detour wherever exits are `P`; the detour code stays for stock exits.

### 3.8 Why it cannot lock, and the one limit
A train only waits in front of a `P`, or at a halt inside a section nobody else can enter, and it
enters a section only with a claimed track at the far end. So trains never meet head-on on the
line and never arrive at a full station. The remaining lock is capacity: both stations of a
section full of trains that all want that section (four trains for two 2-track stations). The
timetable or one more track prevents it.

## 4. Stations

### 4.1 Polom (double track, 4 pax + 2 freight, most trains pass, some terminate)

```
 west (Prerov)                                                               east (Bohumin)

 main 2 (←)
 ── ←S ── E ──[w2]──┬── ←P ═══ 2 pax     ═══════════════ P→ ──┬──[b1]───── ←C ── ←S ──
                    ├── ←P ═══ 4 pax     ═══════════════ P→ ──┤
                    └── ←P ═══ 6 freight ═══════════════ P→ ──┘
 main 1 (→)
 ── S→ ── C→ ─[w1]──┬── ←P ═══ 1 pax     ═══════════════ P→ ──┬──[b2]───── E ─── S→ ──
                    ├── ←P ═══ 3 pax     ═══════════════ P→ ──┤
                    └── ←P ═══ 5 freight ═══════════════ P→ ──┘

 crossover W: w1 → w2 heading west (main 1 → main 2), w1 a little east of w2
 crossover B: b1 → b2 heading east (main 2 → main 1), b1 a little west of b2
```

| Train | Path |
|---|---|
| Passing east | `C→` walk meets `P→` of the planned track (1): takes it, or any free track through (3.7); waits at `P→` if the block beyond `E` is taken |
| Stopping local east | `C→` chooses 3 (schedule) or a free platform; PR #1: waits at `P→` for an express that will pass `E` |
| Terminating from the west | `C→` chooses 1/3/5 (lead-on: back west via W), stops, reverses; `←P` acts normal: w1 → w2 → `E` → `←S`; PR #1 waits for westbound expresses |
| Terminating from the east | `←C` chooses 2/4/6, reverses, `P→`: b1 → b2 → `E` → `S→` |
| Slow freight marked Hold | steps onto a free track when a faster train comes (PR #1) |

Schedules: locals click tracks 3/4 so passing trains keep 1/2.

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

### 4.3 Zabreh na Morave (double track + branch to Postrelmov on the east side)

```
 west (Ceska Trebova)                                                                  east (Olomouc)

 main 2 (←)
 ── ←S ── E ──[w2]──┬── ←P ═══ 2 pax     ═════════════ P→ ──┬──[b1]──────[a2]──[J]── ←C ── ←S ──
                    ├── ←P ═══ 4 pax     ═════════════ P→ ──┤                   │
                    └── ←P ═══ 6 freight ═════════════ P→ ──┘                   └── ←LT ──── branch
 main 1 (→)
 ── S→ ── C→ ─[w1]──┬── ←P ═══ 1 pax     ═════════════ P→ ──┬───[b2]──[a1]──── E ── S→ ──
                    ├── ←P ═══ 3 pax     ═════════════ P→ ──┤
                    ├── ←P ═══ 5 freight ═════════════ P→ ──┤
                    └── ←P ═══ 7 freight ═════════════ P→ ──┘

 crossover W: w1 → w2 heading west (main 1 → main 2)
 crossover B: b1 → b2 heading east (main 2 → main 1)
 crossover A: a1 → a2 heading east (main 1 → main 2)
 order west to east in the east throat: b1, b2, a1, a2, J, then ←C on main 2 and E on main 1
 J: branch junction on main 2, west of ←C
```

Eastbound trains can reach tracks 1/3/5/7; westbound trains and branch trains reach all seven
(1/3/5/7 through crossover A backwards). Planned-first keeps them on their own side normally.

| # | Train | In | Track | Out |
|---|---|---|---|---|
| 1 | Ceska Trebova → Olomouc, stop or pass | `C→` | 1/3/5/7 (pass: through-choose) | `P→` normal → b2, a1 straight → `E` |
| 2 | Olomouc → Ceska Trebova, stop or pass | `←C` | 2/4/6 (1/3/5/7 via A when full) | `←P` normal (from 1/3/5/7 via W) |
| 3 | From west, terminates, back west | `C→` | 1/3/5/7 | reverse, `←P` normal → W |
| 4 | From east, terminates, back east | `←C` | 2/4/6 | reverse, `P→` normal → B |
| 5 | Ceska Trebova → branch, stopping | `C→` chooses 1/3/5/7 (lead-on via A) | | `P→` section → a1 → a2 → J → `←LT` |
| 6 | Ceska Trebova → branch, not stopping | `C→` walk meets `P→`: through-choose 1/3/5/7 | | `P→` section; waits there if the branch is taken |
| 7 | Olomouc → branch (must stop, reverses) | `←C` | 2/4/6 | reverse, `P→` section → J → `←LT` |
| 8 | Branch terminates | section claim from `←LT` | any of 1–7 | reverse, `P→` section (from 1/3/5/7 via A) |
| 9 | Branch → Ceska Trebova, stop or pass | section claim | stop: platform, pass: through track | `←P` normal (from 1/3/5/7 via W) |
| 10 | Branch → Olomouc (must stop, reverses) | section claim | platform | reverse, `P→` normal (from 2/4/6 via B) → `E` |

Trains between Olomouc and the branch have to stop and reverse at Zabreh; there is no direct
connection. PR #1 waiting works for trains joining main 1 before `E` (rows 1, 4, 10), not for
trains going to the branch (their route leaves before `E`).

### 4.4 Branch Zabreh – Postrelmov

```
 Zabreh                                                                                  Sumperk
 ... ←LT ────═ halt A ═──────┬──────═ halt B ═────── LT→ ─┬── ←P ═══ 1 pax ═════ P→ ──┬── ←LT ─── ...
                             │                            └── ←P ═══ 2 pax ═════ P→ ──┘
                             └─ nakladiste                        Postrelmov
```

- Halts: nothing, any number.
- Postrelmov as the end of the line: `LT→` only, tracks end in buffers `▌`, `←P` at their west ends.
- Nakladiste, two options:
  - plain stop, no objects: a freight train serving it holds Zabreh–Postrelmov while loading;
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
9. CLAUDE.md: the design.

## 6. Decisions (defaults until the owner says otherwise)

1. Claim the track only, not the throat.
2. No fallback for trains that enter a single line without passing a `P` (depot on the line,
   junction without `P`): depots belong in stations.
3. Main-line trains may use the tracks the branch uses at Zabreh; only the schedule click steers.
4. Later: several trains following in one direction (direction lock per section, block signals);
   a local at a single-track station waiting for a faster train (the `LT` gives the area PR #1
   needed `E` for).

## 7. Test plan (headless pak64 harness, then Windows)

- Single-track line: two stations (3 and 2 tracks), two halts between, a bay; crossing, passing,
  terminating, freight platform types; the two-halts head-on case must not lock.
- Nakladiste in both variants.
- Zabreh layout: moves 1–10 of 4.3. Polom: terminating both ways.
- Save/load with a claim, schedule change releases a claim, downgrade save opens in stock.
