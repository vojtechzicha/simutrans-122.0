# Pixel mockups of fork features

Draws a planned feature in the game's own pixels, over real screenshots of the owner's save, before any
game code exists. Made for `documentation/fork-platforms-plan.md` (platform cards, boards, displays); the
published page is https://claude.ai/artifact/8tUv6ZQECFpcEDw28TJi7i.

## Data

Screenshots and results are not in the repo. They are in OneDrive, `Games/Simutrans/docs/platforms-mockups`
(`img/` and `out/`: the first, rebuilt Kolín scene; `hr/` and `out2/`: Hranice na Moravě from the save of
2026-11-08; `platforms.html`: the page as published; `labels_all.tsv`: every marker of that save as
x, y, z, on a stop or way, text). Point `MOCKUP_DIR` at that folder (default: `tools/mockups/data`).

```
python3 -m venv venv && venv/bin/pip install pillow
MOCKUP_DIR=…/platforms-mockups venv/bin/python3 tools/mockups/make2.py       # out2/*.png
MOCKUP_DIR=…/platforms-mockups venv/bin/python3 tools/mockups/check2.py      # the board check, must say diff 0
MOCKUP_DIR=…/platforms-mockups venv/bin/python3 tools/mockups/build_page.py  # platforms.html
```

Run from the stored data, these give the published page byte for byte.

## Files

- `sim.py`: the game's text and colours in Python: loads `simutrans/font/cyr.bdf` (the owner's UI font)
  the way `display/font.cc` does, converts colours to the 16-bit screen format, draws boxes and text.
- `make.py`: the departure boards (`train_board` follows `gui/departure_board.cc`), windows in the pakset
  skin, the 3x5 LED font and the in-world display and sign sprites; its own `main2` draws the Kolín set.
- `cards.py`: the platform cards (line chips, direction arrows, wrapping, the combined track table).
- `make2.py`, `hrdata.py`: the Hranice na Moravě mockups. `check2.py` redraws the real train board of a
  screenshot and counts the pixels that differ (0 of 221,760).
- `analyse.py`, `hranice.py`: lines per platform and next stops from a JSON export of the save
  (`-load NAME -export FILE.json`; first argument: the folder holding `czr.json`).
- `page-head.html`, `page-body.html`, `page-script.html`, `build_page.py`: the published page.

## Taking the screenshots

- `harness-snap.patch.txt`: temporary test harness commands. Not for committing as code: it reaches into
  private GUI members. `git apply tools/mockups/harness-snap.patch.txt`, build, run, then
  `git checkout tests/fork_test_harness.cc` and build again. Commands: `winsnap halt X Y WX WY FILE [board
  tab] [W H]`, `winsnap convoy|csched|line ID 0 WX WY FILE` (opens the real window and screenshots the
  display), `labels X0 Y0 X1 Y1 [del]` (lists markers, removes them with a sixth word), `cnvbox`,
  `cnvfind`, `haltname`, `cnvname`. `snap X Y ZOOM FILE` is in the harness already.
- `hranice-snaps.txt`, `hranice-windows.txt`, `kolin-scene.txt`: the `-forktest` scripts used. Run them in
  a test work dir (see `tests/run.sh --one`) with the save copied to its `save/` folder, `fontname =
  font/cyr.bdf` added to its simuconf.tab, and `SDL_VIDEODRIVER=dummy … -screensize 1900x1050 -forktest
  scenarios/NAME.txt`. The owner's save loads in about 30 s. Screenshots are BMP data whatever the name:
  convert with `sips -s format png`.
