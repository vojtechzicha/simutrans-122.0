/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMCONVOI_H
#define SIMCONVOI_H


#include "simtypes.h"
#include "simunits.h"
#include "simcolor.h"
#include "linehandle_t.h"

#include "ifc/sync_steppable.h"

#include "dataobj/route.h"
#include "vehicle/overtaker.h"
#include "tpl/array_tpl.h"
#include "tpl/minivec_tpl.h"
#include "tpl/vector_tpl.h"

#include "convoihandle_t.h"
#include "halthandle_t.h"

#define MAX_MONTHS               12 // Max history

class weg_t;
class depot_t;
class karte_ptr_t;
class player_t;
class vehicle_t;
class vehicle_desc_t;
class schedule_t;
class cbuffer_t;

/**
 * Base class for all vehicle consists. Convoys can be referenced by handles, see halthandle_t.
 */
class convoi_t : public sync_steppable, public overtaker_t
{
public:
	enum {
		CONVOI_CAPACITY = 0,       // the amount of ware that could be transported, theoretically
		CONVOI_TRANSPORTED_GOODS,  // the amount of ware that has been transported
		CONVOI_REVENUE,            // the income this CONVOI generated
		CONVOI_OPERATIONS,         // the cost of operations this CONVOI generated
		CONVOI_PROFIT,             // total profit of this convoi
		CONVOI_DISTANCE,           // total distance traveled this month
		CONVOI_MAXSPEED,           // average max. possible speed
		CONVOI_WAYTOLL,
		MAX_CONVOI_COST            // Total number of cost items
	};

	/** Constants */
	enum { default_vehicle_length=4};

	enum states {INITIAL,
		EDIT_SCHEDULE,
		ROUTING_1,
		DUMMY4,
		DUMMY5,
		NO_ROUTE,
		DRIVING,
		LOADING,
		WAITING_FOR_CLEARANCE,
		WAITING_FOR_CLEARANCE_ONE_MONTH,
		CAN_START,
		CAN_START_ONE_MONTH,
		SELF_DESTRUCT,
		WAITING_FOR_CLEARANCE_TWO_MONTHS,
		CAN_START_TWO_MONTHS,
		LEAVING_DEPOT,
		ENTERING_DEPOT,
		COUPLED,        ///< fork: joined to a primary train, which drives its vehicles (see coupled_convoi)
		UNCOUPLING,     ///< fork: just uncoupled, off the map until the primary has left the platform
		MAX_STATES
	};

private:
	/* The data is laid out such that the most important variables for sync_step and step are
	 * concentrated at the beginning of the structure.
	 * All computations are for 64bit builds.
	 *
	 * We start with a 24 bytes header from virtual tables and data of overtaker_t :(
	 */

	/**
	 * The convoi is not processed every sync step for various actions
	 * (like waiting before signals, loading etc.) Such action will only
	 * continue after a waiting time larger than wait_lock
	 */
	sint32 wait_lock;

	states state;
	// 32 bytes (state is int is 4 byte)

	/**
	* holds id of line with pending update
	* -1 if no pending update
	*/
	linehandle_t line_update_pending;

	uint16 recalc_data_front  : 1; ///< true, when front vehicle has to recalculate braking
	uint16 recalc_data        : 1; ///< true, when convoy has to recalculate weights and speed limits
	uint16 recalc_speed_limit : 1; ///< true, when convoy has to recalculate speed limits

	uint16 previous_delta_v   :12; /// 12 bit! // Stores the previous delta_v value; otherwise these digits are lost during calculation and vehicle do not accelerate
	// 36 bytes
	/**
	 * Overall performance with Gear.
	 * Used in movement calculations.
	 */
	sint32 sum_gear_and_power;

	// 40 bytes
	/**
	 * sum_weight: unloaded weight of all vehicles
	 * sum_gesamtweight: total weight of all vehicles
	 * Not stored, but calculated from individual weights
	 * when loading/driving.
	 */
	sint64 sum_gesamtweight;
	sint64 sum_friction_weight;
	// 56 bytes
	sint32 akt_speed_soll;    // target speed
	sint32 akt_speed;         // current speed
	// 64 bytes

	/**
	 * this give the index of the next signal or the end of the route
	 * convois will slow down before it, if this is not a waypoint or the cannot pass
	 * The slowdown is done by the vehicle routines
	 */
	uint16 next_stop_index;

