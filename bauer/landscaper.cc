/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "landscaper.h"

#include "../simworld.h"
#include "../simdebug.h"
#include "../boden/grund.h"
#include "../descriptor/ground_desc.h"
#include "../dataobj/settings.h"
#include "../dataobj/translator.h"
#include "../player/simplay.h"
#include "../utils/cbuffer_t.h"
#include "../tpl/array_tpl.h"
#include "../utils/for.h"


static karte_ptr_t welt;

// no anchor below or above a point
#define H_NO_LOW  (-30000)
#define H_NO_HIGH (30000)

// a fixpoint of pinning tiles normally takes two or three rounds
#define MAX_PLAN_ROUNDS (64)


static sint32 floordiv( sint32 a, sint32 b )
{
	return a >= 0 ? a / b : -( ( -a + b - 1 ) / b );
}


/**
 * v(p) = min( v(p), v(q) + d ) over the 8 neighbours q, until nothing changes: the lowest
 * height a point may have so that it rises at most d per tile from every given value.
 * Forward and backward raster passes; with uniform steps two are exact, the loop only confirms.
 */
static void spread_up( array_tpl<sint16> &v, sint16 w, sint16 h, sint16 d )
{
	bool changed = true;
	while(  changed  ) {
		changed = false;
		for(  sint16 y = 0;  y < h;  y++  ) {
			for(  sint16 x = 0;  x < w;  x++  ) {
				sint16 best = v[x + y * w];
				if(  x > 0  ) {
					best = min( best, v[x - 1 + y * w] + d );
				}
				if(  y > 0  ) {
					const sint16 *row = &v[(y - 1) * w];
					best = min( best, row[x] + d );
					if(  x > 0  ) {
						best = min( best, row[x - 1] + d );
					}
					if(  x + 1 < w  ) {
						best = min( best, row[x + 1] + d );
					}
				}
				if(  best != v[x + y * w]  ) {
					v[x + y * w] = best;
					changed = true;
				}
			}
		}
		for(  sint16 y = h - 1;  y >= 0;  y--  ) {
			for(  sint16 x = w - 1;  x >= 0;  x--  ) {
				sint16 best = v[x + y * w];
				if(  x + 1 < w  ) {
					best = min( best, v[x + 1 + y * w] + d );
				}
				if(  y + 1 < h  ) {
					const sint16 *row = &v[(y + 1) * w];
					best = min( best, row[x] + d );
					if(  x > 0  ) {
						best = min( best, row[x - 1] + d );
					}
					if(  x + 1 < w  ) {
						best = min( best, row[x + 1] + d );
					}
				}
				if(  best != v[x + y * w]  ) {
					v[x + y * w] = best;
					changed = true;
				}
			}
		}
	}
}


/// the same downwards: v(p) = max( v(p), v(q) - d )
static void spread_down( array_tpl<sint16> &v, sint16 w, sint16 h, sint16 d )
{
	const uint32 n = (uint32)w * (uint32)h;
	for(  uint32 i = 0;  i < n;  i++  ) {
		v[i] = -v[i];
	}
	spread_up( v, w, h, d );
	for(  uint32 i = 0;  i < n;  i++  ) {
		v[i] = -v[i];
	}
}


void landscaper_t::get_corners( const vector_tpl<sint16> &h, sint16 x, sint16 y, sint16 c[4] ) const
{
	c[0] = h[idx( x,     y + 1 )]; // sw
	c[1] = h[idx( x + 1, y + 1 )]; // se
	c[2] = h[idx( x + 1, y     )]; // ne
	c[3] = h[idx( x,     y     )]; // nw
}


void landscaper_t::get_tile_corners( sint16 x, sint16 y, sint16 c[4] ) const
{
	// as the terraformer sees them: a land tile by its ground (shore corners at the water), water by
	// the grid but never above its water
	const grund_t *gr = welt->lookup_kartenboden( x, y );
	if(  gr->is_water()  ) {
		const sint16 w = welt->get_water_hgt( x, y );
		c[0] = min( w, (sint16)welt->lookup_hgt( x,     (sint16)( y + 1 ) ) );
		c[1] = min( w, (sint16)welt->lookup_hgt( (sint16)( x + 1 ), (sint16)( y + 1 ) ) );
		c[2] = min( w, (sint16)welt->lookup_hgt( (sint16)( x + 1 ), y ) );
		c[3] = min( w, (sint16)welt->lookup_hgt( x,     y ) );
	}
	else {
		const sint16 h = gr->get_hoehe();
		const slope_t::type s = gr->get_grund_hang();
		c[0] = h + corner_sw( s );
		c[1] = h + corner_se( s );
		c[2] = h + corner_ne( s );
		c[3] = h + corner_nw( s );
	}
}


