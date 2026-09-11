# macOS GUI test helpers

Scripts used to drive the macOS test build from a shell and verify GUI changes by screenshot.
Not part of the game build.

Build the click tool once:

    swiftc -O -o tools/mac-test/click tools/mac-test/click.swift

Typical flow (run from `simutrans/`):

    ./simutrans -use_workdir -objects pak -lang en -nosound -nomidi -screensize 1024x768 -singleuser &
    ../tools/mac-test/shot.sh start          # screenshot + record window geometry
    ../tools/mac-test/wclick.sh 405 492      # left click at content-relative coords
    ../tools/mac-test/wclick.sh 130 172 middle
    ../tools/mac-test/shot.sh after

Notes:
- `click` posts a two-step cursor move before pressing; without it SDL delivers the press with the
  previous cursor position and buttons do not react.
- The very first click after the window gains focus is swallowed by SDL. Send it twice if needed,
  but never double-send a click that has side effects.
- `save/testline.sve` (ignored by git) is a saved game with one road line and a schedule, useful
  for testing the schedule editor: start with `-load testline`.
