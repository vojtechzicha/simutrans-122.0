/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdlib.h>

#include "simdebug.h"
#include "simunits.h"
#include "simworld.h"
#include "simware.h"
#include "player/finance.h" // convert_money
#include "player/simplay.h"
#include "simconvoi.h"
#include "simhalt.h"
#include "simdepot.h"
#include "gui/simwin.h"
#include "simmenu.h"
#include "simcolor.h"
#include "simmesg.h"
#include "simintr.h"
#include "simlinemgmt.h"
#include "simline.h"
#include "freight_list_sorter.h"

#include "gui/minimap.h"
#include "gui/convoi_info_t.h"
#include "gui/schedule_gui.h"
#include "gui/depot_frame.h"
#include "gui/messagebox.h"
#include "gui/convoi_detail_t.h"
#include "boden/grund.h"
#include "boden/wege/schiene.h" // for railblocks

#include "descriptor/citycar_desc.h"
#include "descriptor/roadsign_desc.h"
#include "descriptor/vehicle_desc.h"

#include "dataobj/schedule.h"
#include "dataobj/route.h"
#include "dataobj/loadsave.h"
#include "dataobj/translator.h"
#include "dataobj/environment.h"

#include "display/viewport.h"

#include "obj/crossing.h"
#include "obj/roadsign.h"
#include "obj/wayobj.h"

#include "vehicle/simroadtraffic.h"
#include "vehicle/simvehicle.h"
#include "vehicle/overtaker.h"

#include "utils/simrandom.h"
#include "utils/simstring.h"
#include "utils/cbuffer_t.h"


/*
 * Waiting time for loading (ms)
 */
#define WTT_LOADING 2000


karte_ptr_t convoi_t::welt;

// fork, coupling: see below
static halthandle_t next_stop_halt(const schedule_t *sched, uint8 from, const player_t *owner);

/*
 * Debugging helper - translate state value to human readable name
 */
static const char * state_names[convoi_t::MAX_STATES] =
{
	"INITIAL",
	"EDIT_SCHEDULE",
	"ROUTING_1",
	"",
	"",
	"NO_ROUTE",
	"DRIVING",
	"LOADING",
	"WAITING_FOR_CLEARANCE",
	"WAITING_FOR_CLEARANCE_ONE_MONTH",
	"CAN_START",
	"CAN_START_ONE_MONTH",
	"SELF_DESTRUCT",
	"WAITING_FOR_CLEARANCE_TWO_MONTHS",
	"CAN_START_TWO_MONTHS",
	"LEAVING_DEPOT",
	"ENTERING_DEPOT",
	"COUPLED",
	"UNCOUPLING"
};


/**
 * Fork, mixed traction: true, if the vehicle pulls (or is no engine) in the given mode,
 * so its power, running cost and top speed count.
 */
static bool is_traction_active(const vehicle_desc_t *desc, bool mixed, bool off_wire, bool both_under_wire)
{
	if(  !mixed  ||  desc->get_power()==0  ) {
		return true;
	}
	if(  desc->get_engine_type()==vehicle_desc_t::electric  ) {
		return !off_wire;
	}
	return off_wire  ||  both_under_wire;
}


void convoi_t::init(player_t *player)
{
	owner = player;

	is_electric = false;
	traction_mixed = traction_off_wire = traction_both_under_wire = false;
	traction_top_speed_under_wire = traction_top_speed_off_wire = SPEED_UNLIMITED;
	traction_power_speed_under_wire = traction_power_speed_off_wire = SPEED_UNLIMITED;
	sum_gesamtweight = sum_weight = 0;
	sum_running_costs = sum_fixed_costs = sum_gear_and_power = previous_delta_v = 0;
	sum_power = 0;
	min_top_speed = SPEED_UNLIMITED;
	speedbonus_kmh = SPEED_UNLIMITED; // speed_to_kmh() not needed

	schedule = NULL;
	schedule_target = koord3d::invalid;
	line = linehandle_t();

	anz_vehikel = 0;
	steps_driven = -1;
	withdraw = false;
	has_obsolete = false;
	no_load = false;
	hold_marker = false;
	hold_divert = false;
	passing_hold_since = 0;
	passing_hold_released = false;
	coupled_first = 0;
	couple_wait_since = 0;
	couple_hold_slot = -1;
	running_late = false;
	late_slot = -1;
	uncouple_since = 0;
	uncouple_warned = false;
	wait_lock = 0;
	arrived_time = 0;

	jahresgewinn = 0;
	total_distance_traveled = 0;

	distance_since_last_stop = 0;
	sum_speed_limit = 0;
	maxspeed_average_count = 0;
	next_reservation_index = 0;

	alte_richtung = ribi_t::none;
	next_wolke = 0;

	state = INITIAL;

	*name_and_id = 0;
	name_offset = 0;

	freight_info_resort = true;
	freight_info_order = 0;
	loading_level = 0;
	loading_limit = 0;

	speed_limit = SPEED_UNLIMITED;
	max_record_speed = 0;
	brake_speed_soll = SPEED_UNLIMITED;
	akt_speed_soll = 0;            // target speed
	akt_speed = 0;                 // current speed
	sp_soll = 0;

	next_stop_index = 65535;

	line_update_pending = linehandle_t();

	home_depot = koord3d::invalid;

	recalc_data_front = true;
	recalc_data = true;
	recalc_speed_limit = true;
}


convoi_t::convoi_t(loadsave_t* file) : fahr(default_vehicle_length, NULL)
{
	self = convoihandle_t();
	init(0);
	rdwr(file);
}


convoi_t::convoi_t(player_t* player) : fahr(default_vehicle_length, NULL)
{
	self = convoihandle_t(this);
	player->book_convoi_number(1);
	init(player);
	set_name( "Unnamed" );
	welt->add_convoi( self );
	init_financial_history();
}


convoi_t::~convoi_t()
{
	owner->book_convoi_number( -1);

	assert(self.is_bound());
	assert(anz_vehikel==0);

	// close windows
	destroy_win( magic_convoi_info+self.get_id() );

DBG_MESSAGE("convoi_t::~convoi_t()", "destroying %d, %p", self.get_id(), this);
	// stop following
	if(welt->get_viewport()->get_follow_convoi()==self) {
		welt->get_viewport()->set_follow_convoi( convoihandle_t() );
	}

	welt->sync.remove( this );
	welt->rem_convoi( self );

	// if lineless convoy -> unregister from stops
	if(  !line.is_bound()  ) {
		unregister_stops();
	}

	// force asynchronous recalculation
	if(schedule) {
		if(!schedule->is_editing_finished()) {
			destroy_win((ptrdiff_t)schedule);
		}
		if (!schedule->empty() && !line.is_bound()) {
			welt->set_schedule_counter();
		}
		delete schedule;
	}

	// deregister from line (again)
	unset_line();

	self.detach();
}


// waypoint: no stop, resp. for airplanes in air (i.e. no air strip below)
bool convoi_t::is_waypoint( koord3d ziel ) const
{
	if(  fahr[0]->get_waytype() == air_wt  ) {
		// separate logic for airplanes, since the can have waypoints over stops etc.
		grund_t *gr = welt->lookup_kartenboden(ziel.get_2d());
		if(  gr == NULL  ||  gr->get_weg(air_wt) == NULL  ) {
			// during flight always a waypoint
			return true;
		}
		else if(  gr->get_depot()  ) {
			// but a depot is not a waypoint
			return false;
		}
		// so we are on a taxiway/runway here ...
	}
	return !haltestelle_t::get_halt(ziel,get_owner()).is_bound();
}


/**
 * unreserves the whole remaining route
 */
void convoi_t::unreserve_route()
{
	// need a route, vehicles, and vehicles must belong to this convoi
	// (otherwise crash during loading when fahr[0]->convoi is not initialized yet
	if(  !route.empty()  &&  anz_vehikel>0  &&  fahr[0]->get_convoi() == this  ) {
		rail_vehicle_t* lok = dynamic_cast<rail_vehicle_t*>(fahr[0]);
		if (lok) {
			// free all reserved blocks
			uint16 dummy;
			lok->block_reserver(get_route(), back()->get_route_index(), dummy, dummy,  100000, false, true);
		}
	}
}


/**
 * reserves route until next_reservation_index
 */
void convoi_t::reserve_route()
{
	if(  !route.empty()  &&  anz_vehikel>0  &&  (is_waiting()  ||  state==DRIVING  ||  state==LEAVING_DEPOT)  ) {
		for(  int idx = back()->get_route_index();  idx < next_reservation_index  /*&&  idx < route.get_count()*/;  idx++  ) {
			if(  grund_t *gr = welt->lookup( route.at(idx) )  ) {
				if(  schiene_t *sch = (schiene_t *)gr->get_weg( front()->get_waytype() )  ) {
					sch->reserve( self, ribi_type( route.at(max(1u,idx)-1u), route.at(min(route.get_count()-1u,idx+1u)) ) );
				}
			}
		}
	}
}

/**
 * Sets route_index of all vehicles to startindex.
 * Puts all vehicles on tile at this position in the route.
 * Convoy stills needs to be pushed that the convoy is right on track.
 * @returns length of convoy minus last vehicle
 */
uint32 convoi_t::move_to(uint16 const start_index)
{
	steps_driven = -1;
	koord3d k = route.at(start_index);
	grund_t* gr = welt->lookup(k);

	uint32 train_length = 0;
	for (unsigned i = 0; i != anz_vehikel; ++i) {
		vehicle_t& v = *fahr[i];

		if(  grund_t const* gr = welt->lookup(v.get_pos())  ) {
			v.mark_image_dirty(v.get_image(), 0);
			v.leave_tile();
			// maybe unreserve this
			if(  schiene_t* const rails = obj_cast<schiene_t>(gr->get_weg(v.get_waytype()))  ) {
				// fork, coupling: a tile of the train we just uncoupled stays ours until we leave it
				if(  !handover_to.is_bound()  ||  !handover_to->uncouple_span.is_contained( v.get_pos() )  ) {
					rails->unreserve(&v);
				}
			}
		}
		// propagate new index to vehicle, will set all movement related variables, in particular pos
		v.initialise_journey(start_index, true);
		// now put vehicle on the tile
		if (gr) {
			v.enter_tile(gr);
		}
		// fork, coupling: a train that was off the map after uncoupling is on it now
		v.clear_flag( obj_t::not_on_map );

		if (i != anz_vehikel - 1U) {
			train_length += v.get_desc()->get_length();
		}
	}
	return train_length;
}


void convoi_t::finish_rd()
{
	if(schedule==NULL) {
		if(  state!=INITIAL  ) {
			grund_t *gr = welt->lookup(home_depot);
			if(gr  &&  gr->get_depot()) {
				dbg->warning( "convoi_t::finish_rd()","No schedule during loading convoi %i: State will be initial!", self.get_id() );
				for( uint8 i=0;  i<anz_vehikel;  i++ ) {
					fahr[i]->set_pos(home_depot);
				}
				state = INITIAL;
			}
			else {
				dbg->error( "convoi_t::finish_rd()","No schedule during loading convoi %i: Convoi will be destroyed!", self.get_id() );
				for( uint8 i=0;  i<anz_vehikel;  i++ ) {
					fahr[i]->set_pos(koord3d::invalid);
				}
				destroy();
				return;
			}
		}
		// anyway reassign convoi pointer ...
		for( uint8 i=0;  i<anz_vehikel;  i++ ) {
			vehicle_t* v = fahr[i];
			v->set_convoi(this);
			if(  state!=INITIAL  &&  welt->lookup(v->get_pos())  ) {
				// mark vehicle as used
				v->set_driven();
			}
		}
		return;
	}
	else {
		// restore next schedule target for non-stop waypoint handling
		const koord3d ziel = schedule->get_current_entry().pos;
		if(  anz_vehikel>0  &&  is_waypoint(ziel)  ) {
			schedule_target = ziel;
		}
	}

	// fork, coupling: a primary takes the vehicles of the train joined to it
	if(  coupled_convoi.is_bound()  &&  state!=COUPLED  &&  state!=UNCOUPLING  ) {
		convoi_t *c = coupled_convoi.get_rep();
		if(  c->state==COUPLED  &&  c->coupled_convoi==self  &&  c->anz_vehikel>0  &&  anz_vehikel>0  ) {
			coupled_first = anz_vehikel;
			fahr.resize( anz_vehikel + c->anz_vehikel, NULL );
			for(  uint8 i=0;  i<c->anz_vehikel;  i++  ) {
				vehicle_t *v = c->fahr[i];
				v->set_leading( false );
				v->set_convoi( this );
				fahr[anz_vehikel++] = v;
				sum_power += v->get_desc()->get_power();
				sum_weight += v->get_desc()->get_weight();
				// the tile was reserved for the joined train while loading
				if(  grund_t *gr = welt->lookup( v->get_pos() )  ) {
					if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( v->get_waytype() ) )  ) {
						sch->unreserve( coupled_convoi );
						sch->reserve( self, ribi_t::none );
					}
				}
			}
			sum_gesamtweight = sum_weight;
			calc_loading();
			recalc_traction( false );
		}
		else {
			dbg->error( "convoi_t::finish_rd()", "convoi %i: the joined train %i is gone", self.get_id(), coupled_convoi.get_id() );
			coupled_convoi = convoihandle_t();
		}
	}
	if(  state==COUPLED  &&  (!coupled_convoi.is_bound()  ||  coupled_convoi->coupled_convoi!=self)  ) {
		dbg->error( "convoi_t::finish_rd()", "convoi %i: coupled without a primary, standing on its own again", self.get_id() );
		coupled_convoi = convoihandle_t();
		state = ROUTING_1;
		welt->sync.add( this );
	}

	bool realign_position = false;
	if(  anz_vehikel>0  &&  (state==COUPLED  ||  state==UNCOUPLING)  ) {
		// fork, coupling: the primary drives our vehicles, or they are off the map for now
		if(  state==UNCOUPLING  ) {
			for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
				fahr[i]->set_leading( false );
				fahr[i]->set_last( i+1==anz_vehikel );
				fahr[i]->set_convoi( this );
			}
		}
		if(  line.is_bound()  ) {
			line->add_convoy( self );
		}
		else {
			register_stops();
		}
		if(  state==UNCOUPLING  ) {
			check_freight();
		}
		recalc_catg_index();
		return;
	}
	if(  anz_vehikel>0  ) {
DBG_MESSAGE("convoi_t::finish_rd()","state=%s, next_stop_index=%d", state_names[state], next_stop_index );
		// only realign convois not leaving depot to avoid jumps through signals
		if(  steps_driven!=-1  ) {
			for( uint8 i=0;  i<anz_vehikel;  i++ ) {
				vehicle_t* v = fahr[i];
				v->set_leading( i==0 );
				v->set_last( i+1==anz_vehikel );
				v->calc_height();
				// this sets the convoi and will renew the block reservation, if needed!
				v->set_convoi(this);
			}
		}
		else {
			// test also for realignment
			sint16 step_pos = 0;
			koord3d drive_pos;
			uint8 const diagonal_vehicle_steps_per_tile = (uint8)(130560U / welt->get_settings().get_pak_diagonal_multiplier());
			for( uint8 i=0;  i<anz_vehikel;  i++ ) {
				vehicle_t* v = fahr[i];
				v->set_leading( i==0 );
				v->set_last( i+1==anz_vehikel );
				v->calc_height();
				// this sets the convoi and will renew the block reservation, if needed!
				v->set_convoi(this);

				// wrong alignment here => must relocate
				if(v->need_realignment()) {
					// diagonal => convoi must restart
					realign_position |= ribi_t::is_bend(v->get_direction())  &&  (state==DRIVING  ||  is_waiting());
				}
				// if version is 99.17 or lower, some convois are broken, i.e. had too large gaps between vehicles
				if(  !realign_position  &&  state!=INITIAL  &&  state!=LEAVING_DEPOT  ) {
					if(  i==0  ) {
						step_pos = v->get_steps();
					}
					else {
						if(  drive_pos!=v->get_pos()  ) {
							// with long vehicles on diagonals, vehicles need not to be on consecutive tiles
							// do some guessing here
							uint32 dist = koord_distance(drive_pos, v->get_pos());
							if (dist>1) {
								step_pos += (dist-1) * diagonal_vehicle_steps_per_tile;
							}
							step_pos += ribi_t::is_bend(v->get_direction()) ? diagonal_vehicle_steps_per_tile : VEHICLE_STEPS_PER_TILE;
						}
						dbg->message("convoi_t::finish_rd()", "v: pos(%s) steps(%d) len=%d ribi=%d prev (%s) step(%d)", v->get_pos().get_str(), v->get_steps(), v->get_desc()->get_length()*16, v->get_direction(),  drive_pos.get_2d().get_str(), step_pos);
						if(  abs( v->get_steps() - step_pos )>15  ) {
							// not where it should be => realign
							realign_position = true;
							dbg->warning( "convoi_t::finish_rd()", "convoi (%s) is broken => realign", get_name() );
						}
					}
					step_pos -= v->get_desc()->get_length_in_steps();
					drive_pos = v->get_pos();
				}
			}
		}
DBG_MESSAGE("convoi_t::finish_rd()","next_stop_index=%d", next_stop_index );

		linehandle_t new_line  = line;
		if(  !new_line.is_bound()  ) {
			// if there is a line with id=0 in the savegame try to assign cnv to this line
			new_line = get_owner()->simlinemgmt.get_line_with_id_zero();
		}
		if(  new_line.is_bound()  ) {
			if (  !schedule->matches( welt, new_line->get_schedule() )  ) {
				// 101 version produced broken line ids => we have to find our line the hard way ...
				vector_tpl<linehandle_t> lines;
				get_owner()->simlinemgmt.get_lines(schedule->get_type(), &lines);
				new_line = linehandle_t();
				FOR(vector_tpl<linehandle_t>, const l, lines) {
					if(  schedule->matches( welt, l->get_schedule() )  ) {
						// if a line is assigned, set line!
						new_line = l;
						break;
					}
				}
			}
			// now the line should match our schedule or else ...
			if(new_line.is_bound()) {
				line = new_line;
				line->add_convoy(self);
				DBG_DEBUG("convoi_t::finish_rd()","%s registers for %d", name_and_id, line.get_id());
			}
			else {
				line = linehandle_t();
			}
		}
	}
	else {
		// no vehicles in this convoi?!?
		dbg->error( "convoi_t::finish_rd()","No vehicles in Convoi %i: will be destroyed!", self.get_id() );
		destroy();
		return;
	}
	// put convoi again right on track?
	if(realign_position  &&  anz_vehikel>1) {
		// display just a warning
		dbg->warning("convoi_t::finish_rd()","cnv %i is currently too long.",self.get_id());

		if (route.empty()) {
			// realigning needs a route
			state = NO_ROUTE;
			owner->report_vehicle_problem( self, koord3d::invalid );
			dbg->error( "convoi_t::finish_rd()", "No valid route, but needs realignment at (%s)!", fahr[0]->get_pos().get_str() );
		}
		else {
			// since start may have been changed
			uint16 start_index = max(1,fahr[anz_vehikel-1]->get_route_index())-1;
			if (start_index > route.get_count()) {
				dbg->error( "convoi_t::finish_rd()", "Routeindex of last vehicle of (%s) too large!", get_name() );
				start_index = 0;
			}

			uint32 train_length = move_to(start_index) + 1;
			const koord3d last_start = fahr[0]->get_pos();

			// now advance all convoi until it is completely on the track
			fahr[0]->set_leading(false); // switches off signal checks ...
			for(unsigned i=0; i<anz_vehikel; i++) {
				vehicle_t* v = fahr[i];

				v->get_smoke(false);
				fahr[i]->do_drive( (VEHICLE_STEPS_PER_CARUNIT*train_length)<<YARDS_PER_VEHICLE_STEP_SHIFT );
				train_length -= v->get_desc()->get_length();
				v->get_smoke(true);

				// eventually reserve this again
				grund_t *gr=welt->lookup(v->get_pos());
				// airplanes may have no ground ...
				if (schiene_t* const sch0 = obj_cast<schiene_t>(gr->get_weg(fahr[i]->get_waytype()))) {
					sch0->reserve(self,ribi_t::none);
				}
			}
			fahr[0]->set_leading(true);
			if(  state != INITIAL  &&  state != EDIT_SCHEDULE  &&  fahr[0]->get_pos() != last_start  ) {
				state = WAITING_FOR_CLEARANCE;
			}
		}
	}
	if(  state==LOADING  ) {
		// the fully the shorter => register again as older convoi
		wait_lock = 2000-loading_level*20;
	}
	// when saving with open window, this can happen
	if(  state==EDIT_SCHEDULE  ) {
		if (env_t::networkmode) {
			wait_lock = 30000; // 60s to drive on, if the client in question had left
		}
		schedule->finish_editing();
	}
	// remove wrong freight
	check_freight();
	// some convois had wrong old direction in them
	if(  state<DRIVING  ||  state==LOADING  ) {
		alte_richtung = fahr[0]->get_direction();
	}
	// if lineless convoy -> register itself with stops
	if(  !line.is_bound()  ) {
		register_stops();
	}

	calc_speedbonus_kmh();
}


// since now convoi states go via tool_t
void convoi_t::call_convoi_tool( const char function, const char *extra ) const
{
	tool_t *tmp_tool = create_tool( TOOL_CHANGE_CONVOI | SIMPLE_TOOL );
	cbuffer_t param;
	param.printf("%c,%u", function, self.get_id());
	if(  extra  &&  *extra  ) {
		param.printf(",%s", extra);
	}
	tmp_tool->set_default_param(param);
	welt->set_tool( tmp_tool, get_owner() );
	// since init always returns false, it is safe to delete immediately
	delete tmp_tool;
}


void convoi_t::rotate90( const sint16 y_size )
{
	record_pos.rotate90( y_size );
	home_depot.rotate90( y_size );
	route.rotate90( y_size );
	if(  schedule_target!=koord3d::invalid  ) {
		schedule_target.rotate90( y_size );
	}
	if(schedule) {
		schedule->rotate90( y_size );
	}
	for(  int i=0;  i<get_own_vehicle_count();  i++  ) {
		fahr[i]->rotate90_freight_destinations( y_size );
	}
	for(  uint32 i=0;  i<uncouple_span.get_count();  i++  ) {
		uncouple_span[i].rotate90( y_size );
	}
	// eventually correct freight destinations (and remove all stale freight)
	check_freight();
}


