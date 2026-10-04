/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "../boden/grund.h"
#include "../boden/wege/runway.h"
#include "../boden/wege/kanal.h"
#include "../boden/wege/schiene.h"
#include "../boden/wege/monorail.h"
#include "../boden/wege/strasse.h"

#include "../bauer/goods_manager.h"

#include "../simworld.h"
#include "../simdebug.h"
#include "../simdepot.h"
#include "../simconvoi.h"
#include "../simunits.h"

#include "../player/simplay.h"
#include "../simfab.h"
#include "../simware.h"
#include "../simhalt.h"
#include "../simsound.h"

#include "../display/simimg.h"
#include "../simmesg.h"
#include "../simcolor.h"
#include "../display/simgraph.h"

#include "../simline.h"

#include "../simintr.h"

#include "../obj/wolke.h"
#include "../obj/signal.h"
#include "../obj/roadsign.h"
#include "../obj/crossing.h"
#include "../obj/gebaeude.h"
#include "../obj/zeiger.h"

#include "../gui/minimap.h"

#include "../descriptor/citycar_desc.h"
#include "../descriptor/goods_desc.h"
#include "../descriptor/skin_desc.h"
#include "../descriptor/roadsign_desc.h"

#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/environment.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

#include "../bauer/vehikelbauer.h"

#include "simvehicle.h"
#include "simroadtraffic.h"


/* get dx and dy from dir (just to remind you)
 * any vehicle (including city cars and pedestrians)
 * will go this distance per sync step.
 * (however, the real dirs are only calculated during display, these are the old ones)
 */
sint8 vehicle_base_t::dxdy[ 8*2 ] = {
	-2,  1, // s
	-2, -1, // w
	-4,  0, // sw
	 0,  2, // se
	 2, -1, // n
	 2,  1, // e
	 4,  0, // ne
	 0, -2  // nw
};


// Constants
uint8 vehicle_base_t::old_diagonal_vehicle_steps_per_tile = 128;
uint8 vehicle_base_t::diagonal_vehicle_steps_per_tile = 181;
uint16 vehicle_base_t::diagonal_multiplier = 724;


// set only once, before loading!
void vehicle_base_t::set_diagonal_multiplier( uint32 multiplier, uint32 old_diagonal_multiplier )
{
	diagonal_multiplier = (uint16)multiplier;
	diagonal_vehicle_steps_per_tile = (uint8)(130560u/diagonal_multiplier) + 1;
	old_diagonal_vehicle_steps_per_tile = (uint8)(130560u/old_diagonal_multiplier) + 1;
}


// if true, convoi, must restart!
bool vehicle_base_t::need_realignment() const
{
	return old_diagonal_vehicle_steps_per_tile!=diagonal_vehicle_steps_per_tile  &&  ribi_t::is_bend(direction);
}

// [0]=xoff [1]=yoff
sint8 vehicle_base_t::driveleft_base_offsets[8][2] =
{
	{  12,  6 },
	{ -12,  6 },
	{   0,  6 },
	{  12,  0 },
	{ -12, -6 },
	{  12, -6 },
	{   0, -6 },
	{ -12,  0 }
};

// [0]=xoff [1]=yoff
sint8 vehicle_base_t::overtaking_base_offsets[8][2];

// recalc offsets for overtaking
void vehicle_base_t::set_overtaking_offsets( bool driving_on_the_left )
{
	sint8 sign = driving_on_the_left ? -1 : 1;
	// a tile has the internal size of
	const sint8 XOFF=12;
	const sint8 YOFF=6;

	overtaking_base_offsets[0][0] = sign * XOFF;
	overtaking_base_offsets[1][0] = -sign * XOFF;
	overtaking_base_offsets[2][0] = 0;
	overtaking_base_offsets[3][0] = sign * XOFF;
	overtaking_base_offsets[4][0] = -sign * XOFF;
	overtaking_base_offsets[5][0] = sign * XOFF;
	overtaking_base_offsets[6][0] = 0;
	overtaking_base_offsets[7][0] = sign * (-XOFF-YOFF);

	overtaking_base_offsets[0][1] = sign * YOFF;
	overtaking_base_offsets[1][1] = sign * YOFF;
	overtaking_base_offsets[2][1] = sign * YOFF;
	overtaking_base_offsets[3][1] = 0;
	overtaking_base_offsets[4][1] = -sign * YOFF;
	overtaking_base_offsets[5][1] = -sign * YOFF;
	overtaking_base_offsets[6][1] = -sign * YOFF;
	overtaking_base_offsets[7][1] = 0;
}


/**
 * Checks if this vehicle must change the square upon next move
 * THIS IS ONLY THERE FOR LOADING OLD SAVES!
 */
bool vehicle_base_t::is_about_to_hop( const sint8 neu_xoff, const sint8 neu_yoff ) const
{
	const sint8 y_off_2 = 2*neu_yoff;
	const sint8 c_plus  = y_off_2 + neu_xoff;
	const sint8 c_minus = y_off_2 - neu_xoff;

	return ! (c_plus < OBJECT_OFFSET_STEPS*2  &&  c_minus < OBJECT_OFFSET_STEPS*2  &&  c_plus > -OBJECT_OFFSET_STEPS*2  &&  c_minus > -OBJECT_OFFSET_STEPS*2);
}


vehicle_base_t::vehicle_base_t():
	obj_t()
{
	image = IMG_EMPTY;
	set_flag( obj_t::is_vehicle );
	steps = 0;
	steps_next = VEHICLE_STEPS_PER_TILE - 1;
	use_calc_height = true;
	dx = 0;
	dy = 0;
	zoff_start = zoff_end = 0;
	disp_lane = 2;
}


vehicle_base_t::vehicle_base_t(koord3d pos):
	obj_t(pos)
{
	image = IMG_EMPTY;
	set_flag( obj_t::is_vehicle );
	pos_next = pos;
	steps = 0;
	steps_next = VEHICLE_STEPS_PER_TILE - 1;
	use_calc_height = true;
	dx = 0;
	dy = 0;
	zoff_start = zoff_end = 0;
	disp_lane = 2;
}





void vehicle_base_t::rotate90()
{
	obj_t::rotate90();
	// directions are counterclockwise to ribis!
	direction = ribi_t::rotate90( direction );
	pos_next.rotate90( welt->get_size().y-1 );
	// new offsets
	sint8 new_dx = -dy*2;
	dy = dx/2;
	dx = new_dx;
}


void vehicle_base_t::leave_tile()
{
	// first: release crossing
	grund_t *gr = welt->lookup(get_pos());
	if(  gr  &&  gr->ist_uebergang()  ) {
		crossing_t *cr = gr->find<crossing_t>(2);
		grund_t *gr2 = welt->lookup(pos_next);
		if(  gr2==NULL  ||  gr2==gr  ||  !gr2->ist_uebergang()  ||  cr->get_logic()!=gr2->find<crossing_t>(2)->get_logic()  ) {
			cr->release_crossing(this);
		}
	}

	// then remove from ground (or search whole map, if failed)
	if(!get_flag(not_on_map)  &&  (gr==NULL  ||  !gr->obj_remove(this)) ) {

		// was not removed (not found?)
		dbg->error("vehicle_base_t::leave_tile()","'typ %i' %p could not be removed from %d %d", get_typ(), this, get_pos().x, get_pos().y);
		DBG_MESSAGE("vehicle_base_t::leave_tile()","checking all plan squares");

		// check, whether it is on another height ...
		const planquadrat_t *pl = welt->access( get_pos().get_2d() );
		if(  pl  ) {
			gr = pl->get_boden_von_obj(this);
			if(  gr  ) {
				gr->obj_remove(this);
				dbg->warning("vehicle_base_t::leave_tile()","removed vehicle typ %i (%p) from %d %d",get_typ(), this, get_pos().x, get_pos().y);
			}
			return;
		}

		koord k;
		bool ok = false;

		for(k.y=0; k.y<welt->get_size().y; k.y++) {
			for(k.x=0; k.x<welt->get_size().x; k.x++) {
				grund_t *gr = welt->access( k )->get_boden_von_obj(this);
				if(gr && gr->obj_remove(this)) {
					dbg->warning("vehicle_base_t::leave_tile()","removed vehicle typ %i (%p) from %d %d",get_name(), this, k.x, k.y);
					ok = true;
				}
			}
		}

		if(!ok) {
			dbg->error("vehicle_base_t::leave_tile()","'%s' %p was not found on any map square!",get_name(), this);
		}
	}
}


void vehicle_base_t::enter_tile(grund_t* gr)
{
	if(!gr) {
		dbg->error("vehicle_base_t::enter_tile()","'%s' new position (%i,%i,%i)!",get_name(), get_pos().x, get_pos().y, get_pos().z );
		gr = welt->lookup_kartenboden(get_pos().get_2d());
		set_pos( gr->get_pos() );
	}
	gr->obj_add(this);
}


/* THE routine for moving vehicles
 * it will drive on as log as it can
 * @return the distance actually traveled
 */
