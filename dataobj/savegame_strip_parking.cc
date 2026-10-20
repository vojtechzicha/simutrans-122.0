/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

#include "savegame_strip_parking.h"
#include "station_reach.h"
#include "schedule.h"
#include "settings.h"

#include "../simcity.h"
#include "../simconvoi.h"
#include "../simdebug.h"
#include "../simfab.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../simplan.h"
#include "../simworld.h"

#include "../bauer/goods_manager.h"
#include "../boden/grund.h"
#include "../boden/wege/weg.h"
#include "../descriptor/building_desc.h"
#include "../descriptor/roadsign_desc.h"
#include "../obj/gebaeude.h"
#include "../obj/roadsign.h"
#include "../player/finance.h"
#include "../player/simplay.h"
#include "../sys/simsys.h"
#include "../tpl/inthashtable_tpl.h"
#include "../tpl/vector_tpl.h"
#include "../utils/cbuffer_t.h"
#include "../utils/plainstring.h"
#include "../vehicle/simvehicle.h"


namespace {

const char *const PARKING_PREFIX = "parking_(catchemnt_area_ext)";
const sint32 FAR_AWAY = 0x7fffffff;

/// why a parking tile stays (KEEP_NONE: it goes)
enum keep_t { KEEP_NONE, KEEP_SCHEDULE, KEEP_LONG_CONVOY, KEEP_CHOOSE_SIGN, KEEP_VEHICLE, KEEP_LIST, KEEP_PASSENGERS, KEEP_MAIL, KEEP_GOODS, KEEP_COUNT };
const char *const keep_names[KEEP_COUNT] = { "removed", "schedule", "longer-convoy", "choose-sign", "vehicle", "keep-list", "passengers", "mail", "goods" };

/// customer classes: passengers, mail, goods (factories only)
enum { CLASS_PAX, CLASS_MAIL, CLASS_GOODS, CLASS_COUNT };
const char *const class_names[CLASS_COUNT] = { "passengers", "mail", "goods" };

enum strip_mode_t { STRIP_SAME, STRIP_NEAR, STRIP_ANY };

struct parking_t
{
	koord3d pos;
	uint16 halt;
	uint8 keep;
	bool removed;
	sint32 own_d2;                  ///< squared distance to the nearest tile of its stop that is no parking
	vector_tpl<uint32> customers;   ///< customers within the far reach of it
	plainstring why;                ///< the customer that keeps it, with its stops now and without it
};

struct stop_tile_t
{
	koord pos;
	uint32 parking;   ///< index into the parking list + 1, 0 for a tile of the stop itself
};

/// a stop with a tile within the far reach of a customer
struct candidate_t
{
	uint16 halt;
	sint32 d2;         ///< squared distance of its nearest tile left, FAR_AWAY if none is within the far reach
	uint16 in_reach;   ///< its tiles left within the Reach
};

struct customer_t
{
	koord pos, lo, hi;   ///< the tile its stop list is on, and the box it is measured from
	const char *kind;
	bool factory;
	vector_tpl<candidate_t> candidates;
	vector_tpl<uint16> before[CLASS_COUNT];
};


inline sint32 dist2_box( koord t, koord lo, koord hi )
{
	const sint32 dx = t.x < lo.x ? lo.x - t.x : (t.x > hi.x ? t.x - hi.x : 0);
	const sint32 dy = t.y < lo.y ? lo.y - t.y : (t.y > hi.y ? t.y - hi.y : 0);
	return dx*dx + dy*dy;
}

inline sint32 cheb_box( koord t, koord lo, koord hi )
{
	const sint32 dx = t.x < lo.x ? lo.x - t.x : (t.x > hi.x ? t.x - hi.x : 0);
	const sint32 dy = t.y < lo.y ? lo.y - t.y : (t.y > hi.y ? t.y - hi.y : 0);
	return dx > dy ? dx : dy;
}

bool is_parking( const grund_t *gr )
{
	const gebaeude_t *gb = gr->find<gebaeude_t>();
	return gb  &&  strncmp( gb->get_tile()->get_desc()->get_name(), PARKING_PREFIX, strlen( PARKING_PREFIX ) ) == 0;
}

bool outermost_first( const parking_t *a, const parking_t *b )
{
	return a->own_d2 > b->own_d2;
}

bool same_set( const vector_tpl<uint16> &a, const vector_tpl<uint16> &b )
{
	if(  a.get_count() != b.get_count()  ) {
		return false;
	}
	FOR( vector_tpl<uint16>, const x, a ) {
		if(  !b.is_contained( x )  ) {
			return false;
		}
	}
	return true;
}


/// plans which parking tiles can go, removes them and checks the result
class strip_planner_t
{
public:
	karte_t *welt;
	sint32 reach, far_reach;
	strip_mode_t mode;
	double tolerance;
	bool compare_mail;