/**
 * Return the convoi position.
 * @return Convoi position
 */
koord3d convoi_t::get_pos() const
{
	if(anz_vehikel > 0 && fahr[0]) {
		return state==INITIAL ? home_depot : fahr[0]->get_pos();
	}
	else {
		return koord3d::invalid;
	}
}


/**
 * Sets the name. Creates a copy of name.
 */
void convoi_t::set_name(const char *name, bool with_new_id)
{
	if(  with_new_id  ) {
		char buf[128];
		name_offset = sprintf(buf,"(%i) ",self.get_id() );
		tstrncpy(buf + name_offset, translator::translate(name, welt->get_settings().get_name_language_id()), lengthof(buf) - name_offset);
		tstrncpy(name_and_id, buf, lengthof(name_and_id));
	}
	else {
		char buf[128];
		// check if there is a id in the name string
		name_offset = sprintf(buf,"(%i) ",self.get_id() );
		if(  strlen(name) < name_offset  ||  strncmp(buf,name,name_offset)!=0) {
			name_offset = 0;
		}
		tstrncpy(buf+name_offset, name+name_offset, sizeof(buf)-name_offset);
		tstrncpy(name_and_id, buf, lengthof(name_and_id));
	}
	// now tell the windows that we were renamed
	convoi_info_t *info = dynamic_cast<convoi_info_t*>(win_get_magic( magic_convoi_info+self.get_id()));
	if (info) {
		info->update_data();
	}
	if(  in_depot()  ) {
		const grund_t *const ground = welt->lookup( get_home_depot() );
		if(  ground  ) {
			const depot_t *const depot = ground->get_depot();
			if(  depot  ) {
				depot_frame_t *const frame = dynamic_cast<depot_frame_t *>( win_get_magic( (ptrdiff_t)depot ) );
				if(  frame  ) {
					frame->update_data();
				}
			}
		}
	}
}


// length of convoi (16 is one tile)
uint32 convoi_t::get_length() const
{
	uint32 len = 0;
	for( uint8 i=0; i<anz_vehikel; i++ ) {
		len += fahr[i]->get_desc()->get_length();
	}
	return len;
}


/**
 * convoi add their running cost for traveling one tile
 */
void convoi_t::add_running_cost( const weg_t *weg )
{
	if(  is_coupled_primary()  ) {
		// fork, coupling: the vehicles of the joined train cost its line
		sint32 coupled_costs = 0;
		for(  uint8 i=coupled_first;  i<anz_vehikel;  i++  ) {
			if(  !fahr[i]->is_idle()  ) {
				coupled_costs -= fahr[i]->get_desc()->get_running_cost();
			}
		}
		convoi_t *c = coupled_convoi.get_rep();
		c->jahresgewinn += coupled_costs;
		c->book( coupled_costs, CONVOI_OPERATIONS );
		c->book( coupled_costs, CONVOI_PROFIT );
		c->book( 1, CONVOI_DISTANCE );
		c->total_distance_traveled ++;
		get_owner()->book_running_costs( coupled_costs, get_schedule()->get_waytype() );
		if(  weg  &&  weg->get_owner()!=get_owner()  &&  weg->get_owner()!=NULL  ) {
			// its share of the toll on a foreign way that depends on running costs
			// (the maintenance share is paid once, by us, for the whole train)
			const sint32 toll = -(coupled_costs*welt->get_settings().get_way_toll_runningcost_percentage())/100l;
			weg->get_owner()->book_toll_received( toll, get_schedule()->get_waytype() );
			get_owner()->book_toll_paid( -toll, get_schedule()->get_waytype() );
			c->book( -toll, CONVOI_WAYTOLL );
			c->book( -toll, CONVOI_PROFIT );
		}
		// the rest is ours
		const sint32 all_costs = sum_running_costs;
		sum_running_costs -= coupled_costs;
		add_running_cost_own( weg );
		sum_running_costs = all_costs;
		return;
	}
	add_running_cost_own( weg );
}


void convoi_t::add_running_cost_own( const weg_t *weg )
{
	jahresgewinn += sum_running_costs;

	if(  weg  &&  weg->get_owner()!=get_owner()  &&  weg->get_owner()!=NULL  ) {
		// running on non-public way costs toll (since running costs are positive => invert)
		sint32 toll = -(sum_running_costs*welt->get_settings().get_way_toll_runningcost_percentage())/100l;
		if(  welt->get_settings().get_way_toll_waycost_percentage()  ) {
			if(  weg->is_electrified()  &&  draws_electricity()  ) {
				// toll for using electricity
				grund_t *gr = welt->lookup(weg->get_pos());
				for(  int i=1;  i<gr->get_top();  i++  ) {
					obj_t *d=gr->obj_bei(i);
					if(  wayobj_t const* const wo = obj_cast<wayobj_t>(d)  )  {
						if(  wo->get_waytype()==weg->get_waytype()  ) {
							toll += (wo->get_desc()->get_maintenance()*welt->get_settings().get_way_toll_waycost_percentage())/100l;
							break;
						}
					}
				}
			}
			// now add normal way toll be maintenance
			toll += (weg->get_desc()->get_maintenance()*welt->get_settings().get_way_toll_waycost_percentage())/100l;
		}
		weg->get_owner()->book_toll_received( toll, get_schedule()->get_waytype() );
		get_owner()->book_toll_paid(         -toll, get_schedule()->get_waytype() );
		book( -toll, CONVOI_WAYTOLL);
		book( -toll, CONVOI_PROFIT);

	}
	get_owner()->book_running_costs( sum_running_costs, get_schedule()->get_waytype());

	book( sum_running_costs, CONVOI_OPERATIONS );
	book( sum_running_costs, CONVOI_PROFIT );

	total_distance_traveled ++;
	distance_since_last_stop++;

	sint32 tile_speed = min( min_top_speed, speed_limit );
	if(  traction_mixed  ) {
		// fork: the engines that pull here may not reach their top speed with this load (diesel off wires)
		tile_speed = min( tile_speed, traction_off_wire ? traction_power_speed_off_wire : traction_power_speed_under_wire );
	}
	sum_speed_limit += speed_to_kmh( tile_speed );
	book( 1, CONVOI_DISTANCE );
}


/**
 * Returns residual power given power, weight, and current speed.
 * @param speed (in internal speed unit)
 * @param total_power sum of power times gear (see calculation of sum_gear_and_power)
 * @param friction_weight weight including friction of the convoy
 * @param total_weight weight of the convoy
 * @returns residual power
 */
static inline sint32 res_power(sint64 speed, sint32 total_power, sint64 friction_weight, sint64 total_weight)
{
	sint32 res = total_power - (sint32)( ( (sint64)speed * ( (friction_weight * (sint64)speed ) / 3125ll + 1ll) ) / 2048ll + (total_weight * 64ll) / 1000ll);
	return res;
}

/* Calculates (and sets) new akt_speed
 * needed for driving, entering and leaving a depot)
 */
void convoi_t::calc_acceleration(uint32 delta_t)
{

	if(  !recalc_data  &&  !recalc_speed_limit  &&  !recalc_data_front  &&  (
		(sum_friction_weight == sum_gesamtweight  &&  akt_speed_soll <= akt_speed  &&  akt_speed_soll+24 >= akt_speed)  ||
		(sum_friction_weight > sum_gesamtweight  &&  akt_speed_soll == akt_speed)  )
		) {
		// at max speed => go with max speed and finish calculation here
		// at slopes/curves, only do this if there is absolutely now change
		akt_speed = akt_speed_soll;
		return;
	}

	// only compute this if a vehicle in the convoi hopped
	if(  recalc_data  ||  recalc_speed_limit  ) {
		// calculate total friction and lowest speed limit
		const vehicle_t* v = front();
		speed_limit = min( min_top_speed, v->get_speed_limit() );
		if (recalc_data) {
			sum_gesamtweight   = v->get_total_weight();
			sum_friction_weight = v->get_frictionfactor() * sum_gesamtweight;
		}

		for(  unsigned i=1; i<anz_vehikel; i++  ) {
			const vehicle_t* v = fahr[i];
			speed_limit = min( speed_limit, v->get_speed_limit() );

			if (recalc_data) {
				int total_vehicle_weight = v->get_total_weight();
				sum_friction_weight += v->get_frictionfactor() * total_vehicle_weight;
				sum_gesamtweight += total_vehicle_weight;
			}
		}
		recalc_data = recalc_speed_limit = false;
		akt_speed_soll = min( speed_limit, brake_speed_soll );
	}

	if(  recalc_data_front  ) {
		// brake at the end of stations/in front of signals and crossings
		const uint32 tiles_left = 1 + get_next_stop_index() - front()->get_route_index();
		brake_speed_soll = SPEED_UNLIMITED;
		if(  tiles_left < 4  ) {
			static sint32 brake_speed_countdown[4] = {
				kmh_to_speed(25),
				kmh_to_speed(50),
				kmh_to_speed(100),
				kmh_to_speed(200)
			};
			brake_speed_soll = brake_speed_countdown[tiles_left];
		}
		akt_speed_soll = min( speed_limit, brake_speed_soll );
		recalc_data_front = false;
	}

	// more pleasant and a little more "physical" model

	// try to simulate quadratic friction
	if(sum_gesamtweight != 0) {
		/*
		 * The parameter consist of two parts (optimized for good looking):
		 *  - every vehicle in a convoi has a the friction of its weight
		 *  - the dynamic friction is calculated that way, that v^2*weight*frictionfactor = 200 kW
		 *    This means that if a vehicle is loaded heavier and/or travels faster, less
		 *    power for acceleration is available.
		 *    since delta_t can have any value, we have to scale the step size by this value.
		 *    However, there is a quadratic friction term => if delta_t is too large the calculation may get weird results
		 *
		 * but for integer, we have to use the order below and calculate actually 64*deccel, like the sum_gear_and_power
		 * since akt_speed=10/128 km/h and we want 64*200kW=(100km/h)^2*100t, we must multiply by (128*2)/100
		 * But since the acceleration was too fast, we just decelerate 4x more => >>6 instead >>8
		 */
		//sint32 deccel = ( ( (akt_speed*sum_friction_weight)>>6 )*(akt_speed>>2) ) / 25 + (sum_gesamtweight*64); // this order is needed to prevent overflows!
		//sint32 deccel = (sint32)( ( (sint64)akt_speed * (sint64)sum_friction_weight * (sint64)akt_speed ) / (25ll*256ll) + sum_gesamtweight * 64ll) / 1000ll; // intermediate still overflows so sint64
		//sint32 deccel = (sint32)( ( (sint64)akt_speed * ( (sum_friction_weight * (sint64)akt_speed ) / 3125ll + 1ll) ) / 2048ll + (sum_gesamtweight * 64ll) / 1000ll);

		// note: result can overflow sint32 and double so we use sint64. Planes are ok.
		//sint32 delta_v =  (sint32)( ( (double)( (akt_speed>akt_speed_soll?0l:sum_gear_and_power) - deccel)*(double)delta_t)/(double)sum_gesamtweight);

		sint64 residual_power = res_power(akt_speed, akt_speed>akt_speed_soll? 0l : sum_gear_and_power, sum_friction_weight, sum_gesamtweight);

		// we normalize delta_t to 1/64th and check for speed limit */
		//sint32 delta_v = ( ( (akt_speed>akt_speed_soll?0l:sum_gear_and_power) - deccel) * delta_t)/sum_gesamtweight;
		sint64 delta_v = ( residual_power * (sint64)delta_t * 1000ll) / (sint64)sum_gesamtweight;

		// we need more accurate arithmetic, so we store the previous value
		delta_v += previous_delta_v;
		previous_delta_v = (uint16) (delta_v & 0x00000FFFll);
		// and finally calculate new speed
		akt_speed = max(akt_speed_soll>>4, akt_speed+(sint32)(delta_v>>12l) );
	}
	else {
		// very old vehicle ...
		akt_speed += 16;
	}

	// obey speed maximum with additional const brake ...
	if(akt_speed > akt_speed_soll) {
		if (akt_speed > akt_speed_soll + 24) {
			akt_speed -= 24;
			if(akt_speed > akt_speed_soll+kmh_to_speed(20)) {
				akt_speed = akt_speed_soll+kmh_to_speed(20);
			}
		}
		else {
			akt_speed = akt_speed_soll;
		}
	}

	// new record?
	if(akt_speed > max_record_speed) {
		max_record_speed = akt_speed;
		record_pos = fahr[0]->get_pos().get_2d();
	}
}


/**
 * Calculates maximal possible speed.
 * Uses iterative technique to take care of integer arithmetic.
 */
sint32 convoi_t::calc_max_speed(uint64 total_power, uint64 total_weight, sint32 speed_limit)
{
	// precision is 0.5 km/h
	const sint32 tol = kmh_to_speed(1)/2;
	// bisection to find max speed
	sint32 pl,pr,pm;
	sint64 sl,sr,sm;

	// test speed_limit
	sr = speed_limit;
	pr = res_power(sr, (sint32)total_power, total_weight, total_weight);
	if (pr >= 0) {
		return (sint32)sr; // convoy can travel at speed given by speed_limit
	}
	sl = 1;
	pl = res_power(sl, (sint32)total_power, total_weight, total_weight);
	if (pl <= 0) {
		return 0; // no power to move at all
	}

	// bisection algorithm to find speed for which residual power is zero
	while (sr - sl > tol) {
		sm = (sl + sr)/2;
		if (sm == sl) break;

		pm = res_power(sm, (sint32)total_power, total_weight, total_weight);

		if (((sint64)pl)*pm <= 0) {
			pr = pm;
			sr = sm;
		}
		else {
			pl = pm;
			sl = sm;
		}
	}
	return (sint32)sl;
}


int convoi_t::get_vehicle_at_length(uint16 length)
{
	int current_length = 0;
	for( int i=0;  i<anz_vehikel;  i++  ) {
		current_length += fahr[i]->get_desc()->get_length();
		if(length<current_length) {
			return i;
		}
	}
	return anz_vehikel;
}


// moves all vehicles of a convoi
sync_result convoi_t::sync_step(uint32 delta_t)
{
	// still have to wait before next action?
	wait_lock -= delta_t;
	if(wait_lock > 0) {
		return SYNC_OK;
	}
	wait_lock = 0;

	switch(state) {
		case INITIAL:
			// in depot, should not be in sync list, remove
			return SYNC_REMOVE;

		case EDIT_SCHEDULE:
		case ROUTING_1:
		case DUMMY4:
		case DUMMY5:
		case NO_ROUTE:
		case CAN_START:
		case CAN_START_ONE_MONTH:
		case CAN_START_TWO_MONTHS:
			// this is an async task, see step()
			break;

		case ENTERING_DEPOT:
			break;

		case LEAVING_DEPOT:
			{
				// ok, so we will accelerate
				akt_speed_soll = max( akt_speed_soll, kmh_to_speed(30) );
				calc_acceleration(delta_t);
				sp_soll += (akt_speed*delta_t);

				// now actually move the units
				while(sp_soll>>12) {
					// Attempt to move one step.
					uint32 sp_hat = fahr[0]->do_drive(1<<YARDS_PER_VEHICLE_STEP_SHIFT);
					if(  sp_hat>0  ) {
						steps_driven++;
					}
					int v_nr = get_vehicle_at_length(steps_driven>>4);
					// stop when depot reached
					if (state==INITIAL) {
						return SYNC_REMOVE;
					}
					if (state==ROUTING_1) {
						break;
					}
					if(  v_nr==anz_vehikel  ) {
						// all are moving
						steps_driven = -1;
						state = DRIVING;
						return SYNC_OK;
					}
					else if(  sp_hat==0  ) {
						// something went wrong. wait for next sync_step()
						return SYNC_OK;
					}
					// now only the right numbers
					for(int i=1; i<=v_nr; i++) {
						fahr[i]->do_drive(sp_hat);
					}
					sp_soll -= sp_hat;
				}
				// smoke for the engines
				next_wolke += delta_t;
				if(next_wolke>500) {
					next_wolke = 0;
					for(int i=0;  i<anz_vehikel;  i++  ) {
						fahr[i]->make_smoke();
					}
				}
			}
			break; // LEAVING_DEPOT

		case DRIVING:
			{
				calc_acceleration(delta_t);

				// now actually move the units
				sp_soll += (akt_speed*delta_t);
				uint32 sp_hat = fahr[0]->do_drive(sp_soll);
				// stop when depot reached ...
				if(state==INITIAL) {
					return SYNC_REMOVE;
				}
				// now move the rest (so all vehikel are moving synchronously)
				for(unsigned i=1; i<anz_vehikel; i++) {
					fahr[i]->do_drive(sp_hat);
				}
				// maybe we have been stopped by something => avoid wide jumps
				sp_soll = (sp_soll-sp_hat) & 0x0FFF;

				// smoke for the engines
				next_wolke += delta_t;
				if(next_wolke>500) {
					next_wolke = 0;
					for(int i=0;  i<anz_vehikel;  i++  ) {
						fahr[i]->make_smoke();
					}
				}
			}
			break; // DRIVING

		case LOADING:
			// loading is an async task, see laden()
			break;

		case WAITING_FOR_CLEARANCE:
		case WAITING_FOR_CLEARANCE_ONE_MONTH:
		case WAITING_FOR_CLEARANCE_TWO_MONTHS:
			// waiting is asynchronous => fixed waiting order and route search
			break;

		case SELF_DESTRUCT:
			// see step, since destruction during a screen update may give strange effects
			break;

		case COUPLED:
		case UNCOUPLING:
			// fork: the primary moves the vehicles, or they are off the map
			return SYNC_REMOVE;

		default:
			dbg->fatal("convoi_t::sync_step()", "Wrong state %d!\n", state);
			break;
	}

	return SYNC_OK;
}


/**
 * Berechne route von Start- zu Zielkoordinate
 */
bool convoi_t::drive_to()
{
	// a new route to the schedule's next stop: no platform detour to let a train by any more
	hold_divert = false;

	if(  anz_vehikel>0  ) {

		// unreserve all tiles that are covered by the train but do not contain one of the wagons,
		// otherwise repositioning of the train drive_to may lead to stray reserved tiles
		if (dynamic_cast<rail_vehicle_t*>(fahr[0])!=NULL  &&  anz_vehikel > 1) {
			// route-index points to next position in route
			// it is completely off when convoi leaves depot
			uint16 index0 = min(fahr[0]->get_route_index()-1, route.get_count());
			for(uint8 i=1; i<anz_vehikel; i++) {
				uint16 index1 = fahr[i]->get_route_index();
				for(uint16 j = index1; j<index0; j++) {
					// unreserve track on tiles between wagons
					grund_t *gr = welt->lookup(route.at(j));
					if (schiene_t *track = (schiene_t *)gr->get_weg( front()->get_waytype() ) ) {
						track->unreserve(self);
					}
				}
				index0 = min(index1-1, route.get_count());
			}
		}

		koord3d start = fahr[0]->get_pos();
		koord3d ziel = schedule->get_current_entry().pos;

		// avoid stopping mid-halt
		if(  start==ziel  ) {
			halthandle_t halt = haltestelle_t::get_halt(ziel,get_owner());
			if(  halt.is_bound()  &&  route.is_contained(start)  ) {
				for(  uint32 i=route.index_of(start);  i<route.get_count();  i++  ) {
					grund_t *gr = welt->lookup(route.at(i));
					if(  gr  && gr->get_halt()==halt  ) {
						ziel = gr->get_pos();
					}
					else {
						break;
					}
				}
			}
		}

		if(  !fahr[0]->calc_route( start, ziel, speed_to_kmh(min_top_speed), &route )  ) {
			if(  state != NO_ROUTE  ) {
				state = NO_ROUTE;
				get_owner()->report_vehicle_problem( self, ziel );
			}
			// wait 25s before next attempt
			wait_lock = 25000;
		}
		else {
			bool route_ok = true;
			const uint8 current_stop = schedule->get_current_stop();
			if(  fahr[0]->get_waytype() != water_wt  ) {
				air_vehicle_t *const plane = dynamic_cast<air_vehicle_t *>(fahr[0]);
				uint32 takeoff = 0, search = 0, landing = 0;
				air_vehicle_t::flight_state plane_state = air_vehicle_t::taxiing;
				if(  plane  ) {
					// due to the complex state system of aircrafts, we have to save index and state
					plane->get_event_index( plane_state, takeoff, search, landing );
				}

				// set next schedule target position if next is a waypoint
				if(  is_waypoint(ziel)  ) {
					schedule_target = ziel;
				}

				// continue route search until the destination is a station
				while(  is_waypoint(ziel)  ) {
					start = ziel;
					schedule->advance();
					ziel = schedule->get_current_entry().pos;

					if(  schedule->get_current_stop() == current_stop  ) {
						// looped around without finding a halt => entire schedule is waypoints.
						break;
					}

					route_t next_segment;
					if(  !fahr[0]->calc_route( start, ziel, speed_to_kmh(min_top_speed), &next_segment )  ) {
						// do we still have a valid route to proceed => then go until there
						if(  route.get_count()>1  ) {
							break;
						}
						// we are stuck on our first routing attempt => give up
						if(  state != NO_ROUTE  ) {
							state = NO_ROUTE;
							get_owner()->report_vehicle_problem( self, ziel );
						}
						// wait 25s before next attempt
						wait_lock = 25000;
						route_ok = false;
						break;
					}
					else {
						bool looped = false;
						if(  fahr[0]->get_waytype() != air_wt  ) {
							 // check if the route circles back on itself (only check the first tile, should be enough)
							looped = route.is_contained(next_segment.at(1));
#if 0
							// this will forbid an eight figure, which might be clever to avoid a problem of reserving one own track
							for(  unsigned i = 1;  i<next_segment.get_count();  i++  ) {
								if(  route.is_contained(next_segment.at(i))  ) {
									looped = true;
									break;
								}
							}
#endif
						}

						if(  looped  ) {
							// proceed upto the waypoint before the loop. Will pause there for a new route search.
							break;
						}
						else {
							uint32 count_offset = route.get_count()-1;
							route.append( &next_segment);
							if(  plane  ) {
								// maybe we need to restore index
								air_vehicle_t::flight_state dummy1;
								uint32 new_takeoff, new_search, new_landing;
								plane->get_event_index( dummy1, new_takeoff, new_search, new_landing );
								if(  takeoff == 0x7FFFFFFF  &&  new_takeoff != 0x7FFFFFFF  ) {
									takeoff = new_takeoff + count_offset;
								}
								if(  landing == 0x7FFFFFFF  &&  new_landing != 0x7FFFFFFF  ) {
									landing = new_landing + count_offset;
								}
								if(  search == 0x7FFFFFFF  &&  new_search != 0x7FFFFFFF ) {
									search = new_search + count_offset;
								}
							}
						}
					}
				}

				if(  plane  ) {
					// due to the complex state system of aircrafts, we have to restore index and state
					plane->set_event_index( plane_state, takeoff, search, landing );
				}
			}

			schedule->set_current_stop(current_stop);
			if(  route_ok  ) {
				vorfahren();
				return true;
			}
		}
	}
	return false;
}