uint32 vehicle_base_t::do_drive(uint32 distance)
{

	uint32 steps_to_do = distance >> YARDS_PER_VEHICLE_STEP_SHIFT;

	if(  steps_to_do == 0  ) {
		// ok, we will not move in this steps
		return 0;
	}
	// ok, so moving ...
	if(  !get_flag(obj_t::dirty)  ) {
		mark_image_dirty( image, 0 );
		set_flag( obj_t::dirty );
	}

	grund_t *gr = NULL; // if hopped, then this is new position

	uint32 steps_target = steps_to_do + steps;

	uint32 distance_travelled; // Return value

	if(  steps_target > steps_next  ) {
		// We are going far enough to hop.

		// We'll be adding steps_next+1 for each hop, as if we
		// started at the beginning of this tile, so for an accurate
		// count of steps done we must subtract the location we started with.
		sint32 steps_done = -steps;

		// Hop as many times as possible.
		while(  steps_target > steps_next  &&  (gr = hop_check())  ) {
			// now do the update for hopping
			steps_target -= steps_next+1;
			steps_done += steps_next+1;
			koord pos_prev(get_pos().get_2d());
			hop(gr);
			use_calc_height = true;

			// set offsets
			set_xoff( (dx<0) ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
			set_yoff( (dy<0) ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );
			if(dx*dy==0) {
				if(dx==0) {
					if(dy>0) {
						set_xoff( pos_prev.x!=get_pos().x ? -OBJECT_OFFSET_STEPS : OBJECT_OFFSET_STEPS );
					}
					else {
						set_xoff( pos_prev.x!=get_pos().x ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
					}
				}
				else {
					if(dx>0) {
						set_yoff( pos_prev.y!=get_pos().y ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );
					}
					else {
						set_yoff( pos_prev.y!=get_pos().y ? -OBJECT_OFFSET_STEPS/2 : OBJECT_OFFSET_STEPS/2 );
					}
				}
			}
		}

		if(  steps_next == 0  ) {
			// only needed for aircrafts, which can turn on the same tile
			// the indicate the turn with this here
			steps_next = VEHICLE_STEPS_PER_TILE - 1;
			steps_target = VEHICLE_STEPS_PER_TILE - 1;
			steps_done -= VEHICLE_STEPS_PER_TILE - 1;
		}

		// Update internal status, how far we got within the tile.
		if(  steps_target <= steps_next  ) {
			steps = steps_target;
		}
		else {
			// could not go as far as we wanted (hop_check failed) => stop at end of tile
			steps = steps_next;
		}

		steps_done += steps;
		distance_travelled = steps_done << YARDS_PER_VEHICLE_STEP_SHIFT;

	}
	else {
		// Just travel to target, it's on same tile
		steps = steps_target;
		distance_travelled = distance & YARDS_VEHICLE_STEP_MASK; // round down to nearest step
	}

	if(use_calc_height) {
		calc_height(gr);
	}
	// remaining steps
	return distance_travelled;
}


// to make smaller steps than the tile granularity, we have to use this trick
void vehicle_base_t::get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const
{
	// vehicles needs finer steps to appear smoother
	sint32 display_steps = (uint32)steps*(uint16)raster_width;
	if(dx && dy) {
		display_steps &= 0xFFFFFC00;
	}
	else {
		display_steps = (display_steps*diagonal_multiplier)>>10;
	}
	xoff += (display_steps*dx) >> 10;
	yoff += ((display_steps*dy) >> 10) + (get_hoff(raster_width))/(4*16);
}


// calcs new direction and applies it to the vehicles
ribi_t::ribi vehicle_base_t::calc_set_direction(const koord3d& start, const koord3d& ende)
{
	ribi_t::ribi direction = ribi_t::none;

	const sint8 di = ende.x - start.x;
	const sint8 dj = ende.y - start.y;

	if(dj < 0 && di == 0) {
		direction = ribi_t::north;
		dx = 2;
		dy = -1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(dj > 0 && di == 0) {
		direction = ribi_t::south;
		dx = -2;
		dy = 1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di < 0 && dj == 0) {
		direction = ribi_t::west;
		dx = -2;
		dy = -1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di >0 && dj == 0) {
		direction = ribi_t::east;
		dx = 2;
		dy = 1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di > 0 && dj > 0) {
		direction = ribi_t::southeast;
		dx = 0;
		dy = 2;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else if(di < 0 && dj < 0) {
		direction = ribi_t::northwest;
		dx = 0;
		dy = -2;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else if(di > 0 && dj < 0) {
		direction = ribi_t::northeast;
		dx = 4;
		dy = 0;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else {
		direction = ribi_t::southwest;
		dx = -4;
		dy = 0;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	}
	// we could artificially make diagonals shorter: but this would break existing game behaviour
	return direction;
}


// this routine calculates the new height
// beware of bridges, tunnels, slopes, ...
void vehicle_base_t::calc_height(grund_t *gr)
{
	use_calc_height = false;   // assume, we are only needed after next hop
	zoff_start = zoff_end = 0; // assume flat way

	if(gr==NULL) {
		gr = welt->lookup(get_pos());
	}
	if(gr==NULL) {
		// slope changed below a moving thing?!?
		return;
	}
	else if(  gr->ist_tunnel()  &&  gr->ist_karten_boden()  &&  !is_flying() ) {
		use_calc_height = true; // to avoid errors if underground mode is switched
		if(  grund_t::underground_mode == grund_t::ugm_none  ||
			(grund_t::underground_mode == grund_t::ugm_level  &&  gr->get_hoehe() < grund_t::underground_level)
		) {
			// need hiding? One of the few uses of XOR: not half driven XOR exiting => not hide!
			ribi_t::ribi hang_ribi = ribi_type( gr->get_grund_hang() );
			if((steps<(steps_next/2))  ^  ((hang_ribi&direction)!=0)  ) {
				set_image(IMG_EMPTY);
			}
			else {
				calc_image();
			}
		}
	}
	else {
		// force a valid image above ground, with special handling of tunnel entraces
		if(  get_image()==IMG_EMPTY  ) {
			if(  !gr->ist_tunnel()  &&  gr->ist_karten_boden()  ) {
				calc_image();
			}
		}

		// will not work great with ways, but is very short!
		slope_t::type hang = gr->get_weg_hang();
		if(  hang  ) {
			const uint slope_height = is_one_high(hang) ? 1 : 2;
			ribi_t::ribi hang_ribi = ribi_type(hang);
			if(  ribi_t::doubles(hang_ribi)  ==  ribi_t::doubles(direction)) {
				zoff_start = hang_ribi & direction                      ? 2*slope_height : 0;  // 0 .. 4
				zoff_end   = hang_ribi & ribi_t::backward(direction) ? 2*slope_height : 0;  // 0 .. 4
			}
			else {
				// only for shadows and movingobjs ...
				zoff_start = hang_ribi & direction                      ? slope_height+1 : 0;  // 0 .. 3
				zoff_end   = hang_ribi & ribi_t::backward(direction) ? slope_height+1 : 0;  // 0 .. 3
			}
		}
		else {
			zoff_start = (gr->get_weg_yoff() * 2) / TILE_HEIGHT_STEP;
			zoff_end = zoff_start;
		}
	}
}


sint16 vehicle_base_t::get_hoff(const sint16 raster_width) const
{
	sint16 h_start = -(sint8)TILE_HEIGHT_STEP * (sint8)zoff_start;
	sint16 h_end   = -(sint8)TILE_HEIGHT_STEP * (sint8)zoff_end;
	return ((h_start*steps + h_end*(256-steps))*raster_width) >> 9;
}


/* true, if one could pass through this field
 * also used for citycars, thus defined here
 */
vehicle_base_t *vehicle_base_t::no_cars_blocking( const grund_t *gr, const convoi_t *cnv, const uint8 current_direction, const uint8 next_direction, const uint8 next_90direction )
{
	// Search vehicle
	for(  uint8 pos=1;  pos<(uint8)gr->get_top();  pos++  ) {
		if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
			if(  v->get_typ()==obj_t::pedestrian  ) {
				continue;
			}

			// check for car
			uint8 other_direction=255;
			bool other_moving = false;
			if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
				// ignore ourself
				if(  cnv == at->get_convoi()  ) {
					continue;
				}
				other_direction = at->get_direction();
				other_moving = at->get_convoi()->get_akt_speed() > kmh_to_speed(1);
			}
			// check for city car
			else if(  v->get_waytype() == road_wt  ) {
				other_direction = v->get_direction();
				if(  private_car_t const* const sa = obj_cast<private_car_t>(v)  ){
					other_moving = sa->get_current_speed() > 1;
				}
			}

			// ok, there is another car ...
			if(  other_direction != 255  ) {
				if(  next_direction == other_direction  &&  !ribi_t::is_threeway(gr->get_weg_ribi(road_wt))  ) {
					// cars going in the same direction and no crossing => that mean blocking ...
					return v;
				}

				const ribi_t::ribi other_90direction = (gr->get_pos().get_2d() == v->get_pos_next().get_2d()) ? other_direction : calc_direction(gr->get_pos(), v->get_pos_next());
				if(  other_90direction == next_90direction  ) {
					// Want to exit in same as other   ~50% of the time
					return v;
				}

				const bool drives_on_left = welt->get_settings().is_drive_left();
				const bool across = next_direction == (drives_on_left ? ribi_t::rotate45l(next_90direction) : ribi_t::rotate45(next_90direction)); // turning across the opposite directions lane
				const bool other_across = other_direction == (drives_on_left ? ribi_t::rotate45l(other_90direction) : ribi_t::rotate45(other_90direction)); // other is turning across the opposite directions lane
				if(  other_direction == next_direction  &&  !(other_across || across)  ) {
					// entering same straight waypoint as other ~18%
					return v;
				}

				const bool straight = next_direction == next_90direction; // driving straight
				const ribi_t::ribi current_90direction = straight ? ribi_t::backward(next_90direction) : (~(next_direction|ribi_t::backward(next_90direction)))&0x0F;
				const bool other_straight = other_direction == other_90direction; // other is driving straight
				const bool other_exit_same_side = current_90direction == other_90direction; // other is exiting same side as we're entering
				const bool other_exit_opposite_side = ribi_t::backward(current_90direction) == other_90direction; // other is exiting side across from where we're entering
				if(  across  &&  ((ribi_t::is_perpendicular(current_90direction,other_direction)  &&  other_moving)  ||  (other_across  &&  other_exit_opposite_side)  ||  ((other_across  ||  other_straight)  &&  other_exit_same_side  &&  other_moving) ) )  {
					// other turning across in front of us from orth entry dir'n   ~4%
					return v;
				}

				const bool headon = ribi_t::backward(current_direction) == other_direction; // we're meeting the other headon
				const bool other_exit_across = (drives_on_left ? ribi_t::rotate90l(next_90direction) : ribi_t::rotate90(next_90direction)) == other_90direction; // other is exiting by turning across the opposite directions lane
				if(  straight  &&  (ribi_t::is_perpendicular(current_90direction,other_direction)  ||  (other_across  &&  other_moving  &&  (other_exit_across  ||  (other_exit_same_side  &&  !headon))) ) ) {
					// other turning across in front of us, but allow if other is stopped - duplicating historic behaviour   ~2%
					return v;
				}
				else if(  other_direction == current_direction  &&  current_90direction == ribi_t::none  ) {
					// entering same diagonal waypoint as other   ~1%
					return v;
				}

				// else other car is not blocking   ~25%
			}
		}
	}

	// way is free
	return NULL;
}


bool vehicle_base_t::is_free_for_passing( const grund_t *gr, const overtaker_t *self, const overtaker_t *other, bool &other_here )
{
	other_here = false;
	for(  uint8 pos=1;  pos<(uint8)gr->get_top();  pos++  ) {
		if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
			const overtaker_t *ov = v->get_overtaker();
			if(  ov  ) {
				if(  ov==other  ) {
					other_here = true;
				}
				else if(  ov!=self  ) {
					return false;
				}
			}
			else if(  v->get_waytype()==road_wt  &&  v->get_typ()!=obj_t::pedestrian  ) {
				// sheep etc.
				return false;
			}
		}
	}
	return true;
}


void vehicle_t::rotate90()
{
	vehicle_base_t::rotate90();
	previous_direction = ribi_t::rotate90( previous_direction );
	last_stop_pos.rotate90( welt->get_size().y-1 );
}


void vehicle_t::rotate90_freight_destinations(const sint16 y_size)
{
	// now rotate the freight
	FOR(slist_tpl<ware_t>, & tmp, fracht) {
		tmp.rotate90(y_size );
	}
}


void vehicle_t::set_convoi(convoi_t *c)
{
	/* cnv can have three values:
	 * NULL: not previously assigned
	 * 1 (only during loading): convoi wants to reserve the whole route
	 * other: previous convoi (in this case, currently always c==cnv)
	 *
	 * if c is NULL, then the vehicle is removed from the convoi
	 * (the rail_vehicle_t::set_convoi etc. routines must then remove a
	 *  possibly pending reservation of stops/tracks)
	 */
	assert(  c==NULL  ||  cnv==NULL  ||  cnv==(convoi_t *)1  ||  c==cnv);
	cnv = c;
	if(cnv) {
		// we need to re-establish the finish flag after loading
		if(leading) {
			route_t const& r = *cnv->get_route();
			check_for_finish = r.empty() || route_index >= r.get_count() || get_pos() == r.at(route_index);
		}
		if(  pos_next != koord3d::invalid  ) {
			route_t const& r = *cnv->get_route();
			if (!r.empty() && route_index < r.get_count() - 1) {
				grund_t const* const gr = welt->lookup(pos_next);
				if (!gr || !gr->get_weg(get_waytype())) {
					if (!(water_wt == get_waytype()  &&  gr->is_water())) { // ships on the open sea are valid
						pos_next = r.at(route_index + 1U);
					}
				}
			}
		}
		// just correct freight destinations
		FOR(slist_tpl<ware_t>, & c, fracht) {
			c.finish_rd(welt);
		}
	}
}


/**
 * Unload freight to halt
 * @return sum of unloaded goods
 */
uint16 vehicle_t::unload_cargo(halthandle_t halt, bool unload_all, bool unload_any )
{
	uint16 sum_menge = 0, sum_delivered = 0, index = 0;
	if(  !halt.is_bound()  ||  !unload_any  ) {
		return 0;
	}

	if(  halt->is_enabled( get_cargo_type() )  ) {
		if(  !fracht.empty()  ) {

			for(  slist_tpl<ware_t>::iterator i = fracht.begin(), end = fracht.end();  i != end;  ) {
				const ware_t& tmp = *i;

				halthandle_t end_halt = tmp.get_ziel();
				halthandle_t via_halt = tmp.get_zwischenziel();

				// check if destination or transfer is still valid
				if(  !end_halt.is_bound() || !via_halt.is_bound()  ) {
					// target halt no longer there => delete and remove from fab in transit
					fabrik_t::update_transit( &tmp, false );
					DBG_MESSAGE("vehicle_t::unload_freight()", "destination of %d %s is no longer reachable",tmp.menge,translator::translate(tmp.get_name()));
					total_freight -= tmp.menge;
					sum_weight -= tmp.menge * tmp.get_desc()->get_weight_per_unit();
					i = fracht.erase( i );
				}
				else if(  end_halt == halt || via_halt == halt  ||  unload_all  ) {

//					printf("Liefere %d %s nach %s via %s an %s\n",
//						tmp->menge,
//						tmp->name(),
//						end_halt->get_name(),
//						via_halt->get_name(),
//						halt->get_name());

					// here, only ordinary goods should be processed
					// (fork: what waits at a halt never counts as boarded)
					ware_t delivered = tmp;
					delivered.boarded_here = 0;
					int menge = halt->liefere_an(delivered);
					sum_menge += menge;
					total_freight -= menge;
					sum_weight -= tmp.menge * tmp.get_desc()->get_weight_per_unit();

					index = tmp.get_index();

					if(end_halt==halt) {
						sum_delivered += menge;
					}

					i = fracht.erase( i );
				}
				else {
					++i;
				}
			}
		}
	}

	if(  sum_menge  ) {
		// book transported goods
		get_owner()->book_transported( sum_menge, get_desc()->get_waytype(), index );

		if(  sum_delivered  ) {
			// book delivered goods to destination
			get_owner()->book_delivered( sum_delivered, get_desc()->get_waytype(), index );
		}

		// add delivered goods to statistics
		cnv->book( sum_menge, convoi_t::CONVOI_TRANSPORTED_GOODS );

		// add delivered goods to halt's statistics
		halt->book( sum_menge, HALT_ARRIVED );
	}
	return sum_menge;
}


/**
 * Load freight from halt
 * @return amount loaded
 */
bool vehicle_t::can_carry_crowd() const
{
	return desc->get_freight_type()==goods_manager_t::passengers  &&  desc->get_capacity() > 0;
}


uint16 vehicle_t::get_standing_max() const
{
	return (uint16)min( 2 * (uint32)desc->get_capacity(), 65535 );
}


uint16 vehicle_t::get_overcrowded_max() const
{
	return (uint16)min( 13 * (uint32)desc->get_capacity() / 2, 65535 );
}


void vehicle_t::get_crowd_split(uint16 &seated, uint16 &standing, uint16 &overcrowded) const
{
	const uint16 seats = desc->get_capacity();
	seated = total_freight < seats ? total_freight : seats;
	const uint16 above = total_freight - seated;
	const uint16 standing_places = get_standing_max() - seats;
	standing = above < standing_places ? above : standing_places;
	overcrowded = above - standing;
}


uint16 vehicle_t::load_cargo(halthandle_t halt, const vector_tpl<halthandle_t>& destination_halts, uint16 limit, bool only_missed)
{
	if(  !halt.is_bound()  ||  !halt->gibt_ab(desc->get_freight_type())  ) {
		return 0;
	}

	const uint16 total_freight_start = total_freight;
	const uint16 capacity_left = limit > total_freight ? limit - total_freight : 0;
	if (capacity_left > 0) {

		slist_tpl<ware_t> freight_add;
		halt->fetch_goods( freight_add, desc->get_freight_type(), capacity_left, destination_halts, only_missed );

		if(  freight_add.empty()  ) {
			// now empty, but usually, we can get it here ...
			return 0;
		}

		for(  slist_tpl<ware_t>::iterator iter_z = freight_add.begin();  iter_z != freight_add.end();  ) {
			ware_t &ware = *iter_z;

			total_freight += ware.menge;
			sum_weight += ware.menge * ware.get_desc()->get_weight_per_unit();

			// fork: boarded here, so a train that leaves first may take them over (before joining,
			// so they stay apart from those who ride through)
			ware.boarded_here = ware.is_passenger()  ||  ware.is_mail();

			// could this be joined with existing freight?
			FOR( slist_tpl<ware_t>, & tmp, fracht ) {
				// for pax: join according next stop
				// for all others we *must* use target coordinates
				if(  ware.same_destination(tmp)  ) {
					tmp.menge += ware.menge;
					ware.menge = 0;
					break;
				}
			}

			// if != 0 we could not join it to existing => load it
			if(  ware.menge != 0  ) {
				++iter_z;
				// we add list directly
			}
			else {
				iter_z = freight_add.erase(iter_z);
			}
		}

		if(  !freight_add.empty()  ) {
			fracht.append_list(freight_add);
		}
	}
	return total_freight - total_freight_start;
}


void vehicle_t::clear_boarded_here()
{
	bool any = false;
	FOR( slist_tpl<ware_t>, & w, fracht ) {
		if(  w.boarded_here  ) {
			w.boarded_here = 0;
			any = true;
		}
	}
	if(  !any  ) {
		return;
	}
	// join what boarded at the last stop with those who rode through it
	for(  slist_tpl<ware_t>::iterator i = fracht.begin();  i != fracht.end();  ++i  ) {
		slist_tpl<ware_t>::iterator j = i;
		++j;
		while(  j != fracht.end()  ) {
			if(  (*i).same_destination( *j )  ) {
				(*i).menge += (*j).menge;
				j = fracht.erase( j );
			}
			else {
				++j;
			}
		}
	}
}


uint16 vehicle_t::take_boarded(halthandle_t via, uint16 amount, slist_tpl<ware_t> &out)
{
	uint16 taken = 0;
	for(  slist_tpl<ware_t>::iterator i = fracht.begin();  i != fracht.end()  &&  taken < amount;  ) {
		ware_t &w = *i;
		if(  !w.boarded_here  ||  w.menge == 0  ||  w.get_zwischenziel() != via  ) {
			++i;
			continue;
		}
		const uint16 n = (uint16)min( (uint32)w.menge, (uint32)(amount - taken) );
		ware_t part = w;
		part.menge = n;
		part.boarded_here = 0;
		out.append( part );
		taken += n;
		total_freight -= n;
		sum_weight -= n * w.get_desc()->get_weight_per_unit();
		if(  n == w.menge  ) {
			i = fracht.erase( i );
		}
		else {
			w.menge -= n;
			++i;
		}
	}
	return taken;
}


uint16 vehicle_t::add_cargo(ware_t &ware, uint16 limit)
{
	if(  ware.menge == 0  ||  total_freight >= limit  ) {
		return 0;
	}
	const uint16 n = (uint16)min( (uint32)ware.menge, (uint32)(limit - total_freight) );
	ware.menge -= n;
	total_freight += n;
	sum_weight += n * ware.get_desc()->get_weight_per_unit();
	FOR( slist_tpl<ware_t>, & tmp, fracht ) {
		if(  ware.same_destination( tmp )  ) {
			tmp.menge += n;
			return n;
		}
	}
	ware_t part = ware;
	part.menge = n;
	fracht.append( part );
	return n;
}


/**
 * Remove freight that no longer can reach it's destination
 * i.e. because of a changed schedule
 */
void vehicle_t::remove_stale_cargo(const schedule_t *sched)
{
	DBG_DEBUG("vehicle_t::remove_stale_cargo()", "called");

	// and now check every piece of ware on board,
	// if its target is somewhere on
	// the new schedule, if not -> remove
	slist_tpl<ware_t> kill_queue;
	total_freight = 0;
	if(  sched == NULL  ) {
		sched = cnv->get_schedule();
	}

	if (!fracht.empty()) {
		FOR(slist_tpl<ware_t>, & tmp, fracht) {
			bool found = false;

			if(  tmp.get_zwischenziel().is_bound()  ) {
				// the original halt exists, but does we still go there?
				FOR(minivec_tpl<schedule_entry_t>, const& i, sched->entries) {
					if(  haltestelle_t::get_halt( i.pos, cnv->get_owner()) == tmp.get_zwischenziel()  ) {
						found = true;
						break;
					}
				}
			}
			if(  !found  ) {
				// the target halt may have been joined or there is a closer one now, thus our original target is no longer valid
				const int offset = sched->get_current_stop();
				const int max_count = sched->entries.get_count();
				for(  int i=0;  i<max_count;  i++  ) {
					// try to unload on next stop
					halthandle_t halt = haltestelle_t::get_halt( sched->entries[ (i+offset)%max_count ].pos, cnv->get_owner() );
					if(  halt.is_bound()  ) {
						if(  halt->is_enabled(tmp.get_index())  ) {
							// ok, lets change here, since goods are accepted here
							tmp.access_zwischenziel() = halt;
							if (!tmp.get_ziel().is_bound()) {
								// set target, to prevent that unload_freight drops cargo
								tmp.set_ziel( halt );
							}
							found = true;
							break;
						}
					}
				}
			}

			if(  !found  ) {
				kill_queue.insert(tmp);
			}
			else {
				// since we need to point at factory (0,0), we recheck this too
				koord k = tmp.get_zielpos();
				fabrik_t *fab = fabrik_t::get_fab( k );
				tmp.set_zielpos( fab ? fab->get_pos().get_2d() : k );

				total_freight += tmp.menge;
			}
		}

		FOR(slist_tpl<ware_t>, const& c, kill_queue) {
			fabrik_t::update_transit( &c, false );
			fracht.remove(c);
		}
	}
	sum_weight =  get_cargo_weight() + desc->get_weight();
}


void vehicle_t::play_sound() const
{
	if(  desc->get_sound() >= 0  &&  !welt->is_fast_forward()  ) {
		welt->play_sound_area_clipped(get_pos().get_2d(), desc->get_sound(), TRAFFIC_SOUND );
	}
}


/**
 * Prepare vehicle for new ride.
 * Sets route_index, pos_next, steps_next.
 * If @p recalc is true this sets position and recalculates/resets movement parameters.
 */
void vehicle_t::initialise_journey(uint16 start_route_index, bool recalc)
{
	route_index = start_route_index+1;
	check_for_finish = false;
	use_calc_height = true;

	if(welt->is_within_limits(get_pos().get_2d())) {
		mark_image_dirty( get_image(), 0 );
	}

	route_t const& r = *cnv->get_route();
	if(!recalc) {
		// always set pos_next
		pos_next = r.at(route_index);
		assert(get_pos() == r.at(start_route_index));
	}
	else {
		// set pos_next
		if (route_index < r.get_count()) {
			pos_next = r.at(route_index);
		}
		else {
			// already at end of route
			check_for_finish = true;
		}
		set_pos(r.at(start_route_index));

		// recalc directions
		previous_direction = direction;
		direction = calc_set_direction( get_pos(), pos_next );

		zoff_start = zoff_end = 0;
		steps = 0;

		set_xoff( (dx<0) ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
		set_yoff( (dy<0) ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );

		calc_image();
	}
	if ( ribi_t::is_single(direction) ) {
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	}
	else {
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	}
}


vehicle_t::vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player) :
	vehicle_base_t(pos)
{
	this->desc = desc;

	set_owner( player );
	purchase_time = welt->get_current_month();
	cnv = NULL;
	speed_limit = SPEED_UNLIMITED;

	route_index = 1;

	smoke = true;
	direction = ribi_t::none;

	current_friction = 4;
	total_freight = 0;
	sum_weight = desc->get_weight();

	leading = last = false;
	check_for_finish = false;
	use_calc_height = true;
	has_driven = false;
	idle = false;
	on_wire = true;

	previous_direction = direction = ribi_t::none;
	target_halt = halthandle_t();
}


vehicle_t::vehicle_t() :
	vehicle_base_t()
{
	smoke = true;

	desc = NULL;
	cnv = NULL;

	route_index = 1;
	current_friction = 4;
	sum_weight = 10;
	total_freight = 0;

	leading = last = false;
	check_for_finish = false;
	use_calc_height = true;
	idle = false;
	on_wire = true;

	previous_direction = direction = ribi_t::none;
}


bool vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, route_t* route)
{
	return route->calc_route(welt, start, ziel, this, max_speed, 0 );
}


grund_t* vehicle_t::hop_check()
{
	// the leading vehicle will do all the checks
	if(leading) {
		if(check_for_finish) {
			// so we are there yet?
			cnv->ziel_erreicht();
			if(cnv->get_state()==convoi_t::INITIAL) {
				// to avoid crashes with airplanes
				use_calc_height = false;
			}
			return NULL;
		}

		// now check, if we can go here
		grund_t *bd = welt->lookup(pos_next);
		if(bd==NULL  ||  !check_next_tile(bd)  ||  cnv->get_route()->empty()) {
			// way (weg) not existent (likely destroyed) or no route ...
			cnv->suche_neue_route();
			return NULL;
		}

		// check for one-way sign etc.
		const waytype_t wt = get_waytype();
		if(  air_wt != wt  &&  route_index < cnv->get_route()->get_count()-1  ) {
			uint8 dir = get_ribi(bd);
			koord3d nextnext_pos = cnv->get_route()->at(route_index+1);
			if ( nextnext_pos == get_pos() ) {
				dbg->error("vehicle_t::hop_check", "route contains point (%s) twice for %s", nextnext_pos.get_str(), cnv->get_name());
			}
			uint8 new_dir = ribi_type(nextnext_pos - pos_next);
			if((dir&new_dir)==0) {
				// new one way sign here?
				cnv->suche_neue_route();
				return NULL;
			}
			// check for recently built bridges/tunnels or reverse branches (really slows down the game, so we do this only on slopes)
			if(  bd->get_weg_hang()  ) {
				grund_t *from;
				if(  !bd->get_neighbour( from, get_waytype(), ribi_type( get_pos(), pos_next ) )  ) {
					// way likely destroyed or altered => reroute
					cnv->suche_neue_route();
					return NULL;
				}
			}
		}

		sint32 restart_speed = -1;
		// ist_weg_frei() berechnet auch die Geschwindigkeit
		// mit der spaeter weitergefahren wird
		if(  !can_enter_tile( bd, restart_speed, 0 )  ) {
			// stop convoi, when the way is not free
			cnv->warten_bis_weg_frei(restart_speed);

			// don't continue
			return NULL;
		}
		// we cache it here, hop() will use it to save calls to karte_t::lookup
		return bd;
	}
	else {
		// this is needed since in convoi_t::vorfahren the flag 'leading' is set to null
		if(check_for_finish) {
			return NULL;
		}
	}
	return welt->lookup(pos_next);
}


bool vehicle_t::can_enter_tile(sint32 &restart_speed, uint8 second_check_count)
{
	grund_t *gr = welt->lookup( pos_next );
	if(  gr  ) {
		return can_enter_tile( gr, restart_speed, second_check_count );
	}
	else {
		if(  !second_check_count  ) {
			cnv->suche_neue_route();
		}
		return false;
	}
}


void vehicle_t::leave_tile()
{
	vehicle_base_t::leave_tile();
#ifndef DEBUG_ROUTES
	if(last  &&  minimap_t::is_visible) {
			minimap_t::get_instance()->calc_map_pixel(get_pos().get_2d());
	}
#endif
}


/** this routine add a vehicle to a tile and will insert it in the correct sort order to prevent overlaps
 */
void vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_base_t::enter_tile(gr);

	if(leading  &&  minimap_t::is_visible  ) {
		minimap_t::get_instance()->calc_map_pixel( get_pos().get_2d() );
	}
}


void vehicle_t::hop(grund_t* gr)
{
	leave_tile();

	koord3d pos_prev = get_pos();
	set_pos( pos_next );  // next field
	if(route_index<cnv->get_route()->get_count()-1) {
		route_index ++;
		pos_next = cnv->get_route()->at(route_index);
	}
	else {
		route_index ++;
		check_for_finish = true;
	}
	previous_direction = direction;

	// check if arrived at waypoint, and update schedule to next destination
	// route search through the waypoint is already complete
	if(  get_pos()==cnv->get_schedule_target()  ) {
		if(  route_index >= cnv->get_route()->get_count()  ) {
			// we end up here after loading a game or when a waypoint is reached which crosses next itself
			cnv->set_schedule_target( koord3d::invalid );
		}
		else {
			cnv->get_schedule()->advance();
			const koord3d ziel = cnv->get_schedule()->get_current_entry().pos;
			cnv->set_schedule_target( cnv->is_waypoint(ziel) ? ziel : koord3d::invalid );
		}
	}
	// fork, coupling: the joined train's schedule passes the waypoints of its own that the pair passes
	if(  cnv->is_coupled_primary()  &&  cnv->front()==this  ) {
		convoi_t *const joined = cnv->get_coupled_convoi().get_rep();
		schedule_t *const js = joined->get_schedule();
		if(  js  &&  !js->empty()  &&  js->get_current_entry().pos==get_pos()  &&  joined->is_waypoint( get_pos() )  ) {
			js->advance();
		}
	}

	// this is a required hack for aircrafts! Aircrafts can turn on a single square, and this confuses the previous calculation!
	if(!check_for_finish  &&  pos_prev==pos_next) {
		direction = calc_set_direction( get_pos(), pos_next);
		steps_next = 0;
	}
	else {
		if(  pos_next!=get_pos()  ) {
			direction = calc_set_direction( pos_prev, pos_next );
		}
		else if(  (  check_for_finish  &&  welt->lookup(pos_next)  &&  ribi_t::is_straight(welt->lookup(pos_next)->get_weg_ribi_unmasked(get_waytype()))  )  ||  welt->lookup(pos_next)->is_halt()) {
			// allow diagonal stops at waypoints on diagonal tracks but avoid them on halts and at straight tracks...
			direction = calc_set_direction( pos_prev, pos_next );
		}
	}

	// change image if direction changes
	if (previous_direction != direction) {
		calc_image();
	}
	sint32 old_speed_limit = speed_limit;

	enter_tile(gr);
	const weg_t *weg = gr->get_weg(get_waytype());
	if(  weg  ) {
		speed_limit = kmh_to_speed( weg->get_max_speed() );
		if(  weg->is_crossing()  ) {
			gr->find<crossing_t>(2)->add_to_crossing(this);
		}
	}
	else {
		speed_limit = SPEED_UNLIMITED;
	}

	// fork, mixed traction: an electric engine entering or leaving catenary changes which engines pull
	if(  cnv->has_mixed_traction()  &&  desc->get_power()  &&  desc->get_engine_type()==vehicle_desc_t::electric  ) {
		const bool was_on_wire = on_wire;
		if(  update_on_wire() != was_on_wire  ) {
			cnv->recalc_traction( false );
		}
	}

	if(  leading  ) {
		if(  check_for_finish  &&  (direction==ribi_t::north  ||  direction==ribi_t::west)  ) {
			steps_next = (steps_next/2)+1;
		}
		cnv->add_running_cost( weg );
		cnv->must_recalc_data_front();
	}

	// update friction and friction weight of convoy
	sint16 old_friction = current_friction;
	calc_friction(gr);

	if (old_friction != current_friction) {
		cnv->update_friction_weight( (current_friction-old_friction) * (sint64)sum_weight);
	}

	// if speed limit changed, then cnv must recalc
	if (speed_limit != old_speed_limit) {
		if (speed_limit < old_speed_limit) {
			if (speed_limit < cnv->get_speed_limit()) {
				// update
				cnv->set_speed_limit(speed_limit);
			}
		}
		else {
			if (old_speed_limit == cnv->get_speed_limit()) {
				// convoy's speed limit may be larger now
				cnv->must_recalc_speed_limit();
			}
		}
	}
}


/** calculates the current friction coefficient based on the current track
 * flat, slope, curve ...
 */
void vehicle_t::calc_friction(const grund_t *gr)
{

	// assume straight flat track
	current_friction = 1;

	// curve: higher friction
	if(previous_direction != direction) {
		current_friction = 8;
	}

	// or a hill?
	const slope_t::type hang = gr->get_weg_hang();
	if(  hang != slope_t::flat  ) {
		const uint slope_height = is_one_high(hang) ? 1 : 2;
		if(  ribi_type(hang) == direction  ) {
			// hill up, since height offsets are negative: heavy decelerate
			current_friction += 15 * slope_height * slope_height;
		}
		else {
			// hill down: accelerate
			current_friction += -7 * slope_height * slope_height;
		}
	}
}


bool vehicle_t::update_on_wire()
{
	if(  const grund_t *gr = welt->lookup( get_pos() )  ) {
		const weg_t *w = gr->get_weg( get_waytype() );
		if(  w == NULL  &&  get_waytype() == tram_wt  ) {
			// tram depots stand on track
			w = gr->get_weg( track_wt );
		}
		on_wire = w != NULL  &&  w->is_electrified();
	}
	return on_wire;
}


void vehicle_t::make_smoke() const
{
	// does it smoke at all? (fork: an idle engine does not)
	if(  smoke  &&  !idle  &&  desc->get_smoke()  ) {
		// only produce smoke when heavily accelerating or steam engine
		if(  cnv->get_akt_speed() < (sint32)((cnv->get_speed_limit() * 7u) >> 3)  ||  desc->get_engine_type() == vehicle_desc_t::steam  ) {
			grund_t* const gr = welt->lookup( get_pos() );
			if(  gr  ) {
				wolke_t* const abgas =  new wolke_t( get_pos(), get_xoff() + ((dx * (sint16)((uint16)steps * OBJECT_OFFSET_STEPS)) >> VEHICLE_STEPS_PER_TILE_SHIFT), get_yoff() + ((dy * (sint16)((uint16)steps * OBJECT_OFFSET_STEPS)) >> VEHICLE_STEPS_PER_TILE_SHIFT), get_hoff() - LEGACY_SMOKE_YOFFSET, DEFAULT_EXHAUSTSMOKE_TIME, DEFAULT_SMOKE_UPLIFT, desc->get_smoke() );
				if(  !gr->obj_add( abgas )  ) {
					abgas->set_flag( obj_t::not_on_map );
					delete abgas;
				}
				else {
					welt->sync_way_eyecandy.add( abgas );
				}
			}
		}
	}
}


/**
 * Payment is done per hop. It iterates all goods and calculates
 * the income for the last hop. This method must be called upon
 * every stop.
 * @return income total for last hop
 */
sint64 vehicle_t::calc_revenue(const koord3d& start, const koord3d& end) const
{
	// may happen when waiting in station
	if (start == end || fracht.empty()) {
		return 0;
	}

	// cnv_kmh = lesser of min_top_speed, power limited top speed, and average way speed limits on trip, except aircraft which are not power limited and don't have speed limits
	sint32 cnv_kmh = cnv->get_speedbonus_kmh();

	sint64 value = 0;

	// cache speedbonus price
	const goods_desc_t* last_freight = NULL;
	sint64 freight_revenue = 0;

	sint32 dist = 0;
	if(  welt->get_settings().get_pay_for_total_distance_mode() == settings_t::TO_PREVIOUS  ) {
		// pay distance traveled
		dist = koord_distance( start, end );
	}

	FOR(slist_tpl<ware_t>, const& ware, fracht) {
		if(  ware.menge==0  ) {
			continue;
		}
		// which distance will be paid?
		switch(welt->get_settings().get_pay_for_total_distance_mode()) {
			case settings_t::TO_TRANSFER: {
				// pay distance traveled to next transfer stop

				// now only use the real gain in difference for the revenue (may as well be negative!)
				if (ware.get_zwischenziel().is_bound()) {
					const koord &zwpos = ware.get_zwischenziel()->get_basis_pos();
					// cast of koord_distance to sint32 is necessary otherwise the r-value would be interpreted as unsigned, leading to overflows
					dist = (sint32)koord_distance( zwpos, start ) - (sint32)koord_distance( end, zwpos );
				}
				else {
					dist = koord_distance( end, start );
				}
				break;
			}
			case settings_t::TO_DESTINATION: {
				// pay only the distance, we get closer to our destination

				// now only use the real gain in difference for the revenue (may as well be negative!)
				const koord &zwpos = ware.get_zielpos();
				// cast of koord_distance to sint32 is necessary otherwise the r-value would be interpreted as unsigned, leading to overflows
				dist = (sint32)koord_distance( zwpos, start ) - (sint32)koord_distance( end, zwpos );
				break;
			}
			default: ; // no need to recompute
		}

		// calculate freight revenue incl. speed-bonus
		if (ware.get_desc() != last_freight) {
			freight_revenue = ware_t::calc_revenue(ware.get_desc(), get_desc()->get_waytype(), cnv_kmh);
			last_freight = ware.get_desc();
		}
		const sint64 price = freight_revenue * (sint64)dist * (sint64)ware.menge;

		// sum up new price
		value += price;
	}

	if(  total_freight > desc->get_capacity()  &&  can_carry_crowd()  ) {
		// fork: standing passengers pay 3/4, overcrowded ones 1/4, shared by all aboard during this hop
		const sint64 all = total_freight;
		const sint64 seated = desc->get_capacity();
		const sint64 standing = all - seated < seated ? all - seated : seated;
		const sint64 overcrowded = all - seated - standing;
		value = value * (4*seated + 3*standing + overcrowded) / (4*all);
	}

	// Rounded value, in cents
	return (value+1500ll)/3000ll;
}


const char *vehicle_t::get_cargo_mass() const
{
	return get_cargo_type()->get_mass();
}


/**
 * Calculate transported cargo total weight in KG
 */
uint32 vehicle_t::get_cargo_weight() const
{
	uint32 weight = 0;

	FOR(slist_tpl<ware_t>, const& c, fracht) {
		weight += c.menge * c.get_desc()->get_weight_per_unit();
	}
	return weight;
}


void vehicle_t::get_cargo_info(cbuffer_t & buf) const
{
	if (fracht.empty()) {
		buf.append("  ");
		buf.append(translator::translate("leer"));
		buf.append("\n");
	} else {
		FOR(slist_tpl<ware_t>, const& ware, fracht) {
			const char * name = "Error in Routing";

			halthandle_t halt = ware.get_ziel();
			if(halt.is_bound()) {
				name = halt->get_name();
			}

			buf.printf("   %u%s %s > %s\n", ware.menge, translator::translate(ware.get_mass()), translator::translate(ware.get_name()), name);
		}
	}
}


/**
 * Delete all vehicle load
 */
void vehicle_t::discard_cargo()
{
	FOR(  slist_tpl<ware_t>, w, fracht ) {
		fabrik_t::update_transit( &w, false );
	}
	fracht.clear();
	sum_weight =  desc->get_weight();
}


void vehicle_t::calc_image()
{
	image_id old_image=get_image();
	if (fracht.empty()) {
		set_image(desc->get_image_id(ribi_t::get_dir(get_direction()),NULL));
	}
	else {
		set_image(desc->get_image_id(ribi_t::get_dir(get_direction()), fracht.front().get_desc()));
	}
	if(old_image!=get_image()) {
		set_flag(obj_t::dirty);
	}
}


image_id vehicle_t::get_loaded_image() const
{
	return desc->get_image_id(ribi_t::dir_south, fracht.empty() ?  NULL  : fracht.front().get_desc());
}


// true, if this vehicle did not moved for some time
bool vehicle_t::is_stuck()
{
	return cnv==NULL  ||  cnv->is_waiting();
}


void vehicle_t::rdwr(loadsave_t *file)
{
	// this is only called from objlist => we save nothing ...
	assert(  file->is_saving()  ); (void)file;
}


void vehicle_t::rdwr_from_convoi(loadsave_t *file)
{
	xml_tag_t r( file, "vehikel_t" );

	sint32 fracht_count = 0;

	if(file->is_saving()) {
		fracht_count = fracht.get_count();
		// we try to have one freight count to guess the right freight
		// when no desc is given
		if(fracht_count==0  &&  desc->get_freight_type()!=goods_manager_t::none  &&  desc->get_capacity()>0) {
			fracht_count = 1;
		}
	}

	obj_t::rdwr(file);

	// since obj_t does no longer save positions
	if(  file->is_version_atleast(101, 0)  ) {
		koord3d pos = get_pos();
		pos.rdwr(file);
		set_pos(pos);
	}


	sint8 hoff = file->is_saving() ? get_hoff() : 0;

	if(file->is_version_less(86, 6)) {
		sint32 l;
		file->rdwr_long(purchase_time);
		file->rdwr_long(l);
		dx = (sint8)l;
		file->rdwr_long(l);
		dy = (sint8)l;
		file->rdwr_long(l);
		hoff = (sint8)(l*TILE_HEIGHT_STEP/16);
		file->rdwr_long(speed_limit);
		file->rdwr_enum(direction);
		file->rdwr_enum(previous_direction);
		file->rdwr_long(fracht_count);
		file->rdwr_long(l);
		route_index = (uint16)l;
		purchase_time = (purchase_time >> welt->ticks_per_world_month_shift) + welt->get_settings().get_starting_year();
DBG_MESSAGE("vehicle_t::rdwr_from_convoi()","bought at %i/%i.",(purchase_time%12)+1,purchase_time/12);
	}
	else {
		// changed several data types to save runtime memory
		file->rdwr_long(purchase_time);
		if(file->is_version_less(99, 18)) {
			file->rdwr_byte(dx);
			file->rdwr_byte(dy);
		}
		else {
			file->rdwr_byte(steps);
			file->rdwr_byte(steps_next);
			if(steps_next==old_diagonal_vehicle_steps_per_tile - 1  &&  file->is_loading()) {
				// reset diagonal length (convoi will be reset anyway, if game diagonal is different)
				steps_next = diagonal_vehicle_steps_per_tile - 1;
			}
		}
		sint16 dummy16 = ((16*(sint16)hoff)/TILE_HEIGHT_STEP);
		file->rdwr_short(dummy16);
		hoff = (sint8)((TILE_HEIGHT_STEP*(sint16)dummy16)/16);
		file->rdwr_long(speed_limit);
		file->rdwr_enum(direction);
		file->rdwr_enum(previous_direction);
		file->rdwr_long(fracht_count);
		file->rdwr_short(route_index);
		// restore dxdy information
		dx = dxdy[ ribi_t::get_dir(direction)*2];
		dy = dxdy[ ribi_t::get_dir(direction)*2+1];
	}

	// convert steps to position
	if(file->is_version_less(99, 18)) {
		sint8 ddx=get_xoff(), ddy=get_yoff()-hoff;
		sint8 i=1;
		dx = dxdy[ ribi_t::get_dir(direction)*2];
		dy = dxdy[ ribi_t::get_dir(direction)*2+1];

		while(  !is_about_to_hop(ddx+dx*i,ddy+dy*i )  &&  i<16 ) {
			i++;
		}
		i--;
		set_xoff( ddx-(16-i)*dx );
		set_yoff( ddy-(16-i)*dy );
		if(file->is_loading()) {
			if(dx && dy) {
				steps = min( VEHICLE_STEPS_PER_TILE - 1, VEHICLE_STEPS_PER_TILE - 1-(i*16) );
				steps_next = VEHICLE_STEPS_PER_TILE - 1;
			}
			else {
				// will be corrected anyway, if in a convoi
				steps = min( diagonal_vehicle_steps_per_tile - 1, diagonal_vehicle_steps_per_tile - 1-(uint8)(((uint16)i*(uint16)(diagonal_vehicle_steps_per_tile - 1))/8) );
				steps_next = diagonal_vehicle_steps_per_tile - 1;
			}
		}
	}

	// information about the target halt
	if(file->is_version_atleast(88, 7)) {
		bool target_info;
		if(file->is_loading()) {
			file->rdwr_bool(target_info);
			cnv = (convoi_t *)target_info; // will be checked during convoi reassignment
		}
		else {
			target_info = target_halt.is_bound();
			file->rdwr_bool(target_info);
		}
	}
	else {
		if(file->is_loading()) {
			cnv = NULL; // no reservation too
		}
	}
	if(file->is_version_less(112, 9)) {
		koord3d pos_prev(koord3d::invalid);
		pos_prev.rdwr(file);
	}

	if(file->is_version_less(99, 5)) {
		koord3d dummy;
		dummy.rdwr(file); // current pos (is already saved as ding => ignore)
	}
	pos_next.rdwr(file);

	if(file->is_saving()) {
		const char *s = desc->get_name();
		file->rdwr_str(s);
	}
	else {
		char s[256];
		file->rdwr_str(s, lengthof(s));
		desc = vehicle_builder_t::get_info(s);
		if(desc==NULL) {
			desc = vehicle_builder_t::get_info(translator::compatibility_name(s));
		}
		if(desc==NULL) {
			welt->add_missing_paks( s, karte_t::MISSING_VEHICLE );
			dbg->warning("vehicle_t::rdwr_from_convoi()","no vehicle pak for '%s' search for something similar", s);
		}
	}

	if(file->is_saving()) {
		if (fracht.empty()  &&  fracht_count>0) {
			// create dummy freight for savegame compatibility
			ware_t ware( desc->get_freight_type() );
			ware.menge = 0;
			ware.set_ziel( halthandle_t() );
			ware.set_zwischenziel( halthandle_t() );
			ware.set_zielpos( get_pos().get_2d() );
			ware.rdwr(file);
		}
		else {
			FOR(slist_tpl<ware_t>, ware, fracht) {
				ware.rdwr(file);
			}
		}
	}
	else {
		for(int i=0; i<fracht_count; i++) {
			ware_t ware(file);
			if(  (desc==NULL  ||  ware.menge>0)  &&  welt->is_within_limits(ware.get_zielpos())  &&  ware.get_desc()  ) {
				// also add, of the desc is unknown to find matching replacement
				fracht.append(ware);
#ifdef CACHE_TRANSIT
				if(  file->is_version_less(112, 1)  )
#endif
					// restore in-transit information
					fabrik_t::update_transit( &ware, true );
			}
			else if(  ware.menge>0  ) {
				if(  ware.get_desc()  ) {
					dbg->error( "vehicle_t::rdwr_from_convoi()", "%i of %s to %s ignored!", ware.menge, ware.get_name(), ware.get_zielpos().get_str() );
				}
				else {
					dbg->error( "vehicle_t::rdwr_from_convoi()", "%i of unknown to %s ignored!", ware.menge, ware.get_zielpos().get_str() );
				}
			}
		}
	}

	// skip first last info (the convoi will know this better than we!)
	if(file->is_version_less(88, 7)) {
		bool dummy = 0;
		file->rdwr_bool(dummy);
		file->rdwr_bool(dummy);
	}

	// koordinate of the last stop
	if(file->is_version_atleast(99, 15)) {
		// This used to be 2d, now it's 3d.
		if(file->is_version_less(112, 8)) {
			if(file->is_saving()) {
				koord last_stop_pos_2d = last_stop_pos.get_2d();
				last_stop_pos_2d.rdwr(file);
			}
			else {
				// loading.  Assume ground level stop (could be wrong, but how would we know?)
				koord last_stop_pos_2d = koord::invalid;
				last_stop_pos_2d.rdwr(file);
				const grund_t* gr = welt->lookup_kartenboden(last_stop_pos_2d);
				if (gr) {
					last_stop_pos = koord3d(last_stop_pos_2d, gr->get_hoehe());
				}
				else {
					// no ground?!?
					last_stop_pos = koord3d::invalid;
				}
			}
		}
		else {
			// current version, 3d
			last_stop_pos.rdwr(file);
		}
	}

	if(file->is_loading()) {
		leading = last = false; // dummy, will be set by convoi afterwards
		if(desc) {
			calc_image();

			// full weight after loading
			sum_weight =  get_cargo_weight() + desc->get_weight();
		}
		// recalc total freight
		total_freight = 0;
		FOR(slist_tpl<ware_t>, const& c, fracht) {
			total_freight += c.menge;
		}
	}

	if(  file->is_version_atleast(110, 0)  ) {
		bool hd = has_driven;
		file->rdwr_bool( hd );
		has_driven = hd;
	}
	else {
		if (file->is_loading()) {
			has_driven = false;
		}
	}
}


uint32 vehicle_t::calc_sale_value() const
{
	// if already used, there is a general price reduction
	double value = (double)desc->get_price();
	if(  has_driven  ) {
		value *= (1000 - welt->get_settings().get_used_vehicle_reduction()) / 1000.0;
	}
	// after 20 year, it has only half value
	return (uint32)( value * pow(0.997, (int)(welt->get_current_month() - get_purchase_time())));
}


void vehicle_t::show_info()
{
	if(  cnv != NULL  ) {
		cnv->open_info_window();
	} else {
		dbg->warning("vehicle_t::show_info()","cnv is null, can't open convoi window!");
	}
}


void vehicle_t::info(cbuffer_t & buf) const
{
	if(cnv) {
		cnv->info(buf);
	}
}


const char *vehicle_t::is_deletable(const player_t *)
{
	return "Fahrzeuge koennen so nicht entfernt werden";
}


vehicle_t::~vehicle_t()
{
	// remove vehicle's marker from the minimap
	minimap_t::get_instance()->calc_map_pixel(get_pos().get_2d());
}


#ifdef MULTI_THREAD
void vehicle_t::display_overlay(int xpos, int ypos) const
{
	if(  cnv  &&  leading  ) {
#else
void vehicle_t::display_after(int xpos, int ypos, bool is_global) const
{
	if(  is_global  &&  cnv  &&  leading  ) {
#endif
		PIXVAL color = 0; // not used, but stop compiler warning about uninitialized
		char tooltip_text[1024];
		tooltip_text[0] = 0;
		uint8 state = env_t::show_vehicle_states;
		if(  state==1  ||  state==2  ) {
			// only show when mouse over vehicle
			if(  welt->get_zeiger()->get_pos()==get_pos()  ) {
				state = 3;
			}
			else {
				state = 0;
			}
		}
		if( state != 3 ) {
			// nothing to show
			return;
		}

		// now find out what has happened
		switch(cnv->get_state()) {
			case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:
			case convoi_t::WAITING_FOR_CLEARANCE:
			case convoi_t::CAN_START:
			case convoi_t::CAN_START_ONE_MONTH:
				if(  state>=3  ) {
					// fork: a passing train or the single-track rules say more than "waiting"
					cbuffer_t reason;
					if(  cnv->append_wait_reason( reason )  ) {
						tstrncpy( tooltip_text, reason, lengthof(tooltip_text) );
					}
					else {
						snprintf( tooltip_text, lengthof(tooltip_text), "%s (%s)", translator::translate("Waiting for clearance!"), cnv->get_schedule()->get_current_entry().pos.get_str() );
					}
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;

			case convoi_t::LOADING:
				if(  state>=3  ) {
					cbuffer_t buf;
					buf.printf( translator::translate("Loading (%i->%i%%)!"), cnv->get_loading_level(), cnv->get_loading_limit() );
					// fork: coupling and timetable
					if(  cnv->is_waiting_for_coupling()  ) {
						buf.printf( " - %s", translator::translate("Waiting for the train to couple with") );
					}
					else if(  cnv->is_running_late()  ) {
						buf.printf( " - %s", translator::translate("Running late (missed coupling)") );
					}
					cbuffer_t departure;
					if(  cnv->append_departure_text( departure )  ) {
						buf.printf( " - %s", (const char *)departure );
					}
					tstrncpy( tooltip_text, buf, lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;

			case convoi_t::EDIT_SCHEDULE:
//			case convoi_t::ROUTING_1:
				if(  state>=3  ) {
					tstrncpy( tooltip_text, translator::translate("Schedule changing!"), lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;

			case convoi_t::DRIVING:
				if(  state>=3  ) {
					grund_t const* const gr = welt->lookup(cnv->get_route()->back());
					if(  gr  &&  gr->get_depot()  ) {
						tstrncpy( tooltip_text, translator::translate("go home"), lengthof(tooltip_text) );
						color = color_idx_to_rgb(COL_GREEN);
					}
					else if(  cnv->get_no_load()  ) {
						tstrncpy( tooltip_text, translator::translate("no load"), lengthof(tooltip_text) );
						color = color_idx_to_rgb(COL_GREEN);
					}
				}
				break;

			case convoi_t::LEAVING_DEPOT:
				if(  state>=2  ) {
					tstrncpy( tooltip_text, translator::translate("Leaving depot!"), lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_GREEN);
				}
				break;

			case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS:
			case convoi_t::CAN_START_TWO_MONTHS:
				{
					snprintf( tooltip_text, lengthof(tooltip_text), "%s (%s)", translator::translate("clf_chk_stucked"), cnv->get_schedule()->get_current_entry().pos.get_str() );
					// fork: and why, if the single-track rules know
					cbuffer_t reason;
					if(  cnv->append_wait_reason( reason )  ) {
						const size_t len = strlen( tooltip_text );
						snprintf( tooltip_text + len, lengthof(tooltip_text) - len, " - %s", (const char *)reason );
					}
					color = color_idx_to_rgb(COL_ORANGE);
				}
				break;

			case convoi_t::NO_ROUTE:
				tstrncpy( tooltip_text, translator::translate("clf_chk_noroute"), lengthof(tooltip_text) );
				color = color_idx_to_rgb(COL_RED);
				break;
		}

		if(  env_t::show_vehicle_states == 2  &&  !tooltip_text[ 0 ]  ) {
			// show line name or simply convoi name
			color = color_idx_to_rgb( cnv->get_owner()->get_player_color1() + 7 );
			if(  cnv->get_line().is_bound()  ) {
				snprintf( tooltip_text, lengthof( tooltip_text ), "%s - %s", cnv->get_line()->get_name(), cnv->get_name() );
			}
			else {
				snprintf( tooltip_text, lengthof( tooltip_text ), "%s", cnv->get_name() );
			}
		}

		// something to show?
		if(  tooltip_text[0]  ) {
			const int width = proportional_string_width(tooltip_text)+7;
			const int raster_width = get_current_tile_raster_width();
			get_screen_offset( xpos, ypos, raster_width );
			xpos += tile_raster_scale_x(get_xoff(), raster_width);
			ypos += tile_raster_scale_y(get_yoff(), raster_width)+14;
			if(ypos>LINESPACE+32  &&  ypos+LINESPACE<display_get_clip_wh().yy) {
				display_ddd_proportional_clip( xpos, ypos, width, 0, color, color_idx_to_rgb(COL_BLACK), tooltip_text, true );
			}
		}
	}
}



road_vehicle_t::road_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
	vehicle_t(pos, desc, player)
{
	cnv = cn;
	choose_pass_standing = false;
}


road_vehicle_t::road_vehicle_t(loadsave_t *file, bool is_first, bool is_last) : vehicle_t()
{
	choose_pass_standing = false;
	rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_first) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL) {
			const goods_desc_t* w = (!fracht.empty() ? fracht.front().get_desc() : goods_manager_t::passengers);
			dbg->warning("road_vehicle_t::road_vehicle_t()","try to find a fitting vehicle for %s.",  w->get_name() );
			desc = vehicle_builder_t::get_best_matching(road_wt, 0, (fracht.empty() ? 0 : 50), is_first?50:0, speed_to_kmh(speed_limit), w, true, last_desc, is_last );
			if(desc) {
				DBG_MESSAGE("road_vehicle_t::road_vehicle_t()","replaced by %s",desc->get_name());
				// still wrong load ...
				calc_image();
			}
			if(!fracht.empty()  &&  fracht.front().menge == 0) {
				// this was only there to find a matching vehicle
				fracht.remove_first();
			}
		}
		if(  desc  ) {
			last_desc = desc;
		}
		calc_disp_lane();
	}
}


void road_vehicle_t::rotate90()
{
	vehicle_t::rotate90();
	calc_disp_lane();
}


void road_vehicle_t::calc_disp_lane()
{
	// driving in the back or the front
	ribi_t::ribi test_dir = welt->get_settings().is_drive_left() ? ribi_t::northeast : ribi_t::southwest;
	disp_lane = get_direction() & test_dir ? 1 : 3;
}

// need to reset halt reservation (if there was one)
bool road_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, route_t* route)
{
	assert(cnv);
	// free target reservation
	if(leading   &&  previous_direction!=ribi_t::none  &&  cnv  &&  target_halt.is_bound() ) {
		// now reserve our choice (beware: might be longer than one tile!)
		for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
			target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
		}
	}
	target_halt = halthandle_t(); // no block reserved
	route_t::route_result_t r = route->calc_route(welt, start, ziel, this, max_speed, cnv->get_tile_length() );
	if(  r == route_t::valid_route_halt_too_short  ) {
		cbuffer_t buf;
		buf.printf( translator::translate("Vehicle %s cannot choose because stop too short!"), cnv->get_name());
		welt->get_message()->add_message( (const char *)buf, ziel.get_2d(), message_t::traffic_jams, PLAYER_FLAG | cnv->get_owner()->get_player_nr(), cnv->front()->get_base_image() );
	}
	return r;
}


bool road_vehicle_t::check_next_tile(const grund_t *bd) const
{
	strasse_t *str=(strasse_t *)bd->get_weg(road_wt);
	if(str==NULL  ||  str->get_max_speed()==0) {
		return false;
	}
	bool electric = cnv!=NULL  ?  cnv->needs_electrification() : desc->get_engine_type()==vehicle_desc_t::electric;
	if(electric  &&  !str->is_electrified()) {
		return false;
	}
	// check for signs
	if(str->has_sign()) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(rs!=NULL) {
			if(  rs->get_desc()->get_min_speed()>0  &&  rs->get_desc()->get_min_speed()>kmh_to_speed(get_desc()->get_topspeed())  ) {
				return false;
			}
			if(  rs->get_desc()->is_private_way()  &&  (rs->get_player_mask() & (1<<get_player_nr()) ) == 0  ) {
				// private road
				return false;
			}
			// do not search further for a free stop beyond here
			if(target_halt.is_bound()  &&  cnv->is_waiting()  &&  rs->get_desc()->get_flags()&roadsign_desc_t::END_OF_CHOOSE_AREA) {
				return false;
			}
		}
	}
	return true;
}


// how expensive to go here (for way search)
int road_vehicle_t::get_cost(const grund_t *gr, const weg_t *w, const sint32 max_speed, ribi_t::ribi from) const
{
	// first favor faster ways
	if(!w) {
		return 0xFFFF;
	}

	// max_speed?
	sint32 max_tile_speed = w->get_max_speed();

	// add cost for going (with maximum speed, cost is 1)
	int costs = (max_speed<=max_tile_speed) ? 1 : 4-(3*max_tile_speed)/max_speed;

	// assume all traffic is not good ... (otherwise even smoke counts ... )
	costs += (w->get_statistics(WAY_STAT_CONVOIS)  >  ( 2 << (welt->get_settings().get_bits_per_month()-16) )  );

	// effect of slope
	if(  gr->get_weg_hang()!=0  ) {
		// check if the slope is upwards, relative to the previous tile
		// 15 hardcoded, see get_cost_upslope()
		costs += 15 * get_sloping_upwards( gr->get_weg_hang(), from );
	}
	return costs;
}


// this routine is called by find_route, to determined if we reached a destination
bool road_vehicle_t::is_target(const grund_t *gr, const grund_t *prev_gr) const
{
	//  just check, if we reached a free stop position of this halt
	if(gr->is_halt()  &&  gr->get_halt()==target_halt  &&  target_halt->is_reservable(gr,cnv->self)) {
		// now we must check the predecessor => try to advance as much as possible
		if(prev_gr!=NULL) {
			const koord dir=gr->get_pos().get_2d()-prev_gr->get_pos().get_2d();
			ribi_t::ribi ribi = ribi_type(dir);
			if(  gr->get_weg(get_waytype())->get_ribi_maske() & ribi  ) {
				// one way sign wrong direction
				return false;
			}
			grund_t *to;
			if(  !gr->get_neighbour(to,road_wt,ribi)  ||  !(to->get_halt()==target_halt)  ||  (gr->get_weg(get_waytype())->get_ribi_maske() & ribi_type(dir))!=0  ||  !target_halt->is_reservable(to,cnv->self)  ) {
				grund_t *next;
				if(  choose_pass_standing  &&  gr->get_neighbour(next,road_wt,ribi)  &&  next->get_halt()==target_halt  &&  (gr->get_weg(get_waytype())->get_ribi_maske() & ribi)==0  ) {
					// a convoi standing on the next tile: we can pass it, so look for a position beyond it
					for(  uint8 i=1;  i<next->get_top();  i++  ) {
						if(  road_vehicle_t const* const other = obj_cast<road_vehicle_t>(next->obj_bei(i))  ) {
							if(  other->get_convoi()!=cnv  &&  other->get_convoi()->is_standing()  ) {
								return false;
							}
						}
					}
				}
				// end of stop: Is it long enough?
				uint16 tiles = cnv->get_tile_length();
				while(  tiles>1  ) {
					if(  !gr->get_neighbour(to,get_waytype(),ribi_t::backward(ribi))  ||  !(to->get_halt()==target_halt)  ||  !target_halt->is_reservable(to,cnv->self)  ) {
						return false;
					}
					gr = to;
					tiles --;
				}
				return true;
			}
			// can advance more
			return false;
		}
//DBG_MESSAGE("is_target()","success at %i,%i",gr->get_pos().x,gr->get_pos().y);
//		return true;
	}
	return false;
}


// to make smaller steps than the tile granularity, we have to use this trick
void road_vehicle_t::get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const
{
	vehicle_base_t::get_screen_offset( xoff, yoff, raster_width );

	if(  welt->get_settings().is_drive_left()  ) {
		const int drive_left_dir = ribi_t::get_dir(get_direction());
		xoff += tile_raster_scale_x( driveleft_base_offsets[drive_left_dir][0], raster_width );
		yoff += tile_raster_scale_y( driveleft_base_offsets[drive_left_dir][1], raster_width );
	}

	// eventually shift position to take care of overtaking
	if(cnv) {
		if(  cnv->is_overtaking()  ) {
			xoff += tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][0], raster_width);
			yoff += tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][1], raster_width);
		}
		else if(  cnv->is_overtaken()  ) {
			xoff -= tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][0], raster_width)/5;
			yoff -= tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][1], raster_width)/5;
		}
	}
}


// chooses a route at a choose sign; returns true on success
bool road_vehicle_t::choose_route(sint32 &restart_speed, ribi_t::ribi start_direction, uint16 index)
{
	if(  cnv->get_schedule_target()!=koord3d::invalid  ) {
		// destination is a waypoint!
		return true;
	}

	// are we heading to a target?
	route_t *rt = cnv->access_route();
	target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
	if(  target_halt.is_bound()  ) {

		// since convois can long than one tile, check is more difficult
		bool can_go_there = true;
		bool original_route = (rt->back() == cnv->get_schedule()->get_current_entry().pos);
		for(  uint32 length=0;  can_go_there  &&  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
			if(  grund_t *gr = welt->lookup( rt->at( rt->get_count()-length-1) )  ) {
				if (gr->get_halt().is_bound()) {
					can_go_there &= target_halt->is_reservable( gr, cnv->self );
				}
				else {
					// if this is the original stop, it is too short!
					can_go_there |= original_route;
				}
			}
		}
		if(  can_go_there  ) {
			// then reserve it ...
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( rt->at( rt->get_count()-length-1) ), cnv->self );
			}
		}
		else {
			// cannot go there => need slot search

			// if we fail, we will wait in a step, much more simulation friendly
			if(!cnv->is_waiting()) {
				restart_speed = -1;
				target_halt = halthandle_t();
				return false;
			}

			// check if there is a free position
			// this is much faster than waysearch
			if(  !target_halt->find_free_position(road_wt,cnv->self,obj_t::road_vehicle  )) {
				restart_speed = 0;
				target_halt = halthandle_t();
				return false;
			}

			// now it make sense to search a route
			route_t target_rt;
			koord3d next3d = rt->at(index);
			// first look for a position beyond standing convois (we pass them), then take the first free one
			choose_pass_standing = true;
			bool found = target_rt.find_route( welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), start_direction, welt->get_settings().get_max_choose_route_steps() );
			choose_pass_standing = false;
			if(  !found  ) {
				found = target_rt.find_route( welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), start_direction, welt->get_settings().get_max_choose_route_steps() );
			}
			if(  !found  ) {
				// nothing empty or not route with less than 33 tiles
				target_halt = halthandle_t();
				restart_speed = 0;
				return false;
			}

			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<target_rt.get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( target_rt.at( target_rt.get_count()-length-1) ), cnv->self );
			}
			rt->remove_koord_from( index );
			rt->append( &target_rt );
		}
	}
	return true;
}


