/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../simdebug.h"
#include "../simworld.h"
#include "../simconvoi.h"
#include "../simobj.h"
#include "../boden/wege/schiene.h"
#include "../boden/grund.h"
#include "../display/simimg.h"
#include "../dataobj/ribi.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/route.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../utils/cbuffer_t.h"
#include "../tpl/vector_tpl.h"
#include "../vehicle/simvehicle.h"

#include "signal.h"


bool roadsign_t::any_autoblock = false;

// fork: autoblocks read from a save or just built, refreshed when the reservations are back
static vector_tpl<koord3d> loaded_autoblocks;

// fork: the longest block an autoblock looks at, and the most branches followed at once
#define AUTOBLOCK_MAX_TILES (256)
#define AUTOBLOCK_MAX_BRANCHES (16)


signal_t::signal_t(loadsave_t *file) :
	roadsign_t(file)
{
	if(desc==NULL) {
		desc = roadsign_t::default_signal;
	}
	state = rot;
}


void signal_t::finish_rd()
{
	roadsign_t::finish_rd();
	if(  desc->is_autoblock()  ) {
		// fork: its aspect needs the reservations, see refresh_loaded_autoblocks
		any_autoblock = true;
		loaded_autoblocks.append( get_pos() );
	}
}


void signal_t::info(cbuffer_t & buf) const
{
	// well, needs to be done
	obj_t::info(buf);

	buf.printf("%s\n%s%u", translator::translate(desc->get_name()), translator::translate("\ndirection:"), get_dir());

	if (char const* const maker = desc->get_copyright()) {
		buf.append("\n\n");
		buf.printf(translator::translate("Constructed by %s"), maker);
	}
}


void signal_t::calc_image()
{
	foreground_image = IMG_EMPTY;
	image_id image = IMG_EMPTY;

	after_xoffset = 0;
	after_yoffset = 0;
	sint8 xoff = 0, yoff = 0;
	const bool left_swap = welt->get_settings().is_signals_left()  &&  desc->get_offset_left();

	grund_t *gr = welt->lookup(get_pos());
	if(gr) {

		const slope_t::type full_hang = gr->get_weg_hang();
		const sint8 hang_diff = slope_t::max_diff(full_hang);
		const ribi_t::ribi hang_dir = ribi_t::backward( ribi_type(full_hang) );

		set_flag(obj_t::dirty);

		weg_t *sch = gr->get_weg(desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt);
		if(sch) {
			uint16 offset=0;
			ribi_t::ribi dir = sch->get_ribi_unmasked() & (~calc_mask());
			if(sch->is_electrified()  &&  (desc->get_count()/8)>1) {
				// fork: an autoblock with yellow images has sets of twelve like a pre-signal
				offset = (desc->is_pre_signal()  ||  desc->is_priority_signal()  ||  (desc->is_autoblock()  &&  desc->has_yellow_aspect())) ? 12 : 8;
			}

			// vertical offset of the signal positions
			if(full_hang==slope_t::flat) {
				yoff = -gr->get_weg_yoff();
				after_yoffset = yoff;
			}
			else {
				const ribi_t::ribi test_hang = left_swap ? ribi_t::backward(hang_dir) : hang_dir;
				if(test_hang==ribi_t::east ||  test_hang==ribi_t::north) {
					yoff = -TILE_HEIGHT_STEP*hang_diff;
					after_yoffset = 0;
				}
				else {
					yoff = 0;
					after_yoffset = -TILE_HEIGHT_STEP*hang_diff;
				}
			}

			// and now calculate the images:
			// we need to hide the "second" image on tunnel entries
			ribi_t::ribi temp_dir = dir;
			if(  gr->get_typ()==grund_t::tunnelboden  &&  gr->ist_karten_boden()  &&
				(grund_t::underground_mode==grund_t::ugm_none  ||  (grund_t::underground_mode==grund_t::ugm_level  &&  gr->get_hoehe()<grund_t::underground_level))   ) {
				// entering tunnel here: hide the image further in if not undergroud/sliced
				const ribi_t::ribi tunnel_hang_dir = ribi_t::backward( ribi_type(gr->get_grund_hang()) );
				if(  tunnel_hang_dir==ribi_t::east ||  tunnel_hang_dir==ribi_t::north  ) {
					temp_dir &= ~ribi_t::southwest;
				}
				else {
					temp_dir &= ~ribi_t::northeast;
				}
			}

			// signs for left side need other offsets and other front/back order
			if(  left_swap  ) {
				const sint16 XOFF = 2*desc->get_offset_left();
				const sint16 YOFF = desc->get_offset_left();

				if(temp_dir&ribi_t::east) {
					image = desc->get_image_id(3+state*4+offset);
					xoff += XOFF;
					yoff += -YOFF;
				}

				if(temp_dir&ribi_t::north) {
					if(image!=IMG_EMPTY) {
						foreground_image = desc->get_image_id(0+state*4+offset);
						after_xoffset += -XOFF;
						after_yoffset += -YOFF;
					}
					else {
						image = desc->get_image_id(0+state*4+offset);
						xoff += -XOFF;
						yoff += -YOFF;
					}
				}

				if(temp_dir&ribi_t::west) {
					foreground_image = desc->get_image_id(2+state*4+offset);
					after_xoffset += -XOFF;
					after_yoffset += YOFF;
				}

				if(temp_dir&ribi_t::south) {
					if(foreground_image!=IMG_EMPTY) {
						image = desc->get_image_id(1+state*4+offset);
						xoff += XOFF;
						yoff += YOFF;
					}
					else {
						foreground_image = desc->get_image_id(1+state*4+offset);
						after_xoffset += XOFF;
						after_yoffset += YOFF;
					}
				}
			}
			else {
				if(temp_dir&ribi_t::east) {
					foreground_image = desc->get_image_id(3+state*4+offset);
				}

				if(temp_dir&ribi_t::north) {
					if(foreground_image==IMG_EMPTY) {
						foreground_image = desc->get_image_id(0+state*4+offset);
					}
					else {
						image = desc->get_image_id(0+state*4+offset);
					}
				}

				if(temp_dir&ribi_t::west) {
					image = desc->get_image_id(2+state*4+offset);
				}

				if(temp_dir&ribi_t::south) {
					if(image==IMG_EMPTY) {
						image = desc->get_image_id(1+state*4+offset);
					}
					else {
						foreground_image = desc->get_image_id(1+state*4+offset);
					}
				}
			}
		}
	}
	set_xoff( xoff );
	set_yoff( yoff );
	set_image(image);
}