/**
 * Ein Fahrzeug hat ein Problem erkannt und erzwingt die
 * Berechnung einer neuen Route
 */
void convoi_t::suche_neue_route()
{
	state = ROUTING_1;
	wait_lock = 0;
}


/**
 * Asynchrne step methode des Convois
 */
void convoi_t::step()
{
	// fork, coupling: not in the sync list, so wait_lock would never run down
	if(  state==COUPLED  ) {
		// the primary drives us; only follow changes of our line's schedule
		if(  line_update_pending.is_bound()  ) {
			check_pending_updates();
			state = COUPLED;
		}
		return;
	}
	if(  state==UNCOUPLING  ) {
		if(  line_update_pending.is_bound()  ) {
			check_pending_updates();
			state = UNCOUPLING;
		}
		step_uncoupling();
		return;
	}

	if(  wait_lock > 0  ) {
		return;
	}

	// moved check to here, as this will apply the same update
	// logic/constraints convois have for manual schedule manipulation
	if (line_update_pending.is_bound()) {
		check_pending_updates();
	}

	switch(state) {

		case LOADING:
			laden();
			break;

		case DUMMY4:
		case DUMMY5:
			break;

		case EDIT_SCHEDULE:
			// schedule window closed?
			if(schedule!=NULL  &&  schedule->is_editing_finished()) {

				set_schedule(schedule);
				schedule_target = koord3d::invalid;

				if(  schedule->empty()  ) {
					// no entry => no route ...
					state = NO_ROUTE;
					owner->report_vehicle_problem( self, koord3d::invalid );
				}
				else {
					// Schedule changed at station
					// this station? then complete loading task else drive on
					halthandle_t h = haltestelle_t::get_halt( get_pos(), get_owner() );
					if(  h.is_bound()  &&  h==haltestelle_t::get_halt( schedule->get_current_entry().pos, get_owner() )  ) {
						if (route.get_count() > 0) {
							koord3d const& pos = route.back();
							if (h == haltestelle_t::get_halt(pos, get_owner())) {
								state = get_pos() == pos ? LOADING : DRIVING;
								break;
							}
						}
						else {
							if(  drive_to()  ) {
								state = DRIVING;
								break;
							}
						}
					}

					if(  schedule->get_current_entry().pos==get_pos()  ) {
						// position in depot: waiting
						grund_t *gr = welt->lookup(schedule->get_current_entry().pos);
						if(  gr  &&  gr->get_depot()  ) {
							betrete_depot( gr->get_depot() );
						}
						else {
							state = ROUTING_1;
						}
					}
					else {
						// go to next
						state = ROUTING_1;
					}
				}
			}
			break;

		case ROUTING_1:
			{
				vehicle_t* v = fahr[0];

				if(  schedule->empty()  ) {
					state = NO_ROUTE;
					owner->report_vehicle_problem( self, koord3d::invalid );
				}
				else {
					// check first, if we are already there:
					assert( schedule->get_current_stop()<schedule->get_count()  );
					if(  v->get_pos()==schedule->get_current_entry().pos  ) {
						schedule->advance();
					}
					// now calculate a new route
					drive_to();
					// finally, was there a record last time?
					if(max_record_speed>welt->get_record_speed(fahr[0]->get_waytype())) {
						welt->notify_record(self, max_record_speed, record_pos);
					}
				}
			}
			break;

		case NO_ROUTE:
			// stuck vehicles
			if (schedule->empty()) {
				// no entries => no route ...
			}
			else {
				// now calculate a new route
				drive_to();
			}
			break;

		case CAN_START:
		case CAN_START_ONE_MONTH:
		case CAN_START_TWO_MONTHS:
			{
				vehicle_t* v = fahr[0];

				sint32 restart_speed = -1;
				if(  v->can_enter_tile( restart_speed, 0 )  ) {
					// can reserve new block => drive on
					state = (steps_driven>=0) ? LEAVING_DEPOT : DRIVING;
					clear_passing_hold();
					if(haltestelle_t::get_halt(v->get_pos(),owner).is_bound()) {
						play_start_sound();
					}
				}
				else if(  steps_driven==0  ) {
					// on rail depot tile, do not reserve this
					if(  grund_t *gr = welt->lookup(fahr[0]->get_pos())  ) {
						if (schiene_t* const sch0 = obj_cast<schiene_t>(gr->get_weg(fahr[0]->get_waytype()))) {
							sch0->unreserve(fahr[0]);
						}
					}
				}
				if(restart_speed>=0) {
					akt_speed = restart_speed;
				}
				if(state==CAN_START  ||  state==CAN_START_ONE_MONTH) {
					set_tiles_overtaking( 0 );
				}
			}
			break;

		case WAITING_FOR_CLEARANCE_ONE_MONTH:
		case WAITING_FOR_CLEARANCE_TWO_MONTHS:
		case WAITING_FOR_CLEARANCE:
			{
				if(  !route.empty()  &&  fahr[0]->get_route_index() >= route.get_count()  &&  fahr[0]->get_pos()==route.back()  ) {
					// fork, coupling: our route was cut right behind our partner, we are there
					ziel_erreicht();
					break;
				}
				sint32 restart_speed = -1;
				if(  fahr[0]->can_enter_tile( restart_speed, 0 )  ) {
					state = (steps_driven>=0) ? LEAVING_DEPOT : DRIVING;
				}
				if(restart_speed>=0) {
					akt_speed = restart_speed;
				}
				if(state!=DRIVING) {
					set_tiles_overtaking( 0 );
				}
			}
			break;

		// must be here; may otherwise confuse window management
		case SELF_DESTRUCT:
			welt->set_dirty();
			destroy();
			return; // must not continue method after deleting this object

		default: /* keeps compiler silent*/
			break;
	}

	// calculate new waiting time
	switch( state ) {
		// handled by routine
		case LOADING:
			break;

		// immediate action needed
		case SELF_DESTRUCT:
		case LEAVING_DEPOT:
		case ENTERING_DEPOT:
		case DRIVING:
		case DUMMY4:
		case DUMMY5:
			wait_lock = 0;
			break;

		// just waiting for action here
		case INITIAL:
			welt->sync.remove(this);
			/* FALLTHROUGH */
		case EDIT_SCHEDULE:
		case NO_ROUTE:
			wait_lock = max( wait_lock, 25000 );
			break;

		// action soon needed
		case ROUTING_1:
		case CAN_START:
		case WAITING_FOR_CLEARANCE:
			wait_lock = max( wait_lock, 500 );
			break;

		// waiting for free way, not too heavy, not to slow
		case CAN_START_ONE_MONTH:
		case WAITING_FOR_CLEARANCE_ONE_MONTH:
		case CAN_START_TWO_MONTHS:
		case WAITING_FOR_CLEARANCE_TWO_MONTHS:
			wait_lock = 2500;
			break;
		default: ;
	}
}


void convoi_t::new_year()
{
	jahresgewinn = 0;
}


void convoi_t::new_month()
{
	// should not happen: leftover convoi without vehicles ...
	if(anz_vehikel==0) {
		DBG_DEBUG("convoi_t::new_month()","no vehicles => self destruct!");
		self_destruct();
		return;
	}
	// update statistics of average speed
	if(  maxspeed_average_count==0  ) {
		financial_history[0][CONVOI_MAXSPEED] = distance_since_last_stop>0 ? get_speedbonus_kmh() : 0;
	}
	maxspeed_average_count = 0;
	// everything normal: update histroy
	for (int j = 0; j<MAX_CONVOI_COST; j++) {
		for (int k = MAX_MONTHS-1; k>0; k--) {
			financial_history[k][j] = financial_history[k-1][j];
		}
		financial_history[0][j] = 0;
	}
	// remind every new month again
	if(  state==NO_ROUTE  ) {
		get_owner()->report_vehicle_problem( self, get_pos() );
	}
	// check for traffic jam
	if(state==WAITING_FOR_CLEARANCE) {
		state = WAITING_FOR_CLEARANCE_ONE_MONTH;
		// check, if now free ...
		// might also reset the state!
		sint32 restart_speed = -1;
		if(  fahr[0]->can_enter_tile( restart_speed, 0 )  ) {
			state = DRIVING;
		}
		if(restart_speed>=0) {
			akt_speed = restart_speed;
		}
	}
	else if(state==WAITING_FOR_CLEARANCE_ONE_MONTH) {
		// make sure, not another vehicle with same line is loading in front
		bool notify = true;
		// check, if we are not waiting for load
		if(  line.is_bound()  &&  loading_level==0  ) {
			for(  uint i=0;  i < line->count_convoys();  i++  ) {
				convoihandle_t cnv = line->get_convoy(i);
				if(  cnv.is_bound()  &&  cnv->get_state()==LOADING  &&  cnv->get_loading_level() < cnv->get_loading_limit()  ) {
					// convoi on this line is waiting for load => assume we are waiting behind
					notify = false;
					break;
				}
			}
		}
		if(  notify  ) {
			get_owner()->report_vehicle_problem( self, koord3d::invalid );
		}
		state = WAITING_FOR_CLEARANCE_TWO_MONTHS;
	}
	// check for traffic jam
	if(state==CAN_START) {
		state = CAN_START_ONE_MONTH;
	}
	else if(state==CAN_START_ONE_MONTH  ||  state==CAN_START_TWO_MONTHS  ) {
		get_owner()->report_vehicle_problem( self, koord3d::invalid );
		state = CAN_START_TWO_MONTHS;
	}
	// check for obsolete vehicles in the convoi
	if(!has_obsolete  &&  welt->use_timeline()) {
		// convoi has obsolete vehicles?
		const int month_now = welt->get_timeline_year_month();
		has_obsolete = false;
		for(unsigned j=0;  j<get_vehicle_count();  j++ ) {
			if (fahr[j]->get_desc()->is_retired(month_now)) {
				has_obsolete = true;
				break;
			}
		}
	}
	// book fixed cost as running cost
	book( sum_fixed_costs, CONVOI_OPERATIONS );
	book( sum_fixed_costs, CONVOI_PROFIT );
	// since convois can be in the depot, the waytypewithout schedule is determined from the vechile (if there)
	// one can argue that convois in depots should not cost money ...
	waytype_t wtyp = ignore_wt;
	if(  schedule_t* s = get_schedule()  ) {
		wtyp = s->get_waytype();
	}
	else if(  !fahr.empty()  ) {
		wtyp = fahr[0]->get_waytype();
	}
	get_owner()->book_running_costs( sum_fixed_costs, wtyp );
	jahresgewinn += sum_fixed_costs;
}


void convoi_t::betrete_depot(depot_t *dep)
{
	// first remove reservation, if train is still on track
	unreserve_route();

	if(  is_coupled_primary()  ) {
		// fork, coupling: the joined train enters the depot as a train of its own
		convoi_t *c = coupled_convoi.get_rep();
		const sint32 own_fixed = sum_fixed_costs;
		while(  anz_vehikel > coupled_first  ) {
			vehicle_t *v = fahr[anz_vehikel-1];
			grund_t* gr = welt->lookup(v->get_pos());
			if(gr) {
				v->set_last(true);
				v->leave_tile();
				v->set_flag( obj_t::not_on_map );
			}
			remove_vehikel_bei( anz_vehikel-1 );
			v->set_convoi( c );
		}
		sum_fixed_costs = own_fixed;
		coupled_convoi = convoihandle_t();
		c->coupled_convoi = convoihandle_t();
		recalc_catg_index();
		c->set_erstes_letztes();
		dep->convoi_arrived(c->self, c->get_schedule());
		destroy_win( magic_convoi_info+c->self.get_id() );
		c->maxspeed_average_count = 0;
		c->state = INITIAL;
	}

	// remove vehicles from world data structure
	for(unsigned i=0; i<anz_vehikel; i++) {
		vehicle_t* v = fahr[i];

		grund_t* gr = welt->lookup(v->get_pos());
		if(gr) {
			// remove from blockstrecke
			v->set_last(true);
			v->leave_tile();
			v->set_flag( obj_t::not_on_map );
		}
	}

	dep->convoi_arrived(self, get_schedule());

	destroy_win( magic_convoi_info+self.get_id() );

	maxspeed_average_count = 0;
	state = INITIAL;
}


void convoi_t::start()
{
	if(state == INITIAL || state == ROUTING_1) {

		// set home depot to location of depot convoi is leaving
		if(route.empty()) {
			home_depot = fahr[0]->get_pos();
		}
		else {
			home_depot = route.front();
			fahr[0]->set_pos( home_depot );
		}
		// put the convoi on the depot ground, to get automatic rotation
		// (vorfahren() will remove it anyway again.)
		grund_t *gr = welt->lookup( home_depot );
		assert(gr);
		gr->obj_add( fahr[0] );

		// put into sync list
		welt->sync.add(this);

		alte_richtung = ribi_t::none;
		no_load = false;

		state = ROUTING_1;

		// recalc weight and image
		// also for any vehicle entered a depot, set_letztes is true! => reset it correctly
		sint64 restwert_delta = 0;
		for(unsigned i=0; i<anz_vehikel; i++) {
			fahr[i]->set_leading( false );
			fahr[i]->set_last( false );
			restwert_delta -= fahr[i]->calc_sale_value();
			fahr[i]->set_driven();
			restwert_delta += fahr[i]->calc_sale_value();
			fahr[i]->clear_flag( obj_t::not_on_map );
		}
		fahr[0]->set_leading( true );
		fahr[anz_vehikel-1]->set_last( true );
		// do not show the vehicle - it will be wrong positioned -vorfahren() will correct this
		fahr[0]->set_image(IMG_EMPTY);

		// update finances for used vehicle reduction when first driven
		owner->update_assets( restwert_delta, get_schedule()->get_waytype());

		// calc state for convoi
		calc_loading();
		calc_speedbonus_kmh();
		maxspeed_average_count = 0;

		if(line.is_bound()) {
			// might have changed the vehicles in this car ...
			line->recalc_catg_index();
		}
		else {
			welt->set_schedule_counter();
		}
		wait_lock = 0;

		DBG_MESSAGE("convoi_t::start()","Convoi %s wechselt von INITIAL nach ROUTING_1", name_and_id);
	}
	else {
		dbg->warning("convoi_t::start()","called with state=%s\n",state_names[state]);
	}
}


/* called, when at a destination
 * can be waypoint, depot or a stop
 * called from the first vehicle_t of a convoi */
void convoi_t::ziel_erreicht()
{
	const vehicle_t* v = fahr[0];
	alte_richtung = v->get_direction();

	// check, what is at destination!
	const grund_t *gr = welt->lookup(v->get_pos());
	depot_t *dp = gr->get_depot();

	if(  hold_divert  &&  !dp  ) {
		// fork: at the platform we took to let a passing train go by; wait there (see
		// rail_vehicle_t::is_held_for_passing_train), then go on to the next stop of the schedule
		hold_divert = false;
		clear_passing_hold();
		akt_speed = 0;
		state = ROUTING_1;
		wait_lock = 0;
		return;
	}

	if(dp) {
		// ok, we are entering a depot
		cbuffer_t buf;

		// we still book the money for the trip; however, the freight will be deleted (by the vehicle in the depot itself)
		calc_gewinn();

		akt_speed = 0;
		buf.printf( translator::translate("%s has entered a depot."), get_name() );
		welt->get_message()->add_message(buf, v->get_pos().get_2d(),message_t::warnings, PLAYER_FLAG|get_owner()->get_player_nr(), IMG_EMPTY);

		betrete_depot(dp);
	}
	else {
		// no depot reached, check for stop!
		halthandle_t halt = haltestelle_t::get_halt(schedule->get_current_entry().pos,owner);
		if(  halt.is_bound() &&  gr->get_weg_ribi(v->get_waytype())!=0  ) {
			// seems to be a stop, so book the money for the trip
			akt_speed = 0;
			halt->book(1, HALT_CONVOIS_ARRIVED);
			state = LOADING;
			arrived_time = welt->get_ticks();
			couple_wait_since = 0;
			couple_hold_slot = -1;
			if(  is_coupled_primary()  ) {
				// fork, coupling: the joined train stops here as well, or parts here
				convoi_t *c = coupled_convoi.get_rep();
				c->arrived_time = arrived_time;
				if(  !c->follow_to_stop( halt )  ) {
					uncouple_here();
				}
			}
			else if(  line.is_bound()  ) {
				// fork, coupling: our primary left without us here, so we run late with its slot
				sint64 slot;
				if(  line->take_missed_coupling( schedule->get_current_stop(), slot )  ) {
					running_late = true;
					late_slot = slot;
				}
			}
		}
		else {
			// Neither depot nor station: waypoint
			schedule->advance();
			state = ROUTING_1;
		}
	}
	wait_lock = 0;
}


/**
 * Wartet bis Fahrzeug 0 freie Fahrt meldet
 */
void convoi_t::warten_bis_weg_frei(sint32 restart_speed)
{
	if(!is_waiting()) {
		state = WAITING_FOR_CLEARANCE;
		wait_lock = 0;
	}
	if(restart_speed>=0) {
		// langsam anfahren
		akt_speed = restart_speed;
	}
}


bool convoi_t::add_vehikel(vehicle_t *v, bool infront)
{
DBG_MESSAGE("convoi_t::add_vehikel()","at pos %i of %i total vehikels.",anz_vehikel,fahr.get_count());
	// extend array if requested
	if(anz_vehikel == fahr.get_count()) {
		fahr.resize(anz_vehikel+1,NULL);
DBG_MESSAGE("convoi_t::add_vehikel()","extend array_tpl to %i totals.",fahr.get_count());
	}
	// now append
	if (anz_vehikel < fahr.get_count()) {
		v->set_convoi(this);

		if(infront) {
			for(unsigned i = anz_vehikel; i > 0; i--) {
				fahr[i] = fahr[i - 1];
			}
			fahr[0] = v;
		}
		else {
			fahr[anz_vehikel] = v;
		}
		anz_vehikel ++;

		const vehicle_desc_t *info = v->get_desc();
		sum_power += info->get_power();
		sum_weight += info->get_weight();
		sum_fixed_costs -= welt->scale_with_month_length( info->get_fixed_cost() );
		sum_gesamtweight = sum_weight;
		calc_loading();
		// power, running costs, top speed and electrification depend on which engines pull
		recalc_traction( true );
		freight_info_resort = true;
		// Add good_catg_index:
		if(v->get_cargo_max() != 0) {
			const goods_desc_t *ware=v->get_cargo_type();
			if(ware!=goods_manager_t::none  ) {
				goods_catg_index.append_unique( ware->get_catg_index() );
			}
		}
		// check for obsolete
		if(!has_obsolete  &&  welt->use_timeline()) {
			has_obsolete = info->is_retired( welt->get_timeline_year_month() );
		}
	}
	else {
		return false;
	}

	// der convoi hat jetzt ein neues ende
	set_erstes_letztes();

DBG_MESSAGE("convoi_t::add_vehikel()","now %i of %i total vehikels.",anz_vehikel,fahr.get_count());
	return true;
}


vehicle_t *convoi_t::remove_vehikel_bei(uint16 i)
{
	vehicle_t *v = NULL;
	if(i<anz_vehikel) {
		v = fahr[i];
		if(v != NULL) {
			for(unsigned j=i; j<anz_vehikel-1u; j++) {
				fahr[j] = fahr[j + 1];
			}

			v->set_convoi(NULL);

			--anz_vehikel;
			fahr[anz_vehikel] = NULL;

			const vehicle_desc_t *info = v->get_desc();
			sum_power -= info->get_power();
			sum_weight -= info->get_weight();
			sum_fixed_costs += welt->scale_with_month_length( info->get_fixed_cost() );
			v->set_idle( false );
		}
		sum_gesamtweight = sum_weight;
		calc_loading();
		freight_info_resort = true;

		// der convoi hat jetzt ein neues ende
		if(anz_vehikel > 0) {
			set_erstes_letztes();
		}

		// power, running costs, top speed and electrification depend on which engines pull
		recalc_traction( true );

		// check for obsolete
		if(has_obsolete) {
			has_obsolete = false;
			const int month_now = welt->get_timeline_year_month();
			for(unsigned i=0; i<anz_vehikel; i++) {
				has_obsolete |= fahr[i]->get_desc()->is_retired(month_now);
			}
		}

		recalc_catg_index();
	}
	return v;
}


// recalc what good this convoy is moving
void convoi_t::recalc_catg_index()
{
	goods_catg_index.clear();

	// fork, coupling: a primary counts only its own vehicles
	for(  uint8 i = 0;  i < get_own_vehicle_count();  i++  ) {
		// Only consider vehicles that really transport something
		// this helps against routing errors through passenger
		// trains pulling only freight wagons
		if(get_vehikel(i)->get_cargo_max() == 0) {
			continue;
		}
		const goods_desc_t *ware=get_vehikel(i)->get_cargo_type();
		if(ware!=goods_manager_t::none  ) {
			goods_catg_index.append_unique( ware->get_catg_index() );
		}
	}
	/* since during composition of convois all kinds of composition could happen,
	 * we do not enforce schedule recalculation here; it will be done anyway all times when leaving the INTI state ...
	 */
}


