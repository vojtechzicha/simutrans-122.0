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
- `simutrans/save/testline.sve` (ignored) is a saved game with one road line and a schedule; start
  with `-load testline` to test the schedule editor without building anything in-game.
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

- The owner plays on Windows. Anything touching input, windowing, sound or fonts must be reasoned
  about for `sys/simsys_w.cc` (GDI) too, not only the SDL2 path, and should be re-verified on Windows.
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

## Open task: run the export on the real save (Windows)

Status 2026-09-11: `-export` and the viewer are done and verified on a pak64 save (macOS), but the
owner's real saves could only be loaded on Windows (see the pakset note above). What is left:
produce `czr.json` from `CZR.sve` there, open it in the viewer, and fix whatever breaks (a loader
crash in export mode, an unfilled field, viewer speed with a real-size file).

Build on Windows with MSYS2, MINGW64 shell, from the repo root (the upstream nightly recipe):

```
pacman -S --needed make mingw-w64-x86_64-gcc mingw-w64-x86_64-freetype mingw-w64-x86_64-zstd \
  mingw-w64-x86_64-libpng mingw-w64-x86_64-brotli mingw-w64-x86_64-bzip2 mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-pkg-config
printf 'BACKEND = gdi\nOSTYPE = mingw\nDEBUG = 0\nOPTIMISE = 1\nMULTI_THREAD = 1\nUSE_FREETYPE = 1\nUSE_ZSTD = 1\nWITH_REVISION = 0\n' > config.default
make -j8
```

Without `STATIC = 1` the exe needs the MinGW DLLs, so run it from the MSYS2 shell (or add
`/mingw64/bin` to PATH). With `STATIC = 1` freetype needs the brotli static workaround the old
`.github/build64-SDL2.sh` did (`git show 846457d54^:.github/build64-SDL2.sh`). Visual Studio
(`Simutrans.sln`, GDI Release) also works but you must supply the .lib files it links against.

Run the export from the Steam game folder so the pakset and the add-ons are found; the user dir
(`Documents\Simutrans`, saves and add-ons) is picked up automatically:

```
cd /d/SteamLibrary/steamapps/common/Simutrans
/path/to/repo/build/default/sim.exe -use_workdir -objects pak128.cs -load CZR \
  -export /c/Users/<user>/Documents/Simutrans/czr.json -nosound -nomidi
```

Do not overwrite the Steam `simutrans.exe`. Loading takes minutes (175 MB save); watch for the
export message in the log, then drop `czr.json` on `tools/saveviewer/index.html`.