const char *landscaper_t::calc_wishes( koord start, koord end, sint8 h_start, sint8 h_end, vector_tpl<sint16> &wish ) const
{
	const sint16 aw = a2.x - a1.x + 2;
	const sint16 ah = a2.y - a1.y + 2;
	const sint16 d = ground_desc_t::double_grounds ? 2 : 1;

	// ramp: grid lines across the main direction, line 0 is the outer edge of the start tile
	const bool along_x = abs( end.x - start.x ) >= abs( end.y - start.y );
	const sint16 lines = along_x ? abs( end.x - start.x ) + 1 : abs( end.y - start.y ) + 1;
	const sint16 dir = along_x ? ( end.x >= start.x ? 1 : -1 ) : ( end.y >= start.y ? 1 : -1 );
	const sint16 first_line = along_x ? ( dir > 0 ? start.x : start.x + 1 ) : ( dir > 0 ? start.y : start.y + 1 );
	const sint32 climb = h_end - h_start;
	// a ramp of three tiles or more keeps both end tiles flat
	const sint16 steps = lines >= 3 ? lines - 2 : lines;
	if(  mode == RAMP  ) {
		if(  abs( climb ) > steps * d  ) {
			static cbuffer_t buf;
			buf.clear();
			buf.printf( translator::translate( "Too steep: this ramp needs at least %d tiles" ), ( abs( climb ) + d - 1 ) / d + ( lines >= 3 ? 2 : 0 ) );
			return buf;
		}
	}

	const sint16 h_max = welt->get_maximumheight();
	wish.clear();
	wish.resize( (uint32)aw * ah );
	for(  sint16 y = a1.y;  y <= a2.y + 1;  y++  ) {
		for(  sint16 x = a1.x;  x <= a2.x + 1;  x++  ) {
			const sint16 here = welt->lookup_hgt( x, y );
			sint32 w = here;
			switch(  mode  ) {
				case LEVEL:
					w = h_start;
					break;
				case RAISE:
					w = here + 1;
					break;
				case LOWER:
					w = here - 1;
					break;
				case RAMP: {
					const sint16 line = ( ( along_x ? x : y ) - first_line ) * dir;
					if(  lines >= 3  ) {
						// flat at both ends, even steps between line 1 and line lines-1
						const sint16 k = max( 0, min( steps, line - 1 ) );
						w = h_start + floordiv( 2 * climb * k + steps, 2 * steps );
					}
					else {
						w = h_start + floordiv( 2 * climb * line + steps, 2 * steps );
					}
					break;
				}
				case SMOOTH: {
					sint32 sum = 0, count = 0;
					for(  sint16 j = -1;  j <= 1;  j++  ) {
						for(  sint16 i = -1;  i <= 1;  i++  ) {
							if(  welt->is_within_grid_limits( (sint16)( x + i ), (sint16)( y + j ) )  ) {
								sum += welt->lookup_hgt( (sint16)( x + i ), (sint16)( y + j ) );
								count++;
							}
						}
					}
					w = floordiv( 2 * sum + count, 2 * count );
					break;
				}
			}
			wish.append( (sint16)max( (sint32)welt->get_minimumheight(), min( (sint32)h_max, w ) ) );
		}
	}

	// a wish steeper than a tile allows (smoothing rounds, map limits) gives way downwards
	array_tpl<sint16> v( (uint32)aw * ah );
	for(  uint32 i = 0;  i < wish.get_count();  i++  ) {
		v[i] = wish[i];
	}
	spread_up( v, aw, ah, d );
	for(  uint32 i = 0;  i < wish.get_count();  i++  ) {
		wish[i] = v[i];
	}
	return NULL;
}


