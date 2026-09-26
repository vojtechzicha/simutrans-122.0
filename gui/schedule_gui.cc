/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simline.h"
#include "../simcolor.h"
#include "../simhalt.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simconvoi.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

#include "../boden/grund.h"

#include "../obj/zeiger.h"

#include "../dataobj/schedule.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../player/simplay.h"

#include "../tpl/vector_tpl.h"

#include "depot_frame.h"
#include "schedule_gui.h"
#include "line_item.h"

#include "components/gui_button.h"
#include "components/gui_image.h"
#include "components/gui_textarea.h"
#include "minimap.h"

static karte_ptr_t welt;

/**
 * Stop type badge (fork): a small square in front of each schedule entry that shows the
 * stop type as a letter. A left click on it cycles to the next type, a right click back.
 */
class gui_stop_type_badge_t : public gui_component_t
{
	uint8 stop_type;
	cbuffer_t tooltip;

public:
	gui_stop_type_badge_t(uint8 t) : stop_type(t)
	{
		tooltip.printf( "%s: %s", translator::translate("Stop type"), translator::translate( schedule_entry_t::get_stop_type_name(t) ) );
		set_size( get_min_size() );
	}

	scr_size get_min_size() const OVERRIDE { return scr_size( LINESPACE, LINESPACE ); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }

	static const char *get_glyph(uint8 t)
	{
		switch(  t  ) {
			case schedule_entry_t::terminal:    return "T";
			case schedule_entry_t::all_off:     return "A";
			case schedule_entry_t::only_load:   return "L";
			case schedule_entry_t::only_unload: return "U";
			case schedule_entry_t::hold:        return "H";
			default:                            return "";
		}
	}

	static PIXVAL get_color(uint8 t)
	{
		switch(  t  ) {
			case schedule_entry_t::terminal:    return color_idx_to_rgb( COL_DARK_RED );
			case schedule_entry_t::all_off:     return color_idx_to_rgb( COL_ORANGE );
			case schedule_entry_t::only_load:   return color_idx_to_rgb( COL_DARK_GREEN );
			case schedule_entry_t::only_unload: return color_idx_to_rgb( COL_DARK_BLUE );
			case schedule_entry_t::hold:        return color_idx_to_rgb( COL_DARK_PURPLE );
			default:                            return color_idx_to_rgb( COL_GREY3 );
		}
	}

	void draw(scr_coord offset) OVERRIDE
	{
		const scr_coord p = pos + offset;
		if(  stop_type == schedule_entry_t::regular  ) {
			// empty frame: nothing special here, but there is something to click
			display_ddd_box_clip_rgb( p.x, p.y, size.w, size.h, get_color(stop_type), get_color(stop_type) );
		}
		else {
			display_fillbox_wh_clip_rgb( p.x, p.y, size.w, size.h, get_color(stop_type), true );
			display_proportional_clip_rgb( p.x + size.w/2, p.y, get_glyph(stop_type), ALIGN_CENTER_H, color_idx_to_rgb( COL_WHITE ), true );
		}
		if(  getroffen( get_mouse_x()-offset.x, get_mouse_y()-offset.y )  ) {
			win_set_tooltip( get_mouse_x() + TOOLTIP_MOUSE_OFFSET_X, p.y + size.h + TOOLTIP_MOUSE_OFFSET_Y, tooltip, this );
		}
	}
};

/**
 * One entry in the list of schedule entries.
 */
class gui_schedule_entry_t : public gui_aligned_container_t, public gui_action_creator_t
{
	schedule_entry_t entry;
	bool is_current;
	uint number;
	player_t* player;
	gui_image_t arrow;
	gui_stop_type_badge_t badge;
	gui_label_buf_t stop;

public:
	gui_schedule_entry_t(player_t* pl, schedule_entry_t e, uint n) : badge(e.stop_type)
	{
		player = pl;
		entry  = e;
		number = n;
		is_current = false;
		set_table_layout(3,1);

		add_component(&arrow);
		arrow.set_image(gui_theme_t::pos_button_img[0], true);

		add_component(&badge);

		add_component(&stop);
		update_label();
	}

	void update_label()
	{
		stop.buf().printf("%i) ", number+1);
		if(  entry.has_timetable()  &&  welt->has_calendar()  ) {
			// timetable marker in front of the name, which may be longer than the window: [8'] or [2h+30']
			schedule_t::append_timetable( stop.buf(), entry );
			stop.buf().append(" ");
		}
		schedule_t::gimme_stop_name(stop.buf(), welt, player, entry, -1);
		if(  entry.has_coupling()  ) {
			// coupling marker behind the name: [+ R12a]
			const char *name = "?";
			vector_tpl<linehandle_t> lines;
			player->simlinemgmt.get_lines( simline_t::line, &lines );
			FOR( vector_tpl<linehandle_t>, const l, lines ) {
				if(  l.get_id()==entry.couple_line_id  ) {
					name = l->get_name();
				}
			}
			stop.buf().printf( " [+ %s]", name );
		}
		stop.update();
	}

