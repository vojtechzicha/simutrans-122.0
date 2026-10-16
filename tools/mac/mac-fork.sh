#!/bin/bash
# Build the fork on macOS and install it as an app that plays the same game as the Windows PCs.
#
# The Windows desktop builds the fork and installs it into Steam and into OneDrive
# (tools/windows/steam-fork.sh). A Mac cannot run that exe, so this script builds the same source
# here and installs ~/Applications/Simutrans Fork.app. Its data folder links the pakset in OneDrive
# (Games/Simutrans/pak128.cs) and the user dir ~/Library/Simutrans links OneDrive's
# Documents/Simutrans: the same pakset, saves and simuconf.tab as on Windows.
#
#   tools/mac/mac-fork.sh deps        Xcode command line tools and the Homebrew packages of the build
#   tools/mac/mac-fork.sh build       build with config.mac-release into build/mac-release
#                                     (config.default, the dev build, is not touched)
#   tools/mac/mac-fork.sh install     make the app, link pakset and user dir, fetch the pakset from OneDrive
#   tools/mac/mac-fork.sh update      git pull (main, fast-forward only) + build + install
#   tools/mac/mac-fork.sh status      installed commit, the Windows build's commit, links
#   tools/mac/mac-fork.sh run         start the app
#   tools/mac/mac-fork.sh uninstall   remove the app and the user dir link (puts back a user dir
#                                     that install moved aside)
#
# Environment: ONEDRIVE (the OneDrive folder, default ~/Library/CloudStorage/OneDrive-*),
# APP (default ~/Applications/Simutrans Fork.app), JOBS.
#
# Written on Windows and checked with bash -n only: the first run on a Mac is its test.
# macOS has bash 3.2 and BSD tools, so keep to what they have (no sha256sum, GNU date or readlink -f).

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd -P)"
APP="${APP:-$HOME/Applications/Simutrans Fork.app}"
CFG=mac-release
BUILT="$REPO/build/$CFG/sim"
BUILT_COMMIT="$BUILT.commit"
USER_DIR="$HOME/Library/Simutrans"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

die() { echo "mac-fork: $*" >&2; exit 1; }
warn() { echo "mac-fork: warning: $*" >&2; }

# the OneDrive folder that holds Games/Simutrans (pakset, the Windows build's commit) and Documents/Simutrans
find_onedrive() {
	local d
	if [ -n "${ONEDRIVE:-}" ]; then
		[ -d "$ONEDRIVE/Games/Simutrans/pak128.cs" ] || die "ONEDRIVE=$ONEDRIVE has no Games/Simutrans/pak128.cs"
		echo "$ONEDRIVE"
		return
	fi
	for d in "$HOME/Library/CloudStorage/OneDrive-Personal" "$HOME/Library/CloudStorage"/OneDrive* "$HOME/OneDrive"; do
		if [ -d "$d/Games/Simutrans/pak128.cs" ]; then
			echo "$d"
			return
		fi
	done
	die "no OneDrive folder with Games/Simutrans/pak128.cs (looked in ~/Library/CloudStorage/OneDrive-* and ~/OneDrive). Sign in to OneDrive and let it sync, or set ONEDRIVE=/path/to/OneDrive"
}

brew_prefix() {
	command -v brew >/dev/null 2>&1 || die "Homebrew not found: install it from https://brew.sh, then run '$0 deps'"
	brew --prefix
}

# commit id in FILE, or nothing
read_commit() {
	[ -f "$1" ] && tr -d '\r\n ' < "$1" || true
}

describe() {
	[ -n "$1" ] || { echo "none"; return; }
	git -C "$REPO" log -1 --format='%h %ci %s' "$1" 2>/dev/null | cut -c1-100 || echo "$1 (not in this repo yet)"
}

