/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SCHEDULE_ENTRY_H
#define DATAOBJ_SCHEDULE_ENTRY_H


#include "koord3d.h"

/**
 * A schedule entry.
 */
struct schedule_entry_t
{
public:
	/**
	 * Stop type (fork): what cargo may do at this entry and how the route planner
	 * treats it, see the table in the schedule documentation.
	 */
	enum stop_type_t {
		regular     = 0, ///< load, unload, transfer, ride through
		terminal    = 1, ///< everything off, load; no transfers, nothing rides through
		all_off     = 2, ///< everything off, no loading; nothing rides through
		only_load   = 3, ///< no unloading; planner never routes cargo to here on this line
		only_unload = 4, ///< no loading; planner never routes cargo from here on this line
		max_stop_type
	};

	schedule_entry_t() : minimum_loading(0), waiting_time_shift(0), waiting_time(0), departure_interval(0), departure_offset(0), stop_type(regular) {}

	schedule_entry_t(koord3d const& pos, uint const minimum_loading, sint8 const waiting_time_shift, uint16 const waiting_time = 0, uint16 const departure_interval = 0, uint16 const departure_offset = 0, uint8 const stop_type = regular) :
		pos(pos),
		minimum_loading(minimum_loading),
		waiting_time_shift(waiting_time_shift),
		waiting_time(waiting_time),
		departure_interval(departure_interval),
		departure_offset(departure_offset),
		stop_type(stop_type < max_stop_type ? stop_type : (uint8)regular)
	{}

	/**
	 * target position
	 */
	koord3d pos;

	/**
	 * Wait for % load at this stops
	 * (ignored on waypoints)
	 */
	uint8 minimum_loading;

	/**
	 * maximum waiting time in 1/2^(16-n) parts of a month
	 * (only active if minimum_loading!=0)
	 */
	sint8 waiting_time_shift;

	/**
	 * maximum waiting time in calendar minutes (settings_t::minutes_per_month per month);
	 * takes precedence over waiting_time_shift when non-zero
	 * (only active if minimum_loading!=0)
	 */
	uint16 waiting_time;

	/// true if the convoy leaves after a maximum waiting time (either unit)
	bool has_waiting_time() const { return waiting_time > 0  ||  waiting_time_shift > 0; }

	/// maximum waiting time in ticks (0 if none)
	uint32 get_waiting_ticks() const;

	/**
	 * Timetable (fork, needs the world calendar and a line): convoys leave this stop only
	 * at fixed slots, every departure_interval calendar minutes counted from midnight
	 * plus departure_offset. 0 = no timetable. Applies on top of the loading rules.
	 */
	uint16 departure_interval;
	uint16 departure_offset;

	bool has_timetable() const { return departure_interval > 0; }

	/// one of stop_type_t
	uint8 stop_type;

	/// the convoy loads cargo at this entry
	bool loads() const { return stop_type != all_off  &&  stop_type != only_unload; }
	/// the convoy unloads cargo at this entry
	bool unloads() const { return stop_type != only_load; }
	/// the convoy unloads everything, also cargo bound elsewhere
	bool unloads_all() const { return stop_type == terminal  ||  stop_type == all_off; }
	/// cargo aboard may stay aboard past this entry
	bool rides_through() const { return stop_type != terminal  &&  stop_type != all_off; }
	/// the planner may route cargo to this entry on this schedule
	bool plans_arrival() const { return stop_type != only_load; }
	/// the planner may route cargo from this entry on this schedule
	bool plans_departure() const { return loads(); }

	static const char *get_stop_type_name(uint8 stop_type);
};

inline bool operator ==(const schedule_entry_t &a, const schedule_entry_t &b)
{
	return a.pos == b.pos  &&  a.minimum_loading == b.minimum_loading  &&  a.waiting_time_shift == b.waiting_time_shift  &&  a.waiting_time == b.waiting_time
		&&  a.departure_interval == b.departure_interval  &&  a.departure_offset == b.departure_offset  &&  a.stop_type == b.stop_type;
}


#endif