/* fork: automatic block signals (autoblok). Display only: nothing reads a signal's state for driving,
 * trains stop because their reservation fails. A block runs from the signal to the next signal or
 * station boundary that applies to a train going on; at a switch every branch counts (red if any is
 * taken), except that the train holding its way through the block is followed along that way. A block
 * is red while another train is in it, and once the head of the train that passed the signal is past
 * it. A free block is yellow when the signal or station boundary at its end is red (or it ends at a
 * buffer stop), green otherwise. Updated on events only: the head of a train passing an autoblock and
 * its last vehicle leaving a block end or an autoblock (rail_vehicle_t::enter_tile, leave_tile), a
 * reservation freed (block_reserver) or taken over a switch that joins a block from the side, any
 * rail signal or station boundary turning red or no longer red (roadsign_t::set_state), placing, and
 * loading. Only a change between red and not red is passed on, and an autoblock is red only by its
 * own block, so an update goes back one signal at most.
 */

// the signal or station boundary on gr that ends the block for a train leaving gr by one of exits
static const roadsign_t *get_block_end(grund_t *gr, const weg_t *way, ribi_t::ribi exits)
{
	const roadsign_t *rs = NULL;
	if(  way->has_signal()  ) {
		rs = gr->find<signal_t>();
	}
	else if(  way->has_sign()  ) {
		rs = rail_vehicle_t::get_station_boundary( gr );
	}
	if(  rs  ) {
		for(  uint8 i=0;  i<4;  i++  ) {
			if(  (exits & ribi_t::nsew[i])  &&  rs->applies_to( ribi_t::nsew[i] )  ) {
				return rs;
			}
		}
	}
	return NULL;
}