const char *landscaper_t::plan( const player_t *player, mode_t m, koord start, koord end, sint8 h_start, sint8 h_end )
{
	mode = m;
	gw = gh = 0;
	old_hgt.clear();
	wish_hgt.clear();
	new_hgt.clear();
	changed_tiles.clear();
	corner_changes = 0;
	blocked_tiles = 0;
	ramp_tiles = ramp_levels = 0;

	if(  !welt->is_within_limits( start )  ||  !welt->is_within_limits( end )  ) {
		return "";
	}
	a1 = koord( min( start.x, end.x ), min( start.y, end.y ) );
	a2 = koord( max( start.x, end.x ), max( start.y, end.y ) );
	if(  a2.x - a1.x + 1 > MAX_SIDE  ||  a2.y - a1.y + 1 > MAX_SIDE  ) {
		static cbuffer_t buf;
		buf.clear();
		buf.printf( translator::translate( "Area too large (at most %d x %d tiles)" ), MAX_SIDE, MAX_SIDE );
		return buf;
	}
	if(  mode == RAMP  ) {
		const bool along_x = abs( end.x - start.x ) >= abs( end.y - start.y );
		const sint16 lines = along_x ? abs( end.x - start.x ) + 1 : abs( end.y - start.y ) + 1;
		ramp_tiles = lines >= 3 ? lines - 2 : lines;
		ramp_levels = h_end - h_start;
	}

	const sint16 d = ground_desc_t::double_grounds ? 2 : 1;
	vector_tpl<sint16> wish;
	if(  const char *err = calc_wishes( start, end, h_start, h_end, wish )  ) {
		return err;
	}

	// changes reach out of the area at most this far
	sint16 reach = 0;
	const sint16 aw = a2.x - a1.x + 2;
	for(  sint16 y = a1.y;  y <= a2.y + 1;  y++  ) {
		for(  sint16 x = a1.x;  x <= a2.x + 1;  x++  ) {
			reach = max( reach, abs( wish[( x - a1.x ) + ( y - a1.y ) * aw] - welt->lookup_hgt( x, y ) ) );
		}
	}
	if(  reach == 0  ) {
		return NULL;
	}
	const sint16 margin = ( reach + d - 1 ) / d + 1;

	const koord size = welt->get_size();
	gx0 = max( 0, a1.x - margin );
	gy0 = max( 0, a1.y - margin );
	const sint16 gx1 = min( size.x, a2.x + 1 + margin );
	const sint16 gy1 = min( size.y, a2.y + 1 + margin );
	gw = gx1 - gx0 + 1;
	gh = gy1 - gy0 + 1;
	const sint16 tw = gw - 1; // tiles of the region
	const sint16 th = gh - 1;
	const uint32 n = (uint32)gw * gh;

	// never below the sea or a lake next to it, unless already there
	const sint16 sea = welt->get_groundwater();
	array_tpl<sint16> floor( n );
	for(  sint16 y = gy0;  y <= gy1;  y++  ) {
		for(  sint16 x = gx0;  x <= gx1;  x++  ) {
			const sint16 here = welt->lookup_hgt( x, y );
			old_hgt.append( here );
			sint16 water = sea;
			for(  sint16 j = -1;  j <= 0;  j++  ) {
				for(  sint16 i = -1;  i <= 0;  i++  ) {
					if(  welt->is_within_limits( (sint16)( x + i ), (sint16)( y + j ) )  ) {
						water = max( water, (sint16)welt->get_water_hgt( (sint16)( x + i ), (sint16)( y + j ) ) );
					}
				}
			}
			floor[idx( x, y )] = min( here, water );
			wish_hgt.append( in_area_grid( x, y ) ? max( floor[idx( x, y )], wish[( x - a1.x ) + ( y - a1.y ) * aw] ) : here );
		}
	}

	// tiles whose height cannot change; more are pinned when a check fails
	array_tpl<uint8> pinned( (uint32)tw * th, 0 );
	for(  sint16 y = 0;  y < th;  y++  ) {
		for(  sint16 x = 0;  x < tw;  x++  ) {
			pinned[x + y * tw] = !welt->is_plan_height_changeable( gx0 + x, gy0 + y );
		}
	}

	array_tpl<uint8> fixed( n );
	array_tpl<sint16> lo( n ), hi( n );
	for(  int round = 0;  ;  round++  ) {
		if(  round == MAX_PLAN_ROUNDS  ) {
			dbg->warning( "landscaper_t::plan()", "no plan after %d rounds at %s", round, a1.get_str() );
			changed_tiles.clear();
			return "Terraforming not possible here";
		}

		// fixed points: corners of pinned tiles, and the rim of the region inside the map
		for(  sint16 y = gy0;  y <= gy1;  y++  ) {
			for(  sint16 x = gx0;  x <= gx1;  x++  ) {
				fixed[idx( x, y )] = ( x == gx0  &&  x > 0 )  ||  ( y == gy0  &&  y > 0 )  ||  ( x == gx1  &&  x < size.x )  ||  ( y == gy1  &&  y < size.y );
			}
		}
		for(  sint16 y = 0;  y < th;  y++  ) {
			for(  sint16 x = 0;  x < tw;  x++  ) {
				if(  pinned[x + y * tw]  ) {
					fixed[x + y * gw] = fixed[x + 1 + y * gw] = fixed[x + ( y + 1 ) * gw] = fixed[x + 1 + ( y + 1 ) * gw] = 1;
				}
			}
		}

		// what the fixed points allow, then the area as close to its wish as that
		for(  uint32 i = 0;  i < n;  i++  ) {
			lo[i] = fixed[i] ? old_hgt[i] : H_NO_LOW;
			hi[i] = fixed[i] ? old_hgt[i] : H_NO_HIGH;
		}
		spread_down( lo, gw, gh, d );
		spread_up( hi, gw, gh, d );
		new_hgt.clear();
		for(  sint16 y = gy0;  y <= gy1;  y++  ) {
			for(  sint16 x = gx0;  x <= gx1;  x++  ) {
				const uint32 i = idx( x, y );
				sint16 h = old_hgt[i];
				if(  !fixed[i]  &&  in_area_grid( x, y )  ) {
					h = max( lo[i], min( wish_hgt[i], hi[i] ) );
				}
				new_hgt.append( h );
			}
		}

		// the land around follows the area and the fixed points, as little as the slopes allow
		for(  sint16 y = gy0;  y <= gy1;  y++  ) {
			for(  sint16 x = gx0;  x <= gx1;  x++  ) {
				const uint32 i = idx( x, y );
				const bool anchor = fixed[i]  ||  in_area_grid( x, y );
				lo[i] = anchor ? new_hgt[i] : H_NO_LOW;
				hi[i] = anchor ? new_hgt[i] : H_NO_HIGH;
			}
		}
		spread_down( lo, gw, gh, d );
		spread_up( hi, gw, gh, d );
		for(  sint16 y = gy0;  y <= gy1;  y++  ) {
			for(  sint16 x = gx0;  x <= gx1;  x++  ) {
				const uint32 i = idx( x, y );
				if(  !fixed[i]  &&  !in_area_grid( x, y )  ) {
					new_hgt[i] = max( lo[i], min( old_hgt[i], hi[i] ) );
				}
			}
		}

		// check every tile that changes; pin those that cannot and plan again
		bool all_ok = true;
		for(  sint16 y = 0;  y < th;  y++  ) {
			for(  sint16 x = 0;  x < tw;  x++  ) {
				const sint16 tx = gx0 + x, ty = gy0 + y;
				sint16 c[4], o[4], f[4];
				get_corners( new_hgt, tx, ty, c );
				get_corners( old_hgt, tx, ty, o );
				f[0] = floor[idx( tx, ty + 1 )];
				f[1] = floor[idx( tx + 1, ty + 1 )];
				f[2] = floor[idx( tx + 1, ty )];
				f[3] = floor[idx( tx, ty )];
				bool up = false, down = false, bad = false;
				for(  int k = 0;  k < 4;  k++  ) {
					up |= c[k] > o[k];
					if(  c[k] < o[k]  ) {
						down = true;
						bad |= c[k] < f[k];
					}
					bad |= c[k] > welt->get_maximumheight();
				}
				if(  !up  &&  !down  ) {
					continue;
				}
				const sint16 cmin = min( min( c[0], c[1] ), min( c[2], c[3] ) );
				const sint16 cmax = max( max( c[0], c[1] ), max( c[2], c[3] ) );
				bad |= cmax - cmin > d;
				if(  down  ) {
					// lower_to drains a sea or lake next to a tile that ends up below its water (a shore
					// tile with a corner under water too), raising its floor: never lower such a tile
					for(  sint16 j = -1;  j <= 1  &&  !bad;  j++  ) {
						for(  sint16 i = -1;  i <= 1  &&  !bad;  i++  ) {
							const sint16 nx = tx + i, ny = ty + j;
							bad = welt->is_within_limits( nx, ny )  &&  cmin < welt->get_water_hgt( nx, ny );
						}
					}
				}
				if(  !bad  &&  up  ) {
					karte_t::terraformer_t digger( &*welt );
					digger.add_raise_node( tx, ty, c[0], c[1], c[2], c[3] );
					bad = digger.can_raise_all( player ) != NULL;
				}
				if(  !bad  &&  down  ) {
					karte_t::terraformer_t digger( &*welt );
					digger.add_lower_node( tx, ty, c[0], c[1], c[2], c[3] );
					bad = digger.can_lower_all( player ) != NULL;
				}
				if(  bad  ) {
					pinned[x + y * tw] = 1;
					all_ok = false;
				}
			}
		}
		if(  all_ok  ) {
			break;
		}
	}

	// result
	for(  sint16 y = 0;  y < th;  y++  ) {
		for(  sint16 x = 0;  x < tw;  x++  ) {
			const sint16 tx = gx0 + x, ty = gy0 + y;
			sint16 c[4], o[4], w[4];
			get_corners( new_hgt, tx, ty, c );
			get_corners( old_hgt, tx, ty, o );
			get_corners( wish_hgt, tx, ty, w );
			sint16 h0[4];
			get_tile_corners( tx, ty, h0 );
			bool changed = false, off = false;
			for(  int k = 0;  k < 4;  k++  ) {
				if(  c[k] > o[k]  ) {
					corner_changes += max( 0, c[k] - h0[k] );
				}
				else if(  c[k] < o[k]  ) {
					corner_changes += max( 0, h0[k] - c[k] );
				}
				changed |= c[k] != o[k];
				off |= c[k] != w[k];
			}
			if(  changed  ) {
				changed_tiles.append( koord( tx, ty ) );
			}
			if(  off  &&  a1.x <= tx  &&  tx <= a2.x  &&  a1.y <= ty  &&  ty <= a2.y  ) {
				blocked_tiles++;
			}
		}
	}
	return NULL;
}


