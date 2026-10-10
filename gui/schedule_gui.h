/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SCHEDULE_GUI_H
#define GUI_SCHEDULE_GUI_H


#include "gui_frame.h"

#include "components/gui_label.h"
#include "components/gui_numberinput.h"
#include "components/gui_textinput.h"
#include "components/gui_combobox.h"
#include "components/gui_button.h"
#include "components/gui_tab_panel.h"
#include "components/action_listener.h"

#include "components/gui_scrollpane.h"

#include "../convoihandle_t.h"
#include "../linehandle_t.h"
#include "simwin.h"
#include "../tpl/vector_tpl.h"


class schedule_t;
class player_t;
class cbuffer_t;
class loadsave_t;
class schedule_gui_stats_t;


/**
 * Tab panel as tall as its tallest page (fork): the settings of the current entry sit in it,
 * and the entry list below takes the rest of the window.
 */
class gui_fixed_height_tab_panel_t : public gui_tab_panel_t
{
public:
	scr_size get_max_size() const OVERRIDE { return scr_size( scr_size::inf.w, get_min_size().h ); }
};


/**
 * GUI for Schedule dialog
 */
class schedule_gui_t : public gui_frame_t, public action_listener_t
{
	enum mode_t {adding, inserting, removing, undefined_mode};

	mode_t mode;

	// only active with lines
	button_t bt_promote_to_line;
	gui_combobox_t line_selector;
	gui_label_buf_t lb_waitlevel;

	// passengers standing or overcrowded when the seats are taken (fork), for the whole schedule
	button_t bt_no_standing, bt_no_overcrowding;

	// always needed
	button_t bt_add, bt_insert, bt_remove; // stop management
	button_t bt_return;

	// settings of the current entry in tabs (fork): timetable, loading, coupling
	gui_fixed_height_tab_panel_t tabs;
	gui_aligned_container_t cont_timetable, cont_loading, cont_coupling;

	gui_label_t lb_wait, lb_load;
	gui_numberinput_t numimp_load;
	gui_combobox_t wait_load;      // stock: fraction of a month
	gui_numberinput_t numimp_wait; // world calendar: minutes
	gui_label_minw_t lb_wait_fmt;  // the same as hours and minutes

	// timetable (fork, world calendar only): departure slots every N minutes plus offset
	gui_label_t lb_interval, lb_offset;
	gui_numberinput_t numimp_interval, numimp_offset;
	gui_label_minw_t lb_interval_fmt, lb_offset_fmt; // the same as hours and minutes
	gui_label_t lb_window;           // how late a convoy may still leave in its slot
	gui_numberinput_t numimp_window;
	gui_label_minw_t lb_window_fmt;  // with "(half the gap)" while it is automatic
	gui_label_t lb_extra;            // more departures per cycle, as a comma separated list
	gui_textinput_t input_extra;
	char extra_buf[64];

	void read_extra_offsets();       // parse input_extra into the current entry
	void show_extra_offsets(const schedule_entry_t &entry);

	/// "= 1h30" next to a minute input (plus the suffix, if any), empty when not shown
	static void show_minutes(gui_label_minw_t &lb, uint16 minutes, bool shown, bool enabled, const char *suffix = NULL);

	/// coupling (fork, rail): at this stop a train of this line joins a train of the chosen line
	gui_label_t lb_couple, lb_couple_wait;
	gui_combobox_t couple_selector; // in cont_coupling, so its position is relative to that page
	gui_numberinput_t numimp_couple_wait;
	gui_label_minw_t lb_couple_wait_fmt;
	uint32 couple_line_count;        // lines in couple_selector, to notice new or deleted lines
	void init_couple_selector();       // all lines of this type but our own (a line never couples with itself)

	/// the schedule belongs to a line (timetable slots only work with lines)
	bool has_line() const;

	schedule_gui_stats_t* stats;
	gui_scrollpane_t scrolly;

	// to add new lines automatically
	uint32 old_line_count;
	uint32 last_schedule_count;

	// set the correct tool now ...
	void update_tool(bool set);

	// changes the waiting/loading levels if allowed
	void update_selection();

	// fork: checkbox states from the schedule
	void update_crowding_buttons();
protected:
	schedule_t *schedule;
	schedule_t* old_schedule;
	player_t *player;
	convoihandle_t cnv;

	linehandle_t new_line, old_line;

	void init(schedule_t* schedule, player_t* player, convoihandle_t cnv);

	/// fork: the line this schedule belongs to (unbound for a convoy without a line)
	virtual linehandle_t get_schedule_line() const;

public:
	schedule_gui_t(schedule_t* schedule = NULL, player_t* player = NULL, convoihandle_t cnv = convoihandle_t());

	virtual ~schedule_gui_t();

	// for updating info ...
	void init_line_selector();

	bool infowin_event(event_t const*) OVERRIDE;

	const char *get_help_filename() const OVERRIDE {return "schedule.txt";}

	/**
	 * Draw the Frame
	 */
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	/**
	 * Set window size and adjust component sizes and/or positions accordingly
	 */
	void set_windowsize(scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	/**
	 * Map rotated, rotate schedules too
	 */
	void map_rotate90( sint16 ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_schedule_rdwr_dummy; }
};

#endif