bool road_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8 second_check_count)
{
	// check for traffic lights (only relevant for the first car in a convoi)
	if(  leading  ) {
		if(  !second_check_count  ) {
			// fork: set again below if a vehicle is in our way (deadlock warning)
			cnv->set_blocked_by( NULL, koord3d::invalid );
		}
		// no further check, when already entered a crossing (to allow leaving it)
		if(  !second_check_count  ) {
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  gr_current  &&  gr_current->ist_uebergang()  ) {
					return true;
				}
			}
			// always allow to leave traffic lights (avoid vehicles stuck on crossings directly after though)
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  const roadsign_t *rs = gr_current->find<roadsign_t>()  ) {
					if(  rs  &&  rs->get_desc()->is_traffic_light()  &&  !gr->ist_uebergang()  ) {
						return true;
					}
				}
			}
		}

		assert(gr);

		const strasse_t *str = (strasse_t *)gr->get_weg(road_wt);
		if(  !str  ||  gr->get_top() > 250  ) {
			// too many cars here or no street
			return false;
		}

		// first: check roadsigns
		const roadsign_t *rs = NULL;
		if(  str->has_sign()  ) {
			rs = gr->find<roadsign_t>();
			route_t const& r = *cnv->get_route();

			if(  rs  &&  (route_index + 1u < r.get_count())  ) {
				// since at the corner, our direction may be diagonal, we make it straight
				uint8 direction90 = ribi_type(get_pos(), pos_next);

				if(  rs->get_desc()->is_traffic_light()  &&  (rs->get_dir()&direction90) == 0  ) {
					// wait here
					restart_speed = 16;
					return false;
				}
				// check, if we reached a choose point
				else {
					// route position after road sign
					const koord pos_next_next = r.at(route_index + 1u).get_2d();
					// since at the corner, our direction may be diagonal, we make it straight
					direction90 = ribi_type( pos_next, pos_next_next );

					if(  rs->is_free_route(direction90)  &&  !target_halt.is_bound()  ) {
						if(  second_check_count  ) {
							return false;
						}
						if(  !choose_route( restart_speed, direction90, route_index )  ) {
							return false;
						}
					}
				}
			}
		}

		vehicle_base_t *obj = NULL;
		uint32 test_index = route_index + 1u;

		// way should be clear for overtaking: we checked previously
		// (except the tile after a passed standing convoi, which may be a junction or crossing)
		if(  !cnv->is_overtaking()  ||  cnv->is_passing_standing_last_tile()  ) {
			// calculate new direction
			route_t const& r = *cnv->get_route();
			koord3d next = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
			ribi_t::ribi curr_direction   = get_direction();
			ribi_t::ribi curr_90direction = calc_direction(get_pos(), pos_next);
			ribi_t::ribi next_direction   = calc_direction(get_pos(), next);
			ribi_t::ribi next_90direction = calc_direction(pos_next, next);
			obj = no_cars_blocking( gr, cnv, curr_direction, next_direction, next_90direction );

			// do not block intersections
			const bool drives_on_left = welt->get_settings().is_drive_left();
			bool int_block = ribi_t::is_threeway(str->get_ribi_unmasked())  &&  (((drives_on_left ? ribi_t::rotate90l(curr_90direction) : ribi_t::rotate90(curr_90direction)) & str->get_ribi_unmasked())  ||  curr_90direction != next_90direction  ||  (rs  &&  rs->get_desc()->is_traffic_light()));

			// check exit from crossings and intersections, allow to proceed after 4 consecutive
			while(  !obj   &&  (str->is_crossing()  ||  int_block)  &&  test_index < r.get_count()  &&  test_index < route_index + 4u  ) {
				if(  str->is_crossing()  ) {
					crossing_t* cr = gr->find<crossing_t>(2);
					if(  !cr->request_crossing(this)  ) {
						restart_speed = 0;
						return false;
					}
				}

				// test next position
				gr = welt->lookup(r.at(test_index));
				if(  !gr  ) {
					// way (weg) not existent (likely destroyed)
					if(  !second_check_count  ) {
						cnv->suche_neue_route();
					}
					return false;
				}

				str = (strasse_t *)gr->get_weg(road_wt);
				if(  !str  ||  gr->get_top() > 250  ) {
					// too many cars here or no street
					if(  !second_check_count  &&  !str) {
						cnv->suche_neue_route();
					}
					return false;
				}

				// check cars
				curr_direction   = next_direction;
				curr_90direction = next_90direction;
				if(  test_index + 1u < r.get_count()  ) {
					next                 = r.at(test_index + 1u);
					next_direction   = calc_direction(r.at(test_index - 1u), next);
					next_90direction = calc_direction(r.at(test_index),      next);
					obj = no_cars_blocking( gr, cnv, curr_direction, next_direction, next_90direction );
				}
				else {
					next                 = r.at(test_index);
					next_90direction = calc_direction(r.at(test_index - 1u), next);
					if(  curr_direction == next_90direction  ||  !gr->is_halt()  ) {
						// check cars but allow to enter intersection if we are turning even when a car is blocking the halt on the last tile of our route
						// preserves old bus terminal behaviour
						obj = no_cars_blocking( gr, cnv, curr_direction, next_90direction, ribi_t::none );
					}
				}

				// check roadsigns
				if(  str->has_sign()  ) {
					rs = gr->find<roadsign_t>();
					if(  rs  ) {
						// check, if we reached a choose point
						if(  rs->is_free_route(curr_90direction)  &&  !target_halt.is_bound()  ) {
							if(  second_check_count  ) {
								return false;
							}
							if(  !choose_route( restart_speed, curr_90direction, test_index )  ) {
								return false;
							}
						}
					}
				}
				else {
					rs = NULL;
				}

				// check for blocking intersection
				int_block = ribi_t::is_threeway(str->get_ribi_unmasked())  &&  (((drives_on_left ? ribi_t::rotate90l(curr_90direction) : ribi_t::rotate90(curr_90direction)) & str->get_ribi_unmasked())  ||  curr_90direction != next_90direction  ||  (rs  &&  rs->get_desc()->is_traffic_light()));

				test_index++;
			}

			if(  obj  &&  test_index > route_index + 1u  &&  !str->is_crossing()  &&  !int_block  ) {
				// found a car blocking us after checking at least 1 intersection or crossing
				// and the car is in a place we could stop. So if it can move, assume it will, so we will too.
				// but check only upto 8 cars ahead to prevent infinite recursion on roundabouts.
				if(  second_check_count >= 8  ) {
					return false;
				}
				if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
					const convoi_t* const ocnv = car->get_convoi();
					// a convoi standing after the intersection: go on, if we can pass it there
					if(  ocnv->is_standing()  &&  ocnv->can_be_overtaken()  &&  cnv->get_tiles_to_pass_standing( ocnv, test_index - 1u ) > 0  ) {
						return true;
					}
					sint32 dummy;
					if(  ocnv->front()->get_route_index() < ocnv->get_route()->get_count()  &&  ocnv->front()->can_enter_tile( dummy, second_check_count + 1 )  ) {
						return true;
					}
				}
			}
		}

		// stuck message ...
		if(  obj  &&  !second_check_count  ) {
			cnv->set_blocked_by( obj, obj->get_pos() );
			if(  obj->is_stuck()  ) {
				// end of traffic jam, but no stuck message, because previous vehicle is stuck too
				restart_speed = 0;
				cnv->set_tiles_overtaking(0);
				cnv->reset_waiting();
			}
			else {
				if(  test_index == route_index + 1u  ) {
					// no intersections or crossings, we might be able to overtake this one ...
					overtaker_t *over = obj->get_overtaker();
					if(  over  &&  !over->is_overtaken()  ) {
						if(  over->is_overtaking()  ) {
							// otherwise we would stop every time being overtaken
							return true;
						}
						// not overtaking/being overtake: we need to make a more thought test!
						if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
							convoi_t* const ocnv = car->get_convoi();
							if(  cnv->can_overtake( ocnv, (ocnv->is_standing() ? 0 : over->get_max_power_speed()), ocnv->get_length_in_steps()+ocnv->get_vehikel(0)->get_steps())  ) {
								return true;
							}
						}
						else if(  private_car_t* const caut = obj_cast<private_car_t>(obj)  ) {
							if(  cnv->can_overtake(caut, caut->get_desc()->get_topspeed(), VEHICLE_STEPS_PER_TILE)  ) {
								return true;
							}
						}
					}
				}
				// we have to wait ...
				restart_speed = (cnv->get_akt_speed()*3)/4;
				cnv->set_tiles_overtaking(0);
			}
		}

		return obj==NULL;
	}

	return true;
}


overtaker_t* road_vehicle_t::get_overtaker()
{
	return cnv;
}


void road_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);
	calc_disp_lane();

	const int cargo = get_total_cargo();
	weg_t *str = gr->get_weg(road_wt);
	if (str) {
		str->book(cargo, WAY_STAT_GOODS);
		if (leading)  {
			str->book(1, WAY_STAT_CONVOIS);
		}
	}
	if (leading)  {
		cnv->update_tiles_overtaking();
	}
}


schedule_t * road_vehicle_t::generate_new_schedule() const
{
	return new truck_schedule_t();
}


void road_vehicle_t::set_convoi(convoi_t *c)
{
	DBG_MESSAGE("road_vehicle_t::set_convoi()","%p",c);
	if(c!=NULL) {
		bool target=(bool)cnv; // only during loadtype: cnv==1 indicates, that the convoi did reserve a stop
		vehicle_t::set_convoi(c);
		if(target  &&  leading  &&  c->get_route()->empty()) {
			// reinitialize the target halt
			const route_t *rt = cnv->get_route();
			target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
			if(  target_halt.is_bound()  ) {
				for(  uint32 i=0;  i<c->get_tile_length()  &&  i+1<rt->get_count();  i++  ) {
					target_halt->reserve_position( welt->lookup( rt->at(rt->get_count()-i-1) ), cnv->self );
				}
			}
		}
	}
	else {
		if(  cnv  &&  leading  &&  target_halt.is_bound()  ) {
			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
				target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
			}
			target_halt = halthandle_t();
		}
		cnv = NULL;
	}
}


/* from now on rail vehicles (and other vehicles using blocks) */
// platforms tried by reserve_hold_platform that do not lead on to the end of choose
static vector_tpl<koord3d> hold_platform_excluded;

// fork: tracks already found not to lead on during find_station_track
static vector_tpl<koord3d> track_search_excluded;

// fork: bay platform tiles of the halt of the current stop search (see rail_vehicle_t::bay_search)
static vector_tpl<koord3d> bay_search_tiles;

// fork: at a choose signal, the ways of the trains that will pass us at our stop, and their routes on
// from there (see get_overtaker_ways)
static vector_tpl<koord3d> overtaker_ways;
static vector_tpl<koord3d> overtaker_routes;
// tiles walked at most: of their routes on, and to tell a loop from a line (leads_back_to_overtaker)
#define OVERTAKER_WALK_TILES (128)

// fork: the train would stand on one of these tiles at the end of rt (the last tiles of its length)
static bool stands_on_any(const route_t &rt, uint16 tiles, const vector_tpl<koord3d> &list)
{
	for(  uint32 idx=rt.get_count();  idx>0  &&  tiles>0;  idx--, tiles--  ) {
		if(  list.is_contained( rt.at(idx-1) )  ) {
			return true;
		}
	}
	return false;
}

// fork: stop positions of platforms a stop search found and keeps as candidates: no target any more,
// but the search may still run through them (unlike track_search_excluded)
static vector_tpl<koord3d> track_search_taken;

/* fork: the stop searches take the shortest platform that fits (get_found_platform_length), so the
 * long ones stay free for long trains; fit is the length the train needs there (with a coupling
 * partner, both). Smaller is better: platforms that fit by length, then those too short, longest first
 */
static uint16 get_platform_rank(uint16 length, uint16 fit)
{
	return length>=fit ? length : 0x8000 - length;
}

/* fork: a bay platform. The track of this platform tile ends in a buffer stop (or a depot) on one side
 * before any switch or station boundary, at most 32 tiles past the tiles of its halt; only a train
 * that turns back there can use it
 */
static bool is_bay_tile(const grund_t *gr, waytype_t wt)
{
	weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
	if(  way==NULL  ) {
		return false;
	}
	const ribi_t::ribi ribi = way->get_ribi_unmasked();
	if(  ribi_t::is_single( ribi )  ) {
		return true;
	}
	if(  !ribi_t::is_twoway( ribi )  ) {
		// a switch
		return false;
	}
	const halthandle_t halt = gr->get_halt();
	for(  int r=0;  r<4;  r++  ) {
		if(  (ribi & ribi_t::nsew[r])==0  ) {
			continue;
		}
		const grund_t *at = gr;
		ribi_t::ribi dir = ribi_t::nsew[r];
		uint8 off = 0; // tiles past the halt
		for(  uint16 steps=0;  steps<1024;  steps++  ) {
			grund_t *to;
			if(  !at->get_neighbour( to, wt, dir )  ) {
				break;
			}
			weg_t const* const to_way = to->get_weg( wt );
			if(  to_way==NULL  ||  rail_vehicle_t::get_station_boundary( to )  ) {
				break;
			}
			const ribi_t::ribi on = to_way->get_ribi_unmasked() & ~ribi_t::reverse_single( dir );
			if(  on==ribi_t::none  ) {
				// buffer stop
				return true;
			}
			if(  !ribi_t::is_single( on )  ) {
				// a switch
				break;
			}
			if(  to->get_halt()!=halt  ) {
				if(  ++off > 32  ) {
					break;
				}
			}
			else {
				off = 0;
			}
			at = to;
			dir = on;
		}
	}
	return false;
}


// fork: the bay platform tiles of this halt; has_through: it has a platform that is no bay
static void get_bay_tiles(halthandle_t halt, waytype_t wt, vector_tpl<koord3d> &bays, bool &has_through)
{
	bays.clear();
	has_through = false;
	if(  !halt.is_bound()  ) {
		return;
	}
	FOR( slist_tpl<haltestelle_t::tile_t>, const &tile, halt->get_tiles() ) {
		if(  tile.grund->get_weg( wt )==NULL  ) {
			continue;
		}
		gebaeude_t const* const gb = tile.grund->find<gebaeude_t>();
		if(  gb  &&  gb->get_tile()->get_desc()->get_extra()!=(uint32)wt  ) {
			// a tram or road stop of the same halt on tram track
			continue;
		}
		if(  is_bay_tile( tile.grund, wt )  ) {
			bays.append_unique( tile.grund->get_pos() );
		}
		else {
			has_through = true;
		}
	}
}

// fork: the path (after its first tile) runs over a platform of halt for waytype wt, not counting the
// platform it ends on (a next stop in the same station, e.g. the departure platform of a terminus)
static bool passes_platform_of(const route_t &path, halthandle_t halt, waytype_t wt)
{
	uint32 end = path.get_count();
	while(  end>1  ) {
		grund_t const* const gr = world()->lookup( path.at(end-1) );
		if(  gr==NULL  ||  gr->get_halt()!=halt  ) {
			break;
		}
		end--;
	}
	for(  uint32 i=1;  i<end;  i++  ) {
		grund_t const* const gr = world()->lookup( path.at(i) );
		if(  gr  &&  gr->get_halt()==halt  ) {
			gebaeude_t const* const gb = gr->find<gebaeude_t>();
			if(  gb  &&  gb->get_tile()->get_desc()->get_extra()==(uint32)wt  ) {
				return true;
			}
		}
	}
	return false;
}

/* fork: the path (from index from on) runs through a platform of halt the other way before it stops there:
 * the train would pass its station, turn round beyond it and come back in (into a bay facing away from
 * it, or onto a track from the far side). Station platforms are straight, so the way is compared per tile.
 */
static bool turns_round_through(const route_t &path, uint32 from, halthandle_t halt)
{
	const uint32 n = path.get_count();
	if(  !halt.is_bound()  ||  n<2  ||  from+1>=n  ) {
		return false;
	}
	const ribi_t::ribi back = ribi_t::backward( ribi_type( path.at(n-2), path.at(n-1) ) );
	for(  uint32 i=from+1;  i<n;  i++  ) {
		grund_t const* const gr = world()->lookup( path.at(i) );
		if(  gr  &&  gr->get_halt()==halt  &&  ribi_type( path.at(i-1), path.at(i) )==back  ) {
			return true;
		}
	}
	return false;
}


rail_vehicle_t::rail_vehicle_t(loadsave_t *file, bool is_first, bool is_last) : vehicle_t()
{
	detour_start = detour_target = detour_exit = koord3d::invalid;
	platform_needs = 0;
	bay_search = 0;
	turn_probe = false;
	stop_any_length = false;
	hold_search = 0;
	hold_avoid_from = hold_avoid_to = 0;
	detour_any_track = false;
	track_search = 0;
	track_search_block = false;
	track_search_start = koord3d::invalid;
	couple_goal = koord3d::invalid;
	stop_search_start = koord3d::invalid;
	couple_in_station = false;
	section_after_pos = koord3d::invalid;
	section_after_stop = 0;
	section_after_found = false;
	section_after_tick = 0;
	vehicle_t::rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_first) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL) {
			int power = (is_first || fracht.empty() || fracht.front() == goods_manager_t::none) ? 500 : 0;
			const goods_desc_t* w = fracht.empty() ? goods_manager_t::none : fracht.front().get_desc();
			dbg->warning("rail_vehicle_t::rail_vehicle_t()","try to find a fitting vehicle for %s.", power>0 ? "engine": w->get_name() );
			if(last_desc!=NULL  &&  last_desc->can_follow(last_desc)  &&  last_desc->get_freight_type()==w  &&  (!is_last  ||  last_desc->get_trailer(0)==NULL)) {
				// same as previously ...
				desc = last_desc;
			}
			else {
				// we have to search
				desc = vehicle_builder_t::get_best_matching(get_waytype(), 0, w!=goods_manager_t::none?5000:0, power, speed_to_kmh(speed_limit), w, false, last_desc, is_last );
			}
			if(desc) {
DBG_MESSAGE("rail_vehicle_t::rail_vehicle_t()","replaced by %s",desc->get_name());
				calc_image();
			}
			else {
				dbg->error("rail_vehicle_t::rail_vehicle_t()","no matching desc found for %s!",w->get_name());
			}
			if (!fracht.empty() && fracht.front().menge == 0) {
				// this was only there to find a matching vehicle
				fracht.remove_first();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
}


rail_vehicle_t::rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
	vehicle_t(pos, desc, player)
{
	cnv = cn;
	detour_start = detour_target = detour_exit = koord3d::invalid;
	platform_needs = 0;
	bay_search = 0;
	turn_probe = false;
	stop_any_length = false;
	hold_search = 0;
	hold_avoid_from = hold_avoid_to = 0;
	detour_any_track = false;
	track_search = 0;
	track_search_block = false;
	track_search_start = koord3d::invalid;
	couple_goal = koord3d::invalid;
	stop_search_start = koord3d::invalid;
	couple_in_station = false;
	section_after_pos = koord3d::invalid;
	section_after_stop = 0;
	section_after_found = false;
	section_after_tick = 0;
}


rail_vehicle_t::~rail_vehicle_t()
{
	if (cnv && leading) {
		route_t const& r = *cnv->get_route();
		if (!r.empty() && route_index < r.get_count()) {
			// free all reserved blocks
			uint16 dummy;
			block_reserver(&r, cnv->back()->get_route_index(), dummy, dummy, target_halt.is_bound() ? 100000 : 1, false, false);
		}
	}
	grund_t *gr = welt->lookup(get_pos());
	if(gr) {
		schiene_t * sch = (schiene_t *)gr->get_weg(get_waytype());
		if(sch) {
			sch->unreserve(this);
		}
	}
}


void rail_vehicle_t::set_convoi(convoi_t *c)
{
	if(c!=cnv) {
		DBG_MESSAGE("rail_vehicle_t::set_convoi()","new=%p old=%p",c,cnv);
		if(leading) {
			if(cnv!=NULL  &&  cnv!=(convoi_t *)1) {
				// free route from old convoi
				route_t const& r = *cnv->get_route();
				if(  !r.empty()  &&  route_index + 1U < r.get_count() - 1  ) {
					uint16 dummy;
					block_reserver(&r, cnv->back()->get_route_index(), dummy, dummy, 100000, false, false);
					target_halt = halthandle_t();
				}
			}
			else if(  c->get_next_reservation_index()==0  ) {
				assert(c!=NULL);
				// eventually search new route
				route_t const& r = *c->get_route();
				if(  (r.get_count()<=route_index  ||  r.empty()  ||  get_pos()==r.back())  &&  c->get_state()!=convoi_t::INITIAL  &&  c->get_state()!=convoi_t::LOADING  &&  c->get_state()!=convoi_t::SELF_DESTRUCT  ) {
					check_for_finish = true;
					dbg->warning("rail_vehicle_t::set_convoi()", "convoi %i had a too high route index! (%i of max %i)", c->self.get_id(), route_index, r.get_count() - 1);
				}
				// set default next stop index
				c->set_next_stop_index( max(route_index,1)-1 );
				// need to reserve new route?
				if(  !check_for_finish  &&  c->get_state()!=convoi_t::SELF_DESTRUCT  &&  (c->get_state()==convoi_t::DRIVING  ||  c->get_state()>=convoi_t::LEAVING_DEPOT)  ) {
					sint32 num_index = cnv==(convoi_t *)1 ? 1001 : 0; // only during loadtype: cnv==1 indicates, that the convoi did reserve a stop
					uint16 next_signal, next_crossing;
					cnv = c;
					if(  block_reserver(&r, max(route_index,1)-1, next_signal, next_crossing, num_index, true, false)  ) {
						c->set_next_stop_index( next_signal>next_crossing ? next_crossing : next_signal );
					}
				}
			}
		}
		vehicle_t::set_convoi(c);
	}
}


// need to reset halt reservation (if there was one)
bool rail_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, route_t* route)
{
	if(leading  &&  route_index<cnv->get_route()->get_count()) {
		// free all reserved blocks
		uint16 dummy;
		block_reserver(cnv->get_route(), cnv->back()->get_route_index(), dummy, dummy, target_halt.is_bound() ? 100000 : 1, false, true);
	}
	cnv->set_next_reservation_index( 0 ); // nothing to reserve
	target_halt = halthandle_t(); // no block reserved
	// use length 8888 tiles to advance to the end of all stations
	return route->calc_route(welt, start, ziel, this, max_speed, 8888 /*cnv->get_tile_length()*/ );
}


bool rail_vehicle_t::check_next_tile(const grund_t *bd) const
{
	schiene_t const* const sch = obj_cast<schiene_t>(bd->get_weg(get_waytype()));
	if(  !sch  ) {
		return false;
	}

	// diesel and steam engines can use electrified track as well.
	// also allow driving on foreign tracks ...
	const bool needs_no_electric = !(cnv!=NULL ? cnv->needs_electrification() : desc->get_engine_type()==vehicle_desc_t::electric);
	if(  (!needs_no_electric  &&  !sch->is_electrified())  ||  sch->get_max_speed() == 0  ) {
		return false;
	}

	if (depot_t *depot = bd->get_depot()) {
		if (depot->get_waytype() != desc->get_waytype()  ||  depot->get_owner() != get_owner()) {
			return false;
		}
	}
	// now check for special signs
	if(sch->has_sign()) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(  rs->get_desc()->get_wtyp()==get_waytype()  ) {
			// fork, mixed traction: the engines that pull on this tile decide
			if(  cnv != NULL  &&  rs->get_desc()->get_min_speed() > 0  &&  rs->get_desc()->get_min_speed() > cnv->get_traction_top_speed( sch->is_electrified() )  ) {
				// below speed limit
				return false;
			}
			if(  rs->get_desc()->is_private_way()  &&  (rs->get_player_mask() & (1<<get_player_nr()) ) == 0  ) {
				// private road
				return false;
			}
		}
	}

	if(  track_search  ) {
		// fork: searching a track in a station (find_station_track), or the way on from it (any track)
		if(  track_search==3  ||  bd->get_pos()==track_search_start  ) {
			return true;
		}
		if(  track_search_block  ||  track_search_excluded.is_contained( bd->get_pos() )  ) {
			return false;
		}
		return turn_probe  ||  sch->can_reserve( cnv->self );
	}

	if(  detour_target!=koord3d::invalid  ||  hold_search  ) {
		// we are searching a way through a choose area (reserve_choose_detour) or a platform in it (reserve_hold_platform):
		if(  bd->get_pos()==detour_start  ) {
			return true;
		}
		// we cannot pass an end of choose area, only reach the one on our route
		if(  sch->has_sign()  &&  (hold_search  ||  bd->get_pos()!=detour_target)  ) {
			const roadsign_t* rs = bd->find<roadsign_t>();
			if(  rs  &&  rs->get_desc()->get_wtyp()==get_waytype()  &&  (rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA)  ) {
				return false;
			}
		}
		// only free track, not even our own
		return detour_any_track  ||  !sch->is_reserved();
	}

	if(  couple_search.is_bound()  ) {
		// fork, coupling: the way to our partner, over any track up to the end of choose
		if(  couple_in_station  &&  track_search_block  ) {
			// not on past a signal or station boundary
			return false;
		}
		if(  sch->has_sign()  ) {
			const roadsign_t* rs = bd->find<roadsign_t>();
			if(  rs  &&  rs->get_desc()->get_wtyp()==get_waytype()  &&  (rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA)  ) {
				return false;
			}
		}
		return true;
	}

	if(  target_halt.is_bound()  &&  cnv->is_waiting()  ) {
		// we are searching a stop here:
		// ok, we can go where we already are ...
		if(bd->get_pos()==get_pos()) {
			return true;
		}
		// we cannot pass an end of choose area
		if(sch->has_sign()) {
			const roadsign_t* rs = bd->find<roadsign_t>();
			if(  rs->get_desc()->get_wtyp()==get_waytype()  ) {
				if(  rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA  ) {
					return false;
				}
			}
		}
		// but we can only use empty blocks ...
		// now check, if we could enter here (fork: any block while probing the way in)
		return turn_probe  ||  sch->can_reserve(cnv->self);
	}

	return true;
}


// how expensive to go here (for way search)
int rail_vehicle_t::get_cost(const grund_t *gr, const weg_t *w, const sint32 max_speed, ribi_t::ribi from) const
{
	// first favor faster ways
	if(  w==NULL  ) {
		// only occurs when deletion during way search
		return 999;
	}

	// add cost for going (with maximum speed, cost is 1)
	const sint32 max_tile_speed = w->get_max_speed();
	int costs = (max_speed<=max_tile_speed) ? 1 : 4-(3*max_tile_speed)/max_speed;

	// effect of slope
	if(  gr->get_weg_hang()!=0  ) {
		// check if the slope is upwards, relative to the previous tile
		// 25 hardcoded, see get_cost_upslope()
		costs += 25 * get_sloping_upwards( gr->get_weg_hang(), from );
	}

	return costs;
}


// fork: during a stop search at stop_search_halt, never on from a platform of it to a tile that is not
// (its far end; the way in is behind the search anyway)
ribi_t::ribi rail_vehicle_t::get_ribi(const grund_t *gr) const
{
	ribi_t::ribi ribi = gr->get_weg_ribi( get_waytype() );
	if(  stop_search_halt.is_bound()  &&  gr->get_halt()==stop_search_halt  &&  gr->get_pos()!=stop_search_start  ) {
		for(  int r=0;  r<4;  r++  ) {
			grund_t *to;
			if(  (ribi & ribi_t::nsew[r])  &&  gr->get_neighbour( to, get_waytype(), ribi_t::nsew[r] )  &&  to->get_halt()!=stop_search_halt  ) {
				ribi &= ~ribi_t::nsew[r];
			}
		}
	}
	return ribi;
}


// fork: the direction a train leaves gr in, coming from prev_gr: the other end of the way (signals never
// stand on a switch; on one, e.g. a station boundary from a dat without is_single_way, the direction it
// came in). roadsign_t::applies_to takes this exit direction; on a curve it differs from the entry
static ribi_t::ribi get_exit_dir(const grund_t *gr, const grund_t *prev_gr, waytype_t wt)
{
	const ribi_t::ribi in = ribi_type( prev_gr->get_pos(), gr->get_pos() );
	weg_t const* const way = gr->get_weg( wt );
	const ribi_t::ribi out = way ? way->get_ribi_unmasked() & ~ribi_t::backward( in ) : ribi_t::none;
	return ribi_t::is_single( out ) ? out : in;
}


// this routine is called by find_route, to determined if we reached a destination
bool rail_vehicle_t::is_target(const grund_t *gr,const grund_t *prev_gr) const
{
	const schiene_t * sch1 = (const schiene_t *) gr->get_weg(get_waytype());
	if(  track_search==1  ||  track_search==2  ) {
		// fork: a track in a station (find_station_track); never search on past a signal that applies
		track_search_block = false;
		if(  prev_gr==NULL  ||  gr->get_pos()==track_search_start  ) {
			return false;
		}
		const ribi_t::ribi dir = get_exit_dir( gr, prev_gr, get_waytype() );
		if(  track_search==1  &&  !track_search_taken.is_contained( gr->get_pos() )  &&  is_stop_position( gr, prev_gr, track_search_halt )  ) {
			return true;
		}
		const bool signal = signal_applies( gr, dir );
		if(  track_search==2  &&  signal  ) {
			return true;
		}
		const roadsign_t *lt = get_station_boundary( gr );
		track_search_block = signal  ||  (lt  &&  lt->applies_to( dir ));
		return false;
	}
	if(  couple_search.is_bound()  ) {
		// fork, coupling: a tile of our partner standing there, or the end of its route into the stop
		if(  couple_goal!=koord3d::invalid ? gr->get_pos()==couple_goal : sch1->get_reserved_convoi()==couple_search  ) {
			return true;
		}
		if(  couple_in_station  ) {
			// in a station (find_partner_track): never search on past a signal or boundary that applies
			track_search_block = false;
			if(  prev_gr  ) {
				const ribi_t::ribi dir = get_exit_dir( gr, prev_gr, get_waytype() );
				const roadsign_t *lt = get_station_boundary( gr );
				track_search_block = signal_applies( gr, dir )  ||  (lt  &&  lt->applies_to( dir ));
			}
		}
		return false;
	}
	if(  detour_target!=koord3d::invalid  ) {
		// way through a choose area: back on the planned route at the end of choose tile,
		// driving on in the planned direction (check_next_tile made sure it is free)
		if(  gr->get_pos()!=detour_target  ||  prev_gr==NULL  ||  prev_gr->get_pos()==detour_exit  ) {
			return false;
		}
		const ribi_t::ribi ribi = ribi_type( prev_gr->get_pos(), gr->get_pos() );
		return (sch1->get_ribi_maske() & ribi)==0;
	}
	if(  hold_search  ) {
		// platform to let a passing train by: any of our stops (check_next_tile made sure the way is free)
		const halthandle_t halt = haltestelle_t::get_halt( gr->get_pos(), get_owner() );
		if(  !gr->is_halt()  ||  !halt.is_bound()  ) {
			return false;
		}
		if(  hold_platform_excluded.is_contained( gr->get_pos() )  ) {
			// no way on to the end of choose from there (see reserve_hold_platform)
			return false;
		}
		if(  hold_search==1  ) {
			// first try to keep off the planned way, the passing train will want it
			route_t const* const route = cnv->get_route();
			for(  uint32 i=hold_avoid_from;  i<=hold_avoid_to  &&  i<route->get_count();  i++  ) {
				if(  route->at(i)==gr->get_pos()  ) {
					return false;
				}
			}
		}
		return is_stop_position( gr, prev_gr, halt );
	}
	// first check blocks, if we can go there (fork: not a platform found not to lead on)
	if(  (turn_probe  ||  sch1->can_reserve(cnv->self))  &&  !track_search_excluded.is_contained( gr->get_pos() )  &&  !track_search_taken.is_contained( gr->get_pos() )  ) {
		//  just check, if we reached a free stop position of this halt
		return is_stop_position( gr, prev_gr, target_halt );
	}
	return false;
}


uint16 rail_vehicle_t::get_found_platform_length(const route_t &rt) const
{
	const uint32 count = rt.get_count();
	const grund_t *gr = welt->lookup( rt.back() );
	if(  count<2  ||  gr==NULL  ||  !gr->is_halt()  ) {
		return 0;
	}
	const waytype_t wt = get_waytype();
	const halthandle_t halt = gr->get_halt();
	const ribi_t::ribi back = ribi_type( rt.back(), rt.at(count-2) );
	const ribi_t::ribi forward = ribi_t::backward( back );
	uint16 run = 1;
	grund_t *to;
	while(  run<1024  &&  gr->get_neighbour( to, wt, back )  &&  to->get_halt()==halt  &&  (to->get_weg( wt )->get_ribi_maske() & forward)==0  &&  is_platform_suitable( to )  ) {
		run ++;
		gr = to;
	}
	return run;
}


bool rail_vehicle_t::is_stop_position(const grund_t *gr, const grund_t *prev_gr, halthandle_t halt) const
{
	if(  gr->is_halt()  &&  gr->get_halt()==halt  &&  is_platform_suitable(gr)  ) {
		// now we must check the predecessor ...
		if(  prev_gr!=NULL  ) {
			const koord dir=gr->get_pos().get_2d()-prev_gr->get_pos().get_2d();
			const ribi_t::ribi ribi = ribi_type(dir);
			if(  gr->get_weg(get_waytype())->get_ribi_maske() & ribi  ) {
				// signal/one way sign wrong direction
				return false;
			}
			grund_t *to;
			if(  !gr->get_neighbour(to,get_waytype(),ribi)  ||  !(to->get_halt()==halt)  ||  (to->get_weg(get_waytype())->get_ribi_maske() & ribi_type(dir))!=0  ) {
				// end of stop: Is it long enough?
				// end of stop could be also signal! (fork: any length while probing, see setup_bay_search)
				uint16 tiles = stop_any_length ? 1 : cnv->get_tile_length();
				while(  tiles>1  ) {
					if(  gr->get_weg(get_waytype())->get_ribi_maske() & ribi  ||  !gr->get_neighbour(to,get_waytype(),ribi_t::backward(ribi))  ||  !(to->get_halt()==halt)  ||  !is_platform_suitable(to)  ) {
						return false;
					}
					gr = to;
					tiles --;
				}
				return true;
			}
		}
	}
	return false;
}


bool rail_vehicle_t::is_longblock_signal_clear(signal_t *sig, uint16 next_block, sint32 &restart_speed)
{
	// longblock signal: first check, whether there is a signal coming up on the route => just like normal signal
	uint16 next_signal, next_crossing;
	if(  !block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		// not even the "Normal" signal route part is free => no bother checking further on
		sig->set_state( roadsign_t::rot );
		restart_speed = 0;
		return false;
	}

	if(  next_signal != INVALID_INDEX  ) {
		// success, and there is a signal before end of route => finished
		sig->set_state( roadsign_t::gruen );
		cnv->set_next_stop_index( min( next_crossing, next_signal ) );
		return true;
	}

	// no signal before end_of_route => need to do route search in a step
	if(  !cnv->is_waiting()  ) {
		restart_speed = -1;
		return false;
	}

	// now we can use the route search array
	// (route until end is already reserved at this point!)
	uint8 schedule_index = cnv->get_schedule()->get_current_stop()+1;
	route_t target_rt;
	koord3d cur_pos = cnv->get_route()->back();
	uint16 dummy, next_next_signal;
	if(schedule_index >= cnv->get_schedule()->get_count()) {
		schedule_index = 0;
	}
	while(  schedule_index != cnv->get_schedule()->get_current_stop()  ) {
		// now search
		// search for route
		bool success = target_rt.calc_route( welt, cur_pos, cnv->get_schedule()->entries[schedule_index].pos, this, speed_to_kmh(cnv->get_min_top_speed()), 8888 /*cnv->get_tile_length()*/ );
		if(  target_rt.is_contained(get_pos())  ) {
			// do not reserve route going through my current stop&
			break;
		}
		if(  success  ) {
			success = block_reserver( &target_rt, 1, next_next_signal, dummy, 0, true, false );
			block_reserver( &target_rt, 1, dummy, dummy, 0, false, false );
		}

		if(  success  ) {
			// ok, would be free
			if(  next_next_signal<target_rt.get_count()  ) {
				// and here is a signal => finished
				// (however, if it is this signal, we need to renew reservation ...
				if(  target_rt.at(next_next_signal) == cnv->get_route()->at( next_block )  ) {
					block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false );
				}
				sig->set_state( roadsign_t::gruen );
				cnv->set_next_stop_index( min( min( next_crossing, next_signal ), cnv->get_route()->get_count() ) );
				return true;
			}
		}

		if(  !success  ) {
			block_reserver( cnv->get_route(), next_block+1, next_next_signal, dummy, 0, false, false );
			sig->set_state( roadsign_t::rot );
			restart_speed = 0;
			return false;
		}
		// prepare for next leg of schedule
		cur_pos = target_rt.back();
		schedule_index ++;
		if(schedule_index >= cnv->get_schedule()->get_count()) {
			schedule_index = 0;
		}
	}
	if(  cnv->get_next_stop_index()-1 <= route_index  ) {
		cnv->set_next_stop_index( cnv->get_route()->get_count()-1 );
	}
	return true;
}


