#!/usr/bin/env bash
# Build the fork on Windows and run it through Steam.
#
# Steam starts <Steam game dir>/simutrans.exe. This script keeps the exe Steam
# shipped as simutrans-stock.exe next to it and copies the fork build over
# simutrans.exe, so the normal Steam "Play" button runs the fork. Run it from
# Git Bash (the MSYS2 MinGW64 toolchain does the compiling).
#
#   tools/windows/steam-fork.sh build              make the fork (STATIC = 1, no DLLs needed)
#   tools/windows/steam-fork.sh install            back up the Steam exe, copy the fork in, add the
#                                                  fork's English texts (text/en.tab) and help pages
#   tools/windows/steam-fork.sh update             build + install
#   tools/windows/steam-fork.sh restore            put the Steam exe back
#   tools/windows/steam-fork.sh status             which exe is installed right now
#   tools/windows/steam-fork.sh downgrade NAME [OUT.sve]
#                                                  rewrite the save NAME (from the user save dir)
#                                                  as OUT.sve in save version 0.122.0, readable by
#                                                  the stock game; default OUT is NAME-122.0.sve
#   tools/windows/steam-fork.sh export NAME OUT.json
#                                                  JSON dump of a save (tools/saveviewer)
#   tools/windows/steam-fork.sh signals FILE.dat [SIZE]
#                                                  build the fork's makeobj and compile FILE.dat (the
#                                                  platform signal and station boundary, see
#                                                  tools/fork-signals) into the pakset folder; SIZE 128
#
# Override the locations with STEAM_DIR, SIM_USER_DIR and PAKSET in the environment.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STEAM_DIR="${STEAM_DIR:-/d/SteamLibrary/steamapps/common/Simutrans}"
SIM_USER_DIR="${SIM_USER_DIR:-/d/OneDrive/Documents/Simutrans}"
PAKSET="${PAKSET:-pak128.cs}"
MSYS_BASH="${MSYS_BASH:-/c/msys64/usr/bin/bash.exe}"
JOBS="${JOBS:-20}"

BUILT="$REPO/build/default/sim.exe"
MAKEOBJ="$REPO/build/default/makeobj/makeobj.exe"
STEAM_EXE="$STEAM_DIR/simutrans.exe"
STOCK_EXE="$STEAM_DIR/simutrans-stock.exe"
FORK_HASH="$STEAM_DIR/simutrans-fork.sha256"
STEAM_EN_TAB="$STEAM_DIR/text/en.tab"
STOCK_EN_TAB="$STEAM_DIR/text/en.tab-stock"
FORK_TEXT_MARK="# fork texts, added by tools/windows/steam-fork.sh"

