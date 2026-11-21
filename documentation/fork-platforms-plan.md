# Platforms: what leaves from where (plan)

Status: implemented, all four phases (2026-11-21, see CLAUDE.md "Platforms"; objects with placeholder art, tools/fork-platforms). Mockups on the owner's Hranice na Moravě:
https://claude.ai/artifact/8tUv6ZQECFpcEDw28TJi7i (made with `tools/mockups`, see its README).

## 1. Why

The owner's game (save of 2026-11-08) holds 61,939 markers. About 14,200 sit within one tile of a stop and
list lines ("N3/7: Praha hlavní nádraží (Ex1/1,2,4…)", "6: 532 Radíkov, 538 Horní újezd, 927 Nový Jičín",
"1,7,8,13 směr aut.st.", "příjezd 2,4,5,6,9"); 1,691 of those start with a track or stand number. About
23,200 others are road signs ("směr Kutná Hora", motorway exits) and are not part of this. The line lists
are written by hand and have to be edited whenever a line changes. The feature generates them from the
schedules and uses the same platform numbers on the boards, in the convoy window and on optional displays.

Decided by the owner: one number per platform (no separate platform/track pair); in-world displays say
"VIA"; no message when a track changes; a display shows the next departure only; tracks show next stop and
destination together, stands show the next stop and can be switched per stop to next stop + destination; the default view is chosen stops plus the stop
under the mouse.

## 2. Platforms

A platform is where one vehicle stands to take passengers.
- **Track** (rail, monorail, maglev, narrow gauge): a row of the stop's tiles on one track, as
  `rail_vehicle_t::get_station_tracks` finds them (tram and road stops of the halt are skipped).
- **Stand** (road, tram): one stop tile, in one travel direction. A through tile has two stands, one per kerb; a
  dead-end tile or bay has one. Per tile, not per row: the owner numbers the tiles of a bus station row as separate
  stands (Hranice 14 and 15). Layover tiles are never stands.
- Ships and aircraft: one platform per stop tile row, listed in the tab and on the board, no cards.

Stand direction of a schedule entry: the heading on arrival, learned from every vehicle that calls (and the way
out from every departure), saved per line entry; until then a guess from the stops before and after it (no route
search: the vehicles teach it within a game hour). A schedule entry keeps naming a tile, as now.

Number: one text of up to 7 characters. Tracks get the next free number across the station when they
appear and keep it (stored, so adding a track never renumbers the others). Stands have none until one is
typed or imported. The number lives on the platform's anchor tile; when that tile goes it moves to the
nearest tile left in the row, when two rows join the older number wins, when a row is cut the part with
the anchor keeps it. Duplicates are allowed and flagged in the tab.

## 3. Lines of a platform

Worked out per stop when a line registers or unregisters there or a schedule changes (with
`haltestelle_t::rebuild_connections`), never per frame; not saved.
1. Lines: passenger lines with at least one convoy that have a loading entry on the platform's tiles (the
   departure board's stream rule: the entry loads and is not followed by another entry of the same stop).
   The line number is the board's (`board_names_t`, board texts of savegame 122.14 included). Convoys
   without a line are not listed.
2. Next stop: the next entry where passengers may get off (`plans_arrival`), skipping waypoints, depots,
   Hold and further entries of this stop. Named with the board's short stop name; the town is dropped when
   it is this stop's town.
3. Groups: lines with the same next stop. A line with several loading entries towards different next
   stops is in each group.
4. A group names its next stop only when the platform has more than one group, or one of its lines also
   boards at another platform of this stop towards another next stop. Otherwise the chips stand alone.
5. Each group has a direction: the first step of the way out, as one of the four screen diagonals.
6. Arrivals only: lines that have only non-loading entries there (All off, Only unload) form an "arr."
   group. Hold entries count for nothing.
7. Form, kept per stop and per kind (saved):
   - Tracks, default **combined**: a table with one row per next stop (arrow, name) and, beside it, every
     run of lines with the same destination (the board's destination) followed by that name. The other
     form is compact (next stop only), for crowded stations.
   - Stands, default **next stop**. The other form is next stop + destination: inside a group every run
     of lines with the same destination is followed by that name, then the arrow and the next stop
     ("345 Olomouc, aut.nádraží ↙ Šromotovo náměstí").
   Switched by the buttons in the Platforms tab ("Tracks: …", "Stands: …"), by Shift+click on a card of
   that kind, or for stands by the marker import where the markers name destinations.
8. Order: groups by their first line; lines by letters, then number (Ex1, Ex4, LE, R8; 1, 2, 13, 521).

## 4. Cards on the map

A card is the platform's label: number box, then group cells. A cell is line chips (train navy, bus blue,
tram red, as on the boards), six to a row, then the arrow and the next stop. Cells flow left to right and
wrap at 420 px (tracks) or 190 px (stands). More than three rows of chips in a cell end in "+N"; more than
four cells end in "+N". One step zoomed out: chips and arrows only. Further out: hidden.
- Place: a track's card starts at the platform end that is further left on screen. A stand's card sits
  beside its kerb, right of the travel direction, joined by a leader. A card that would cover the stop's
  name tag or an earlier card of the stop moves by a row.
