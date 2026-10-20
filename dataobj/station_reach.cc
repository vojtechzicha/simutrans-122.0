/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "station_reach.h"

#include "../simworld.h"
#include "../simhalt.h"
#include "../simplan.h"
#include "../simcity.h"
#include "../simfab.h"
#include "../boden/grund.h"
#include "../obj/gebaeude.h"
#include "../descriptor/building_desc.h"
#include "../descriptor/factory_desc.h"
#include "settings.h"


karte_ptr_t station_reach_t::welt;
int station_reach_t::suspended = 0;
vector_tpl<koord3d> station_reach_t::halt_marks;
vector_tpl<koord3d> station_reach_t::preview_marks;


sint16 station_reach_t::get_far_reach()
{
	const settings_t &s = welt->get_settings();
	return s.get_station_far_reach() > s.get_station_coverage() ? (sint16)s.get_station_far_reach() : 0;
}


bool station_reach_t::is_suspended()
{
	return suspended > 0  ||  welt->is_destroying();
}


void station_reach_t::resume()
{
	if(  suspended > 0  &&  --suspended == 0  ) {
		update_all( false );
	}
}


bool station_reach_t::get_customer( koord pos, koord &lo, koord &hi )
{
	const grund_t *gr = welt->lookup_kartenboden( pos );
	if(  !gr  ) {
		return false;
	}
	const gebaeude_t *gb = gr->find<gebaeude_t>();
	if(  !gb  ||  !gb->get_tile()  ||  !gb->get_tile()->get_desc()  ) {
		return false;
	}
	if(  const fabrik_t *fab = gb->get_fabrik()  ) {
		// workers and mail go to the pos tile, measured from the whole factory
		if(  fab->get_pos().get_2d() != pos  ) {
			return false;
		}
		get_factory_box( fab, lo, hi );
		return true;
	}
	if(  gb->get_stadt()  ||  gb->get_tile()->get_desc()->is_attraction()  ) {
		lo = hi = pos;
		return true;
	}
	return false;
}


void station_reach_t::get_factory_box( const fabrik_t *fab, koord &lo, koord &hi )
{
	const koord pos = fab->get_pos().get_2d();
	const koord size = fab->get_desc()->get_building()->get_size( fab->get_rotate() );
	const sint16 k = max( size.x, size.y ) - 1;
	lo = hi = pos;
	for(  sint16 y = pos.y - k;  y <= pos.y + k;  y++  ) {
		for(  sint16 x = pos.x - k;  x <= pos.x + k;  x++  ) {
			const koord t( x, y );
			if(  welt->is_within_limits( t )  &&  fabrik_t::get_fab( t ) == fab  ) {
				lo.clip_max( t );
				hi.clip_min( t );
			}
		}
	}
}


bool station_reach_t::is_customer( koord pos )
{
	koord lo, hi;
	return get_customer( pos, lo, hi );
}


uint8 station_reach_t::get_serving_classes( halthandle_t halt )
{
	uint8 classes = halt->get_reach_served();
	if(  !halt->get_pax_enabled()  ) {
		classes &= ~planquadrat_t::FAR_PAX;
	}
	if(  !halt->get_mail_enabled()  ) {
		classes &= ~planquadrat_t::FAR_MAIL;
	}
	// goods keep the stock Reach
	return classes & (planquadrat_t::FAR_PAX | planquadrat_t::FAR_MAIL);
}


// squared straight-line distance from a tile to a box of tiles
static inline sint32 dist2_box( sint16 x, sint16 y, koord lo, koord hi )
{
	const sint32 dx = x < lo.x ? lo.x - x : (x > hi.x ? x - hi.x : 0);
	const sint32 dy = y < lo.y ? lo.y - y : (y > hi.y ? y - hi.y : 0);
	return dx * dx + dy * dy;
}


// the nearest stops found so far for one search
struct nearest_t {
	sint32 best;
	vector_tpl<halthandle_t> halts;

	nearest_t() : best( 0x7FFFFFFF ) {}

	void reset() { best = 0x7FFFFFFF; halts.clear(); }

	void offer( halthandle_t h, sint32 d )
	{
		if(  d < best  ) {
			best = d;
			halts.clear();
			halts.append( h );
		}
		else if(  d == best  ) {
			halts.append_unique( h );
		}
	}
};