bool rail_vehicle_t::is_choose_signal_clear(signal_t *sig, const uint16 start_block, sint32 &restart_speed)
{
	bool choose_ok = false;
	target_halt = halthandle_t();
	platform_needs = 0; // set below only for a stop in this area
	bay_search = 0;
	// fork: route index of a platform signal that ends the choice (the train passes the station)
	uint32 through_at = INVALID_INDEX;

	uint16 next_signal, next_crossing;
	grund_t const* const target = welt->lookup(cnv->get_route()->back());

	if(  cnv->get_schedule_target()!=koord3d::invalid  ) {
		// destination is a waypoint!
		goto skip_choose;
	}

	if(  target==NULL  ) {
		cnv->suche_neue_route();
		return false;
	}

	// first check, if we are not heading to a waypoint
	if(  !target->get_halt().is_bound()  ) {
		goto skip_choose;
	}

	// now we might choose something at least
	choose_ok = true;

	// check, if there is another choose signal or end_of_choose on the route
	for(  uint32 idx=start_block+1;  choose_ok  &&  idx<cnv->get_route()->get_count();  idx++  ) {
		grund_t *gr = welt->lookup(cnv->get_route()->at(idx));
		if(  gr==0  ) {
			choose_ok = false;
			break;
		}
		if(  gr->get_halt()==target->get_halt()  ) {
			target_halt = gr->get_halt();
			break;
		}
		weg_t *way = gr->get_weg(get_waytype());
		if(  way==0  ) {
			choose_ok = false;
			break;
		}
		if(  way->has_sign()  ) {
			roadsign_t *rs = gr->find<roadsign_t>(1);
			if(  rs  &&  rs->get_desc()->get_wtyp()==get_waytype()  ) {
				if(  rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA  ) {
					// end of choose on route => not choosing here
					choose_ok = false;
				}
			}
			if(  rs  &&  rs->get_desc()->is_station_boundary()  ) {
				// fork: a station boundary (single-track line) => not choosing here
				choose_ok = false;
			}
		}
		if(  way->has_signal()  ) {
			signal_t *sig = gr->find<signal_t>(1);
			if(  sig  &&  sig->get_desc()->is_choose_sign()  ) {
				// second choose signal on route => not choosing here
				choose_ok = false;
			}
			else if(  sig  &&  sig->get_desc()->is_platform_signal()  &&  idx+1<cnv->get_route()->get_count()  &&  sig->applies_to( ribi_type( cnv->get_route()->at(idx), cnv->get_route()->at(idx+1) ) )  ) {
				// fork: the exit signal of a station track before our stop => we pass this station
				choose_ok = false;
				through_at = idx;
			}
		}
	}

skip_choose:
	if(  !choose_ok  ) {
		// fork: a train marked as Hold steps aside into a free platform when a passing train comes
		if(  cnv->is_hold_marked()  &&  cnv->get_schedule_target()==koord3d::invalid  ) {
			// (a pending waypoint would be skipped by the new route from that platform)
			const uint16 end_of_choose = get_passed_end_of_choose( start_block );
			bool stuck;
			if(  end_of_choose!=INVALID_INDEX  &&  get_passing_train( end_of_choose, halthandle_t(), cnv->get_route()->at(start_block), stuck ).is_bound()  ) {
				if(  !cnv->is_waiting()  ) {
					// the route search needs a step: come to the signal first
					restart_speed = -1;
					return false;
				}
				if(  reserve_hold_platform( start_block, end_of_choose, next_signal, next_crossing )  ) {
					sig->set_state(  roadsign_t::gruen );
					cnv->set_next_stop_index( min( next_crossing, next_signal ) );
					return true;
				}
			}
		}
		// just act as normal signal
		if(  block_reserver( cnv->get_route(), start_block+1, next_signal, next_crossing, 0, true, false )  ) {
			sig->set_state(  roadsign_t::gruen );
			cnv->set_next_stop_index( min( next_crossing, next_signal ) );
			return true;
		}
		if(  through_at!=INVALID_INDEX  ) {
			// fork: passing the station => any free track up to a platform signal from which we lead on
			if(  !cnv->is_waiting()  ) {
				restart_speed = -1;
				return false;
			}
			route_t path;
			if(  find_station_track( cnv->get_route(), start_block, halthandle_t(), 0, koord3d::invalid, path, cnv->get_last_waypoint_index( *cnv->get_route(), start_block ) )  ) {
				route_t *route = cnv->access_route();
				const route_t old_route( *route );
				if(  route_through( route, start_block, path, false )  &&  block_reserver( route, start_block+1, next_signal, next_crossing, 0, true, false )  ) {
					sig->set_state(  roadsign_t::gruen );
					cnv->set_next_stop_index( min( next_crossing, next_signal ) );
					return true;
				}
				route->clear();
				route->append( &old_route );
			}
			sig->set_state(  roadsign_t::rot );
			restart_speed = 0;
			return false;
		}
		// not free => a train passing the choose area may overtake on another track
		const uint16 end_of_choose = get_choose_detour_end( start_block );
		if(  end_of_choose!=INVALID_INDEX  ) {
			if(  !cnv->is_waiting()  ) {
				// the route search needs a step: come to the signal first
				restart_speed = -1;
				return false;
			}
			if(  reserve_choose_detour( start_block, end_of_choose, next_signal, next_crossing )  ) {
				sig->set_state(  roadsign_t::gruen );
				cnv->set_next_stop_index( min( next_crossing, next_signal ) );
				return true;
			}
		}
		// not free => wait here if directly in front
		sig->set_state(  roadsign_t::rot );
		restart_speed = 0;
		return false;
	}

	{
		// fork, coupling: to the platform of our partner, first come first served
		bool standing;
		const convoihandle_t partner = cnv->find_partner_at( target->get_halt(), standing );
		if(  partner.is_bound()  ) {
			const int result = reserve_to_partner( sig, start_block, partner, standing, restart_speed );
			if(  result >= 0  ) {
				return result > 0;
			}
			// no way to its platform from here: choose as usual
		}
	}

	target_halt = target->get_halt();
	platform_needs = get_platform_needs( cnv->get_schedule()->get_current_entry(), target_halt );
	// fork: the platform must lead on to the next stop (platform signals leave the other side's tracks two-way)
	schedule_t const* const schedule = cnv->get_schedule();
	const koord3d next_stop = schedule  &&  !schedule->empty() ? schedule->entries[ (schedule->get_current_stop()+1) % schedule->get_count() ].pos : koord3d::invalid;
	// fork: bay platforms only for a train that turns back here
	bool prefer_bays = false;
	if(  cnv->is_waiting()  ) {
		prefer_bays = setup_bay_search( cnv->get_route(), start_block, target_halt, next_stop, false );
	}
	else if(  is_bay_tile( target, get_waytype() )  ) {
		// planned into a bay: whether we turn back here needs a route search, that needs a step
		restart_speed = -1;
		target_halt = halthandle_t();
		return false;
	}
	bool planned_ok = is_planned_platform_suitable();
	if(  planned_ok  &&  turns_round_through( *cnv->get_route(), start_block, target_halt )  ) {
		// fork: planned to run through the station and come back in: only if there is no other way in
		if(  !cnv->is_waiting()  ) {
			restart_speed = -1;
			target_halt = halthandle_t();
			return false;
		}
		planned_ok = !can_enter_without_turning( cnv->get_route()->at(start_block), ribi_type(get_pos(), pos_next), target_halt, false );
	}
	// fork: a train that will pass us at our stop wants its way through here: stand off it if we can
	const bool step_aside = planned_ok  &&  get_overtaker_ways( start_block, overtaker_ways, overtaker_routes )  &&  stands_on_any( *cnv->get_route(), cnv->get_tile_length(), overtaker_ways );
	if(  step_aside  &&  !cnv->is_waiting()  ) {
		// the platform search needs a step: come to the signal first
		restart_speed = -1;
		target_halt = halthandle_t();
		overtaker_ways.clear();
		overtaker_routes.clear();
		return false;
	}
	if(  !step_aside  ) {
		overtaker_ways.clear();
		overtaker_routes.clear();
	}
	bool planned_taken = false;
	if(  step_aside  ||  !planned_ok  ||  !block_reserver( cnv->get_route(), start_block+1, next_signal, next_crossing, 100000, true, false )  ) {
		// no free route to target!
		// note: any old reservations should be invalid after the block reserver call.
		// => We can now start freshly all over

		if(!cnv->is_waiting()) {
			restart_speed = -1;
			target_halt = halthandle_t();
			return false;
		}
		// now we are in a step and can use the route search array

		// now it we are in a step and can use the route search
		route_t target_rt;
		const int richtung = ribi_type(get_pos(), pos_next); // to avoid confusion at diagonals
		uint32 planned_onward = 0xFFFFFFFFul; // measured when a platform is found
		bool found = false;
		// fork: first never on out of a platform of the halt (see stop_search_halt), then the search as before
		const uint8 bay_mode = bay_search;
		// fork: of the platforms that do, the shortest that fits (the first found of those as long)
		const uint16 fit = cnv->get_platform_length_needed( target_halt );
		// fork: stepping aside, first only platforms off the ways of the trains that pass us; none free:
		// as before (the planned platform, then any)
		for(  uint8 round=step_aside ? 0 : 1;  !found  &&  round<2;  round++  ) {
			if(  round==1  &&  step_aside  ) {
				overtaker_ways.clear();
				overtaker_routes.clear();
				if(  planned_ok  &&  block_reserver( cnv->get_route(), start_block+1, next_signal, next_crossing, 100000, true, false )  ) {
					planned_taken = true;
					break;
				}
			}
			route_t best_rt;
			uint16 best_rank = 0xFFFF;
			for(  uint8 pass=0;  !found  &&  pass<2;  pass++  ) {
				const halthandle_t no_through = pass==0 ? target_halt : halthandle_t();
				stop_search_start = cnv->get_route()->at(start_block);
				track_search_excluded.clear();
				track_search_taken.clear();
				// a train turning back here tries the bays first, then any platform
				bay_search = prefer_bays ? 2 : bay_mode;
				// fork: a platform whose way on runs over another platform only when no other is free
				vector_tpl<koord3d> crossing;
				bool avoid_crossing = true;
				for(  ;;  ) {
					// every rejected or taken platform is excluded, so this ends after at most as many tries as platforms
					for(  uint16 attempt=0;  !found  &&  attempt<256;  attempt++  ) {
						// (only this search: the way on from a platform below leaves it)
						stop_search_halt = no_through;
						const bool ok = target_rt.find_route( welt, cnv->get_route()->at(start_block), this, speed_to_kmh(cnv->get_min_top_speed()), richtung, welt->get_settings().get_max_choose_route_steps() );
						stop_search_halt = halthandle_t();
						if(  !ok  ) {
							break;
						}
						if(  turns_round_through( target_rt, 0, target_halt )  ) {
							// fork: never through the station and back in (a bay facing the other way)
							track_search_excluded.append( target_rt.back() );
							continue;
						}
						if(  !overtaker_ways.empty()  &&  ( stands_on_any( target_rt, cnv->get_tile_length(), overtaker_ways )  ||  !leads_back_to_overtaker( target_rt ) )  ) {
							// fork: stepping aside: not on their way, and not the other direction's track
							track_search_taken.append( target_rt.back() );
							continue;
						}
						const uint16 rank = get_platform_rank( get_found_platform_length( target_rt ), fit );
						if(  rank>=best_rank  ) {
							// no better than the one we have: not worth the way on
							track_search_taken.append( target_rt.back() );
							continue;
						}
						if(  next_stop!=koord3d::invalid  &&  planned_onward==0xFFFFFFFFul  ) {
							planned_onward = get_onward_length( cnv->get_route()->back(), next_stop );
						}
						const uint8 onward = next_stop==koord3d::invalid ? (uint8)ONWARD_OK : leads_on_like_planned( target_rt.back(), next_stop, planned_onward, target_halt );
						if(  onward==ONWARD_OK  ||  (onward==ONWARD_CROSSING  &&  !avoid_crossing)  ) {
							best_rt.clear();
							best_rt.append( &target_rt );
							best_rank = rank;
							// none can fit better: take it, else look for a shorter one
							found = rank<=fit;
							track_search_taken.append( target_rt.back() );
						}
						else {
							track_search_excluded.append( target_rt.back() );
							if(  onward==ONWARD_CROSSING  ) {
								crossing.append( target_rt.back() );
							}
						}
					}
					found |= best_rt.get_count()>=2;
					if(  found  ) {
						target_rt.clear();
						target_rt.append( &best_rt );
						break;
					}
					if(  avoid_crossing  &&  !crossing.empty()  ) {
						// nothing better: again with those
						avoid_crossing = false;
						FOR( vector_tpl<koord3d>, const &k, crossing ) {
							track_search_excluded.remove( k );
						}
						crossing.clear();
						continue;
					}
					if(  bay_search!=2  ) {
						break;
					}
					bay_search = 0;
					avoid_crossing = true;
				}
			}
		}
		stop_search_start = koord3d::invalid;
		bay_search = 0;
		track_search_excluded.clear();
		track_search_taken.clear();
		overtaker_ways.clear();
		overtaker_routes.clear();
		if(  planned_taken  ) {
			// fork: nothing off their way, the planned platform after all
		}
		else if(  !found  ) {
			// nothing empty or not route with less than get_max_choose_route_steps() tiles
			target_halt = halthandle_t();
			sig->set_state(  roadsign_t::rot );
			restart_speed = 0;
			return false;
		}
		else {
			// try to alloc the whole route
			cnv->access_route()->remove_koord_from(start_block);
			cnv->access_route()->append( &target_rt );
			if(  !block_reserver( cnv->get_route(), start_block+1, next_signal, next_crossing, 100000, true, false )  ) {
				dbg->error( "rail_vehicle_t::is_choose_signal_clear()", "could not reserved route after find_route!" );
				target_halt = halthandle_t();
				sig->set_state(  roadsign_t::rot );
				restart_speed = 0;
				return false;
			}
		}
		// reserved route to target
	}
	sig->set_state(  roadsign_t::gruen );
	cnv->set_next_stop_index( min( next_crossing, next_signal ) );
	return true;
}


// fork: the station boundary sign on this tile, if any
const roadsign_t *rail_vehicle_t::get_station_boundary(const grund_t *gr)
{
	for(  uint8 i=0;  i<2;  i++  ) {
		weg_t const* const way = gr->get_weg_nr(i);
		if(  way  &&  way->has_sign()  ) {
			roadsign_t const* const rs = gr->find<roadsign_t>();
			return rs  &&  rs->get_desc()->is_station_boundary() ? rs : NULL;
		}
	}
	return NULL;
}


/* fork: aspects of station boundaries drawn as entry signals and of choose signals with a yellow
 * aspect, display only. Yellow: the train goes to another platform than the one in its schedule
 * (yellow_aspect_rule 0), or its way takes a switch to the side (rule 1, and trains that do not stop
 * at the end of the way).
 */

// true when the way over path[from..to] takes a switch to the side
static bool takes_diverging_switch(const vector_tpl<koord3d> &path, uint32 from, uint32 to, waytype_t wt)
{
	for(  uint32 k=from+1;  k<=to  &&  k+1<path.get_count();  k++  ) {
		grund_t const* const gr = world()->lookup( path[k] );
		weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
		if(  way==NULL  ||  !ribi_t::is_threeway( way->get_ribi_unmasked() )  ) {
			continue;
		}
		const ribi_t::ribi in = ribi_type( path[k-1], path[k] );
		const ribi_t::ribi out = ribi_type( path[k], path[k+1] );
		if(  in==out  ) {
			continue;
		}
		// on a diagonal line the heading alternates from tile to tile: that is straight on too
		// (a single step aside between two tiles of the same heading is the switch to the next track)
		if(  k>=2  &&  k+2<path.get_count()  &&  ribi_type( path[k-2], path[k-1] )==out  &&  ribi_type( path[k+1], path[k+2] )==in  ) {
			continue;
		}
		return true;
	}
	return false;
}


// true when the way from path[from] on runs over the stop tile, or ends on its platform in front of it
// (e.g. behind a coupling partner)
static bool reaches_stop_platform(const vector_tpl<koord3d> &path, uint32 from, koord3d stop, waytype_t wt)
{
	for(  uint32 k=from;  k<path.get_count();  k++  ) {
		if(  path[k]==stop  ) {
			return true;
		}
	}
	grund_t const* gr = path.get_count()>=2 ? world()->lookup( path.back() ) : NULL;
	grund_t const* const stop_gr = world()->lookup( stop );
	if(  gr==NULL  ||  stop_gr==NULL  ||  !stop_gr->get_halt().is_bound()  ||  gr->get_halt()!=stop_gr->get_halt()  ) {
		return false;
	}
	const ribi_t::ribi dir = ribi_type( path[path.get_count()-2], path.back() );
	for(  uint16 n=0;  n<256;  n++  ) {
		grund_t *to;
		if(  !gr->get_neighbour( to, wt, dir )  ||  to->get_halt()!=stop_gr->get_halt()  ) {
			return false;
		}
		if(  to->get_pos()==stop  ) {
			return true;
		}
		gr = to;
	}
	return false;
}


static roadsign_t::signalstate get_way_aspect(const vector_tpl<koord3d> &path, uint32 from, uint32 to, bool stops, koord3d stop, waytype_t wt)
{
	bool side;
	if(  stops  &&  env_t::yellow_aspect_rule==0  &&  stop!=koord3d::invalid  ) {
		side = !reaches_stop_platform( path, from, stop, wt );
	}
	else {
		side = takes_diverging_switch( path, from, to, wt );
	}
	return side ? roadsign_t::naechste_rot : roadsign_t::gruen;
}


// the train was let past the signal at route index from: its reserved way from there
static roadsign_t::signalstate get_route_aspect(const convoi_t *cnv, uint32 from, waytype_t wt)
{
	const vector_tpl<koord3d> &path = cnv->get_route()->get_route();
	if(  from+1>=path.get_count()  ) {
		return roadsign_t::gruen;
	}
	const uint32 reserved_to = cnv->get_next_reservation_index();
	// the reservation reaches the end of the route: the train stops there
	const bool stops = reserved_to>=path.get_count();
	const koord3d stop = cnv->get_schedule()->entries[ cnv->get_route_entry() ].pos;
	return get_way_aspect( path, from, min( reserved_to, path.get_count() )-1, stops, stop, wt );
}


static roadsign_t::signalstate get_claim_aspect(const convoi_t *cnv, waytype_t wt)
{
	const vector_tpl<koord3d> &path = cnv->get_claim_path();
	return get_way_aspect( path, 0, path.get_count()-1, cnv->get_claim_stops(), cnv->get_claim_stop(), wt );
}


void rail_vehicle_t::update_boundary_aspect(koord3d pos)
{
	karte_t *welt = world();
	if(  welt->is_destroying()  ) {
		return;
	}
	grund_t *gr = welt->lookup( pos );
	roadsign_t *rs = gr ? const_cast<roadsign_t *>( get_station_boundary( gr ) ) : NULL;
	if(  rs==NULL  ||  !rs->shows_aspects()  ) {
		return;
	}
	const waytype_t wt = rs->get_desc()->get_wtyp()!=tram_wt ? rs->get_desc()->get_wtyp() : track_wt;
	roadsign_t::signalstate aspect = roadsign_t::rot;
	schiene_t const* const sch = (schiene_t const*)gr->get_weg( wt );
	const convoihandle_t res = sch ? sch->get_reserved_convoi() : convoihandle_t();
	if(  res.is_bound()  &&  res->get_vehicle_count()>0  ) {
		// the train coming here, let in here, or leaving the station here
		const route_t *r = res->get_route();
		uint32 idx = INVALID_INDEX;
		const uint32 front_at = res->front()->get_route_index();
		for(  uint32 i=front_at>0 ? front_at-1 : 0;  i+1<r->get_count();  i++  ) {
			if(  r->at(i)==pos  ) {
				idx = i;
				break;
			}
		}
		if(  idx!=INVALID_INDEX  &&  rs->applies_to( ribi_type( r->at(idx), r->at(idx+1) ) )  ) {
			grund_t const* const next = welt->lookup( r->at(idx+1) );
			schiene_t const* const next_sch = next ? (schiene_t const*)next->get_weg( wt ) : NULL;
			if(  next_sch  &&  next_sch->get_reserved_convoi()==res  ) {
				// let in
				aspect = get_route_aspect( res.get_rep(), idx, wt );
			}
			else if(  res->get_claim_boundary()==pos  &&  res->get_section_wait()==convoi_t::SECTION_WAIT_NONE  ) {
				// on its way here, it holds its track in the station
				aspect = get_claim_aspect( res.get_rep(), wt );
			}
		}
	}
	else {
		// the nearest train holding a track behind this boundary
		uint32 best = 0xFFFFFFFFu;
		FOR( vector_tpl<convoihandle_t>, const cnv, welt->convoys() ) {
			if(  cnv->get_claim_boundary()==pos  &&  cnv->get_section_wait()!=convoi_t::SECTION_WAIT_ENTRY  ) {
				const uint32 dist = koord_distance( cnv->get_pos().get_2d(), pos.get_2d() );
				if(  dist<best  ) {
					best = dist;
					aspect = get_claim_aspect( cnv.get_rep(), wt );
				}
			}
		}
	}
	if(  rs->get_state()!=aspect  ) {
		rs->set_state( aspect );
	}
}


bool rail_vehicle_t::signal_applies(const grund_t *gr, ribi_t::ribi dir) const
{
	weg_t const* const way = gr->get_weg( get_waytype() );
	if(  way==NULL  ||  !way->has_signal()  ) {
		return false;
	}
	signal_t const* const sig = gr->find<signal_t>();
	if(  sig==NULL  ||  sig->get_desc()->is_block_post()  ) {
		return false;
	}
	// a stock signal applies to every train that can pass it; a platform signal only in its direction
	return !sig->get_desc()->is_platform_signal()  ||  sig->applies_to( dir );
}


bool rail_vehicle_t::block_post_applies(const grund_t *gr, ribi_t::ribi dir)
{
	for(  uint8 i=0;  i<2;  i++  ) {
		weg_t const* const way = gr->get_weg_nr(i);
		if(  way  &&  way->has_signal()  ) {
			signal_t const* const sig = gr->find<signal_t>();
			return sig  &&  sig->get_desc()->is_block_post()  &&  sig->applies_to( dir );
		}
	}
	return false;
}


bool rail_vehicle_t::is_stop_point(const route_t *route, uint32 index) const
{
	grund_t const* const gr = welt->lookup( route->at(index) );
	if(  gr==NULL  ) {
		return false;
	}
	ribi_t::ribi dir = ribi_t::all;
	if(  index+1 < route->get_count()  ) {
		dir = ribi_type( route->at(index), route->at(index+1) );
	}
	else if(  index > 0  ) {
		dir = ribi_type( route->at(index-1), route->at(index) );
	}
	if(  signal_applies( gr, dir )  ) {
		return true;
	}
	if(  cnv->in_section()  &&  block_post_applies( gr, dir )  ) {
		// fork: a block post divides the single-track line for trains that entered it at a platform
		// signal; to any other train it is not there (it holds the whole line, as before)
		return true;
	}
	roadsign_t const* const lt = get_station_boundary( gr );
	return lt  &&  lt->applies_to( dir );
}


// index in path of the first platform tile of the station track at its end (after its last switch);
// 0 when that track has no platform (nothing to claim then)
static uint16 get_track_start(const route_t &path, waytype_t wt)
{
	uint32 first = 1;
	for(  uint32 i=path.get_count()-1;  i>0;  i--  ) {
		grund_t const* const gr = world()->lookup( path.at(i-1) );
		weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
		if(  way  &&  ribi_t::is_threeway( way->get_ribi_unmasked() )  ) {
			first = i;
			break;
		}
	}
	for(  uint32 i=first;  i<path.get_count();  i++  ) {
		grund_t const* const gr = world()->lookup( path.at(i) );
		if(  gr  &&  gr->is_halt()  ) {
			return i;
		}
	}
	return 0;
}


// fork: index in path of the first tile claimed at the station at its end: the platform tiles of the
// track (get_track_start), or for a track without a platform (a passing loop) the track from its last
// switch on, so two trains coming from both ends never count on the same free track
static uint16 get_claim_start(const route_t &path, waytype_t wt)
{
	const uint16 start = get_track_start( path, wt );
	if(  start>0  ||  path.get_count()<2  ) {
		return start;
	}
	for(  uint32 i=path.get_count()-1;  i>0;  i--  ) {
		grund_t const* const gr = world()->lookup( path.at(i-1) );
		weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
		if(  way  &&  ribi_t::is_threeway( way->get_ribi_unmasked() )  ) {
			return i;
		}
	}
	return 1;
}


// some platform of this halt holds a train of length tiles (halt tiles in a row along the track);
// fork: not counting the tiles in skip (bays for a train that does not turn back)
static bool has_platform_for(halthandle_t halt, uint16 length, waytype_t wt, const vector_tpl<koord3d> *skip)
{
	FOR( slist_tpl<haltestelle_t::tile_t>, const &tile, halt->get_tiles() ) {
		weg_t const* const way = tile.grund->get_weg( wt );
		if(  way==NULL  ||  (skip  &&  skip->is_contained( tile.grund->get_pos() ))  ) {
			continue;
		}
		uint16 run = 1;
		for(  int r=0;  r<4  &&  run<length;  r++  ) {
			if(  (way->get_ribi_unmasked() & ribi_t::nsew[r])==0  ) {
				continue;
			}
			const grund_t *gr = tile.grund;
			grund_t *to;
			while(  run<length  &&  gr->get_neighbour( to, wt, ribi_t::nsew[r] )  &&  to->get_halt()==halt  &&  (skip==NULL  ||  !skip->is_contained( to->get_pos() ))  ) {
				run ++;
				gr = to;
			}
		}
		if(  run >= length  ) {
			return true;
		}
	}
	return false;
}


void rail_vehicle_t::collect_way_out(const route_t &rt, const uint32 from, vector_tpl<koord3d> &tiles) const
{
	for(  uint32 i=from;  i<rt.get_count()  &&  tiles.get_count()<128;  i++  ) {
		tiles.append( rt.at(i) );
		grund_t const* const gr = welt->lookup( rt.at(i) );
		roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL;
		if(  lt  &&  i+1<rt.get_count()  &&  !lt->applies_to( ribi_type( rt.at(i), rt.at(i+1) ) )  ) {
			break;
		}
	}
}


void rail_vehicle_t::get_way_out(convoihandle_t c, const koord3d via, vector_tpl<koord3d> &tiles)
{
	route_t const* const r = c->get_route();
	if(  r->empty()  ) {
		return;
	}
	uint32 from = min( max( c->front()->get_route_index(), 1u ) - 1, r->get_count()-1 );
	bool on_route = false;
	for(  uint32 i=from;  i<r->get_count();  i++  ) {
		if(  r->at(i)==via  ) {
			from = i;
			on_route = true;
			break;
		}
	}
	schedule_t const* const sch = c->get_schedule();
	if(  !on_route  &&  c->has_claim()  &&  c->get_claim_path().is_contained( via )  &&  sch  &&  !sch->empty()  ) {
		// a track claimed at a station further on (its route ends before): from there over its claim,
		// then on to the stop after the one it claimed it for (or to that stop, when it runs through)
		const vector_tpl<koord3d> &claim = c->get_claim_path();
		uint32 k = 0;
		while(  claim[k]!=via  ) {
			k++;
		}
		for(  ;  k<claim.get_count()  &&  tiles.get_count()<128;  k++  ) {
			tiles.append( claim[k] );
		}
		uint8 idx = 0;
		for(  uint8 e=0;  e<sch->get_count();  e++  ) {
			if(  sch->entries[e].pos==c->get_claim_stop()  ) {
				idx = c->get_claim_stops() ? (e+1) % sch->get_count() : e;
				break;
			}
		}
		route_t on;
		const uint8 old_search = track_search;
		track_search = 3;
		const bool ok = on.calc_route( welt, claim.back(), sch->entries[idx].pos, this, speed_to_kmh( c->get_min_top_speed() ), 8888 )!=route_t::no_route;
		track_search = old_search;
		if(  ok  &&  on.get_count()>=2  ) {
			collect_way_out( on, 1, tiles );
		}
		return;
	}
	collect_way_out( *r, from, tiles );
	grund_t const* const last_gr = welt->lookup( tiles.back() );
	roadsign_t const* const lt = last_gr ? get_station_boundary( last_gr ) : NULL;
	if(  (lt  &&  tiles.back()!=r->back())  ||  sch==NULL  ||  sch->empty()  ||  tiles.get_count()>=128  ) {
		// leaves the station along its route
		return;
	}
	// its route ends in the station: on from its stop to the next one
	uint8 idx = sch->get_current_stop();
	const halthandle_t end_halt = haltestelle_t::get_halt( r->back(), c->get_owner() );
	if(  r->back()==sch->entries[idx].pos  ||  (end_halt.is_bound()  &&  end_halt==haltestelle_t::get_halt( sch->entries[idx].pos, c->get_owner() ))  ) {
		// still on the way to that stop (it stands at the far end of the platform)
		idx = (idx+1) % sch->get_count();
	}
	route_t on;
	const uint8 old_search = track_search;
	track_search = 3;
	const bool ok = on.calc_route( welt, r->back(), sch->entries[idx].pos, this, speed_to_kmh( c->get_min_top_speed() ), 8888 )!=route_t::no_route;
	track_search = old_search;
	if(  ok  &&  on.get_count()>=2  ) {
		collect_way_out( on, 1, tiles );
	}
}


bool rail_vehicle_t::locks_with_others(const route_t &track, const uint32 track_from, const vector_tpl<koord3d> &way_out)
{
	vector_tpl<convoihandle_t> seen;
	FOR( vector_tpl<koord3d>, const &pos, way_out ) {
		grund_t const* const gr = welt->lookup( pos );
		schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
		const convoihandle_t c = sch ? sch->get_reserved_convoi() : convoihandle_t();
		if(  !c.is_bound()  ||  c==cnv->self  ||  c->get_vehicle_count()==0  ||  seen.is_contained( c )  ) {
			continue;
		}
		seen.append( c );
		vector_tpl<koord3d> theirs;
		get_way_out( c, pos, theirs );
		for(  uint32 k=track_from;  k<track.get_count();  k++  ) {
			if(  theirs.is_contained( track.at(k) )  ) {
				return true;
			}
		}
	}
	return false;
}


bool rail_vehicle_t::track_locks(const route_t &track, const koord3d next_stop)
{
	if(  next_stop==koord3d::invalid  ||  track.get_count()<2  ) {
		return false;
	}
	route_t on;
	const uint8 old_search = track_search;
	track_search = 3;
	const bool ok = on.calc_route( welt, track.back(), next_stop, this, speed_to_kmh( cnv->get_min_top_speed() ), 8888 )!=route_t::no_route;
	track_search = old_search;
	if(  !ok  ||  on.get_count()<2  ) {
		return false;
	}
	vector_tpl<koord3d> way_out;
	collect_way_out( on, 1, way_out );
	return locks_with_others( track, get_claim_start( track, get_waytype() ), way_out );
}


bool rail_vehicle_t::find_station_track(const route_t *route, uint32 start, halthandle_t halt, uint8 needs, koord3d next_stop, route_t &path, uint32 keep_to)
{
	path.clear();
	const uint32 count = route->get_count();
	if(  start+1 >= count  ) {
		return false;
	}
	const sint32 speed = speed_to_kmh( cnv->get_min_top_speed() );

	// the planned track first: stopping at the end of the route, or passing up to the next signal that applies
	uint32 planned_end = count-1;
	if(  !halt.is_bound()  ) {
		planned_end = INVALID_INDEX;
		for(  uint32 i=start+1;  i+1<count;  i++  ) {
			if(  is_stop_point( route, i )  ) {
				planned_end = i;
				break;
			}
		}
		if(  planned_end==INVALID_INDEX  ) {
			return false;
		}
	}
	bool planned_ok = true;
	for(  uint32 i=start+1;  planned_ok  &&  i<=planned_end;  i++  ) {
		grund_t const* const gr = welt->lookup( route->at(i) );
		schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
		planned_ok = sch  &&  sch->can_reserve( cnv->self );
	}
	platform_needs = halt.is_bound() ? needs : 0;
	// fork: bay platforms only for a train that turns back there, then first
	const bool prefer_bays = setup_bay_search( route, start, halt, next_stop, true );
	if(  planned_ok  &&  halt.is_bound()  ) {
		// every tile the train stands on must suit (platform types)
		uint16 tiles = cnv->get_tile_length();
		for(  uint32 idx=count;  planned_ok  &&  idx>start+1  &&  tiles>0;  idx--, tiles--  ) {
			grund_t const* const gr = welt->lookup( route->at(idx-1) );
			if(  gr==NULL  ||  gr->get_halt()!=halt  ) {
				break;
			}
			planned_ok = is_platform_suitable( gr );
		}
		if(  planned_ok  &&  tiles>0  &&  has_platform_for( halt, cnv->get_tile_length(), get_waytype(), bay_search==1 ? &bay_search_tiles : NULL )  ) {
			// too short, and a platform long enough exists: the train would stand in the throat
			// (where no platform is long enough, it stops sticking out as in the stock game)
			planned_ok = false;
		}
	}
	if(  planned_ok  &&  halt.is_bound()  &&  turns_round_through( *route, start, halt )  ) {
		// through the station and back in: only if there is no other way in
		planned_ok = !can_enter_without_turning( route->at(start), ribi_type( route->at(start), route->at(start+1) ), halt, true );
	}
	// the way on from the planned platform, also the measure for the others (measured when needed)
	uint32 planned_onward = 0xFFFFFFFFul;
	if(  planned_ok  &&  halt.is_bound()  &&  next_stop!=koord3d::invalid  ) {
		// it must lead on to the next stop, like any other track
		planned_onward = get_onward_length( route->back(), next_stop );
		planned_ok = planned_onward>0;
	}
	// fork: not a track whose way out runs over a train whose way out runs over that track
	if(  planned_ok  ) {
		route_t planned;
		for(  uint32 i=start;  i<=planned_end;  i++  ) {
			planned.append( route->at(i) );
		}
		if(  halt.is_bound()  ) {
			planned_ok = !track_locks( planned, next_stop );
		}
		else {
			vector_tpl<koord3d> way_out;
			collect_way_out( *route, planned_end, way_out );
			planned_ok = !locks_with_others( planned, get_claim_start( planned, get_waytype() ), way_out );
		}
	}
	// fork: a train that will overtake us there wants its way through (overtaker_ways): first only
	// platforms off it; none free: as before (the planned platform, then any)
	const bool aside = halt.is_bound()  &&  !overtaker_ways.empty();
	const bool planned_on_way = planned_ok  &&  aside  &&  stands_on_any( *route, cnv->get_tile_length(), overtaker_ways );
	if(  planned_ok  &&  !planned_on_way  ) {
		for(  uint32 i=start;  i<=planned_end;  i++  ) {
			path.append( route->at(i) );
		}
		bay_search = 0;
		return true;
	}

	// schedule waypoints ahead stay on the way. Passing: a waypoint on the planned track allows only
	// that track (as at a stock choose signal), those beyond are kept on the way on. Stopping: the way
	// up to the last waypoint stays, a free track is searched from there
	uint32 from = start;
	if(  !halt.is_bound()  ) {
		for(  uint32 i=start+1;  i<=planned_end;  i++  ) {
			if(  cnv->is_pending_waypoint( route->at(i) )  ) {
				return false;
			}
		}
	}
	else if(  keep_to!=INVALID_INDEX  &&  keep_to>start  ) {
		if(  keep_to+1>=count  ) {
			bay_search = 0;
			return false;
		}
		for(  uint32 i=start+1;  i<=keep_to;  i++  ) {
			grund_t const* const gr = welt->lookup( route->at(i) );
			schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
			if(  sch==NULL  ||  !sch->can_reserve( cnv->self )  ) {
				bay_search = 0;
				return false;
			}
		}
		from = keep_to;
	}

	// search a free track (stopping: a stop position of halt, passing: up to a signal that applies)
	track_search_halt = halt;
	track_search_start = route->at(from);
	const ribi_t::ribi start_dir = ribi_type( route->at(from), route->at(from+1) );
	bool found = false;
	// fork: stopping, first never on out of a platform of halt (see stop_search_halt), then as before
	const uint8 bay_mode = bay_search;
	// stopping: of the platforms that do, the shortest that fits (the first found of those as long)
	const uint16 fit = halt.is_bound() ? cnv->get_platform_length_needed( halt ) : 0;
	for(  uint8 round=aside ? 0 : 1;  !found  &&  round<2;  round++  ) {
		if(  round==1  &&  planned_on_way  ) {
			// nothing off the overtaker's way: the planned platform after all
			path.clear();
			for(  uint32 i=start;  i<=planned_end;  i++  ) {
				path.append( route->at(i) );
			}
			found = true;
			break;
		}
		route_t best;
		uint16 best_rank = 0xFFFF;
		for(  uint8 pass=halt.is_bound() ? 0 : 1;  !found  &&  pass<2;  pass++  ) {
			const halthandle_t no_through = pass==0 ? halt : halthandle_t();
			stop_search_start = route->at(from);
			track_search_excluded.clear();
			track_search_taken.clear();
			// a train turning back there tries the bays first, then any track
			bay_search = prefer_bays ? 2 : bay_mode;
			// a platform whose way on runs over another platform only when no other is free
			vector_tpl<koord3d> crossing;
			bool avoid_crossing = true;
			for(  ;;  ) {
				// every rejected or taken track is excluded, so this ends after at most as many tries as tracks
				for(  uint16 attempt=0;  !found  &&  attempt<256;  attempt++  ) {
					route_t candidate;
					track_search = halt.is_bound() ? 1 : 2;
					track_search_block = false;
					stop_search_halt = no_through;
					const bool ok = candidate.find_route( welt, route->at(from), this, speed, start_dir, welt->get_settings().get_max_choose_route_steps() );
					stop_search_halt = halthandle_t();
					// the way on from there may use any track
					track_search = 3;
					bool leads_on = false;
					bool crosses = false;
					uint16 rank = 0xFFFF;
					if(  ok  &&  candidate.get_count()>=2  ) {
						if(  halt.is_bound()  &&  round==0  &&  ( stands_on_any( candidate, cnv->get_tile_length(), overtaker_ways )  ||  !leads_back_to_overtaker( candidate ) )  ) {
							// stepping aside: not on its way, and not the other direction's track
							track_search = 0;
							track_search_taken.append( candidate.back() );
							continue;
						}
						if(  halt.is_bound()  ) {
							rank = get_platform_rank( get_found_platform_length( candidate ), fit );
							if(  rank>=best_rank  ) {
								// no better than the one we have: not worth the way on
								track_search = 0;
								track_search_taken.append( candidate.back() );
								continue;
							}
							// never through the station and back in (a bay facing the other way)
							if(  !turns_round_through( candidate, 0, halt )  ) {
								if(  next_stop!=koord3d::invalid  &&  planned_onward==0xFFFFFFFFul  ) {
									planned_onward = get_onward_length( route->back(), next_stop );
								}
								const uint8 onward = next_stop==koord3d::invalid ? (uint8)ONWARD_OK : leads_on_like_planned( candidate.back(), next_stop, planned_onward, halt );
								crosses = onward==ONWARD_CROSSING;
								leads_on = onward==ONWARD_OK  ||  (crosses  &&  !avoid_crossing);
								if(  leads_on  &&  track_locks( candidate, next_stop )  ) {
									// fork: a train in its way out has its way out over it
									leads_on = false;
									crosses = false;
								}
							}
						}
						else {
							route_t on;
							leads_on = cnv->calc_route_on( candidate.back(), *route, start, candidate.get_route(), on )
								&&  on.at(1)!=candidate.at( candidate.get_count()-2 );
							if(  leads_on  ) {
								// not much longer than the planned way
								const uint32 planned_len = count-1-start;
								const uint32 new_len = candidate.get_count()-1 + on.get_count()-1;
								leads_on = new_len <= planned_len + (planned_end-start)/2 + 4;
							}
							if(  leads_on  ) {
								// fork: and no train in the way out whose way out runs over this track
								vector_tpl<koord3d> way_out;
								collect_way_out( on, 1, way_out );
								leads_on = !locks_with_others( candidate, get_claim_start( candidate, get_waytype() ), way_out );
							}
						}
					}
					track_search = 0;
					if(  !ok  ||  candidate.get_count()<2  ) {
						break;
					}
					if(  leads_on  &&  halt.is_bound()  ) {
						best.clear();
						best.append( &candidate );
						best_rank = rank;
						// none can fit better: take it, else look for a shorter one
						found = rank<=fit;
						track_search_taken.append( candidate.back() );
					}
					else if(  leads_on  ) {
						best.clear();
						best.append( &candidate );
						found = true;
					}
					else {
						track_search_excluded.append( candidate.back() );
						if(  crosses  ) {
							crossing.append( candidate.back() );
						}
					}
				}
				found |= best.get_count()>=2;
				if(  found  ) {
					for(  uint32 i=start;  i<from;  i++  ) {
						path.append( route->at(i) );
					}
					path.append( &best );
					break;
				}
				if(  avoid_crossing  &&  !crossing.empty()  ) {
					// nothing better: again with those
					avoid_crossing = false;
					FOR( vector_tpl<koord3d>, const &k, crossing ) {
						track_search_excluded.remove( k );
					}
					crossing.clear();
					continue;
				}
				if(  bay_search!=2  ) {
					break;
				}
				bay_search = 0;
				avoid_crossing = true;
			}
		}
	}
	stop_search_start = koord3d::invalid;
	bay_search = 0;
	track_search_excluded.clear();
	track_search_taken.clear();
	track_search = 0;
	track_search_start = koord3d::invalid;
	return found;
}