- View switch (`env_t`, a key and the display settings): off, chosen stops (default), all stops. A stop is
  chosen in its Platforms tab (saved with the stop). The stop under the mouse and a stop with an open
  window always show their cards.
- Pointing at a card opens the full form: every line with its destination. A click opens the platform
  window; with a build tool selected clicks pass through.
- Drawn in the overlay pass (`grund_t::display_overlay` level), for visible stops, from a layout cached
  per stop and zoom.

## 5. Windows

- **Platforms tab** of the stop window: one row per platform (number input, kind, lines by next stop,
  always named here). Buttons: map labels for this stop, show next stop or next stop + destination, numbers from
  markers, number tracks tool (general tool, click a platform in the world, like Move name). Hovering a
  row marks the platform's tiles.
- **Platform window**: the next departure at window size (the game font doubled with one pixel of extra
  spacing), then the lines by next stop with destinations; a line chip opens the line.
- **Departure boards**: Track column on the train board, Stand column on the other board when a stand of
  the stop has a number. States: planned (the schedule's platform, via colour), confirmed (standing there,
  or route reserved or claim held into the stop; white), changed (confirmed differs from planned; yellow
  box that blinks every 3 s, planned number struck through below). Road: confirmed when the stop
  reservation is held (choose sign).
- **Convoy window**: "Departure: 11:34 (in 14 min), track 6" / "track 3 instead of 7"; the destination
  sign and the hover sign end with the number.
- **Schedule editor**: the entry list names the platform after the stop; the tile tooltip names it while
  picking a stop.

## 6. Displays and signs (objects)

Optional decoration. Road sign flags `PLATFORM_DISPLAY` (bit 13, makeobj `is_platformdisplay=1`) and
`TRACK_SIGN` (bit 14, `is_tracksign=1`), for track, tram and road way types; no direction mask, never a
stop point, ignored by every search. One sign per tile as now; on a road tile it serves one kerb or both
(cycled like a signal), Ctrl+click builds on every stand of the stop. The game draws the panel, the plate, the
posts and the text, so the art is only a foot on the ground; a 3x5 LED font with Czech accents, stepping one pixel down per two along (mirrored
for the other axis), at the normal zoom and closer. A display shows the next departure of the platforms
next to its tile, one row each, alternating destination and "VIA …" every 3 s; rows come from the board's
streams, cached a calendar minute. A track sign shows the number and up to three line chips.

## 7. Moving over from markers

Batch tools like `-strip-parking` (dry run, TSV report, backup, keep file), through `steam-fork.sh`.
- `platform-numbers`: a marker within one tile of a platform starting with `N3/7:`, `K2:`, `14 –`, `J –`
  or `3:` gives that platform its number (from `N3/7` the track). Markers naming two stands are reported.
  A stop whose markers name a destination after a line number is switched to next stop + destination and
  listed in the report.
- `strip-line-markers`: removes a marker at a stop only when every line number in it is on that stop's
  cards (`<…>` numbers ignored). Anything else stays.

## 8. Save, export, network

Savegame 122.18: platform numbers and anchors, the chosen switch and the form per stop, stand direction
per line entry. Older saves: tracks numbered across, stands without. `-saveversion 0.122.0` leaves it out.
JSON: `platforms` per stop (number, kind, tiles, direction, groups), `platform` and `platform_state` per
convoy, `platform` per schedule entry; the viewer lists them. `-rename` takes `PLATFORM` rows (position,
old, new). Numbers change through a tool command (network safe); cards and views are client display.

## 9. Tests

Scenarios with new harness checks: `expect_platform X Y NUMBER`, `expect_lines X Y "Ex1,RJ>Olomouc|…"`,
`expect_platform_state CONVOY planned|confirmed|changed NUMBER`. Cases: a four-track station with a
branch, a through road stop with a line both ways, a bus station of bays, arrival/layover/departure tiles,
numbers through extending, cutting and joining a stop, save and load, a choose signal moving a train.

## 10. Order

1. Platforms, stand direction, lines by next stop, cards with the three views, Platforms tab, platform
   window. This replaces the hand-written line lists.
2. Numbers: typing, the tool, marker import and strip, save 122.18, export.
3. Boards: Track and Stand columns with states, convoy window, schedule editor.
4. Displays and signs: flags, makeobj, LED text, pakset art.

## 11. Open

Nothing at the moment.
