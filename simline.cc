/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "simtypes.h"
#include "simline.h"
#include "simhalt.h"
#include "simworld.h"

#include "utils/simstring.h"
#include "dataobj/schedule.h"
#include "dataobj/translator.h"
#include "dataobj/loadsave.h"
#include "gui/gui_theme.h"
#include "player/simplay.h"
#include "player/finance.h" // convert_money
#include "vehicle/simvehicle.h"
#include "simconvoi.h"
#include "convoihandle_t.h"
#include "simlinemgmt.h"

uint8 convoi_to_line_catgory_[convoi_t::MAX_CONVOI_COST] = {
	LINE_CAPACITY, LINE_TRANSPORTED_GOODS, LINE_REVENUE, LINE_OPERATIONS, LINE_PROFIT, LINE_DISTANCE, LINE_MAXSPEED, LINE_WAYTOLL
};


uint8 simline_t::convoi_to_line_catgory(uint8 cnv_cost)
{
	assert(cnv_cost < convoi_t::MAX_CONVOI_COST);
	return convoi_to_line_catgory_[cnv_cost];
}


karte_ptr_t simline_t::welt;


simline_t::simline_t(player_t* player, linetype type)
{
	self = linehandle_t(this);
	char printname[128];
	sprintf(printname, "(%i) %s", self.get_id(), translator::translate("Line", welt->get_settings().get_name_language_id()));
	name = printname;

	init_financial_history();
	this->type = type;
	this->schedule = NULL;
	this->player = player;
	withdraw = false;
	hold_marker = false;
	state_color = SYSCOL_TEXT;
	create_schedule();
}


simline_t::simline_t(player_t* player, linetype type, loadsave_t *file)
{
	// id will be read and assigned during rdwr
	self = linehandle_t();
	this->type = type;
	this->schedule = NULL;
	this->player = player;
	withdraw = false;
	hold_marker = false;
	create_schedule();
	rdwr(file);
	// now self has the right id but the this-pointer is not assigned to the quickstone handle yet
	// do this explicitly
	// some savegames have line_id=0, resolve that in finish_rd
	if (self.get_id()!=0) {
		self = linehandle_t(this, self.get_id());
	}
}


simline_t::~simline_t()
{
	DBG_DEBUG("simline_t::~simline_t()", "deleting schedule=%p", schedule);

	assert(count_convoys()==0);
	unregister_stops();

	delete schedule;
	self.detach();
	DBG_MESSAGE("simline_t::~simline_t()", "line %d (%p) destroyed", self.get_id(), this);
}


simline_t::linetype simline_t::waytype_to_linetype(const waytype_t wt)
{
	switch (wt) {
		case road_wt: return simline_t::truckline;
		case track_wt: return simline_t::trainline;
		case water_wt: return simline_t::shipline;
		case monorail_wt: return simline_t::monorailline;
		case maglev_wt: return simline_t::maglevline;
		case tram_wt: return simline_t::tramline;
		case narrowgauge_wt: return simline_t::narrowgaugeline;
		case air_wt: return simline_t::airline;
		default: return simline_t::MAX_LINE_TYPE;
	}
}


const char *simline_t::get_linetype_name(const simline_t::linetype lt)
{
	static const char *lt2name[MAX_LINE_TYPE] = {"All", "Truck", "Train", "Ship", "Air", "Monorail", "Tram", "Maglev", "Narrowgauge" };
	return translator::translate( lt2name[lt] );
}


waytype_t simline_t::linetype_to_waytype(const linetype lt)
{
	static const waytype_t wt2lt[MAX_LINE_TYPE] = { invalid_wt, road_wt, track_wt, water_wt, air_wt, monorail_wt, tram_wt, maglev_wt, narrowgauge_wt };
	return wt2lt[lt];
}