bool rail_vehicle_t::find_partner_track(const route_t *route, uint32 start, convoihandle_t partner, route_t &path)
{
	path.clear();
	if(  start+1 >= route->get_count()  ) {
		return false;
	}
	route_t target_rt;
	couple_search = partner;
	couple_goal = koord3d::invalid;
	couple_in_station = true;
	track_search_block = false;
	const ribi_t::ribi start_dir = ribi_type( route->at(start), route->at(start+1) );
	const bool found = target_rt.find_route( welt, route->at(start), this, speed_to_kmh( cnv->get_min_top_speed() ), start_dir, welt->get_settings().get_max_choose_route_steps() );
	couple_search = convoihandle_t();
	couple_in_station = false;
	track_search_block = false;
	if(  !found  ||  target_rt.get_count()<3  ) {
		return false;
	}
	// up to the tile right behind it
	target_rt.remove_koord_from( target_rt.get_count()-2 );
	// on its platform (not in the throat), and its track behind it must be free (the throat is reserved at the station boundary)
	const uint16 track_start = get_track_start( target_rt, get_waytype() );
	grund_t const* const end_gr = welt->lookup( target_rt.back() );
	if(  track_start==0  ||  end_gr==NULL  ||  !end_gr->is_halt()  ) {
		return false;
	}
	for(  uint32 i=track_start;  i<target_rt.get_count();  i++  ) {
		grund_t const* const gr = welt->lookup( target_rt.at(i) );
		schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
		if(  sch==NULL  ||  !sch->can_reserve( cnv->self )  ) {
			return false;
		}
	}
	path.append( &target_rt );
	return true;
}


// the track at pos leads on to a tile reserved by c
static bool leads_to_convoi(koord3d pos, convoihandle_t c, waytype_t wt)
{
	grund_t *gr = world()->lookup( pos );
	weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
	if(  way==NULL  ) {
		return false;
	}
	for(  int r=0;  r<4;  r++  ) {
		grund_t *to;
		if(  (way->get_ribi_unmasked() & ribi_t::nsew[r])  &&  gr->get_neighbour( to, wt, ribi_t::nsew[r] )  ) {
			schiene_t const* const sch = (schiene_t const*)to->get_weg( wt );
			if(  sch  &&  sch->get_reserved_convoi()==c  ) {
				return true;
			}
		}
	}
	return false;
}


// fork: the way on from a platform of halt runs over another platform of halt: past the tiles of its own
// platform it turns off onto another track at a switch and then comes to a platform of halt (a next stop
// at halt itself does not count; a platform split by a switch it runs straight through is one track)
static bool runs_over_other_platform(const route_t &on, halthandle_t halt, waytype_t wt)
{
	karte_t *welt = world();
	const uint32 n = on.get_count();
	grund_t const* const last = n>0 ? welt->lookup( on.at(n-1) ) : NULL;
	if(  !halt.is_bound()  ||  last==NULL  ||  last->get_halt()==halt  ) {
		return false;
	}
	// the tiles of its own platform
	uint32 i = 0;
	while(  i<n  &&  welt->lookup( on.at(i) )  &&  welt->lookup( on.at(i) )->get_halt()==halt  ) {
		i++;
	}
	bool turned = false;
	for(  ;  i<n;  i++  ) {
		grund_t const* const gr = welt->lookup( on.at(i) );
		if(  gr==NULL  ) {
			continue;
		}
		if(  turned  &&  gr->get_halt()==halt  ) {
			// a platform for us (not a tram or road stop of the same halt on a crossing)
			gebaeude_t const* const gb = gr->find<gebaeude_t>();
			if(  gb  &&  gb->get_tile()->get_desc()->get_extra()==(uint32)wt  ) {
				return true;
			}
		}
		weg_t const* const way = gr->get_weg( wt );
		if(  i>0  &&  i+1<n  &&  way  &&  ribi_t::is_threeway( way->get_ribi_unmasked() )  &&  ribi_type( on.at(i-1), on.at(i) )!=ribi_type( on.at(i), on.at(i+1) )  ) {
			turned = true;
		}
	}
	return false;
}


uint32 rail_vehicle_t::get_onward_length(koord3d from, koord3d next_stop, halthandle_t halt, bool *crosses)
{
	route_t on;
	const uint8 old_search = track_search;
	track_search = 3;
	const bool ok = on.calc_route( welt, from, next_stop, this, speed_to_kmh( cnv->get_min_top_speed() ), 0 )!=route_t::no_route;
	track_search = old_search;
	if(  crosses  ) {
		*crosses = ok  &&  runs_over_other_platform( on, halt, get_waytype() );
	}
	return ok ? max( on.get_count(), 1u ) : 0;
}


uint8 rail_vehicle_t::leads_on_like_planned(koord3d from, koord3d next_stop, uint32 planned_length, halthandle_t halt)
{
	bool crosses = false;
	const uint32 length = get_onward_length( from, next_stop, halt, &crosses );
	// as for a way through a station (find_station_track): at most half as long again, plus a few tiles
	if(  length==0  ||  (planned_length!=0  &&  length > planned_length + planned_length/2 + 8)  ) {
		return ONWARD_NONE;
	}
	return crosses ? ONWARD_CROSSING : ONWARD_OK;
}


bool rail_vehicle_t::route_through(route_t *route, uint32 start, const route_t &path, bool stops)
{
	if(  path.get_count()<2  ||  start>=route->get_count()  ) {
		return false;
	}
	route_t new_route;
	for(  uint32 i=0;  i<=start;  i++  ) {
		new_route.append( route->at(i) );
	}
	for(  uint32 k=1;  k<path.get_count();  k++  ) {
		new_route.append( path.at(k) );
	}
	if(  !stops  &&  path.back()!=route->back()  ) {
		// passing: from the signal at the end of the track on to the end of the route
		bool planned = start+path.get_count() < route->get_count();
		for(  uint32 k=0;  planned  &&  k<path.get_count();  k++  ) {
			planned = route->at(start+k)==path.at(k);
		}
		if(  planned  ) {
			// the planned track: the route on from there stays (with its waypoints)
			for(  uint32 i=start+path.get_count();  i<route->get_count();  i++  ) {
				new_route.append( route->at(i) );
			}
		}
		else {
			route_t on;
			track_search = 3;
			const bool ok = cnv->calc_route_on( path.back(), *route, start, path.get_route(), on );
			track_search = 0;
			if(  !ok  ||  on.at(1)==path.at( path.get_count()-2 )  ) {
				return false;
			}
			for(  uint32 k=1;  k<on.get_count();  k++  ) {
				new_route.append( on.at(k) );
			}
		}
	}
	if(  new_route.get_count() >= INVALID_INDEX  ) {
		return false;
	}
	route->clear();
	route->append( &new_route );
	return true;
}


uint32 rail_vehicle_t::get_platform_exit_signal(uint32 here) const
{
	if(  !cnv->may_hold_at_platform()  ) {
		return INVALID_INDEX;
	}
	grund_t const* const gr_here = welt->lookup( get_pos() );
	if(  gr_here==NULL  ||  !gr_here->is_halt()  ) {
		return INVALID_INDEX;
	}
	const halthandle_t halt = gr_here->get_halt();
	const route_t *route = cnv->get_route();
	for(  uint32 i=here;  i+1<route->get_count();  i++  ) {
		grund_t const* const gr = welt->lookup( route->at(i) );
		weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
		if(  way==NULL  ||  gr->get_depot()  ||  way->is_crossing()  ||  ribi_t::is_threeway( way->get_ribi_unmasked() )  ) {
			// not the track the train stands on any more
			return INVALID_INDEX;
		}
		if(  gr->is_halt()  &&  gr->get_halt()!=halt  ) {
			return INVALID_INDEX;
		}
		if(  is_stop_point( route, i )  ) {
			signal_t const* const sig = gr->find<signal_t>();
			return sig  &&  sig->get_desc()->is_platform_signal()  &&  signal_applies( gr, ribi_type( route->at(i), route->at(i+1) ) ) ? i : INVALID_INDEX;
		}
	}
	return INVALID_INDEX;
}


/* fork: exit signal of a station track. An ordinary signal, unless the train leaves the station through a
 * station boundary onto a single-track line: then the line up to the next station must be free and a track
 * there is claimed, see documentation/fork-rail-signalling-plan.md
 */
bool rail_vehicle_t::is_platform_signal_clear(signal_t *sig, uint16 next_block, sint32 &restart_speed)
{
	route_t *route = cnv->access_route();
	uint16 next_signal, next_crossing;

	// a train at a platform signal is in no single-track section (set again below when it enters one)
	cnv->clear_section();

	// leaving through a station boundary before any signal that applies?
	bool section = false;
	koord3d leave_boundary = koord3d::invalid;
	for(  uint32 i=next_block+1;  i<route->get_count();  i++  ) {
		grund_t const* const gr = welt->lookup( route->at(i) );
		if(  gr==NULL  ) {
			break;
		}
		if(  roadsign_t const* const lt = get_station_boundary( gr )  ) {
			const ribi_t::ribi dir = i+1<route->get_count() ? ribi_type( route->at(i), route->at(i+1) ) : ribi_type( route->at(i-1), route->at(i) );
			section = !lt->applies_to( dir );
			leave_boundary = route->at(i);
			break;
		}
		if(  is_stop_point( route, i )  ) {
			break;
		}
	}

	if(  !section  ) {
		// an ordinary exit signal; a claim from before is out of date here
		cnv->release_claim( true );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
		if(  block_reserver( route, next_block+1, next_signal, next_crossing, 0, true, false )  ) {
			sig->set_state( roadsign_t::gruen );
			cnv->set_next_stop_index( min( next_crossing, next_signal ) );
			return true;
		}
		sig->set_state( roadsign_t::rot );
		restart_speed = 0;
		return false;
	}

	// single-track section: the route searches need a step
	if(  !cnv->is_waiting()  ) {
		restart_speed = -1;
		return false;
	}
	cnv->release_claim( true );

	// follow the route and the next schedule legs to the station boundary into the next station
	schedule_t const* const schedule = cnv->get_schedule();
	const sint32 speed = speed_to_kmh( cnv->get_min_top_speed() );
	vector_tpl<koord3d> ahead;
	vector_tpl<uint32> leg_end;
	for(  uint32 i=next_block;  i<route->get_count();  i++  ) {
		ahead.append( route->at(i) );
	}
	leg_end.append( ahead.get_count()-1 );
	// the route runs through the schedule waypoints ahead: the first leg ends at the entry after them
	const uint8 first_entry = cnv->get_route_entry();
	uint8 entry_index = first_entry;
	sint32 enter = -1, end_signal = -1;
	uint32 scan_from = 1;
	track_search = 3; // the legs over any track, the tiles are checked below
	for(  uint8 legs=0;  legs<schedule->get_count();  legs++  ) {
		for(  uint32 i=scan_from;  i<ahead.get_count();  i++  ) {
			grund_t const* const gr = welt->lookup( ahead[i] );
			if(  gr==NULL  ) {
				break;
			}
			const ribi_t::ribi dir = i+1<ahead.get_count() ? ribi_type( ahead[i], ahead[i+1] ) : ribi_type( ahead[i-1], ahead[i] );
			if(  roadsign_t const* const lt = get_station_boundary( gr )  ) {
				if(  lt->applies_to( dir )  ) {
					enter = i;
					break;
				}
				continue;
			}
			if(  i+1<ahead.get_count()  &&  signal_applies( gr, dir )  ) {
				// the line ends without a station boundary (e.g. joins a double track)
				end_signal = i;
				break;
			}
		}
		if(  enter>=0  ||  end_signal>=0  ) {
			break;
		}
		// on with the next leg of the schedule
		entry_index = (entry_index+1) % schedule->get_count();
		if(  entry_index==first_entry  ) {
			break;
		}
		route_t leg;
		if(  leg.calc_route( welt, ahead.back(), schedule->entries[entry_index].pos, this, speed, 8888 )==route_t::no_route  ||  leg.get_count()<2  ) {
			break;
		}
		scan_from = ahead.get_count();
		for(  uint32 k=1;  k<leg.get_count();  k++  ) {
			ahead.append( leg.at(k) );
		}
		leg_end.append( ahead.get_count()-1 );
	}
	track_search = 0;

	if(  enter<0  &&  end_signal<0  ) {
		// no end of the section found (no way on, or the schedule never leaves the line): stay red
		sig->set_state( roadsign_t::rot );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_LINE, halthandle_t() );
		restart_speed = 0;
		return false;
	}

	// the line must be free up to there; with a block post on the way, beyond it only trains we may
	// follow (they go the same way into the same station, their track there claimed)
	const uint32 last = enter>=0 ? (uint32)enter : (uint32)end_signal;
	const koord3d enter_boundary = enter>=0 ? ahead[enter] : koord3d::invalid;
	uint32 first_post = 0xFFFFFFFFul;
	if(  enter>=0  &&  leave_boundary!=koord3d::invalid  &&  leave_boundary!=enter_boundary  ) {
		for(  uint32 i=1;  i<(uint32)enter;  i++  ) {
			grund_t const* const gr = welt->lookup( ahead[i] );
			if(  gr  &&  block_post_applies( gr, ribi_type( ahead[i], ahead[i+1] ) )  ) {
				first_post = i;
				break;
			}
		}
	}
	bool follows = false;
	for(  uint32 i=1;  i<=last;  i++  ) {
		grund_t const* const gr = welt->lookup( ahead[i] );
		schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
		if(  sch  &&  !sch->can_reserve( cnv->self )  &&  i>first_post  &&  may_follow( sch->get_reserved_convoi(), enter_boundary )  ) {
			follows = true;
			continue;
		}
		if(  sch==NULL  ||  !sch->can_reserve( cnv->self )  ) {
			sig->set_state( roadsign_t::rot );
			cnv->set_section_wait( convoi_t::SECTION_WAIT_LINE, halthandle_t(), enter_boundary, leave_boundary, ahead[i] );
			restart_speed = 0;
			return false;
		}
	}

	// fork: a faster train coming through this station behind us goes first
	if(  holds_for_overtaker( leave_boundary, next_block )  ) {
		sig->set_state( roadsign_t::rot );
		restart_speed = 0;
		return false;
	}

	// the line is ours, unless another train has waited long for it (see yields_to_waiting)
	halthandle_t waiting_halt;
	if(  yields_to_waiting( leave_boundary, follows, ahead, last, waiting_halt )  ) {
		sig->set_state( roadsign_t::rot );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_YIELD, waiting_halt );
		restart_speed = 0;
		return false;
	}

	if(  enter>=0  ) {
		// the leg that enters the next station; does the train stop there?
		uint32 leg = 0;
		while(  leg_end[leg] < (uint32)enter  ) {
			leg++;
		}
		const uint32 leg_start = leg==0 ? 0 : leg_end[leg-1];
		route_t leg_route;
		for(  uint32 i=leg_start;  i<=leg_end[leg];  i++  ) {
			leg_route.append( ahead[i] );
		}
		const uint32 at = enter - leg_start;
		bool stops = true;
		for(  uint32 i=at+1;  stops  &&  i+1<leg_route.get_count();  i++  ) {
			stops = !is_stop_point( &leg_route, i );
		}
		const uint8 leg_entry = (first_entry + leg) % schedule->get_count();
		// a schedule waypoint ahead after the station boundary stays on the way
		const uint32 keep_to = leg==0 ? cnv->get_last_waypoint_index( leg_route, at ) : INVALID_INDEX;
		const halthandle_t halt = stops ? haltestelle_t::get_halt( leg_route.back(), get_owner() ) : halthandle_t();
		if(  !stops  ||  halt.is_bound()  ) {
			const uint8 needs = stops ? get_platform_needs( schedule->entries[leg_entry], halt ) : 0;
			const koord3d next_stop = stops ? schedule->entries[ (leg_entry+1) % schedule->get_count() ].pos : koord3d::invalid;
			route_t path;
			// fork, coupling: right behind our partner standing there, if we couple at the stop we go to now
			bool standing = false;
			const convoihandle_t partner = stops  &&  leg==0  &&  keep_to==INVALID_INDEX ? cnv->find_partner_at( halt, standing ) : convoihandle_t();
			const bool to_partner = partner.is_bound()  &&  standing  &&  find_partner_track( &leg_route, at, partner, path );
			// fork: a faster train right behind us runs through there: stand off its way if we can
			if(  !to_partner  &&  stops  ) {
				get_station_overtaker_ways( leave_boundary, enter_boundary );
			}
			const bool track_found = to_partner  ||  find_station_track( &leg_route, at, halt, needs, next_stop, path, keep_to );
			overtaker_ways.clear();
			overtaker_routes.clear();
			if(  !track_found  ) {
				grund_t const* const end_gr = welt->lookup( leg_route.back() );
				const halthandle_t full_halt = halt.is_bound() ? halt : (end_gr ? end_gr->get_halt() : halthandle_t());
				sig->set_state( roadsign_t::rot );
				cnv->set_section_wait( convoi_t::SECTION_WAIT_TRACK, full_halt );
				check_section_lock( full_halt, next_block );
				restart_speed = 0;
				return false;
			}
			const uint16 track_start = get_claim_start( path, get_waytype() );
			if(  track_start>0  ) {
				// the last free track there only if the trains there can still get away (3.8);
				// behind our partner we take no track of its own
				grund_t const* const track_gr = welt->lookup( path.at(track_start) );
				const halthandle_t track_halt = track_gr ? track_gr->get_halt() : halthandle_t();
				if(  !to_partner  &&  keeps_last_track( track_halt, path.at(track_start), next_block )  ) {
					sig->set_state( roadsign_t::rot );
					cnv->set_section_wait( convoi_t::SECTION_WAIT_LAST_TRACK, track_halt );
					restart_speed = 0;
					return false;
				}
				cnv->set_claim( path, track_start, stops, schedule->entries[leg_entry].pos );
			}
			if(  leg==0  &&  track_start>0  ) {
				// the current route through the claimed track
				track_search = 3;
				const sint8 via = cnv->route_via_claim( *route, next_block );
				track_search = 0;
				if(  via<0  ) {
					cnv->release_claim( true );
					sig->set_state( roadsign_t::rot );
					restart_speed = 0;
					return false;
				}
			}
		}
	}

	if(  follows  &&  !cnv->has_claim()  ) {
		// a train following another onto the line needs its track there claimed (not a wait for the
		// line: nobody gives way to it)
		halthandle_t enter_halt;
		for(  uint32 k=(uint32)enter+1;  !enter_halt.is_bound()  &&  k<ahead.get_count()  &&  k<(uint32)enter+64;  k++  ) {
			if(  grund_t const* const gr = welt->lookup( ahead[k] )  ) {
				enter_halt = gr->get_halt();
			}
		}
		sig->set_state( roadsign_t::rot );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_TRACK, enter_halt );
		restart_speed = 0;
		return false;
	}
	if(  enter>=0  ) {
		// in the section now: its block posts stop us (reserve only up to the first one)
		cnv->set_section( leave_boundary, enter_boundary );
	}
	// reserve the way to the first stop, or up to the first block post or the station boundary
	if(  !block_reserver( route, next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		cnv->release_claim( true );
		cnv->clear_section();
		sig->set_state( roadsign_t::rot );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_LINE, halthandle_t(), enter_boundary, leave_boundary );
		restart_speed = 0;
		return false;
	}
	cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
	cnv->clear_section_hold();
	sig->set_state( roadsign_t::gruen );
	cnv->set_next_stop_index( min( next_crossing, next_signal ) );
	return true;
}


static bool section_waited_long(const convoi_t *c);


/* fork: entering a station from a single-track line: into the claimed track when the throat is free;
 * a train without a claim chooses a track here
 */
bool rail_vehicle_t::is_station_boundary_clear(uint16 next_block, sint32 &restart_speed)
{
	route_t *route = cnv->access_route();
	uint16 next_signal, next_crossing;
	if(  cnv->has_claim()  &&  cnv->get_claim_boundary()!=route->at(next_block)  ) {
		// claimed for another station: out of date
		cnv->release_claim( true );
	}
	bool stops = true;
	for(  uint32 i=next_block+1;  stops  &&  i+1<route->get_count();  i++  ) {
		stops = !is_stop_point( route, i );
	}
	schedule_t const* const schedule = cnv->get_schedule();
	const halthandle_t halt = stops ? haltestelle_t::get_halt( route->back(), get_owner() ) : halthandle_t();
	// the route runs through the schedule waypoints ahead, those after the boundary stay on the way
	const schedule_entry_t &entry = schedule->entries[ cnv->get_route_entry() ];
	const uint32 keep_to = cnv->get_last_waypoint_index( *route, next_block );

	// fork, coupling: right behind our partner in this station; it may have come after we chose a track
	bool to_partner = false;
	bool standing = false;
	const convoihandle_t partner = halt.is_bound()  &&  keep_to==INVALID_INDEX ? cnv->find_partner_at( halt, standing ) : convoihandle_t();
	if(  partner.is_bound()  &&  standing  ) {
		const koord3d end = cnv->has_claim() ? cnv->get_claim_end() : route->back();
		to_partner = leads_to_convoi( end, partner, get_waytype() );
		if(  !to_partner  ) {
			if(  !cnv->is_waiting()  ) {
				restart_speed = -1;
				return false;
			}
			route_t path;
			if(  find_partner_track( route, next_block, partner, path )  ) {
				cnv->set_claim( path, get_track_start( path, get_waytype() ), true, entry.pos );
				to_partner = true;
			}
		}
	}
	else if(  partner.is_bound()  &&  partner->get_state()==convoi_t::DRIVING  &&  !section_waited_long( cnv )  ) {
		// our partner is still running in: we can follow it once it stands
		cnv->set_section_wait( convoi_t::SECTION_WAIT_ENTRY, halthandle_t() );
		restart_speed = 0;
		return false;
	}

	if(  !cnv->has_claim()  &&  !to_partner  ) {
		if(  !cnv->is_waiting()  ) {
			restart_speed = -1;
			return false;
		}
		if(  !stops  ||  halt.is_bound()  ) {
			const uint8 needs = stops ? get_platform_needs( entry, halt ) : 0;
			const koord3d next_stop = stops ? schedule->entries[ (cnv->get_route_entry()+1) % schedule->get_count() ].pos : koord3d::invalid;
			route_t path;
			if(  !find_station_track( route, next_block, halt, needs, next_stop, path, keep_to )  ) {
				cnv->set_section_wait( convoi_t::SECTION_WAIT_TRACK, halt );
				restart_speed = 0;
				return false;
			}
			const uint16 track_start = get_track_start( path, get_waytype() );
			if(  track_start>0  ) {
				cnv->set_claim( path, track_start, stops, entry.pos );
			}
			else if(  !route_through( route, next_block, path, stops )  ) {
				restart_speed = 0;
				return false;
			}
		}
	}
	if(  cnv->has_claim()  ) {
		if(  !cnv->route_follows_claim( *route, next_block )  &&  !cnv->is_waiting()  ) {
			// the new route needs a route search, that needs a step
			restart_speed = -1;
			return false;
		}
		track_search = 3;
		const sint8 via = cnv->route_via_claim( *route, next_block );
		track_search = 0;
		if(  via<0  ) {
			cnv->release_claim( true );
			restart_speed = 0;
			return false;
		}
	}
	// out of the single-track section: block posts of a line after the station do not cut this
	// reservation short (they count again from the next platform signal)
	const koord3d sec_from = cnv->get_section_from(), sec_to = cnv->get_section_to();
	cnv->clear_section();
	if(  !block_reserver( route, next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		// the throat is in use for a moment
		cnv->set_section( sec_from, sec_to );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_ENTRY, halthandle_t() );
		restart_speed = 0;
		return false;
	}
	// the claimed tiles are part of the reservation now
	cnv->release_claim( false );
	cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
	cnv->set_next_stop_index( min( next_crossing, next_signal ) );
	return true;
}


/* fork: block post on a single-track line. It stops only trains that entered the line at a platform
 * signal (is_stop_point); those hold their track at the next station already, so the next block is
 * all they need. The opposite direction is kept off the whole line at the platform signals.
 */
bool rail_vehicle_t::is_block_post_clear(signal_t *sig, uint16 next_block, sint32 &restart_speed)
{
	uint16 next_signal, next_crossing;
	if(  block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		sig->set_state( roadsign_t::gruen );
		cnv->set_next_stop_index( min( next_crossing, next_signal ) );
		cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
		return true;
	}
	sig->set_state( roadsign_t::rot );
	cnv->set_section_wait( convoi_t::SECTION_WAIT_BLOCK, halthandle_t() );
	restart_speed = 0;
	return false;
}


/* fork: keeping the stations of single-track lines from locking up, see
 * documentation/fork-rail-signalling-plan.md (3.8). A train at a platform signal may take the last
 * free track of the next station only if some train there could leave it again afterwards: its next
 * station has a free track (or is the one we leave), or, one station further, a train there can go
 * to a station with a free track.
 */

// the tracks of a station: its platform rows, each with the plain track on to the next switch or
// station boundary (where its platform signals stand), at most 32 tiles past the platforms
struct station_tracks_t {
	vector_tpl<koord3d> tiles;
	vector_tpl<uint16> track; // track number of each tile
	vector_tpl<bool> bay;     // each track: a bay platform (is_bay_tile), of use only to trains turning back
	uint16 count;
	bool has_through;         // some track is no bay
};

static void get_station_tracks(halthandle_t halt, waytype_t wt, station_tracks_t &st)
{
	st.tiles.clear();
	st.track.clear();
	st.bay.clear();
	st.count = 0;
	st.has_through = false;
	if(  !halt.is_bound()  ) {
		return;
	}
	vector_tpl<koord3d> todo;
	vector_tpl<uint8> todo_off; // tiles past the platform
	FOR( slist_tpl<haltestelle_t::tile_t>, const &tile, halt->get_tiles() ) {
		if(  tile.grund->get_weg( wt )==NULL  ||  st.tiles.is_contained( tile.grund->get_pos() )  ) {
			continue;
		}
		const uint16 t = st.count++;
		const bool bay = is_bay_tile( tile.grund, wt );
		st.bay.append( bay );
		st.has_through |= !bay;
		st.tiles.append( tile.grund->get_pos() );
		st.track.append( t );
		todo.append( tile.grund->get_pos() );
		todo_off.append( 0 );
		while(  !todo.empty()  ) {
			const koord3d pos = todo.pop_back();
			const uint8 off = todo_off.pop_back();
			grund_t const* const gr = world()->lookup( pos );
			weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
			if(  way==NULL  ) {
				continue;
			}
			for(  int r=0;  r<4;  r++  ) {
				grund_t *to;
				if(  (way->get_ribi_unmasked() & ribi_t::nsew[r])==0  ||  !gr->get_neighbour( to, wt, ribi_t::nsew[r] )  ||  st.tiles.is_contained( to->get_pos() )  ) {
					continue;
				}
				uint8 to_off = 0;
				if(  to->get_halt()!=halt  ) {
					weg_t const* const to_way = to->get_weg( wt );
					if(  to_way==NULL  ||  ribi_t::is_threeway( to_way->get_ribi_unmasked() )  ||  rail_vehicle_t::get_station_boundary( to )  ||  off>=32  ) {
						continue;
					}
					to_off = off + 1;
				}
				st.tiles.append( to->get_pos() );
				st.track.append( t );
				todo.append( to->get_pos() );
				todo_off.append( to_off );
			}
		}
	}
}


// tracks of the station with no tile reserved, not counting the one with tile except;
// bays count only with bays set, or when the station has nothing else (a terminus)
static uint16 count_free_tracks(const station_tracks_t &st, waytype_t wt, koord3d except, bool bays)
{
	vector_tpl<bool> busy( st.count );
	for(  uint16 t=0;  t<st.count;  t++  ) {
		busy.append( false );
	}
	for(  uint32 i=0;  i<st.tiles.get_count();  i++  ) {
		if(  st.tiles[i]==except  ) {
			busy[ st.track[i] ] = true;
			continue;
		}
		grund_t const* const gr = world()->lookup( st.tiles[i] );
		schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( wt ) : NULL;
		if(  sch  &&  sch->is_reserved()  ) {
			busy[ st.track[i] ] = true;
		}
	}
	uint16 free_tracks = 0;
	for(  uint16 t=0;  t<st.count;  t++  ) {
		free_tracks += !busy[t]  &&  (bays  ||  !st.bay[t]  ||  !st.has_through);
	}
	return free_tracks;
}


// long enough at a platform signal to look further (and to warn of a lock)
static bool section_waited_long(const convoi_t *c)
{
	const uint32 since = c->get_section_wait_since();
	if(  since==0  ) {
		return false;
	}
	karte_t *const welt = world();
	const sint64 limit = welt->has_calendar() ? welt->calendar_minutes_to_ticks( 30 ) : (sint64)(welt->ticks_per_world_month >> 3);
	return (sint64)(uint32)(welt->get_ticks() - since) > limit;
}


bool rail_vehicle_t::may_follow(convoihandle_t c, koord3d to) const
{
	// into the same station boundary, with its track at the station claimed: then it never waits on
	// the line for a track there
	if(  !c.is_bound()  ||  c==cnv->self  ||  to==koord3d::invalid  ||  c->get_section_to()!=to
		||  !c->has_claim()  ||  c->get_claim_boundary()!=to  ||  c->get_vehicle_count()==0  ) {
		return false;
	}
	if(  c->get_section_from()!=to  ) {
		return true;
	}
	// it turns back at a halt on the line: only once it has turned, i.e. the rest of its route enters
	// the station there (on its way out it comes towards us; a route never turns round within itself,
	// drive_to ends it at a waypoint where it would)
	route_t const* const r = c->get_route();
	for(  uint32 i=max( c->front()->get_route_index(), 1 );  i<r->get_count();  i++  ) {
		if(  r->at(i)==to  ) {
			grund_t const* const gr = welt->lookup( to );
			roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL;
			const ribi_t::ribi dir = i+1<r->get_count() ? ribi_type( r->at(i), r->at(i+1) ) : ribi_type( r->at(i-1), r->at(i) );
			return lt  &&  lt->applies_to( dir );
		}
	}
	return false;
}


/* Fork: the longest waiter gets a single-track line first. A train that could enter the line waits
 * while another one has waited at least block_yield_minutes for it: from the other end (it would
 * enter the station through the boundary we leave by) or from our side (it would leave through it
 * too), and started waiting before us. Without this a train with a short way (turning back at a halt
 * on the line, or following through a block post) can keep a train that needs the whole line out
 * for ever. Only trains that wait for the line itself count (not for a track at the next station: a
 * train that could go might free the track it waits for). A train that has waited
 * block_yield_minutes + 4 hours is no longer waited for (something that does not move holds its way),
 * except that followers never go while a train waits at the other end.
 */
bool rail_vehicle_t::yields_to_waiting(koord3d from, bool follows, const vector_tpl<koord3d> &ahead, uint32 last, halthandle_t &who) const
{
	if(  from==koord3d::invalid  ) {
		return false;
	}
	const uint16 minutes = welt->get_settings().get_block_yield_minutes();
	const bool cal = welt->has_calendar();
	const sint64 limit = minutes==0 ? 0 : cal ? welt->calendar_minutes_to_ticks( minutes ) : (sint64)(welt->ticks_per_world_month >> 4);
	const sint64 window = limit + (cal ? welt->calendar_minutes_to_ticks( 240 ) : (sint64)(welt->ticks_per_world_month >> 1));
	const uint32 now = welt->get_ticks();
	const bool we_wait = cnv->get_section_wait()!=convoi_t::SECTION_WAIT_NONE;
	const uint32 our_since = cnv->get_section_wait_since();
	FOR( vector_tpl<convoihandle_t>, const c, welt->convoys() ) {
		if(  c==cnv->self  ||  c->get_section_wait()!=convoi_t::SECTION_WAIT_LINE  ||  c->get_vehicle_count()==0  ) {
			continue;
		}
		// still standing at its signal (not gone to a depot or elsewhere since)
		const int st = c->get_state();
		if(  st<convoi_t::WAITING_FOR_CLEARANCE  ||  st>convoi_t::CAN_START_TWO_MONTHS  ||  st==convoi_t::SELF_DESTRUCT  ) {
			continue;
		}
		const bool opposite = c->get_section_wait_boundary()==from;
		if(  !opposite  &&  c->get_section_wait_from()!=from  ) {
			continue;
		}
		bool on_our_way = false;
		for(  uint32 i=1;  !on_our_way  &&  i<=last  &&  i<ahead.get_count();  i++  ) {
			on_our_way = ahead[i]==c->get_section_wait_tile();
		}
		if(  !on_our_way  ) {
			// what keeps it waiting is off our way (at its end of the line, or in the throat of our
			// station, e.g. a train coming in over its switch): from our side it must go into the same
			// station (not a train blocked on another branch); and not when that tile is held by us or
			// by a train loading at its platform (it could not go first; a train standing at a signal or
			// block post is about to move on)
			if(  !opposite  &&  !(last<ahead.get_count()  &&  c->get_section_wait_boundary()==ahead[last])  ) {
				continue;
			}
			grund_t const* const gr = welt->lookup( c->get_section_wait_tile() );
			schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
			const convoihandle_t holder = sch ? sch->get_reserved_convoi() : convoihandle_t();
			if(  holder.is_bound()  &&  holder!=c  &&  (holder==cnv->self  ||  holder->get_state()==convoi_t::LOADING)  ) {
				continue;
			}
		}
		const sint64 waited = (sint64)(uint32)(now - c->get_section_wait_since());
		if(  waited < limit  ||  (waited >= window  &&  !(follows  &&  opposite))  ) {
			continue;
		}
		// it waits longer than we do (the same tick: the older convoy)
		const sint32 diff = (sint32)(our_since - c->get_section_wait_since());
		if(  we_wait  &&  !(diff > 0  ||  (diff==0  &&  c.get_id() < cnv->self.get_id()))  ) {
			continue;
		}
		// the station it waits at: where its route starts (its front stands past the platform)
		who = c->get_route()->empty() ? halthandle_t() : haltestelle_t::get_halt( c->get_route()->front(), c->get_owner() );
		if(  !who.is_bound()  ) {
			who = haltestelle_t::get_halt( c->front()->get_pos(), c->get_owner() );
		}
		return true;
	}
	return false;
}