	sint32 speed_limit;
	// needed for speed control/calculation
	sint32 brake_speed_soll;    // brake target speed
	/**
	 * Lowest top speed of all vehicles. Doesn't get saved, but calculated
	 * from the vehicles data
	 */
	sint32 min_top_speed;

	sint32 sp_soll;           // steps to go
	sint32 max_record_speed; // current convois fastest speed ever

	// things for the world record
	koord record_pos;

	/* Number of steps the current convoi did already
	 * (only needed for leaving/entering depot)
	 */
	sint16 steps_driven;
	/**
	 * The vehicles of this convoi
	 *
	 */
	array_tpl<vehicle_t*> fahr;
	/**
	 * Number of vehicles in this convoi.
	 */
	// TODO number of vehicles is stored in array_tpl too!
	uint8 anz_vehikel;

	uint32 next_wolke; // time to next smoke

	/**
	 * Route of this convoi - a sequence of coordinates. Actually
	 * the path of the first vehicle
	 */
	route_t route;

	/**
	 * assigned line
	 */
	linehandle_t line;

	/**
	* All vehicle-schedule pointers point here
	*/
	schedule_t *schedule;

	koord3d schedule_target;

	/**
	* loading_level was minimum_loading before. Actual percentage loaded for loadable vehicles (station length!).
	* needed as int, since used by the gui
	*/
	sint32 loading_level;

	/**
	* At which loading level is the train allowed to start? 0 during driving.
	* needed as int, since used by the gui
	*/
	sint32 loading_limit;

	/*
	 * a list of all catg_index, which can be transported by this convoy.
	 */
	minivec_tpl<uint8> goods_catg_index;

	/**
	* Convoi owner
	*/
	player_t *owner;

	/**
	* Current map
	*/
	static karte_ptr_t welt;

	/**
	* the convoi is being withdrawn from service
	*/
	bool withdraw;

	/**
	* nothing will be loaded onto this convoi
	*/
	bool no_load;

	/**
	 * Fork, rail: marked as Hold, the convoi may stop at stations it passes to let a passing train
	 * go by (see rail_vehicle_t::is_choose_signal_clear); a line can set it for all its convois
	 */
	bool hold_marker;

	/**
	 * Fork, rail: on the way to a platform off the schedule to let a passing train go by; arriving
	 * there is no stop of the schedule, the convoi only waits and then goes on to its next stop
	 */
	bool hold_divert;

	/**
	 * Fork, rail: waiting at a stop for this passing train to go by since passing_hold_since (ticks),
	 * see rail_vehicle_t::is_held_for_passing_train(); passing_hold_released: done waiting at this
	 * stop (time limit or a train stuck at the entry signal). Not saved.
	 */
	convoihandle_t passing_hold_for;
	uint32 passing_hold_since;
	bool passing_hold_released;

	/**
	 * Fork, rail: the track claimed at the next station of a single-track section, see
	 * rail_vehicle_t::is_platform_signal_clear(). claim_path runs from the station boundary through
	 * the claimed track, to the stop (claim_stops) or to the platform signal at its far end; its tiles
	 * from claim_first on are reserved for this convoi. Empty: no claim. Saved (122.7).
	 */
	vector_tpl<koord3d> claim_path;
	uint16 claim_first;
	bool claim_stops;
	// the schedule stop whose way enters that station (the claim is out of date without it)
	koord3d claim_stop;

	// fork, rail: why the convoi waits at a platform signal or station boundary (not saved)
	uint8 section_wait;
	halthandle_t section_wait_halt;
	// since this tick (0 = not waiting); warned once that the stations are locked
	uint32 section_wait_since;
	bool section_lock_warned;

	/**
	 * Fork, coupling (rail). A primary train carries the vehicles of the train that joined it at
	 * the end of fahr, from index coupled_first on, and coupled_convoi is that train. The joined
	 * train is in state COUPLED: its fahr points to the same vehicles (they belong to the primary
	 * while coupled, see get_vehicle_owner) and its coupled_convoi is the primary.
	 * A train in state UNCOUPLING has just been uncoupled: its vehicles are off the map until the
	 * primary (its coupled_convoi) has left uncouple_span (tiles, rear to front), then it appears
	 * there. The primary hands over the span tiles as its last vehicle leaves them (handover_to).
	 */
	convoihandle_t coupled_convoi;
	uint8 coupled_first;
	convoihandle_t handover_to;
	vector_tpl<koord3d> uncouple_span;