void station_reach_t::update_tile( koord pos )
{
	planquadrat_t *plan = welt->access( pos );
	if(  !plan  ) {
		return;
	}
	plan->clear_far_entries();
	const sint16 reach = get_far_reach();
	koord lo, hi;
	if(  reach == 0  ||  !get_customer( pos, lo, hi )  ) {
		return;
	}

	// classes no stop within the Reach serves; potential entries only where no passenger stop is in the Reach at all
	uint8 needed = planquadrat_t::FAR_PAX | planquadrat_t::FAR_MAIL;
	bool pax_in_reach = false;
	const halthandle_t *list = plan->get_haltlist();
	for(  uint8 i = 0;  i < plan->get_haltlist_count();  i++  ) {
		const halthandle_t h = list[i];
		if(  h.is_bound()  ) {
			needed &= ~get_serving_classes( h );
			pax_in_reach |= h->get_pax_enabled();
		}
	}
	const bool potential = !pax_in_reach;
	if(  needed == 0  &&  !potential  ) {
		return;
	}

	static nearest_t near_pax, near_mail, near_any;
	near_pax.reset();
	near_mail.reset();
	near_any.reset();

	// rings of growing Chebyshev distance around the box; no tile of ring r is nearer than r
	for(  sint16 r = 0;  r <= reach;  r++  ) {
		const sint32 r2 = (sint32)r * r;
		if(  !((needed & planquadrat_t::FAR_PAX)  &&  r2 <= near_pax.best)  &&  !((needed & planquadrat_t::FAR_MAIL)  &&  r2 <= near_mail.best)  &&  !(potential  &&  r2 <= near_any.best)  ) {
			break;
		}
		for(  sint16 y = lo.y - r;  y <= hi.y + r;  y++  ) {
			const bool full_row = r == 0  ||  y == lo.y - r  ||  y == hi.y + r;
			const sint16 step = full_row ? 1 : (sint16)(hi.x - lo.x + 2 * r);
			for(  sint16 x = lo.x - r;  x <= hi.x + r;  x += step  ) {
				const planquadrat_t *p = welt->access( x, y );
				if(  !p  ) {
					continue;
				}
				for(  uint8 g = 0;  g < p->get_boden_count();  g++  ) {
					const halthandle_t h = p->get_boden_bei( g )->get_halt();
					if(  !h.is_bound()  ) {
						continue;
					}
					const sint32 d = dist2_box( x, y, lo, hi );
					const uint8 serving = get_serving_classes( h ) & needed;
					if(  serving & planquadrat_t::FAR_PAX  ) {
						near_pax.offer( h, d );
					}
					if(  serving & planquadrat_t::FAR_MAIL  ) {
						near_mail.offer( h, d );
					}
					if(  potential  &&  h->get_pax_enabled()  ) {
						near_any.offer( h, d );
					}
				}
			}
		}
	}

	// one entry per stop, passengers and mail together
	FOR( vector_tpl<halthandle_t>, const h, near_pax.halts ) {
		plan->add_far_entry( h, planquadrat_t::FAR_PAX | (near_mail.halts.is_contained( h ) ? planquadrat_t::FAR_MAIL : 0), false );
	}
	FOR( vector_tpl<halthandle_t>, const h, near_mail.halts ) {
		if(  !near_pax.halts.is_contained( h )  ) {
			plan->add_far_entry( h, planquadrat_t::FAR_MAIL, false );
		}
	}
	// the nearest passenger stop if every stop had a line, unless it serves this house already
	FOR( vector_tpl<halthandle_t>, const h, near_any.halts ) {
		if(  !near_pax.halts.is_contained( h )  &&  !near_mail.halts.is_contained( h )  ) {
			plan->add_far_entry( h, planquadrat_t::FAR_PAX, true );
		}
	}
}