void simline_t::set_schedule(schedule_t* schedule)
{
	if(  this->schedule  &&  schedule  &&  schedule != this->schedule  ) {
		// fork: an edit keeps the used slots and missed couplings of the entries it did not touch
		keep_slots_across_edit( this->schedule, schedule );
	}
	else {
		last_departure_slot.clear();
		missed_couplings.clear();
	}
	if (this->schedule) {
		unregister_stops();
		delete this->schedule;
	}
	this->schedule = schedule;
}


// fork: same timetable (interval and all departure offsets)?
static bool same_timetable(const schedule_entry_t &a, const schedule_entry_t &b)
{
	if(  a.departure_interval != b.departure_interval  ) {
		return false;
	}
	uint16 oa[schedule_entry_t::MAX_EXTRA_OFFSETS + 1], ob[schedule_entry_t::MAX_EXTRA_OFFSETS + 1];
	const uint8 na = a.get_departure_offsets( oa );
	const uint8 nb = b.get_departure_offsets( ob );
	return na == nb  &&  memcmp( oa, ob, na * sizeof(uint16) ) == 0;
}


// fork: same tile, or same halt of this owner
static bool same_stop(koord3d a, koord3d b, const player_t *owner)
{
	if(  a == b  ) {
		return true;
	}
	const halthandle_t h = haltestelle_t::get_halt( a, owner );
	return h.is_bound()  &&  h == haltestelle_t::get_halt( b, owner );
}


void simline_t::keep_slots_across_edit(const schedule_t *old_schedule, const schedule_t *new_schedule)
{
	// which old entry each new entry continues: the longest run of the same stops in the same order
	// (same tile, or same halt), so entries added, removed or moved elsewhere do not shift the rest
	const uint32 n = old_schedule->get_count();
	const uint32 m = new_schedule->get_count();
	uint16 *len = new uint16[(n+1) * (m+1)];
	#define LEN(i, j) len[(i) * (m+1) + (j)]
	for(  uint32 i = n+1;  i-- > 0;  ) {
		for(  uint32 j = m+1;  j-- > 0;  ) {
			if(  i == n  ||  j == m  ) {
				LEN(i, j) = 0;
			}
			else if(  same_stop( old_schedule->entries[i].pos, new_schedule->entries[j].pos, player )  ) {
				LEN(i, j) = LEN(i+1, j+1) + 1;
			}
			else {
				LEN(i, j) = max( LEN(i+1, j), LEN(i, j+1) );
			}
		}
	}
	vector_tpl<sint16> from( m );
	for(  uint32 j=0;  j<m;  j++  ) {
		from.append( -1 );
	}
	for(  uint32 i=0, j=0;  i<n  &&  j<m;  ) {
		if(  same_stop( old_schedule->entries[i].pos, new_schedule->entries[j].pos, player )  &&  LEN(i, j) == LEN(i+1, j+1) + 1  ) {
			from[j] = i;
			i++;
			j++;
		}
		else if(  LEN(i+1, j) >= LEN(i, j+1)  ) {
			i++;
		}
		else {
			j++;
		}
	}
	#undef LEN
	delete [] len;

	// used slots: kept where the entry stayed and its timetable did not change
	vector_tpl<sint64> slots( m );
	for(  uint32 j=0;  j<m;  j++  ) {
		const sint16 i = from[j];
		const bool keep = i >= 0  &&  (uint32)i < last_departure_slot.get_count()  &&  same_timetable( old_schedule->entries[i], new_schedule->entries[j] );
		slots.append( keep ? last_departure_slot[i] : -1 );
	}
	last_departure_slot.clear();
	FOR( vector_tpl<sint64>, const s, slots ) {
		last_departure_slot.append( s );
	}

	// missed couplings: follow their entry, dropped with it
	for(  uint32 k = missed_couplings.get_count();  k-- > 0;  ) {
		sint16 to = -1;
		for(  uint32 j=0;  j<m  &&  to<0;  j++  ) {
			if(  from[j] == missed_couplings[k].entry  ) {
				to = j;
			}
		}
		if(  to < 0  ) {
			missed_couplings.remove_at( k );
		}
		else {
			missed_couplings[k].entry = (uint8)to;
		}
	}
}