void convoi_t::set_erstes_letztes()
{
	// anz_vehikel muss korrekt init sein
	if(anz_vehikel>0) {
		fahr[0]->set_leading(true);
		for(unsigned i=1; i<anz_vehikel; i++) {
			fahr[i]->set_leading(false);
			fahr[i - 1]->set_last(false);
		}
		fahr[anz_vehikel - 1]->set_last(true);
	}
	else {
		dbg->warning("convoi_t::set_erstes_letzes()", "called with anz_vehikel==0!");
	}
}


// remove wrong freight when schedule changes etc.
void convoi_t::check_freight()
{
	// fork, coupling: only our own vehicles, against our own schedule
	for(unsigned i=0; i<get_own_vehicle_count(); i++) {
		fahr[i]->remove_stale_cargo( schedule );
	}
	calc_loading();
	freight_info_resort = true;
}


bool convoi_t::set_schedule(schedule_t * f)
{
	if(  state==SELF_DESTRUCT  ) {
		return false;
	}

	DBG_DEBUG("convoi_t::set_schedule()", "new=%p, old=%p", f, schedule);
	assert(f != NULL);

	// happens to be identical?
	if(schedule!=f) {
		// now check, we we have been bond to a line we are about to lose:
		bool changed = false;
		if(  line.is_bound()  ) {
			if(  !f->matches( welt, line->get_schedule() )  ) {
				// change from line to individual schedule
				// -> unset line now and register stops from new schedule later
				changed = true;
				unset_line();
			}
		}
		else {
			if(  !f->matches( welt, schedule )  ) {
				// merely change schedule and do not involve line
				// -> unregister stops from old schedule now and register stops from new schedule later
				changed = true;
				unregister_stops();
			}
		}
		// destroy a possibly open schedule window
		if(  schedule  &&  !schedule->is_editing_finished()  ) {
			destroy_win((ptrdiff_t)schedule);
			delete schedule;
		}
		schedule = f;
		if(  changed  ) {
			// if line is unset or schedule is changed
			// -> register stops from new schedule
			register_stops();
			welt->set_schedule_counter(); // must trigger refresh
		}
	}

	// remove wrong freight
	check_freight();

	// ok, now we have a schedule
	if(state != INITIAL  &&  state != COUPLED  &&  state != UNCOUPLING) {
		state = EDIT_SCHEDULE;
	}
	// to avoid jumping trains
	alte_richtung = fahr[0]->get_direction();
	wait_lock = 0;
	return true;
}


schedule_t *convoi_t::create_schedule()
{
	if(schedule == NULL) {
		const vehicle_t* v = fahr[0];

		if (v != NULL) {
			schedule = v->generate_new_schedule();
			schedule->finish_editing();
		}
	}

	return schedule;
}


/* checks, if we go in the same direction;
 * true: convoy prepared
 * false: must recalculate position
 * on all error we better use the normal starting procedure ...
 */
bool convoi_t::can_go_alte_richtung()
{
	// invalid route? nothing to test, must start new
	if(route.empty()) {
		return false;
	}

	// going backwards? then recalculate all
	ribi_t::ribi neue_richtung_rwr = ribi_t::backward(fahr[0]->calc_direction(route.front(), route.at(min(2, route.get_count() - 1))));
//	DBG_MESSAGE("convoi_t::go_alte_richtung()","neu=%i,rwr_neu=%i,alt=%i",neue_richtung_rwr,ribi_t::backward(neue_richtung_rwr),alte_richtung);
	if(neue_richtung_rwr&alte_richtung) {
		akt_speed = 8;
		return false;
	}

	// now get the actual length and the tile length
	uint16 convoi_length = 15;
	uint16 tile_length = 24;
	unsigned i; // for visual C++
	const vehicle_t* pred = NULL;
	for(i=0; i<anz_vehikel; i++) {
		const vehicle_t* v = fahr[i];
		grund_t *gr = welt->lookup(v->get_pos());

		// not last vehicle?
		// the length of last vehicle does not matter when it comes to positioning of vehicles
		if ( i+1 < anz_vehikel) {
			convoi_length += v->get_desc()->get_length();
		}

		if(gr==NULL  ||  (pred!=NULL  &&  (abs(v->get_pos().x-pred->get_pos().x)>=2  ||  abs(v->get_pos().y-pred->get_pos().y)>=2))  ) {
			// ending here is an error!
			// this is an already broken train => restart
			dbg->warning("convoi_t::go_alte_richtung()","broken convoy (id %i) found => fixing!",self.get_id());
			akt_speed = 8;
			return false;
		}

		// now check, if ribi is straight and train is not
		ribi_t::ribi weg_ribi = gr->get_weg_ribi_unmasked(v->get_waytype());
		if(ribi_t::is_straight(weg_ribi)  &&  (weg_ribi|v->get_direction())!=weg_ribi) {
			dbg->warning("convoi_t::go_alte_richtung()","convoy with wrong vehicle directions (id %i) found => fixing!",self.get_id());
			akt_speed = 8;
			return false;
		}

		if(  pred  &&  pred->get_pos()!=v->get_pos()  ) {
			tile_length += (ribi_t::is_straight(welt->lookup(pred->get_pos())->get_weg_ribi_unmasked(pred->get_waytype())) ? 16 : 8192/vehicle_t::get_diagonal_multiplier())*koord_distance(pred->get_pos(),v->get_pos());
		}

		pred = v;
	}
	// check if convoi is way too short (even for diagonal tracks)
	tile_length += (ribi_t::is_straight(welt->lookup(fahr[anz_vehikel-1]->get_pos())->get_weg_ribi_unmasked(fahr[anz_vehikel-1]->get_waytype())) ? 16 : 8192/vehicle_t::get_diagonal_multiplier());
	if(  convoi_length>tile_length  ) {
		dbg->warning("convoi_t::go_alte_richtung()","convoy too short (id %i) => fixing!",self.get_id());
		akt_speed = 8;
		return false;
	}

	uint16 length = min((convoi_length/16u)+4u,route.get_count()); // maximum length in tiles to check

	// we just check, whether we go back (i.e. route tiles other than zero have convoi vehicles on them)
	for( int index=1;  index<length;  index++ ) {
		grund_t *gr=welt->lookup(route.at(index));
		// now check, if we are already here ...
		for(unsigned i=0; i<anz_vehikel; i++) {
			if (gr->obj_ist_da(fahr[i])) {
				// we are turning around => start slowly and rebuilt train
				akt_speed = 8;
				return false;
			}
		}
	}

//DBG_MESSAGE("convoi_t::go_alte_richtung()","alte=%d, neu_rwr=%d",alte_richtung,neue_richtung_rwr);

	// we continue our journey; however later cars need also a correct route entry
	// eventually we need to add their positions to the convois route
	koord3d pos = fahr[0]->get_pos();
	assert(pos == route.front());
	if(welt->lookup(pos)->get_depot()) {
		return false;
	}
	else {
		for(i=0; i<anz_vehikel; i++) {
			vehicle_t* v = fahr[i];
			// eventually add current position to the route
			if (route.front() != v->get_pos() && route.at(1) != v->get_pos()) {
				route.insert(v->get_pos());
			}
		}
	}

	// since we need the route for every vehicle of this convoi,
	// we must set the current route index (instead assuming 1)
	length = min((convoi_length/8u),route.get_count()-1); // maximum length in tiles to check
	bool ok=false;
	for(i=0; i<anz_vehikel; i++) {
		vehicle_t* v = fahr[i];

		// this is such awkward, since it takes into account different vehicle length
		const koord3d vehicle_start_pos = v->get_pos();
		for( int idx=0;  idx<=length;  idx++  ) {
			if(route.at(idx)==vehicle_start_pos) {
				// set route index, no recalculations necessary
				v->initialise_journey(idx, false );
				ok = true;

				// check direction
				uint8 richtung = v->get_direction();
				uint8 neu_richtung = v->calc_direction( route.at(max(idx-1,0)), v->get_pos_next());
				// we need to move to this place ...
				if(neu_richtung!=richtung  &&  (i!=0  ||  anz_vehikel==1  ||  ribi_t::is_bend(neu_richtung)) ) {
					// 90 deg bend!
					return false;
				}

				break;
			}
		}
		// too short?!? (rather broken then!)
		if(!ok) {
			return false;
		}
	}

	return true;
}


// put the convoi on its way
bool convoi_t::lay_out_on_route()
{
	bool at_dest = false;
	// start route from the beginning at index 0, place everything on start
	uint32 train_length = move_to(0);

	// move one train length to the start position ...
	// in north/west direction, we leave the vehicle away to start as much back as possible
	ribi_t::ribi neue_richtung = fahr[0]->get_direction();
	if(neue_richtung==ribi_t::south  ||  neue_richtung==ribi_t::east) {
		// drive the convoi to the same position, but do not hop into next tile!
		if(  train_length%16==0  ) {
			// any space we need => just add
			train_length += fahr[anz_vehikel-1]->get_desc()->get_length();
		}
		else {
			// limit train to front of tile
			train_length += min( (train_length%CARUNITS_PER_TILE)-1, fahr[anz_vehikel-1]->get_desc()->get_length() );
		}
	}
	else {
		train_length += 1;
	}
	train_length = max(1,train_length);

	// now advance all convoi until it is completely on the track
	fahr[0]->set_leading(false); // switches off signal checks ...
	uint32 dist = VEHICLE_STEPS_PER_CARUNIT*train_length<<YARDS_PER_VEHICLE_STEP_SHIFT;
	for(unsigned i=0; i<anz_vehikel; i++) {
		vehicle_t* v = fahr[i];

		v->get_smoke(false);
		uint32 const driven = fahr[i]->do_drive( dist );
		if (i==0  &&  driven < dist) {
			// we are already at our destination
			at_dest = true;
		}
		// this gives the length in carunits, 1/CARUNITS_PER_TILE of a full tile => all cars closely coupled!
		v->get_smoke(true);

		uint32 const vlen = ((VEHICLE_STEPS_PER_CARUNIT*v->get_desc()->get_length())<<YARDS_PER_VEHICLE_STEP_SHIFT);
		if (vlen > dist) {
			break;
		}
		dist = driven - vlen;
	}
	fahr[0]->set_leading(true);
	return at_dest;
}


void convoi_t::vorfahren()
{
	// init speed settings
	sp_soll = 0;
	set_tiles_overtaking( 0 );
	recalc_data_front = true;
	recalc_data = true;

	koord3d k0 = route.front();
	grund_t *gr = welt->lookup(k0);
	bool at_dest = false;
	if(gr  &&  gr->get_depot()) {
		// start in depot
		for(unsigned i=0; i<anz_vehikel; i++) {
			vehicle_t* v = fahr[i];

			// remove from old position
			grund_t* gr = welt->lookup(v->get_pos());
			if(gr) {
				gr->obj_remove(v);
				if(gr->ist_uebergang()) {
					crossing_t *cr = gr->find<crossing_t>(2);
					cr->release_crossing(v);
				}
				// eventually unreserve this
				if(  schiene_t* const sch0 = obj_cast<schiene_t>(gr->get_weg(fahr[i]->get_waytype()))  ) {
					sch0->unreserve(v);
				}
			}
			v->initialise_journey(0, true);
			// set at new position
			gr = welt->lookup(v->get_pos());
			assert(gr);
			v->enter_tile(gr);
		}

		// just advances the first vehicle
		vehicle_t* v0 = fahr[0];
		v0->set_leading(false); // switches off signal checks ...
		v0->get_smoke(false);
		steps_driven = 0;
		// drive half a tile:
		for(int i=0; i<anz_vehikel; i++) {
			fahr[i]->do_drive( (VEHICLE_STEPS_PER_TILE/2)<<YARDS_PER_VEHICLE_STEP_SHIFT );
		}
		v0->get_smoke(true);
		v0->set_leading(true); // switches on signal checks to reserve the next route

		// the vehicles were placed without hopping: check the catenary under the electric engines
		recalc_traction( false );

		// until all other are on the track
		state = CAN_START;
	}
	else {
		// still leaving depot (steps_driven!=0) or going in other direction or misalignment?
		if(  steps_driven>0  ||  !can_go_alte_richtung()  ) {

			at_dest = lay_out_on_route();
		}
		// the vehicles may have been placed without hopping: check the catenary under the electric engines
		recalc_traction( false );
		if (!at_dest) {
			state = CAN_START;

			// to advance more smoothly
			sint32 restart_speed = -1;
			if(  fahr[0]->can_enter_tile( restart_speed, 0 )  ) {
				// can reserve new block => drive on
				if(haltestelle_t::get_halt(k0,owner).is_bound()) {
					play_start_sound();
				}
				state = DRIVING;
			}
		}
		else {
			ziel_erreicht();
		}
	}

	// finally reserve route (if needed)
	if(  fahr[0]->get_waytype()!=air_wt  &&  !at_dest  ) {
		// do not pre-reserve for airplanes
		for(unsigned i=0; i<anz_vehikel; i++) {
			// eventually reserve this
			vehicle_t const& v = *fahr[i];
			if (schiene_t* const sch0 = obj_cast<schiene_t>(welt->lookup(v.get_pos())->get_weg(v.get_waytype()))) {
				sch0->reserve(self,ribi_t::none);
			}
			else {
				break;
			}
		}
	}

	wait_lock = 0;
	INT_CHECK("simconvoi 711");
}


void convoi_t::rdwr_convoihandle_t(loadsave_t *file, convoihandle_t &cnv)
{
	if(  file->is_version_atleast(112, 3)  ) {
		uint16 id = (file->is_saving()  &&  cnv.is_bound()) ? cnv.get_id() : 0;
		file->rdwr_short( id );
		if (file->is_loading()) {
			cnv.set_id( id );
		}
	}
}


void convoi_t::rdwr(loadsave_t *file)
{
	xml_tag_t t( file, "convoi_t" );

	sint32 dummy;
	sint32 owner_n = welt->sp2num(owner);

	if(file->is_saving()) {
		if(  file->is_version_less(101, 0)  ) {
			file->wr_obj_id("Convoi");
			// the matching read is in karte_t::laden(loadsave*)...
		}
	}

	// do the update, otherwise we might lose the line after save & reload
	if(file->is_saving()  &&  line_update_pending.is_bound()) {
		check_pending_updates();
	}

	simline_t::rdwr_linehandle_t(file, line);

	// we want persistent convoihandles so we can keep dialogues open in network games
	if(  file->is_loading()  ) {
		if(  file->is_version_less(112, 3)  ) {
			self = convoihandle_t( this );
		}
		else {
			uint16 id;
			file->rdwr_short( id );
			self = convoihandle_t( this, id );
		}
	}
	else if(  file->is_version_atleast(112, 3)  ) {
		uint16 id = self.get_id();
		file->rdwr_short( id );
	}

	// fork, coupling: a primary saves only its own vehicles, the joined train saves the rest
	const uint8 saved_vehicles = file->is_saving() ? get_own_vehicle_count() : 0;
	dummy = file->is_saving() ? saved_vehicles : anz_vehikel;
	file->rdwr_long(dummy);
	anz_vehikel = file->is_saving() ? anz_vehikel : (uint8)dummy;

	if(file->is_version_less(99, 14)) {
		// was anz_ready
		file->rdwr_long(dummy);
	}

	file->rdwr_long(wait_lock);
	// some versions may produce broken safegames apparently
	if(wait_lock > 60000) {
		dbg->warning("convoi_t::sync_prepre()","Convoi %d: wait lock out of bounds: wait_lock = %d, setting to 60000",self.get_id(), wait_lock);
		wait_lock = 60000;
	}

	bool dummy_bool=false;
	file->rdwr_bool(dummy_bool);
	file->rdwr_long(owner_n);
	file->rdwr_long(akt_speed);
	file->rdwr_long(akt_speed_soll);
	file->rdwr_long(sp_soll);
	if(  file->is_saving()  &&  file->is_version_less(122, 6)  &&  (state==COUPLED  ||  state==UNCOUPLING)  ) {
		// fork, coupling: older games know no coupled trains; a joined train becomes a train of its
		// own standing behind the primary, which looks for a new route at once
		states old_state = ROUTING_1;
		file->rdwr_enum(old_state);
	}
	else {
		file->rdwr_enum(state);
	}
	file->rdwr_enum(alte_richtung);

	// read the yearly income (which has since then become a 64 bit value)
	// will be recalculated later directly from the history
	if(file->is_version_less(89, 4)) {
		file->rdwr_long(dummy);
	}

	route.rdwr(file);

	if(file->is_loading()) {
		// extend array if requested (only needed for trains)
		if(anz_vehikel > fahr.get_count()) {
			fahr.resize(anz_vehikel, NULL);
		}
		owner = welt->get_player( owner_n );

		// sanity check for values ... plus correction
		if(sp_soll < 0) {
			sp_soll = 0;
		}
	}

	file->rdwr_str(name_and_id + name_offset, lengthof(name_and_id) - name_offset);
	if(file->is_loading()) {
		set_name(name_and_id+name_offset); // will add id automatically
	}

	koord3d dummy_pos;
	if(file->is_saving()) {
		for(unsigned i=0; i<saved_vehicles; i++) {
			file->wr_obj_id( fahr[i]->get_typ() );
			fahr[i]->rdwr_from_convoi(file);
		}
	}
	else {
		bool override_monorail = false;
		is_electric = false;
		for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
			obj_t::typ typ = (obj_t::typ)file->rd_obj_id();
			vehicle_t *v = 0;

			const bool first = (i==0);
			const bool last = (i==anz_vehikel-1u);
			if(override_monorail) {
				// ignore type for ancient monorails
				v = new monorail_vehicle_t(file, first, last);
			}
			else {
				switch(typ) {
					case obj_t::old_automobil:
					case obj_t::road_vehicle: v = new road_vehicle_t(file, first, last);  break;
					case obj_t::old_waggon:
					case obj_t::rail_vehicle:    v = new rail_vehicle_t(file, first, last);     break;
					case obj_t::old_schiff:
					case obj_t::water_vehicle:    v = new water_vehicle_t(file, first, last);     break;
					case obj_t::old_aircraft:
					case obj_t::air_vehicle:    v = new air_vehicle_t(file, first, last);     break;
					case obj_t::old_monorailwaggon:
					case obj_t::monorail_vehicle:    v = new monorail_vehicle_t(file, first, last);     break;
					case obj_t::maglev_vehicle:         v = new maglev_vehicle_t(file, first, last);     break;
					case obj_t::narrowgauge_vehicle:    v = new narrowgauge_vehicle_t(file, first, last);     break;
					default:
						dbg->fatal("convoi_t::convoi_t()","Can't load vehicle type %d", typ);
				}
			}

			// no matching vehicle found?
			if(v->get_desc()==NULL) {
				// will create orphan object, but better than crashing at deletion ...
				dbg->error("convoi_t::convoi_t()","Can't load vehicle and no replacement found!");
				i --;
				anz_vehikel --;
				continue;
			}

			// in very old games, monorail was a railway
			// so we need to convert this
			// freight will be lost, but game will be loadable
			if(i==0  &&  v->get_desc()->get_waytype()==monorail_wt  &&  v->get_typ()==obj_t::rail_vehicle) {
				override_monorail = true;
				vehicle_t *v_neu = new monorail_vehicle_t( v->get_pos(), v->get_desc(), v->get_owner(), NULL );
				v->discard_cargo();
				delete v;
				v = v_neu;
			}

			if(file->is_version_less(99, 4)) {
				dummy_pos.rdwr(file);
			}

			const vehicle_desc_t *info = v->get_desc();
			assert(info);

			// if we load a game from a file which was saved from a
			// game with a different vehicle.tab, there might be no vehicle
			// info
			if(info) {
				sum_power += info->get_power();
				sum_weight += info->get_weight();
				sum_fixed_costs -= welt->scale_with_month_length( info->get_fixed_cost() );
				has_obsolete |= welt->use_timeline()  &&  info->is_retired( welt->get_timeline_year_month() );
				// we do not add maintenance here, the fixed costs are booked as running costs
			}

			// some versions save vehicles after leaving depot with koord3d::invalid
			if(v->get_pos()==koord3d::invalid) {
				state = INITIAL;
			}

			// fork, coupling: a train waiting to appear after uncoupling is not on the map
			if(state!=INITIAL  &&  state!=UNCOUPLING) {
				grund_t *gr;
				gr = welt->lookup(v->get_pos());
				if(!gr) {
					gr = welt->lookup_kartenboden(v->get_pos().get_2d());
					if(gr) {
						dbg->error("convoi_t::rdwr()", "invalid position %s for vehicle %s in state %d (setting to %i,%i,%i)", v->get_pos().get_str(), v->get_name(), state, gr->get_pos().x, gr->get_pos().y, gr->get_pos().z );
						v->set_pos( gr->get_pos() );
					}
					else {
						dbg->fatal("convoi_t::rdwr()", "invalid position %s for vehicle %s in state %d", v->get_pos().get_str(), v->get_name(), state);
					}
					state = INITIAL;
				}
				// add to blockstrecke
				if(v->get_waytype()==track_wt  ||  v->get_waytype()==monorail_wt  ||  v->get_waytype()==maglev_wt  ||  v->get_waytype()==narrowgauge_wt) {
					schiene_t* sch = (schiene_t*)gr->get_weg(v->get_waytype());
					if(sch) {
						sch->reserve(self,ribi_t::none);
					}
					// add to crossing
					if(gr->ist_uebergang()) {
						gr->find<crossing_t>()->add_to_crossing(v);
					}
				}
				if(  gr->get_top()>253  ) {
					dbg->warning( "convoi_t::rdwr()", "cannot put vehicle on ground at (%s)", gr->get_pos().get_str() );
				}
				gr->obj_add(v);
				v->clear_flag(obj_t::not_on_map);
			}
			else {
				v->set_flag(obj_t::not_on_map);
			}

			// add to convoi
			fahr[i] = v;
		}
		sum_gesamtweight = sum_weight;
	}

	bool has_schedule = (schedule != NULL);
	file->rdwr_bool(has_schedule);
	if(has_schedule) {
		//DBG_MESSAGE("convoi_t::rdwr()","convoi has a schedule, state %s!",state_names[state]);
		const vehicle_t* v = fahr[0];
		if(file->is_loading() && v) {
			schedule = v->generate_new_schedule();
		}
		// hack to load corrupted games -> there is a schedule
		// but no vehicle so we can't determine the exact type of
		// schedule needed. This hack is safe because convois
		// without vehicles get deleted right after loading.
		// Since generic schedules are not allowed, we use a train_schedule_t
		if(schedule == 0) {
			schedule = new train_schedule_t();
		}

		// now read the schedule, we have one for sure here
		schedule->rdwr( file );
	}

	if(file->is_loading()) {
		next_wolke = 0;
		calc_loading();
	}

	if(  file->is_loading()  ) {
		// power, running costs, top speed and electrification (the tiles are checked again in finish_rd)
		recalc_traction( false );
	}

	// since sp_ist became obsolete, sp_soll is used modulo 65536
	sp_soll &= 65535;

	if(file->is_version_less(88, 4)) {
		// load statistics
		int j;
		for (j = 0; j<3; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (j = 2; j<5; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][CONVOI_DISTANCE] = 0;
			financial_history[k][CONVOI_MAXSPEED] = 0;
			financial_history[k][CONVOI_WAYTOLL] = 0;
		}
	}
	else if(  file->is_version_less(102, 3)  ){
		// load statistics
		for (int j = 0; j<5; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][CONVOI_DISTANCE] = 0;
			financial_history[k][CONVOI_MAXSPEED] = 0;
			financial_history[k][CONVOI_WAYTOLL] = 0;
		}
	}
	else if(  file->is_version_less(111, 1)  ){
		// load statistics
		for (int j = 0; j<6; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][CONVOI_MAXSPEED] = 0;
			financial_history[k][CONVOI_WAYTOLL] = 0;
		}
	}
	else if(  file->is_version_less(112, 8)  ){
		// load statistics
		for (int j = 0; j<7; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][CONVOI_WAYTOLL] = 0;
		}
	}
	else
	{
		// load statistics
		for (int j = 0; j<MAX_CONVOI_COST; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
	}

	// the convoi odometer
	if(  file->is_version_atleast(102, 3)  ){
		file->rdwr_longlong( total_distance_traveled);
	}

	// since it was saved as an signed int
	// we recalc it anyhow
	if(file->is_loading()) {
		jahresgewinn = 0;
		for(int i=welt->get_last_month()%12;  i>=0;  i--  ) {
			jahresgewinn += financial_history[i][CONVOI_PROFIT];
		}
	}

	// save/restore pending line updates
	if(file->is_version_atleast(84, 9)  &&  file->is_version_less(99, 13)) {
		file->rdwr_long(dummy); // ignore
	}
	if(file->is_loading()) {
		line_update_pending = linehandle_t();
	}

	if(file->is_version_atleast(84, 10)) {
		home_depot.rdwr(file);
	}

	// Old versions recorded last_stop_pos in convoi, not in vehicle
	koord3d last_stop_pos_convoi = koord3d(0,0,0);
	if (anz_vehikel !=0) {
		last_stop_pos_convoi = fahr[0]->last_stop_pos;
	}
	if(file->is_version_atleast(87, 1)) {
		last_stop_pos_convoi.rdwr(file);
	}
	else {
		last_stop_pos_convoi =
			!route.empty()   ? route.front()      :
			anz_vehikel != 0 ? fahr[0]->get_pos() :
			koord3d(0, 0, 0);
	}

	// for leaving the depot routine
	if(file->is_version_less(99, 14)) {
		steps_driven = -1;
	}
	else {
		file->rdwr_short(steps_driven);
	}

	// waiting time left ...
	if(file->is_version_atleast(99, 17)) {
		if(file->is_saving()) {
			if(  has_schedule  &&  schedule->get_current_entry().has_waiting_time()  ) {
				uint32 diff_ticks = arrived_time + schedule->get_current_entry().get_waiting_ticks() - welt->get_ticks();
				file->rdwr_long(diff_ticks);
			}
			else {
				uint32 diff_ticks = 0xFFFFFFFFu; // write old WAIT_INFINITE value for backwards compatibility
				file->rdwr_long(diff_ticks);
			}
		}
		else {
			uint32 diff_ticks = 0;
			file->rdwr_long(diff_ticks);
			arrived_time = has_schedule ? welt->get_ticks() - schedule->get_current_entry().get_waiting_ticks() + diff_ticks : 0;
		}
	}

	// since 99015, the last stop will be maintained by the vehikels themselves
	if(file->is_version_less(99, 15)) {
		for(unsigned i=0; i<anz_vehikel; i++) {
			fahr[i]->last_stop_pos = last_stop_pos_convoi;
		}
	}

	// overtaking status
	if(file->is_version_less(100, 1)) {
		set_tiles_overtaking( 0 );
	}
	else {
		file->rdwr_byte(tiles_overtaking);
		set_tiles_overtaking( tiles_overtaking );
	}
	// no_load, withdraw
	if(file->is_version_less(102, 1)) {
		no_load = false;
		withdraw = false;
	}
	else {
		file->rdwr_bool(no_load);
		file->rdwr_bool(withdraw);
	}

	if(file->is_version_atleast(111, 1)) {
		file->rdwr_long( distance_since_last_stop );
		file->rdwr_long( sum_speed_limit );
	}

	if(  file->is_version_atleast(111, 2)  ) {
		file->rdwr_long( maxspeed_average_count );
	}

	if(  file->is_version_atleast(111, 3)  ) {
		file->rdwr_short( next_stop_index );
		file->rdwr_short( next_reservation_index );
	}

	if(  file->is_version_atleast(122, 5)  ) {
		// fork: Hold marker, on the way to a platform to let a passing train go by
		file->rdwr_bool( hold_marker );
		file->rdwr_bool( hold_divert );
	}

	if(  file->is_version_atleast(122, 6)  ) {
		// fork: coupling
		rdwr_convoihandle_t( file, coupled_convoi );
		file->rdwr_byte( coupled_first );
		rdwr_convoihandle_t( file, handover_to );
		uint16 span_count = uncouple_span.get_count();
		file->rdwr_short( span_count );
		if(  file->is_loading()  ) {
			uncouple_span.clear();
		}
		for(  uint16 i=0;  i<span_count;  i++  ) {
			koord3d pos = file->is_saving() ? uncouple_span[i] : koord3d::invalid;
			pos.rdwr( file );
			if(  file->is_loading()  ) {
				uncouple_span.append( pos );
			}
		}
		// waiting for the partner: saved as ticks waited so far
		uint32 waited = couple_wait_since ? welt->get_ticks() - couple_wait_since : 0;
		file->rdwr_long( waited );
		if(  file->is_loading()  ) {
			couple_wait_since = waited ? max( 1u, welt->get_ticks() - waited ) : 0;
		}
		file->rdwr_longlong( couple_hold_slot );
		file->rdwr_bool( running_late );
		file->rdwr_longlong( late_slot );
	}
	else if(  file->is_loading()  ) {
		coupled_convoi = convoihandle_t();
		handover_to = convoihandle_t();
	}

	if(  file->is_loading()  ) {
		reserve_route();
		recalc_catg_index();
	}
}


