/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_STATION_REACH_H
#define DATAOBJ_STATION_REACH_H


#include "../halthandle_t.h"
#include "koord.h"
#include "koord3d.h"
#include "../tpl/vector_tpl.h"


class karte_ptr_t;
class fabrik_t;


/**
 * Fork, station far reach (documentation/fork-station-reach-plan.md): a house, attraction or
 * factory that no stop reaches (station_coverage) is served by its nearest stop with a tile within
 * station_far_reach tiles (straight-line distance, ties all serve), for passengers and mail. Stops
 * without a line or convoy for that cargo do not count. The result lives in the far entries at the
 * end of the tile's halt list (planquadrat_t); the display-only potential entries are the nearest
 * passenger stop as if every stop had a line.
 */
class station_reach_t
{
private:
	static karte_ptr_t welt;

	/// while loading or building a map nothing is updated, resume() rebuilds everything
	static int suspended;

	/// tiles marked by mark_halt() and mark_preview()
	static vector_tpl<koord3d> halt_marks;
	static vector_tpl<koord3d> preview_marks;

	static bool get_customer( koord pos, koord &lo, koord &hi );
	static void update_box( koord ul, koord lr );
	static void set_marked( const koord3d &pos, bool mark );

public:
	/// far reach in tiles, 0 when off
	static sint16 get_far_reach();
	static bool is_active() { return get_far_reach() > 0; }
	static bool is_suspended();

	static void suspend() { suspended++; }
	/// the outermost resume() rebuilds all far entries
	static void resume();

	/// a city building, an attraction or the pos tile of a factory
	static bool is_customer( koord pos );

	/// bounding box of the tiles a factory stands on (its pos is not always a corner, e.g. after rotating)
	static void get_factory_box( const fabrik_t *fab, koord &lo, koord &hi );

	/// recomputes the far entries of one tile
	static void update_tile( koord pos );
	/// recomputes every tile that a change of a stop tile at pos can affect
	static void update_area( koord pos );
	/// recomputes everything around a stop whose cargo or line service changed
	static void update_halt( halthandle_t halt );
	/// a stop is deleted: its tiles (bounding box ul..lr) are gone already
	static void halt_removed( halthandle_t halt, koord ul, koord lr );
	/// a house or attraction is removed from the tile
	static void clear_tile( koord pos );
	/// everything; clear first when far entries may sit on tiles that are no customers any more
	static void update_all( bool clear_first );

	/// classes (planquadrat_t::FAR_*) this halt serves for the far reach: handled and carried by a line
	static uint8 get_serving_classes( halthandle_t halt );

	/// display: (un)marks the houses this stop serves or would serve beyond the Reach
	static void mark_halt( halthandle_t halt, bool mark );
	/// display: (un)marks the houses a new passenger stop at pos would take as their nearest stop
	static void mark_preview( koord pos, bool mark );
};

#endif
