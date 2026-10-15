/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_DEPARTURE_BOARD_H
#define GUI_DEPARTURE_BOARD_H


#include "components/gui_component.h"

#include "../halthandle_t.h"
#include "../linehandle_t.h"
#include "../convoihandle_t.h"
#include "../tpl/vector_tpl.h"
#include "../tpl/minivec_tpl.h"
#include "../simtypes.h"


class cbuffer_t;
class schedule_t;
class player_t;


/**
 * Fork: the names a departure board shows, parsed from the names the owner gives lines, convoys
 * and stops (see the comments in departure_board.cc for the formats).
 */
class board_names_t
{
public:
	/// the public line number: "[R14]" in the header of the line name, else a licence number
	/// (100167 -> 167) or a category with a one or two digit number (Ex1/1 -> Ex1); empty if none
	static void get_line_number(cbuffer_t &buf, const char *line_name);

	/// the train category the line name starts with (Os041/1 -> Os), empty if none
	static void get_line_category(cbuffer_t &buf, const char *line_name);

	/// the train number at the start of the convoy name (R 802 "Moravan": ... -> R 802 Moravan),
	/// else the convoy id
	static void get_train_number(cbuffer_t &buf, convoihandle_t cnv);

	/**
	 * The name of a stop as a board shows it: a stop with several names ("Šumperk [R12] <290> /
	 * Šumperk, aut.nádraží") gives the rail one to trains and the "Town, Stop" one to the others,
	 * without line, track or district marks; the town is left out when it is board_town.
	 */
	static void get_stop_name(cbuffer_t &buf, const char *halt_name, bool train, const char *board_town);

	/// the town of the "Town, Stop" name of a stop, empty if it has none
	static void get_stop_town(cbuffer_t &buf, const char *halt_name, bool train);
};


/**
 * Fork: a departure board drawn like the real ones at a station or stop, for one kind of
 * departures (passenger trains, other passenger vehicles, freight).
 *
 * A line with a timetable at the stop gets a row for every slot; the train that will take it shows
 * only once it loads there, stands there after loading or runs to the stop. A slot no train takes
 * shows its growing delay while it stays open and is gone when it closes. Lines without a timetable
 * and convoys without a line show the trains loading there or on their way. The next departure of
 * every line sets the time span of the board; each line then shows its further departures within
 * it, up to five rows.
 */
class gui_departure_board_view_t : public gui_component_t
{
public:
	enum kind_t { KIND_TRAINS = 0, KIND_OTHERS, KIND_FREIGHT, MAX_KINDS };

	/// the kind of departure board the vehicles of this schedule and goods categories appear on
	static kind_t get_kind(const schedule_t *schedule, const minivec_tpl<uint8> &catg_index);
	static kind_t get_kind(convoihandle_t cnv);
	static kind_t get_kind(linehandle_t line);

	/// bit (1 << kind) for every kind of departures served at the halt
	static uint8 get_kinds_at(halthandle_t halt);

	/// what the board shows by itself (no texts of the schedule) for departures from entry: the line number of
	/// line (empty without one) and the destination (empty: not in service)
	static void get_auto_texts(const schedule_t *schedule, uint8 entry, const player_t *owner, linehandle_t line, cbuffer_t &line_number, cbuffer_t &destination);

public:
	struct row_t {
		convoihandle_t cnv;  ///< unbound: a timetable slot no train is known for yet
		linehandle_t line;
		uint16 stream;       ///< index into streams
		uint16 group;        ///< rows of one line (or convoy without a line)
		sint16 alt;          ///< coupled trains leaving together: the other train's row in alt_rows (-1: none)
		sint64 scheduled;    ///< board time of the slot; -1: no timetable
		sint64 expected;     ///< board time it is expected to leave
		sint32 delay;        ///< minutes after the slot, 0 = on time; -1 without a timetable
		bool booked;         ///< it finished loading and booked its slot, but still stands there
		char train[48];