die() { echo "steam-fork: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

ensure_config() {
	if [ ! -f "$REPO/config.default" ]; then
		echo "writing config.default for the MinGW GDI build"
		cat > "$REPO/config.default" <<-'EOF'
		BACKEND = gdi
		OSTYPE = mingw
		DEBUG = 0
		OPTIMISE = 1
		MULTI_THREAD = 1
		USE_FREETYPE = 1
		USE_ZSTD = 1
		WITH_REVISION = 0
		STATIC = 1
		FREETYPE_CONFIG = pkg-config freetype2
		EOF
	fi
	grep -q '^STATIC *= *1' "$REPO/config.default" || echo "warning: config.default has no STATIC = 1, the exe will need the MinGW DLLs on PATH" >&2
}

do_build() {
	ensure_config
	[ -x "$MSYS_BASH" ] || die "MSYS2 not found at $MSYS_BASH (winget install MSYS2.MSYS2)"
	MSYSTEM=MINGW64 "$MSYS_BASH" -lc "cd '$REPO' && make -j$JOBS"
	[ -f "$BUILT" ] || die "build produced no $BUILT"
	echo "built $BUILT"
}

# Steam replaces simutrans.exe on a game update. If the exe in the game dir is
# neither our fork nor the stock copy we already have, it is a new stock version.
backup_stock() {
	[ -f "$STEAM_EXE" ] || die "no $STEAM_EXE, is the Steam game installed?"
	local cur; cur="$(sha "$STEAM_EXE")"
	if [ -f "$FORK_HASH" ] && [ "$cur" = "$(cat "$FORK_HASH")" ]; then
		echo "installed exe is the fork, stock backup kept"
		return
	fi
	if [ -f "$STOCK_EXE" ] && [ "$cur" = "$(sha "$STOCK_EXE")" ]; then
		echo "installed exe is the stock version, backup is current"
		return
	fi
	if [ -f "$STOCK_EXE" ]; then
		local dated="$STEAM_DIR/simutrans-stock-$(date -r "$STOCK_EXE" +%Y%m%d).exe"
		echo "new stock exe from Steam, previous backup kept as $(basename "$dated")"
		mv -f "$STOCK_EXE" "$dated"
	fi
	cp -p "$STEAM_EXE" "$STOCK_EXE"
	echo "backed up Steam exe to $(basename "$STOCK_EXE")"
}

do_install() {
	[ -f "$BUILT" ] || die "no build, run '$0 build' first"
	backup_stock
	cp -f "$BUILT" "$STEAM_EXE"
	sha "$STEAM_EXE" > "$FORK_HASH"
	echo "installed fork as $STEAM_EXE"
	install_texts
}

# The Steam text/en.tab is newer than 122.0, so keep it and append the pairs of the fork's
# en.tab whose key it lacks (a later pair wins when the game reads the file). The help pages
# the fork changed replace Steam's, whose originals are kept as NAME-stock.
install_texts() {
	[ -f "$STEAM_EN_TAB" ] || die "no $STEAM_EN_TAB"
	if ! grep -qF "$FORK_TEXT_MARK" "$STEAM_EN_TAB"; then
		# no fork texts in it: Steam's own file (first install or a Steam update)
		cp -p "$STEAM_EN_TAB" "$STOCK_EN_TAB"
	fi
	[ -f "$STOCK_EN_TAB" ] || die "no stock backup at $STOCK_EN_TAB"
	local tmp="$STEAM_EN_TAB.tmp"
	{
		cat "$STOCK_EN_TAB"
		printf '%s\r\n' "$FORK_TEXT_MARK"
		# pairs: every non-comment line after the language name is key, value, key, value, ...
		LC_ALL=C awk '
			{ sub(/\r$/, "") }
			FNR == 1 || /^#/ { next }
			FILENAME == ARGV[1] { if( ++n % 2 ) have[$0] = 1; next }
			++m % 2 { key = $0; next }
			!(key in have) { printf "%s\r\n%s\r\n", key, $0; added++ }
			END { print added + 0 " fork texts added to en.tab" > "/dev/stderr" }
		' "$STOCK_EN_TAB" "$REPO/simutrans/text/en.tab"
	} > "$tmp"
	mv -f "$tmp" "$STEAM_EN_TAB"
	local f
	for f in $(git -C "$REPO" diff --name-only 122.0 -- simutrans/text/en/); do
		local dst="$STEAM_DIR/${f#simutrans/}"
		if [ -f "$dst" ] && [ ! -f "$dst-stock" ]; then
			cp -p "$dst" "$dst-stock"
		fi
		cp -f "$REPO/$f" "$dst"
	done
	echo "installed fork texts into $STEAM_DIR/text"
}

restore_texts() {
	if [ -f "$STOCK_EN_TAB" ]; then
		cp -f "$STOCK_EN_TAB" "$STEAM_EN_TAB"
	fi
	local f
	for f in "$STEAM_DIR"/text/en/*-stock; do
		[ -f "$f" ] && cp -f "$f" "${f%-stock}"
	done
	echo "restored stock texts"
}

do_restore() {
	[ -f "$STOCK_EXE" ] || die "no stock backup at $STOCK_EXE"
	cp -f "$STOCK_EXE" "$STEAM_EXE"
	rm -f "$FORK_HASH"
	echo "restored stock exe to $STEAM_EXE"
	restore_texts
}

do_status() {
	[ -f "$STEAM_EXE" ] || die "no $STEAM_EXE"
	local cur; cur="$(sha "$STEAM_EXE")"
	if [ -f "$FORK_HASH" ] && [ "$cur" = "$(cat "$FORK_HASH")" ]; then
		echo "Steam runs the fork ($(date -r "$STEAM_EXE" '+%Y-%m-%d %H:%M'))"
	elif [ -f "$STOCK_EXE" ] && [ "$cur" = "$(sha "$STOCK_EXE")" ]; then
		echo "Steam runs the stock exe"
	else
		echo "Steam runs an exe this script did not install"
	fi
	[ -f "$STOCK_EXE" ] && echo "stock backup: $STOCK_EXE ($(date -r "$STOCK_EXE" '+%Y-%m-%d'))"
	[ -f "$BUILT" ] && echo "last build:   $BUILT ($(date -r "$BUILT" '+%Y-%m-%d %H:%M'))"
}

# run the fork headless from the Steam dir, so the pakset and the add-ons are found
run_batch() {
	local exe="$BUILT"
	[ -f "$exe" ] || exe="$STEAM_EXE"
	[ -f "$exe" ] || die "no fork exe to run"
	(cd "$STEAM_DIR" && PATH="/c/msys64/mingw64/bin:$PATH" "$exe" -use_workdir -objects "$PAKSET" -nosound -nomidi "$@")
}

do_downgrade() {
	local name="${1:-}"; [ -n "$name" ] || die "usage: $0 downgrade NAME [OUT.sve]"
	name="${name%.sve}"
	local out="${2:-$SIM_USER_DIR/save/$name-122.0.sve}"
	[ -f "$SIM_USER_DIR/save/$name.sve" ] || die "no save $SIM_USER_DIR/save/$name.sve"
	echo "writing $out with save version 0.122.0"
	run_batch -load "$name" -saveas "$out" -saveversion 0.122.0 | grep -i 'saving game\|error\|WRONGSAVE' || true
	[ -f "$out" ] || die "no output file written"
	echo "done: $out ($(du -h "$out" | cut -f1))"
}

do_export() {
	local name="${1:-}" out="${2:-}"
	[ -n "$name" ] && [ -n "$out" ] || die "usage: $0 export NAME OUT.json"
	run_batch -load "${name%.sve}" -export "$out" | grep -i 'error\|WRONGSAVE' || true
	[ -f "$out" ] || die "no output file written"
	echo "done: $out ($(du -h "$out" | cut -f1))"
}

# the fork's makeobj knows is_platformsignal and station_boundary; the objects go into the pakset
# folder as an add-on, the rest of the pakset stays as Steam installed it
do_signals() {
	local dat="${1:-}" size="${2:-128}"
	[ -n "$dat" ] && [ -f "$dat" ] || die "usage: $0 signals FILE.dat [SIZE]"
	ensure_config
	[ -x "$MSYS_BASH" ] || die "MSYS2 not found at $MSYS_BASH (winget install MSYS2.MSYS2)"
	# makeobj/Makefile adds -march=pentium for mingw, which the 64 bit compiler refuses
	MSYSTEM=MINGW64 "$MSYS_BASH" -lc "cd '$REPO' && make -j$JOBS makeobj OS_OPT='-DPNG_STATIC -DZLIB_STATIC'"
	[ -f "$MAKEOBJ" ] || die "build produced no $MAKEOBJ"
	local dir; dir="$(cd "$(dirname "$dat")" && pwd)"
	(cd "$dir" && PATH="/c/msys64/mingw64/bin:$PATH" "$MAKEOBJ" "pak$size" "$STEAM_DIR/$PAKSET/" "$(basename "$dat")")
	echo "done: objects from $dat are in $STEAM_DIR/$PAKSET/"
}

case "${1:-}" in
	build)     do_build ;;
	install)   do_install ;;
	update)    do_build; do_install ;;
	restore)   do_restore ;;
	status)    do_status ;;
	downgrade) shift; do_downgrade "$@" ;;
	export)    shift; do_export "$@" ;;
	signals)   shift; do_signals "$@" ;;
	*)         sed -n '2,25p' "$0"; exit 1 ;;
esac