	vector_tpl<parking_t *> parking;
	vector_tpl<stop_tile_t> *stop_tiles;   ///< per halt id
	vector_tpl<customer_t *> customers;
	inthashtable_tpl<uint32, uint32> parking_at;   ///< tile -> parking index + 1
	vector_tpl<plainstring> keep_not_found;

	strip_planner_t( karte_t *w ) : welt( w ), stop_tiles( new vector_tpl<stop_tile_t>[ quickstone_tpl<haltestelle_t>::get_size() ] ) {}

	~strip_planner_t()
	{
		FOR( vector_tpl<parking_t *>, p, parking ) {
			delete p;
		}
		FOR( vector_tpl<customer_t *>, c, customers ) {
			delete c;
		}
		delete [] stop_tiles;
	}

	uint32 key( koord k ) const { return (uint32)k.y * (uint32)welt->get_size().x + (uint32)k.x; }

	void collect_parking()
	{
		FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
			FOR( slist_tpl<haltestelle_t::tile_t>, const& t, halt->get_tiles() ) {
				stop_tile_t st;
				st.pos = t.grund->get_pos().get_2d();
				st.parking = 0;
				if(  is_parking( t.grund )  ) {
					parking_t *p = new parking_t();
					p->pos = t.grund->get_pos();
					p->halt = halt.get_id();
					p->keep = KEEP_NONE;
					p->removed = false;
					p->own_d2 = FAR_AWAY;
					parking.append( p );
					st.parking = parking.get_count();
					parking_at.set( key( st.pos ), st.parking );
				}
				stop_tiles[halt.get_id()].append( st );
			}
		}
		FOR( vector_tpl<parking_t *>, p, parking ) {
			FOR( vector_tpl<stop_tile_t>, const& t, stop_tiles[p->halt] ) {
				if(  t.parking == 0  ) {
					const koord d = t.pos - p->pos.get_2d();
					const sint32 d2 = (sint32)d.x*d.x + (sint32)d.y*d.y;
					if(  d2 < p->own_d2  ) {
						p->own_d2 = d2;
					}
				}
			}
		}
	}

	parking_t *parking_on( koord3d pos ) const
	{
		const uint32 i = parking_at.get( key( pos.get_2d() ) );
		return i  &&  parking[i-1]->pos == pos ? parking[i-1] : NULL;
	}

	void keep( parking_t *p, uint8 why )
	{
		if(  p  &&  p->keep == KEEP_NONE  ) {
			p->keep = why;
		}
	}

	/// tiles a vehicle stops on: schedule entries, longer convoys around them, choose sign searches, standing convoys
	void keep_stop_positions()
	{
		vector_tpl<schedule_t *> schedules;
		vector_tpl<uint16> lengths;   // the longest convoy (in tiles) that runs that schedule
		FOR( vector_tpl<convoihandle_t>, const cnv, welt->convoys() ) {
			if(  cnv->get_schedule()  ) {
				schedules.append( cnv->get_schedule() );
				lengths.append( cnv->get_tile_length() );
			}
		}
		for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
			player_t *player = welt->get_player( i );
			if(  !player  ) {
				continue;
			}
			FOR( vector_tpl<linehandle_t>, const line, player->simlinemgmt.get_line_list() ) {
				uint16 len = 1;
				for(  uint32 c = 0;  c < line->count_convoys();  c++  ) {
					len = max( len, line->get_convoy( c )->get_tile_length() );
				}
				schedules.append( line->get_schedule() );
				lengths.append( len );
			}
		}
		for(  uint32 s = 0;  s < schedules.get_count();  s++  ) {
			FOR( minivec_tpl<schedule_entry_t>, const& e, schedules[s]->entries ) {
				keep( parking_on( e.pos ), KEEP_SCHEDULE );
				if(  lengths[s] < 2  ) {
					continue;
				}
				// route_t::calc_route moves a long convoy on into the next tiles of the stop (and it may come in over them)
				const grund_t *gr = welt->lookup( e.pos );
				if(  !gr  ||  !gr->get_halt().is_bound()  ) {
					continue;
				}
				const waytype_t wt = gr->hat_weg( road_wt ) ? road_wt : track_wt;
				for(  int d = 0;  d < 4;  d++  ) {
					const grund_t *g = gr;
					for(  int k = 1;  k < lengths[s];  k++  ) {
						grund_t *to;
						if(  !g->get_neighbour( to, wt, ribi_t::nsew[d] )  ||  to->get_halt() != gr->get_halt()  ) {
							break;
						}
						keep( parking_on( to->get_pos() ), KEEP_LONG_CONVOY );
						g = to;
					}
				}
			}
		}