# Is the build MINE the commit the Windows desktop built (OneDrive's simutrans-fork.commit)? The
# saves come from that build, and a save from a newer build may not load in an older one.
compare_with_windows() {
	local od="$1" mine="$2" win
	win="$(read_commit "$od/Games/Simutrans/simutrans-fork.commit")"
	if [ -z "$win" ]; then
		warn "no Games/Simutrans/simutrans-fork.commit in OneDrive, cannot compare with the Windows build"
	elif [ "$win" = "$mine" ]; then
		echo "same commit as the Windows build"
	elif ! git -C "$REPO" cat-file -e "$win^{commit}" 2>/dev/null; then
		warn "the Windows build ($win) is not in this repo: push main on the desktop, then run '$0 update'; saves written there may not load here until then"
	elif git -C "$REPO" merge-base --is-ancestor "$win" "$mine"; then
		echo "newer than the Windows build ($(git -C "$REPO" log -1 --format=%h "$win")): if this build changed the save format, run steam-fork.sh update on the desktop before loading saves from here there"
	else
		warn "older than the Windows build ($(git -C "$REPO" log -1 --format=%h "$win")): run '$0 update'"
	fi
}

do_deps() {
	if ! xcode-select -p >/dev/null 2>&1; then
		echo "installing the Xcode command line tools: confirm the dialog, then run '$0 deps' again"
		xcode-select --install || true
		exit 1
	fi
	brew_prefix >/dev/null
	brew install sdl2 freetype libpng zstd pkgconf
}

ensure_config() {
	local cfg="$REPO/config.$CFG" prefix
	prefix="$(brew_prefix)"
	[ -f "$cfg" ] && return
	echo "writing config.$CFG"
	# USE_ZSTD as in the Windows build: without it zstd-compressed saves do not load
	cat > "$cfg" <<EOF
# release build of the fork for macOS, written by tools/mac/mac-fork.sh (config.default stays the dev build)
BACKEND = sdl2
OSTYPE = mac
DEBUG = 0
OPTIMISE = 1
MULTI_THREAD = 1
USE_FREETYPE = 1
USE_ZSTD = 1
AV_FOUNDATION = 1
WITH_REVISION = 0
FREETYPE_CONFIG = pkg-config freetype2
FLAGS = -I$prefix/include
LDFLAGS = -L$prefix/lib
EOF
}

do_build() {
	ensure_config
	local prefix
	prefix="$(brew_prefix)"
	export PATH="$prefix/bin:$PATH"
	export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
	pkg-config --exists sdl2 freetype2 || die "SDL2 or FreeType not found, run '$0 deps'"
	if [ -n "$(git -C "$REPO" status --porcelain --untracked-files=no)" ]; then
		warn "the repo has uncommitted changes; they are built in, but the app is labelled with HEAD"
	fi
	make -C "$REPO" CFG=$CFG -j"$JOBS"
	[ -f "$BUILT" ] || die "build produced no $BUILT"
	git -C "$REPO" rev-parse HEAD > "$BUILT_COMMIT"
	echo "built $BUILT: $(describe "$(read_commit "$BUILT_COMMIT")")"
}

# ~/Library/Simutrans (the macOS user dir, sys/simsys.cc) becomes a link to OneDrive's Documents/Simutrans
link_user_dir() {
	local docs="$1" aside
	if [ -L "$USER_DIR" ]; then
		[ "$(readlink "$USER_DIR")" = "$docs" ] && return
		rm "$USER_DIR"
	elif [ -e "$USER_DIR" ]; then
		aside="$USER_DIR-local-$(date +%Y%m%d-%H%M%S)"
		mv "$USER_DIR" "$aside"
		echo "moved the old local user dir aside to $aside"
	fi
	ln -s "$docs" "$USER_DIR"
	echo "user dir $USER_DIR -> $docs"
}