/*
 * Timetable slots (fork). A day is divided into cycles of departure_interval minutes starting
 * at midnight; within every cycle the entry's offsets are departure slots (all below 1440).
 * A slot stays open for half the gap to the following slot.
 */

// splits calendar minutes into day and minute of the day
static void split_day(sint64 minutes, sint64 &day, sint64 &minute_of_day)
{
	day = minutes / 1440;
	minute_of_day = minutes - day * 1440;
	if(  minute_of_day < 0  ) {
		minute_of_day += 1440;
		day --;
	}
}


// the last slot at or before this minute of the day, if the day has one yet
static bool slot_at_or_before(const schedule_entry_t &entry, const uint16 *offsets, uint8 n, sint64 minute_of_day, sint64 &slot)
{
	const sint64 interval = entry.departure_interval;
	const sint64 cycle = minute_of_day / interval;
	const sint64 rem = minute_of_day - cycle * interval;
	for(  int i = n-1;  i >= 0;  i--  ) {
		if(  offsets[i] <= rem  ) {
			slot = cycle * interval + offsets[i];
			return true;
		}
	}
	if(  cycle == 0  ) {
		return false;
	}
	slot = (cycle - 1) * interval + offsets[n-1];
	return true;
}


// the first slot after this minute of the day; 1440 and more means the next day
static sint64 slot_after(const schedule_entry_t &entry, const uint16 *offsets, uint8 n, sint64 minute_of_day)
{
	const sint64 interval = entry.departure_interval;
	const sint64 cycle = minute_of_day / interval;
	const sint64 rem = minute_of_day - cycle * interval;
	for(  uint8 i = 0;  i < n;  i++  ) {
		if(  offsets[i] > rem  ) {
			const sint64 slot = cycle * interval + offsets[i];
			if(  slot < 1440  ) {
				return slot;
			}
			break;
		}
	}
	const sint64 slot = (cycle + 1) * interval + offsets[0];
	if(  slot < 1440  ) {
		return slot;
	}
	return 1440 + offsets[0];
}


// the slot after the given one, restarting at the first offset after midnight
static sint64 next_departure_slot(const schedule_entry_t &entry, sint64 slot)
{
	uint16 offsets[schedule_entry_t::MAX_EXTRA_OFFSETS + 1];
	const uint8 n = entry.get_departure_offsets( offsets );
	sint64 day, minute_of_day;
	split_day( slot, day, minute_of_day );
	return day * 1440 + slot_after( entry, offsets, n, minute_of_day );
}


// the first slot a convoy may use from now on: the open one, or else the next to come
static sint64 first_departure_slot(const schedule_entry_t &entry, sint64 now)
{
	uint16 offsets[schedule_entry_t::MAX_EXTRA_OFFSETS + 1];
	const uint8 n = entry.get_departure_offsets( offsets );
	sint64 day, minute_of_day;
	split_day( now, day, minute_of_day );
	sint64 prev;
	if(  slot_at_or_before( entry, offsets, n, minute_of_day, prev )  ) {
		const sint64 gap = slot_after( entry, offsets, n, prev ) - prev;
		if(  (minute_of_day - prev) * 2 <= gap  ) {
			return day * 1440 + prev;
		}
	}
	return day * 1440 + slot_after( entry, offsets, n, minute_of_day );
}


uint32 simline_t::count_earlier_waiting(convoihandle_t cnv) const
{
	const schedule_t *cnv_schedule = cnv->get_schedule();
	if(  cnv_schedule == NULL  ||  cnv_schedule->empty()  ) {
		return 0;
	}
	const uint8 idx = cnv_schedule->get_current_stop();
	uint32 count = 0;
	FOR(vector_tpl<convoihandle_t>, const &other, line_managed_convoys) {
		if(  other == cnv  ||  !other.is_bound()  ||  other->get_state() != convoi_t::LOADING  ) {
			continue;
		}
		const schedule_t *other_schedule = other->get_schedule();
		if(  other_schedule == NULL  ||  other_schedule->get_current_stop() != idx  ) {
			continue;
		}
		if(  (sint32)(other->get_arrived_time() - cnv->get_arrived_time()) < 0  ) {
			count ++;
		}
	}
	return count;
}