		// road_vehicle_t::choose_route takes any free tile of the stop it heads for, found within max_choose_route_steps
		const uint32 steps = welt->get_settings().get_max_choose_route_steps();
		FOR( slist_tpl<weg_t *>, const w, weg_t::get_alle_wege() ) {
			if(  w->get_waytype() != road_wt  ||  !w->has_sign()  ) {
				continue;
			}
			grund_t *start = welt->lookup( w->get_pos() );
			const roadsign_t *rs = start ? start->find<roadsign_t>() : NULL;
			if(  !rs  ||  !rs->get_desc()->is_choose_sign()  ) {
				continue;
			}
			vector_tpl<grund_t *> queue;
			inthashtable_tpl<uint32, uint8> seen;
			queue.append( start );
			seen.set( key( start->get_pos().get_2d() ), 1 );
			for(  uint32 q = 0;  q < queue.get_count()  &&  q < steps;  q++  ) {
				grund_t *gr = queue[q];
				keep( parking_on( gr->get_pos() ), KEEP_CHOOSE_SIGN );
				const weg_t *road = gr->get_weg( road_wt );
				for(  int d = 0;  road  &&  d < 4;  d++  ) {
					grund_t *to;
					if(  (road->get_ribi_unmasked() & ribi_t::nsew[d])  &&  gr->get_neighbour( to, road_wt, ribi_t::nsew[d] )  &&  !seen.get( key( to->get_pos().get_2d() ) )  ) {
						seen.set( key( to->get_pos().get_2d() ), 1 );
						queue.append( to );
					}
				}
			}
		}

