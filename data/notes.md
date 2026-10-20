# Locality factor input: notes

## Save

- Save: `FORK-CZR-VZ.sve` in `D:\OneDrive\Documents\Simutrans\save`. File date 2026-10-20 19:38:19, 175,634,081 bytes.
  This is the owner's last manual save from playing. It still has the parking tiles: the stripped copy
  `FORK-CZR-VZ-noparking-20261020-130518.sve` from 13:07 is an older state and was not played.
- Exported with the worktree build of `origin/claude/zen-goodall-c07syv` (358ea1f) on 2026-10-20 17:51:41Z.
  `settings.locality_factor` is in the export.
- In-game date: game year 2051, month 7 (`absolute_month` 24618, ticks 3661275097). The calendar date is
  1931-05-02 03:39, from `minutes_per_month` 480 and `bits_per_month` 23.
- Month data: index 0 of every stats array is the current month, only 45.8% done. Index 1 is last month,
  which is complete, and index 2 the month before. City `this_month` is the same partial month, and
  `last_month` is the complete one.

## locality_factor in the config files

- `D:\SteamLibrary\steamapps\common\Simutrans\pak128.cs\config\simuconf.tab`: none.
- `D:\OneDrive\Documents\Simutrans\simuconf.tab` (the user's): none.
- `D:\SteamLibrary\steamapps\common\Simutrans\config\simuconf.tab` (the global one, lines 148-149):
  ```
  locality_factor[0]=1835,1
  locality_factor[1]=2050,2500
  ```
- In the game, saved with it: `locality_factor` 2500 (year 2051 is past 2050). `locality_factor_table` is
  1835 -> 1 and 2050 -> 2500; the other 8 slots are year 0, factor 1 (unused).
- Other settings saved with the game differ from both simuconf files:

  | Setting | Game | Global simuconf | Pakset simuconf |
  |---|---|---|---|
  | `passenger_factor` | 1 | 16 | 16 |
  | `factory_worker_percentage` | 25 | 33 | 33 |
  | `tourist_percentage` | 4 | 16 | 16 |
  | `factory_worker_radius` | 177 | n/a | 77 |
  | `station_coverage` | 2 | n/a | 3 |

  `station_far_reach` 13 comes from the user's simuconf, which overrides the saved value on load.
  `pay_for_total_distance` is 0.

## Owner's answers

- **Which lines count as local city transport (MHD):** all bus, tram and metro lines. That means every
  passenger line that is not a train, regional and intercity buses included. In the data:
  - **Buses:** `type` `road` with 0 in `goods_categories` (0 = passengers, 1 = mail, others freight). There
    are 1,811 such lines; 216 more road lines carry only freight.
  - **Trams:** `type` `tram` with category 0, 69 lines.
  - **Metro:** `type` `rail`, picked out by name: the 4 Praha lines whose names start with `[A]`, `[B]` or
    `[C]` (`[A] Praha,Depo Hostivař - Praha,Nemocnice Motol`, `[A] Praha,Skalka - ...`,
    `[B] Praha,Černý Most - Praha,Zličín`, `[C] Praha,Letňany - Praha,Háje`). Every other passenger rail line
    (164) is a train.
  - **Not MHD:** 7 air and 7 ship passenger lines.
- **Cities the owner cares most about:** Praha, Brno, Ostrava, Olomouc.
- **Owner's note:** "Praha is a difficult city to consider". In the game Praha is 22 separate cities,
  `Praha 1` to `Praha 22`, each with its own entry in `cities` and its own generated and transported figures.
  Brno, Ostrava and Olomouc are one city each.

## Data notes

- 2,475 lines; 411 of them have no convoy. 13,777 stops and 166 cities.
- Passengers transported last month by line type, from the lines' `transported[1]`:
  - rail 9,401,855;
  - road 1,475,613;
  - tram 326,348;
  - air 638;
  - ship 508.
- Mail is hardly transported: Praha 4 delivered 1 of 17,635 mail last month.