bool simline_t::get_planned_departure(convoihandle_t cnv, sint64 ready_at, sint64 &slot) const
{
	const schedule_t *cnv_schedule = cnv->get_schedule();
	if(  cnv_schedule == NULL  ||  cnv_schedule->empty()  ) {
		return false;
	}
	const uint8 idx = cnv_schedule->get_current_stop();
	const schedule_entry_t &entry = cnv_schedule->entries[idx];
	if(  !entry.has_timetable()  ||  !welt->has_calendar()  ) {
		return false;
	}
	sint64 s = first_departure_slot( entry, max( ready_at, welt->get_calendar_minutes() ) );
	if(  idx < last_departure_slot.get_count()  &&  last_departure_slot[idx] == s  ) {
		// this slot is gone already
		s = next_departure_slot( entry, s );
	}
	// every convoy that arrived earlier takes one slot before us
	for(  uint32 n = count_earlier_waiting( cnv );  n > 0;  n --  ) {
		s = next_departure_slot( entry, s );
	}
	slot = s;
	return true;
}


bool simline_t::get_open_departure_slot(const schedule_entry_t &entry, sint64 &slot)
{
	if(  !entry.has_timetable()  ||  !welt->has_calendar()  ) {
		return false;
	}
	uint16 offsets[schedule_entry_t::MAX_EXTRA_OFFSETS + 1];
	const uint8 n = entry.get_departure_offsets( offsets );
	sint64 day, minute_of_day;
	split_day( welt->get_calendar_minutes(), day, minute_of_day );
	sint64 prev;
	if(  !slot_at_or_before( entry, offsets, n, minute_of_day, prev )  ) {
		// before the first slot of the day
		return false;
	}
	const sint64 gap = slot_after( entry, offsets, n, prev ) - prev;
	if(  (minute_of_day - prev) * 2 > gap  ) {
		// the slot is closed again, wait for the next one
		return false;
	}
	slot = day * 1440 + prev;
	return true;
}


bool simline_t::can_take_departure_slot(convoihandle_t cnv, sint64 &slot) const
{
	const schedule_t *cnv_schedule = cnv->get_schedule();
	if(  cnv_schedule == NULL  ||  cnv_schedule->empty()  ) {
		return false;
	}
	const uint8 idx = cnv_schedule->get_current_stop();
	const schedule_entry_t &entry = cnv_schedule->entries[idx];
	if(  !get_open_departure_slot( entry, slot )  ) {
		return false;
	}
	if(  idx < last_departure_slot.get_count()  &&  last_departure_slot[idx] == slot  ) {
		// somebody left in this slot already
		return false;
	}
	// first come, first served: a ready convoy that arrived earlier at this stop goes first
	FOR(vector_tpl<convoihandle_t>, const &other, line_managed_convoys) {
		if(  other == cnv  ||  !other.is_bound()  ||  other->get_state() != convoi_t::LOADING  ) {
			continue;
		}
		const schedule_t *other_schedule = other->get_schedule();
		if(  other_schedule == NULL  ||  other_schedule->get_current_stop() != idx  ) {
			continue;
		}
		if(  (sint32)(other->get_arrived_time() - cnv->get_arrived_time()) < 0  &&  other->is_ready_to_depart()  ) {
			return false;
		}
	}
	return true;
}


void simline_t::book_departure_slot(uint8 entry, sint64 slot)
{
	while(  last_departure_slot.get_count() <= entry  ) {
		last_departure_slot.append( -1 );
	}
	last_departure_slot[entry] = slot;
}