	/**
	 * Fork, coupling: waiting at a stop for the partner since this tick (0 = not waiting), and the
	 * timetable slot held meanwhile (-1 = none), so a late departure keeps it.
	 */
	uint32 couple_wait_since;
	sint64 couple_hold_slot;

	/**
	 * Fork, coupling: running late after the primary left without us; late_slot is the slot
	 * inherited for the current stop (-1 = none), see simline_t::get_late_departure_slot.
	 */
	bool running_late;
	sint64 late_slot;

	/// Fork, coupling: UNCOUPLING since this tick; warned once that the platform stays taken. Not saved.
	uint32 uncouple_since;
	bool uncouple_warned;

	/**
	* the convoi caches its freight info; it is only recalculation after loading or resorting
	*/
	bool freight_info_resort;

	// true, if at least one vehicle of a convoi is obsolete
	bool has_obsolete;

	// true, if all engines require catenary (fork: an electric engine together with another engine does not)
	bool is_electric;

	/**
	 * Fork, mixed traction: the convoy has electric engines and other engines (diesel, steam, ...).
	 * The electric ones pull only under wires, the others off wires, and under wires only when that
	 * gives a higher loaded top speed (traction_both_under_wire, chosen at departure).
	 * An idle engine adds neither power, running cost nor its top speed limit. Not saved.
	 */
	bool traction_mixed;
	bool traction_off_wire;        ///< at least one electric engine is on a tile without catenary
	bool traction_both_under_wire; ///< under wires the other engines pull too
	sint32 traction_top_speed_under_wire; ///< top speed limit of the engines that pull under wires
	sint32 traction_top_speed_off_wire;   ///< top speed limit of the engines that pull off wires
	sint32 traction_power_speed_under_wire; ///< speed reachable under wires with the load at departure
	sint32 traction_power_speed_off_wire;   ///< speed reachable off wires with the load at departure

	/**
	* the convoi caches its freight info; it is only recalculation after loading or resorting
	*/
	uint8 freight_info_order;

	/*
	 * caches the running costs (fork: only of the vehicles that are not idle engines)
	 */
	sint32 sum_running_costs;
	sint32 sum_fixed_costs;

	/**
	* Overall performance.
	* Not used in movement code.
	*/
	uint32 sum_power;

	/// sum_weight: unloaded weight of all vehicles
	sint64 sum_weight;

	/**
	 * this give the index until which the route has been reserved. It is used for
	 * restoring reservations after loading a game.
	 */
	uint16 next_reservation_index;

	/**
	 * Time when convoi arrived at the current stop
	 * Used to calculate when it should depart due to the 'month wait time'
	 */
	uint32 arrived_time;

	/**
	* accumulated profit over a year
	*/
	sint64 jahresgewinn;

	/* the odometer */
	sint64 total_distance_traveled;

	uint32 distance_since_last_stop; // number of tiles entered since last stop
	uint32 sum_speed_limit; // sum of the speed limits encountered since the last stop

	sint32 speedbonus_kmh; // speed used for speedbonus calculation in km/h
	sint32 maxspeed_average_count; // just a simple count to average for statistics


	ribi_t::ribi alte_richtung;

	/**
	 * struct holds new financial history for convoi
	 */
	sint64 financial_history[MAX_MONTHS][MAX_CONVOI_COST];


	/**
	 * the koordinate of the home depot of this convoi
	 * the last depot visited is considered being the home depot
	 */
	koord3d home_depot;

	/**
	 * Name of the convoi.
	 * @see set_name
	 */
	uint8 name_offset;
	char name_and_id[128];

	/**
	* Initialize all variables with default values.
	* Each constructor must call this method first!
	*/
	void init(player_t *player);

	/**
	* Calculate route from Start to Target Coordinate
	*/
	bool drive_to();

	/**
	* Setup vehicles for moving in same direction than before
	* if the direction is the same as before
	*/
	bool can_go_alte_richtung();

	/**
	 * remove all track reservations (trains only)
	 */
	void unreserve_route();

	// reserve route until next_reservation_index
	void reserve_route();

	/**
	* Mark first and last vehicle.
	*/
	void set_erstes_letztes();

	// returns the index of the vehikel at position length (16=1 tile)
	int get_vehicle_at_length(uint16);

	/**
	* calculate income for last hop
	* only used for entering depot or recalculating routes when a schedule window is opened
	*/
	void calc_gewinn();