	void draw(scr_coord offset) OVERRIDE
	{
		update_label();
		if (is_current) {
			display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, size.w, size.h, SYSCOL_LIST_BACKGROUND_SELECTED_F, false);
		}
		gui_aligned_container_t::draw(offset);
	}

	void set_active(bool yesno)
	{
		is_current = yesno;
		arrow.set_image(gui_theme_t::pos_button_img[yesno ? 1: 0], true);
		stop.set_color(yesno ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_TEXT);
	}

	/**
	 * Listeners receive the entry index on left click (select this entry).
	 * A middle click requests deletion of this entry; it is signalled as
	 * the negative value -(index+1), see delete_request_to_index().
	 * A click on the stop type badge requests the next (left) or previous
	 * (right click) stop type; it is signalled as index plus a flag above 0xffff.
	 */
	static bool is_delete_request(long v) { return v < 0; }
	static long delete_request_to_index(long v) { return -v - 1; }
	static long toggle_request(uint n, bool backwards) { return (backwards ? 0x20000L : 0x10000L) + (long)n; }
	static bool is_toggle_request(long v) { return v >= 0x10000L; }
	static bool is_toggle_backwards(long v) { return v >= 0x20000L; }
	static long toggle_request_to_index(long v) { return v & 0xffffL; }

	bool infowin_event(const event_t *ev) OVERRIDE
	{
		if( ev->ev_class == EVENT_CLICK ) {
			if(  ev->ev_code == MOUSE_MIDBUTTON  ) {
				// middle click: remove this entry from the schedule
				call_listeners( value_t( -(long)number - 1 ) );
			}
			else if(  badge.getroffen( ev->mx, ev->my )  ) {
				// stop type badge: cycle the stop type
				call_listeners( value_t( toggle_request( number, IS_RIGHTCLICK(ev) ) ) );
			}
			else if(  IS_RIGHTCLICK(ev)  ||  ev->mx < stop.get_pos().x) {
				// just center on it
				welt->get_viewport()->change_world_position( entry.pos );
			}
			else {
				call_listeners(number);
			}
			return true;
		}
		return false;
	}
};

/**
 * List of displayed schedule entries.
 */
class schedule_gui_stats_t : public gui_aligned_container_t, action_listener_t, public gui_action_creator_t
{
	static cbuffer_t buf;

	vector_tpl<gui_schedule_entry_t*> entries;
	schedule_t *last_schedule; ///< last displayed schedule
	zeiger_t *current_stop_mark; ///< mark current stop on map
public:
	schedule_t *schedule;      ///< schedule under editing
	player_t*  player;

	schedule_gui_stats_t()
	{
		set_table_layout(1,0);
		last_schedule = NULL;

		current_stop_mark = new zeiger_t(koord3d::invalid, NULL );
		current_stop_mark->set_image( tool_t::general_tool[TOOL_SCHEDULE_ADD]->cursor );
	}
	~schedule_gui_stats_t()
	{
		delete current_stop_mark;
		delete last_schedule;
	}

	// shows/deletes highlighting of tiles
	void highlight_schedule(bool marking)
	{
		marking &= env_t::visualize_schedule;
		FOR(minivec_tpl<schedule_entry_t>, const& i, schedule->entries) {
			if (grund_t* const gr = welt->lookup(i.pos)) {
				for(  uint idx=0;  idx<gr->get_top();  idx++  ) {
					obj_t *obj = gr->obj_bei(idx);
					if(  marking  ) {
						if(  !obj->is_moving()  ) {
							obj->set_flag( obj_t::highlight );
						}
					}
					else {
						obj->clear_flag( obj_t::highlight );
					}
				}
				gr->set_flag( grund_t::dirty );
				// here on water
				if(  gr->is_water()  ||  gr->ist_natur()  ) {
					if(  marking  ) {
						gr->set_flag( grund_t::marked );
					}
					else {
						gr->clear_flag( grund_t::marked );
					}
				}

			}
		}
		// always remove
		if(  grund_t *old_gr = welt->lookup(current_stop_mark->get_pos())  ) {
			current_stop_mark->mark_image_dirty( current_stop_mark->get_image(), 0 );
			old_gr->obj_remove( current_stop_mark );
			old_gr->set_flag( grund_t::dirty );
			current_stop_mark->set_pos( koord3d::invalid );
		}
		// add if required
		if(  marking  &&  schedule->get_current_stop() < schedule->get_count() ) {
			current_stop_mark->set_pos( schedule->entries[schedule->get_current_stop()].pos );
			if(  grund_t *gr = welt->lookup(current_stop_mark->get_pos())  ) {
				gr->obj_add( current_stop_mark );
				current_stop_mark->set_flag( obj_t::dirty );
				gr->set_flag( grund_t::dirty );
			}
		}
		current_stop_mark->clear_flag( obj_t::highlight );
	}

	void update_schedule()
	{
		// compare schedules
		bool ok = (last_schedule != NULL)  &&  last_schedule->entries.get_count() == schedule->entries.get_count();
		for(uint i=0; ok  &&  i<last_schedule->entries.get_count(); i++) {
			ok = last_schedule->entries[i] == schedule->entries[i];
		}
		if (ok) {
			if (!last_schedule->empty()) {
				entries[ last_schedule->get_current_stop() ]->set_active(false);
				entries[ schedule->get_current_stop() ]->set_active(true);
				last_schedule->set_current_stop( schedule->get_current_stop() );
			}
		}
		else {
			remove_all();
			entries.clear();
			buf.clear();
			buf.append(translator::translate("Please click on the map to add\nwaypoints or stops to this\nschedule."));
			if (schedule->empty()) {
				new_component<gui_textarea_t>(&buf);
			}
			else {
				for(uint i=0; i<schedule->entries.get_count(); i++) {
					entries.append( new_component<gui_schedule_entry_t>(player, schedule->entries[i], i));
					entries.back()->add_listener( this );
				}
				entries[ schedule->get_current_stop() ]->set_active(true);
			}
			if (last_schedule) {
				last_schedule->copy_from(schedule);
			}
			else {
				last_schedule = schedule->copy();
			}
			set_size(get_min_size());
		}
		highlight_schedule(true);
	}
	void draw(scr_coord offset) OVERRIDE
	{
		update_schedule();

		gui_aligned_container_t::draw(offset);
	}
	bool action_triggered(gui_action_creator_t *, value_t v) OVERRIDE
	{
		// has to be one of the entries
		call_listeners(v);
		return true;
	}
};