// the aspect for the block entered from start in direction dir: red if a tile is reserved by another
// train than owner (unless owner holds its way through it), else yellow if a block end is red
static roadsign_t::signalstate get_block_aspect(grund_t *start, ribi_t::ribi dir, convoihandle_t owner, waytype_t wt)
{
	bool ignore_others = false;
	if(  owner.is_bound()  ) {
		grund_t *to;
		if(  start->get_neighbour( to, wt, dir )  ) {
			schiene_t const* const sch = (schiene_t const*)to->get_weg( wt );
			if(  sch  &&  sch->get_reserved_convoi()==owner  ) {
				// the train at the signal holds its way through the block (never red, as stock sets it)
				ignore_others = true;
			}
		}
	}
	bool caution = false;
	grund_t *from[AUTOBLOCK_MAX_BRANCHES];
	ribi_t::ribi dirs[AUTOBLOCK_MAX_BRANCHES];
	int n = 0;
	from[n] = start;
	dirs[n++] = dir;
	int tiles = 0;
	while(  n>0  ) {
		n--;
		const ribi_t::ribi d = dirs[n];
		grund_t *to;
		if(  !from[n]->get_neighbour( to, wt, d )  ) {
			caution = true;
			continue;
		}
		schiene_t const* const sch = (schiene_t const*)to->get_weg( wt );
		if(  sch==NULL  ) {
			caution = true;
			continue;
		}
		if(  sch->get_ribi_maske() & d  ) {
			// a one-way signal we would pass against its direction: no train of this block goes that way
			continue;
		}
		if(  !ignore_others  &&  sch->is_reserved()  &&  sch->get_reserved_convoi()!=owner  ) {
			return roadsign_t::rot;
		}
		if(  ++tiles > AUTOBLOCK_MAX_TILES  ) {
			break;
		}
		ribi_t::ribi exits = sch->get_ribi() & ~ribi_t::backward( d );
		if(  const roadsign_t *end = get_block_end( to, sch, exits )  ) {
			if(  end->get_state()==roadsign_t::rot  ) {
				caution = true;
			}
			continue;
		}
		if(  exits==ribi_t::none  ) {
			// buffer stop
			caution = true;
		}
		else if(  ignore_others  &&  !ribi_t::is_single( exits )  ) {
			// a switch: the way the train at the signal reserved, if it reaches that far
			ribi_t::ribi taken = ribi_t::none;
			for(  uint8 i=0;  i<4;  i++  ) {
				grund_t *next;
				if(  (exits & ribi_t::nsew[i])  &&  to->get_neighbour( next, wt, ribi_t::nsew[i] )  ) {
					schiene_t const* const next_sch = (schiene_t const*)next->get_weg( wt );
					if(  next_sch  &&  next_sch->get_reserved_convoi()==owner  ) {
						taken |= ribi_t::nsew[i];
					}
				}
			}
			if(  taken!=ribi_t::none  ) {
				exits = taken;
			}
		}
		for(  uint8 i=0;  i<4  &&  n<AUTOBLOCK_MAX_BRANCHES;  i++  ) {
			if(  exits & ribi_t::nsew[i]  ) {
				from[n] = to;
				dirs[n++] = ribi_t::nsew[i];
			}
		}
	}
	return caution ? roadsign_t::naechste_rot : roadsign_t::gruen;
}


// true when the head of cnv, which holds the tile at pos, is past it (its way ahead does not run there)
static bool head_is_past(convoihandle_t cnv, koord3d pos)
{
	if(  !cnv.is_bound()  ||  cnv->get_vehicle_count()==0  ) {
		return false;
	}
	vehicle_t const* const front = cnv->front();
	if(  front->get_pos()==pos  ) {
		return false;
	}
	route_t const* const r = cnv->get_route();
	const uint32 count = r->get_count();
	if(  count==0  ) {
		return false;
	}
	// from the head's tile up to its reservation
	const uint32 head = front->get_route_index()>0 ? front->get_route_index()-1u : 0;
	uint32 to = cnv->get_next_reservation_index();
	if(  to<head+1  ) {
		to = head+1;
	}
	if(  to>count-1  ) {
		to = count-1;
	}
	for(  uint32 i=head;  i<=to;  i++  ) {
		if(  r->at(i)==pos  ) {
			return false;
		}
	}
	return true;
}


roadsign_t::signalstate signal_t::get_autoblock_aspect() const
{
	const waytype_t wt = desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt;
	grund_t *gr = welt->lookup( get_pos() );
	schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( wt ) : NULL;
	if(  sch==NULL  ) {
		return get_state();
	}
	// the train that reserved up to or through this signal: its own reservation does not count until
	// its head is past the signal
	const convoihandle_t owner = sch->get_reserved_convoi();
	signalstate aspect = gruen;
	if(  head_is_past( owner, get_pos() )  ) {
		aspect = rot;
	}
	else {
		const ribi_t::ribi ribi = sch->get_ribi_unmasked();
		for(  uint8 i=0;  i<4  &&  aspect!=rot;  i++  ) {
			if(  (ribi & ribi_t::nsew[i])  &&  applies_to( ribi_t::nsew[i] )  ) {
				const signalstate block = get_block_aspect( gr, ribi_t::nsew[i], owner, wt );
				if(  block==rot  ||  block==naechste_rot  ) {
					aspect = block;
				}
			}
		}
		if(  aspect==naechste_rot  &&  !desc->has_yellow_aspect()  ) {
			// no yellow images: green as before
			aspect = gruen;
		}
	}
	return aspect;
}


void signal_t::refresh_autoblock(bool tell_behind)
{
	const signalstate aspect = get_autoblock_aspect();
	if(  state!=aspect  ) {
		if(  tell_behind  ) {
			set_state( aspect );
		}
		else {
			state = aspect;
			calc_image();
		}
	}
}