	/**
	* Recalculates loading level and limit.
	* While driving loading_limit will be set to 0.
	*/
	void calc_loading();

	/* Calculates (and sets) akt_speed
	 * needed for driving, entering and leaving a depot)
	 */
	void calc_acceleration(uint32 delta_t);

	/**
	* initialize the financial history
	*/
	void init_financial_history();

	/**
	* unset line -> remove cnv from line
	*/
	void unset_line();

	// matches two halts; if the pos is not identical, maybe the halt still is
	bool matches_halt( const koord3d pos1, const koord3d pos2 );

	/**
	 * Register the convoy with the stops in the schedule
	 */
	void register_stops();

	/**
	 * Unregister the convoy from the stops in the schedule
	 */
	void unregister_stops();

	uint32 move_to(uint16 start_index);

	/**
	 * Puts the whole train on the start of its route and drives it on until it is completely on
	 * the track (the vehicles are placed without hopping). Returns true if the route was too
	 * short for that, i.e. the train is at its destination already.
	 */
	bool lay_out_on_route();

	/// fork, coupling: the tiles under the vehicles, rear to front
	void get_train_tiles(vector_tpl<koord3d> &tiles) const;

	/// fork, coupling: joins the train C to the primary P standing next to it at a stop
	static bool couple(convoihandle_t P, convoihandle_t C);

	/// fork, coupling: at a stop where the schedules part, the joined train stays behind
	void uncouple_here();

	/// fork, coupling: while UNCOUPLING, takes the span tiles over and appears when all are ours
	void step_uncoupling();

	/// fork, coupling: the joined train takes its vehicles back where they stand (primary removed)
	void release_coupled_in_place();

	/// fork, coupling: a primary gives the joined train its vehicles back (they stay where they are)
	void detach_coupled_vehicles();

	/// fork, coupling: the joined train's schedule follows the primary to this stop; false if it does not stop here
	bool follow_to_stop(halthandle_t halt);

public:
	/**
	* Convoi haelt an Haltestelle und setzt quote fuer Fracht
	*/
	void hat_gehalten(halthandle_t halt);

	const route_t* get_route() const { return &route; }
	route_t* access_route() { return &route; }

	const koord3d get_schedule_target() const { return schedule_target; }
	void set_schedule_target( koord3d t ) { schedule_target = t; }

	/**
	* get line
	*/
	linehandle_t get_line() const {return line;}

	/// tick of the arrival at the current stop (valid while loading)
	uint32 get_arrived_time() const { return arrived_time; }

	/**
	 * loading rules satisfied: the minimum load is reached, the maximum waiting time is over,
	 * or the convoy must not load at all. Without a timetable this is the departure condition.
	 */
	bool is_ready_to_depart() const;

	/**
	 * Fork: the calendar minute this convoy is expected to leave its current stop, while loading.
	 * With a timetable it is the planned slot; otherwise the end of the maximum waiting time.
	 * latest is set when the convoy is still waiting for its minimum load, so it may leave earlier.
	 * False when nothing is known (no calendar, ready without a timetable, no maximum wait).
	 */
	bool get_planned_departure(sint64 &minutes, bool &latest) const;

	/* true, if electrification needed for this convoi */
	bool needs_electrification() const { return is_electric; }

	/// fork: true, if the convoy has electric and other engines (mixed traction)
	bool has_mixed_traction() const { return traction_mixed; }

	/// fork: true, if electric engines of this convoy are pulling now (for the electricity toll)
	bool draws_electricity() const { return is_electric  ||  (traction_mixed  &&  !traction_off_wire); }

	/// fork, mixed traction: true, if the other engines pull under wires as well
	bool get_traction_both_under_wire() const { return traction_both_under_wire; }

	/// fork, mixed traction: true, if an electric engine is off wires, so only the other engines pull
	bool is_traction_off_wire() const { return traction_off_wire; }

	/**
	 * fork: top speed limit of the engines that pull on a tile with (@p electrified) or without
	 * catenary; for route checks like minimum speed signs. min_top_speed for other convoys.
	 */
	sint32 get_traction_top_speed(bool electrified) const
	{
		return traction_mixed ? (electrified ? traction_top_speed_under_wire : traction_top_speed_off_wire) : min_top_speed;
	}

	/// fork: power (kW, without gear) of the engines that pull now
	uint32 get_active_power() const;