# OneDrive keeps files online until they are opened: read them once, so the game does not wait for
# downloads (Finder > Always Keep on This Device does the same for good)
fetch_from_onedrive() {
	local pak="$1" docs="$2" save
	echo "making sure the pakset is on this Mac (up to 550 MB from OneDrive the first time) ..."
	find "$pak/" -type f -exec cat {} + > /dev/null || warn "some pakset files could not be read; is OneDrive online?"
	save="$(ls -t "$docs/save/"*.sve 2>/dev/null | head -1 || true)"
	if [ -n "$save" ]; then
		echo "and the newest save, $(basename "$save") ..."
		cat "$save" > /dev/null || warn "could not read $save"
	fi
}

write_launcher() {
	local launcher="$1" od_game="$2"
	{
		echo '#!/bin/sh'
		echo '# Simutrans Fork launcher, written by tools/mac/mac-fork.sh install'
		printf 'ONEDRIVE_GAME=%q\n' "$od_game"
		printf 'REPO=%q\n' "$REPO"
		cat <<'EOF'
RES="$(cd "$(dirname "$0")/../Resources" && pwd -P)"
alert() {
	osascript -e "display alert \"Simutrans Fork\" message \"$1\"" >/dev/null 2>&1 || echo "$1" >&2
}
if [ ! -d "$RES/game/pak128.cs/" ]; then
	alert "The pakset in OneDrive (Games/Simutrans/pak128.cs) is not available. Start OneDrive and let it sync."
	exit 1
fi
if [ ! -f "$HOME/Library/Simutrans/simuconf.tab" ]; then
	alert "The user folder (~/Library/Simutrans -> OneDrive Documents/Simutrans) is not available. Start OneDrive and let it sync, or run tools/mac/mac-fork.sh install again."
	exit 1
fi
# the Windows desktop built a newer fork: its saves may not load in this one
win="$(tr -d '\r\n ' < "$ONEDRIVE_GAME/simutrans-fork.commit" 2>/dev/null || true)"
mine="$(tr -d '\r\n ' < "$RES/simutrans-fork.commit" 2>/dev/null || true)"
if [ -n "$win" ] && [ "$win" != "$mine" ] && ! git -C "$REPO" merge-base --is-ancestor "$win" "$mine" 2>/dev/null; then
	osascript -e 'display notification "The Windows build is newer. Run tools/mac/mac-fork.sh update, or saves from Windows may not load." with title "Simutrans Fork"' >/dev/null 2>&1 || true
fi
cd "$RES/game" && exec ../../MacOS/simutrans -use_workdir "$@"
EOF
	} > "$launcher"
	chmod +x "$launcher"
}

write_plist() {
	local plist="$1" version="$2"
	cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleDisplayName</key>
	<string>Simutrans Fork</string>
	<key>CFBundleName</key>
	<string>Simutrans Fork</string>
	<key>CFBundleExecutable</key>
	<string>launch</string>
	<key>CFBundleIconFile</key>
	<string>simutrans.icns</string>
	<key>CFBundleIdentifier</key>
	<string>com.vojtechzicha.simutrans-fork</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>122.0</string>
	<key>CFBundleVersion</key>
	<string>$version</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.simulation-games</string>
	<key>NSHighResolutionCapable</key>
	<false/>
</dict>
</plist>
EOF
}

check_app_path() {
	case "$APP" in
		*?.app) ;;
		*) die "APP must be a path ending in .app: $APP" ;;
	esac
}