/**
 * Entries in the waiting-time selection.
 */
class gui_waiting_time_item_t : public gui_scrolled_list_t::const_text_scrollitem_t
{
private:
	cbuffer_t buf;
	sint8 wait;

public:
	gui_waiting_time_item_t(sint8 w) : gui_scrolled_list_t::const_text_scrollitem_t(NULL, SYSCOL_TEXT)
	{
		wait = w;
		if (wait == 0) {
			buf.append(translator::translate("off"));
		}
		else {
			buf.printf("1/%d",  1<<(16 - wait) );
		}
	}

	char const* get_text () const OVERRIDE { return buf; }

	sint8 get_wait_shift() const { return wait; }
};

cbuffer_t schedule_gui_stats_t::buf;

schedule_gui_t::schedule_gui_t(schedule_t* schedule_, player_t* player_, convoihandle_t cnv_) :
	gui_frame_t( translator::translate("Fahrplan"), NULL),
	line_selector(line_scrollitem_t::compare),
	lb_waitlevel(SYSCOL_TEXT_HIGHLIGHT, gui_label_t::right),
	lb_wait(world()->has_calendar() ? "Wait time (min)" : "month wait time"),
	lb_load("Full load"),
	lb_interval("Departure every (min)"),
	lb_offset("Offset (min)"),
	lb_extra("Also at (min)"),
	lb_couple("Couple with"),
	lb_couple_wait("Wait for it (min)"),
	couple_selector(line_scrollitem_t::compare),
	stats(new schedule_gui_stats_t() ),
	scrolly(stats)
{
	schedule = NULL;
	player   = NULL;
	couple_table = NULL;
	if (schedule_) {
		init(schedule_, player_, cnv_);
	}
}

schedule_gui_t::~schedule_gui_t()
{
	if(  player  ) {
		update_tool( false );
		// hide schedule on minimap (may not current, but for safe)
		minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );
	}
	delete schedule;
	delete stats;
}

