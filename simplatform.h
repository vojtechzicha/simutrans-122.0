/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMPLATFORM_H
#define SIMPLATFORM_H


#include <string.h>

#include "halthandle_t.h"
#include "linehandle_t.h"
#include "convoihandle_t.h"
#include "dataobj/koord3d.h"
#include "dataobj/ribi.h"
#include "tpl/vector_tpl.h"
#include "simtypes.h"


class schedule_t;
class haltestelle_t;
class cbuffer_t;
class convoi_t;
class player_t;


/*
 * Fork: platforms (documentation/fork-platforms-plan.md). A platform is where one vehicle stands to take
 * passengers: a station track (a row of the stop's tiles on one track), or a stand (a stop tile of a road or tram
 * stop, in one travel direction; a through tile has two, a dead end one). Ships and
 * aircraft get one per stop tile. The lines that board there are worked out from the schedules, grouped by the
 * next stop they go to; nothing of that is saved. What is saved is the number of each platform (the stop's
 * platform labels) and the travel directions the lines' vehicles were seen to take (simline_t::entry_ways).
 */


/// a line boarding at a platform (one schedule entry)
struct platform_member_t
{
	linehandle_t line;
	uint8 entry;              ///< the schedule entry at this stop
	waytype_t wt;             ///< the line's waytype (the chip colour)
	bool numbered;            ///< number is a public line number; else a category or the line id
	char number[24];          ///< what the line chip says
	char destination[96];     ///< the departure board's destination
	halthandle_t dest_halt;

	platform_member_t() : entry(0), wt(invalid_wt), numbered(false) { number[0] = 0; destination[0] = 0; }
};


/// the lines of a platform that go to the same next stop
struct platform_group_t
{
	halthandle_t next;        ///< the next stop where passengers may get off
	char next_name[96];       ///< as a departure board names it
	ribi_t::ribi out;         ///< the way the vehicles leave (one direction), 0 unknown
	bool name_shown;          ///< show the next stop on a compact card (plan 3, rule 4)
	vector_tpl<platform_member_t> members;

	platform_group_t() : out(0), name_shown(false) { next_name[0] = 0; }
};


/// line chips with what they have in common: one chip per line number, and the destinations they go to
struct platform_run_t
{
	vector_tpl<const platform_member_t *> chips;
	char destination[160];    ///< empty when made without destinations

	platform_run_t() { destination[0] = 0; }
	platform_run_t(const platform_run_t &o) { copy( o ); }
	platform_run_t &operator=(const platform_run_t &o) { if(  this != &o  ) { copy( o ); } return *this; }
	void copy(const platform_run_t &o) {
		chips.clear();
		for(  uint32 i = 0;  i < o.chips.get_count();  i++  ) {
			chips.append( o.chips[i] );
		}
		memcpy( destination, o.destination, sizeof(destination) );
	}
};


class platform_t
{
public:
	enum kind_t { TRACK = 0, STAND = 1, OTHER = 2 };

	uint8 kind;
	waytype_t wt;                   ///< the stop's waytype (of its building)
	vector_tpl<koord3d> tiles;      ///< along the row, lowest coordinate first
	ribi_t::ribi axis;              ///< ribi_t::northsouth or ribi_t::eastwest, 0 for a tile on its own
	ribi_t::ribi dir;               ///< stands: the travel direction there (one direction, towards the closed end at a dead end); 0 otherwise
	bool dead_end;                  ///< stands: the row ends in a dead end
	char number[8];                 ///< empty: none
	vector_tpl<platform_group_t *> groups;   ///< owned
	vector_tpl<platform_member_t> arrivals;   ///< lines that only let passengers off here

	platform_t() : kind(TRACK), wt(invalid_wt), axis(0), dir(0), dead_end(false) { number[0] = 0; }
	~platform_t() { FOR( vector_tpl<platform_group_t *>, g, groups ) { delete g; } }

