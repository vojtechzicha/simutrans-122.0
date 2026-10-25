/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef BAUER_LANDSCAPER_H
#define BAUER_LANDSCAPER_H


#include "../simtypes.h"
#include "../dataobj/koord.h"
#include "../tpl/vector_tpl.h"

class player_t;


/**
 * Fork: shapes the terrain of a rectangle of tiles at once.
 *
 * The plan is a height for every grid point. The area gets the heights asked for as far as the
 * tiles that cannot change (ways, buildings, bridges over it, ...) allow, and the land around it
 * changes only as far as the slopes need, never steeper than a tile allows. Plan first (the tool
 * shows it while dragging), then apply.
 */
class landscaper_t
{
public:
	enum mode_t {
		LEVEL,  ///< every grid point at h_start
		RAISE,  ///< every grid point one level up
		LOWER,  ///< every grid point one level down
		RAMP,   ///< even slope from h_start at the start tile to h_end at the end tile, flat end tiles
		SMOOTH, ///< every grid point the mean of its neighbours
		GENTLE  ///< no slope steeper than one level per tile, cutting and filling about equally
	};

	/// longest side of an area in tiles
	static const sint16 MAX_SIDE = 128;

	landscaper_t() : mode(LEVEL), gentle(true), gx0(0), gy0(0), gw(0), gh(0), corner_changes(0), blocked_tiles(0), ramp_tiles(0), ramp_levels(0) {}

	/**
	 * Plans the change of the tiles from @p start to @p end (any two opposite corners).
	 * @param gentle_edges the land around the area (and the area towards ways and buildings) slopes at
	 *        one level per tile where it can, else at the steepest a tile allows (two with double slopes)
	 * @return NULL or an error; after an error nothing is planned.
	 */
	const char *plan( const player_t *player, mode_t mode, bool gentle_edges, koord start, koord end, sint8 h_start, sint8 h_end );

	/**
	 * Changes the map as planned and books the cost. Call right after plan(), the map must not
	 * have changed in between.
	 * @return NULL or an error; after an error the map is unchanged.
	 */
	const char *apply( player_t *player ) const;

	/// tiles that change (in the area and around it)
	const vector_tpl<koord> &get_changed_tiles() const { return changed_tiles; }

	/// tiles of the area that cannot get the asked heights (ways, buildings or the slopes to them)
	uint32 get_blocked_tiles() const { return blocked_tiles; }

	/// cost as booked (negative), the stock price of raising or lowering a grid point once
	sint64 get_cost() const;

	/// ramp only: tiles that slope and the height it climbs (negative going down)
	sint16 get_ramp_tiles() const { return ramp_tiles; }
	sint16 get_ramp_levels() const { return ramp_levels; }

private:
	mode_t mode;
	bool gentle;

	/// planned region of grid points: origin and size
	sint16 gx0, gy0, gw, gh;
	/// area in tiles
	koord a1, a2;

	vector_tpl<sint16> old_hgt;   ///< current heights
	vector_tpl<sint16> wish_hgt;  ///< asked heights (area points only)
	vector_tpl<sint16> new_hgt;   ///< planned heights

	vector_tpl<koord> changed_tiles;
	sint64 corner_changes; ///< sum of all corner changes of all tiles, as raise_to/lower_to count them
	uint32 blocked_tiles;
	sint16 ramp_tiles, ramp_levels;

	inline uint32 idx( sint16 x, sint16 y ) const { return (uint32)(x - gx0) + (uint32)(y - gy0) * (uint32)gw; }
	/// a point on the border of the planned region that is not on the border of the map
	bool is_rim( sint16 x, sint16 y ) const;
	inline bool in_area_grid( sint16 x, sint16 y ) const { return a1.x <= x  &&  x <= a2.x + 1  &&  a1.y <= y  &&  y <= a2.y + 1; }

	/// asked heights of the area points, NULL or an error
	const char *calc_wishes( koord start, koord end, sint8 h_start, sint8 h_end, vector_tpl<sint16> &wish ) const;

	/// planned heights of the four corners of tile (x,y): sw, se, ne, nw
	void get_corners( const vector_tpl<sint16> &h, sint16 x, sint16 y, sint16 c[4] ) const;

	/// current corners of tile (x,y) as raise_to/lower_to count them: sw, se, ne, nw
	void get_tile_corners( sint16 x, sint16 y, sint16 c[4] ) const;
};

#endif
