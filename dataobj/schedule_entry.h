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
	schedule_entry_t() : minimum_loading(0), waiting_time_shift(0), waiting_time(0) {}

	schedule_entry_t(koord3d const& pos, uint const minimum_loading, sint8 const waiting_time_shift, uint16 const waiting_time = 0) :
		pos(pos),
		minimum_loading(minimum_loading),
		waiting_time_shift(waiting_time_shift),
		waiting_time(waiting_time)
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
};

inline bool operator ==(const schedule_entry_t &a, const schedule_entry_t &b)
{
	return a.pos == b.pos  &&  a.minimum_loading == b.minimum_loading  &&  a.waiting_time_shift == b.waiting_time_shift  &&  a.waiting_time == b.waiting_time;
}


#endif