bool convoi_t::is_hold_marked() const
{
	return hold_marker  ||  (line.is_bound()  &&  line->get_hold_marker());
}


void convoi_t::open_info_window()
{
	if(  in_depot()  ) {
		// if ownership matches, we can try to open the depot dialog
		if(  get_owner()==welt->get_active_player()  ) {
			grund_t *const ground = welt->lookup( get_home_depot() );
			if(  ground  ) {
				depot_t *const depot = ground->get_depot();
				if(  depot  ) {
					depot->show_info();
					// try to activate this particular convoy in the depot
					depot_frame_t *const frame = dynamic_cast<depot_frame_t *>( win_get_magic( (ptrdiff_t)depot ) );
					if(  frame  ) {
						frame->activate_convoi(self);
					}
				}
			}
		}
	}
	else {
		if(  env_t::verbose_debug  ) {
			dump();
		}
		create_win( new convoi_info_t(self), w_info, magic_convoi_info+self.get_id() );
	}
}


void convoi_t::info(cbuffer_t & buf) const
{
	const vehicle_t* v = fahr[0];
	if (v != NULL) {
		char tmp[128];

		buf.printf("\n %d/%dkm/h (%1.2f$/km)\n", speed_to_kmh(min_top_speed), v->get_desc()->get_topspeed(), get_running_cost() / 100.0);
		buf.printf(" %s: %ikW\n", translator::translate("Leistung"), sum_power);
		buf.printf(" %s: %i (%i) t\n", translator::translate("Gewicht"), sum_weight, sum_gesamtweight - sum_weight);
		buf.printf(" %s: ", translator::translate("Gewinn"));
		money_to_string(tmp, (double)jahresgewinn);
		buf.append(tmp);
		buf.append("\n");
	}
}


// sort order of convoi
void convoi_t::set_sortby(uint8 sort_order)
{
	freight_info_order = sort_order;
	freight_info_resort = true;
}


// caches the last info; resorts only when needed
void convoi_t::get_freight_info(cbuffer_t & buf)
{
	if(freight_info_resort) {
		freight_info_resort = false;
		// rebuilt the list with goods ...
		vector_tpl<ware_t> total_fracht;

		size_t const n = goods_manager_t::get_count();
		ALLOCA(uint32, max_loaded_waren, n);
		MEMZERON(max_loaded_waren, n);

		for(  uint32 i = 0;  i != anz_vehikel;  ++i  ) {
			const vehicle_t* v = fahr[i];

			// first add to capacity indicator
			const goods_desc_t* ware_desc = v->get_desc()->get_freight_type();
			const uint16 menge = v->get_desc()->get_capacity();
			if(menge>0  &&  ware_desc!=goods_manager_t::none) {
				max_loaded_waren[ware_desc->get_index()] += menge;
			}

			// then add the actual load
			FOR(slist_tpl<ware_t>, ware, v->get_cargo()) {
				FOR(vector_tpl<ware_t>, & tmp, total_fracht) {
					// could this be joined with existing freight?

					// for pax: join according next stop
					// for all others we *must* use target coordinates
					if( ware.same_destination(tmp) ) {
						tmp.menge += ware.menge;
						ware.menge = 0;
						break;
					}
				}

				// if != 0 we could not join it to existing => load it
				if(ware.menge != 0) {
					total_fracht.append(ware);
				}
			}

			INT_CHECK("simconvoi 2643");
		}
		buf.clear();

		// apend info on total capacity
		slist_tpl <ware_t>capacity;
		for (size_t i = 0; i != n; ++i) {
			if(max_loaded_waren[i]>0  &&  i!=goods_manager_t::INDEX_NONE) {
				ware_t ware(goods_manager_t::get_info(i));
				ware.menge = max_loaded_waren[i];
				// append to category?
				slist_tpl<ware_t>::iterator j   = capacity.begin();
				slist_tpl<ware_t>::iterator end = capacity.end();
				while (j != end && j->get_desc()->get_catg_index() < ware.get_desc()->get_catg_index()) ++j;
				if (j != end && j->get_desc()->get_catg_index() == ware.get_desc()->get_catg_index()) {
					j->menge += max_loaded_waren[i];
				} else {
					// not yet there
					capacity.insert(j, ware);
				}
			}
		}

		// show new info
		freight_list_sorter_t::sort_freight(total_fracht, buf, (freight_list_sorter_t::sort_mode_t)freight_info_order, &capacity, "loaded");
	}
}


void convoi_t::open_schedule_window( bool show )
{
	DBG_MESSAGE("convoi_t::open_schedule_window()","Id = %ld, State = %d, Lock = %d",self.get_id(), state, wait_lock);

	// manipulation of schedule not allowed while:
	// - just starting
	// - a line update is pending
	if(  (state==EDIT_SCHEDULE  ||  line_update_pending.is_bound())  &&  get_owner()==welt->get_active_player()  ) {
		if (show) {
			create_win( new news_img("Not allowed!\nThe convoi's schedule can\nnot be changed currently.\nTry again later!"), w_time_delete, magic_none );
		}
		return;
	}
	if(  state==COUPLED  ||  state==UNCOUPLING  ) {
		// fork, coupling: the primary drives us; its line's schedule decides where we part
		if(  show  &&  get_owner()==welt->get_active_player()  ) {
			create_win( new news_img("Not allowed!\nThis train is coupled to another\ntrain. Change its line's\nschedule instead."), w_time_delete, magic_none );
		}
		return;
	}

	if(state==DRIVING) {
		// book the current value of goods
		calc_gewinn();
	}

	akt_speed = 0; // stop the train ...
	if(state!=INITIAL) {
		state = EDIT_SCHEDULE;
	}
	wait_lock = 25000;
	alte_richtung = fahr[0]->get_direction();

	if(  show  ) {
		// Open schedule dialog
		create_win( new schedule_gui_t(schedule,get_owner(),self), w_info, (ptrdiff_t)schedule );
		// TODO: what happens if no client opens the window??
	}
	schedule->start_editing();
}


/**
 * Check validity of convoi with respect to vehicle constraints
 */
bool convoi_t::pruefe_alle()
{
	bool ok = anz_vehikel == 0  ||  fahr[0]->get_desc()->can_follow(NULL);
	unsigned i;

	const vehicle_t* pred = fahr[0];
	for(i = 1; ok && i < anz_vehikel; i++) {
		const vehicle_t* v = fahr[i];
		ok = pred->get_desc()->can_lead(v->get_desc())  &&
				 v->get_desc()->can_follow(pred->get_desc());
		pred = v;
	}
	if(ok) {
		ok = pred->get_desc()->can_lead(NULL);
	}

	return ok;
}


/**
 * Kontrolliert Be- und Entladen
 *
 * minimum_loading is now stored in the object (not returned)
 */
void convoi_t::laden()
{
	if(  state == EDIT_SCHEDULE  ) {
		return;
	}

	// fork, coupling: join the partner standing next to us
	if(  !coupled_convoi.is_bound()  &&  !handover_to.is_bound()  ) {
		bool standing;
		const convoihandle_t partner = find_partner_at( haltestelle_t::get_halt( schedule->get_current_entry().pos, owner ), standing );
		if(  partner.is_bound()  &&  standing  ) {
			if(  can_couple_here( partner.get_rep(), this )  ) {
				couple( partner, self );
			}
			else if(  can_couple_here( this, partner.get_rep() )  ) {
				couple( self, partner );
			}
			if(  state!=LOADING  ) {
				// we joined the other train
				return;
			}
		}
	}

	// just wait a little longer if this is a non-bound halt
	wait_lock = (WTT_LOADING*2)+(self.get_id())%1024;

	halthandle_t halt = haltestelle_t::get_halt(schedule->get_current_entry().pos,owner);
	// eigene haltestelle ?
	if(  halt.is_bound()  ) {
		const player_t* halt_owner = halt->get_owner();
		if(  halt_owner == get_owner()  ||  halt_owner == welt->get_public_player()  ) {
			// loading/unloading ...
			halt->request_loading( self );
		}
	}
}


/**
 * calculate income for last hop
 */
void convoi_t::calc_gewinn()
{
	sint64 gewinn = 0;

	for(unsigned i=0; i<anz_vehikel; i++) {
		vehicle_t* v = fahr[i];
		sint64 tmp = v->calc_revenue(v->last_stop_pos, v->get_pos() );
		// get_schedule is needed as v->get_waytype() returns track_wt for trams (instead of tram_wt
		owner->book_revenue(tmp, fahr[0]->get_pos().get_2d(), get_schedule()->get_waytype(), v->get_cargo_type()->get_index() );
		v->last_stop_pos = v->get_pos();
		if(  get_vehicle_owner(i) != self  ) {
			// fork, coupling: the joined train earned this
			convoi_t *c = coupled_convoi.get_rep();
			c->jahresgewinn += tmp;
			c->book( tmp, CONVOI_PROFIT );
			c->book( tmp, CONVOI_REVENUE );
		}
		else {
			gewinn += tmp;
		}
	}

	// update statistics of average speed
	if(  distance_since_last_stop  ) {
		financial_history[0][CONVOI_MAXSPEED] *= maxspeed_average_count;
		financial_history[0][CONVOI_MAXSPEED] += get_speedbonus_kmh();
		maxspeed_average_count ++;
		financial_history[0][CONVOI_MAXSPEED] /= maxspeed_average_count;
	}
	distance_since_last_stop = 0;
	sum_speed_limit = 0;

	if(gewinn) {
		jahresgewinn += gewinn;

		book(gewinn, CONVOI_PROFIT);
		book(gewinn, CONVOI_REVENUE);
	}
}


/**
 * convoi an haltestelle anhalten
 *
 * minimum_loading is now stored in the object (not returned)
 */