void station_reach_t::update_box( koord ul, koord lr )
{
	const sint16 reach = get_far_reach();
	ul -= koord( reach, reach );
	lr += koord( reach, reach );
	ul.clip_min( koord( 0, 0 ) );
	lr.clip_max( welt->get_size() - koord( 1, 1 ) );
	for(  sint16 y = ul.y;  y <= lr.y;  y++  ) {
		for(  sint16 x = ul.x;  x <= lr.x;  x++  ) {
			const koord k( x, y );
			if(  welt->access( k )->get_far_count() > 0  ||  is_customer( k )  ) {
				update_tile( k );
			}
		}
	}
	// a factory counts from its whole footprint, but its entries are at its pos tile, which may lie outside
	vector_tpl<fabrik_t *> fabs;
	FOR( vector_tpl<fabrik_t *>, const fab, fabrik_t::sind_da_welche( ul, lr ) ) {
		fabs.append( fab );
	}
	FOR( vector_tpl<fabrik_t *>, const fab, fabs ) {
		const koord k = fab->get_pos().get_2d();
		if(  k.x < ul.x  ||  k.y < ul.y  ||  k.x > lr.x  ||  k.y > lr.y  ) {
			update_tile( k );
		}
	}
}


void station_reach_t::update_area( koord pos )
{
	if(  is_suspended()  ||  !is_active()  ) {
		return;
	}
	update_box( pos, pos );
}


void station_reach_t::update_halt( halthandle_t halt )
{
	if(  is_suspended()  ||  !is_active()  ||  !halt.is_bound()  ||  halt->get_tiles().empty()  ) {
		return;
	}
	koord ul( 0x7FFF, 0x7FFF ), lr( 0, 0 );
	FOR( slist_tpl<haltestelle_t::tile_t>, const& t, halt->get_tiles() ) {
		const koord k = t.grund->get_pos().get_2d();
		ul.clip_max( k );
		lr.clip_min( k );
	}
	update_box( ul, lr );
}


void station_reach_t::halt_removed( halthandle_t halt, koord ul, koord lr )
{
	if(  ul.x > lr.x  ||  ul.y > lr.y  ||  welt->is_destroying()  ) {
		// had no tiles: every tile removal updated its surroundings already (or the whole map goes)
		return;
	}
	const sint16 reach = max( get_far_reach(), welt->get_settings().get_station_far_reach() );
	koord bl = ul - koord( reach, reach ), br = lr + koord( reach, reach );
	bl.clip_min( koord( 0, 0 ) );
	br.clip_max( welt->get_size() - koord( 1, 1 ) );
	// no handle of a deleted stop may stay behind
	for(  sint16 y = bl.y;  y <= br.y;  y++  ) {
		for(  sint16 x = bl.x;  x <= br.x;  x++  ) {
			planquadrat_t *p = welt->access( x, y );
			const uint8 count = p->get_haltlist_count_with_potential();
			for(  uint8 i = count - p->get_far_count();  i < count;  i++  ) {
				if(  p->get_haltlist()[i] == halt  ) {
					p->clear_far_entries();
					break;
				}
			}
		}
	}
	if(  !is_suspended()  &&  is_active()  ) {
		// houses in its Reach need another stop now; the halt has no tiles any more, so it is not found
		update_box( ul, lr );
	}
}


void station_reach_t::clear_tile( koord pos )
{
	if(  planquadrat_t *p = welt->access( pos )  ) {
		p->clear_far_entries();
	}
}


void station_reach_t::update_all( bool clear_first )
{
	if(  clear_first  ) {
		const koord size = welt->get_size();
		for(  sint16 y = 0;  y < size.y;  y++  ) {
			for(  sint16 x = 0;  x < size.x;  x++  ) {
				planquadrat_t *p = welt->access( x, y );
				if(  p->get_far_count() > 0  ) {
					p->clear_far_entries();
				}
			}
		}
	}
	if(  !is_active()  ) {
		return;
	}
	FOR( weighted_vector_tpl<stadt_t *>, const city, welt->get_cities() ) {
		FOR( weighted_vector_tpl<gebaeude_t *>, const gb, city->get_city_buildings() ) {
			update_tile( gb->get_pos().get_2d() );
		}
	}
	FOR( weighted_vector_tpl<gebaeude_t *>, const gb, welt->get_attractions() ) {
		update_tile( gb->get_pos().get_2d() );
	}
	FOR( slist_tpl<fabrik_t *>, const fab, welt->get_fab_list() ) {
		update_tile( fab->get_pos().get_2d() );
	}
}