void schedule_gui_t::init(schedule_t* schedule_, player_t* player, convoihandle_t cnv)
{
	// initialization
	this->old_schedule = schedule_;
	this->cnv = cnv;
	this->player = player;
	set_owner(player);

	// prepare editing
	old_schedule->start_editing();
	schedule = old_schedule->copy();
	if(  !cnv.is_bound()  ) {
		old_line = new_line = linehandle_t();
	}
	else {
		// set this schedule as current to show on minimap if possible
		minimap_t::get_instance()->set_selected_cnv( cnv );
		old_line = new_line = cnv->get_line();
	}
	old_line_count = 0;
	couple_table = NULL;

	stats->player = player;
	stats->schedule = schedule;
	stats->update_schedule();
	stats->add_listener(this);

	set_table_layout(1,0);

	if(  cnv.is_bound()  ) {
		add_table(3,1);
		// things, only relevant to convois, like creating/selecting lines
		new_component<gui_label_t>("Serves Line:");
		bt_promote_to_line.init( button_t::roundbox, "promote to line");
		bt_promote_to_line.set_tooltip("Create a new line based on this schedule");
		bt_promote_to_line.add_listener(this);
		new_component<gui_fill_t>();
		add_component(&bt_promote_to_line);
		end_table();

		line_selector.clear_elements();

		init_line_selector();
		line_selector.add_listener(this);
		add_component(&line_selector);
	}

	// standing and overcrowded passengers (fork)
	add_table(2,1);
	{
		bt_no_standing.init( button_t::square_state, "Disable standing" );
		bt_no_standing.set_tooltip( "Passengers do not stand when all seats are taken" );
		bt_no_standing.add_listener(this);
		add_component(&bt_no_standing);

		bt_no_overcrowding.init( button_t::square_state, "Disable overcrowding" );
		bt_no_overcrowding.set_tooltip( "Passengers who missed a full vehicle do not overcrowd the next one" );
		bt_no_overcrowding.add_listener(this);
		add_component(&bt_no_overcrowding);
	}
	end_table();
	update_crowding_buttons();

	// loading level and waiting time
	add_table(2,2);
	{
		add_component(&lb_load);

		numimp_load.set_width( 60 );
		numimp_load.set_value( schedule->get_current_entry().minimum_loading );
		numimp_load.set_limits( 0, 100 );
		numimp_load.set_increment_mode( gui_numberinput_t::PROGRESS );
		numimp_load.add_listener(this);
		add_component(&numimp_load);

		add_component(&lb_wait);

		if(  welt->has_calendar()  ) {
			// world calendar: waiting time in minutes, 0 = off
			numimp_wait.set_width( 60 );
			numimp_wait.set_value( schedule->get_current_entry().waiting_time );
			numimp_wait.set_limits( 0, welt->get_settings().get_minutes_per_month() );
			numimp_wait.set_increment_mode( 1 );
			numimp_wait.add_listener(this);
			add_component(&numimp_wait);
		}
		else {
			add_component(&wait_load);
			wait_load.add_listener(this);

			wait_load.new_component<gui_waiting_time_item_t>(0);
			for(sint8 w = 7; w<=16; w++) {
				wait_load.new_component<gui_waiting_time_item_t>(w);
			}
			wait_load.set_rigid(true);
		}
	}
	end_table();

	if(  welt->has_calendar()  ) {
		// timetable: departure slots, 0 = none; three rows (interval, offset, more offsets)
		add_table(3,3);
		{
			add_component(&lb_interval);
			numimp_interval.set_width( 84 );
			numimp_interval.set_value( schedule->get_current_entry().departure_interval );
			numimp_interval.set_limits( 0, 24*60 );
			numimp_interval.set_increment_mode( 1 );
			numimp_interval.add_listener(this);
			add_component(&numimp_interval);
			// room for the widest text, the layout is not recomputed when the value changes
			lb_interval_fmt.set_min_width( proportional_string_width("= 23h59") );
			lb_offset_fmt.set_min_width( proportional_string_width("= 23h59") );
			add_component(&lb_interval_fmt);

			add_component(&lb_offset);
			numimp_offset.set_width( 84 );
			numimp_offset.set_value( schedule->get_current_entry().departure_offset );
			numimp_offset.set_limits( 0, 24*60-1 );
			numimp_offset.set_increment_mode( 1 );
			numimp_offset.add_listener(this);
			add_component(&numimp_offset);
			add_component(&lb_offset_fmt);

			// the rare case of several departures per cycle, e.g. "11,31,41" next to offset 1
			add_component(&lb_extra);
			extra_buf[0] = 0;
			input_extra.set_text( extra_buf, sizeof(extra_buf) );
			input_extra.set_width( 120 );
			input_extra.add_listener(this);
			add_component(&input_extra, 2);
		}
		end_table();
	}

	if(  schedule->allows_hold()  ) {
		// coupling: a train of this line joins a train of that line here
		couple_table = add_table(2,2);
		{
			add_component(&lb_couple);
			init_couple_selector();
			couple_selector.add_listener(this);
			add_component(&couple_selector);

			add_component(&lb_couple_wait);
			numimp_couple_wait.set_width( 84 );
			numimp_couple_wait.set_limits( 0, 24*60 );
			numimp_couple_wait.set_increment_mode( 1 );
			numimp_couple_wait.add_listener(this);
			add_component(&numimp_couple_wait);
		}
		end_table();
	}

	// return tickets
	if(  !env_t::hide_rail_return_ticket  ||  schedule->get_waytype()==road_wt  ||  schedule->get_waytype()==air_wt  ||  schedule->get_waytype()==water_wt  ) {
		//  hide the return ticket on rail stuff, where it causes much trouble
		bt_return.init(button_t::roundbox, "return ticket");
		bt_return.set_tooltip("Add stops for backward travel");
		bt_return.add_listener(this);
		add_component(&bt_return);
	}

	// action button row
	add_table(3,1)->set_force_equal_columns(true);
	bt_add.init(button_t::roundbox_state | button_t::flexible, "Add Stop");
	bt_add.set_tooltip("Appends stops at the end of the schedule");
	bt_add.add_listener(this);
	bt_add.pressed = true;
	add_component(&bt_add);

	bt_insert.init(button_t::roundbox_state | button_t::flexible, "Ins Stop");
	bt_insert.set_tooltip("Insert stop before the current stop");
	bt_insert.add_listener(this);
	bt_insert.pressed = false;
	add_component(&bt_insert);

	bt_remove.init(button_t::roundbox_state | button_t::flexible, "Del Stop");
	bt_remove.set_tooltip("Delete the current stop");
	bt_remove.add_listener(this);
	bt_remove.pressed = false;
	add_component(&bt_remove);
	end_table();

	scrolly.set_show_scroll_x(true);
	scrolly.set_scroll_amount_y(LINESPACE+1);
	add_component(&scrolly);

	mode = adding;
	update_selection();

	set_resizemode(diagonal_resize);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
}


void schedule_gui_t::update_tool(bool set)
{
	if(!set  ||  mode==removing  ||  mode==undefined_mode) {
		// reset tools, if still selected ...
		if(welt->get_tool(player->get_player_nr())==tool_t::general_tool[TOOL_SCHEDULE_ADD]) {
			if(tool_t::general_tool[TOOL_SCHEDULE_ADD]->get_default_param()==(const char *)schedule) {
				welt->set_tool( tool_t::general_tool[TOOL_QUERY], player );
			}
		}
		else if(welt->get_tool(player->get_player_nr())==tool_t::general_tool[TOOL_SCHEDULE_INS]) {
			if(tool_t::general_tool[TOOL_SCHEDULE_INS]->get_default_param()==(const char *)schedule) {
				welt->set_tool( tool_t::general_tool[TOOL_QUERY], player );
			}
		}
	}
	else {
		//  .. or set them again
		if(mode==adding) {
			tool_t::general_tool[TOOL_SCHEDULE_ADD]->set_default_param((const char *)schedule);
			welt->set_tool( tool_t::general_tool[TOOL_SCHEDULE_ADD], player );
		}
		else if(mode==inserting) {
			tool_t::general_tool[TOOL_SCHEDULE_INS]->set_default_param((const char *)schedule);
			welt->set_tool( tool_t::general_tool[TOOL_SCHEDULE_INS], player );
		}
	}
}


