#!/bin/zsh
# usage: shot.sh NAME [OUTDIR] -> captures the running Simutrans window into OUTDIR/NAME.png
# and writes the window geometry (x,y,w,h in screen points) to OUTDIR/geo.txt for wclick.sh.
OUT=${2:-/tmp/simutrans-test}; mkdir -p "$OUT"
pid=$(pgrep -f "simutrans -use_workdir" | head -1)
[ -z "$pid" ] && { echo "simutrans not running" >&2; exit 1; }
geo=$(osascript -e 'tell application "System Events" to tell (first process whose unix id is '$pid')
set frontmost to true
set p to position of window 1
set s to size of window 1
return (item 1 of p as text) & "," & (item 2 of p as text) & "," & (item 1 of s as text) & "," & (item 2 of s as text)
end tell')
echo "$geo" > "$OUT/geo.txt"
sleep 0.5
screencapture -x -R "$geo" "$OUT/$1.png"
echo "geo=$geo file=$OUT/$1.png"