bool simline_t::take_departure_slot(convoihandle_t cnv)
{
	const schedule_t *cnv_schedule = cnv->get_schedule();
	if(  cnv_schedule == NULL  ||  cnv_schedule->empty()  ) {
		return true;
	}
	sint64 slot;
	if(  !can_take_departure_slot( cnv, slot )  ) {
		return false;
	}
	book_departure_slot( cnv_schedule->get_current_stop(), slot );
	return true;
}


bool simline_t::get_late_departure_slot(convoihandle_t cnv, sint64 inherited, sint64 &slot) const
{
	const schedule_t *cnv_schedule = cnv->get_schedule();
	if(  cnv_schedule == NULL  ||  cnv_schedule->empty()  ||  !welt->has_calendar()  ) {
		return false;
	}
	const uint8 idx = cnv_schedule->get_current_stop();
	const schedule_entry_t &entry = cnv_schedule->entries[idx];
	if(  !entry.has_timetable()  ) {
		return false;
	}
	const sint64 last = idx < last_departure_slot.get_count() ? last_departure_slot[idx] : -1;
	sint64 candidate = -1;
	if(  inherited >= 0  &&  inherited > last  ) {
		candidate = inherited;
	}
	else if(  last >= 0  ) {
		candidate = next_departure_slot( entry, last );
	}
	if(  candidate < 0  ||  candidate > welt->get_calendar_minutes()  ) {
		// nothing known, or the slot is still ahead: on time again
		return false;
	}
	// a ready convoy of the line that arrived earlier goes first
	FOR(vector_tpl<convoihandle_t>, const &other, line_managed_convoys) {
		if(  other == cnv  ||  !other.is_bound()  ||  other->get_state() != convoi_t::LOADING  ) {
			continue;
		}
		const schedule_t *other_schedule = other->get_schedule();
		if(  other_schedule == NULL  ||  other_schedule->get_current_stop() != idx  ) {
			continue;
		}
		if(  (sint32)(other->get_arrived_time() - cnv->get_arrived_time()) < 0  &&  other->is_ready_to_depart()  ) {
			slot = -1;
			return true;
		}
	}
	slot = candidate;
	return true;
}


void simline_t::add_missed_coupling(uint8 entry, sint64 slot)
{
	// a handful is plenty; drop the oldest beyond that
	if(  missed_couplings.get_count() >= 8  ) {
		missed_couplings.remove_at( 0 );
	}
	missed_coupling_t m;
	m.entry = entry;
	m.slot = slot;
	missed_couplings.append( m );
}


bool simline_t::take_missed_coupling(uint8 entry, sint64 &slot)
{
	for(  uint32 i=0;  i<missed_couplings.get_count();  i++  ) {
		if(  missed_couplings[i].entry == entry  ) {
			slot = missed_couplings[i].slot;
			missed_couplings.remove_at( i );
			return true;
		}
	}
	return false;
}


void simline_t::create_schedule()
{
	switch(type) {
		case simline_t::truckline:       set_schedule(new truck_schedule_t()); break;
		case simline_t::trainline:       set_schedule(new train_schedule_t()); break;
		case simline_t::shipline:        set_schedule(new ship_schedule_t()); break;
		case simline_t::airline:         set_schedule(new airplane_schedule_t()); break;
		case simline_t::monorailline:    set_schedule(new monorail_schedule_t()); break;
		case simline_t::tramline:        set_schedule(new tram_schedule_t()); break;
		case simline_t::maglevline:      set_schedule(new maglev_schedule_t()); break;
		case simline_t::narrowgaugeline: set_schedule(new narrowgauge_schedule_t()); break;
		default:
			dbg->fatal( "simline_t::create_schedule()", "Cannot create default schedule!" );
	}
}