void schedule_gui_t::show_extra_offsets(const schedule_entry_t &entry)
{
	// the text field keeps what the player typed while the entry stays the same
	cbuffer_t buf;
	for(  uint8 k=0;  k<entry.extra_offset_count;  k++  ) {
		buf.printf( "%s%d", k ? "," : "", entry.extra_offsets[k] );
	}
	if(  strcmp( buf, extra_buf ) != 0  ) {
		tstrncpy( extra_buf, buf, sizeof(extra_buf) );
		input_extra.set_text( extra_buf, sizeof(extra_buf) ); // resets the cursor
	}
}


void schedule_gui_t::read_extra_offsets()
{
	schedule_entry_t &entry = schedule->entries[schedule->get_current_stop()];
	uint16 offsets[schedule_entry_t::MAX_EXTRA_OFFSETS];
	uint8 n = 0;
	const char *p = entry.has_timetable()  &&  has_line() ? extra_buf : "";
	while(  *p  &&  n < schedule_entry_t::MAX_EXTRA_OFFSETS  ) {
		while(  *p  &&  (*p < '0'  ||  *p > '9')  ) {
			p++;
		}
		if(  *p  ) {
			offsets[n++] = (uint16)atoi( p );
			while(  *p >= '0'  &&  *p <= '9'  ) {
				p++;
			}
		}
	}
	entry.set_extra_offsets( offsets, n );
	// echo the cleaned list back
	extra_buf[0] = 0;
	show_extra_offsets( entry );
}


void schedule_gui_t::init_couple_selector()
{
	couple_selector.clear_elements();
	couple_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("<no coupling>"), SYSCOL_TEXT );
	vector_tpl<linehandle_t> lines;
	player->simlinemgmt.get_lines( schedule->get_type(), &lines );
	FOR( vector_tpl<linehandle_t>, line, lines ) {
		couple_selector.new_component<line_scrollitem_t>( line );
	}
	line_scrollitem_t::sort_mode = line_scrollitem_t::SORT_BY_NAME;
	couple_selector.sort( 1 );
	couple_selector.set_selection( 0 );
	couple_line_count = player->simlinemgmt.get_line_count();
}


bool schedule_gui_t::has_line() const
{
	// a line's own schedule (no convoy) or a convoy that serves a line
	return !cnv.is_bound()  ||  new_line.is_bound();
}


void schedule_gui_t::update_selection()
{
	lb_wait.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	wait_load.disable();
	numimp_wait.disable();
	lb_interval.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	lb_offset.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	lb_extra.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	numimp_interval.disable();
	numimp_offset.disable();
	lb_couple.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	lb_couple_wait.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
	couple_selector.disable();
	numimp_couple_wait.disable();

	if(  !schedule->empty()  ) {
		schedule->set_current_stop( min(schedule->get_count()-1,schedule->get_current_stop()) );
		const uint8 current_stop = schedule->get_current_stop();
		if(  haltestelle_t::get_halt(schedule->entries[current_stop].pos, player).is_bound()  ) {
			lb_load.set_color( SYSCOL_TEXT );
			numimp_load.enable();
			numimp_load.set_value( schedule->entries[current_stop].minimum_loading );

			// timetable slots: only lines have them
			schedule_entry_t &entry = schedule->entries[current_stop];
			if(  entry.departure_interval > 0  &&  entry.departure_offset >= entry.departure_interval  ) {
				entry.departure_offset = entry.departure_interval - 1;
			}
			numimp_interval.set_value( entry.departure_interval );
			numimp_offset.set_limits( 0, entry.departure_interval > 0 ? entry.departure_interval - 1 : 0 );
			numimp_offset.set_value( entry.departure_offset );
			// long intervals read better as hours
			lb_interval_fmt.buf().append("= ");
			schedule_t::append_minutes( lb_interval_fmt.buf(), entry.departure_interval );
			lb_interval_fmt.update();
			lb_offset_fmt.buf().append("= ");
			schedule_t::append_minutes( lb_offset_fmt.buf(), entry.departure_offset );
			lb_offset_fmt.update();
			lb_interval_fmt.set_color( entry.departure_interval > 0  &&  has_line() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED );
			lb_offset_fmt.set_color( entry.departure_interval > 0  &&  has_line() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED );
			show_extra_offsets( entry );
			if(  has_line()  ) {
				lb_interval.set_color( SYSCOL_TEXT );
				numimp_interval.enable();
				if(  entry.departure_interval > 0  ) {
					lb_offset.set_color( SYSCOL_TEXT );
					numimp_offset.enable();
					lb_extra.set_color( SYSCOL_TEXT );
				}
			}

			sint8 wait = 0;
			uint16 wait_minutes = 0;
			if(  schedule->entries[current_stop].minimum_loading>0  ) {
				lb_wait.set_color( SYSCOL_TEXT );
				wait_load.enable();
				numimp_wait.enable();

				wait = schedule->entries[current_stop].waiting_time_shift;
				wait_minutes = schedule->entries[current_stop].waiting_time;
				if(  wait_minutes == 0  &&  wait > 0  &&  welt->has_calendar()  ) {
					// stock fraction of a month from an older schedule: show its minute equivalent
					const sint32 minutes_per_month = welt->get_settings().get_minutes_per_month();
					wait_minutes = (uint16)max( 1, (minutes_per_month + (1 << (15 - wait))) >> (16 - wait) );
				}
			}
			numimp_wait.set_value( wait_minutes );

			for(int i=0; i<wait_load.count_elements(); i++) {
				if (gui_waiting_time_item_t *item = dynamic_cast<gui_waiting_time_item_t*>( wait_load.get_element(i) ) ) {
					if (item->get_wait_shift() == wait) {
						wait_load.set_selection(i);
						break;
					}
				}
			}

			// coupling: the line whose train we join here, and how long to wait for it
			int couple_sel = 0;
			for(  int i=1;  i<couple_selector.count_elements();  i++  ) {
				if(  line_scrollitem_t *li = dynamic_cast<line_scrollitem_t*>( couple_selector.get_element(i) )  ) {
					if(  li->get_line().is_bound()  &&  li->get_line().get_id()==entry.couple_line_id  ) {
						couple_sel = i;
					}
				}
			}
			couple_selector.set_selection( couple_sel );
			numimp_couple_wait.set_value( entry.couple_max_wait );
			lb_couple.set_color( SYSCOL_TEXT );
			couple_selector.enable();
			if(  entry.has_coupling()  &&  welt->has_calendar()  ) {
				lb_couple_wait.set_color( SYSCOL_TEXT );
				numimp_couple_wait.enable();
			}

			if(  !entry.loads()  ) {
				// nothing boards here, so loading rules do not apply
				lb_load.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
				numimp_load.disable();
				lb_wait.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
				wait_load.disable();
				numimp_wait.disable();
			}
		}
		else {
			lb_load.set_color( SYSCOL_BUTTON_TEXT_DISABLED );
			numimp_load.disable();
			numimp_load.set_value( 0 );
			numimp_interval.set_value( 0 );
			numimp_offset.set_value( 0 );
			lb_interval_fmt.buf().clear();
			lb_interval_fmt.update();
			lb_offset_fmt.buf().clear();
			lb_offset_fmt.update();
			extra_buf[0] = 0;
		}
	}
}