bool rail_vehicle_t::get_section_after(convoi_t *c, halthandle_t at, halthandle_t &next)
{
	next = halthandle_t();
	schedule_t const* const schedule = c->get_schedule();
	if(  c->get_vehicle_count()==0  ||  schedule==NULL  ||  schedule->empty()  ||  c->front()->get_waytype()!=get_waytype()  ) {
		return false;
	}
	rail_vehicle_t *const f = (rail_vehicle_t *)c->front();

	const uint32 now = welt->get_ticks();
	if(  f->section_after_tick!=0  &&  f->section_after_at==at  &&  f->section_after_pos==f->get_pos()  &&  f->section_after_stop==schedule->get_current_stop()
		&&  (uint32)(now - f->section_after_tick) < (welt->ticks_per_world_month >> 4)  ) {
		next = f->section_after_next;
		return f->section_after_found;
	}

	// standing there (maybe short of the platform, in front of its platform signal), or on its way in?
	station_tracks_t st;
	get_station_tracks( at, get_waytype(), st );
	bool is_there = false;
	for(  uint8 i=0;  !is_there  &&  i<c->get_vehicle_count();  i++  ) {
		is_there = st.tiles.is_contained( c->get_vehikel(i)->get_pos() );
	}

	// the route from the train on, then the next legs of its schedule
	vector_tpl<koord3d> ahead;
	route_t const* const r = c->get_route();
	if(  !r->empty()  ) {
		for(  uint32 i = f->get_route_index()>0 ? f->get_route_index()-1 : 0;  i<r->get_count();  i++  ) {
			ahead.append( r->at(i) );
		}
	}
	if(  ahead.empty()  ) {
		ahead.append( f->get_pos() );
	}
	uint8 entry = schedule->get_current_stop();
	{
		// the route leads to the current entry; a train standing there goes on to the next one
		const koord3d goal = schedule->entries[entry].pos;
		const halthandle_t goal_halt = haltestelle_t::get_halt( goal, c->get_owner() );
		if(  ahead.back()==goal  ||  (goal_halt.is_bound()  &&  haltestelle_t::get_halt( ahead.back(), c->get_owner() )==goal_halt)  ) {
			entry = (entry+1) % schedule->get_count();
		}
	}

	const sint32 speed = speed_to_kmh( c->get_min_top_speed() );
	const uint8 old_search = f->track_search;
	uint8 phase = is_there ? 1 : 0; // 0 on the way in, 1 in the station, 2 on the single-track line after it
	bool found = false;
	uint8 legs = 0;
	for(  uint32 i=0;  ;  i++  ) {
		if(  i+1 >= ahead.get_count()  ) {
			// on with the next leg of the schedule
			if(  legs >= schedule->get_count()  ||  ahead.get_count() > 16384  ) {
				break;
			}
			legs++;
			route_t leg;
			f->track_search = 3;
			const bool ok = leg.calc_route( welt, ahead.back(), schedule->entries[entry].pos, f, speed, 8888 )!=route_t::no_route;
			f->track_search = old_search;
			entry = (entry+1) % schedule->get_count();
			if(  !ok  ||  leg.get_count()<2  ) {
				break;
			}
			for(  uint32 k=1;  k<leg.get_count();  k++  ) {
				ahead.append( leg.at(k) );
			}
		}
		grund_t const* const gr = welt->lookup( ahead[i] );
		if(  gr==NULL  ) {
			break;
		}
		const ribi_t::ribi dir = ribi_type( ahead[i], ahead[i+1] );
		roadsign_t const* const lt = get_station_boundary( gr );
		if(  phase==0  ) {
			if(  gr->get_halt()==at  ) {
				phase = 1;
			}
		}
		else if(  phase==1  ) {
			if(  lt  &&  !lt->applies_to( dir )  ) {
				// leaving through the station boundary
				phase = 2;
			}
			else if(  lt  ||  (signal_applies( gr, dir )  &&  !st.tiles.is_contained( ahead[i] ))  ) {
				// a signal after the station tracks (or another station) before any station boundary:
				// no single-track section from here (signals on the station tracks may be passed on the way)
				break;
			}
		}
		else if(  lt  ) {
			if(  lt->applies_to( dir )  ) {
				// entering the next station: the halt of its first platform tile
				for(  uint32 k=i+1;  k<ahead.get_count()  &&  k<i+64;  k++  ) {
					grund_t const* const pgr = welt->lookup( ahead[k] );
					if(  pgr  &&  pgr->get_halt().is_bound()  ) {
						next = pgr->get_halt();
						break;
					}
				}
				found = next.is_bound();
				break;
			}
		}
		else if(  signal_applies( gr, dir )  ) {
			// the line ends at a signal (joins a double track)
			break;
		}
	}

	f->section_after_at = at;
	f->section_after_pos = f->get_pos();
	f->section_after_stop = schedule->get_current_stop();
	f->section_after_tick = now ? now : 1;
	f->section_after_found = found;
	f->section_after_next = next;
	return found;
}


bool rail_vehicle_t::station_can_release(halthandle_t s, int depth, section_check_t &ck)
{
	station_tracks_t st;
	get_station_tracks( s, get_waytype(), st );
	// which train is on (or has reserved) which track; the newcomer on the track it claims
	vector_tpl<convoihandle_t> occ_train;
	vector_tpl<uint16> occ_track;
	for(  uint32 i=0;  i<st.tiles.get_count();  i++  ) {
		convoihandle_t c;
		if(  st.tiles[i]==ck.claimed  &&  s==ck.full  ) {
			c = ck.newcomer;
		}
		else {
			grund_t const* const gr = welt->lookup( st.tiles[i] );
			schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
			c = sch ? sch->get_reserved_convoi() : convoihandle_t();
		}
		if(  !c.is_bound()  ) {
			continue;
		}
		bool known = false;
		for(  uint32 k=0;  !known  &&  k<occ_train.get_count();  k++  ) {
			known = occ_train[k]==c  &&  occ_track[k]==st.track[i];
		}
		if(  !known  ) {
			occ_train.append( c );
			occ_track.append( st.track[i] );
		}
	}
	if(  occ_train.empty()  ) {
		return true;
	}
	vector_tpl<convoihandle_t> done;
	for(  uint32 k=0;  k<occ_train.get_count();  k++  ) {
		const convoihandle_t c = occ_train[k];
		if(  done.is_contained( c )  ) {
			continue;
		}
		// only a train alone on a track frees one by leaving
		bool alone = false;
		for(  uint32 j=0;  !alone  &&  j<occ_train.get_count();  j++  ) {
			if(  occ_train[j]==c  ) {
				alone = true;
				for(  uint32 m=0;  alone  &&  m<occ_train.get_count();  m++  ) {
					alone = occ_track[m]!=occ_track[j]  ||  occ_train[m]==c;
				}
			}
		}
		done.append( c );
		if(  !alone  ) {
			continue;
		}
		if(  c!=ck.newcomer  ) {
			// on its way out already: it has its track at the next station
			bool is_there = false;
			for(  uint8 v=0;  !is_there  &&  v<c->get_vehicle_count();  v++  ) {
				is_there = st.tiles.is_contained( c->get_vehikel(v)->get_pos() );
			}
			bool claim_elsewhere = c->has_claim();
			for(  uint32 i=0;  claim_elsewhere  &&  i<st.tiles.get_count();  i++  ) {
				claim_elsewhere = !c->is_claimed_tile( st.tiles[i] );
			}
			if(  is_there  &&  claim_elsewhere  ) {
				return true;
			}
		}
		halthandle_t next;
		const bool found = get_section_after( c.get_rep(), s, next );
		if(  !found  ||  next==s  ||  next==ck.freed  ) {
			// leaves without a single-track section (or we cannot tell), or to where a track gets free
			return true;
		}
		if(  next!=ck.full  ) {
			station_tracks_t nt;
			get_station_tracks( next, get_waytype(), nt );
			if(  count_free_tracks( nt, get_waytype(), koord3d::invalid, false )>0  ) {
				return true;
			}
			// only bays free there: of use to it only if it turns back there (to come back here)
			halthandle_t after;
			if(  count_free_tracks( nt, get_waytype(), koord3d::invalid, true )>0  &&  get_section_after( c.get_rep(), next, after )  &&  after==s  ) {
				return true;
			}
		}
		if(  depth>0  &&  !ck.visited.is_contained( next )  ) {
			ck.visited.append( next );
			if(  station_can_release( next, depth-1, ck )  ) {
				return true;
			}
		}
	}
	return false;
}


halthandle_t rail_vehicle_t::get_section_origin(uint16 next_block) const
{
	route_t const* const route = cnv->get_route();
	for(  uint32 i=min( (uint32)next_block, route->get_count() );  i>0;  i--  ) {
		grund_t const* const gr = welt->lookup( route->at(i-1) );
		if(  gr==NULL  ||  get_station_boundary( gr )  ||  next_block-i > 64  ) {
			break;
		}
		if(  gr->get_halt().is_bound()  ) {
			return gr->get_halt();
		}
	}
	return halthandle_t();
}


bool rail_vehicle_t::keeps_last_track(halthandle_t x, koord3d claimed_tile, uint16 next_block)
{
	if(  !x.is_bound()  ) {
		return false;
	}
	station_tracks_t st;
	get_station_tracks( x, get_waytype(), st );
	// a free bay is of no use to a train that runs through, so only other tracks count here
	if(  count_free_tracks( st, get_waytype(), claimed_tile, false )>0  ) {
		return false;
	}
	section_check_t ck;
	ck.full = x;
	ck.freed = get_section_origin( next_block );
	ck.newcomer = cnv->self;
	ck.claimed = claimed_tile;
	ck.visited.append( x );
	if(  ck.freed==x  ) {
		return false;
	}
	if(  ck.freed.is_bound()  ) {
		// our track there gets free only if no other train stands on it or has it reserved
		station_tracks_t ost;
		get_station_tracks( ck.freed, get_waytype(), ost );
		vector_tpl<uint16> ours;
		for(  uint8 v=0;  v<cnv->get_vehicle_count();  v++  ) {
			for(  uint32 i=0;  i<ost.tiles.get_count();  i++  ) {
				if(  ost.tiles[i]==cnv->get_vehikel(v)->get_pos()  ) {
					ours.append_unique( ost.track[i] );
				}
			}
		}
		bool shared = ours.empty();
		for(  uint32 i=0;  !shared  &&  i<ost.tiles.get_count();  i++  ) {
			if(  ours.is_contained( ost.track[i] )  ) {
				grund_t const* const gr = welt->lookup( ost.tiles[i] );
				schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
				shared = sch  &&  sch->is_reserved()  &&  sch->get_reserved_convoi()!=cnv->self;
			}
		}
		if(  shared  ) {
			ck.freed = halthandle_t();
		}
	}
	// two stations ahead; after a long wait all of them, so this rule never holds everybody up by itself
	return !station_can_release( x, section_waited_long( cnv ) ? 64 : 1, ck );
}


void rail_vehicle_t::check_section_lock(halthandle_t x, uint16 next_block)
{
	if(  !x.is_bound()  ||  cnv->is_section_lock_warned()  ||  !section_waited_long( cnv )  ) {
		return;
	}
	cnv->set_section_lock_warned();
	section_check_t ck;
	ck.claimed = koord3d::invalid;
	ck.visited.append( x );
	if(  station_can_release( x, 64, ck )  ) {
		// the trains there can leave: a wait, not a lock
		return;
	}
	const halthandle_t origin = get_section_origin( next_block );
	if(  origin.is_bound()  ) {
		ck.visited.append_unique( origin );
	}
	// one message per lock: the other trains in it find the same stations
	static vector_tpl<halthandle_t> warned_halts;
	static uint32 warned_tick = 0;
	if(  warned_tick!=0  &&  (uint32)(welt->get_ticks() - warned_tick) < welt->ticks_per_world_month  ) {
		FOR( vector_tpl<halthandle_t>, const h, ck.visited ) {
			if(  warned_halts.is_contained( h )  ) {
				return;
			}
		}
	}
	warned_halts.clear();
	cbuffer_t names;
	FOR( vector_tpl<halthandle_t>, const h, ck.visited ) {
		warned_halts.append( h );
		if(  names.len()>0  ) {
			names.append( ", " );
		}
		names.append( h->get_name() );
	}
	warned_tick = welt->get_ticks() ? welt->get_ticks() : 1;
	cbuffer_t buf;
	buf.printf( translator::translate("Trains are locked up at %s: these stations are full and every train there waits for a track at another of them."), names.get_str() );
	welt->get_message()->add_message( buf, x->get_basis_pos(), message_t::warnings, PLAYER_FLAG|get_owner()->get_player_nr(), IMG_EMPTY );
}


// station flags (haltestelle_t::PAX, POST, WARE) of the platform building at this tile; all without one
static uint8 get_platform_enables(const grund_t *gr)
{
	const gebaeude_t *gb = gr->find<gebaeude_t>();
	const uint8 enables = gb ? gb->get_tile()->get_desc()->get_enabled() : 0;
	return enables ? enables : (uint8)(haltestelle_t::PAX | haltestelle_t::POST | haltestelle_t::WARE);
}


uint8 rail_vehicle_t::get_platform_needs(const schedule_entry_t &entry, halthandle_t halt) const
{
	if(  entry.stop_type == schedule_entry_t::hold  ||  !halt.is_bound()  ) {
		// nothing is loaded here: any platform will do
		return 0;
	}
	// passengers and mail need a passenger platform, everything else a freight platform
	uint8 needs = 0;
	for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) {
		const vehicle_t *v = cnv->get_vehikel(i);
		if(  v->get_cargo_max() > 0  ) {
			const goods_desc_t *cargo = v->get_cargo_type();
			needs |= (cargo == goods_manager_t::passengers  ||  cargo == goods_manager_t::mail) ? (haltestelle_t::PAX | haltestelle_t::POST) : haltestelle_t::WARE;
		}
	}
	if(  needs  ) {
		// a station without such a platform long enough for us: take any rather than wait forever
		const uint16 length = cnv->get_tile_length();
		FOR( slist_tpl<haltestelle_t::tile_t>, const &tile, halt->get_tiles() ) {
			weg_t const* const way = tile.grund->get_weg( get_waytype() );
			if(  way==NULL  ||  (get_platform_enables( tile.grund ) & needs)==0  ) {
				continue;
			}
			// suitable tiles of this halt in a row along the track, through this tile
			uint16 run = 1;
			for(  int r=0;  r<4  &&  run<length;  r++  ) {
				if(  (way->get_ribi_unmasked() & ribi_t::nsew[r])==0  ) {
					continue;
				}
				const grund_t *gr = tile.grund;
				grund_t *to;
				while(  run<length  &&  gr->get_neighbour( to, get_waytype(), ribi_t::nsew[r] )  &&  to->get_halt()==halt  &&  (get_platform_enables( to ) & needs)  ) {
					run ++;
					gr = to;
				}
			}
			if(  run >= length  ) {
				return needs;
			}
		}
	}
	return 0;
}


bool rail_vehicle_t::is_platform_suitable(const grund_t *gr) const
{
	if(  platform_needs != 0  &&  (get_platform_enables( gr ) & platform_needs) == 0  ) {
		return false;
	}
	// fork: bay platforms
	return bay_search == 0  ||  bay_search_tiles.is_contained( gr->get_pos() ) == (bay_search == 2);
}


bool rail_vehicle_t::setup_bay_search(const route_t *route, uint32 start, halthandle_t halt, koord3d next_stop, bool station_search)
{
	bay_search = 0;
	bool has_through;
	get_bay_tiles( halt, get_waytype(), bay_search_tiles, has_through );
	if(  bay_search_tiles.empty()  ) {
		return false;
	}
	if(  reverses_at_stop( route, start, next_stop )  ) {
		// any platform, bays first
		return true;
	}
	// no bay, unless there is nothing else (a terminus)
	bay_search = has_through ? 1 : 0;
	if(  bay_search==1  &&  start+1<route->get_count()  &&  bay_search_tiles.is_contained( route->back() )  ) {
		// planned into a bay: nor when no other platform can be reached from here at all, taken or not
		// and however short: the bays are all we can use, rather than wait for good
		stop_any_length = true;
		if(  !can_enter_without_turning( route->at(start), ribi_type( route->at(start), route->at(start+1) ), halt, station_search )  ) {
			bay_search = 0;
		}
		stop_any_length = false;
	}
	return false;
}


bool rail_vehicle_t::can_enter_without_turning(koord3d start, ribi_t::ribi dir, halthandle_t halt, bool station_search)
{
	// every stop position of halt, taken or not; the one found first each time is excluded, so this
	// ends after at most as many tries as stop positions
	const sint32 speed = speed_to_kmh( cnv->get_min_top_speed() );
	const uint8 old_search = track_search;
	bool found = false;
	turn_probe = true;
	// first never on out of a platform of halt (see stop_search_halt), then as before
	stop_search_start = start;
	for(  uint8 pass=0;  !found  &&  pass<2;  pass++  ) {
		track_search_excluded.clear();
		for(  uint16 attempt=0;  !found  &&  attempt<256;  attempt++  ) {
			route_t probe;
			if(  station_search  ) {
				track_search = 1;
				track_search_halt = halt;
				track_search_start = start;
				track_search_block = false;
			}
			stop_search_halt = pass==0 ? halt : halthandle_t();
			const bool ok = probe.find_route( welt, start, this, speed, dir, welt->get_settings().get_max_choose_route_steps() );
			stop_search_halt = halthandle_t();
			track_search = old_search;
			if(  !ok  ||  probe.get_count()<2  ) {
				break;
			}
			found = !turns_round_through( probe, 0, halt );
			if(  !found  ) {
				track_search_excluded.append( probe.back() );
			}
		}
	}
	stop_search_start = koord3d::invalid;
	track_search_excluded.clear();
	turn_probe = false;
	if(  station_search  ) {
		track_search_start = koord3d::invalid;
	}
	return found;
}


bool rail_vehicle_t::reverses_at_stop(const route_t *route, uint32 start, koord3d next_stop)
{
	const uint32 n = route->get_count();
	if(  next_stop==koord3d::invalid  ||  n<2  ||  start+1>=n  ) {
		return true;
	}
	const sint32 speed = speed_to_kmh( cnv->get_min_top_speed() );
	const uint8 old_search = track_search;
	track_search = 3;
	bool reverses = true;
	route_t on;
	if(  on.calc_route( welt, route->back(), next_stop, this, speed, 0 )!=route_t::no_route  &&  on.get_count()>=2  ) {
		// the way on starts back the way we came
		reverses = on.at(1)==route->at(n-2);
		if(  reverses  ) {
			// maybe only because this platform leads nowhere else (a bay): does the way on from the last
			// switch before it go on without turning back through another track of the station? (Not
			// when it leaves the station at once, e.g. a train coming out of a depot on a spur there.)
			grund_t const* const stop_gr = welt->lookup( route->back() );
			const halthandle_t halt = stop_gr ? stop_gr->get_halt() : halthandle_t();
			for(  uint32 i=n-2;  i>start;  i--  ) {
				grund_t const* const gr = welt->lookup( route->at(i) );
				weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
				if(  way  &&  ribi_t::is_threeway( way->get_ribi_unmasked() )  ) {
					route_t from_switch;
					if(  from_switch.calc_route( welt, route->at(i), next_stop, this, speed, 0 )!=route_t::no_route  &&  from_switch.get_count()>=2  ) {
						reverses = from_switch.at(1)==route->at(i-1)  ||  !passes_platform_of( from_switch, halt, get_waytype() );
					}
					break;
				}
			}
		}
	}
	track_search = old_search;
	return reverses;
}


bool rail_vehicle_t::is_planned_platform_suitable() const
{
	route_t const* const route = cnv->get_route();
	uint16 tiles = cnv->get_tile_length();
	for(  uint32 idx=route->get_count();  idx>0  &&  tiles>0;  idx--, tiles--  ) {
		grund_t const* const gr = welt->lookup( route->at(idx-1) );
		if(  gr==NULL  ||  gr->get_halt()!=target_halt  ) {
			break;
		}
		if(  !is_platform_suitable( gr )  ) {
			return false;
		}
	}
	return true;
}


// the convoi carries passengers or mail
static bool carries_passengers(const convoi_t *c)
{
	for(  uint8 i=0;  i<c->get_vehicle_count();  i++  ) {
		const vehicle_t *v = c->get_vehikel(i);
		if(  v->get_cargo_max() > 0  &&  (v->get_cargo_type() == goods_manager_t::passengers  ||  v->get_cargo_type() == goods_manager_t::mail)  ) {
			return true;
		}
	}
	return false;
}


// the signal at this tile is a choose signal
static bool has_choose_signal(const grund_t *gr, const weg_t *way)
{
	if(  way->has_signal()  ) {
		signal_t const* const sig = gr->find<signal_t>(1);
		return sig  &&  sig->get_desc()->is_choose_sign();
	}
	return false;
}


// the sign at this tile ends a choose area for this waytype
static bool has_end_of_choose(const grund_t *gr, const weg_t *way)
{
	if(  way->has_sign()  ) {
		roadsign_t const* const rs = gr->find<roadsign_t>(1);
		return rs  &&  rs->get_desc()->get_wtyp()==way->get_waytype()  &&  (rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA);
	}
	return false;
}


convoihandle_t rail_vehicle_t::get_passing_train(const uint32 end_of_choose, const halthandle_t halt, const koord3d behind, bool &stuck) const
{
	stuck = false;
	const sint64 window = welt->calendar_minutes_to_ticks( welt->get_settings().get_passing_hold_minutes() );
	route_t const* const route = cnv->get_route();
	if(  window <= 0  ||  end_of_choose+1 >= route->get_count()  ) {
		return convoihandle_t();
	}
	const koord3d eoc_pos = route->at( end_of_choose );
	const koord3d eoc_next = route->at( end_of_choose+1 );
	const bool we_carry_passengers = carries_passengers( cnv );
	// fork: where we stand (a train entering through a station boundary must not need these tiles)
	vector_tpl<koord3d> our_tiles;
	for(  uint8 i=0;  halt.is_bound()  &&  i<cnv->get_vehicle_count();  i++  ) {
		our_tiles.append_unique( cnv->get_vehikel(i)->get_pos() );
	}

	convoihandle_t passing;
	sint64 passing_eta = window + 1;
	FOR( vector_tpl<convoihandle_t>, const other, welt->convoys() ) {
		if(  other == cnv->self  ||  other->get_vehicle_count()==0  ||  other->in_depot()  ) {
			continue;
		}
		vehicle_t const* const front = other->front();
		route_t const* const r = other->get_route();
		if(  front->get_waytype() != get_waytype()  ||  r->empty()  ) {
			continue;
		}
		// far away trains cannot matter: a route is never shorter than the distance
		const sint64 speed = max( other->get_min_top_speed(), 1 );
		const sint64 reach = max( (window * speed) >> 20, (sint64)256 );
		if(  koord_distance( front->get_pos(), eoc_pos ) > reach  ) {
			continue;
		}
		const uint32 from = max( front->get_route_index(), 1u ) - 1;

		// waiting at red at a choose signal into this station (for a platform or a way through)?
		const int st = other->get_state();
		if(  halt.is_bound()  &&  (st==convoi_t::WAITING_FOR_CLEARANCE  ||  st==convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH  ||  st==convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS)  &&  other->get_akt_speed()==0  ) {
			const uint32 sig_idx = other->get_next_stop_index() - 1u;
			grund_t const* const sig_gr = sig_idx < r->get_count() ? welt->lookup( r->at(sig_idx) ) : NULL;
			weg_t const* const sig_way = sig_gr ? sig_gr->get_weg( get_waytype() ) : NULL;
			if(  sig_way  &&  has_choose_signal( sig_gr, sig_way )  ) {
				for(  uint32 i=sig_idx+1;  i<r->get_count();  i++  ) {
					grund_t const* const gr = welt->lookup( r->at(i) );
					weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
					if(  way==NULL  ) {
						break;
					}
					if(  r->at(i)==eoc_pos  ||  gr->get_halt()==halt  ) {
						stuck = true;
						return convoihandle_t();
					}
					if(  has_end_of_choose( gr, way )  ) {
						break;
					}
				}
			}
		}

		// fork: waiting for a track at this station (at a station boundary, or at the platform signal
		// of the station before): it cannot get in either
		if(  halt.is_bound()  &&  other->get_akt_speed()==0  &&  other->get_section_wait_halt()==halt  ) {
			const uint8 sw = other->get_section_wait();
			if(  sw==convoi_t::SECTION_WAIT_TRACK  ||  sw==convoi_t::SECTION_WAIT_ENTRY  ||  sw==convoi_t::SECTION_WAIT_LAST_TRACK  ) {
				stuck = true;
				return convoihandle_t();
			}
		}

		if(  we_carry_passengers  &&  !carries_passengers( other.get_rep() )  ) {
			continue;
		}
		// runs on past our end of choose in our direction, so it does not stop here
		uint32 ix = INVALID_INDEX;
		for(  uint32 i=from;  i+1 < r->get_count();  i++  ) {
			if(  r->at(i)==eoc_pos  &&  r->at(i+1)==eoc_next  ) {
				ix = i;
				break;
			}
		}
		if(  ix==INVALID_INDEX  ) {
			continue;
		}
		// and enters the area through a choose signal, so it can get round us
		// (fork: or through a station boundary, see below)
		uint32 choose_idx = INVALID_INDEX;
		bool via_boundary = false;
		for(  uint32 i=ix;  i>0  &&  choose_idx==INVALID_INDEX;  i--  ) {
			grund_t const* const gr = welt->lookup( r->at(i-1) );
			weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
			if(  way==NULL  ||  has_end_of_choose( gr, way )  ) {
				break;
			}
			if(  has_choose_signal( gr, way )  ) {
				choose_idx = i-1;
			}
			else if(  roadsign_t const* const lt = get_station_boundary( gr )  ) {
				if(  !lt->applies_to( ribi_type( r->at(i-1), r->at(i) ) )  ) {
					// the way out of another station
					break;
				}
				choose_idx = i-1;
				via_boundary = true;
			}
		}
		if(  choose_idx==INVALID_INDEX  ) {
			continue;
		}
		if(  via_boundary  ) {
			// fork: through a station boundary (only for a train waiting at a stop): with a track claimed
			// there (it got green at the station before for a free track here), let in there (its way
			// in reserved) or already past it, so it comes and never waits for a track; and its way to
			// our end of choose must not run over the tiles we stand on (it chose its track at the
			// boundary, nothing gets it round us later)
			if(  !halt.is_bound()  ) {
				continue;
			}
			bool comes = from > choose_idx  ||  other->get_claim_boundary()==r->at(choose_idx);
			if(  !comes  ) {
				grund_t const* const lt_gr = welt->lookup( r->at(choose_idx) );
				schiene_t const* const lt_way = lt_gr ? (schiene_t const*)lt_gr->get_weg( get_waytype() ) : NULL;
				comes = lt_way  &&  lt_way->get_reserved_convoi()==other;
			}
			if(  !comes  ) {
				continue;
			}
			bool over_us = false;
			for(  uint32 i=from;  !over_us  &&  i<ix;  i++  ) {
				over_us = our_tiles.is_contained( r->at(i) );
			}
			if(  over_us  ) {
				continue;
			}
		}
		if(  behind!=koord3d::invalid  ) {
			// it must still come by where we are
			bool behind_us = false;
			for(  uint32 i=from;  i<ix  &&  !behind_us;  i++  ) {
				behind_us = r->at(i)==behind;
			}
			if(  !behind_us  ) {
				continue;
			}
		}
		if(  from > choose_idx  &&  (other->is_standing()  ||  st==convoi_t::CAN_START  ||  st==convoi_t::CAN_START_ONE_MONTH  ||  st==convoi_t::CAN_START_TWO_MONTHS)  ) {
			// stands at a stop in this area itself: it is no passing train
			continue;
		}
		const sint64 eta = ( (sint64)(ix - from) << (8+12) ) / speed;
		if(  eta <= window  &&  eta < passing_eta  ) {
			passing = other;
			passing_eta = eta;
		}
	}
	return passing;
}


bool rail_vehicle_t::is_held_for_passing_train()
{
	if(  cnv->is_passing_hold_released()  ) {
		return false;
	}
	// only at a stop inside a choose area: its end of choose lies ahead of us before any choose signal
	grund_t const* const here = welt->lookup( get_pos() );
	const halthandle_t halt = here ? here->get_halt() : halthandle_t();
	route_t const* const route = cnv->get_route();
	uint32 end_of_choose = INVALID_INDEX;
	uint8 signals_passed = 0;
	for(  uint32 idx = max( route_index, 1u ) - 1;  halt.is_bound()  &&  end_of_choose==INVALID_INDEX  &&  idx+1 < route->get_count();  idx++  ) {
		grund_t const* const gr = welt->lookup( route->at(idx) );
		weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
		if(  way==NULL  ||  has_choose_signal( gr, way )  ||  get_station_boundary( gr )  ) {
			// fork: nor across a station boundary (single-track lines have no end of choose; holding
			// there would hand the line to trains coming the other way)
			break;
		}
		if(  is_stop_point( route, idx )  &&  ++signals_passed > 1  ) {
			// fork: only the end of choose right after our own exit signal counts
			break;
		}
		if(  has_end_of_choose( gr, way )  ) {
			end_of_choose = idx;
		}
	}
	bool stuck = false;
	const convoihandle_t passing = end_of_choose!=INVALID_INDEX ? get_passing_train( end_of_choose, halt, koord3d::invalid, stuck ) : convoihandle_t();
	if(  stuck  ) {
		// a train cannot get into the station: waiting would block it, so leave
		cnv->release_passing_hold();
		return false;
	}
	if(  !passing.is_bound()  ) {
		cnv->set_passing_hold( convoihandle_t(), 0 );
		return false;
	}
	const uint32 now = welt->get_ticks();
	if(  !cnv->get_passing_hold_for().is_bound()  ) {
		cnv->set_passing_hold( passing, now );
	}
	else if(  (sint64)(now - cnv->get_passing_hold_since()) > welt->calendar_minutes_to_ticks( welt->get_settings().get_passing_hold_max_minutes() )  ) {
		// waited long enough at this stop
		cnv->release_passing_hold();
		return false;
	}
	else {
		cnv->set_passing_hold( passing, cnv->get_passing_hold_since() );
	}
	// fork: not waiting for the line while held (else a train at the signal ahead would let us go first)
	cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
	return true;
}


convoihandle_t rail_vehicle_t::get_section_overtaker(const koord3d leave_boundary, bool &stuck, const bool any_way) const
{
	stuck = false;
	route_t const* const route = cnv->get_route();
	if(  route->empty()  ) {
		return convoihandle_t();
	}
	const halthandle_t our_next = haltestelle_t::get_halt( route->back(), get_owner() );
	const sint32 our_speed = speed_to_kmh( cnv->get_min_top_speed() );
	const bool we_carry_passengers = carries_passengers( cnv );
	const koord3d our_pos = get_pos();

	convoihandle_t best;
	uint32 best_dist = 0xFFFFFFFFul;
	FOR( vector_tpl<convoihandle_t>, const other, welt->convoys() ) {
		if(  other == cnv->self  ||  other->get_vehicle_count()==0  ||  other->in_depot()  ) {
			continue;
		}
		const int st = other->get_state();
		if(  st!=convoi_t::DRIVING  &&  st!=convoi_t::WAITING_FOR_CLEARANCE  &&  st!=convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH  &&  st!=convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS  ) {
			// standing at a stop, coupled, in or out of a depot: no train running through
			continue;
		}
		vehicle_t const* const front = other->front();
		route_t const* const r = other->get_route();
		if(  front->get_waytype() != get_waytype()  ||  r->get_count() < 2  ||  koord_distance( front->get_pos(), our_pos ) > 256  ) {
			continue;
		}
		if(  we_carry_passengers  &&  !carries_passengers( other.get_rep() )  ) {
			continue;
		}
		const sint32 other_speed = speed_to_kmh( other->get_min_top_speed() );
		if(  other_speed < our_speed  ) {
			continue;
		}
		const uint32 from = max( front->get_route_index(), 1u ) - 1;
		// it leaves through our exit
		uint32 j = INVALID_INDEX;
		for(  uint32 i=from;  i+1 < r->get_count()  &&  i < from+512;  i++  ) {
			if(  r->at(i)==leave_boundary  ) {
				grund_t const* const gr = welt->lookup( r->at(i) );
				roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL;
				if(  lt  &&  !lt->applies_to( ribi_type( r->at(i), r->at(i+1) ) )  ) {
					j = i;
				}
				break;
			}
		}
		if(  j==INVALID_INDEX  ) {
			continue;
		}
		// the station boundary it enters our station through (none behind it: it is in the station)
		uint32 in = INVALID_INDEX;
		bool other_station = false;
		for(  uint32 k=j;  k>from;  k--  ) {
			grund_t const* const gr = welt->lookup( r->at(k-1) );
			if(  roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL  ) {
				if(  lt->applies_to( ribi_type( r->at(k-1), r->at(k) ) )  ) {
					in = k-1;
				}
				else {
					other_station = true;
				}
				break;
			}
		}
		if(  other_station  ||  (in==INVALID_INDEX  &&  j-from > 128)  ) {
			continue;
		}
		bool waits_for_line = false;
		if(  in!=INVALID_INDEX  ) {
			// at most at the station before ours: no station boundary into another station before
			bool farther = false;
			for(  uint32 k=from+1;  k<in  &&  !farther;  k++  ) {
				grund_t const* const gr = welt->lookup( r->at(k) );
				roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL;
				farther = lt  &&  lt->applies_to( ribi_type( r->at(k), r->at(k+1) ) );
			}
			if(  farther  ) {
				continue;
			}
			const uint8 sw = other->get_section_wait();
			if(  other->get_akt_speed()==0  &&  (sw==convoi_t::SECTION_WAIT_TRACK  ||  sw==convoi_t::SECTION_WAIT_LAST_TRACK)  ) {
				// it finds no track in our station: waiting for it would only block it
				stuck = true;
				return convoihandle_t();
			}
			bool comes = other->get_claim_boundary()==r->at(in);
			if(  !comes  ) {
				grund_t const* const gr = welt->lookup( r->at(in) );
				schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
				comes = sch  &&  sch->get_reserved_convoi()==other;
			}
			if(  !comes  &&  sw==convoi_t::SECTION_WAIT_LINE  &&  other->get_akt_speed()==0  &&  other->get_section_wait_boundary()==r->at(in)  ) {
				// red at the platform signal before only while the line behind us clears: it claims a
				// track here at its next step (unless what stops it is another train)
				grund_t const* const gr = welt->lookup( other->get_section_wait_tile() );
				schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
				const convoihandle_t holder = sch ? sch->get_reserved_convoi() : convoihandle_t();
				comes = waits_for_line = !holder.is_bound()  ||  holder==cnv->self;
			}
			if(  !comes  ) {
				continue;
			}
		}
		// worth it: faster, or it runs through our next stop (it does not stop there, we do)
		bool skips = false;
		if(  our_next.is_bound()  ) {
			for(  uint32 k=j;  k<r->get_count();  k++  ) {
				grund_t const* const gr = welt->lookup( r->at(k) );
				if(  gr  &&  gr->get_halt()==our_next  ) {
					for(  uint32 m=k+1;  m<r->get_count()  &&  !skips;  m++  ) {
						grund_t const* const gr2 = welt->lookup( r->at(m) );
						skips = gr2  &&  gr2->get_halt()!=our_next;
					}
					break;
				}
			}
		}
		if(  !skips  &&  other_speed*100 < our_speed*115  ) {
			continue;
		}
		// its way out must not need a tile we hold (a train still at the station before chooses a free
		// track when it claims one)
		if(  !waits_for_line  &&  !any_way  ) {
			bool needs_ours = false;
			for(  uint32 k=(in==INVALID_INDEX ? from : in)+1;  k<=j  &&  !needs_ours;  k++  ) {
				grund_t const* const gr = welt->lookup( r->at(k) );
				schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( get_waytype() ) : NULL;
				needs_ours = sch  &&  sch->get_reserved_convoi()==cnv->self;
			}
			if(  needs_ours  ) {
				continue;
			}
		}
		if(  j-from < best_dist  ) {
			best = other;
			best_dist = j-from;
		}
	}
	return best;
}


bool rail_vehicle_t::get_station_overtaker_ways(const koord3d leave_boundary, const koord3d enter_boundary)
{
	overtaker_ways.clear();
	overtaker_routes.clear();
	if(  !welt->has_calendar()  ||  leave_boundary==koord3d::invalid  ||  enter_boundary==koord3d::invalid  ) {
		return false;
	}
	bool stuck = false;
	const convoihandle_t overtaker = get_section_overtaker( leave_boundary, stuck, true );
	if(  stuck  ||  !overtaker.is_bound()  ) {
		return false;
	}
	route_t const* const r = overtaker->get_route();
	const uint32 from = max( overtaker->front()->get_route_index(), 1u ) - 1;
	// into that station through its boundary, and out of it through another one, without stopping there
	uint32 in = INVALID_INDEX, out = INVALID_INDEX;
	for(  uint32 i=from;  i+1 < r->get_count()  &&  i < from+1024;  i++  ) {
		grund_t const* const gr = welt->lookup( r->at(i) );
		roadsign_t const* const lt = gr ? get_station_boundary( gr ) : NULL;
		if(  lt==NULL  ) {
			continue;
		}
		const bool applies = lt->applies_to( ribi_type( r->at(i), r->at(i+1) ) );
		if(  in==INVALID_INDEX  ) {
			if(  r->at(i)==enter_boundary  &&  applies  ) {
				in = i;
			}
		}
		else if(  !applies  ) {
			out = i;
			break;
		}
		else {
			// into yet another station: no way through ours
			break;
		}
	}
	if(  in==INVALID_INDEX  ||  out==INVALID_INDEX  ) {
		return false;
	}
	for(  uint32 i=in;  i<=out;  i++  ) {
		overtaker_ways.append_unique( r->at(i) );
	}
	for(  uint32 i=out+1;  i<r->get_count()  &&  i<=out+OVERTAKER_WALK_TILES;  i++  ) {
		overtaker_routes.append_unique( r->at(i) );
	}
	return true;
}


bool rail_vehicle_t::holds_for_overtaker(const koord3d leave_boundary, const uint16 next_block)
{
	if(  !welt->has_calendar()  ||  leave_boundary==koord3d::invalid  ||  cnv->is_section_hold_released()  ) {
		return false;
	}
	// only a train that stopped in this station (its route runs from the stop to here without a station
	// boundary), or one marked Hold
	route_t const* const route = cnv->get_route();
	bool may_hold = cnv->is_hold_marked();
	if(  !may_hold  &&  !route->empty()  ) {
		may_hold = haltestelle_t::get_halt( route->front(), get_owner() ).is_bound();
		for(  uint32 i=1;  may_hold  &&  i<=next_block  &&  i<route->get_count();  i++  ) {
			grund_t const* const gr = welt->lookup( route->at(i) );
			may_hold = gr==NULL  ||  get_station_boundary( gr )==NULL;
		}
	}
	if(  !may_hold  ) {
		return false;
	}
	bool stuck = false;
	const convoihandle_t overtaker = get_section_overtaker( leave_boundary, stuck );
	if(  stuck  ) {
		cnv->release_section_hold();
		return false;
	}
	if(  !overtaker.is_bound()  ) {
		// keep the clock: several trains in turn make us wait no longer in all
		cnv->set_section_hold( convoihandle_t(), cnv->get_section_hold_since() );
		return false;
	}
	const uint32 now = welt->get_ticks();
	const uint32 since = cnv->get_section_hold_since()!=0 ? cnv->get_section_hold_since() : (now ? now : 1);
	if(  (sint64)(uint32)(now - since) > welt->calendar_minutes_to_ticks( welt->get_settings().get_passing_hold_max_minutes() )  ) {
		cnv->release_section_hold();
		return false;
	}
	cnv->set_section_hold( overtaker, since );
	cnv->set_section_wait( convoi_t::SECTION_WAIT_HOLD, halthandle_t() );
	return true;
}