void station_reach_t::set_marked( const koord3d &pos, bool mark )
{
	if(  grund_t *gr = welt->lookup_kartenboden( pos.get_2d() )  ) {
		if(  mark  ) {
			gr->set_flag( grund_t::marked );
		}
		else if(  !stadt_t::keeps_frame( gr )  ) {
			gr->clear_flag( grund_t::marked );
		}
		gr->set_flag( grund_t::dirty );
	}
}


void station_reach_t::mark_halt( halthandle_t halt, bool mark )
{
	if(  !mark  ) {
		FOR( vector_tpl<koord3d>, const& k, halt_marks ) {
			set_marked( k, false );
		}
		halt_marks.clear();
		return;
	}
	if(  !is_active()  ||  !halt.is_bound()  ||  halt->get_tiles().empty()  ) {
		return;
	}
	const sint16 reach = get_far_reach();
	koord ul( 0x7FFF, 0x7FFF ), lr( 0, 0 );
	FOR( slist_tpl<haltestelle_t::tile_t>, const& t, halt->get_tiles() ) {
		const koord k = t.grund->get_pos().get_2d();
		ul.clip_max( k );
		lr.clip_min( k );
	}
	ul -= koord( reach, reach );
	lr += koord( reach, reach );
	ul.clip_min( koord( 0, 0 ) );
	lr.clip_max( welt->get_size() - koord( 1, 1 ) );
	for(  sint16 y = ul.y;  y <= lr.y;  y++  ) {
		for(  sint16 x = ul.x;  x <= lr.x;  x++  ) {
			const planquadrat_t *p = welt->access( x, y );
			const uint8 count = p->get_haltlist_count_with_potential();
			for(  uint8 i = count - p->get_far_count();  i < count;  i++  ) {
				if(  p->get_haltlist()[i] == halt  ) {
					const koord3d k = p->get_kartenboden()->get_pos();
					set_marked( k, true );
					halt_marks.append( k );
					break;
				}
			}
		}
	}
}


void station_reach_t::mark_preview( koord pos, bool mark )
{
	if(  !mark  ) {
		FOR( vector_tpl<koord3d>, const& k, preview_marks ) {
			set_marked( k, false );
		}
		preview_marks.clear();
		return;
	}
	const sint16 reach = get_far_reach();
	if(  reach == 0  ||  !welt->is_within_limits( pos )  ) {
		return;
	}
	const sint16 cov = welt->get_settings().get_station_coverage();
	for(  sint16 dy = -reach;  dy <= reach;  dy++  ) {
		for(  sint16 dx = -reach;  dx <= reach;  dx++  ) {
			if(  abs( dx ) <= cov  &&  abs( dy ) <= cov  ) {
				// in the Reach of the new stop: the cursor area shows it
				continue;
			}
			const koord k = pos + koord( dx, dy );
			koord lo, hi;
			if(  !welt->is_within_limits( k )  ||  !get_customer( k, lo, hi )  ) {
				continue;
			}
			const planquadrat_t *p = welt->access( k );
			const uint8 count = p->get_haltlist_count_with_potential();
			const uint8 first_far = count - p->get_far_count();
			bool pax_in_reach = false;
			for(  uint8 i = 0;  i < first_far;  i++  ) {
				if(  p->get_haltlist()[i].is_bound()  &&  p->get_haltlist()[i]->get_pax_enabled()  ) {
					pax_in_reach = true;
				}
			}
			if(  pax_in_reach  ) {
				continue;
			}
			// the nearest passenger stop it has now, with or without a line
			sint32 now = 0x7FFFFFFF;
			for(  uint8 i = first_far;  i < count;  i++  ) {
				const halthandle_t h = p->get_haltlist()[i];
				if(  !h.is_bound()  ||  !(p->get_far_classes( i ) & planquadrat_t::FAR_PAX)  ) {
					continue;
				}
				FOR( slist_tpl<haltestelle_t::tile_t>, const& t, h->get_tiles() ) {
					const koord tk = t.grund->get_pos().get_2d();
					const sint32 d = dist2_box( tk.x, tk.y, lo, hi );
					if(  d < now  ) {
						now = d;
					}
				}
			}
			if(  dist2_box( pos.x, pos.y, lo, hi ) <= now  ) {
				const koord3d k3 = p->get_kartenboden()->get_pos();
				set_marked( k3, true );
				preview_marks.append( k3 );
			}
		}
	}
}