	/**
	 * Fork: recalculates which engines pull (power, running costs, top speed) from the tiles of the
	 * electric engines. With @p choose also decides whether the other engines pull under wires,
	 * from the current weight. Called when the convoy changes, departs, and when an electric engine
	 * of a mixed traction convoy enters or leaves catenary.
	 */
	void recalc_traction(bool choose);

	/**
	 * Fork: the power (times gear) and the top speed of the engines that pull in a mode, plus the
	 * unpowered vehicles. For a convoy without mixed traction everything counts in every mode.
	 */
	void calc_traction_sums(bool off_wire, bool both_under_wire, sint32 &gear_and_power, sint32 &top_speed, sint32 &running_costs) const;

	/**
	 * Fork: top speed at @p total_weight under wires (the better choice of engines) or off wires.
	 * Same as calc_max_speed() with all engines for a convoy without mixed traction.
	 */
	sint32 calc_traction_max_speed(uint64 total_weight, bool off_wire) const;

	/// fork: plays the sound of the first engine that pulls (stock: of the front vehicle)
	void play_start_sound() const;

	/**
	* set line
	*/
	void set_line(linehandle_t );

	// updates a line schedule and tries to find the best next station to go
	void check_pending_updates();

	// true if this is a waypoint
	bool is_waypoint( koord3d ) const;

	/* changes the state of a convoi via tool_t; mandatory for networkmode!
	 * for list of commands and parameter see tool_t::tool_change_convoi_t
	 */
	void call_convoi_tool( const char function, const char *extra ) const;

	/**
	 * set state: only use by tool_t::tool_change_convoi_t
	 */
	void set_state( uint16 new_state ) { assert(new_state<MAX_STATES); state = (states)new_state; }

	/**
	* get state
	*/
	int get_state() const { return state; }

	/**
	* true if in waiting state (maybe also due to starting)
	*/
	bool is_waiting() { return (state>=WAITING_FOR_CLEARANCE  &&  state<=CAN_START_TWO_MONTHS)  &&  state!=SELF_DESTRUCT; }

	/**
	* reset state to no error message
	*/
	void reset_waiting() { state=WAITING_FOR_CLEARANCE; }

	/**
	* The handle for ourselves. In Anlehnung an 'this' aber mit
	* allen checks beim Zugriff.
	*/
	convoihandle_t self;

	/**
	 * The profit in this year
	 */
	const sint64 & get_jahresgewinn() const {return jahresgewinn;}

	const sint64 & get_total_distance_traveled() const { return total_distance_traveled; }

	/**
	 * @return the total monthly fix cost for all vehicles in convoi
	 */
	sint64 get_fixed_cost() const { return -sum_fixed_costs; }

	/**
	 * returns the total running cost for all vehicles in convoi
	 */
	sint32 get_running_cost() const { return -sum_running_costs; }

	/**
	 * returns the total new purchase cost for all vehicles in convoy
	 */
	sint64 get_purchase_cost() const;

	/**
	* Constructor for loading from file,
	*/
	convoi_t(loadsave_t *file);

	convoi_t(player_t* player);

	virtual ~convoi_t();

	/**
	* Load or save this convoi data
	*/
	void rdwr(loadsave_t *file);

	/**
	 * method to load/save convoihandle_t
	 */
	static void rdwr_convoihandle_t(loadsave_t *file, convoihandle_t &cnv);

	void finish_rd();

	void rotate90( const sint16 y_size );

	/**
	* Called if a vehicle enters a depot
	*/
	void betrete_depot(depot_t *dep);

	/**
	* Return the internal name of the convois
	* @return Name of the convois
	*/
	const char *get_internal_name() const {return name_and_id+name_offset;}

	/**
	* Allows editing ...
	* @return Name of the Convois
	*/
	char *access_internal_name() {return name_and_id+name_offset;}

	/**
	* Return the name of the convois
	* @return Name of the convois
	*/
	const char *get_name() const {return name_and_id;}

	/**
	* Sets the name. Copies name into this->name and translates it.
	*/
	void set_name(const char *name, bool with_new_id = true);

	/**
	 * Return the position of the convois.
	 * @return Position of the convois
	 */
	koord3d get_pos() const;

	/**
	 * @return current speed, this might be different from topspeed
	 *         actual currently set speed.
	 */
	const sint32& get_akt_speed() const { return akt_speed; }