		// a convoy standing on it or a reservation, right now
		FOR( vector_tpl<parking_t *>, p, parking ) {
			const grund_t *gr = welt->lookup( p->pos );
			halthandle_t halt;
			halt.set_id( p->halt );
			FOR( slist_tpl<haltestelle_t::tile_t>, const& t, halt->get_tiles() ) {
				if(  t.grund == gr  &&  t.reservation.is_bound()  ) {
					keep( p, KEEP_VEHICLE );
				}
			}
			for(  uint8 i = 0;  i < gr->obj_count();  i++  ) {
				const obj_t *obj = gr->obj_bei( i );
				if(  obj->get_typ() == obj_t::road_vehicle  ||  obj->get_typ() == obj_t::rail_vehicle  ) {
					const convoi_t *cnv = static_cast<const vehicle_t *>(obj)->get_convoi();
					if(  cnv  &&  cnv->is_standing()  ) {
						keep( p, KEEP_VEHICLE );
					}
				}
			}
		}
	}

	/// every tile of the stops named in the file stays
	void keep_listed( const char *path )
	{
		if(  !path  ) {
			return;
		}
		FILE *f = fopen( path, "r" );
		if(  !f  ) {
			dbg->error( "savegame_strip_parking_t::apply", "cannot read the keep list %s", path );
			keep_not_found.append( plainstring( path ) );
			return;
		}
		char line[4096];
		int nr = 0;
		while(  fgets( line, sizeof(line), f )  ) {
			char *s = line;
			if(  nr++ == 0  &&  (uint8)s[0] == 0xEF  &&  (uint8)s[1] == 0xBB  &&  (uint8)s[2] == 0xBF  ) {
				s += 3;   // UTF-8 byte order mark
			}
			size_t len = strlen( s );
			while(  len > 0  &&  (s[len-1] == '\n'  ||  s[len-1] == '\r'  ||  s[len-1] == ' '  ||  s[len-1] == '\t')  ) {
				s[--len] = 0;
			}
			if(  s[0] == 0  ||  s[0] == '#'  ) {
				continue;
			}
			bool found = false;
			FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
				if(  strcmp( halt->get_name(), s ) == 0  ) {
					found = true;
					FOR( vector_tpl<stop_tile_t>, const& t, stop_tiles[halt.get_id()] ) {
						if(  t.parking  ) {
							keep( parking[t.parking-1], KEEP_LIST );
						}
					}
				}
			}
			if(  !found  ) {
				keep_not_found.append( plainstring( s ) );
			}
		}
		fclose( f );
	}

	/// sets the candidate's distance and tiles in the Reach from the stop's tiles still there
	void recompute( customer_t &cu, uint16 halt_id )
	{
		FOR( vector_tpl<candidate_t>, &c, cu.candidates ) {
			if(  c.halt != halt_id  ) {
				continue;
			}
			c.d2 = FAR_AWAY;
			c.in_reach = 0;
			FOR( vector_tpl<stop_tile_t>, const& t, stop_tiles[halt_id] ) {
				if(  t.parking  &&  parking[t.parking-1]->removed  ) {
					continue;
				}
				const sint32 cb = cheb_box( t.pos, cu.lo, cu.hi );
				if(  cb > far_reach  ) {
					continue;
				}
				c.d2 = min( c.d2, dist2_box( t.pos, cu.lo, cu.hi ) );
				if(  cb <= reach  ) {
					c.in_reach++;
				}
			}
			return;
		}
	}

	void add_customer( koord pos, koord lo, koord hi, const char *kind )
	{
		customer_t *cu = new customer_t();
		cu->pos = pos;
		cu->lo = lo;
		cu->hi = hi;
		cu->kind = kind;
		cu->factory = strcmp( kind, "factory" ) == 0;
		customers.append( cu );
	}

	/// the customers of station_reach_t: city buildings, attractions, factories (from their pos tile)
	void collect_customers()
	{
		inthashtable_tpl<uint32, uint32> houses;   // tile -> customer index + 1
		FOR( weighted_vector_tpl<stadt_t *>, const city, welt->get_cities() ) {
			FOR( weighted_vector_tpl<gebaeude_t *>, const gb, city->get_city_buildings() ) {
				const koord k = gb->get_pos().get_2d();
				if(  houses.get( key( k ) )  ||  welt->lookup_kartenboden( k )->find<gebaeude_t>() != gb  ) {
					continue;   // a second tile entry, or a city list entry whose tile has no building
				}
				add_customer( k, k, k, "house" );
				houses.set( key( k ), customers.get_count() );
			}
		}
		FOR( weighted_vector_tpl<gebaeude_t *>, const gb, welt->get_attractions() ) {
			const koord k = gb->get_pos().get_2d();
			if(  houses.get( key( k ) )  ||  welt->lookup_kartenboden( k )->find<gebaeude_t>() != gb  ) {
				continue;
			}
			add_customer( k, k, k, "attraction" );
			houses.set( key( k ), customers.get_count() );
		}
		vector_tpl<uint32> factories;
		FOR( slist_tpl<fabrik_t *>, const fab, welt->get_fab_list() ) {
			koord lo, hi;
			station_reach_t::get_factory_box( fab, lo, hi );
			add_customer( fab->get_pos().get_2d(), lo, hi, "factory" );
			factories.append( customers.get_count() - 1 );
		}

		// candidates: every stop with a tile within the far reach
		FOR( vector_tpl<customer_t *>, cu, customers ) {
			inthashtable_tpl<uint16, uint8> got;
			for(  sint32 y = cu->lo.y - far_reach;  y <= cu->hi.y + far_reach;  y++  ) {
				for(  sint32 x = cu->lo.x - far_reach;  x <= cu->hi.x + far_reach;  x++  ) {
					const planquadrat_t *plan = welt->access( x, y );
					if(  !plan  ) {
						continue;
					}
					for(  uint g = 0;  g < plan->get_boden_count();  g++  ) {
						const halthandle_t halt = plan->get_boden_bei( g )->get_halt();
						if(  halt.is_bound()  &&  !got.get( halt.get_id() )  ) {
							got.set( halt.get_id(), 1 );
							candidate_t c;
							c.halt = halt.get_id();
							c.d2 = FAR_AWAY;
							c.in_reach = 0;
							cu->candidates.append( c );
							recompute( *cu, halt.get_id() );
						}
					}
				}
			}
			evaluate( *cu, cu->before );
		}

		// which customers each parking tile can change
		FOR( vector_tpl<parking_t *>, p, parking ) {
			const koord k = p->pos.get_2d();
			for(  sint32 y = k.y - far_reach;  y <= k.y + far_reach;  y++  ) {
				for(  sint32 x = k.x - far_reach;  x <= k.x + far_reach;  x++  ) {
					if(  x >= 0  &&  y >= 0  &&  x < welt->get_size().x  &&  y < welt->get_size().y  ) {
						if(  const uint32 i = houses.get( key( koord( x, y ) ) )  ) {
							p->customers.append( i - 1 );
						}
					}
				}
			}
			FOR( vector_tpl<uint32>, const i, factories ) {
				if(  cheb_box( k, customers[i]->lo, customers[i]->hi ) <= far_reach  ) {
					p->customers.append( i );
				}
			}
		}
	}

	/// the stops serving a customer, as the game computes them: Reach (stops handling the class) plus far reach
	void evaluate( const customer_t &cu, vector_tpl<uint16> *serving ) const
	{
		for(  int c = 0;  c < CLASS_COUNT;  c++  ) {
			serving[c].clear();
		}
		uint8 needed = planquadrat_t::FAR_PAX | planquadrat_t::FAR_MAIL;
		FOR( vector_tpl<candidate_t>, const& c, cu.candidates ) {
			halthandle_t halt;
			halt.set_id( c.halt );
			if(  c.in_reach == 0  ||  !halt.is_bound()  ) {
				continue;
			}
			if(  halt->get_pax_enabled()  ) {
				serving[CLASS_PAX].append_unique( c.halt );
			}
			if(  halt->get_mail_enabled()  ) {
				serving[CLASS_MAIL].append_unique( c.halt );
			}
			if(  cu.factory  &&  halt->get_ware_enabled()  ) {
				serving[CLASS_GOODS].append_unique( c.halt );
			}
			needed &= ~station_reach_t::get_serving_classes( halt );
		}
		if(  far_reach <= reach  ) {
			return;
		}
		for(  int c = CLASS_PAX;  c <= CLASS_MAIL;  c++  ) {
			const uint8 bit = c == CLASS_PAX ? planquadrat_t::FAR_PAX : planquadrat_t::FAR_MAIL;
			if(  !(needed & bit)  ) {
				continue;
			}
			// the nearest stop with a line for it, ties all serve; in addition to stops without a line in the Reach
			sint32 best = FAR_AWAY;
			vector_tpl<uint16> nearest;
			FOR( vector_tpl<candidate_t>, const& cand, cu.candidates ) {
				halthandle_t halt;
				halt.set_id( cand.halt );
				if(  cand.d2 == FAR_AWAY  ||  !halt.is_bound()  ||  !(station_reach_t::get_serving_classes( halt ) & bit)  ) {
					continue;
				}
				if(  cand.d2 < best  ) {
					best = cand.d2;
					nearest.clear();
				}
				if(  cand.d2 == best  ) {
					nearest.append_unique( cand.halt );
				}
			}
			FOR( vector_tpl<uint16>, const id, nearest ) {
				serving[c].append_unique( id );
			}
		}
	}

	/// straight-line distance from the customer to the nearest tile of the stop that is no parking
	double distance( const customer_t &cu, uint16 halt_id ) const
	{
		sint32 best = FAR_AWAY;
		FOR( vector_tpl<stop_tile_t>, const& t, stop_tiles[halt_id] ) {
			if(  t.parking == 0  ) {
				best = min( best, dist2_box( t.pos, cu.lo, cu.hi ) );
			}
		}
		return best == FAR_AWAY ? 1e9 : sqrt( (double)best );
	}

	/// KEEP_NONE if the customer's stops now are allowed by the mode, else the class (KEEP_PASSENGERS, ...) that is not
	uint8 check( const customer_t &cu, vector_tpl<uint16> *now ) const
	{
		evaluate( cu, now );
		for(  int c = 0;  c < CLASS_COUNT;  c++  ) {
			if(  (c == CLASS_MAIL  &&  !compare_mail)  ||  same_set( now[c], cu.before[c] )  ) {
				continue;
			}
			if(  c == CLASS_GOODS  ) {
				return KEEP_GOODS;   // the far reach does not do goods
			}
			if(  cu.before[c].empty()  ) {
				continue;   // newly served
			}
			if(  now[c].empty()  ||  mode == STRIP_SAME  ) {
				return KEEP_PASSENGERS + c;
			}
			if(  mode == STRIP_ANY  ) {
				continue;
			}
			double nearest = 1e9;
			FOR( vector_tpl<uint16>, const id, now[c] ) {
				const double d = distance( cu, id );   // not min(): it takes int
				if(  d < nearest  ) {
					nearest = d;
				}
			}
			FOR( vector_tpl<uint16>, const id, cu.before[c] ) {
				if(  !now[c].is_contained( id )  &&  fabs( distance( cu, id ) - nearest ) > tolerance + 1e-9  ) {
					return KEEP_PASSENGERS + c;
				}
			}
		}
		return KEEP_NONE;
	}

	void set_removed( parking_t *p, bool removed )
	{
		p->removed = removed;
		FOR( vector_tpl<uint32>, const i, p->customers ) {
			recompute( *customers[i], p->halt );
		}
	}

	void append_stops( cbuffer_t &buf, const customer_t &cu, const vector_tpl<uint16> &stops ) const
	{
		if(  stops.empty()  ) {
			buf.append( "none" );
		}
		for(  uint32 i = 0;  i < stops.get_count();  i++  ) {
			halthandle_t halt;
			halt.set_id( stops[i] );
			buf.printf( "%s%s (%.1f)", i ? "; " : "", halt.is_bound() ? halt->get_name() : "?", distance( cu, stops[i] ) );
		}
	}

	/// outermost first: each tile goes when no customer it can change breaks the rule; a second pass retries the others
	void plan()
	{
		vector_tpl<parking_t *> order;
		FOR( vector_tpl<parking_t *>, p, parking ) {
			if(  p->keep == KEEP_NONE  ) {
				order.append( p );
			}
		}
		std::stable_sort( order.begin(), order.end(), outermost_first );
		vector_tpl<uint16> now[CLASS_COUNT];
		for(  int pass = 0;  pass < 2;  pass++  ) {
			FOR( vector_tpl<parking_t *>, p, order ) {
				if(  p->removed  ) {
					continue;
				}
				set_removed( p, true );
				uint8 why = KEEP_NONE;
				const customer_t *breaking = NULL;
				FOR( vector_tpl<uint32>, const i, p->customers ) {
					why = check( *customers[i], now );
					if(  why != KEEP_NONE  ) {
						breaking = customers[i];
						break;
					}
				}
				if(  why == KEEP_NONE  ) {
					continue;
				}
				if(  pass == 1  ) {
					p->keep = why;
					const int c = why - KEEP_PASSENGERS;
					cbuffer_t buf;
					buf.printf( "%s %s: ", breaking->kind, breaking->pos.get_str() );
					append_stops( buf, *breaking, breaking->before[c] );
					buf.append( " | without it: " );
					append_stops( buf, *breaking, now[c] );
					p->why = buf.get_str();
				}
				set_removed( p, false );
			}
		}
	}

	/// the customer's stops as the game's lists have them
	void live( const customer_t &cu, vector_tpl<uint16> *serving ) const
	{
		const planquadrat_t *plan = welt->access( cu.pos );
		const uint8 stock = plan->get_haltlist_count_with_potential() - plan->get_far_count();
		for(  int c = 0;  c < CLASS_COUNT;  c++  ) {
			serving[c].clear();
		}
		for(  uint8 i = 0;  i < plan->get_haltlist_count();  i++  ) {
			const halthandle_t halt = plan->get_haltlist()[i];
			if(  plan->is_serving( i, goods_manager_t::INDEX_PAS )  ) {
				serving[CLASS_PAX].append_unique( halt.get_id() );
			}
			if(  plan->is_serving( i, goods_manager_t::INDEX_MAIL )  ) {
				serving[CLASS_MAIL].append_unique( halt.get_id() );
			}
			if(  cu.factory  &&  i < stock  &&  halt->get_ware_enabled()  ) {
				serving[CLASS_GOODS].append_unique( halt.get_id() );
			}
		}
	}

	/// customers whose live lists differ from the model, the first ones written to the report
	int compare_live( FILE *report, const char *what ) const
	{
		int differ = 0;
		vector_tpl<uint16> model[CLASS_COUNT], game[CLASS_COUNT];
		FOR( vector_tpl<customer_t *>, const cu, customers ) {
			evaluate( *cu, model );
			live( *cu, game );
			for(  int c = 0;  c < CLASS_COUNT;  c++  ) {
				if(  !same_set( model[c], game[c] )  ) {
					if(  differ++ < 20  &&  report  ) {
						cbuffer_t buf;
						buf.printf( "error\t%s\t%s %s %s: game ", what, cu->kind, cu->pos.get_str(), class_names[c] );
						append_stops( buf, *cu, game[c] );
						buf.append( ", planned " );
						append_stops( buf, *cu, model[c] );
						fprintf( report, "%s\n", buf.get_str() );
					}
				}
			}
		}
		return differ;
	}
};

} // namespace