/**
 * Mouse clicks are hereby reported to its GUI-Components
 */
bool schedule_gui_t::infowin_event(const event_t *ev)
{
	// couple_selector sits in its own table, so its position is relative to that table
	const scr_coord couple_off = couple_table ? couple_table->get_pos() : scr_coord(0,0);
	if( (ev)->ev_class == EVENT_CLICK  &&  !((ev)->ev_code==MOUSE_WHEELUP  ||  (ev)->ev_code==MOUSE_WHEELDOWN)  &&  !line_selector.getroffen(ev->cx, ev->cy-D_TITLEBAR_HEIGHT)  &&  !couple_selector.getroffen(ev->cx-couple_off.x, ev->cy-D_TITLEBAR_HEIGHT-couple_off.y)  )  {

		// close combo box; we must do it ourselves, since the box does not receive outside events ...
		line_selector.close_box();
		couple_selector.close_box();
	}
	else if(  ev->ev_class == INFOWIN  &&  ev->ev_code == WIN_CLOSE  &&  schedule!=NULL  ) {

		stats->highlight_schedule( false );

		update_tool( false );
		schedule->cleanup();
		schedule->finish_editing();
		// now apply the changes
		if(  cnv.is_bound()  ) {
			// do not send changes if the convoi is about to be deleted
			if(  cnv->get_state() != convoi_t::SELF_DESTRUCT  ) {
				// if a line is selected
				if(  new_line.is_bound()  ) {
					// if the selected line is different to the convoi's line, apply it
					if(  new_line!=cnv->get_line()  ) {
						char id[16];
						sprintf( id, "%i,%i", new_line.get_id(), schedule->get_current_stop() );
						cnv->call_convoi_tool( 'l', id );
					}
					else {
						cbuffer_t buf;
						schedule->sprintf_schedule( buf );
						cnv->call_convoi_tool( 'g', buf );
					}
				}
				else {
					cbuffer_t buf;
					schedule->sprintf_schedule( buf );
					cnv->call_convoi_tool( 'g', buf );
				}

				if(  cnv->in_depot()  ) {
					const grund_t *const ground = welt->lookup( cnv->get_home_depot() );
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
		}
	}
	else if(  ev->ev_class == INFOWIN  &&  (ev->ev_code == WIN_TOP  ||  ev->ev_code == WIN_OPEN)  &&  schedule!=NULL  ) {
		// just to be sure, renew the tools ...
		update_tool( true );
	}

	return gui_frame_t::infowin_event(ev);
}


bool schedule_gui_t::action_triggered( gui_action_creator_t *comp, value_t p)
{
DBG_MESSAGE("schedule_gui_t::action_triggered()","comp=%p combo=%p",comp,&line_selector);

	if(comp == &bt_add) {
		mode = adding;
		bt_add.pressed = true;
		bt_insert.pressed = false;
		bt_remove.pressed = false;
		update_tool( true );
	}
	else if(comp == &bt_insert) {
		mode = inserting;
		bt_add.pressed = false;
		bt_insert.pressed = true;
		bt_remove.pressed = false;
		update_tool( true );
	}
	else if(comp == &bt_remove) {
		mode = removing;
		bt_add.pressed = false;
		bt_insert.pressed = false;
		bt_remove.pressed = true;
		update_tool( false );
	}
	else if(comp == &bt_no_standing) {
		schedule->set_no_standing( !schedule->is_no_standing() );
		update_crowding_buttons();
	}
	else if(comp == &bt_no_overcrowding) {
		schedule->set_no_overcrowding( !schedule->is_no_overcrowding() );
		update_crowding_buttons();
	}
	else if(comp == &numimp_load) {
		if (!schedule->empty()) {
			schedule->entries[schedule->get_current_stop()].minimum_loading = (uint8)p.i;
			update_selection();
		}
	}
	else if(comp == &wait_load) {
		if(!schedule->empty()) {
			if (gui_waiting_time_item_t *item = dynamic_cast<gui_waiting_time_item_t*>( wait_load.get_selected_item())) {
				schedule->entries[schedule->get_current_stop()].waiting_time_shift = item->get_wait_shift();

				update_selection();
			}
		}
	}
	else if(comp == &numimp_wait) {
		if(!schedule->empty()) {
			// minutes replace any stock fraction of a month
			schedule->entries[schedule->get_current_stop()].waiting_time = (uint16)p.i;
			schedule->entries[schedule->get_current_stop()].waiting_time_shift = 0;
			update_selection();
		}
	}
	else if(comp == &numimp_interval) {
		if(!schedule->empty()) {
			schedule->entries[schedule->get_current_stop()].departure_interval = (uint16)p.i;
			update_selection(); // clamps the offset
		}
	}
	else if(comp == &numimp_offset) {
		if(!schedule->empty()) {
			schedule_entry_t &entry = schedule->entries[schedule->get_current_stop()];
			entry.departure_offset = (uint16)p.i;
			// the main offset must not appear in the list as well
			entry.set_extra_offsets( entry.extra_offsets, entry.extra_offset_count );
			update_selection();
		}
	}
	else if(comp == &input_extra) {
		if(!schedule->empty()) {
			read_extra_offsets();
			update_selection();
		}
	}
	else if(comp == &couple_selector) {
		if(!schedule->empty()) {
			schedule_entry_t &entry = schedule->entries[schedule->get_current_stop()];
			line_scrollitem_t *li = dynamic_cast<line_scrollitem_t*>( couple_selector.get_selected_item() );
			const uint16 id = li  &&  li->get_line().is_bound() ? li->get_line().get_id() : 0;
			if(  id  &&  !entry.has_coupling()  &&  entry.couple_max_wait==0  ) {
				// a sensible start: wait a quarter of an hour for the other train
				entry.couple_max_wait = 15;
			}
			entry.couple_line_id = id;
			if(  !id  ) {
				entry.couple_max_wait = 0;
			}
			update_selection();
		}
	}
	else if(comp == &numimp_couple_wait) {
		if(!schedule->empty()) {
			schedule->entries[schedule->get_current_stop()].couple_max_wait = (uint16)p.i;
			update_selection();
		}
	}
	else if(comp == &bt_return) {
		schedule->add_return_way();
	}
	else if(comp == &line_selector) {
		uint32 selection = p.i;
		if(  line_scrollitem_t *li = dynamic_cast<line_scrollitem_t*>(line_selector.get_element(selection))  ) {
			new_line = li->get_line();
			stats->highlight_schedule( false );
			schedule->copy_from( new_line->get_schedule() );
			schedule->start_editing();
		}
		else {
			// remove line
			new_line = linehandle_t();
			line_selector.set_selection( 0 );
		}
	}
	else if(comp == &bt_promote_to_line) {
		// update line schedule via tool!
		tool_t *tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
		cbuffer_t buf;
		buf.printf( "c,0,%i,%ld,", (int)schedule->get_type(), (long)(intptr_t)old_schedule );
		schedule->sprintf_schedule( buf );
		tool->set_default_param(buf);
		welt->set_tool( tool, player );
		// since init always returns false, it is safe to delete immediately
		delete tool;
	}
	else if (comp == stats) {
		if(  gui_schedule_entry_t::is_delete_request(p.i)  ) {
			// middle click on one of the schedule entries: remove it, keep the current mode
			const int line = gui_schedule_entry_t::delete_request_to_index(p.i);
			if(  line >= 0  &&  line < schedule->get_count()  ) {
				stats->highlight_schedule( false );
				schedule->set_current_stop( line );
				schedule->remove();
				update_selection();
			}
		}
		else if(  gui_schedule_entry_t::is_toggle_request(p.i)  ) {
			// click on the stop type badge: next or previous stop type, and select the entry
			const int line = gui_schedule_entry_t::toggle_request_to_index(p.i);
			if(  line >= 0  &&  line < schedule->get_count()  ) {
				const uint8 count = schedule_entry_t::max_stop_type;
				schedule_entry_t &entry = schedule->entries[line];
				do {
					entry.stop_type = (uint8)( (entry.stop_type + (gui_schedule_entry_t::is_toggle_backwards(p.i) ? count-1 : 1)) % count );
				} while(  entry.stop_type == schedule_entry_t::hold  &&  !schedule->allows_hold()  );
				schedule->set_current_stop( line );
				update_selection();
			}
		}
		else {
			// click on one of the schedule entries
			const int line = p.i;

			if(  line >= 0 && line < schedule->get_count()  ) {
				schedule->set_current_stop( line );
				if(  mode == removing  ) {
					stats->highlight_schedule( false );
					schedule->remove();
					action_triggered( &bt_add, value_t() );
				}
				update_selection();
			}
		}
	}
	// recheck lines
	if(  cnv.is_bound()  ) {
		// unequal to line => remove from line ...
		if(  new_line.is_bound()  &&  !schedule->matches(welt,new_line->get_schedule())  ) {
			new_line = linehandle_t();
			line_selector.set_selection(0);
		}
		// only assign old line, when new_line is not equal
		if(  !new_line.is_bound()  &&  old_line.is_bound()  &&   schedule->matches(welt,old_line->get_schedule())  ) {
			new_line = old_line;
			init_line_selector();
		}
	}
	return true;
}


void schedule_gui_t::init_line_selector()
{
	line_selector.clear_elements();
	int selection = 0;
	vector_tpl<linehandle_t> lines;

	player->simlinemgmt.get_lines(schedule->get_type(), &lines);

	// keep assignment with identical schedules
	if(  new_line.is_bound()  &&  !schedule->matches( welt, new_line->get_schedule() )  ) {
		if(  old_line.is_bound()  &&  schedule->matches( welt, old_line->get_schedule() )  ) {
			new_line = old_line;
		}
		else {
			new_line = linehandle_t();
		}
	}
	int offset = 0;
	if(  !new_line.is_bound()  ) {
		selection = 0;
		offset = 1;
		line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("<no line>"), SYSCOL_TEXT ) ;
	}

	FOR(  vector_tpl<linehandle_t>,  line,  lines  ) {
		line_selector.new_component<line_scrollitem_t>(line) ;
		if(  !new_line.is_bound()  ) {
			if(  schedule->matches( welt, line->get_schedule() )  ) {
				selection = line_selector.count_elements()-1;
				new_line = line;
			}
		}
		else if(  new_line == line  ) {
			selection = line_selector.count_elements()-1;
		}
	}

	line_selector.set_selection( selection );
	line_scrollitem_t::sort_mode = line_scrollitem_t::SORT_BY_NAME;
	line_selector.sort( offset );
	old_line_count = player->simlinemgmt.get_line_count();
	last_schedule_count = schedule->get_count();
}



void schedule_gui_t::draw(scr_coord pos, scr_size size)
{
	if(  player->simlinemgmt.get_line_count()!=old_line_count  ||  last_schedule_count!=schedule->get_count()  ) {
		// lines added or deleted
		init_line_selector();
		last_schedule_count = schedule->get_count();
	}
	if(  schedule->allows_hold()  &&  player->simlinemgmt.get_line_count()!=couple_line_count  ) {
		init_couple_selector();
		update_selection();
	}

	// after loading in network games, the schedule might still being updated
	if(  cnv.is_bound()  &&  cnv->get_state()==convoi_t::EDIT_SCHEDULE  &&  schedule->is_editing_finished()  ) {
		assert( convoi_t::EDIT_SCHEDULE==1 ); // convoi_t::EDIT_SCHEDULE is 1
		schedule->start_editing();
		cnv->call_convoi_tool( 's', "1" );
	}

	// a line chosen in the selector brings its own settings
	update_crowding_buttons();

	// always dirty, to cater for shortening of halt names and change of selections
	set_dirty();
	gui_frame_t::draw(pos,size);
}


void schedule_gui_t::update_crowding_buttons()
{
	bt_no_standing.pressed = schedule->is_no_standing();
	// no standing means no overcrowding either
	bt_no_overcrowding.pressed = !schedule->allows_overcrowding();
	bt_no_overcrowding.enable( schedule->allows_standing() );
}


/**
 * Set window size and adjust component sizes and/or positions accordingly
 */
void schedule_gui_t::set_windowsize(scr_size size)
{
	gui_frame_t::set_windowsize(size);
	// manually enlarge size of wait_load combobox
	wait_load.set_size( scr_size(numimp_load.get_size().w, wait_load.get_size().h) );
	numimp_wait.set_size( scr_size(numimp_load.get_size().w, numimp_wait.get_size().h) );
	// four digits plus the arrows need more than the stock 60 pixels
	numimp_interval.set_size( scr_size(max(numimp_load.get_size().w, 84), numimp_interval.get_size().h) );
	numimp_offset.set_size( scr_size(max(numimp_load.get_size().w, 84), numimp_offset.get_size().h) );
	// make scrolly take all of space
	scrolly.set_size( scr_size(scrolly.get_size().w, get_client_windowsize().h - scrolly.get_pos().y - D_MARGIN_BOTTOM));

}


void schedule_gui_t::map_rotate90( sint16 y_size)
{
	schedule->rotate90(y_size);
}


void schedule_gui_t::rdwr(loadsave_t *file)
{
	// this handles only schedules of bound convois
	// lines are handled by line_management_gui_t

	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	// convoy data
	convoi_t::rdwr_convoihandle_t(file, cnv);

	// save edited schedule
	if(  file->is_loading()  ) {
		// dummy types
		schedule = new truck_schedule_t();
	}
	schedule->rdwr(file);

	if(  file->is_loading()  ) {
		if(  cnv.is_bound() ) {

			schedule_t *save_schedule = schedule->copy();

			init(cnv->get_schedule(), cnv->get_owner(), cnv);
			// init replaced schedule, restore
			schedule->copy_from(save_schedule);
			delete save_schedule;

			set_windowsize(size);

			// draw will init editing phase again, has to be synchronized
			cnv->get_schedule()->finish_editing();
			schedule->finish_editing();

			win_set_magic(this, (ptrdiff_t)cnv->get_schedule());
		}
		else {
			player = NULL; // prevent destructor from updating
			destroy_win( this );
			dbg->error( "schedule_gui_t::rdwr", "Could not restore schedule window for (%d)", cnv.get_id() );
		}
	}
}