void convoi_t::hat_gehalten(halthandle_t halt)
{
	grund_t *gr=welt->lookup(fahr[0]->get_pos());

	// now find out station length
	uint16 vehicles_loading = 0;
	if(  gr->is_water()  ) {
		// harbour has any size
		vehicles_loading = anz_vehikel;
	}
	else {
		// calculate real station length
		// and numbers of vehicles that can be (un)loaded
		koord zv = koord( ribi_t::backward(fahr[0]->get_direction()) );
		koord3d pos = fahr[0]->get_pos();
		// start on bridge?
		pos.z += gr->get_weg_yoff() / TILE_HEIGHT_STEP;
		// difference between actual station length and vehicle lenghts
		sint16 station_length = -fahr[vehicles_loading]->get_desc()->get_length();
		do {
			// advance one station tile
			station_length += CARUNITS_PER_TILE;

			while(station_length >= 0) {
				vehicles_loading++;
				if (vehicles_loading < anz_vehikel) {
					station_length -= fahr[vehicles_loading]->get_desc()->get_length();
				}
				else {
					// all vehicles fit into station
					goto station_tile_search_ready;
				}
			}

			// search for next station tile
			pos += zv;
			gr = welt->lookup(pos);
			if (gr == NULL) {
				gr = welt->lookup(pos-koord3d(0,0,1));
				if (gr == NULL) {
					gr = welt->lookup(pos-koord3d(0,0,2));
				}
				if (gr  &&  (pos.z != gr->get_hoehe() + gr->get_weg_yoff()/TILE_HEIGHT_STEP) ) {
					// not end/start of bridge
					break;
				}
			}

		}  while(  gr  &&  gr->get_halt() == halt  );
		// finished
station_tile_search_ready: ;
	}

	// what the vehicles of a train may do here; a coupled primary has a second set for the joined train
	struct portion_t {
		bool next_depot;          // next stop in schedule will be a depot
		bool loads, no_load, hold_loading;
		const schedule_entry_t *entry;
		vector_tpl<halthandle_t> destination_halts;
		portion_t() : next_depot(false), loads(false), no_load(false), hold_loading(false), entry(NULL) {}
	};
	portion_t portions[2];
	convoi_t *const joined = is_coupled_primary() ? coupled_convoi.get_rep() : NULL;
	for(  int p=0;  p<(joined ? 2 : 1);  p++  ) {
		convoi_t *const c = p==0 ? this : joined;
		portion_t &pt = portions[p];
		const schedule_t *const sched = c->schedule;

		// stop type (fork): what this entry lets us do
		pt.entry = &sched->get_current_entry();
		pt.loads = pt.entry->loads();
		pt.no_load = c->no_load;

		// prepare a list of all destination halts in the schedule
		if (!pt.no_load  &&  pt.loads) {
			const uint8 count = sched->get_count();
			for(  uint8 i=1;  i<count;  i++  ) {
				const uint8 wrap_i = (i + sched->get_current_stop()) % count;
				const schedule_entry_t &next_entry = sched->entries[wrap_i];

				const halthandle_t plan_halt = haltestelle_t::get_halt(next_entry.pos, owner);
				if(plan_halt == halt) {
					// we will come later here again ...
					break;
				}
				else if(  !plan_halt.is_bound()  ) {
					if(  grund_t *gr = welt->lookup( next_entry.pos )  ) {
						if(  gr->get_depot()  ) {

							pt.next_depot = i==1;
							// do not load for stops after a depot
							break;
						}
					}
					continue;
				}
				if(  next_entry.unloads()  ) {
					pt.destination_halts.append(plan_halt);
				}
				if(  !next_entry.rides_through()  ) {
					// terminal or all-off: nothing aboard continues past it
					break;
				}
			}
		}

		// timetable (fork): while a convoy of the line that arrived earlier still waits at this stop,
		// it leaves first and gets the passengers; we only unload until then
		if(  !pt.no_load  &&  c->line.is_bound()  &&  pt.entry->has_timetable()  &&  welt->has_calendar()  ) {
			pt.hold_loading = c->line->count_earlier_waiting( c->self ) > 0;
		}
	}

	// only load vehicles in station
	// don't load when vehicle is being withdrawn
	bool changed_loading_level = false;
	uint32 time = WTT_LOADING; // min time for loading/unloading
	sint64 gewinn = 0;

	// cargo type of previous vehicle that could not be filled
	const goods_desc_t* cargo_type_prev = NULL;

	for(unsigned i=0; i<vehicles_loading; i++) {
		vehicle_t* v = fahr[i];
		// fork, coupling: the vehicles of the joined train follow its schedule and earn for it
		const bool of_joined = joined  &&  i>=coupled_first;
		const portion_t &pt = portions[of_joined ? 1 : 0];

		// we need not to call this on the same position
		if(  v->last_stop_pos != v->get_pos()  ) {
			sint64 tmp;
			// calc_revenue
			tmp = v->calc_revenue(v->last_stop_pos, v->get_pos() );
			owner->book_revenue(tmp, fahr[0]->get_pos().get_2d(), get_schedule()->get_waytype(), v->get_cargo_type()->get_index());
			v->last_stop_pos = v->get_pos();
			if(  of_joined  ) {
				joined->jahresgewinn += tmp;
				joined->book( tmp, CONVOI_PROFIT );
				joined->book( tmp, CONVOI_REVENUE );
			}
			else {
				gewinn += tmp;
			}
		}

		uint16 amount = v->unload_cargo(halt, pt.next_depot  ||  pt.entry->unloads_all(), pt.entry->unloads()  );
		if(  of_joined  &&  amount  ) {
			// the vehicle booked it for the train it runs in
			book( -(sint64)amount, CONVOI_TRANSPORTED_GOODS );
			joined->book( amount, CONVOI_TRANSPORTED_GOODS );
		}

		if(  !pt.no_load  &&  pt.loads  &&  !pt.hold_loading  &&  !pt.next_depot  &&  v->get_total_cargo() < v->get_cargo_max()  ) {
			// load if: unloaded something (might go back) or previous non-filled car requested different cargo type
			if (amount>0  ||  cargo_type_prev==NULL  ||  !cargo_type_prev->is_interchangeable(v->get_cargo_type())) {
				// load
				amount += v->load_cargo(halt, pt.destination_halts);
			}
			if (v->get_total_cargo() < v->get_cargo_max()) {
				// not full
				cargo_type_prev = v->get_cargo_type();
			}
		}

		if(  amount  ) {
			time = max( time, (amount*v->get_desc()->get_loading_time()) / max(v->get_cargo_max(), 1) );
			v->mark_image_dirty(v->get_image(), 0);
			v->calc_image();
			changed_loading_level = true;
		}
	}
	freight_info_resort |= changed_loading_level;
	if(  changed_loading_level  ) {
		halt->recalc_status();
	}

	// any unloading/loading went on?
	if(  changed_loading_level  ) {
		calc_loading();
		if(  joined  ) {
			joined->calc_loading();
			joined->freight_info_resort = true;
		}
	}
	loading_limit = schedule->get_current_entry().minimum_loading;
	if(  joined  ) {
		// fork, coupling: the joined train's loading rules hold the whole train as well
		joined->loading_limit = joined->schedule->get_current_entry().minimum_loading;
	}

	// update statistics of average speed
	if(  distance_since_last_stop  ) {
		financial_history[0][CONVOI_MAXSPEED] *= maxspeed_average_count;
		financial_history[0][CONVOI_MAXSPEED] += get_speedbonus_kmh();
		maxspeed_average_count ++;
		financial_history[0][CONVOI_MAXSPEED] /= maxspeed_average_count;
	}
	distance_since_last_stop = 0;
	sum_speed_limit = 0;

	if(gewinn) {
		jahresgewinn += gewinn;

		book(gewinn, CONVOI_PROFIT);
		book(gewinn, CONVOI_REVENUE);
	}

	// loading is finished => maybe drive on
	bool depart = is_ready_to_depart()  &&  (joined==NULL  ||  joined->is_ready_to_depart());
	const bool timetabled = !no_load  &&  line.is_bound()  &&  schedule->get_current_entry().has_timetable()  &&  welt->has_calendar();
	sint64 slot = -1;
	if(  depart  &&  timetabled  ) {
		// timetable (fork): only in an open slot, one convoy per slot, first come first served
		if(  couple_hold_slot >= 0  ) {
			// kept while we waited for our partner
			slot = couple_hold_slot;
		}
		else if(  running_late  ) {
			// after a missed coupling: the due slot at once
			if(  line->get_late_departure_slot( self, late_slot, slot )  ) {
				depart = slot >= 0;
			}
			else {
				// on time again
				running_late = false;
				late_slot = -1;
				depart = line->can_take_departure_slot( self, slot );
			}
		}
		else {
			depart = line->can_take_departure_slot( self, slot );
		}
	}

	// fork, coupling: wait for the train that joins us or that we join, but not for ever
	linehandle_t partner_line;
	uint8 partner_entry = 255;
	bool missed_partner = false;
	if(  depart  &&  !no_load  &&  late_slot < 0  ) {
		// (a train with a slot inherited here knows its primary is gone already)
		uint16 max_wait;
		if(  expects_partner( max_wait, partner_line, partner_entry )  ) {
			if(  couple_wait_since==0  ) {
				couple_wait_since = max( 1u, welt->get_ticks() );
				couple_hold_slot = slot;
			}
			if(  (sint64)(welt->get_ticks() - couple_wait_since) < welt->calendar_minutes_to_ticks( max_wait )  ) {
				depart = false;
			}
			else {
				missed_partner = true;
				// both stood here all along but could not get together: tell the player
				bool standing;
				const convoihandle_t partner = find_partner_at( halt, standing );
				if(  partner.is_bound()  &&  standing  ) {
					cbuffer_t buf;
					buf.printf( translator::translate("%s and %s could not couple at %s (no free track behind the first train). They run separately."), get_name(), partner->get_name(), halt->get_name() );
					welt->get_message()->add_message( buf, get_pos().get_2d(), message_t::warnings, PLAYER_FLAG|get_owner()->get_player_nr(), IMG_EMPTY );
				}
			}
		}
	}

	if(  depart  ) {

		if(  withdraw  &&  (loading_level == 0  ||  goods_catg_index.empty())  ) {
			// destroy when empty
			self_destruct();
			return;
		}

		if(  timetabled  &&  slot >= 0  ) {
			line->book_departure_slot( schedule->get_current_stop(), slot );
			if(  missed_partner  &&  partner_line.is_bound()  &&  partner_entry != 255  ) {
				// fork, coupling: the train that should have joined us runs late with this slot
				partner_line->add_missed_coupling( partner_entry, slot );
			}
		}
		couple_wait_since = 0;
		couple_hold_slot = -1;
		late_slot = -1;

		if(  joined  ) {
			// fork, coupling: together on to the same next stop, or the joined train stays here
			const halthandle_t next = next_stop_halt( schedule, schedule->get_current_stop(), owner );
			if(  !next.is_bound()  ||  next!=next_stop_halt( joined->schedule, joined->schedule->get_current_stop(), owner )  ) {
				uncouple_here();
			}
			else {
				const schedule_entry_t &joined_entry = joined->schedule->get_current_entry();
				sint64 joined_slot;
				if(  joined->line.is_bound()  &&  simline_t::get_open_departure_slot( joined_entry, joined_slot )  ) {
					// its slot is used as well
					joined->line->book_departure_slot( joined->schedule->get_current_stop(), joined_slot );
				}
				joined->schedule->advance();
			}
		}

		calc_speedbonus_kmh();

		// add available capacity after loading(!) to statistics
		for (unsigned i = 0; i<anz_vehikel; i++) {
			get_vehicle_owner(i)->book(get_vehikel(i)->get_cargo_max()-get_vehikel(i)->get_total_cargo(), CONVOI_CAPACITY);
		}

		// Advance schedule
		schedule->advance();
		state = ROUTING_1;
		loading_limit = 0;
		if(  coupled_convoi.is_bound()  ) {
			coupled_convoi->loading_limit = 0;
		}
	}

	INT_CHECK( "convoi_t::hat_gehalten" );

	// at least wait the minimum time for loading
	wait_lock = time;
}


bool convoi_t::is_ready_to_depart() const
{
	const schedule_entry_t &entry = schedule->get_current_entry();
	if(  loading_level >= loading_limit  ||  no_load  ||  !entry.loads()  ) {
		return true;
	}
	return entry.has_waiting_time()  &&  welt->get_ticks() - arrived_time > entry.get_waiting_ticks();
}


bool convoi_t::get_planned_departure(sint64 &minutes, bool &latest) const
{
	if(  state != LOADING  ||  !welt->has_calendar()  ||  schedule == NULL  ||  schedule->empty()  ) {
		return false;
	}
	const schedule_entry_t &entry = schedule->get_current_entry();
	const sint64 now = welt->get_calendar_minutes();
	// when will the loading rules let us go?
	sint64 ready_at = now;
	latest = false;
	if(  !is_ready_to_depart()  ) {
		if(  !entry.has_waiting_time()  ) {
			// waits for its load, no telling how long
			return false;
		}
		ready_at = max( now, welt->get_calendar_minutes_at( arrived_time + entry.get_waiting_ticks() ) );
		latest = true;
	}
	if(  is_coupled_primary()  &&  !coupled_convoi->is_ready_to_depart()  ) {
		// fork, coupling: the joined train's loading rules hold us too
		const convoi_t *c = coupled_convoi.get_rep();
		const schedule_entry_t &c_entry = c->schedule->get_current_entry();
		if(  !c_entry.has_waiting_time()  ) {
			return false;
		}
		ready_at = max( ready_at, welt->get_calendar_minutes_at( c->arrived_time + c_entry.get_waiting_ticks() ) );
		latest = true;
	}
	if(  couple_wait_since  ) {
		// fork, coupling: waiting for the partner, but not longer than this
		uint16 max_wait;
		linehandle_t partner_line;
		uint8 partner_entry;
		if(  expects_partner( max_wait, partner_line, partner_entry )  ) {
			minutes = max( ready_at, welt->get_calendar_minutes_at( couple_wait_since + (uint32)welt->calendar_minutes_to_ticks( max_wait ) ) );
			latest = true;
			return true;
		}
	}
	if(  line.is_bound()  &&  entry.has_timetable()  &&  !no_load  ) {
		sint64 slot;
		if(  running_late  &&  line->get_late_departure_slot( self, late_slot, slot )  ) {
			// fork, coupling: late, in a slot that is due already
			if(  slot >= 0  ) {
				minutes = ready_at;
				return true;
			}
		}
		return line->get_planned_departure( self, ready_at, minutes );
	}
	if(  latest  ) {
		minutes = ready_at;
		return true;
	}
	return false;
}


sint64 convoi_t::calc_restwert() const
{
	sint64 result = 0;

	for(uint i=0; i<anz_vehikel; i++) {
		result += fahr[i]->calc_sale_value();
	}
	return result;
}


/**
 * Calculate loading_level and loading_limit. This depends on current state (loading or not).
 */
void convoi_t::calc_loading()
{
	int fracht_max = 0;
	int fracht_menge = 0;
	for(unsigned i=0; i<anz_vehikel; i++) {
		const vehicle_t* v = fahr[i];
		fracht_max += v->get_cargo_max();
		fracht_menge += v->get_total_cargo();
	}
	loading_level = fracht_max > 0 ? (fracht_menge*100)/fracht_max : 100;
	loading_limit = 0; // will be set correctly from hat_gehalten() routine

	// since weight has changed
	recalc_data=true;
}


void convoi_t::calc_traction_sums(bool off_wire, bool both_under_wire, sint32 &gear_and_power, sint32 &top_speed, sint32 &running_costs) const
{
	gear_and_power = 0;
	top_speed = SPEED_UNLIMITED;
	running_costs = 0;
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		const vehicle_desc_t *desc = fahr[i]->get_desc();
		if(  is_traction_active( desc, traction_mixed, off_wire, both_under_wire )  ) {
			gear_and_power += desc->get_power()*desc->get_gear();
			top_speed = min( top_speed, kmh_to_speed( desc->get_topspeed() ) );
			running_costs -= desc->get_running_cost();
		}
	}
}


void convoi_t::recalc_traction(bool choose)
{
	bool has_electric = false;
	bool has_other = false;
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		const vehicle_desc_t *desc = fahr[i]->get_desc();
		if(  desc->get_power()  ) {
			if(  desc->get_engine_type()==vehicle_desc_t::electric  ) {
				has_electric = true;
			}
			else {
				has_other = true;
			}
		}
	}
	is_electric = has_electric  &&  !has_other;
	traction_mixed = has_electric  &&  has_other;

	traction_off_wire = false;
	if(  traction_mixed  ) {
		for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
			const vehicle_desc_t *desc = fahr[i]->get_desc();
			if(  desc->get_power()  &&  desc->get_engine_type()==vehicle_desc_t::electric  &&  !fahr[i]->update_on_wire()  ) {
				traction_off_wire = true;
			}
		}
		if(  choose  ) {
			// the other engines pull under wires only if that gives a higher top speed with the current load
			sint64 total_weight = 0;
			for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
				total_weight += fahr[i]->get_total_weight();
			}
			sint32 power_electric, top_electric, power_both, top_both, costs;
			calc_traction_sums( false, false, power_electric, top_electric, costs );
			calc_traction_sums( false, true, power_both, top_both, costs );
			traction_both_under_wire = total_weight > 0  &&
				calc_max_speed( power_both, total_weight, top_both ) > calc_max_speed( power_electric, total_weight, top_electric );
		}
	}
	else {
		traction_both_under_wire = false;
	}

	if(  traction_mixed  ) {
		// the limits in both modes, for route checks of tiles with and without catenary
		sint32 power, costs;
		calc_traction_sums( false, traction_both_under_wire, power, traction_top_speed_under_wire, costs );
		calc_traction_sums( true, false, power, traction_top_speed_off_wire, costs );
	}

	const sint32 old_gear_and_power = sum_gear_and_power;
	const sint32 old_top_speed = min_top_speed;
	calc_traction_sums( traction_off_wire, traction_both_under_wire, sum_gear_and_power, min_top_speed, sum_running_costs );
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		fahr[i]->set_idle( !is_traction_active( fahr[i]->get_desc(), traction_mixed, traction_off_wire, traction_both_under_wire ) );
	}
	if(  old_gear_and_power != sum_gear_and_power  ||  old_top_speed != min_top_speed  ) {
		recalc_speed_limit = true;
		// other road vehicles judge overtaking by the speed this convoy reaches with the engines that pull now
		if(  anz_vehikel > 0  &&  front()->get_overtaker()  ) {
			sint64 total_weight = 0;
			for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
				total_weight += fahr[i]->get_total_weight();
			}
			if(  total_weight > 0  ) {
				max_power_speed = calc_max_speed( sum_gear_and_power, total_weight, min_top_speed );
			}
		}
	}
}


uint32 convoi_t::get_active_power() const
{
	uint32 power = 0;
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		if(  !fahr[i]->is_idle()  ) {
			power += fahr[i]->get_desc()->get_power();
		}
	}
	return power;
}


sint32 convoi_t::calc_traction_max_speed(uint64 total_weight, bool off_wire) const
{
	sint32 power, top_speed, costs;
	if(  !traction_mixed  ) {
		calc_traction_sums( false, false, power, top_speed, costs );
		return calc_max_speed( power, total_weight, top_speed );
	}
	if(  off_wire  ) {
		calc_traction_sums( true, false, power, top_speed, costs );
		return calc_max_speed( power, total_weight, top_speed );
	}
	calc_traction_sums( false, false, power, top_speed, costs );
	const sint32 speed_electric = calc_max_speed( power, total_weight, top_speed );
	calc_traction_sums( false, true, power, top_speed, costs );
	return max( speed_electric, calc_max_speed( power, total_weight, top_speed ) );
}


void convoi_t::play_start_sound() const
{
	if(  !traction_mixed  ) {
		fahr[0]->play_sound();
		return;
	}
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		if(  fahr[i]->get_desc()->get_power()  &&  !fahr[i]->is_idle()  ) {
			fahr[i]->play_sound();
			return;
		}
	}
}


void convoi_t::calc_speedbonus_kmh()
{
	// fork: choose the engines for the current load first, this also sets min_top_speed
	recalc_traction( true );

	// init with default
	const sint32 cnv_min_top_kmh = speed_to_kmh( min_top_speed );
	speedbonus_kmh = cnv_min_top_kmh;
	// flying aircraft have 0 friction --> speed not limited by power, so just use top_speed
	if(  front()!=NULL  &&  front()->get_waytype() != air_wt  ) {
		sint32 total_max_weight = 0;
		sint32 total_weight = 0;
		for(  unsigned i=0;  i<anz_vehikel;  i++  ) {
			const vehicle_desc_t* const desc = fahr[i]->get_desc();
			total_max_weight += desc->get_weight();
			total_weight += fahr[i]->get_total_weight(); // convoi_t::sum_gesamweight may not be updated yet when this method is called...
			if(  desc->get_freight_type() == goods_manager_t::none  ) {
				; // nothing
			}
			else if(  desc->get_freight_type()->get_catg() == 0  ) {
				// use full weight for passengers, mail, and special goods
				total_max_weight += desc->get_freight_type()->get_weight_per_unit() * desc->get_capacity();
			}
			else {
				// use actual weight for regular goods
				total_max_weight += fahr[i]->get_cargo_weight();
			}
		}
		// very old vehicles have zero weight ...
		if(  total_weight>0  ) {

			// fork: under wires with the better choice of engines; the average of the speed limits
			// (sum_speed_limit) accounts for the slower sections off wires
			speedbonus_kmh = speed_to_kmh( calc_traction_max_speed( total_max_weight, false ) );
			if(  traction_mixed  ) {
				// credited per tile in add_running_cost(), so a slow section off wires lowers the average
				traction_power_speed_under_wire = calc_traction_max_speed( total_max_weight, false );
				traction_power_speed_off_wire = calc_traction_max_speed( total_max_weight, true );
			}

			// convoi overtakers use current actual weight for achievable speed
			if(  front()->get_overtaker()  ) {
				max_power_speed = calc_max_speed(sum_gear_and_power, total_weight, min_top_speed);
			}
		}
	}
}


// return the current bonus speed
sint32 convoi_t::get_speedbonus_kmh() const
{
	if(  distance_since_last_stop > 0  &&  front()!=NULL  &&  front()->get_waytype() != air_wt  ) {
		return min( speedbonus_kmh, (sint32)(sum_speed_limit / distance_since_last_stop) );
	}
	return speedbonus_kmh;
}


// return the current bonus speed
uint32 convoi_t::get_average_kmh() const
{
	if(  distance_since_last_stop > 0  ) {
		return sum_speed_limit / distance_since_last_stop;
	}
	return speedbonus_kmh;
}


/**
 * Schedule convois for self destruction. Will be executed
 * upon next sync step
 */
void convoi_t::self_destruct()
{
	line_update_pending = linehandle_t(); // does not bother to add it to a new line anyway ...
	// convois in depot are not contained in the map array!
	if(state==INITIAL) {
		destroy();
	}
	else {
		state = SELF_DESTRUCT;
		wait_lock = 0;
	}
}


/**
 * Helper method to remove convois from the map that cannot
 * removed normally (i.e. by sending to a depot) anymore.
 * This is a workaround for bugs in the game.
 */
void convoi_t::destroy()
{
	// fork, coupling: part from the other train first
	if(  coupled_convoi.is_bound()  ) {
		convoi_t *other = coupled_convoi.get_rep();
		if(  other->coupled_convoi==self  &&  other->is_coupled_primary()  ) {
			// we are joined to it: our vehicles leave its train (and the map, below)
			other->detach_coupled_vehicles();
		}
		else if(  is_coupled_primary()  ) {
			release_coupled_in_place();
		}
		coupled_convoi = convoihandle_t();
	}
	if(  handover_to.is_bound()  ) {
		// the train we uncoupled takes the platform when it is free
		handover_to = convoihandle_t();
	}

	// can be only done here, with a valid convoihandle ...
	if(fahr[0]) {
		fahr[0]->set_convoi(NULL);
	}

	if(  state == INITIAL  ) {
		// in depot => not on map
		for(  uint8 i = anz_vehikel;  i-- != 0;  ) {
			fahr[i]->set_flag( obj_t::not_on_map );
		}
	}
	state = SELF_DESTRUCT;

	if(schedule!=NULL  &&  !schedule->is_editing_finished()) {
		destroy_win((ptrdiff_t)schedule);
	}

	if(  line.is_bound()  ) {
		// needs to be done here to remove correctly ware catg from lines
		unset_line();
		delete schedule;
		schedule = NULL;
	}

	// pay the current value
	owner->book_new_vehicle( calc_restwert(), get_pos().get_2d(), fahr[0] ? fahr[0]->get_desc()->get_waytype() : ignore_wt );

	for(  uint8 i = anz_vehikel;  i-- != 0;  ) {
		if(  !fahr[i]->get_flag( obj_t::not_on_map )  ) {
			// remove from rails/roads/crossings
			grund_t *gr = welt->lookup(fahr[i]->get_pos());
			fahr[i]->set_last( true );
			fahr[i]->leave_tile();
			if(  gr  &&  gr->ist_uebergang()  ) {
				gr->find<crossing_t>()->release_crossing(fahr[i]);
			}
			fahr[i]->set_flag( obj_t::not_on_map );

		}
		// no need to substract maintenance, since it is booked as running costs

		fahr[i]->discard_cargo();
		fahr[i]->cleanup(owner);
		delete fahr[i];
	}
	anz_vehikel = 0;

	delete this;
}


/**
 * Debug info nach stderr
 */
void convoi_t::dump() const
{
	dbg->debug("convoi::dump()",
		"\nanz_vehikel = %d\n"
		"wait_lock = %d\n"
		"owner_n = %d\n"
		"akt_speed = %d\n"
		"akt_speed_soll = %d\n"
		"sp_soll = %d\n"
		"state = %d\n"
		"statename = %s\n"
		"alte_richtung = %d\n"
		"jahresgewinn = %ld\n" // %lld crashes mingw now, cast gewinn to long ...
		"name = '%s'\n"
		"line_id = '%d'\n"
		"schedule = '%p'",
		(int)anz_vehikel,
		(int)wait_lock,
		(int)welt->sp2num(owner),
		(int)akt_speed,
		(int)akt_speed_soll,
		(int)sp_soll,
		(int)state,
		(const char *)(state_names[state]),
		(int)alte_richtung,
		(long)(jahresgewinn/100),
		(const char *)name_and_id,
		line.is_bound() ? line.get_id() : 0,
		(const void *)schedule );
}


void convoi_t::book(sint64 amount, int cost_type)
{
	assert(  cost_type<MAX_CONVOI_COST);

	financial_history[0][cost_type] += amount;
	if (line.is_bound()) {
		line->book( amount, simline_t::convoi_to_line_catgory(cost_type) );
	}
}


void convoi_t::init_financial_history()
{
	for (int j = 0; j<MAX_CONVOI_COST; j++) {
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][j] = 0;
		}
	}
}


sint64 convoi_t::get_purchase_cost() const
{
	sint64 purchase_cost = 0;
	for(  unsigned i = 0;  i < get_vehicle_count();  i++  ) {
		purchase_cost += fahr[i]->get_desc()->get_price();
	}
	return purchase_cost;
}


/**
* set line
* since convoys must operate on a copy of the route's schedule, we apply a fresh copy
*/
void convoi_t::set_line(linehandle_t org_line)
{
	// to remove a convoi from a line, call unset_line(); passing a NULL is not allowed!
	if(!org_line.is_bound()) {
		return;
	}
	if(  line.is_bound()  ) {
		unset_line();
	}
	else {
		// originally a lineless convoy -> unregister itself from stops as it now belongs to a line
		unregister_stops();
		// must trigger refresh if old schedule was not empty
		if (schedule  &&  !schedule->empty()) {
			welt->set_schedule_counter();
		}
	}
	line_update_pending = org_line;
	check_pending_updates();
}


