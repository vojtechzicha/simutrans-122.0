#!/bin/bash
# Fork: runs the headless test scenarios (tests/scenarios/*.txt) with the game's -forktest batch mode and
# prints PASS/FAIL per scenario. See tests/README.md.
#
#   tests/run.sh [-e EXE] [-j N] [-t SECONDS] [NAME...]   run the named scenarios (default: all)
#   tests/run.sh --clean                                  remove the work dirs (pakset links first)
#
#   -e EXE      the game to test (default build/default/sim.exe, or build/default/sim)
#   -j N        scenarios run in parallel (default 4)
#   -t SECONDS  time limit per scenario (default 1800)
#
# Environment: PAKSET (the pak128.cs folder, default OneDrive's Games/Simutrans/pak128.cs),
# TESTS_WORK (where the work dirs go, default %LOCALAPPDATA%/simutrans-fork-tests or $TMPDIR/...).
# Each scenario runs in a work dir of its own (TESTS_WORK/NAME) with copies of simutrans/config, text,
# font and themes, tests/simuconf.tab, the scenarios and layouts, and a link to the pakset. The work
# dirs are kept outside the repo, so `git clean` never walks into the pakset through that link.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TESTS=$ROOT/tests

case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*) WINDOWS=1 ;;
	*) WINDOWS=0 ;;
esac

if [ -z "${TESTS_WORK:-}" ]; then
	if [ $WINDOWS = 1 ] && [ -n "${LOCALAPPDATA:-}" ]; then
		TESTS_WORK=$(cygpath -u "$LOCALAPPDATA")/simutrans-fork-tests
	else
		TESTS_WORK=${TMPDIR:-/tmp}/simutrans-fork-tests
	fi
fi

find_pakset() {
	if [ -n "${PAKSET:-}" ]; then
		echo "$PAKSET"
		return
	fi
	local d
	for d in /d/OneDrive/Games/Simutrans/pak128.cs "$HOME/Library/CloudStorage"/OneDrive*/Games/Simutrans/pak128.cs "$HOME/OneDrive/Games/Simutrans/pak128.cs"; do
		if [ -d "$d" ]; then
			echo "$d"
			return
		fi
	done
}

# removes a link to a folder without touching what it points to
unlink_dir() {
	if [ $WINDOWS = 1 ]; then
		cmd //c rmdir "$(cygpath -w "$1")" > /dev/null 2>&1
	else
		rm -f "$1"
	fi
}

link_dir() {
	if [ $WINDOWS = 1 ]; then
		cmd //c mklink //J "$(cygpath -w "$1")" "$(cygpath -w "$2")" > /dev/null
	else
		ln -s "$2" "$1"
	fi
}

if [ "${1:-}" = "--clean" ]; then
	[ -d "$TESTS_WORK" ] || exit 0
	for wd in "$TESTS_WORK"/*/; do
		[ -e "$wd/pak128.cs" ] && unlink_dir "$wd/pak128.cs"
		if [ -e "$wd/pak128.cs" ]; then
			echo "could not unlink $wd/pak128.cs, leaving $wd alone" >&2
			continue
		fi
		rm -rf "$wd"
	done
	rmdir "$TESTS_WORK" 2> /dev/null
	exit 0
fi

# one scenario: tests/run.sh --one NAME EXE SECONDS (used by the parallel runner below)
if [ "${1:-}" = "--one" ]; then
	name=$2; exe=$3; limit=$4
	wd=$TESTS_WORK/$name
	mkdir -p "$wd"
	if [ ! -e "$wd/pak128.cs" ]; then
		link_dir "$wd/pak128.cs" "$PAKSET_DIR"
	fi
	for d in config text font themes; do
		rm -rf "${wd:?}/$d"
		cp -r "$ROOT/simutrans/$d" "$wd/"
	done
	rm -rf "${wd:?}/scenarios" "${wd:?}/layouts" "${wd:?}/save"
	cp -r "$TESTS/scenarios" "$TESTS/layouts" "$wd/"
	mkdir -p "$wd/save"
	cp "$TESTS/simuconf.tab" "$wd/simuconf.tab"
	rm -f "$wd/settings.xml"
	cd "$wd" || exit 1
	run=()
	if command -v timeout > /dev/null; then
		run=(timeout "$limit")
	fi
	start=$(date +%s)
	SDL_VIDEODRIVER=dummy ${run[@]+"${run[@]}"} "$exe" -use_workdir -singleuser -objects pak128.cs/ -nosound -nomidi -forktest "scenarios/$name.txt" > "$name.out" 2>&1
	code=$?
	took=$(( $(date +%s) - start ))
	log=$wd/scenarios/$name.txt.log
	result=$(grep -a '^RESULT ' "$log" 2> /dev/null | tail -1 | cut -d' ' -f2)
	[ -z "$result" ] && result="NO-RESULT(exit $code)"
	printf '%-28s %-8s %4ds  %s\n' "$name" "$result" "$took" "$log"
	exit 0
fi

exe=""
jobs=4
limit=1800
while getopts "e:j:t:" opt; do
	case $opt in
		e) exe=$OPTARG ;;
		j) jobs=$OPTARG ;;
		t) limit=$OPTARG ;;
		*) sed -n '2,10p' "$0"; exit 2 ;;
	esac
done
shift $((OPTIND-1))

if [ -z "$exe" ]; then
	exe=$ROOT/build/default/sim.exe
	[ -x "$exe" ] || exe=$ROOT/build/default/sim
fi
case "$exe" in /*) ;; *) exe=$(pwd)/$exe ;; esac
if [ ! -x "$exe" ]; then
	echo "no game at $exe: build it first (make), or pass -e EXE" >&2
	exit 2
fi
PAKSET_DIR=$(find_pakset)
if [ -z "$PAKSET_DIR" ] || [ ! -d "$PAKSET_DIR" ]; then
	echo "no pak128.cs found: set PAKSET=/path/to/pak128.cs" >&2
	exit 2
fi
export PAKSET_DIR TESTS_WORK

names=("$@")
if [ ${#names[@]} = 0 ]; then
	for f in "$TESTS"/scenarios/*.txt; do
		names+=("$(basename "$f" .txt)")
	done
fi
for n in "${names[@]}"; do
	if [ ! -f "$TESTS/scenarios/$n.txt" ]; then
		echo "no scenario tests/scenarios/$n.txt" >&2
		exit 2
	fi
done

echo "game:    $exe"
echo "pakset:  $PAKSET_DIR"
echo "work:    $TESTS_WORK"
mkdir -p "$TESTS_WORK"
summary=$TESTS_WORK/summary.txt
printf '%s\n' "${names[@]}" | xargs -P "$jobs" -I{} bash "$0" --one {} "$exe" "$limit" | tee "$summary"

failed=$(grep -vc ' PASS ' "$summary")
echo
for n in "${names[@]}"; do
	log=$TESTS_WORK/$n/scenarios/$n.txt.log
	if ! grep -aq '^RESULT PASS' "$log" 2> /dev/null; then
		echo "--- $n:"
		grep -a -E '^(FAIL|ERROR)' "$log" 2> /dev/null | head -10
	fi
done
echo "$(( ${#names[@]} - failed )) of ${#names[@]} passed"
[ "$failed" = 0 ]
