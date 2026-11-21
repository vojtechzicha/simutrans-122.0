# Platform display and track/stand sign (fork objects)

Pak objects for the platforms feature (`documentation/fork-platforms-plan.md`, section 6). They are decoration:
the cards on the map and the boards work without them.

- **Platform display**: shows the next departure from the track or stand it stands on, a row per platform of
  its tile (a stand display set for both kerbs of a through stop has two): the number, the time, the line and,
  every 3 seconds in turn, the destination or the stops on the way (or, when the train is late, the delay).
- **Track or stand sign**: the platform's number on a plate and up to three of its lines.

The game draws the panel, the plate, the posts and the text itself (`gui/platform_cards.cc`, in the overlay pass,
at the normal zoom and closer), in a 3x5 LED font with Czech accents that steps one pixel down for every two along
the platform (up for platforms running north-south). So the object's own images are only a foot on the ground.
Rail displays are navy and white like the train board, road and tram ones amber on black like the other board.

## Dat keys (fork makeobj only)

```
Obj=roadsign
Name=...
waytype=track | road | tram_track
is_platformdisplay=1     (or is_tracksign=1)
Image[0..3]=...          N, S, W, E, like a sign; at most 4 (more would make it a traffic light)
cursor=... icon=...
```

A stock makeobj ignores the keys (the object becomes a sign with no effect), and so does a stock game reading a pak
made by the fork's makeobj.

## Placing them

With the sign tool on a tile of a track or stand of a stop (refused elsewhere). Clicking it again turns it: one
direction, the other one, both. For a stand that is which kerb it serves; for a track only which way it faces.
Ctrl+click puts one on every track (rail objects) or every stand (road and tram objects) of the stop, on the
middle tile of each track. One sign per tile, as for every sign.

## Building

`platform_objects.png` is placeholder art made by `make_placeholder_png.py` (standard library only): row 0 the
feet, row 1 the toolbar icons in the top left 32x32 pixels of their tile. On Windows,
`tools/windows/steam-fork.sh platforms` builds the fork's makeobj and writes `fork-platform-objects.pak` into the
pakset folder. Elsewhere: `makeobj pak128 OUT.pak platform_objects.dat` with the fork's makeobj (`make makeobj`).
Real art goes into the pakset repo with the same keys.