bool savegame_strip_parking_t::apply( karte_t *welt, const char *mode, const char *keep_path, bool compare_mail, bool free, bool dry_run, const char *report_path )
{
	strip_planner_t planner( welt );
	if(  strcmp( mode, "same" ) == 0  ) {
		planner.mode = STRIP_SAME;
		planner.tolerance = 0;
	}
	else if(  strcmp( mode, "any" ) == 0  ) {
		planner.mode = STRIP_ANY;
		planner.tolerance = 0;
	}
	else if(  strcmp( mode, "near" ) == 0  ||  (strncmp( mode, "near=", 5 ) == 0  &&  atof( mode + 5 ) >= 0)  ) {
		planner.mode = STRIP_NEAR;
		planner.tolerance = mode[4] == '=' ? atof( mode + 5 ) : 2.0;
	}
	else {
		dbg->error( "savegame_strip_parking_t::apply", "unknown mode '%s' (same, near=T or any)", mode );
		return false;
	}
	planner.compare_mail = compare_mail;
	planner.reach = welt->get_settings().get_station_coverage();
	planner.far_reach = max( (sint32)station_reach_t::get_far_reach(), planner.reach );

	FILE *report = fopen( report_path, "w" );
	if(  !report  ) {
		dbg->error( "savegame_strip_parking_t::apply", "cannot write the report %s", report_path );
		return false;
	}
	const uint32 t0 = dr_time();
	planner.collect_parking();
	planner.keep_stop_positions();
	planner.keep_listed( keep_path );
	planner.collect_customers();
	// the model must see what the game sees before anything goes
	const int differ_before = planner.compare_live( report, "before" );
	if(  differ_before  ) {
		fprintf( report, "error\t%d customers: the planned stops differ from the game's, nothing removed\n", differ_before );
		fclose( report );
		dbg->error( "savegame_strip_parking_t::apply", "%d customers differ from the model, nothing removed", differ_before );
		return false;
	}
	planner.plan();

	// the stops that disappear: made only of parking, all of it going
	vector_tpl<plainstring> stops_gone;
	FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
		bool any = false, all_go = true;
		FOR( vector_tpl<stop_tile_t>, const& t, planner.stop_tiles[halt.get_id()] ) {
			if(  t.parking == 0  ||  !planner.parking[t.parking-1]->removed  ) {
				all_go = false;
			}
			any = true;
		}
		if(  any  &&  all_go  ) {
			stops_gone.append( plainstring( halt->get_name() ) );
		}
	}

	uint32 counts[KEEP_COUNT] = { 0 };
	FOR( vector_tpl<parking_t *>, const p, planner.parking ) {
		counts[p->removed ? (uint8)KEEP_NONE : p->keep]++;
	}

	// remove for real, as the remove tool does
	int failed = 0, differ_after = 0;
	sint64 cost = 0;
	if(  !dry_run  ) {
		sint64 cash[MAX_PLAYER_COUNT];
		for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
			cash[i] = welt->get_player( i ) ? welt->get_player( i )->get_finance()->get_account_balance() : 0;
		}
		FOR( vector_tpl<parking_t *>, const p, planner.parking ) {
			if(  !p->removed  ) {
				continue;
			}
			grund_t *gr = welt->lookup( p->pos );
			const halthandle_t halt = gr ? gr->get_halt() : halthandle_t();
			if(  !halt.is_bound()  ||  halt.get_id() != p->halt  ||  !haltestelle_t::remove( halt->get_owner(), p->pos )  ) {
				failed++;
				fprintf( report, "error\tcould not remove %s\n", p->pos.get_str() );
			}
		}
		for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
			player_t *player = welt->get_player( i );
			if(  player  ) {
				const sint64 spent = cash[i] - player->get_finance()->get_account_balance();
				cost += spent;
				if(  free  &&  spent != 0  ) {
					player_t::book_construction_costs( player, spent, koord::invalid, ignore_wt );
				}
			}
		}
		differ_after = planner.compare_live( report, "after" );
	}

	// report: summary, every parking tile, the stops that go, every customer whose stops change
	cbuffer_t mode_text;
	if(  planner.mode == STRIP_NEAR  ) {
		mode_text.printf( "near=%g", planner.tolerance );
	}
	else {
		mode_text.append( planner.mode == STRIP_SAME ? "same" : "any" );
	}
	fprintf( report, "# strip-parking %s, mail %s, Reach %d, far reach %d, %s\n", mode_text.get_str(), compare_mail ? "compared" : "not compared",
		planner.reach, planner.far_reach, dry_run ? "dry run, nothing removed" : "removed" );
	fprintf( report, "# parking tiles %u: removed %u, kept %u (schedule %u, longer convoy %u, choose sign %u, vehicle %u, keep list %u, passengers %u, mail %u, goods %u)\n",
		planner.parking.get_count(), counts[KEEP_NONE], planner.parking.get_count() - counts[KEEP_NONE], counts[KEEP_SCHEDULE], counts[KEEP_LONG_CONVOY],
		counts[KEEP_CHOOSE_SIGN], counts[KEEP_VEHICLE], counts[KEEP_LIST], counts[KEEP_PASSENGERS], counts[KEEP_MAIL], counts[KEEP_GOODS] );
	if(  !dry_run  ) {
		fprintf( report, "# removal cost %.2f%s, failed %d, customers differing from the plan afterwards %d\n", cost / 100.0, free ? " (booked back)" : "", failed, differ_after );
	}
	FOR( vector_tpl<plainstring>, const& name, planner.keep_not_found ) {
		fprintf( report, "warning\tkeep list: no stop named '%s'\n", name.c_str() );
	}
	fprintf( report, "tile\tposition\tstop id\tstop\tstatus\twhy\n" );
	FOR( vector_tpl<parking_t *>, const p, planner.parking ) {
		halthandle_t halt;
		halt.set_id( p->halt );
		fprintf( report, "tile\t%s\t%u\t%s\t%s\t%s\n", p->pos.get_str(), p->halt, halt.is_bound() ? halt->get_name() : "(stop removed)",
			p->removed ? "removed" : "kept", p->removed ? "" : (p->why.c_str() ? p->why.c_str() : keep_names[p->keep]) );
	}
	FOR( vector_tpl<plainstring>, const& name, stops_gone ) {
		fprintf( report, "stop-removed\t%s\n", name.c_str() );
	}
	vector_tpl<uint16> now[CLASS_COUNT];
	FOR( vector_tpl<customer_t *>, const cu, planner.customers ) {
		planner.evaluate( *cu, now );
		for(  int c = 0;  c < CLASS_COUNT;  c++  ) {
			if(  (c == CLASS_MAIL  &&  !compare_mail)  ||  same_set( now[c], cu->before[c] )  ) {
				continue;
			}
			cbuffer_t buf;
			buf.printf( "customer\t%s\t%s\t%s\t", cu->kind, cu->pos.get_str(), class_names[c] );
			planner.append_stops( buf, *cu, cu->before[c] );
			buf.append( "\t" );
			planner.append_stops( buf, *cu, now[c] );
			fprintf( report, "%s\n", buf.get_str() );
		}
	}
	fclose( report );

	dbg->message( "savegame_strip_parking_t::apply", "%s: %u of %u parking tiles %s, %d stops gone, in %u ms", mode, counts[KEEP_NONE], planner.parking.get_count(),
		dry_run ? "can go" : "removed", stops_gone.get_count(), dr_time() - t0 );
	if(  failed  ||  differ_after  ) {
		dbg->error( "savegame_strip_parking_t::apply", "%d tiles not removed, %d customers differ from the plan: not saving", failed, differ_after );
		return false;
	}
	return true;
}