	/**
	 * @return total power of this convoi
	 */
	const uint32 & get_sum_power() const {return sum_power;}
	const sint32 & get_min_top_speed() const {return min_top_speed;}
	const sint32 & get_speed_limit() const {return speed_limit;}

	void set_speed_limit(sint32 s) { speed_limit = s;}

	/// @returns weight of the convoy's vehicles (excluding freight)
	const sint64 & get_sum_weight() const {return sum_weight;}

	/// @returns weight of convoy including freight
	const sint64 & get_sum_gesamtweight() const {return sum_gesamtweight;}

	/// changes sum_friction_weight, called when vehicle changed tile and friction changes as well.
	void update_friction_weight(sint64 delta_friction_weight) { sum_friction_weight += delta_friction_weight; }

	/// @returns theoretical max speed of a convoy with given @p total_power and @p total_weight
	static sint32 calc_max_speed(uint64 total_power, uint64 total_weight, sint32 speed_limit);

	uint32 get_length() const;

	/**
	 * @return length of convoi in the correct units for movement
	 */
	uint32 get_length_in_steps() const { return get_length() * VEHICLE_STEPS_PER_CARUNIT; }

	/**
	 * Add the costs for travelling one tile
	 */
	void add_running_cost( const weg_t *weg );

	/// fork, coupling: add_running_cost for our own vehicles (sum_running_costs)
	void add_running_cost_own( const weg_t *weg );

	/**
	 * moving the vehicles of a convoi and acceleration/deceleration
	 * all other stuff => convoi_t::step()
	 */
	sync_result sync_step(uint32 delta_t) OVERRIDE;

	/**
	 * All things like route search or loading, that may take a little
	 */
	void step();

	/**
	* sets a new convoi in route
	*/
	void start();

	void ziel_erreicht(); ///< Called, when the first vehicle reaches the target

	/**
	* When a vehicle has detected a problem
	* force calculate a new route
	*/
	void suche_neue_route();

	/**
	* Wait until vehicle 0 reports free route
	* will be called during a hop_check, if the road/track is blocked
	*/
	void warten_bis_weg_frei(sint32 restart_speed);

	/**
	* @return Vehicle count
	*/
	uint8 get_vehicle_count() const { return anz_vehikel; }

	/**
	 * @return Vehicle at position i
	 */
	vehicle_t* get_vehikel(uint16 i) const { return fahr[i]; }

	vehicle_t* front() const { return fahr[0]; }

	vehicle_t* back() const { return fahr[anz_vehikel - 1]; }

	/**
	* Adds a vehicle at the start or end of the convoi.
	*/
	bool add_vehikel(vehicle_t *v, bool infront = false);

	/**
	* Removes vehicles at position i
	*/
	vehicle_t * remove_vehikel_bei(unsigned short i);

	const minivec_tpl<uint8> &get_goods_catg_index() const { return goods_catg_index; }

	// recalculates the good transported by this convoy and (in case of changes) will start schedule recalculation
	void recalc_catg_index();

	/**
	* Sets a schedule
	*/
	bool set_schedule(schedule_t *f);

	schedule_t* get_schedule() const { return schedule; }

	/**
	* Creates a new schedule if there isn't one already.
	*/
	schedule_t * create_schedule();

	// remove wrong freight when schedule changes etc.
	void check_freight();

	/**
	* @return Owner of this convoi
	*/
	player_t * get_owner() const { return owner; }

	/**
	* Opens an information window
	* @see simwin
	*/
	void open_info_window();

	/**
	* @return a description string for the object, der z.B. in einem
	* Beobachtungsfenster angezeigt wird.
	* @see simwin
	*/
	void info(cbuffer_t & buf) const;

	/**
	* @param buf the buffer to fill
	* @return Freight description text (buf)
	*/
	void get_freight_info(cbuffer_t & buf);

	/// fork: builds the freight list into buf now, without the cache of get_freight_info()
	void build_freight_info(cbuffer_t & buf, uint8 sort_order) const;
	void set_sortby(uint8 order);
	uint8 get_sortby() const { return freight_info_order; }

	/**
	* Opens the schedule window
	* @see simwin
	*/
	void open_schedule_window( bool show );

	/**
	* pruefe ob Beschraenkungen fuer alle Fahrzeuge erfuellt sind
	*/
	bool pruefe_alle();

	/**
	* Control loading and unloading
	*/
	void laden();

	/**
	* Setup vehicles before starting to move
	*/
	void vorfahren();