do_install() {
	[ -f "$BUILT" ] && [ -f "$BUILT_COMMIT" ] || die "no build, run '$0 build' first"
	check_app_path
	local od pak docs res game commit
	od="$(find_onedrive)"
	pak="$od/Games/Simutrans/pak128.cs"
	docs="$od/Documents/Simutrans"
	[ -f "$docs/simuconf.tab" ] || die "no $docs/simuconf.tab: OneDrive's Documents/Simutrans (saves, settings) is not on this Mac yet"
	commit="$(read_commit "$BUILT_COMMIT")"
	res="$APP/Contents/Resources"
	game="$res/game"

	# a fresh bundle every time; rm -rf removes the pak128.cs symlink inside, never the pakset itself
	rm -rf "$APP"
	mkdir -p "$APP/Contents/MacOS" "$res"
	# tracked game data of the built commit only (config, fonts, texts with the fork's en.tab and help
	# pages, themes, scripts, music), not the saves, settings or pakset of a dev working tree
	git -C "$REPO" archive "$commit" simutrans | tar -x -C "$res"
	mv "$res/simutrans" "$game"
	ln -s "$pak" "$game/pak128.cs"
	# a new file, never one overwritten in place (macOS kills a binary whose signature cache is stale)
	cp "$BUILT" "$APP/Contents/MacOS/simutrans"
	cp "$BUILT_COMMIT" "$res/simutrans-fork.commit"
	cp "$REPO/OSX/simutrans.icns" "$res/simutrans.icns"
	write_launcher "$APP/Contents/MacOS/launch" "$od/Games/Simutrans"
	write_plist "$APP/Contents/Info.plist" "$(git -C "$REPO" log -1 --format=%h "$commit")"
	/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
	echo "installed $APP: $(describe "$commit")"

	link_user_dir "$docs"
	fetch_from_onedrive "$pak" "$docs"
	compare_with_windows "$od" "$commit"
	echo "done: start 'Simutrans Fork' from Launchpad or Spotlight, or run '$0 run'"
}

do_update() {
	local branch
	branch="$(git -C "$REPO" branch --show-current)"
	if [ "$branch" = main ] && [ -z "$(git -C "$REPO" status --porcelain --untracked-files=no)" ]; then
		git -C "$REPO" pull --ff-only origin main
	else
		warn "not pulling: the repo is on '${branch:-a detached HEAD}' or has local changes; building what is checked out"
	fi
	do_build
	do_install
}

do_status() {
	local od mine
	od="$(find_onedrive)"
	mine="$(read_commit "$APP/Contents/Resources/simutrans-fork.commit")"
	if [ -d "$APP" ]; then
		echo "app:       $APP (installed $(stat -f %Sm "$APP"))"
	else
		echo "app:       not installed ($APP)"
	fi
	echo "installed: $(describe "$mine")"
	echo "repo HEAD: $(describe "$(git -C "$REPO" rev-parse HEAD)")"
	echo "Windows:   $(describe "$(read_commit "$od/Games/Simutrans/simutrans-fork.commit")")"
	[ -n "$mine" ] && compare_with_windows "$od" "$mine"
	echo "pakset:    $(readlink "$APP/Contents/Resources/game/pak128.cs" 2>/dev/null || echo 'not linked')"
	if [ -L "$USER_DIR" ]; then
		echo "user dir:  $USER_DIR -> $(readlink "$USER_DIR")"
	else
		echo "user dir:  $USER_DIR is not a link to OneDrive (run install)"
	fi
}

do_uninstall() {
	check_app_path
	local aside
	# removes the pak128.cs symlink inside, not the pakset
	rm -rf "$APP"
	echo "removed $APP"
	if [ -L "$USER_DIR" ]; then
		rm "$USER_DIR"
		aside="$(ls -d "$USER_DIR"-local-* 2>/dev/null | tail -1 || true)"
		if [ -n "$aside" ]; then
			mv "$aside" "$USER_DIR"
			echo "put back the local user dir from $aside"
		else
			echo "removed the user dir link (the saves stay in OneDrive)"
		fi
	fi
}

case "${1:-}" in
	deps|build|install|update|status|run|uninstall)
		[ "$(uname -s)" = Darwin ] || die "this is the macOS script; on Windows use tools/windows/steam-fork.sh" ;;
esac

case "${1:-}" in
	deps)      do_deps ;;
	build)     do_build ;;
	install)   do_install ;;
	update)    do_update ;;
	status)    do_status ;;
	run)       open "$APP" ;;
	uninstall) do_uninstall ;;
	*)         sed -n '2,25p' "$0"; exit 1 ;;
esac