void roadsign_t::refresh_autoblocks_ending_here()
{
	if(  preview  ||  desc==NULL  ||  welt->is_destroying()  ||  !(desc->is_signal_type()  ||  desc->is_station_boundary())  ) {
		return;
	}
	const waytype_t wt = desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt;
	if(  wt!=track_wt  &&  wt!=monorail_wt  &&  wt!=maglev_wt  &&  wt!=narrowgauge_wt  ) {
		return;
	}
	grund_t *gr = welt->lookup( get_pos() );
	weg_t const* const way = gr ? gr->get_weg( wt ) : NULL;
	if(  way==NULL  ) {
		return;
	}
	// back against the one direction it applies to, else every way
	const ribi_t::ribi ribi = way->get_ribi_unmasked();
	ribi_t::ribi exits = ribi_t::none;
	for(  uint8 i=0;  i<4;  i++  ) {
		if(  (ribi & ribi_t::nsew[i])  &&  applies_to( ribi_t::nsew[i] )  ) {
			exits |= ribi_t::nsew[i];
		}
	}
	signal_t::refresh_autoblocks_behind( get_pos(), ribi_t::is_single( exits ) ? exits : (ribi_t::ribi)ribi_t::none, wt );
}


void signal_t::refresh_autoblocks_behind(koord3d pos, ribi_t::ribi exit_dir, waytype_t wt)
{
	if(  !any_autoblock  ) {
		return;
	}
	grund_t *start = welt->lookup( pos );
	weg_t const* const way = start ? start->get_weg( wt ) : NULL;
	if(  way==NULL  ) {
		return;
	}
	grund_t *from[AUTOBLOCK_MAX_BRANCHES];
	ribi_t::ribi dirs[AUTOBLOCK_MAX_BRANCHES];
	int lengths[AUTOBLOCK_MAX_BRANCHES];
	int n = 0;
	const ribi_t::ribi back = way->get_ribi_unmasked() & ~exit_dir;
	for(  uint8 i=0;  i<4;  i++  ) {
		if(  back & ribi_t::nsew[i]  ) {
			from[n] = start;
			lengths[n] = 0;
			dirs[n++] = ribi_t::nsew[i];
		}
	}
	// each branch up to the longest block, and all of them together a few times that
	int tiles = 0;
	while(  n>0  &&  tiles<4*AUTOBLOCK_MAX_TILES  ) {
		n--;
		const ribi_t::ribi d = dirs[n];
		const int length = lengths[n]+1;
		grund_t *to;
		if(  length>AUTOBLOCK_MAX_TILES  ||  !from[n]->get_neighbour( to, wt, d )  ) {
			continue;
		}
		weg_t const* const w = to->get_weg( wt );
		if(  w==NULL  ) {
			continue;
		}
		tiles++;
		// a train here going towards pos leaves this tile in direction back
		const ribi_t::ribi towards = ribi_t::backward( d );
		if(  w->get_ribi_maske() & towards  ) {
			// a one-way signal no train passes towards pos
			continue;
		}
		if(  w->has_signal()  ) {
			signal_t *sig = to->find<signal_t>();
			if(  sig  &&  sig->applies_to( towards )  ) {
				if(  sig->get_desc()->is_autoblock()  ) {
					sig->refresh_autoblock();
				}
				continue;
			}
		}
		else if(  w->has_sign()  ) {
			const roadsign_t *lt = rail_vehicle_t::get_station_boundary( to );
			if(  lt  &&  lt->applies_to( towards )  ) {
				continue;
			}
		}
		const ribi_t::ribi next = w->get_ribi_unmasked() & ~towards;
		for(  uint8 i=0;  i<4  &&  n<AUTOBLOCK_MAX_BRANCHES;  i++  ) {
			if(  next & ribi_t::nsew[i]  ) {
				from[n] = to;
				lengths[n] = length;
				dirs[n++] = ribi_t::nsew[i];
			}
		}
	}
}


void signal_t::refresh_autoblocks_around(koord3d front, koord3d rear, waytype_t wt)
{
	if(  !any_autoblock  ||  (wt!=track_wt  &&  wt!=monorail_wt  &&  wt!=maglev_wt  &&  wt!=narrowgauge_wt)  ) {
		return;
	}
	refresh_autoblocks_behind( front, ribi_t::none, wt );
	if(  rear!=front  ) {
		refresh_autoblocks_behind( rear, ribi_t::none, wt );
	}
}


void signal_t::refresh_loaded_autoblocks()
{
	FOR( vector_tpl<koord3d>, const pos, loaded_autoblocks ) {
		if(  grund_t *gr = welt->lookup( pos )  ) {
			signal_t *sig = gr->find<signal_t>();
			if(  sig  &&  sig->get_desc()->is_autoblock()  ) {
				sig->refresh_autoblock();
			}
		}
	}
	loaded_autoblocks.clear();
}