	/**
	* Calculate the total value of the convoi as the sum of all vehicle values.
	*/
	sint64 calc_restwert() const;

	/**
	* Check if this convoi has entered a depot.
	*/
	bool in_depot() const { return state == INITIAL; }

	/**
	* loading_level was minimum_loading before. Actual percentage loaded of loadable
	* vehicles.
	*/
	const sint32 &get_loading_level() const { return loading_level; }

	/**
	* At which loading level is the train allowed to start? 0 during driving.
	*/
	const sint32 &get_loading_limit() const { return loading_limit; }

	/**
	* Schedule convois for self destruction. Will be executed
	* upon next sync step
	*/
	void self_destruct();

	/**
	* Helper method to remove convois from the map that cannot
	* removed normally (i.e. by sending to a depot) anymore.
	* This is a workaround for bugs in the game.
	*/
	void destroy();

	/**
	* Debug info to stderr
	*/
	void dump() const;

	/**
	* book a certain amount into the convois financial history
	* is called from vehicle during un/load
	*/
	void book(sint64 amount, int cost_type);

	/**
	* return a pointer to the financial history
	*/
	sint64* get_finance_history() { return *financial_history; }

	/**
	* return a specified element from the financial history
	*/
	sint64 get_finance_history(int month, int cost_type) const { return financial_history[month][cost_type]; }
	sint64 get_stat_converted(int month, int cost_type) const;

	/**
	* only purpose currently is to roll financial history
	*/
	void new_month();

	/**
	 * Method for yearly action
	 */
	void new_year();

	void set_update_line(linehandle_t l) { line_update_pending = l; }

	void set_home_depot(koord3d hd) { home_depot = hd; }

	koord3d get_home_depot() { return home_depot; }

	/**
	 * Sends convoi to nearest depot.
	 * Has to be called synchronously on all clients in networkmode!
	 * @returns success message
	 */
	const char* send_to_depot(bool local);

	/**
	 * this give the index of the next signal or the end of the route
	 * convois will slow down before it, if this is not a waypoint or the cannot pass
	 * The slowdown is done by the vehicle routines
	 */
	uint16 get_next_stop_index() const {return next_stop_index;}
	void set_next_stop_index(uint16 n);

	/* including this route_index, the route was reserved the last time
	 * currently only used for tracks
	 */
	uint16 get_next_reservation_index() const {return next_reservation_index;}
	void set_next_reservation_index(uint16 n);

	/* the current state of the convoi */
	PIXVAL get_status_color() const;

	// returns tiles needed for this convoi
	uint16 get_tile_length() const;

	bool has_obsolete_vehicles() const { return has_obsolete; }

	bool get_withdraw() const { return withdraw; }

	void set_withdraw(bool new_withdraw);

	bool get_no_load() const { return no_load; }

	void set_no_load(bool new_no_load) { no_load = new_no_load; }

	void must_recalc_data() { recalc_data = true; }
	void must_recalc_data_front() { recalc_data_front = true; }
	void must_recalc_speed_limit() { recalc_speed_limit = true; }

	// calculates the speed used for the speedbonus base, and the max achievable speed at current power/weight for overtakers
	void calc_speedbonus_kmh();
	sint32 get_speedbonus_kmh() const;

	// just a guess of the speed
	uint32 get_average_kmh() const;

	// Overtaking for convois
	bool can_overtake(overtaker_t *other_overtaker, sint32 other_speed, sint16 steps_other) OVERRIDE;

	// standing at a stop (loading, or finding its route before leaving): road traffic may pass it
	bool is_standing() const { return state==LOADING  ||  state==ROUTING_1  ||  state==NO_ROUTE; }

	// fork, coupling (see coupled_convoi)
	bool is_coupled() const { return state==COUPLED; }
	bool is_coupled_primary() const { return coupled_convoi.is_bound()  &&  state!=COUPLED  &&  state!=UNCOUPLING; }
	convoihandle_t get_coupled_convoi() const { return coupled_convoi; }
	uint8 get_coupled_first() const { return coupled_first; }
	/// the convoi whose vehicle i is (the joined train for its part of a coupled primary)
	convoihandle_t get_vehicle_owner(uint8 i) const { return is_coupled_primary()  &&  i>=coupled_first ? coupled_convoi : self; }
	/// the number of the primary's own vehicles, at the start of fahr
	uint8 get_own_vehicle_count() const { return is_coupled_primary() ? coupled_first : anz_vehikel; }
	/// fork, coupling: the stop where a coupled primary will leave its joined train behind (unbound: none found)
	halthandle_t get_uncouple_halt() const;
	bool is_running_late() const { return running_late; }
	bool is_waiting_for_coupling() const { return couple_wait_since!=0; }
	/// fork, coupling: the primary hands the tile over to the train it just uncoupled
	void handover_tile(koord3d pos);