		row_t() : stream(0), group(0), alt(-1), scheduled(-1), expected(0), delay(-1), booked(false) { train[0] = 0; }
	};

private:
	struct stream_t {
		linehandle_t line;
		convoihandle_t cnv;  ///< the convoy, for a convoy without a line
		uint8 entry;         ///< the schedule entry at this halt it departs from
		halthandle_t dest_halt;
		char line_number[32];
		char category[16];
		char destination[128];
		char via[256];
	};

	halthandle_t halt;
	kind_t kind;

	vector_tpl<stream_t> streams;
	vector_tpl<row_t> rows;
	vector_tpl<row_t> alt_rows;  ///< the second train of a coupled pair, shown in turn with the first

	/// calendar minute (or tick block without calendar) of the last rebuild
	sint64 last_refresh;
	uint32 last_line_count, last_convoy_count;

	// column widths and row height, set by layout()
	scr_coord_val col_w[5];
	scr_coord_val row_h, header_h;
	scr_size min_size;

	/// board time now: calendar minutes, or ticks without the calendar
	sint64 now;

	bool is_train_board() const { return kind == KIND_TRAINS; }

	void rebuild();
	void add_line_streams(linehandle_t line, vector_tpl<row_t> &line_rows);
	void add_convoy_stream(convoihandle_t cnv, vector_tpl<row_t> &line_rows);
	void add_stream_rows(const schedule_t *schedule, linehandle_t line, convoihandle_t lineless, uint8 entry, halthandle_t at, vector_tpl<row_t> &stream_rows);
	void add_timed_rows(const schedule_t *schedule, linehandle_t line, uint8 entry, uint8 anchor, vector_tpl<row_t> &stream_rows);
	void estimate_run_times(const schedule_t *schedule, linehandle_t line, uint8 anchor, uint8 stretch, sint64 *run) const;
	bool fill_stream_texts(stream_t &s, const schedule_t *schedule, player_t *owner);
	void merge_coupled_rows();
	void layout();
	void measure_row(const row_t &r, bool &any_via);

	sint64 ticks_to_board_time(sint64 delta_ticks) const;
	void format_time(cbuffer_t &buf, const row_t &row) const;
	sint32 get_row_at(scr_coord_val y) const;

public:
	gui_departure_board_view_t();

	void set_halt(halthandle_t h);
	void set_kind(kind_t k);
	kind_t get_kind() const { return kind; }

	/// rebuilds the rows when the calendar minute changed (or lines or convoys came or went)
	void update(bool force = false);

	void draw(scr_coord offset) OVERRIDE;
	bool infowin_event(event_t const *ev) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return min_size; }
	scr_size get_max_size() const OVERRIDE;
};



/**
 * Fork: what the destination sign of a convoy says, in the look of the departure board: its line number,
 * train number and where its trip goes (the board's rules: the next Terminal or All off stop, sidings,
 * where the schedule turns).
 */
struct convoy_sign_t
{
	char line_number[32];
	char train[48];          ///< train number (trains only)
	char destination[128];   ///< empty: not in service (a run to a siding)
	halthandle_t dest_halt;
	bool train_style;        ///< rail: the blue train board look, else the amber stop look
	waytype_t waytype;

	/// fills the sign (kept for a game minute); false without a schedule
	bool update(convoihandle_t cnv);

	/// the sign in a frame at x, y (train number, line badge, destination; over the vehicle); returns its
	/// width, with draw false only measures
	scr_coord_val draw_row(scr_coord_val x, scr_coord_val y, bool draw) const;

	/// the sign in one line filling area: line badge, then the destination
	void draw_sign(scr_rect area) const;
};


/// Fork: a convoy's destination sign as a component (convoy window), as wide as the component above it
class gui_convoy_sign_t : public gui_component_t
{
	convoihandle_t cnv;
	gui_component_t *width_of;
public:
	gui_convoy_sign_t() : width_of(NULL) {}
	void set_convoy(convoihandle_t c, gui_component_t *same_width) { cnv = c; width_of = same_width; }
	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
