/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../simdebug.h"
#include "../simworld.h"
#include "../simobj.h"
#include "../boden/wege/schiene.h"
#include "../boden/grund.h"
#include "../display/simimg.h"
#include "../dataobj/ribi.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../utils/cbuffer_t.h"
#include "../tpl/vector_tpl.h"
#include "../vehicle/simvehicle.h"

#include "signal.h"


bool signal_t::any_autoblock = false;

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
				offset = (desc->is_pre_signal()  ||  desc->is_priority_signal()) ? 12 : 8;
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
 * taken). Updated on events only: a train's last vehicle leaving a block end or an autoblock
 * (rail_vehicle_t::leave_tile), a reservation freed (block_reserver) or taken over a switch that
 * joins a block from the side, placing, and loading.
 */

// the signal or station boundary on gr ends the block for a train leaving gr by one of exits
static bool ends_block(grund_t *gr, const weg_t *way, ribi_t::ribi exits)
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
				return true;
			}
		}
	}
	return false;
}


// false if a tile of the block entered from start in direction dir is reserved by another train than owner
static bool is_block_free(grund_t *start, ribi_t::ribi dir, convoihandle_t owner, waytype_t wt)
{
	if(  owner.is_bound()  ) {
		grund_t *to;
		if(  start->get_neighbour( to, wt, dir )  ) {
			schiene_t const* const sch = (schiene_t const*)to->get_weg( wt );
			if(  sch  &&  sch->get_reserved_convoi()==owner  ) {
				// the train at the signal holds its way through the block (green, as stock sets it)
				return true;
			}
		}
	}
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
			continue;
		}
		schiene_t const* const sch = (schiene_t const*)to->get_weg( wt );
		if(  sch==NULL  ) {
			continue;
		}
		if(  sch->get_ribi_maske() & d  ) {
			// a one-way signal we would pass against its direction: no train of this block goes that way
			continue;
		}
		if(  sch->is_reserved()  &&  sch->get_reserved_convoi()!=owner  ) {
			return false;
		}
		if(  ++tiles > AUTOBLOCK_MAX_TILES  ) {
			break;
		}
		const ribi_t::ribi exits = sch->get_ribi() & ~ribi_t::backward( d );
		if(  ends_block( to, sch, exits )  ) {
			continue;
		}
		for(  uint8 i=0;  i<4  &&  n<AUTOBLOCK_MAX_BRANCHES;  i++  ) {
			if(  exits & ribi_t::nsew[i]  ) {
				from[n] = to;
				dirs[n++] = ribi_t::nsew[i];
			}
		}
	}
	return true;
}


void signal_t::refresh_autoblock()
{
	const waytype_t wt = desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt;
	grund_t *gr = welt->lookup( get_pos() );
	schiene_t const* const sch = gr ? (schiene_t const*)gr->get_weg( wt ) : NULL;
	if(  sch==NULL  ) {
		return;
	}
	// the train that reserved up to or through this signal: its own reservation does not count
	const convoihandle_t owner = sch->get_reserved_convoi();
	const ribi_t::ribi ribi = sch->get_ribi_unmasked();
	bool free = true;
	for(  uint8 i=0;  i<4  &&  free;  i++  ) {
		if(  (ribi & ribi_t::nsew[i])  &&  applies_to( ribi_t::nsew[i] )  ) {
			free = is_block_free( gr, ribi_t::nsew[i], owner, wt );
		}
	}
	const signalstate aspect = free ? gruen : rot;
	if(  state!=aspect  ) {
		set_state( aspect );
	}
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
	int n = 0;
	const ribi_t::ribi back = way->get_ribi_unmasked() & ~exit_dir;
	for(  uint8 i=0;  i<4;  i++  ) {
		if(  back & ribi_t::nsew[i]  ) {
			from[n] = start;
			dirs[n++] = ribi_t::nsew[i];
		}
	}
	int tiles = 0;
	while(  n>0  &&  tiles<AUTOBLOCK_MAX_TILES  ) {
		n--;
		const ribi_t::ribi d = dirs[n];
		grund_t *to;
		if(  !from[n]->get_neighbour( to, wt, d )  ) {
			continue;
		}
		weg_t const* const w = to->get_weg( wt );
		if(  w==NULL  ) {
			continue;
		}
		tiles++;
		// a train here going towards pos leaves this tile in direction back
		const ribi_t::ribi towards = ribi_t::backward( d );
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