	/**
	 * Fork, coupling: does this train wait for a partner at its current stop? max_wait is the
	 * waiting time in calendar minutes; for a primary also the line and entry of the partner.
	 */
	bool expects_partner(uint16 &max_wait, linehandle_t &partner_line, uint8 &partner_entry) const;

	/**
	 * Fork, coupling: the partner at the stop this train heads for, standing there or with its
	 * route reserved into it (standing is set accordingly). Unbound if there is none.
	 */
	convoihandle_t find_partner_at(halthandle_t halt, bool &standing) const;

	/**
	 * Fork, coupling, called when reserving: cut our route before the first tile of our partner
	 * standing at our next stop, so we stop right behind it. Returns true if the route was cut.
	 */
	bool cut_route_before_partner(uint16 start_index);

	/// fork, coupling: can the train C join the primary P at the stop where both stand?
	static bool can_couple_here(const convoi_t *P, const convoi_t *C);

	// fork, rail: marked as Hold by itself or by its line
	bool get_hold_marker() const { return hold_marker; }
	void set_hold_marker(bool on) { hold_marker = on; }
	bool is_hold_marked() const;
	bool is_hold_divert() const { return hold_divert; }
	void set_hold_divert(bool on) { hold_divert = on; }

	// fork, rail: waiting at a stop for a passing train (see passing_hold_for)
	convoihandle_t get_passing_hold_for() const { return passing_hold_for; }
	uint32 get_passing_hold_since() const { return passing_hold_since; }
	bool is_passing_hold_released() const { return passing_hold_released; }
	void set_passing_hold(convoihandle_t for_cnv, uint32 since) { passing_hold_for = for_cnv; passing_hold_since = since; }
	void release_passing_hold() { passing_hold_for = convoihandle_t(); passing_hold_released = true; }
	void clear_passing_hold() { passing_hold_for = convoihandle_t(); passing_hold_released = false; }

	// fork, rail: track claimed at the next station of a single-track section (see claim_path)
	bool has_claim() const { return !claim_path.empty(); }
	koord3d get_claim_boundary() const { return claim_path.empty() ? koord3d::invalid : claim_path[0]; }
	bool is_claimed_tile(koord3d pos) const;
	// path: from the station boundary through the track; reserves its tiles from first on;
	// for_stop: the schedule stop whose way enters that station
	void set_claim(const route_t &path, uint16 first, bool stops, koord3d for_stop);
	// unreserve: free the claimed tiles (not those under this convoi)
	void release_claim(bool unreserve);
	// reserves the claimed tiles again (after loading)
	void reserve_claim();
	// the route from index from on goes through the claimed track (and on to its end when passing):
	// 1 done, 0 the route does not reach the station boundary, -1 the claimed track does not lead on
	sint8 route_via_claim(route_t &r, uint32 from);
	// true when route_via_claim would not change the route (no route search needed)
	bool route_follows_claim(const route_t &r, uint32 from) const;

	// SECTION_WAIT_LAST_TRACK: the last free track there is kept, taking it could lock the stations up
	enum { SECTION_WAIT_NONE = 0, SECTION_WAIT_LINE, SECTION_WAIT_TRACK, SECTION_WAIT_ENTRY, SECTION_WAIT_LAST_TRACK };
	void set_section_wait(uint8 why, halthandle_t halt);
	uint8 get_section_wait() const { return section_wait; }
	halthandle_t get_section_wait_halt() const { return section_wait_halt; }
	uint32 get_section_wait_since() const { return section_wait_since; }
	bool is_section_lock_warned() const { return section_lock_warned; }
	void set_section_lock_warned() { section_lock_warned = true; }

	/**
	 * Passing a standing convoi (see convoi_t::is_standing()), whose first tile is route tile start_index.
	 * @return tiles for set_tiles_passing_standing(), or 0 if we cannot pass it
	 */
	sint8 get_tiles_to_pass_standing(const overtaker_t *other, uint32 start_index) const;
};

#endif
