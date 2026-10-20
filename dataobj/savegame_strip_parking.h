/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SAVEGAME_STRIP_PARKING_H
#define DATAOBJ_SAVEGAME_STRIP_PARKING_H


class karte_t;

/**
 * Fork: removes the parking tiles (road stops named parking_(catchemnt_area_ext)*, used to stretch a
 * stop's Reach) of a loaded game that the station far reach makes unnecessary, for the command line
 * switch -strip-parking (then -saveas writes the result; without it nothing is removed).
 * Design and measurements: documentation/fork-station-reach-plan.md, section 6.
 *
 * Never removed: a tile a schedule points at, a tile a convoy longer than one tile stands on at such
 * a stop, a tile a bus at a road choose sign may pick, a tile with a standing convoy or a reservation,
 * and every tile of a stop named in keep_path (one stop name per line, UTF-8, # starts a comment).
 * The others go, outermost first, when every house, attraction and factory keeps its stops by the mode:
 *   same     exactly the same stops
 *   near=T   (near = near=2) the same stops, or at least one, and every stop it loses is within T tiles
 *            (nearer or farther) of the nearest stop it keeps or gets (distance to the stop's own tiles)
 *   any      at least one stop
 * for passengers, mail only with compare_mail, goods of factories always the same stops.
 * The removal goes through the remove tool's code (costs as there, unless free); afterwards every
 * customer's stops are compared with what was planned. The report (TSV) lists every parking tile with
 * what happened to it and why, the stops that disappear and every customer whose stops change.
 */
class savegame_strip_parking_t
{
public:
	/// @return false when nothing may be saved: bad arguments, or the game's stops differ from the plan
	static bool apply( karte_t *welt, const char *mode, const char *keep_path, bool compare_mail, bool free, bool dry_run, const char *report_path );
};

#endif
