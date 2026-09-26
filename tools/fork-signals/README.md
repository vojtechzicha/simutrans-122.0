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
Image[0..3]=...     like the end-of-choose sign: N,S,W,E
```

Image order matters for the station boundary. A signal shows the image of the direction it applies
to, but a roadsign shows the image of its stored ribi, which is the opposite direction. So the
station boundary's `Image[N]` must show the board as the southbound train sees it, and so on. A sheet
drawn like the signal sheets (column = the direction it applies to) needs N/S and W/E swapped in the
dat, otherwise every LT that looks right applies to the trains leaving the station and the
single-track section is never protected. The pak128.cs set's `build.py` does that swap.

The fastest way to real art is to copy the dat entry of an existing signal and of the end-of-choose
sign in the pakset, give them new names and add the flag line. The pak node format is unchanged, so
the stock game still reads the objects: the platform signal as a plain one-way signal, the station
boundary as a sign with no effect. A stock-format save (`steam-fork.sh downgrade`) writes platform
signals two-way so the stock game can still drive through the stations.

## Building

- pak64 placeholder art (for tests): `python3 make_placeholder_png.py`, then
  `makeobj pak64 ./ fork_signals_pak64.dat` with the fork's makeobj (`make makeobj`), and copy the two
  `roadsign.*.pak` files into the pakset folder.
- pak128.cs on Windows: put the dat and the images in a folder and run
  `tools/windows/steam-fork.sh signals FOLDER/FILE.dat` (builds the fork's makeobj and writes the paks
  into the pakset folder in the Steam game dir).

The objects are built on their own, so the pakset itself does not need rebuilding. If a pakset is
ever built with its own patched makeobj, port the two keys from
`descriptor/writer/roadsign_writer.cc` (search for `is_platformsignal` and `station_boundary`).