bool rail_vehicle_t::get_overtaker_ways(const uint16 start_block, vector_tpl<koord3d> &way, vector_tpl<koord3d> &onward) const
{
	way.clear();
	onward.clear();
	route_t const* const route = cnv->get_route();
	if(  !welt->has_calendar()  ||  (uint32)start_block+1 >= route->get_count()  ) {
		return false;
	}
	const koord3d sig_pos = route->at( start_block );
	const koord3d sig_next = route->at( start_block+1 );

	// how long we stand there: running in, a timetable slot or the maximum wait, then waiting for it
	const sint64 our_speed = max( cnv->get_min_top_speed(), 1 );
	const sint64 arrival = ( (sint64)(route->get_count()-1-start_block) << (8+12) ) / our_speed;
	sint64 dwell = 0;
	schedule_t const* const schedule = cnv->get_schedule();
	if(  schedule  &&  !schedule->empty()  ) {
		const schedule_entry_t &entry = schedule->get_current_entry();
		if(  entry.minimum_loading>0  ) {
			// waiting for a load without a maximum wait may take for ever: a month
			dwell = entry.has_waiting_time() ? (sint64)entry.get_waiting_ticks() : (sint64)welt->ticks_per_world_month;
		}
		sint64 slot;
		const sint64 ready_at = welt->get_calendar_minutes_at( welt->get_ticks() + (uint32)arrival );
		if(  !cnv->get_no_load()  &&  cnv->get_line().is_bound()  &&  cnv->get_line()->get_planned_departure( cnv->self, ready_at, slot )  &&  slot > ready_at  ) {
			const sint64 to_slot = welt->calendar_minutes_to_ticks( slot - ready_at );
			if(  to_slot > dwell  ) {
				dwell = to_slot;
			}
		}
	}
	const sint64 limit = arrival + dwell + welt->calendar_minutes_to_ticks( welt->get_settings().get_passing_hold_minutes() );

	FOR( vector_tpl<convoihandle_t>, const other, welt->convoys() ) {
		if(  other == cnv->self  ||  other->get_vehicle_count()==0  ||  other->in_depot()  ||  other->is_coupled()  ) {
			continue;
		}
		vehicle_t const* const front = other->front();
		route_t const* const r = other->get_route();
		if(  front->get_waytype() != get_waytype()  ||  r->get_count() < 2  ) {
			continue;
		}
		// far away trains cannot matter: a route is never shorter than the distance
		const sint64 speed = max( other->get_min_top_speed(), 1 );
		const sint64 reach = (limit * speed) >> 20;
		if(  koord_distance( front->get_pos(), sig_pos ) > reach  &&  koord_distance( front->get_pos(), sig_pos ) > 256  ) {
			continue;
		}
		// it comes by our signal the way we do (a standing train's route ends at its stop: not before it leaves)
		const uint32 from = max( front->get_route_index(), 1u ) - 1;
		uint32 at = INVALID_INDEX;
		for(  uint32 i=from;  i+1 < r->get_count();  i++  ) {
			if(  r->at(i)==sig_pos  &&  r->at(i+1)==sig_next  ) {
				at = i;
				break;
			}
		}
		if(  at==INVALID_INDEX  ) {
			continue;
		}
		// its way through the area, up to an end of choose or the next signal that applies; it must go on
		// from there (else it stops in the area)
		uint32 end = INVALID_INDEX;
		for(  uint32 i=at+1;  i+1 < r->get_count();  i++  ) {
			grund_t const* const gr = welt->lookup( r->at(i) );
			weg_t const* const w = gr ? gr->get_weg( get_waytype() ) : NULL;
			if(  w==NULL  ||  has_choose_signal( gr, w )  ||  get_station_boundary( gr )  ) {
				break;
			}
			if(  has_end_of_choose( gr, w )  ||  signal_applies( gr, ribi_type( r->at(i), r->at(i+1) ) )  ) {
				end = i;
				break;
			}
		}
		if(  end==INVALID_INDEX  ) {
			continue;
		}
		const sint64 eta = ( (sint64)(end - from) << (8+12) ) / speed;
		if(  eta > limit  ) {
			continue;
		}
		for(  uint32 i=at+1;  i<=end;  i++  ) {
			way.append_unique( r->at(i) );
		}
		// where a track of our direction rejoins it (see leads_back_to_overtaker)
		for(  uint32 i=end+1;  i<r->get_count()  &&  i<=end+OVERTAKER_WALK_TILES;  i++  ) {
			onward.append_unique( r->at(i) );
		}
	}
	return !way.empty();
}


bool rail_vehicle_t::leads_back_to_overtaker(const route_t &rt) const
{
	const uint32 n = rt.get_count();
	if(  n<2  ) {
		return false;
	}
	// a track of our direction runs back into the way of the train passing us (the rejoining switch, the
	// end of choose); the other direction's main track runs on out of the station to a signal or station
	// boundary against us. Walk every branch forward from the stop position, never back towards the
	// entry; each must end on their way, in a dead end, or at a tile seen already
	const ribi_t::ribi travel = ribi_type( rt.at(n-2), rt.at(n-1) );
	vector_tpl<koord3d> seen;
	vector_tpl<koord3d> open_pos;
	vector_tpl<ribi_t::ribi> open_dir;
	open_pos.append( rt.at(n-1) );
	open_dir.append( travel );
	seen.append( rt.at(n-1) );
	grund_t const* const start_gr = welt->lookup( rt.at(n-1) );
	const halthandle_t halt = start_gr ? start_gr->get_halt() : halthandle_t();
	while(  !open_pos.empty()  ) {
		const koord3d pos = open_pos.back();
		const ribi_t::ribi in = open_dir.back();
		open_pos.pop_back();
		open_dir.pop_back();
		grund_t const* const gr = welt->lookup( pos );
		weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
		if(  way==NULL  ) {
			continue;
		}
		const ribi_t::ribi exits = way->get_ribi_unmasked() & ~ribi_t::backward( in ) & ~ribi_t::backward( travel );
		for(  uint8 r=0;  r<4;  r++  ) {
			const ribi_t::ribi dir = ribi_t::nsew[r];
			grund_t *to;
			if(  (exits & dir)==0  ||  !gr->get_neighbour( to, get_waytype(), dir )  ) {
				continue;
			}
			const koord3d next = to->get_pos();
			if(  overtaker_ways.is_contained( next )  ||  overtaker_routes.is_contained( next )  ||  seen.is_contained( next )  ) {
				// back on their way, or a branch walked already
				continue;
			}
			weg_t const* const next_way = to->get_weg( get_waytype() );
			if(  next_way==NULL  ) {
				continue;
			}
			if(  next_way->get_ribi_maske() & dir  ) {
				// a one-way signal or sign against us: the other direction's track
				return false;
			}
			roadsign_t const* const lt = get_station_boundary( to );
			if(  lt  &&  lt->applies_to( ribi_t::backward( dir ) )  ) {
				// their way into a station from our exit side
				return false;
			}
			if(  to->is_halt()  &&  to->get_halt()!=halt  ) {
				// on into another station: a line
				return false;
			}
			if(  seen.get_count() >= OVERTAKER_WALK_TILES  ) {
				// runs on without coming back to their way: a line, not a loop
				return false;
			}
			seen.append( next );
			if(  !ribi_t::is_single( next_way->get_ribi_unmasked() )  ) {
				// (a dead end ends the branch)
				open_pos.append( next );
				open_dir.append( dir );
			}
		}
	}
	return true;
}


uint16 rail_vehicle_t::get_passed_end_of_choose(const uint16 start_block) const
{
	route_t const* const route = cnv->get_route();
	grund_t const* const target = welt->lookup( route->back() );
	const halthandle_t route_halt = target ? target->get_halt() : halthandle_t();

	// find the end of choose sign; no detour when we stop in the area or meet another choose signal
	uint16 end_of_choose = INVALID_INDEX;
	for(  uint32 idx=start_block+1;  end_of_choose==INVALID_INDEX  &&  idx+1<route->get_count();  idx++  ) {
		grund_t const* const gr = welt->lookup( route->at(idx) );
		weg_t const* const way = gr ? gr->get_weg( get_waytype() ) : NULL;
		if(  way==NULL  ||  (route_halt.is_bound()  &&  gr->get_halt()==route_halt)  ) {
			return INVALID_INDEX;
		}
		if(  way->has_sign()  ) {
			roadsign_t const* const rs = gr->find<roadsign_t>(1);
			if(  rs  &&  rs->get_desc()->get_wtyp()==get_waytype()  &&  (rs->get_desc()->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA)  ) {
				end_of_choose = idx;
			}
		}
		if(  way->has_signal()  ) {
			signal_t const* const sig = gr->find<signal_t>(1);
			if(  sig  &&  sig->get_desc()->is_choose_sign()  ) {
				return INVALID_INDEX;
			}
		}
	}
	if(  end_of_choose==INVALID_INDEX  ) {
		return INVALID_INDEX;
	}

	// a waypoint of the schedule in the area must not be skipped
	schedule_t const* const schedule = cnv->get_schedule();
	for(  uint32 idx=start_block+1;  schedule  &&  idx<=end_of_choose;  idx++  ) {
		for(  uint8 i=0;  i<schedule->get_count();  i++  ) {
			if(  schedule->entries[i].pos==route->at(idx)  &&  cnv->is_waypoint( route->at(idx) )  ) {
				return INVALID_INDEX;
			}
		}
	}
	return end_of_choose;
}


uint16 rail_vehicle_t::get_choose_detour_end(const uint16 start_block) const
{
	route_t const* const route = cnv->get_route();
	const uint16 end_of_choose = get_passed_end_of_choose( start_block );
	if(  end_of_choose==INVALID_INDEX  ) {
		return INVALID_INDEX;
	}

	// who blocks our way through the area?
	convoihandle_t blocker;
	for(  uint32 idx=start_block+1;  idx<=end_of_choose  &&  !blocker.is_bound();  idx++  ) {
		schiene_t const* const sch = obj_cast<schiene_t>( welt->lookup( route->at(idx) )->get_weg( get_waytype() ) );
		if(  sch  &&  !sch->can_reserve( cnv->self )  ) {
			blocker = sch->get_reserved_convoi();
		}
	}
	if(  !blocker.is_bound()  ) {
		// our way through the area is free, something after it blocks
		return INVALID_INDEX;
	}
	if(  blocker->is_standing()  ||  blocker->is_waiting()  ) {
		return end_of_choose;
	}

	// a running train is only overtaken when it stops at a station in this area
	route_t const* const blocker_route = blocker->get_route();
	grund_t const* const blocker_target = blocker_route->empty() ? NULL : welt->lookup( blocker_route->back() );
	const halthandle_t blocker_halt = blocker_target ? blocker_target->get_halt() : halthandle_t();
	if(  blocker_halt.is_bound()  ) {
		for(  uint32 idx=start_block+1;  idx<=end_of_choose;  idx++  ) {
			if(  welt->lookup( route->at(idx) )->get_halt()==blocker_halt  ) {
				return end_of_choose;
			}
		}
	}
	return INVALID_INDEX;
}


bool rail_vehicle_t::has_onward_path(const route_t &to_platform, const uint16 end_of_choose)
{
	route_t const* const route = cnv->get_route();
	const uint32 n = to_platform.get_count();
	if(  n < 2  ) {
		return false;
	}
	// the passing train may already hold parts of that way, so any track counts
	route_t onward;
	detour_start = to_platform.back();
	detour_target = route->at(end_of_choose);
	detour_exit = route->at(end_of_choose+1);
	detour_any_track = true;
	const ribi_t::ribi dir = ribi_type( to_platform.at(n-2), to_platform.at(n-1) );
	const bool found = onward.find_route( welt, detour_start, this, speed_to_kmh(cnv->get_min_top_speed()), dir, welt->get_settings().get_max_choose_route_steps() );
	detour_start = detour_target = detour_exit = koord3d::invalid;
	detour_any_track = false;
	return found;
}


bool rail_vehicle_t::reserve_hold_platform(const uint16 start_block, const uint16 end_of_choose, uint16 &next_signal, uint16 &next_crossing)
{
	route_t const* const route = cnv->get_route();

	// search a free platform before the end of choose, first off the planned way, then anywhere;
	// nothing is loaded there, so any platform type will do
	route_t target_rt;
	bool found = false;
	platform_needs = 0;
	bay_search = 0;
	detour_start = route->at(start_block);
	hold_avoid_from = start_block+1;
	hold_avoid_to = end_of_choose;
	const ribi_t::ribi start_dir = ribi_type( route->at(start_block), route->at(start_block+1) );
	for(  uint8 pass=1;  !found  &&  pass<=2;  pass++  ) {
		// the platform must lead on forward to our end of choose (no bay platform, no reversing);
		// otherwise try the next one, a few times
		hold_platform_excluded.clear();
		for(  uint8 attempt=0;  !found  &&  attempt<4;  attempt++  ) {
			detour_start = route->at(start_block);
			hold_search = pass;
			if(  !target_rt.find_route( welt, detour_start, this, speed_to_kmh(cnv->get_min_top_speed()), start_dir, welt->get_settings().get_max_choose_route_steps() )  ) {
				break;
			}
			hold_search = 0;
			grund_t const* const end_gr = welt->lookup( target_rt.back() );
			found = !turns_round_through( target_rt, 0, end_gr ? end_gr->get_halt() : halthandle_t() )  &&  has_onward_path( target_rt, end_of_choose );
			if(  !found  ) {
				hold_platform_excluded.append( target_rt.back() );
			}
		}
	}
	hold_search = 0;
	detour_start = koord3d::invalid;
	hold_platform_excluded.clear();
	if(  !found  ) {
		return false;
	}

	route_t new_route( *route );
	new_route.remove_koord_from( start_block );
	new_route.append( &target_rt );
	if(  !block_reserver( &new_route, start_block+1, next_signal, next_crossing, 100000, true, false )  ) {
		return false;
	}
	route_t *const rt = cnv->access_route();
	rt->clear();
	rt->append( &new_route );
	// arriving there is no stop of the schedule
	cnv->set_hold_divert( true );
	return true;
}


bool rail_vehicle_t::reserve_choose_detour(const uint16 start_block, const uint16 end_of_choose, uint16 &next_signal, uint16 &next_crossing)
{
	route_t const* const route = cnv->get_route();

	// search a free way from the signal to the end of choose tile
	route_t detour;
	detour_start = route->at(start_block);
	detour_target = route->at(end_of_choose);
	detour_exit = route->at(end_of_choose+1);
	const ribi_t::ribi start_dir = ribi_type( route->at(start_block), route->at(start_block+1) );
	const bool found = detour.find_route( welt, detour_start, this, speed_to_kmh(cnv->get_min_top_speed()), start_dir, welt->get_settings().get_max_choose_route_steps() );
	detour_start = detour_target = detour_exit = koord3d::invalid;
	if(  !found  ) {
		return false;
	}

	// not much longer than the planned way, else it is no overtaking but a trip elsewhere
	const uint32 planned = end_of_choose - start_block;
	if(  detour.get_count()-1 > planned + planned/2 + 4  ) {
		return false;
	}

	// planned route with the detour in place of the way through the area
	route_t new_route( *route );
	new_route.remove_koord_from( start_block );
	new_route.append( &detour );
	const uint32 detour_end = new_route.get_count()-1;
	for(  uint32 i=end_of_choose+1;  i<route->get_count();  i++  ) {
		new_route.append( route->at(i) );
	}
	if(  new_route.get_count() >= INVALID_INDEX  ) {
		return false;
	}

	// reserve through all signals up to the end of choose, then up to the next signal as usual;
	// the block reserver frees everything again if a tile is taken
	int signals = 0;
	for(  uint32 i=start_block+1;  i<=detour_end;  i++  ) {
		if(  i<new_route.get_count()-1  &&  is_stop_point( &new_route, i )  ) {
			signals ++;
		}
	}
	if(  !block_reserver( &new_route, start_block+1, next_signal, next_crossing, signals, true, false )  ) {
		return false;
	}

	route_t *const rt = cnv->access_route();
	rt->clear();
	rt->append( &new_route );
	return true;
}


int rail_vehicle_t::reserve_to_partner(signal_t *sig, const uint16 start_block, convoihandle_t partner, bool standing, sint32 &restart_speed)
{
	uint16 next_signal, next_crossing;
	route_t const* const route = cnv->get_route();
	// already on the way there?
	const koord3d goal = standing ? partner->front()->get_pos() : partner->get_route()->back();
	bool on_way = false;
	for(  uint32 i=start_block+1;  !on_way  &&  i<route->get_count();  i++  ) {
		const grund_t *gr = welt->lookup( route->at(i) );
		const schiene_t *sch = gr ? obj_cast<schiene_t>( gr->get_weg( get_waytype() ) ) : NULL;
		on_way = route->at(i)==goal  ||  (standing  &&  sch  &&  sch->get_reserved_convoi()==partner);
	}
	if(  !on_way  ) {
		if(  !cnv->is_waiting()  ) {
			// the route search needs a step: come to the signal first
			restart_speed = -1;
			return 0;
		}
		route_t target_rt;
		couple_search = partner;
		couple_goal = standing ? koord3d::invalid : goal;
		const ribi_t::ribi start_dir = ribi_type( route->at(start_block), route->at(start_block+1) );
		const bool found = target_rt.find_route( welt, route->at(start_block), this, speed_to_kmh(cnv->get_min_top_speed()), start_dir, welt->get_settings().get_max_choose_route_steps() );
		couple_search = convoihandle_t();
		couple_goal = koord3d::invalid;
		if(  !found  ) {
			// no way there from this signal
			return -1;
		}
		cnv->access_route()->remove_koord_from( start_block );
		cnv->access_route()->append( &target_rt );
	}
	// reserves up to the tile right behind a standing partner (see convoi_t::cut_route_before_partner);
	// fails while the partner still runs in, then we try again
	if(  !block_reserver( cnv->get_route(), start_block+1, next_signal, next_crossing, 100000, true, false )  ) {
		sig->set_state( roadsign_t::rot );
		restart_speed = 0;
		return 0;
	}
	sig->set_state( roadsign_t::gruen );
	cnv->set_next_stop_index( min( next_crossing, next_signal ) );
	return 1;
}


bool rail_vehicle_t::is_pre_signal_clear(signal_t *sig, uint16 next_block, sint32 &restart_speed)
{
	// parse to next signal; if needed recurse, since we allow cascading
	uint16 next_signal, next_crossing;
	if(  block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		if(  next_signal == INVALID_INDEX  ||  cnv->get_route()->at(next_signal) == cnv->get_route()->back()  ||  is_signal_clear( next_signal, restart_speed )  ) {
			// ok, end of route => we can go
			sig->set_state( roadsign_t::gruen );
			cnv->set_next_stop_index( min( next_signal, next_crossing ) );
			return true;
		}
		// when we reached here, the way is apparently not free => release reservation and set state to next free
		sig->set_state( roadsign_t::naechste_rot );
		block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, false, false );
		restart_speed = 0;
		return false;
	}
	// if we end up here, there was not even the next block free
	sig->set_state( roadsign_t::rot );
	restart_speed = 0;
	return false;
}



bool rail_vehicle_t::is_priority_signal_clear(signal_t *sig, uint16 next_block, sint32 &restart_speed)
{
	// parse to next signal; if needed recurse, since we allow cascading
	uint16 next_signal, next_crossing;

	if(  block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false )  ) {
		if(  next_signal == INVALID_INDEX  ||  cnv->get_route()->at(next_signal) == cnv->get_route()->back()  ||  is_signal_clear( next_signal, restart_speed )  ) {
			// ok, end of route => we can go
			sig->set_state( roadsign_t::gruen );
			cnv->set_next_stop_index( min( next_signal, next_crossing ) );

			return true;
		}

		// when we reached here, the way after the last signal is not free though the way before is => we can still go
		if(  cnv->get_next_stop_index()<=next_signal+1  ) {
			// only show third aspect on last signal of cascade
			sig->set_state( roadsign_t::naechste_rot );
		}
		else {
			sig->set_state( roadsign_t::gruen );
		}
		cnv->set_next_stop_index( min( next_signal, next_crossing ) );

		return false;
	}

	// if we end up here, there was not even the next block free
	sig->set_state( roadsign_t::rot );
	restart_speed = 0;

	return false;
}


bool rail_vehicle_t::is_signal_clear(uint16 next_block, sint32 &restart_speed)
{
	// called, when there is a signal; will call other signal routines if needed
	grund_t *gr_next_block = welt->lookup(cnv->get_route()->at(next_block));
	signal_t *sig = gr_next_block->find<signal_t>();
	if(  sig==NULL  ) {
		if(  get_station_boundary( gr_next_block )  ) {
			// fork: entering a station from a single-track line
			const bool clear = is_station_boundary_clear( next_block, restart_speed );
			update_boundary_aspect( gr_next_block->get_pos() );
			return clear;
		}
		dbg->error( "rail_vehicle_t::is_signal_clear()", "called at %s without a signal!", cnv->get_route()->at(next_block).get_str() );
		return true;
	}

	if(  sig->get_desc()->is_platform_signal()  ) {
		// fork: exit signal of a station track
		return is_platform_signal_clear( sig, next_block, restart_speed );
	}

	if(  sig->get_desc()->is_block_post()  ) {
		// fork: block post on a single-track line
		return is_block_post_clear( sig, next_block, restart_speed );
	}

	// action depend on the next signal
	const roadsign_desc_t *sig_desc=sig->get_desc();

	// simple signal: fail, if next block is not free
	if(  sig_desc->is_simple_signal()  ) {

		uint16 next_signal, next_crossing;
		if(  block_reserver( cnv->get_route(), next_block+1, next_signal, next_crossing, 0, true, false )  ) {
			if(  sig_desc->is_autoblock()  ) {
				// fork: green or yellow by the block ahead
				sig->refresh_autoblock();
			}
			else {
				sig->set_state(  roadsign_t::gruen );
			}
			cnv->set_next_stop_index( min( next_crossing, next_signal ) );
			return true;
		}
		// not free => wait here if directly in front
		sig->set_state(  roadsign_t::rot );
		restart_speed = 0;
		return false;
	}

	if(  sig_desc->is_pre_signal()  ) {
		return is_pre_signal_clear( sig, next_block, restart_speed );
	}

	if (  sig_desc->is_priority_signal()  ) {
		return is_priority_signal_clear( sig, next_block, restart_speed );
	}

	if(  sig_desc->is_longblock_signal()  ) {
		return is_longblock_signal_clear( sig, next_block, restart_speed );
	}

	if(  sig_desc->is_choose_sign()  ) {
		const bool clear = is_choose_signal_clear( sig, next_block, restart_speed );
		if(  clear  &&  sig_desc->has_yellow_aspect()  ) {
			// fork: green or yellow for the way chosen
			sig->set_state( get_route_aspect( cnv, next_block, get_waytype() ) );
		}
		return clear;
	}

	dbg->error( "rail_vehicle_t::is_signal_clear()", "felt through at signal at %s", cnv->get_route()->at(next_block).get_str() );
	return false;
}


bool rail_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	assert(leading);
	uint16 next_signal, next_crossing;
	if(  cnv->get_state()==convoi_t::CAN_START  ||  cnv->get_state()==convoi_t::CAN_START_ONE_MONTH  ||  cnv->get_state()==convoi_t::CAN_START_TWO_MONTHS  ) {
		// fork: set again below while the platform signal ahead keeps us at the stop
		cnv->set_platform_hold( false );
		// fork: a train that does not stop here may overtake us first
		if(  is_held_for_passing_train()  ) {
			restart_speed = 0;
			return false;
		}
		// reserve first block at the start until the next signal
		grund_t *gr_current = welt->lookup( get_pos() );
		weg_t *w = gr_current ? gr_current->get_weg(get_waytype()) : NULL;
		const uint32 here = max(route_index,1)-1;
		// fork: the platform signal at the end of the track we stand on: wait for it here at the stop
		// position (boarding on, convoi_t::load_while_held) instead of creeping up to it
		const uint32 exit_signal = w ? get_platform_exit_signal( here ) : INVALID_INDEX;
		if(  exit_signal!=INVALID_INDEX  ) {
			if(  exit_signal>here  ) {
				// the platform up to it
				if(  !block_reserver( cnv->get_route(), here, next_signal, next_crossing, 0, true, false )  ) {
					restart_speed = 0;
					return false;
				}
				if(  next_signal!=exit_signal  ||  next_crossing<next_signal  ) {
					// (cannot happen, the way there was checked) as stock: drive up to it
					cnv->set_next_stop_index( next_crossing<next_signal ? next_crossing : next_signal );
					cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
					return true;
				}
			}
			else {
				// standing on it: reserved up to here (a save while held must not reserve beyond it on load)
				cnv->set_next_reservation_index( here+1 );
			}
			cnv->set_next_stop_index( exit_signal );
			if(  !is_signal_clear( exit_signal, restart_speed )  ) {
				cnv->set_platform_hold( true );
				restart_speed = 0;
				return false;
			}
			return true;
		}
		if(  w==NULL  ||  !((here<cnv->get_route()->get_count()  &&  is_stop_point( cnv->get_route(), here ))  ||  w->is_crossing())  ) {
			// free track => reserve up to next signal
			if(  !block_reserver(cnv->get_route(), max(route_index,1)-1, next_signal, next_crossing, 0, true, false )  ) {
				restart_speed = 0;
				return false;
			}
			cnv->set_next_stop_index( next_crossing<next_signal ? next_crossing : next_signal );
			cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
			return true;
		}
		cnv->set_next_stop_index( max(route_index,1)-1 );
		if(  steps<steps_next  ) {
			// not yet at tile border => can drive to signal safely
			return true;
		}
		// we start with a signal/crossing => use stuff below ...
	}

	assert(gr);
	if(gr->get_top()>250) {
		// too many objects here
		return false;
	}

	schiene_t *w = (schiene_t *)gr->get_weg(get_waytype());
	if(w==NULL) {
		return false;
	}

	/* this should happen only before signals ...
	 * but if it is already reserved, we can save lots of other checks later
	 */
	if(  !w->can_reserve(cnv->self)  ) {
		restart_speed = 0;
		return false;
	}

	// is there any signal/crossing to be reserved?
	uint16 next_block = cnv->get_next_stop_index()-1;
	if(  next_block >= cnv->get_route()->get_count()  ) {
		// no obstacle in the way => drive on ...
		return true;
	}

	// signal disappeared, train passes the tile of former signal
	if(  next_block+1 < route_index  ) {
		// we need to reserve the next block even if there is no signal present anymore
		bool ok = block_reserver( cnv->get_route(), route_index, next_signal, next_crossing, 0, true, false );
		if (ok) {
			cnv->set_next_stop_index( min( next_crossing, next_signal ) );
			// fork: no longer waiting at the signal that was there
			cnv->set_section_wait( convoi_t::SECTION_WAIT_NONE, halthandle_t() );
		}
		return ok;
		// if reservation was not possible the train will wait on the track until block is free
	}

	if(  next_block <= route_index+3  ) {
		koord3d block_pos=cnv->get_route()->at(next_block);
		grund_t *gr_next_block = welt->lookup(block_pos);
		const schiene_t *sch1 = gr_next_block ? (const schiene_t *)gr_next_block->get_weg(get_waytype()) : NULL;
		if(sch1==NULL) {
			// way (weg) not existent (likely destroyed)
			cnv->suche_neue_route();
			return false;
		}

		// Is a crossing?
		// note: crossing and signal might exist on same tile
		// so first check crossing
		if(  sch1->is_crossing()  ) {
			if(  crossing_t* cr = gr_next_block->find<crossing_t>(2)  ) {
				// ok, here is a draw/turnbridge ...
				bool ok = cr->request_crossing(this);
				if(!ok) {
					// cannot cross => wait here
					restart_speed = 0;
					return cnv->get_next_stop_index()>route_index+1;
				}
				else if(  !is_stop_point( cnv->get_route(), next_block )  ) {
					// can reserve: find next place to do something and drive on
					if(  block_pos == cnv->get_route()->back()  ) {
						// is also last tile => go on ...
						cnv->set_next_stop_index( INVALID_INDEX );
						return true;
					}
					else if(  !block_reserver( cnv->get_route(), cnv->get_next_stop_index(), next_signal, next_crossing, 0, true, false )  ) {
						dbg->error( "rail_vehicle_t::can_enter_tile()", "block not free but was reserved!" );
						return false;
					}
					cnv->set_next_stop_index( next_crossing<next_signal ? next_crossing : next_signal );
				}
			}
		}

		// next check for signal (fork: or a station boundary; a platform signal only in its direction)
		if(  is_stop_point( cnv->get_route(), next_block )  ) {
			if(  !is_signal_clear( next_block, restart_speed )  ) {
				// only return false, if we are directly in front of the signal
				return cnv->get_next_stop_index()>route_index;
			}
		}
	}
	return true;
}


// fork: the signals and entry signals a train no longer goes past, set once its reservation is freed
// (a signal turning red refreshes the autoblocks behind it, which look at the reservations)
void rail_vehicle_t::refresh_freed_signals(const vector_tpl<signal_t *> &signals, const vector_tpl<koord3d> &boundaries)
{
	FOR( vector_tpl<signal_t *>, const sig, signals ) {
		if(  sig->get_desc()->is_autoblock()  ) {
			sig->refresh_autoblock();
		}
		else {
			sig->set_state( roadsign_t::rot );
		}
	}
	FOR( vector_tpl<koord3d>, const pos, boundaries ) {
		update_boundary_aspect( pos );
	}
}


/**
 * reserves or un-reserves all blocks and returns the handle to the next block (if there)
 * if count is larger than 1, (and defined) maximum MAX_CHOOSE_BLOCK_TILES tiles will be checked
 * (freeing or reserving a choose signal path)
 * if (!reserve && force_unreserve) then un-reserve everything till the end of the route
 * return the last checked block
 */
bool rail_vehicle_t::block_reserver(const route_t *route, uint16 start_index, uint16 &next_signal_index, uint16 &next_crossing_index, int count, bool reserve, bool force_unreserve  ) const
{
	bool success=true;
#ifdef MAX_CHOOSE_BLOCK_TILES
	int max_tiles=2*MAX_CHOOSE_BLOCK_TILES; // max tiles to check for choosesignals
#endif
	slist_tpl<grund_t *> signs; // switch all signals on their way too ...

	// fork, coupling: stop right behind our partner standing at our next stop
	if(  reserve  &&  route==cnv->get_route()  ) {
		cnv->cut_route_before_partner( start_index );
	}

	if(start_index>=route->get_count()) {
		cnv->set_next_reservation_index( max(route->get_count(),1)-1 );
		return 0;
	}

	if(route->at(start_index)==get_pos()  &&  reserve) {
		start_index++;
	}

	if(  !reserve  ) {
		cnv->set_next_reservation_index( start_index );
	}

	// find next block segment en route
	uint16 i=start_index;
	next_signal_index=INVALID_INDEX;
	next_crossing_index=INVALID_INDEX;
	bool unreserve_now = false;
	vector_tpl<signal_t *> autoblocks; // fork: freed signals get their aspect once the loop is done
	vector_tpl<koord3d> boundaries;    // fork: and freed entry signals (an aspect change looks at the blocks behind)
	vector_tpl<uint16> junctions;      // fork: switches reserved, may join autoblock sections from the side
	for ( ; success  &&  count>=0  &&  i<route->get_count(); i++) {

		koord3d pos = route->at(i);
		grund_t *gr = welt->lookup(pos);
		schiene_t * sch1 = gr ? (schiene_t *)gr->get_weg(get_waytype()) : NULL;
		if(sch1==NULL  &&  reserve) {
			// reserve until the end of track
			break;
		}
		// we un-reserve also nonexistent tiles! (may happen during deletion)

#ifdef MAX_CHOOSE_BLOCK_TILES
		max_tiles--;
		if(max_tiles<0  &&   count>1) {
			break;
		}
#endif
		if(reserve) {
			if(  i<route->get_count()-1  &&  is_stop_point( route, i )  ) {
				if(count) {
					signs.append(gr);
				}
				count --;
				next_signal_index = i;
			}
			if(  !sch1->reserve( cnv->self, ribi_type( route->at(max(1u,i)-1u), route->at(min(route->get_count()-1u,i+1u)) ) )  ) {
				success = false;
			}
			else if(  signal_t::any_autoblock  &&  ribi_t::is_threeway( sch1->get_ribi_unmasked() )  ) {
				junctions.append( i );
			}
			if(next_crossing_index==INVALID_INDEX  &&  sch1->is_crossing()) {
				next_crossing_index = i;
			}
		}
		else if(sch1) {
			if(!sch1->unreserve(cnv->self)) {
				if(unreserve_now) {
					// reached an reserved or free track => finished
					refresh_freed_signals( autoblocks, boundaries );
					return false;
				}
			}
			else {
				// un-reserve from here (used during sale, since there might be reserved tiles not freed)
				unreserve_now = !force_unreserve;
			}
			if(sch1->has_signal()) {
				// fork: red, or an autoblock green again if nobody else holds its block, once all is freed
				if(  signal_t* signal = gr->find<signal_t>()  ) {
					autoblocks.append( signal );
				}
			}
			else if(  sch1->has_sign()  ) {
				// fork: an entry signal we no longer go past
				boundaries.append( pos );
			}
			if(sch1->is_crossing()) {
				gr->find<crossing_t>()->release_crossing(this);
			}
		}
	}

	if(!reserve) {
		refresh_freed_signals( autoblocks, boundaries );
		return false;
	}
	// here we go only with reserve

//DBG_MESSAGE("block_reserver()","signals at %i, success=%d",next_signal_index,success);

	// free, in case of un-reserve or no success in reservation
	if(!success) {
		// free reservation
		for ( int j=start_index; j<i; j++) {
			schiene_t * sch1 = (schiene_t *)welt->lookup( route->at(j))->get_weg(get_waytype());
			sch1->unreserve(cnv->self);
		}
		cnv->set_next_reservation_index( start_index );
		return false;
	}

	// ok, switch everything green ...
	FOR(slist_tpl<grund_t*>, const g, signs) {
		if (signal_t* const signal = g->find<signal_t>()) {
			if(  signal->get_desc()->is_autoblock()  ) {
				signal->refresh_autoblock();
			}
			else {
				signal->set_state(roadsign_t::gruen);
			}
		}
	}
	// fork: a train joining over a switch takes the block of the autoblocks on the other branches
	FOR( vector_tpl<uint16>, const j, junctions ) {
		const ribi_t::ribi exit_dir = j+1u<route->get_count() ? ribi_type( route->at(j), route->at(j+1) ) : (ribi_t::ribi)ribi_t::none;
		signal_t::refresh_autoblocks_behind( route->at(j), exit_dir, get_waytype() );
	}
	cnv->set_next_reservation_index( i );

	return true;
}


/* beware: we must un-reserve rail blocks... */
void rail_vehicle_t::leave_tile()
{
	// fork, timetable: past its exit signal: the time it stood since it was ready is part of the delay
	if(  leading  &&  cnv  &&  cnv->is_departure_pending()  ) {
		route_t const* const r = cnv->get_route();
		if(  route_index > 0  &&  (uint32)(route_index-1) < r->get_count()  &&  r->at(route_index-1)==get_pos()  &&  is_stop_point( r, route_index-1 )  ) {
			cnv->finish_departure_delay();
		}
	}
	vehicle_t::leave_tile();
	// fix counters
	if(last) {
		grund_t *gr = welt->lookup( get_pos() );
		if(gr) {
			schiene_t *sch0 = (schiene_t *) gr->get_weg(get_waytype());
			if(sch0) {
				sch0->unreserve(this);
				if(  cnv  &&  cnv->is_claimed_tile( get_pos() )  ) {
					// fork: the track claimed in a station can be the one we are leaving
					sch0->reserve( cnv->self, ribi_t::none );
				}
				// fork, coupling: the train we just uncoupled takes the tile over
				if(  cnv  ) {
					cnv->handover_tile( get_pos() );
				}
				// tell next signal?
				// and switch to red
				const roadsign_t *end = NULL; // fork: the signal or entry signal we left
				bool was_red = false;
				if(sch0->has_signal()) {
					signal_t* sig = gr->find<signal_t>();
					end = sig;
					was_red = sig  &&  sig->get_state()==roadsign_t::rot;
					if(  sig  &&  sig->get_desc()->is_autoblock()  ) {
						// fork: red while we are still in its block (green if we turned back out of it)
						sig->refresh_autoblock();
					}
					else if(sig) {
						sig->set_state(roadsign_t::rot);
					}
				}
				else if(  sch0->has_sign()  ) {
					// fork: an entry signal turns red behind the train, or shows the next train's aspect
					end = get_station_boundary( gr );
					was_red = end  &&  end->get_state()==roadsign_t::rot;
					update_boundary_aspect( get_pos() );
				}
				// fork: we left the block of the autoblocks behind us, which ends here (unless turning
				// red just refreshed them)
				if(  signal_t::any_autoblock  &&  end  ) {
					const ribi_t::ribi exit_dir = pos_next!=get_pos() ? ribi_type( get_pos(), pos_next ) : (ribi_t::ribi)ribi_t::none;
					if(  was_red!=(end->get_state()==roadsign_t::rot)  &&  exit_dir!=ribi_t::none  &&  end->applies_to( exit_dir )  ) {
						// done by the aspect change
					}
					else {
						signal_t::refresh_autoblocks_behind( get_pos(), exit_dir, get_waytype() );
					}
				}
			}
		}
	}
}


void rail_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(  schiene_t *sch0 = (schiene_t *) gr->get_weg(get_waytype())  ) {
		// way statistics
		const int cargo = get_total_cargo();
		sch0->book(cargo, WAY_STAT_GOODS);
		if(leading) {
			sch0->book(1, WAY_STAT_CONVOIS);
			sch0->reserve( cnv->self, get_direction() );
		}
	}
	// fork: an autoblock turns red as soon as our head is past it (the autoblocks behind it are red
	// anyway: we still hold its tile, which is part of their blocks)
	if(  leading  &&  !last  &&  signal_t::any_autoblock  &&  cnv  &&  route_index>=2  &&  route_index-2u<cnv->get_route()->get_count()  ) {
		const koord3d prev = cnv->get_route()->at( route_index-2u );
		grund_t *gr_prev = prev!=get_pos() ? welt->lookup( prev ) : NULL;
		weg_t const* const way_prev = gr_prev ? gr_prev->get_weg( get_waytype() ) : NULL;
		if(  way_prev  &&  way_prev->has_signal()  ) {
			signal_t *sig = gr_prev->find<signal_t>();
			if(  sig  &&  sig->get_desc()->is_autoblock()  ) {
				sig->refresh_autoblock( false );
			}
		}
	}
}