void simline_t::add_convoy(convoihandle_t cnv)
{
	if (line_managed_convoys.empty()  &&  self.is_bound()) {
		// first convoi -> ok, now we can announce this connection to the stations
		// unbound self can happen during loading if this line had line_id=0
		register_stops(schedule);
	}

	// first convoi may change line type
	if (type == trainline  &&  line_managed_convoys.empty() &&  cnv.is_bound()) {
		// check, if needed to convert to tram/monorail line
		if (vehicle_t const* const v = cnv->front()) {
			switch (v->get_desc()->get_waytype()) {
				case tram_wt:     type = simline_t::tramline;     break;
				// elevated monorail were saved with wrong coordinates for some versions.
				// We try to recover here
				case monorail_wt: type = simline_t::monorailline; break;
				default:          break;
			}
		}
	}
	// only add convoy if not already member of line
	line_managed_convoys.append_unique(cnv);

	// what goods can this line transport?
	bool update_schedules = false;
	if(  cnv->get_state()!=convoi_t::INITIAL  ) {
		FOR(minivec_tpl<uint8>, const catg_index, cnv->get_goods_catg_index()) {
			if(  !goods_catg_index.is_contained( catg_index )  ) {
				goods_catg_index.append( catg_index, 1 );
				update_schedules = true;
			}
		}
	}

	// will not hurt ...
	financial_history[0][LINE_CONVOIS] = count_convoys();
	recalc_status();

	// do we need to tell the world about our new schedule?
	if(  update_schedules  ) {
		welt->set_schedule_counter();
	}
}


void simline_t::remove_convoy(convoihandle_t cnv)
{
	if(line_managed_convoys.is_contained(cnv)) {
		line_managed_convoys.remove(cnv);
		recalc_catg_index();
		financial_history[0][LINE_CONVOIS] = count_convoys();
		recalc_status();
	}
	if(line_managed_convoys.empty()) {
		unregister_stops();
	}
}


// invalid line id prior to 110.0
#define INVALID_LINE_ID_OLD ((uint16)(-1))
// invalid line id from 110.0 on
#define INVALID_LINE_ID ((uint16)(0))

void simline_t::rdwr_linehandle_t(loadsave_t *file, linehandle_t &line)
{
	uint16 id;
	if (file->is_saving()) {
		id = line.is_bound() ? line.get_id() :
			 (file->is_version_less(110, 0)  ? INVALID_LINE_ID_OLD : INVALID_LINE_ID);
	}
	else {
		// to avoid undefined errors during loading
		id = 0;
	}

	if(file->is_version_less(88, 3)) {
		sint32 dummy=id;
		file->rdwr_long(dummy);
		id = (uint16)dummy;
	}
	else {
		file->rdwr_short(id);
	}
	if (file->is_loading()) {
		// invalid line_id's: 0 and 65535
		if (id == INVALID_LINE_ID_OLD) {
			id = 0;
		}
		line.set_id(id);
	}
}