/**
* unset line
* removes convoy from route without destroying its schedule
* => no need to recalculate connections!
*/
void convoi_t::unset_line()
{
	if(  line.is_bound()  ) {
DBG_DEBUG("convoi_t::unset_line()", "removing old destinations from line=%d, schedule=%p",line.get_id(),schedule);
		line->remove_convoy(self);
		line = linehandle_t();
		line_update_pending = linehandle_t();
	}
}


// matches two halts; if the pos is not identical, maybe the halt still is the same
bool convoi_t::matches_halt( const koord3d pos1, const koord3d pos2 )
{
	halthandle_t halt1 = haltestelle_t::get_halt(pos1, owner );
	return pos1==pos2  ||  (halt1.is_bound()  &&  halt1==haltestelle_t::get_halt( pos2, owner ));
}


// updates a line schedule and tries to find the best next station to go
void convoi_t::check_pending_updates()
{
	if(  line_update_pending.is_bound()  ) {
		// create dummy schedule
		if(  schedule==NULL  ) {
			schedule = create_schedule();
		}
		schedule_t* new_schedule = line_update_pending->get_schedule();
		int current_stop = schedule->get_current_stop(); // save current position of schedule
		bool is_same = false;
		bool is_depot = false;
		koord3d current = koord3d::invalid, depot = koord3d::invalid;

		if (schedule->empty() || new_schedule->empty()) {
			// was no entry or is no entry => goto  1st stop
			current_stop = 0;
		}
		else {
			// something to check for ...
			current = schedule->get_current_entry().pos;

			if(  current_stop<new_schedule->get_count() &&  current==new_schedule->entries[current_stop].pos  ) {
				// next pos is the same => keep the convoi state
				is_same = true;
			}
			else {
				// check depot first (must also keept this state)
				is_depot = (welt->lookup(current)  &&  welt->lookup(current)->get_depot() != NULL);

				if(is_depot) {
					// depot => current_stop+1 (depot will be restore later before this)
					depot = current;
					schedule->remove();
					current = schedule->get_current_entry().pos;
				}

				/* there could be only one entry that matches best:
				 * we try first same sequence as in old schedule;
				 * if not found, we try for same nextnext station
				 * (To detect also places, where only the platform
				 *  changed, we also compare the halthandle)
				 */
				const koord3d next = schedule->entries[(current_stop+1)%schedule->get_count()].pos;
				const koord3d nextnext = schedule->entries[(current_stop+2)%schedule->get_count()].pos;
				const koord3d nextnextnext = schedule->entries[(current_stop+3)%schedule->get_count()].pos;
				int how_good_matching = 0;
				const uint8 new_count = new_schedule->get_count();

				for(  uint8 i=0;  i<new_count;  i++  ) {
					int quality =
						matches_halt(current,new_schedule->entries[i].pos)*3 +
						matches_halt(next,new_schedule->entries[(i+1)%new_count].pos)*4 +
						matches_halt(nextnext,new_schedule->entries[(i+2)%new_count].pos)*2 +
						matches_halt(nextnextnext,new_schedule->entries[(i+3)%new_count].pos);
					if(  quality>how_good_matching  ) {
						// better match than previous: but depending of distance, the next number will be different
						if(  matches_halt(current,new_schedule->entries[i].pos)  ) {
							current_stop = i;
						}
						else if(  matches_halt(next,new_schedule->entries[(i+1)%new_count].pos)  ) {
							current_stop = i+1;
						}
						else if(  matches_halt(nextnext,new_schedule->entries[(i+2)%new_count].pos)  ) {
							current_stop = i+2;
						}
						else if(  matches_halt(nextnextnext,new_schedule->entries[(i+3)%new_count].pos)  ) {
							current_stop = i+3;
						}
						current_stop %= new_count;
						how_good_matching = quality;
					}
				}

				if(how_good_matching==0) {
					// nothing matches => take the one from the line
					current_stop = new_schedule->get_current_stop();
				}
				// if we go to same, then we do not need route recalculation ...
				is_same = matches_halt(current,new_schedule->entries[current_stop].pos);
			}
		}

		// we may need to update the line and connection tables
		if(  !line.is_bound()  ) {
			line_update_pending->add_convoy(self);
		}
		line = line_update_pending;
		line_update_pending = linehandle_t();

		// destroy old schedule and all related windows
		if(!schedule->is_editing_finished()) {
			schedule->copy_from( new_schedule );
			schedule->set_current_stop(current_stop); // set new schedule current position to best match
			schedule->start_editing();
		}
		else {
			schedule->copy_from( new_schedule );
			schedule->set_current_stop(current_stop); // set new schedule current position to one before best match
		}

		if(is_depot) {
			// next was depot. restore it
			schedule->insert(welt->lookup(depot));
			schedule->set_current_stop( (schedule->get_current_stop()+schedule->get_count()-1)%schedule->get_count() );
		}

		if (state != INITIAL) {
			// remove wrong freight
			check_freight();

			if(  state==COUPLED  ||  state==UNCOUPLING  ) {
				// fork, coupling: the primary drives us, we part where the schedules part
			}
			else if(is_same  ||  is_depot) {
				/* same destination
				 * We are already there => keep current state
				 */
			}
			else {
				// need re-routing
				state = EDIT_SCHEDULE;
			}
			// make this change immediately
			if(  state!=LOADING  ) {
				wait_lock = 0;
			}
		}
	}
}


/**
 * Register the convoy with the stops in the schedule
 */
void convoi_t::register_stops()
{
	if(  schedule  ) {
		FOR(minivec_tpl<schedule_entry_t>, const& i, schedule->entries) {
			halthandle_t const halt = haltestelle_t::get_halt(i.pos, get_owner());
			if(  halt.is_bound()  ) {
				halt->add_convoy(self);
			}
		}
	}
}


/**
 * Unregister the convoy from the stops in the schedule
 */
void convoi_t::unregister_stops()
{
	if(  schedule  ) {
		FOR(minivec_tpl<schedule_entry_t>, const& i, schedule->entries) {
			halthandle_t const halt = haltestelle_t::get_halt(i.pos, get_owner());
			if(  halt.is_bound()  ) {
				halt->remove_convoy(self);
			}
		}
	}
}


// set next stop before breaking will occur (or route search etc.)
// currently only used for tracks
void convoi_t::set_next_stop_index(uint16 n)
{
	// stop at station or signals, not at waypoints
	if(  n==INVALID_INDEX  ) {
		// find out if stop or waypoint, waypoint: do not brake at waypoints
		grund_t const* const gr = welt->lookup(route.back());
		if(  gr  &&  gr->is_halt()  ) {
			n = route.get_count()-1-1; // extra -1 to brake 1 tile earlier when entering station
		}
	}
	next_stop_index = n+1;
}


/* including this route_index, the route was reserved the last time
 * currently only used for tracks
 */
void convoi_t::set_next_reservation_index(uint16 n)
{
	// stop at station or signals, not at waypoints
	if(  n==INVALID_INDEX  ) {
		n = route.get_count()-1;
	}
	next_reservation_index = n;
}


/*
 * the current state saved as color
 * Meanings are BLACK (ok), WHITE (no convois), YELLOW (no vehicle moved), RED (last month income minus), BLUE (at least one convoi vehicle is obsolete)
 */
PIXVAL convoi_t::get_status_color() const
{
	if(state==INITIAL) {
		// in depot/under assembly
		return SYSCOL_TEXT_HIGHLIGHT;
	}
	else if (state == WAITING_FOR_CLEARANCE_ONE_MONTH || state == CAN_START_ONE_MONTH || get_state() == NO_ROUTE) {
		// stuck or no route
		return color_idx_to_rgb(COL_ORANGE);
	}
	else if(financial_history[0][CONVOI_PROFIT]+financial_history[1][CONVOI_PROFIT]<0) {
		// ok, not performing best
		return MONEY_MINUS;
	}
	else if((financial_history[0][CONVOI_OPERATIONS]|financial_history[1][CONVOI_OPERATIONS])==0) {
		// nothing moved
		return SYSCOL_TEXT_UNUSED;
	}
	else if(has_obsolete) {
		return SYSCOL_OBSOLETE;
	}
	// normal state
	return SYSCOL_TEXT;
}


// returns tiles needed for this convoi
uint16 convoi_t::get_tile_length() const
{
	uint16 carunits=0;
	for(uint8 i=0;  i<anz_vehikel-1;  i++) {
		carunits += fahr[i]->get_desc()->get_length();
	}
	// the last vehicle counts differently in stations and for reserving track
	// (1) add 8 = 127/256 tile to account for the driving in stations in north/west direction
	//     see at the end of vehicle_t::hop()
	// (2) for length of convoi for loading in stations the length of the last vehicle matters
	//     see convoi_t::hat_gehalten
	carunits += max(CARUNITS_PER_TILE/2, fahr[anz_vehikel-1]->get_desc()->get_length());

	uint16 tiles = (carunits + CARUNITS_PER_TILE - 1) / CARUNITS_PER_TILE;
	return tiles;
}


// if withdraw and empty, then self destruct
void convoi_t::set_withdraw(bool new_withdraw)
{
	withdraw = new_withdraw;
	if(  withdraw  &&  (loading_level==0  ||  goods_catg_index.empty())) {
		// test if convoi in depot and not driving
		grund_t *gr = welt->lookup( get_pos());
		if(  gr  &&  gr->get_depot()  &&  state == INITIAL  ) {
#if 1
			// do not touch line bound convois in depots
			withdraw = false;
			no_load = false;
#else
			// disassemble also line bound convois in depots
			gr->get_depot()->disassemble_convoi(self, true);
#endif
		}
		else {
			self_destruct();
		}
	}
}


/**
 * conditions for a city car to overtake another overtaker.
 * The city car is not overtaking/being overtaken.
 */
bool convoi_t::can_overtake(overtaker_t *other_overtaker, sint32 other_speed, sint16 steps_other)
{
	if(fahr[0]->get_waytype()!=road_wt) {
		return false;
	}

	if (!other_overtaker->can_be_overtaken()) {
		return false;
	}

	if(  other_speed == 0  ) {
		// passing a standing convoi: only alongside it, the tile after it is checked when we get there
		const sint8 tiles = get_tiles_to_pass_standing( other_overtaker, fahr[0]->get_route_index() );
		if(  tiles == 0  ) {
			return false;
		}
		set_tiles_passing_standing( tiles );
		return true;
	}

	int diff_speed = akt_speed - other_speed;
	if(  diff_speed < kmh_to_speed(5)  ) {
		return false;
	}

	// Number of tiles overtaking will take
	int n_tiles = 0;

	// Distance it takes overtaking (unit: vehicle_steps) = my_speed * time_overtaking
	// time_overtaking = tiles_to_overtake/diff_speed
	// tiles_to_overtake = convoi_length + current pos within tile + (pos_other_convoi within tile + length of other convoi) - one tile
	sint32 distance = akt_speed*(fahr[0]->get_steps()+get_length_in_steps()+steps_other-VEHICLE_STEPS_PER_TILE)/diff_speed;
	sint32 time_overtaking = 0;

	// Conditions for overtaking:
	// Flat tiles, with no stops, no crossings, no signs, no change of road speed limit
	// First phase: no traffic except me and my overtaken car in the dangerous zone
	unsigned int route_index = fahr[0]->get_route_index()+1;
	koord3d pos = fahr[0]->get_pos();
	koord3d pos_prev = route_index > 2 ? route.at(route_index-2) : pos;
	koord3d pos_next;

	while( distance > 0 ) {

		if(  route_index >= route.get_count()  ) {
			return false;
		}

		pos_next = route.at(route_index++);
		grund_t *gr = welt->lookup(pos);
		// no ground, or slope => about
		if(  gr==NULL  ||  gr->get_weg_hang()!=slope_t::flat  ) {
			return false;
		}

		weg_t *str = gr->get_weg(road_wt);
		if(  str==NULL  ) {
			return false;
		}
		// the only roadsign we must account for are choose points and traffic lights
		if(  str->has_sign()  ) {
			const roadsign_t *rs = gr->find<roadsign_t>(1);
			if(rs) {
				const roadsign_desc_t *rb = rs->get_desc();
				if(rb->is_choose_sign()  ||  rb->is_traffic_light()  ) {
					// because we may need to stop here ...
					return false;
				}
			}
		}
		// not overtaking on railroad crossings or on normal crossings ...
		if(  str->is_crossing()  ||  ribi_t::is_threeway(str->get_ribi())  ) {
			return false;
		}
		// street gets too slow (TODO: should be able to be correctly accounted for)
		if(  akt_speed > kmh_to_speed(str->get_max_speed())  ) {
			return false;
		}

		int d = ribi_t::is_straight(str->get_ribi()) ? VEHICLE_STEPS_PER_TILE : vehicle_base_t::get_diagonal_vehicle_steps_per_tile();
		distance -= d;
		time_overtaking += d;

		// Check for other vehicles
		const uint8 top = gr->get_top();
		for(  uint8 j=1;  j<top;  j++ ) {
			if (vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(j))) {
				// check for other traffic on the road
				const overtaker_t *ov = v->get_overtaker();
				if(ov) {
					if(this!=ov  &&  other_overtaker!=ov) {
						return false;
					}
				}
				else if(  v->get_waytype()==road_wt  &&  v->get_typ()!=obj_t::pedestrian  ) {
					// sheeps etc.
					return false;
				}
			}
		}
		n_tiles++;
		pos_prev = pos;
		pos = pos_next;
	}

	// Second phase: only facing traffic is forbidden

	// the original routine was checking using the maximum road speed.
	// However, we can tolerate slower vehicles if they are closer

	// Furthermore, if we reach the end of the route for a vehcile as fast as us,
	// we simply assume it to be ok too
	sint32 overtaking_distance = time_overtaking;
	distance = 0; // distance to needed traveled to crash int us from this point
	time_overtaking = (time_overtaking << 16)/akt_speed;
	while(  time_overtaking > 0  ) {

		if(  route_index >= route.get_count()  ) {
			return distance>=time_overtaking; // we assume ok, if there is enough distance when we would face ourselves
		}

		pos_next = route.at(route_index++);
		grund_t *gr= welt->lookup(pos);
		if(  gr==NULL  ) {
			// will cause a route search, but is ok
			break;
		}

		weg_t *str = gr->get_weg(road_wt);
		if(  str==NULL  ) {
			break;
		}
		// cannot check for oncoming traffic over crossings
		if(  ribi_t::is_threeway(str->get_ribi()) ) {
			return false;
		}

		if(  ribi_t::is_straight(str->get_ribi())  ) {
			time_overtaking -= (VEHICLE_STEPS_PER_TILE<<16) / kmh_to_speed(str->get_max_speed());
			distance -= VEHICLE_STEPS_PER_TILE;
		}
		else {
			time_overtaking -= (vehicle_base_t::get_diagonal_vehicle_steps_per_tile()<<16) / kmh_to_speed(str->get_max_speed());
			distance -= vehicle_base_t::get_diagonal_vehicle_steps_per_tile();
		}

		// Check for other vehicles in facing direction
		ribi_t::ribi their_direction = ribi_t::backward( fahr[0]->calc_direction(pos_prev, pos_next) );
		const uint8 top = gr->get_top();
		for(  uint8 j=1;  j<top;  j++ ) {
			vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(j));
			if(  v  &&  v->get_direction() == their_direction  &&  v->get_overtaker()  ) {
				// tolerated distance us>them: total_distance*akt_speed > current_distance*other_speed
				if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(v)  ) {
					convoi_t* const ocnv = car->get_convoi();
					if(  ocnv  &&  ocnv->get_max_power_speed()*distance > akt_speed*overtaking_distance  ) {
						return false;
					}
				}
				else if(  private_car_t* const caut = obj_cast<private_car_t>(v)  ) {
					if(  caut->get_desc()->get_topspeed()*distance > akt_speed*overtaking_distance  ) {
						return false;
					}
				}
			}
		}
		pos_prev = pos;
		pos = pos_next;
	}

	set_tiles_overtaking( 1+n_tiles );
	other_overtaker->set_tiles_overtaking( -1-(n_tiles*(akt_speed-diff_speed))/akt_speed );
	return true;
}


/**
 * Passing a standing convoi: our route from start_index must run along its tiles (no
 * crossings or junctions there, since nothing is checked alongside it) and go on for at
 * least one tile, and all of these must be free of other traffic. Bends, junctions and
 * crossings before it or after it do not matter, the tile after it is checked like in
 * normal driving when we get there.
 */
sint8 convoi_t::get_tiles_to_pass_standing(const overtaker_t *other, uint32 start_index) const
{
	sint8 alongside = 0;
	for(  uint32 idx = start_index;  idx < route.get_count()  &&  alongside < 16;  idx++  ) {
		const grund_t *gr = welt->lookup( route.at(idx) );
		const weg_t *str = gr ? gr->get_weg(road_wt) : NULL;
		bool other_here;
		if(  str==NULL  ||  !vehicle_base_t::is_free_for_passing( gr, this, other, other_here )  ) {
			return 0;
		}
		if(  !other_here  ) {
			// first tile after it
			return alongside > 0 ? alongside + 1 : 0;
		}
		if(  str->is_crossing()  ||  ribi_t::is_threeway( str->get_ribi() )  ) {
			return 0;
		}
		alongside++;
	}
	// our route ends alongside it: we stop behind it
	return 0;
}


sint64 convoi_t::get_stat_converted(int month, int cost_type) const
{
	sint64 value = financial_history[month][cost_type];
	switch(cost_type) {
		case CONVOI_REVENUE:
		case CONVOI_OPERATIONS:
		case CONVOI_PROFIT:
		case CONVOI_WAYTOLL:
			value = convert_money(value);
			break;
		default: ;
	}
	return value;
}


/* ---------------------------------------------------------------------------------------------
 * Fork: coupling of trains (see coupled_convoi in simconvoi.h and "Coupling" in CLAUDE.md)
 */

// the halt of the first stop after schedule entry `from`, waypoints skipped; unbound if a depot comes first
static halthandle_t next_stop_halt(const schedule_t *sched, uint8 from, const player_t *owner)
{
	karte_ptr_t welt;
	const uint8 count = sched->get_count();
	for(  uint8 i=1;  i<=count;  i++  ) {
		const koord3d pos = sched->entries[(from+i)%count].pos;
		const halthandle_t halt = haltestelle_t::get_halt( pos, owner );
		if(  halt.is_bound()  ) {
			return halt;
		}
		const grund_t *gr = welt->lookup( pos );
		if(  gr  &&  gr->get_depot()  ) {
			break;
		}
	}
	return halthandle_t();
}


// the line of this player with this id
static linehandle_t find_line_by_id(player_t *owner, uint16 id)
{
	vector_tpl<linehandle_t> lines;
	owner->simlinemgmt.get_lines( simline_t::line, &lines );
	FOR( vector_tpl<linehandle_t>, const l, lines ) {
		if(  l.get_id()==id  ) {
			return l;
		}
	}
	return linehandle_t();
}


// some entry of this line couples with the line with this id
static bool line_couples_with(linehandle_t l, uint16 id)
{
	FOR( minivec_tpl<schedule_entry_t>, const &e, l->get_schedule()->entries ) {
		if(  e.couple_line_id==id  ) {
			return true;
		}
	}
	return false;
}


// two neighbouring tiles connected by track of this waytype
static bool tiles_connected(koord3d a, koord3d b, waytype_t wt)
{
	karte_ptr_t welt;
	if(  a==b  ||  koord_distance( a.get_2d(), b.get_2d() )!=1  ) {
		return false;
	}
	grund_t *gr = welt->lookup( a );
	grund_t *to;
	return gr  &&  gr->get_neighbour( to, wt, ribi_type( a, b ) )  &&  to->get_pos()==b;
}


void convoi_t::get_train_tiles(vector_tpl<koord3d> &tiles) const
{
	tiles.clear();
	for(  uint8 i=anz_vehikel;  i-- > 0;  ) {
		const koord3d pos = fahr[i]->get_pos();
		if(  tiles.empty()  ||  tiles.back()!=pos  ) {
			tiles.append( pos );
		}
	}
}


bool convoi_t::can_couple_here(const convoi_t *P, const convoi_t *C)
{
	if(  P==C  ||  P->owner!=C->owner  ||  !P->line.is_bound()  ||  P->line==C->line  ) {
		return false;
	}
	if(  P->coupled_convoi.is_bound()  ||  C->coupled_convoi.is_bound()  ||  P->handover_to.is_bound()  ||  C->handover_to.is_bound()  ) {
		// one partner per train
		return false;
	}
	if(  P->anz_vehikel==0  ||  C->anz_vehikel==0  ||  P->schedule==NULL  ||  C->schedule==NULL  ||  P->schedule->empty()  ||  C->schedule->empty()  ) {
		return false;
	}
	if(  (uint32)P->anz_vehikel + C->anz_vehikel > 255  ) {
		// the vehicle count of a convoi is a uint8
		return false;
	}
	if(  !dynamic_cast<rail_vehicle_t *>(P->fahr[0])  ||  !dynamic_cast<rail_vehicle_t *>(C->fahr[0])  ||  P->fahr[0]->get_waytype()!=C->fahr[0]->get_waytype()  ) {
		return false;
	}
	const schedule_entry_t &entry = C->schedule->get_current_entry();
	if(  entry.couple_line_id!=P->line.get_id()  ) {
		return false;
	}
	const halthandle_t halt = haltestelle_t::get_halt( entry.pos, C->owner );
	if(  !halt.is_bound()  ||  halt!=haltestelle_t::get_halt( P->schedule->get_current_entry().pos, P->owner )  ) {
		return false;
	}
	// both go on to the same stop
	const halthandle_t next = next_stop_halt( C->schedule, C->schedule->get_current_stop(), C->owner );
	return next.is_bound()  &&  next==next_stop_halt( P->schedule, P->schedule->get_current_stop(), P->owner );
}