schedule_t * rail_vehicle_t::generate_new_schedule() const
{
	return desc->get_waytype()==tram_wt ? new tram_schedule_t() : new train_schedule_t();
}


schedule_t * monorail_vehicle_t::generate_new_schedule() const
{
	return new monorail_schedule_t();
}


schedule_t * maglev_vehicle_t::generate_new_schedule() const
{
	return new maglev_schedule_t();
}


schedule_t * narrowgauge_vehicle_t::generate_new_schedule() const
{
	return new narrowgauge_schedule_t();
}


water_vehicle_t::water_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
	vehicle_t(pos, desc, player)
{
	cnv = cn;
}


water_vehicle_t::water_vehicle_t(loadsave_t *file, bool is_first, bool is_last) : vehicle_t()
{
	vehicle_t::rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_first) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL) {
			dbg->warning("water_vehicle_t::water_vehicle_t()", "try to find a fitting vehicle for %s.", !fracht.empty() ? fracht.front().get_name() : "passengers");
			desc = vehicle_builder_t::get_best_matching(water_wt, 0, fracht.empty() ? 0 : 30, 100, 40, !fracht.empty() ? fracht.front().get_desc() : goods_manager_t::passengers, true, last_desc, is_last );
			if(desc) {
				calc_image();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
}


void water_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(  weg_t *ch = gr->get_weg(water_wt)  ) {
		// we are in a channel, so book statistics
		ch->book(get_total_cargo(), WAY_STAT_GOODS);
		if (leading)  {
			ch->book(1, WAY_STAT_CONVOIS);
		}
	}
}


bool water_vehicle_t::check_next_tile(const grund_t *bd) const
{
	if(  bd->is_water()  ) {
		return true;
	}
	// channel can have more stuff to check
	const weg_t *w = bd->get_weg(water_wt);
#ifdef ENABLE_WATERWAY_SIGNS
	if(  w  &&  w->has_sign()  ) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(  rs->get_desc()->get_wtyp()==get_waytype()  ) {
			if(  cnv !=NULL  &&  rs->get_desc()->get_min_speed() > 0  &&  rs->get_desc()->get_min_speed() > cnv->get_min_top_speed()  ) {
				// below speed limit
				return false;
			}
			if(  rs->get_desc()->is_private_way()  &&  (rs->get_player_mask() & (1<<get_player_nr()) ) == 0  ) {
				// private road
				return false;
			}
		}
	}
#endif
	return (w  &&  w->get_max_speed()>0);
}


/** Since slopes are handled different for ships
 */
void water_vehicle_t::calc_friction(const grund_t *gr)
{
	// or a hill?
	if(gr->get_weg_hang()) {
		// hill up or down => in lock => decelerate
		current_friction = 16;
	}
	else {
		// flat track
		current_friction = 1;
	}

	if(previous_direction != direction) {
		// curve: higher friction
		current_friction *= 2;
	}
}


bool water_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	restart_speed = -1;

	if(leading) {

		assert(gr);
		if(  gr->get_top()>251  ) {
			// too many ships already here ..
			return false;
		}
		weg_t *w = gr->get_weg(water_wt);
		if(w  &&  w->is_crossing()) {
			// ok, here is a draw/turn-bridge ...
			crossing_t* cr = gr->find<crossing_t>();
			if(!cr->request_crossing(this)) {
				restart_speed = 0;
				return false;
			}
		}
	}
	return true;
}


schedule_t * water_vehicle_t::generate_new_schedule() const
{
	return new ship_schedule_t();
}


/**** from here on planes ***/


// for flying things, everywhere is good ...
// another function only called during route searching
ribi_t::ribi air_vehicle_t::get_ribi(const grund_t *gr) const
{
	switch(state) {
		case taxiing:
		case looking_for_parking:
			return gr->get_weg_ribi(air_wt);

		case taxiing_to_halt:
		{
			// we must invert all one way signs here, since we start from the target position here!
			weg_t *w = gr->get_weg(air_wt);
			if(w) {
				ribi_t::ribi r = w->get_ribi_unmasked();
				if(  ribi_t::ribi mask = w->get_ribi_maske()  ) {
					r &= mask;
				}
				return r;
			}
			return ribi_t::none;
		}

		case landing:
		case departing:
		{
			ribi_t::ribi dir = gr->get_weg_ribi(air_wt);
			if(dir==0) {
				return ribi_t::all;
			}
			return dir;
		}

		case flying:
		case circling:
			return ribi_t::all;
	}
	return ribi_t::none;
}


// how expensive to go here (for way search)
int air_vehicle_t::get_cost(const grund_t *, const weg_t *w, const sint32, ribi_t::ribi) const
{
	// first favor faster ways
	int costs = 0;

	if(state==flying) {
		if(w==NULL) {
			costs += 1;
		}
		else {
			if(w->get_desc()->get_styp()==type_flat) {
				costs += 25;
			}
		}
	}
	else {
		// only, if not flying ...
		const runway_t *rw =(const runway_t *)w;
		// if we are on a runway, then take into account how many convois are already going there
		if(  rw->get_desc()->get_styp()==1  ) {
			costs += rw->get_reservation_count()*9; // encourage detours even during take off
		}
		if(w->get_desc()->get_styp()==type_flat) {
			costs += 3;
		}
		else {
			costs += 2;
		}
	}

	return costs;
}


// whether the ground is drivable or not depends on the current state of the airplane
bool air_vehicle_t::check_next_tile(const grund_t *bd) const
{
	switch (state) {
		case taxiing:
		case taxiing_to_halt:
		case looking_for_parking:
//DBG_MESSAGE("check_next_tile()","at %i,%i",bd->get_pos().x,bd->get_pos().y);
			return (bd->hat_weg(air_wt)  &&  bd->get_weg(air_wt)->get_max_speed()>0);

		case landing:
		case departing:
		case flying:
		case circling:
		{
//DBG_MESSAGE("air_vehicle_t::check_next_tile()","(cnv %i) in idx %i",cnv->self.get_id(),route_index );
			// here a height check could avoid too high mountains
			return true;
		}
	}
	return false;
}


// this routine is called by find_route, to determined if we reached a destination
bool air_vehicle_t::is_target(const grund_t *gr,const grund_t *) const
{
	if(state!=looking_for_parking  ||  !target_halt.is_bound()) {
		// search for the end of the runway
		const weg_t *w=gr->get_weg(air_wt);
		if(w  &&  w->get_desc()->get_styp()==type_runway) {
			// ok here is a runway
			ribi_t::ribi ribi= w->get_ribi_unmasked();
			if(ribi_t::is_single(ribi)  &&  (ribi&approach_dir)!=0) {
				// pointing in our direction
				// here we should check for length, but we assume everything is ok
				return true;
			}
		}
	}
	else {
		// otherwise we just check, if we reached a free stop position of this halt
		if(gr->get_halt()==target_halt  &&  target_halt->is_reservable(gr,cnv->self)) {
			return true;
		}
	}
	return false;
}


/* finds a free stop, calculates a route and reserve the position
 * else return false
 */
bool air_vehicle_t::find_route_to_stop_position()
{
	if(target_halt.is_bound()) {
//DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","bound! (cnv %i)",cnv->self.get_id());
		return true; // already searched with success
	}

	// check for skipping circle
	route_t *rt=cnv->access_route();

//DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","can approach? (cnv %i)",cnv->self.get_id());

	grund_t const* const last = welt->lookup(rt->back());
	target_halt = last ? last->get_halt() : halthandle_t();
	if(!target_halt.is_bound()) {
		return true; // no halt to search
	}

	// then: check if the search point is still on a runway (otherwise just proceed)
	grund_t const* const target = welt->lookup(rt->at(search_for_stop));
	if(target==NULL  ||  !target->hat_weg(air_wt)) {
		target_halt = halthandle_t();
		block_reserver( search_for_stop, 0xFFFFu, false ); // unreserve all tiles
		DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","no runway found at (%s)",rt->at(search_for_stop).get_str());
		return true; // no runway any more ...
	}

	// is our target occupied?
//	DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","state %i",state);
	if(!target_halt->find_free_position(air_wt,cnv->self,obj_t::air_vehicle)  ) {
		target_halt = halthandle_t();
		DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","no free position found!");
		return false;
	}
	else {
		// calculate route to free position:

		// if we fail, we will wait in a step, much more simulation friendly
		// and the route finder is not re-entrant!
		if(!cnv->is_waiting()) {
			target_halt = halthandle_t();
			return false;
		}

		// now search a route
//DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","some free: find route from index %i",suchen);
		route_t target_rt;
		flight_state prev_state = state;
		state = looking_for_parking;
		if(!target_rt.find_route( welt, rt->at(search_for_stop), this, 500, ribi_t::all, welt->get_settings().get_max_choose_route_steps() )) {
DBG_MESSAGE("aircraft_t::find_route_to_stop_position()","found no route to free one");

			// just make sure, that there is a route at all, otherwise start route search again
			for(  uint32 i=search_for_stop;  i<rt->get_count();  i++  ) {
				grund_t const* const target = welt->lookup( rt->at( i ) );
				if(  target == NULL  ||  !target->hat_weg( air_wt )  ) {
					DBG_MESSAGE( "aircraft_t::find_route_to_stop_position()", "no runway found at (%s)", rt->at( search_for_stop ).get_str() );
					get_convoi()->set_state(convoi_t::ROUTING_1);
					block_reserver( search_for_stop, 0xFFFFu, false ); // unreserve all tiles
					return false; // find new route
				}
			}

			// circle slowly another round ...
			target_halt = halthandle_t();
			state = prev_state;
			return false;
		}
		state = prev_state;

		// now reserve our choice ...
		target_halt->reserve_position(welt->lookup(target_rt.back()), cnv->self);
		//DBG_MESSAGE("aircraft_t::find_route_to_stop_position()", "found free stop near %i,%i,%i", target_rt.back().x, target_rt.back().y, target_rt.back().z);
		rt->remove_koord_from(search_for_stop);
		rt->append( &target_rt );
		return true;
	}
}


// main routine: searches the new route in up to three steps
// must also take care of stops under traveling and the like
bool air_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, route_t* route)
{
//DBG_MESSAGE("aircraft_t::calc_route()","search route from %i,%i,%i to %i,%i,%i",start.x,start.y,start.z,ziel.x,ziel.y,ziel.z);

	if(leading  &&  cnv) {
		// free target reservation
		if(  target_halt.is_bound() ) {
			if (grund_t* const target = welt->lookup(cnv->get_route()->back())) {
				target_halt->unreserve_position(target,cnv->self);
			}
		}
		// free runway reservation
		block_reserver( route_index, route->get_count(), false );
	}
	target_halt = halthandle_t(); // no block reserved

	const weg_t *w=welt->lookup(start)->get_weg(air_wt);
	bool start_in_the_air = (w==NULL);
	bool end_in_air=false;

	search_for_stop = takeoff = touchdown = 0x7ffffffful;
	if(!start_in_the_air) {

		// see, if we find a direct route: We are finished
		state = taxiing;
		if(route->calc_route( welt, start, ziel, this, max_speed, 0 )) {
			// ok, we can taxi to our location
			return true;
		}
	}

	if(start_in_the_air  ||  (w->get_desc()->get_styp()==type_runway  &&  ribi_t::is_single(w->get_ribi())) ) {
		// we start here, if we are in the air or at the end of a runway
		search_start = start;
		start_in_the_air = true;
		route->clear();
//DBG_MESSAGE("aircraft_t::calc_route()","start in air at %i,%i,%i",search_start.x,search_start.y,search_start.z);
	}
	else {
		// not found and we are not on the takeoff tile (where the route search will fail too) => we try to calculate a complete route, starting with the way to the runway

		// second: find start runway end
		state = taxiing;
#ifdef USE_DIFFERENT_WIND
		approach_dir = get_approach_ribi( ziel, start ); // reverse
//DBG_MESSAGE("aircraft_t::calc_route()","search runway start near %i,%i,%i with corner in %x",start.x,start.y,start.z, approach_dir);
#else
		approach_dir = ribi_t::northeast; // reverse
		DBG_MESSAGE("aircraft_t::calc_route()","search runway start near (%s)",start.get_str());
#endif
		if(!route->find_route( welt, start, this, max_speed, ribi_t::all, 100 )) {
			DBG_MESSAGE("aircraft_t::calc_route()","failed");
			return false;
		}
		// save the route
		search_start = route->back();
		//DBG_MESSAGE("aircraft_t::calc_route()","start at ground (%s)",search_start.get_str());
	}

	// second: find target runway end

	state = taxiing_to_halt; // only used for search

#ifdef USE_DIFFERENT_WIND
	approach_dir = get_approach_ribi( start, ziel ); // reverse
	//DBG_MESSAGE("aircraft_t::calc_route()","search runway target near %i,%i,%i in corners %x",ziel.x,ziel.y,ziel.z,approach_dir);
#else
	approach_dir = ribi_t::southwest; // reverse
	//DBG_MESSAGE("aircraft_t::calc_route()","search runway target near %i,%i,%i in corners %x",ziel.x,ziel.y,ziel.z);
#endif
	route_t end_route;

	if(!end_route.find_route( welt, ziel, this, max_speed, ribi_t::all, welt->get_settings().get_max_choose_route_steps() )) {
		// well, probably this is a waypoint
		if(  grund_t *target = welt->lookup(ziel)  ) {
			if(  !target->get_weg(air_wt)  ) {
				end_in_air = true;
				search_end = ziel;
			}
			else {
				// we have a taxiway/illegal runway here we cannot reach
				return false; // no route!
			}
		}
		else {
			// illegal coordinates?
			return false;
		}
	}
	else {
		// save target route
		search_end = end_route.back();
	}
	//DBG_MESSAGE("aircraft_t::calc_route()","end at ground (%s)",search_end.get_str());

	// create target route
	if(!start_in_the_air) {
		takeoff = route->get_count()-1;
		koord start_dir(welt->lookup(search_start)->get_weg_ribi(air_wt));
		if(start_dir!=koord(0,0)) {
			// add the start
			ribi_t::ribi start_ribi = ribi_t::backward(ribi_type(start_dir));
			const grund_t *gr=NULL;
			// add the start
			int endi = 1;
			int over = 3;
			// now add all runway + 3 ...
			do {
				if(!welt->is_within_limits(search_start.get_2d()+(start_dir*endi)) ) {
					break;
				}
				gr = welt->lookup_kartenboden(search_start.get_2d()+(start_dir*endi));
				if(over<3  ||  (gr->get_weg_ribi(air_wt)&start_ribi)==0) {
					over --;
				}
				endi ++;
				route->append(gr->get_pos());
			} while(  over>0  );
			// out of map
			if(gr==NULL) {
				dbg->error("aircraft_t::calc_route()","out of map!");
				return false;
			}
			// need some extra step to avoid 180 deg turns
			if( start_dir.x!=0  &&  sgn(start_dir.x)!=sgn(search_end.x-search_start.x)  ) {
				route->append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord(0,(search_end.y>search_start.y) ? 1 : -1 ) )->get_pos() );
				route->append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord(0,(search_end.y>search_start.y) ? 2 : -2 ) )->get_pos() );
			}
			else if( start_dir.y!=0  &&  sgn(start_dir.y)!=sgn(search_end.y-search_start.y)  ) {
				route->append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord((search_end.x>search_start.x) ? 1 : -1 ,0) )->get_pos() );
				route->append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord((search_end.x>search_start.x) ? 2 : -2 ,0) )->get_pos() );
			}
		}
		else {
			// init with startpos
			dbg->error("aircraft_t::calc_route()","Invalid route calculation: start is on a single direction field ...");
		}
		state = taxiing;
		flying_height = 0;
		target_height = (sint16)get_pos().z*TILE_HEIGHT_STEP;
	}
	else {
		// init with current pos (in air ... )
		route->clear();
		route->append( start );
		state = flying;
		if(flying_height==0) {
			flying_height = 3*TILE_HEIGHT_STEP;
		}
		takeoff = 0;
		target_height = ((sint16)get_pos().z+3)*TILE_HEIGHT_STEP;
	}

//DBG_MESSAGE("aircraft_t::calc_route()","take off ok");

	koord3d landing_start=search_end;
	if(!end_in_air) {
		// now find way to start of landing pos
		ribi_t::ribi end_ribi = welt->lookup(search_end)->get_weg_ribi(air_wt);
		koord end_dir(end_ribi);
		end_ribi = ribi_t::backward(end_ribi);
		if(end_dir!=koord(0,0)) {
			// add the start
			const grund_t *gr;
			int endi = 1;
			int over = 3;
			// now add all runway + 3 ...
			do {
				if(!welt->is_within_limits(search_end.get_2d()+(end_dir*endi)) ) {
					break;
				}
				gr = welt->lookup_kartenboden(search_end.get_2d()+(end_dir*endi));
				if(over<3  ||  (gr->get_weg_ribi(air_wt)&end_ribi)==0) {
					over --;
				}
				endi ++;
				landing_start = gr->get_pos();
			} while(  over>0  );
		}
	}
	else {
		search_for_stop = touchdown = 0x7FFFFFFFul;
	}

	// just some straight routes ...
	if(!route->append_straight_route(welt,landing_start)) {
		// should never fail ...
		dbg->error( "aircraft_t::calc_route()", "No straight route found!" );
		return false;
	}

	if(!end_in_air) {

		// find starting direction
		int offset = 0;
		switch(welt->lookup(search_end)->get_weg_ribi(air_wt)) {
			case ribi_t::north: offset = 0; break;
			case ribi_t::west: offset = 4; break;
			case ribi_t::south: offset = 8; break;
			case ribi_t::east: offset = 12; break;
		}

		// now make a curve
		koord circlepos=landing_start.get_2d();
		static const koord circle_koord[16]={ koord(0,1), koord(0,1), koord(1,0), koord(0,1), koord(1,0), koord(1,0), koord(0,-1), koord(1,0), koord(0,-1), koord(0,-1), koord(-1,0), koord(0,-1), koord(-1,0), koord(-1,0), koord(0,1), koord(-1,0) };

		// circle to the left
		for(  int  i=0;  i<16;  i++  ) {
			circlepos += circle_koord[(offset+i+16)%16];
			if(welt->is_within_limits(circlepos)) {
				route->append( welt->lookup_kartenboden(circlepos)->get_pos() );
			}
			else {
				// could only happen during loading old savegames;
				// in new versions it should not possible to build a runway here
				route->clear();
				dbg->error("aircraft_t::calc_route()","airport too close to the edge! (Cannot go to %i,%i!)",circlepos.x,circlepos.y);
				return false;
			}
		}

		touchdown = route->get_count()+2;
		route->append_straight_route(welt,search_end);

		// now the route reach point (+1, since it will check before entering the tile ...)
		search_for_stop = route->get_count()-1;

		// now we just append the rest
		for( int i=end_route.get_count()-2;  i>=0;  i--  ) {
			route->append(end_route.at(i));
		}
	}

//DBG_MESSAGE("aircraft_t::calc_route()","departing=%i  touchdown=%i   suchen=%i   total=%i  state=%i",takeoff, touchdown, suchen, route->get_count()-1, state );
	return true;
}


/* reserves runways (reserve true) or removes the reservation
 * finishes when reaching end tile or leaving the ground (end of runway)
 * @return true if the reservation is successful
 */
bool air_vehicle_t::block_reserver( uint32 start, uint32 end, bool reserve ) const
{
	bool start_now = false;
	bool success = true;

	const route_t *route = cnv->get_route();
	if(route->empty()) {
		return false;
	}

	for(  uint32 i=start;  success  &&  i<end  &&  i<route->get_count();  i++) {

		grund_t *gr = welt->lookup(route->at(i));
		runway_t *sch1 = gr ? (runway_t *)gr->get_weg(air_wt) : NULL;
		if(  !sch1  ) {
			if(reserve) {
				if(!start_now) {
					// touched down here
					start = i;
				}
				else {
					// most likely left the ground here ...
					end = i;
					break;
				}
			}
		}
		else {
			// we un-reserve also nonexistent tiles! (may happen during deletion)
			if(reserve) {
				start_now = true;
				sch1->add_convoi_reservation(cnv->self);
				if(  !sch1->reserve(cnv->self,ribi_t::none)  ) {
					// unsuccessful => must un-reserve all
					success = false;
					end = i;
					break;
				}
				// end of runway?
				if(  i > start  &&  (ribi_t::is_single( sch1->get_ribi_unmasked() )  ||  sch1->get_desc()->get_styp() != type_runway)   ) {
					end = i;
					break;
				}
			}
			else {
				// we always unreserve everything
				sch1->unreserve(cnv->self);
			}
		}
	}

	// un-reserve if not successful
	if(  !success  &&  reserve  ) {
		for(  uint32 i=start;  i<end;  i++  ) {
			grund_t *gr = welt->lookup(route->at(i));
			if (gr) {
				runway_t* sch1 = (runway_t *)gr->get_weg(air_wt);
				if (sch1) {
					sch1->unreserve(cnv->self);
				}
			}
		}
		return false;
	}

	if(  reserve  &&  end<touchdown  ) {
		// reserve runway for landing for load balancing
		for(  uint32 i=touchdown;  i<route->get_count();  i++  ) {
			if(  grund_t *gr = welt->lookup(route->at(i))  ) {
				if(  runway_t* sch1 = (runway_t *)gr->get_weg(air_wt)  ) {
					if(  sch1->get_desc()->get_styp()!=type_runway  ) {
						break;
					}
					sch1->add_convoi_reservation( cnv->self );
				}
			}
		}
	}

	return success;
}


// handles all the decisions on the ground an in the air
bool air_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	restart_speed = -1;

	assert(gr);
	if(gr->get_top()>250) {
		// too many objects here
		return false;
	}

	if(  route_index < takeoff  &&  route_index > 1  &&  takeoff<cnv->get_route()->get_count()-1  ) {
		// check, if tile occupied by a plane on ground
		if(  route_index > 1  ) {
			for(  uint8 i = 1;  i<gr->get_top();  i++  ) {
				obj_t *obj = gr->obj_bei(i);
				if(  obj->get_typ()==obj_t::air_vehicle  &&  ((air_vehicle_t *)obj)->is_on_ground()  ) {
					restart_speed = 0;
					return false;
				}
			}
		}
		// need to reserve runway?
		runway_t *rw = (runway_t *)gr->get_weg(air_wt);
		if(rw==NULL) {
			cnv->suche_neue_route();
			return false;
		}
		// next tile a runway => then reserve
		if(rw->get_desc()->get_styp()==type_runway) {
			// try to reserve the runway
			if(!block_reserver(takeoff,takeoff+100,true)) {
				// runway already blocked ...
				restart_speed = 0;
				return false;
			}
		}
		return true;
	}

	if(  state == taxiing  ) {
		// enforce on ground for taxiing
		flying_height = 0;
		// we may need to unreserve the runway after leaving it
		if(  route_index >= touchdown  ) {
			runway_t *rw = (runway_t *)gr->get_weg(air_wt);
			// next tile a not runway => then unreserve
			if(  rw == NULL  ||  rw->get_desc()->get_styp() != type_runway  ||  gr->is_halt()  ) {
				block_reserver( touchdown, search_for_stop+1, false );
			}
		}
	}

	if(  route_index == takeoff  &&  state == taxiing  ) {
		// try to reserve the runway if not already done
		if(route_index==2  &&  !block_reserver(takeoff,takeoff+100,true)) {
			// runway blocked, wait at start of runway
			restart_speed = 0;
			return false;
		}
		// stop shortly at the end of the runway
		state = departing;
		restart_speed = 0;
		return false;
	}

//DBG_MESSAGE("aircraft_t::ist_weg_frei()","index %i<>%i",route_index,touchdown);

	// check for another circle ...
	if(  route_index==(touchdown-3)  ) {
		if(  !block_reserver( touchdown, search_for_stop+1, true )  ) {
			route_index -= 16;
			return true;
		}
		state = landing;
		return true;
	}

	if(  route_index==touchdown-16-3  &&  state!=circling  ) {
		// just check, if the end of runway is free; we will wait there
		if(  block_reserver( touchdown, search_for_stop+1, true )  ) {
			route_index += 16;
			// can land => set landing height
			state = landing;
		}
		else {
			// circle slowly next round
			state = circling;
			cnv->must_recalc_data();
			if(  leading  ) {
				cnv->must_recalc_data_front();
			}
		}
	}

	if(route_index==search_for_stop  &&  state==landing  &&  !target_halt.is_bound()) {

		// if we fail, we will wait in a step, much more simulation friendly
		// and the route finder is not re-entrant!
		if(!cnv->is_waiting()) {
			return false;
		}

		// nothing free here?
		if(find_route_to_stop_position()) {
			// stop reservation successful
			state = taxiing;
			return true;
		}
		restart_speed = 0;
		return false;
	}

	if(state==looking_for_parking) {
		state = taxiing;
	}

	if(state == taxiing  &&  gr->is_halt()  &&  gr->find<air_vehicle_t>()) {
		// the next step is a parking position. We do not enter, if occupied!
		restart_speed = 0;
		return false;
	}

	return true;
}


// this must also change the internal modes for the calculation
void air_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(  this->is_on_ground()  ) {
		runway_t *w=(runway_t *)gr->get_weg(air_wt);
		if(w) {
			const int cargo = get_total_cargo();
			w->book(cargo, WAY_STAT_GOODS);
			if (leading) {
				w->book(1, WAY_STAT_CONVOIS);
			}
		}
	}
}


air_vehicle_t::air_vehicle_t(loadsave_t *file, bool is_first, bool is_last) : vehicle_t()
{
	rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_first) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL) {
			dbg->warning("aircraft_t::aircraft_t()", "try to find a fitting vehicle for %s.", !fracht.empty() ? fracht.front().get_name() : "passengers");
			desc = vehicle_builder_t::get_best_matching(air_wt, 0, 101, 1000, 800, !fracht.empty() ? fracht.front().get_desc() : goods_manager_t::passengers, true, last_desc, is_last );
			if(desc) {
				calc_image();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
}


air_vehicle_t::air_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
	vehicle_t(pos, desc, player)
{
	cnv = cn;
	state = taxiing;
	flying_height = 0;
	target_height = pos.z;
}


air_vehicle_t::~air_vehicle_t()
{
	// mark aircraft (after_image) dirty, since we have no "real" image
	const int raster_width = get_current_tile_raster_width();
	sint16 yoff = tile_raster_scale_y(-flying_height-get_hoff()-2, raster_width);

	mark_image_dirty( image, yoff);
	mark_image_dirty( image, 0 );
}


void air_vehicle_t::set_convoi(convoi_t *c)
{
	DBG_MESSAGE("aircraft_t::set_convoi()","%p",c);
	if(leading  &&  (uintptr_t)cnv > 1) {
		// free stop reservation
		route_t const& r = *cnv->get_route();
		if(target_halt.is_bound()) {
			target_halt->unreserve_position(welt->lookup(r.back()), cnv->self);
			target_halt = halthandle_t();
		}
		if (!r.empty()) {
			// free runway reservation
			if(route_index>=takeoff  &&  route_index<touchdown-4  &&  state!=flying) {
				block_reserver( takeoff, takeoff+100, false );
			}
			else if(route_index>=touchdown-1  &&  state!=taxiing) {
				block_reserver( touchdown, search_for_stop+1, false );
			}
		}
	}
	// maybe need to restore state?
	if(c!=NULL) {
		bool target=(bool)cnv;
		vehicle_t::set_convoi(c);
		if(leading) {
			if(target) {
				// reinitialize the target halt
				grund_t* const target=welt->lookup(cnv->get_route()->back());
				target_halt = target->get_halt();
				if(target_halt.is_bound()) {
					target_halt->reserve_position(target,cnv->self);
				}
			}
			// restore reservation
			if(  grund_t *gr = welt->lookup(get_pos())  ) {
				if(  weg_t *weg = gr->get_weg(air_wt)  ) {
					if(  weg->get_desc()->get_styp()==type_runway  ) {
						// but only if we are on a runway ...
						if(  route_index>=takeoff  &&  route_index<touchdown-21  &&  state!=flying  ) {
							block_reserver( takeoff, takeoff+100, true );
						}
						else if(  route_index>=touchdown-1  &&  state!=taxiing  ) {
							block_reserver( touchdown, search_for_stop+1, true );
						}
					}
				}
			}
		}
	}
	else {
		vehicle_t::set_convoi(NULL);
	}
}


schedule_t *air_vehicle_t::generate_new_schedule() const
{
	return new airplane_schedule_t();
}


void air_vehicle_t::rdwr_from_convoi(loadsave_t *file)
{
	xml_tag_t t( file, "aircraft_t" );

	// initialize as vehicle_t::rdwr_from_convoi calls get_image()
	if (file->is_loading()) {
		state = taxiing;
		flying_height = 0;
	}
	vehicle_t::rdwr_from_convoi(file);

	file->rdwr_enum(state);
	file->rdwr_short(flying_height);
	flying_height &= ~(TILE_HEIGHT_STEP-1);
	file->rdwr_short(target_height);
	file->rdwr_long(search_for_stop);
	file->rdwr_long(touchdown);
	file->rdwr_long(takeoff);
}


#ifdef USE_DIFFERENT_WIND
// well lots of code to make sure, we have at least two different directions for the runway search
uint8 air_vehicle_t::get_approach_ribi( koord3d start, koord3d ziel )
{
	uint8 dir = ribi_type(start, ziel); // reverse
	// make sure, there are at last two directions to choose, or you might en up with not route
	if(ribi_t::is_single(dir)) {
		dir |= (dir<<1);
		if(dir>16) {
			dir += 1;
		}
	}
	return dir&0x0F;
}
#endif


void air_vehicle_t::hop(grund_t* gr)
{
	sint32 new_speed_limit = SPEED_UNLIMITED;
	sint32 new_friction = 0;

	// take care of in-flight height ...
	const sint16 h_cur = (sint16)get_pos().z*TILE_HEIGHT_STEP;
	const sint16 h_next = (sint16)pos_next.z*TILE_HEIGHT_STEP;

	switch(state) {
		case departing: {
			flying_height = 0;
			target_height = h_cur;
			new_friction = max( 1, 28/(1+(route_index-takeoff)*2) ); // 9 5 4 3 2 2 1 1...

			// take off, when a) end of runway or b) last tile of runway or c) fast enough
			weg_t *weg=welt->lookup(get_pos())->get_weg(air_wt);
			if(  (weg==NULL  ||  // end of runway (broken runway)
				 weg->get_desc()->get_styp()!=type_runway  ||  // end of runway (grass now ... )
				 (route_index>takeoff+1  &&  ribi_t::is_single(weg->get_ribi_unmasked())) )  ||  // single ribi at end of runway
				 cnv->get_akt_speed()>kmh_to_speed(desc->get_topspeed())/3 // fast enough
			) {
				state = flying;
				new_friction = 1;
				block_reserver( takeoff, touchdown-1, false );
				flying_height = h_cur - h_next;
				target_height = h_cur+TILE_HEIGHT_STEP*3;
			}
			break;
		}
		case circling: {
			new_speed_limit = kmh_to_speed(desc->get_topspeed())/3;
			new_friction = 4;
			// do not change height any more while circling
			flying_height += h_cur;
			flying_height -= h_next;
			break;
		}
		case flying: {
			// since we are at a tile border, round up to the nearest value
			flying_height += h_cur;
			if(  flying_height < target_height  ) {
				flying_height = (flying_height+TILE_HEIGHT_STEP) & ~(TILE_HEIGHT_STEP-1);
			}
			else if(  flying_height > target_height  ) {
				flying_height = (flying_height-TILE_HEIGHT_STEP);
			}
			flying_height -= h_next;
			// did we have to change our flight height?
			if(  target_height-h_next > TILE_HEIGHT_STEP*5  ) {
				// Move down
				target_height -= TILE_HEIGHT_STEP*2;
			}
			else if(  target_height-h_next < TILE_HEIGHT_STEP*2  ) {
				// Move up
				target_height += TILE_HEIGHT_STEP*2;
			}
			break;
		}
		case landing: {
			new_speed_limit = kmh_to_speed(desc->get_topspeed())/3; // ==approach speed
			new_friction = 8;
			flying_height += h_cur;
			if(  flying_height < target_height  ) {
				flying_height = (flying_height+TILE_HEIGHT_STEP) & ~(TILE_HEIGHT_STEP-1);
			}
			else if(  flying_height > target_height  ) {
				flying_height = (flying_height-TILE_HEIGHT_STEP);
			}

			if (route_index >= touchdown)  {
				// come down, now!
				target_height = h_next;

				// touchdown!
				if (flying_height==h_next) {
					const sint32 taxi_speed = kmh_to_speed( min( 60, desc->get_topspeed()/4 ) );
					if(  cnv->get_akt_speed() <= taxi_speed  ) {
						new_speed_limit = taxi_speed;
						new_friction = 16;
					}
					else {
						const sint32 runway_left = search_for_stop - route_index;
						new_speed_limit = min( new_speed_limit, runway_left*runway_left*taxi_speed ); // ...approach 540 240 60 60
						const sint32 runway_left_fr = max( 0, 6-runway_left );
						new_friction = max( new_friction, min( desc->get_topspeed()/12, 4 + 4*(runway_left_fr*runway_left_fr+1) )); // ...8 8 12 24 44 72 108 152
					}
				}
			}
			else {
				// runway is on this height
				const sint16 runway_height = cnv->get_route()->at(touchdown).z*TILE_HEIGHT_STEP;

				// we are too low, ascent asap
				if (flying_height < runway_height + TILE_HEIGHT_STEP) {
					target_height = runway_height + TILE_HEIGHT_STEP;
				}
				// too high, descent
				else if (flying_height + h_next - h_cur > runway_height + (sint16)(touchdown-route_index-1)*TILE_HEIGHT_STEP) {
					target_height = runway_height +  (touchdown-route_index-1)*TILE_HEIGHT_STEP;
				}
			}
			flying_height -= h_next;
			break;
		}
		default: {
			new_speed_limit = kmh_to_speed( min( 60, desc->get_topspeed()/4 ) );
			new_friction = 16;
			flying_height = 0;
			target_height = h_next;
			break;
		}
	}

	// hop to next tile
	vehicle_t::hop(gr);

	speed_limit = new_speed_limit;
	current_friction = new_friction;

	// friction factors and speed limit may have changed
	// TODO use the same logic as in vehicle_t::hop
	cnv->must_recalc_data();
}


// this routine will display the aircraft (if in flight)
#ifdef MULTI_THREAD
void air_vehicle_t::display_after(int xpos_org, int ypos_org, const sint8 clip_num) const
#else
void air_vehicle_t::display_after(int xpos_org, int ypos_org, bool is_global) const
#endif
{
	if(  image != IMG_EMPTY  &&  !is_on_ground()  ) {
		int xpos = xpos_org, ypos = ypos_org;

		const int raster_width = get_current_tile_raster_width();
		const sint16 z = get_pos().z;
		if(  z + flying_height/TILE_HEIGHT_STEP - 1 > grund_t::underground_level  ) {
			return;
		}
		const sint16 target = target_height - ((sint16)z*TILE_HEIGHT_STEP);
		sint16 current_flughohe = flying_height;
		if(  current_flughohe < target  ) {
			current_flughohe += (steps*TILE_HEIGHT_STEP) >> 8;
		}
		else if(  current_flughohe > target  ) {
			current_flughohe -= (steps*TILE_HEIGHT_STEP) >> 8;
		}

		sint8 hoff = get_hoff();
		ypos += tile_raster_scale_y(get_yoff()-current_flughohe-hoff-2, raster_width);
		xpos += tile_raster_scale_x(get_xoff(), raster_width);
		get_screen_offset( xpos, ypos, raster_width );

		display_swap_clip_wh(CLIP_NUM_VAR);
		// will be dirty
		// the aircraft!!!
		display_color( image, xpos, ypos, get_player_nr(), true, true/*get_flag(obj_t::dirty)*/  CLIP_NUM_PAR);
#ifndef MULTI_THREAD
		vehicle_t::display_after( xpos_org, ypos_org - tile_raster_scale_y( current_flughohe - hoff - 2, raster_width ), is_global );
#endif
		display_swap_clip_wh(CLIP_NUM_VAR);
	}
#ifdef MULTI_THREAD
}
void air_vehicle_t::display_overlay(int xpos_org, int ypos_org) const
{
	if(  image != IMG_EMPTY  &&  !is_on_ground()  ) {
		const int raster_width = get_current_tile_raster_width();
		const sint16 z = get_pos().z;
		if(  z + flying_height/TILE_HEIGHT_STEP - 1 > grund_t::underground_level  ) {
			return;
		}
		const sint16 target = target_height - ((sint16)z*TILE_HEIGHT_STEP);
		sint16 current_flughohe = flying_height;
		if(  current_flughohe < target  ) {
			current_flughohe += (steps*TILE_HEIGHT_STEP) >> 8;
		}
		else if(  current_flughohe > target  ) {
			current_flughohe -= (steps*TILE_HEIGHT_STEP) >> 8;
		}

		vehicle_t::display_overlay( xpos_org, ypos_org - tile_raster_scale_y( current_flughohe - get_hoff() - 2, raster_width ) );
	}
#endif
	else if(  is_on_ground()  ) {
		// show loading tooltips on ground
#ifdef MULTI_THREAD
		vehicle_t::display_overlay( xpos_org, ypos_org );
#else
		vehicle_t::display_after( xpos_org, ypos_org, is_global );
#endif
	}
}


const char *air_vehicle_t::is_deletable(const player_t *player)
{
	if (is_on_ground()) {
		return vehicle_t::is_deletable(player);
	}
	return NULL;
}
