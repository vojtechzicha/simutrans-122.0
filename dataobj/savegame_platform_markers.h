/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SAVEGAME_PLATFORM_MARKERS_H
#define DATAOBJ_SAVEGAME_PLATFORM_MARKERS_H


class karte_t;

/**
 * Fork, platforms (documentation/fork-platforms-plan.md, section 7): moves the owner's hand-written markers at stops
 * over to the platforms, for the command line switch -platform-markers MODE (with -saveas, else a dry run).
 *
 * MODE numbers: a marker within one tile of a stop whose text (or a part of it between " | ") starts with a
 * platform number gives that platform its number: "N3/7: ..." and "K2: ..." a track (7, 2), "14 - ...", "14: ...",
 * "J - ..." a track or stand (one or two digits with an optional letter, or one capital letter). The platform is
 * the track the marker stands on, else the one platform near it, else the one near it whose lines the text names
 * most; a marker that fits none or several is reported. A stop whose stand markers name destinations after the
 * line numbers ("1: 345 Olomouc, 521 Lipnik") shows its stand cards with destinations.
 *
 * MODE strip: removes a marker within one tile of a stop when it names line numbers and every one of them is a
 * line on the cards of the stops near it (a chip, or the first word of a line name, "Ex1/1,2" and "RJ1/1-7" read as
 * Ex1/1, Ex1/2, RJ1/1 ... RJ1/7; numbers in <> are left out). Markers with anything else stay.
 *
 * MODE both: numbers first, then strip. keep_path names stops (one name per line) whose markers stay as they are.
 * The report (TSV) lists every marker near a stop: x, y, z, what was done or why not, the stop and the text.
 */
class savegame_platform_markers_t
{
public:
	/// @return false on bad arguments
	static bool apply( karte_t *welt, const char *mode, const char *keep_path, const char *report_path );
};

#endif