sint64 landscaper_t::get_cost() const
{
	// raise_to/lower_to count four corners of a grid point, grid_raise books one change per point
	return welt->get_settings().cst_alter_land * ( ( corner_changes + 3 ) >> 2 );
}


const char *landscaper_t::apply( player_t *player ) const
{
	if(  changed_tiles.empty()  ) {
		return NULL;
	}

	// everything up first, then down: max(old,new) is a valid surface in between
	karte_t::terraformer_t raiser( &*welt );
	karte_t::terraformer_t lowerer( &*welt );
	bool any_up = false, any_down = false;
	FOR( vector_tpl<koord>, const k, changed_tiles ) {
		sint16 c[4], o[4];
		get_corners( new_hgt, k.x, k.y, c );
		get_corners( old_hgt, k.x, k.y, o );
		bool up = false, down = false;
		for(  int i = 0;  i < 4;  i++  ) {
			up |= c[i] > o[i];
			down |= c[i] < o[i];
		}
		if(  up  ) {
			raiser.add_raise_node( k.x, k.y, c[0], c[1], c[2], c[3] );
			any_up = true;
		}
		if(  down  ) {
			lowerer.add_lower_node( k.x, k.y, c[0], c[1], c[2], c[3] );
			any_down = true;
		}
	}
	if(  any_up  ) {
		raiser.iterate( true );
		if(  const char *err = raiser.can_raise_all( player )  ) {
			return err;
		}
	}
	if(  any_down  ) {
		lowerer.iterate( false );
		if(  const char *err = lowerer.can_lower_all( player )  ) {
			return err;
		}
	}

	int changes = 0;
	if(  any_up  ) {
		changes += raiser.raise_all();
	}
	if(  any_down  ) {
		changes += lowerer.lower_all();
	}
	welt->set_dirty();

	const sint64 ops = ( changes + 3 ) >> 2;
	if(  ops > 0  ) {
		player_t::book_construction_costs( player, welt->get_settings().cst_alter_land * ops, changed_tiles[0], ignore_wt );
	}
	return NULL;
}