bool convoi_t::expects_partner(uint16 &max_wait, linehandle_t &partner_line, uint8 &partner_entry) const
{
	max_wait = 0;
	partner_line = linehandle_t();
	partner_entry = 255;
	if(  !welt->has_calendar()  ||  schedule==NULL  ||  schedule->empty()  ||  anz_vehikel==0  ||  coupled_convoi.is_bound()  ||  !dynamic_cast<rail_vehicle_t *>(fahr[0])  ) {
		return false;
	}
	const schedule_entry_t &entry = schedule->get_current_entry();
	const halthandle_t halt = haltestelle_t::get_halt( entry.pos, owner );
	if(  !halt.is_bound()  ) {
		return false;
	}
	const halthandle_t next = next_stop_halt( schedule, schedule->get_current_stop(), owner );
	if(  !next.is_bound()  ) {
		return false;
	}
	if(  entry.has_coupling()  ) {
		// we join a train of that line here
		linehandle_t l = find_line_by_id( owner, entry.couple_line_id );
		if(  l.is_bound()  &&  l!=line  &&  l->count_convoys()>0  &&  entry.couple_max_wait>0  ) {
			max_wait = entry.couple_max_wait;
			partner_line = l;
			return true;
		}
	}
	if(  line.is_bound()  ) {
		// a train of another line joins us here
		FOR( vector_tpl<linehandle_t>, const l, halt->registered_lines ) {
			if(  l==line  ||  l->count_convoys()==0  ) {
				continue;
			}
			const schedule_t *ls = l->get_schedule();
			for(  uint8 i=0;  i<ls->get_count();  i++  ) {
				const schedule_entry_t &e = ls->entries[i];
				if(  e.couple_line_id==line.get_id()  &&  e.couple_max_wait>0  &&  haltestelle_t::get_halt( e.pos, owner )==halt  &&  next_stop_halt( ls, i, owner )==next  ) {
					max_wait = e.couple_max_wait;
					partner_line = l;
					partner_entry = i;
					return true;
				}
			}
		}
	}
	return false;
}


convoihandle_t convoi_t::find_partner_at(halthandle_t halt, bool &standing) const
{
	standing = false;
	if(  !halt.is_bound()  ||  anz_vehikel==0  ||  schedule==NULL  ||  schedule->empty()  ||  coupled_convoi.is_bound()  ||  handover_to.is_bound()  ||  !dynamic_cast<rail_vehicle_t *>(fahr[0])  ) {
		return convoihandle_t();
	}
	const schedule_entry_t &entry = schedule->get_current_entry();
	if(  haltestelle_t::get_halt( entry.pos, owner )!=halt  ) {
		return convoihandle_t();
	}

	// the trains that may couple with us: those of the lines at this stop we join or that join us
	vector_tpl<convoihandle_t> candidates;
	FOR( vector_tpl<linehandle_t>, const l, halt->registered_lines ) {
		if(  l==line  ||  l->get_owner()!=owner  ) {
			continue;
		}
		if(  entry.couple_line_id==l.get_id()  ||  (line.is_bound()  &&  line_couples_with( l, line.get_id() ))  ) {
			FOR( vector_tpl<convoihandle_t>, const c, l->get_convoys() ) {
				candidates.append( c );
			}
		}
	}
	if(  line.is_bound()  ) {
		// trains without a line may join us too
		FOR( vector_tpl<convoihandle_t>, const c, halt->registered_convoys ) {
			if(  c.is_bound()  &&  !c->get_line().is_bound()  ) {
				candidates.append( c );
			}
		}
	}

	convoihandle_t best, heading;
	FOR( vector_tpl<convoihandle_t>, const other, candidates ) {
		const convoi_t *o = other.get_rep();
		if(  !can_couple_here( this, o )  &&  !can_couple_here( o, this )  ) {
			continue;
		}
		if(  o->state==LOADING  ) {
			// standing there: the one that came first
			if(  !best.is_bound()  ||  (sint32)(o->arrived_time - best->arrived_time) < 0  ) {
				best = other;
			}
		}
		else if(  (o->state==DRIVING  ||  (o->state>=WAITING_FOR_CLEARANCE  &&  o->state<=CAN_START_TWO_MONTHS  &&  o->state!=SELF_DESTRUCT))  &&  !o->route.empty()  ) {
			// on its way in: its route is reserved into this stop
			const grund_t *gr = welt->lookup( o->route.back() );
			const schiene_t *sch = gr ? obj_cast<schiene_t>( gr->get_weg( o->fahr[0]->get_waytype() ) ) : NULL;
			if(  sch  &&  sch->get_reserved_convoi()==other  &&  gr->get_halt()==halt  ) {
				heading = other;
			}
		}
	}
	if(  best.is_bound()  ) {
		standing = true;
		return best;
	}
	return heading;
}


bool convoi_t::cut_route_before_partner(uint16 start_index)
{
	if(  route.get_count()<2  ||  schedule==NULL  ||  schedule->empty()  ||  schedule_target!=koord3d::invalid  ||  anz_vehikel==0  ) {
		return false;
	}
	const halthandle_t halt = haltestelle_t::get_halt( schedule->get_current_entry().pos, owner );
	if(  !halt.is_bound()  ||  haltestelle_t::get_halt( route.back(), owner )!=halt  ) {
		return false;
	}
	bool standing;
	const convoihandle_t partner = find_partner_at( halt, standing );
	if(  !partner.is_bound()  ||  !standing  ) {
		return false;
	}
	const waytype_t wt = fahr[0]->get_waytype();
	const uint32 next_index = fahr[0]->get_route_index();
	for(  uint32 i=max((uint32)start_index,1u);  i<route.get_count();  i++  ) {
		const grund_t *gr = welt->lookup( route.at(i) );
		const schiene_t *sch = gr ? obj_cast<schiene_t>( gr->get_weg( wt ) ) : NULL;
		if(  sch  &&  sch->get_reserved_convoi()==partner  ) {
			if(  i < next_index  ) {
				return false;
			}
			// stop on the tile before, right behind our partner
			route.remove_koord_from( i-1 );
			return true;
		}
	}
	return false;
}


bool convoi_t::couple(convoihandle_t primary, convoihandle_t joining)
{
	convoi_t *P = primary.get_rep();
	convoi_t *C = joining.get_rep();

	// the tiles of both trains in a row, rear to front in the direction of the primary
	vector_tpl<koord3d> tp, tc, chain;
	P->get_train_tiles( tp );
	C->get_train_tiles( tc );
	const waytype_t wt = P->fahr[0]->get_waytype();
	if(  tiles_connected( tp.back(), tc.front(), wt )  ) {
		// the joining train stands ahead of the primary, facing the same way
		FOR( vector_tpl<koord3d>, const k, tp ) { chain.append( k ); }
		FOR( vector_tpl<koord3d>, const k, tc ) { chain.append( k ); }
	}
	else if(  tiles_connected( tp.back(), tc.back(), wt )  ) {
		// head to head
		FOR( vector_tpl<koord3d>, const k, tp ) { chain.append( k ); }
		for(  uint32 i=tc.get_count();  i-- > 0;  ) { chain.append( tc[i] ); }
	}
	else if(  tiles_connected( tp.front(), tc.back(), wt )  ) {
		// the joining train stands behind the primary, facing the same way
		FOR( vector_tpl<koord3d>, const k, tc ) { chain.append( k ); }
		FOR( vector_tpl<koord3d>, const k, tp ) { chain.append( k ); }
	}
	else if(  tiles_connected( tp.front(), tc.front(), wt )  ) {
		// tail to tail
		for(  uint32 i=tc.get_count();  i-- > 0;  ) { chain.append( tc[i] ); }
		FOR( vector_tpl<koord3d>, const k, tp ) { chain.append( k ); }
	}
	else {
		// not next to each other (another platform of the same stop): the joining train comes over to
		// the track behind the primary, on which it came in, if that is free and long enough
		uint32 length = 0;
		for(  uint8 i=0;  i<P->anz_vehikel;  i++  ) {
			length += P->fahr[i]->get_desc()->get_length();
		}
		for(  uint8 i=0;  i<C->anz_vehikel;  i++  ) {
			length += C->fahr[i]->get_desc()->get_length();
		}
		const uint32 needed = (length + CARUNITS_PER_TILE - 1) / CARUNITS_PER_TILE + 1;
		const uint32 back_index = P->back()->get_route_index();
		const uint32 front_index = P->fahr[0]->get_route_index();
		if(  back_index==0  ||  front_index==0  ||  front_index > P->route.get_count()  ||  back_index > front_index
			||  P->route.at(back_index-1)!=P->back()->get_pos()  ||  P->route.at(front_index-1)!=P->fahr[0]->get_pos()  ) {
			return false;
		}
		uint32 first = back_index-1;
		while(  front_index-first < needed  ) {
			if(  first==0  ) {
				// the way behind is too short
				return false;
			}
			first --;
			const grund_t *gr = welt->lookup( P->route.at(first) );
			const schiene_t *sch = gr ? obj_cast<schiene_t>( gr->get_weg( wt ) ) : NULL;
			if(  sch==NULL  ||  (sch->is_reserved()  &&  sch->get_reserved_convoi()!=primary)  ) {
				// taken by another train
				return false;
			}
		}
		for(  uint32 i=first;  i<front_index;  i++  ) {
			chain.append( P->route.at(i) );
		}
	}
	if(  chain.get_count() >= INVALID_INDEX  ) {
		return false;
	}

	// all their reservations go, the whole row is reserved for the primary below
	C->unreserve_route();
	P->unreserve_route();
	FOR( vector_tpl<koord3d>, const k, tc ) {
		if(  grund_t *gr = welt->lookup( k )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( wt ) )  ) {
				sch->unreserve( joining );
			}
		}
	}
	FOR( vector_tpl<koord3d>, const k, chain ) {
		if(  grund_t *gr = welt->lookup( k )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( wt ) )  ) {
				sch->unreserve( joining );
				sch->unreserve( primary );
			}
		}
	}

	// the vehicles of the joining train go to the end of the primary;
	// they stay in its list too, and their fixed costs and goods stay its own
	const sint32 own_fixed = P->sum_fixed_costs;
	const uint8 first = P->anz_vehikel;
	for(  uint8 i=0;  i<C->anz_vehikel;  i++  ) {
		vehicle_t *v = C->fahr[i];
		v->set_leading( false );
		v->set_last( false );
		v->set_convoi( NULL );
		P->add_vehikel( v );
	}
	P->sum_fixed_costs = own_fixed;
	P->coupled_first = first;
	P->coupled_convoi = joining;
	C->coupled_convoi = primary;
	P->recalc_catg_index();

	// the whole train anew on the row of tiles: primary first, then the joined train
	P->route.clear();
	FOR( vector_tpl<koord3d>, const k, chain ) {
		P->route.append( k );
	}
	P->lay_out_on_route();
	P->alte_richtung = P->fahr[0]->get_direction();
	P->recalc_traction( false );
	P->calc_loading();
	P->freight_info_resort = true;
	for(  uint32 i=0;  i<chain.get_count();  i++  ) {
		if(  grund_t *gr = welt->lookup( chain[i] )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( wt ) )  ) {
				sch->reserve( primary, ribi_type( chain[max(1u,i)-1u], chain[min(chain.get_count()-1u,i+1u)] ) );
			}
		}
	}
	P->next_reservation_index = 0;
	P->couple_wait_since = 0;
	P->clear_passing_hold();

	// the joined train rides along
	C->state = COUPLED;
	C->arrived_time = P->arrived_time;
	C->couple_wait_since = 0;
	C->couple_hold_slot = -1;
	C->running_late = false;
	C->late_slot = -1;
	C->route.clear();
	C->clear_passing_hold();
	C->freight_info_resort = true;
	C->wait_lock = 0;
	welt->sync.remove( C );

	DBG_MESSAGE( "convoi_t::couple()", "%s joined %s", C->get_name(), P->get_name() );
	return true;
}


bool convoi_t::follow_to_stop(halthandle_t halt)
{
	// our schedule goes to the entry of this stop, over waypoints only
	const uint8 count = schedule->get_count();
	uint8 idx = schedule->get_current_stop();
	for(  uint8 n=0;  n<count;  n++  ) {
		const koord3d pos = schedule->entries[idx].pos;
		const halthandle_t h = haltestelle_t::get_halt( pos, owner );
		if(  h==halt  ) {
			schedule->set_current_stop( idx );
			return true;
		}
		const grund_t *gr = welt->lookup( pos );
		if(  h.is_bound()  ||  (gr  &&  gr->get_depot())  ) {
			// another stop or a depot first
			return false;
		}
		idx = (idx+1) % count;
	}
	return false;
}


void convoi_t::uncouple_here()
{
	convoi_t *c = coupled_convoi.get_rep();

	// the joined train appears on the tiles of the whole train once we have left them
	get_train_tiles( c->uncouple_span );

	const sint32 own_fixed = sum_fixed_costs;
	while(  anz_vehikel > coupled_first  ) {
		vehicle_t *v = fahr[anz_vehikel-1];
		// off the map; the tile stays reserved for us until we hand it over
		v->set_last( false );
		v->set_leading( false );
		v->mark_image_dirty( v->get_image(), 0 );
		v->leave_tile();
		v->set_flag( obj_t::not_on_map );
		remove_vehikel_bei( anz_vehikel-1 );
		v->set_convoi( c );
	}
	sum_fixed_costs = own_fixed;
	coupled_convoi = convoihandle_t();
	recalc_catg_index();
	calc_loading();
	freight_info_resort = true;

	handover_to = c->self;
	c->coupled_convoi = self;
	c->state = UNCOUPLING;
	for(  uint8 i=0;  i<c->anz_vehikel;  i++  ) {
		c->fahr[i]->set_leading( false );
		c->fahr[i]->set_last( i+1==c->anz_vehikel );
	}
	c->calc_loading();
	c->freight_info_resort = true;
	c->wait_lock = 0;

	DBG_MESSAGE( "convoi_t::uncouple_here()", "%s leaves %s behind", get_name(), c->get_name() );
}


void convoi_t::handover_tile(koord3d pos)
{
	if(  !handover_to.is_bound()  ||  handover_to->state!=UNCOUPLING  ) {
		handover_to = convoihandle_t();
		return;
	}
	if(  handover_to->uncouple_span.is_contained( pos )  ) {
		if(  grund_t *gr = welt->lookup( pos )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( fahr[0]->get_waytype() ) )  ) {
				if(  !sch->is_reserved()  ) {
					sch->reserve( handover_to, ribi_t::none );
				}
			}
		}
	}
}


void convoi_t::step_uncoupling()
{
	convoi_t *P = coupled_convoi.is_bound() ? coupled_convoi.get_rep() : NULL;
	if(  P  &&  P->handover_to!=self  ) {
		// it gave us up
		P = NULL;
	}
	const waytype_t wt = fahr[0]->get_waytype();

	// take over the tiles, as far as the primary does not need them any more
	bool all_ours = true;
	FOR( vector_tpl<koord3d>, const k, uncouple_span ) {
		grund_t *gr = welt->lookup( k );
		schiene_t *sch = gr ? obj_cast<schiene_t>( gr->get_weg( wt ) ) : NULL;
		if(  sch==NULL  ) {
			continue;
		}
		const convoihandle_t holder = sch->get_reserved_convoi();
		if(  holder==self  ) {
			continue;
		}
		if(  !holder.is_bound()  ) {
			sch->reserve( self, ribi_t::none );
			continue;
		}
		if(  P  &&  holder==P->self  ) {
			// does the primary still stand there or will it drive through?
			bool needed = P->state==ROUTING_1  ||  P->state==LOADING  ||  P->state==EDIT_SCHEDULE  ||  P->state==NO_ROUTE;
			for(  uint8 i=0;  !needed  &&  i<P->anz_vehikel;  i++  ) {
				needed = P->fahr[i]->get_pos()==k;
			}
			if(  !needed  &&  P->anz_vehikel>0  ) {
				for(  uint32 idx=max(1,P->back()->get_route_index())-1;  !needed  &&  idx<P->route.get_count();  idx++  ) {
					needed = P->route.at(idx)==k;
				}
			}
			if(  !needed  ) {
				sch->unreserve( P->self );
				sch->reserve( self, ribi_t::none );
				continue;
			}
		}
		all_ours = false;
	}
	if(  !all_ours  ) {
		// the platform stays taken: warn once, the player may have to clear it
		if(  uncouple_since==0  ) {
			uncouple_since = max( 1u, welt->get_ticks() );
		}
		const sint64 limit = welt->has_calendar() ? welt->calendar_minutes_to_ticks( 30 ) : (sint64)(welt->ticks_per_world_month >> 3);
		if(  !uncouple_warned  &&  (sint64)(welt->get_ticks() - uncouple_since) > limit  ) {
			uncouple_warned = true;
			const halthandle_t halt = haltestelle_t::get_halt( uncouple_span.back(), owner );
			cbuffer_t buf;
			buf.printf( translator::translate("%s cannot continue after uncoupling at %s: its platform is still occupied."), get_name(), halt.is_bound() ? halt->get_name() : uncouple_span.back().get_str() );
			welt->get_message()->add_message( buf, uncouple_span.back().get_2d(), message_t::warnings, PLAYER_FLAG|get_owner()->get_player_nr(), IMG_EMPTY );
		}
		return;
	}
	uncouple_since = 0;
	uncouple_warned = false;

	// appear at the rear end of the tiles, facing as the whole train did
	route.clear();
	FOR( vector_tpl<koord3d>, const k, uncouple_span ) {
		route.append( k );
	}
	set_erstes_letztes();
	lay_out_on_route();
	for(  uint8 i=0;  i<anz_vehikel;  i++  ) {
		fahr[i]->last_stop_pos = fahr[i]->get_pos();
	}
	// keep only the tiles we stand on
	FOR( vector_tpl<koord3d>, const k, uncouple_span ) {
		if(  grund_t *gr = welt->lookup( k )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( wt ) )  ) {
				bool on_it = false;
				for(  uint8 i=0;  !on_it  &&  i<anz_vehikel;  i++  ) {
					on_it = fahr[i]->get_pos()==k;
				}
				if(  on_it  ) {
					sch->reserve( self, ribi_t::none );
				}
				else {
					sch->unreserve( self );
				}
			}
		}
	}
	if(  P  ) {
		P->handover_to = convoihandle_t();
	}
	coupled_convoi = convoihandle_t();
	uncouple_span.clear();
	alte_richtung = fahr[0]->get_direction();
	recalc_traction( false );
	calc_loading();
	next_reservation_index = 0;
	welt->sync.add( this );

	const halthandle_t halt = haltestelle_t::get_halt( fahr[0]->get_pos(), owner );
	if(  halt.is_bound()  &&  halt==haltestelle_t::get_halt( schedule->get_current_entry().pos, owner )  ) {
		// load here as a train of its own
		state = LOADING;
		arrived_time = welt->get_ticks();
	}
	else {
		state = ROUTING_1;
	}
	wait_lock = 0;
	DBG_MESSAGE( "convoi_t::step_uncoupling()", "%s is back on the map", get_name() );
}


void convoi_t::detach_coupled_vehicles()
{
	if(  !is_coupled_primary()  ) {
		return;
	}
	convoi_t *c = coupled_convoi.get_rep();
	const sint32 own_fixed = sum_fixed_costs;
	while(  anz_vehikel > coupled_first  ) {
		vehicle_t *v = fahr[anz_vehikel-1];
		v->set_leading( false );
		remove_vehikel_bei( anz_vehikel-1 );
		v->set_convoi( c );
	}
	sum_fixed_costs = own_fixed;
	coupled_convoi = convoihandle_t();
	c->coupled_convoi = convoihandle_t();
	recalc_catg_index();
	calc_loading();
	c->set_erstes_letztes();
	c->recalc_traction( false );
	c->calc_loading();
	freight_info_resort = true;
	c->freight_info_resort = true;
}


void convoi_t::release_coupled_in_place()
{
	if(  !is_coupled_primary()  ) {
		return;
	}
	convoi_t *c = coupled_convoi.get_rep();
	detach_coupled_vehicles();
	// it stands where it is and finds its way on
	const waytype_t wt = c->fahr[0]->get_waytype();
	for(  uint8 i=0;  i<c->anz_vehikel;  i++  ) {
		if(  grund_t *gr = welt->lookup( c->fahr[i]->get_pos() )  ) {
			if(  schiene_t *sch = obj_cast<schiene_t>( gr->get_weg( wt ) )  ) {
				sch->unreserve( self );
				sch->reserve( c->self, ribi_t::none );
			}
		}
	}
	c->route.clear();
	c->alte_richtung = c->fahr[0]->get_direction();
	c->state = ROUTING_1;
	c->wait_lock = 0;
	welt->sync.add( c );
}


const char* convoi_t::send_to_depot(bool local)
{
	if(  state==COUPLED  ||  state==UNCOUPLING  ) {
		// fork, coupling: the primary decides where the vehicles go
		return "Not allowed while coupled to another train";
	}
	// iterate over all depots and try to find shortest route
	route_t *shortest_route = new route_t();
	route_t *route = new route_t();
	koord3d home = koord3d::invalid;
	vehicle_t *v = front();
	FOR(slist_tpl<depot_t*>, const depot, depot_t::get_depot_list()) {
		if (depot->get_waytype() != v->get_desc()->get_waytype()  ||  depot->get_owner() != get_owner()) {
			continue;
		}
		koord3d pos = depot->get_pos();

		if(!shortest_route->empty()  &&  koord_distance(pos, get_pos()) >= shortest_route->get_count()-1) {
			// the current route is already shorter, no need to search further
			continue;
		}
		if (v->calc_route(get_pos(), pos, 50, route)) { // do not care about speed
			if(  route->get_count() < shortest_route->get_count()  ||  shortest_route->empty()  ) {
				// just swap the pointers
				sim::swap(shortest_route, route);
				home = pos;
			}
		}
	}
	delete route;
	DBG_MESSAGE("shortest route has ", "%i hops", shortest_route->get_count()-1);

	if (local) {
		if (convoi_info_t *info = dynamic_cast<convoi_info_t*>(win_get_magic( magic_convoi_info+self.get_id()))) {
			info->route_search_finished();
		}
	}
	// if route to a depot has been found, update the convoi's schedule
	const char *txt;
	if(  !shortest_route->empty()  ) {
		schedule_t *schedule = get_schedule()->copy();
		schedule->insert(welt->lookup(home));
		schedule->set_current_stop( (schedule->get_current_stop()+schedule->get_count()-1)%schedule->get_count() );
		set_schedule(schedule);
		txt = "Convoi has been sent\nto the nearest depot\nof appropriate type.\n";
	}
	else {
		txt = "Home depot not found!\nYou need to send the\nconvoi to the depot\nmanually.";
	}
	delete shortest_route;

	return txt;
}