	bool contains(koord3d k) const { return tiles.is_contained(k); }
	bool has_lines() const { return !groups.empty()  ||  !arrivals.empty(); }

	/// the tile its number belongs to
	koord3d get_anchor() const { return tiles.empty() ? koord3d::invalid : tiles[0]; }

	/// "Track", "Stand" or "Pier"/"Gate" (translated)
	const char *get_kind_name() const;
};


/// a stop's platform number, kept with the stop (saved, savegame 122.18)
struct platform_label_t
{
	koord3d anchor;    ///< a tile of the platform (the first of its row when the number was given)
	uint8 dir;         ///< stands: the travel direction; 0 for tracks
	char text[8];

	platform_label_t() : anchor(koord3d::invalid), dir(0) { text[0] = 0; }
};


/// the worked out platforms of a stop (not saved)
class halt_platforms_t
{
public:
	vector_tpl<platform_t *> list;
	uint32 stamp;     ///< haltestelle_t::platform_stamp it was made for
	uint32 epoch;     ///< platforms_t::epoch it was made for

	halt_platforms_t() : stamp(0xFFFFFFFFu), epoch(0xFFFFFFFFu) {}
	~halt_platforms_t() { clear(); }
	void clear();
};


class platforms_t
{
public:
	/// bumped when names change (stops, lines) or lines get or lose all their convoys: every stop's platforms are made again
	static uint32 epoch;
	static void names_changed() { epoch++; }

	/// is this the kind of platform a card is drawn for (tracks and stands)
	static bool has_card(const platform_t *p) { return p->kind != platform_t::OTHER; }

	/// works out the platforms of the stop; also gives its tracks numbers, see plan 2
	static void build(haltestelle_t *halt, halt_platforms_t &out);

	/// the platform of the stop a vehicle stands on at pos heading dir (dir 0: the first one with the tile); -1 none
	static sint32 find(const vector_tpl<platform_t *> &list, koord3d pos, ribi_t::ribi dir);

	/// the platform where entry of the line boards (or would board) at halt; -1 none
	static sint32 find_entry_platform(haltestelle_t *halt, linehandle_t line, uint8 entry);

	/// the platform of schedule entry (its stop tile and how the vehicles come there); -1 none
	static sint32 find_schedule_platform(const vector_tpl<platform_t *> &list, const schedule_t *schedule, uint8 entry, linehandle_t line);

	/**
	 * Where a departure from entry of schedule (of line, by convoy cnv; NULL: a slot no train is known for) leaves
	 * at halt: planned, the platform of the schedule's stop tile; now, the one the convoy stands at or has reserved
	 * into (rail: its route reserved to the stop, or a claimed track; road: a choose sign sent it to another place),
	 * -1 if not known yet. Indices into halt->get_platforms().
	 */
	static void get_platforms_of(halthandle_t halt, const schedule_t *schedule, uint8 entry, linehandle_t line, const convoi_t *cnv, sint32 &planned, sint32 &now);

	/// sort key comparison of two line numbers: letters, then the number (Ex1, Ex4, LE, R8; 1, 2, 13, 521)
	static int compare_numbers(const char *a, const char *b);

	/// appends ", track 7" for a schedule entry at a stop whose platform has a number (schedule editor)
	static void append_entry_platform(cbuffer_t &buf, const schedule_t *schedule, uint8 entry, linehandle_t line, const player_t *owner);

	/// "track 7", "stand B" (translated); empty when the platform has no number
	static void get_platform_text(cbuffer_t &buf, const platform_t *p);

	/**
	 * The chips of a group or of the arrivals: one per line number (lines of the same number, e.g. Ex1/1 and
	 * Ex1/2, share it). With destinations: each number with the places its lines end, and numbers going to the
	 * same places in one run ("1, 13 Kaufland"); without: one run of all chips.
	 */
	static void get_runs(const vector_tpl<platform_member_t> &members, bool with_destinations, vector_tpl<platform_run_t> &runs);
};


#endif