void simline_t::rdwr(loadsave_t *file)
{
	xml_tag_t s( file, "simline_t" );

	assert(schedule);

	file->rdwr_str(name);

	rdwr_linehandle_t(file, self);

	schedule->rdwr(file);

	//financial history
	if(  file->is_version_less(102, 3)  ) {
		for (int j = 0; j<6; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][LINE_DISTANCE] = 0;
			financial_history[k][LINE_MAXSPEED] = 0;
			financial_history[k][LINE_WAYTOLL] = 0;
		}
	}
	else if(  file->is_version_less(111, 1)  ) {
		for (int j = 0; j<7; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][LINE_MAXSPEED] = 0;
			financial_history[k][LINE_WAYTOLL] = 0;
		}
	}
	else if(  file->is_version_less(112, 8)  ) {
		for (int j = 0; j<8; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (size_t k = MAX_MONTHS; k-- != 0;) {
			financial_history[k][LINE_WAYTOLL] = 0;
		}
	}
	else {
		for (int j = 0; j<MAX_LINE_COST; j++) {
			for (size_t k = MAX_MONTHS; k-- != 0;) {
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
	}

	if(file->is_version_atleast(102, 2)) {
		file->rdwr_bool(withdraw);
	}

	if(file->is_version_atleast(122, 2)) {
		// fork: timetable slots already used
		uint8 count = (uint8)min( last_departure_slot.get_count(), 255 );
		file->rdwr_byte(count);
		if(  file->is_loading()  ) {
			last_departure_slot.clear();
			for(  uint8 i=0;  i<count;  i++  ) {
				sint64 slot = -1;
				file->rdwr_longlong(slot);
				last_departure_slot.append(slot);
			}
		}
		else {
			for(  uint8 i=0;  i<count;  i++  ) {
				file->rdwr_longlong(last_departure_slot[i]);
			}
		}
	}

	if(  file->is_version_atleast(122, 5)  ) {
		// fork: Hold marker
		file->rdwr_bool(hold_marker);
	}

	if(  file->is_version_atleast(122, 6)  ) {
		// fork: slots of primary trains that left without the train of this line (coupling)
		uint8 count = (uint8)missed_couplings.get_count();
		file->rdwr_byte(count);
		if(  file->is_loading()  ) {
			missed_couplings.clear();
		}
		for(  uint8 i=0;  i<count;  i++  ) {
			missed_coupling_t m;
			if(  file->is_saving()  ) {
				m = missed_couplings[i];
			}
			file->rdwr_byte(m.entry);
			file->rdwr_longlong(m.slot);
			if(  file->is_loading()  ) {
				missed_couplings.append( m );
			}
		}
	}

	// otherwise initialized to zero if loading ...
	financial_history[0][LINE_CONVOIS] = count_convoys();
}



void simline_t::finish_rd()
{
	if(  !self.is_bound()  ) {
		// get correct handle
		self = player->simlinemgmt.get_line_with_id_zero();
		assert( self.get_rep() == this );
		DBG_MESSAGE("simline_t::finish_rd", "assigned id=%d to line %s", self.get_id(), get_name());
	}
	if (!line_managed_convoys.empty()) {
		register_stops(schedule);
	}
	recalc_status();
}



void simline_t::register_stops(schedule_t * schedule)
{
DBG_DEBUG("simline_t::register_stops()", "%d schedule entries in schedule %p", schedule->get_count(),schedule);
	FOR(minivec_tpl<schedule_entry_t>, const& i, schedule->entries) {
		halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
		if(halt.is_bound()) {
//DBG_DEBUG("simline_t::register_stops()", "halt not null");
			halt->add_line(self);
		}
		else {
DBG_DEBUG("simline_t::register_stops()", "halt null");
		}
	}
}



void simline_t::unregister_stops()
{
	unregister_stops(schedule);
}


void simline_t::unregister_stops(schedule_t * schedule)
{
	FOR(minivec_tpl<schedule_entry_t>, const& i, schedule->entries) {
		halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
		if(halt.is_bound()) {
			halt->remove_line(self);
		}
	}
}


void simline_t::renew_stops()
{
	if (!line_managed_convoys.empty()) {
		register_stops( schedule );
		DBG_DEBUG("simline_t::renew_stops()", "Line id=%d, name='%s'", self.get_id(), name.c_str());
	}
}


void simline_t::check_freight()
{
	FOR(vector_tpl<convoihandle_t>, const i, line_managed_convoys) {
		i->check_freight();
	}
}


void simline_t::new_month()
{
	recalc_status();
	// then calculate maxspeed
	sint64 line_max_speed = 0, line_max_speed_count = 0;
	FOR(vector_tpl<convoihandle_t>, const i, line_managed_convoys) {
		if (!i->in_depot()) {
			// since convoi stepped first, our history is in month 1 ...
			line_max_speed += i->get_finance_history(1, convoi_t::CONVOI_MAXSPEED);
			line_max_speed_count ++;
		}
	}
	// to avoid div by zero
	if(  line_max_speed_count  ) {
		line_max_speed /= line_max_speed_count;
	}
	financial_history[0][LINE_MAXSPEED] = line_max_speed;
	// now roll history
	for (int j = 0; j<MAX_LINE_COST; j++) {
		for (int k = MAX_MONTHS-1; k>0; k--) {
			financial_history[k][j] = financial_history[k-1][j];
		}
		financial_history[0][j] = 0;
	}
	financial_history[0][LINE_CONVOIS] = count_convoys();
}


void simline_t::init_financial_history()
{
	MEMZERO(financial_history);
}



/*
 * the current state saved as color
 * Meanings are BLACK (ok), WHITE (no convois), YELLOW (no vehicle moved), RED (last month income minus), BLUE (at least one convoi vehicle is obsolete)
 */
void simline_t::recalc_status()
{
	if(financial_history[0][LINE_CONVOIS]==0) {
		// no convois assigned to this line
		state_color = SYSCOL_EMPTY;
		withdraw = false;
	}
	else if(financial_history[0][LINE_PROFIT]<0) {
		// ok, not performing best
		state_color = MONEY_MINUS;
	}
	else if((financial_history[0][LINE_OPERATIONS]|financial_history[1][LINE_OPERATIONS])==0) {
		// nothing moved
		state_color = SYSCOL_TEXT_UNUSED;
	}
	else if(welt->use_timeline()) {
		// convois has obsolete vehicles?
		bool has_obsolete = false;
		FOR(vector_tpl<convoihandle_t>, const i, line_managed_convoys) {
			has_obsolete = i->has_obsolete_vehicles();
			if (has_obsolete) break;
		}
		// now we have to set it
		state_color = has_obsolete ? SYSCOL_OBSOLETE : SYSCOL_TEXT;
	}
	else {
		// normal state
		state_color = SYSCOL_TEXT;
	}
}



// recalc what good this line is moving
void simline_t::recalc_catg_index()
{
	// first copy old
	minivec_tpl<uint8> old_goods_catg_index(goods_catg_index.get_count());
	FOR(minivec_tpl<uint8>, const i, goods_catg_index) {
		old_goods_catg_index.append(i);
	}
	goods_catg_index.clear();
	withdraw = !line_managed_convoys.empty();
	// then recreate current
	FOR(vector_tpl<convoihandle_t>, const i, line_managed_convoys) {
		// what goods can this line transport?
		convoi_t const& cnv = *i;
		withdraw &= cnv.get_withdraw();

		FOR(minivec_tpl<uint8>, const catg_index, cnv.get_goods_catg_index()) {
			goods_catg_index.append_unique( catg_index );
		}
	}
	// if different => schedule need recalculation
	if(  goods_catg_index.get_count()!=old_goods_catg_index.get_count()  ) {
		// surely changed
		welt->set_schedule_counter();
	}
	else {
		// maybe changed => must test all entries
		FOR(minivec_tpl<uint8>, const i, goods_catg_index) {
			if (!old_goods_catg_index.is_contained(i)) {
				// different => recalc
				welt->set_schedule_counter();
				break;
			}
		}
	}
}



void simline_t::set_withdraw( bool yes_no )
{
	withdraw = yes_no && !line_managed_convoys.empty();
	// convois in depots will be immediately destroyed, thus we go backwards
	for (size_t i = line_managed_convoys.get_count(); i-- != 0;) {
		line_managed_convoys[i]->set_no_load(yes_no); // must be first, since set withdraw might destroy convoi if in depot!
		line_managed_convoys[i]->set_withdraw(yes_no);
	}
}


sint64 simline_t::get_stat_converted(int month, int cost_type) const
{
	sint64 value = financial_history[month][cost_type];
	switch(cost_type) {
		case LINE_REVENUE:
		case LINE_OPERATIONS:
		case LINE_PROFIT:
		case LINE_WAYTOLL:
			value = convert_money(value);
			break;
		default: ;
	}
	return value;
}
