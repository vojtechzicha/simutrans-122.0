# Platform signal and station boundary (fork objects)

Two pak objects for the fork's single-track signalling (design and rules:
`documentation/fork-rail-signalling-plan.md`):

- **Platform signal**: the exit signal at each end of a station track. One-way: it applies only to
  trains leaving the track in its direction, and unlike a stock one-way signal it never makes the
  track one-way.
- **Station boundary** (lichobeznikova tabulka): a sign where a single-track line enters a station,
  just outside the outermost switch, facing the trains that enter.

## Dat keys (fork makeobj only)

```
Obj=roadsign
Name=PlatformSignal
waytype=track
is_signal=1
is_platformsignal=1
Image[0..7]=...     like a normal signal: red N,S,W,E then green N,S,W,E

Obj=roadsign
Name=StationBoundary
waytype=track
station_boundary=1
Image[0..3]=...     N,S,W,E laid out like a signal (see below)

Obj=roadsign
Name=BlockPost
waytype=track
is_signal=1
is_blockpost=1
Image[0..7]=...     like the platform signal: red N,S,W,E then green N,S,W,E

Obj=roadsign
Name=Autoblock
waytype=track
is_signal=1
is_autoblock=1
Image[0..7]=...     like a plain signal: red N,S,W,E then green N,S,W,E (16 with a catenary set),
                    or 0..11 with yellow N,S,W,E after them (24 with a catenary set)
```

- **Block post** (hradlo): a block signal on the open single-track line between two stations, placed
  as a pair (one object per direction on two neighbouring tiles). One-way and never makes the track
  one-way, like the platform signal. It lets a second train follow the first one in the same
  direction (see the signalling plan).
- **Autoblock** (automaticky blok): a plain signal in every respect (one-way like a stock signal,
  same running and reservations) that shows red while another train holds its block ahead, up to
  the next signal or station boundary, and as soon as the head of a passing train is past it. A
  free block shows yellow when the signal or station boundary at its end is red (or the block ends
  at a buffer stop) and green when it is not; without yellow images it shows green. A plain signal
  shows red unless a train has reserved past it, so the autoblock before it shows yellow until the
  train reserves through it. Autoblocks belong on double track or long plain lines, not
  between a platform signal and the next station boundary (any signal there ends section mode).

The station boundary's images are laid out like a signal's: `Image[E]` shows the board as the
eastbound train sees it (with signals on the right: on its right, face towards it), and that sign
applies to eastbound trains. The game picks a roadsign's image by its stored direction, so for this
one sign the fork reads the stored direction as the direction it applies to (`roadsign_t::applies_to`),
not as the blocked one like a stock one-way sign. Do not reorder the images to compensate: the
per-image offsets, the foreground/background layer and the height on slopes belong to the slot, so
reordered art stands on the wrong side and floats on slopes.

A station boundary drawn as an entry signal shows aspects (display only; the stock game shows the
first four images). Give it 8 images, red N,S,W,E then green N,S,W,E, or 12 with yellow N,S,W,E
after them; 16 or 24 add the same set again for track with catenary. Red: no train let in; green:
the train goes to its scheduled platform (or, passing, straight over every switch); yellow: another
platform (or a switch to the side). A choose signal with exactly 12 images (red, green, yellow) gets
the same yellow; choose signals with 8, 16, 20 or 24 images keep the stock layout. With 4 images a
station boundary stays a plain board (D3).

The fastest way to real art is to copy the dat entry of an existing signal and of the end-of-choose
sign in the pakset, give them new names and add the flag line. The pak node format is unchanged, so
the stock game still reads the objects: the platform signal as a plain one-way signal, the station
boundary as a sign with no effect. A stock-format save (`steam-fork.sh downgrade`) writes platform
signals two-way so the stock game can still drive through the stations.

## Building

- pak64 placeholder art (for tests): `python3 make_placeholder_png.py`, then
  `makeobj pak64 ./ fork_signals_pak64.dat` with the fork's makeobj (`make makeobj`), and copy the
  `roadsign.*.pak` files into the pakset folder.
- pak128.cs on Windows: put the dat and the images in a folder and run
  `tools/windows/steam-fork.sh signals FOLDER/FILE.dat` (builds the fork's makeobj and writes the paks
  into the pakset folder in the Steam game dir).

The objects are built on their own, so the pakset itself does not need rebuilding. If a pakset is
ever built with its own patched makeobj, port the four keys from
`descriptor/writer/roadsign_writer.cc` (search for `is_platformsignal`, `station_boundary`, `is_blockpost`
and `is_autoblock`).
