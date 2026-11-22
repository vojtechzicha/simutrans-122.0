/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_PLATFORM_INFO_H
#define GUI_PLATFORM_INFO_H


#include "gui_frame.h"
#include "components/action_listener.h"
#include "components/gui_aligned_container.h"
#include "components/gui_button.h"
#include "components/gui_label.h"
#include "components/gui_textinput.h"

#include "../halthandle_t.h"
#include "../dataobj/koord3d.h"
#include "../tpl/vector_tpl.h"
#include "../utils/cbuffer_t.h"


class platform_t;


/**
 * Fork: marks the tiles of one platform on the map (the Platforms tab while pointing at a row, the platform
 * window's "Show on the map"). One platform at a time.
 */
class platform_marker_t
{
public:
	/// owner: who marked it (a Platforms tab: it must keep touching the mark every frame while shown, else expire drops it)
	static void mark(halthandle_t halt, koord3d anchor, uint8 dir, const void *owner = NULL);
	static void unmark();
	static bool is_marked(halthandle_t halt, koord3d anchor, uint8 dir);
	static const void *get_owner();
	static void touch();
	/// every frame: a mark whose owner did not touch it in the last frame goes
	static void expire();
};


/// Fork: the departure panel and the lines of one platform (the platform window)
class gui_platform_view_t : public gui_component_t
{
	halthandle_t halt;
	koord3d anchor;
	uint8 dir;
	scr_size size;
public:
	gui_platform_view_t() : dir(0) {}
	void set_platform(halthandle_t h, koord3d a, uint8 d) { halt = h; anchor = a; dir = d; }
	const platform_t *get_platform() const;
	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


/// Fork: the platform window: the next departure from a platform at window size, and its lines
class platform_info_t : public gui_frame_t, private action_listener_t
{
	halthandle_t halt;
	koord3d anchor;
	uint8 dir;
	gui_platform_view_t view;
	button_t show_on_map, stop_window;
	uint32 last_stamp;
	cbuffer_t title;

	void update_title();

public:
	platform_info_t(halthandle_t halt, koord3d anchor, uint8 dir);
	~platform_info_t();

	/// opens the window of that platform, or brings it to the front
	static void open(halthandle_t halt, koord3d anchor, uint8 dir);

	void draw(scr_coord pos, scr_size size) OVERRIDE;
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
	const char *get_help_filename() const OVERRIDE { return "station.txt"; }
};


/// Fork: the lines of a platform in one line (Platforms tab): by next stop, always named
class gui_platform_lines_t : public gui_component_t
{
	halthandle_t halt;
	uint32 index;
public:
	gui_platform_lines_t(halthandle_t h, uint32 i) : halt(h), index(i) {}
	void set_platform(halthandle_t h, uint32 i) { halt = h; index = i; }
	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return scr_size( scr_size::inf.w, get_min_size().h ); }
	bool infowin_event(const event_t *ev) OVERRIDE;
};


// a number input as wide as a few letters
class gui_platform_number_input_t : public gui_textinput_t
{
public:
	scr_size get_min_size() const OVERRIDE { return scr_size( proportional_string_width( "MMMMM" ) + 8, ::max( (int)(LINESPACE + 4), (int)D_BUTTON_HEIGHT ) ); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

/**
 * Fork: where a platform is, in one line: "Stand  ↗ northbound  2707,1908" (a track: "Track  2510,2070"). A click
 * centres the map on it.
 */
class gui_platform_where_t : public gui_component_t
{
	halthandle_t halt;
	koord3d anchor;
	uint8 dir;
public:
	gui_platform_where_t() : anchor(koord3d::invalid), dir(0) {}
	void set_platform(halthandle_t h, koord3d a, uint8 d) { halt = h; anchor = a; dir = d; }
	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
	bool infowin_event(const event_t *ev) OVERRIDE;
};


/**
 * Fork: the number editor, a small window by a numbering plate on the map. Type the number; Enter (or Down, Tab)
 * keeps it and goes on to the next platform in the walk order (platforms_t::get_walk_order), Shift+Enter (or Up)
 * back. The platform it numbers is marked on the map; the stop shows its plates while it is open.
 */
class platform_number_editor_t : public gui_frame_t, private action_listener_t
{
	halthandle_t halt;
	koord3d anchor;
	uint8 dir;
	gui_platform_where_t where;
	gui_label_buf_t position;
	gui_platform_lines_t lines;
	gui_platform_number_input_t input;
	char text[8];
	char loaded[8];      ///< the number the input was filled with
	button_t prev, next, next_free, close;
	cbuffer_t title;

	gui_aligned_container_t *number_row;

	sint32 get_index() const;
	void set_target(koord3d a, uint8 d);
	void focus_input();
	void save();
	void step(int delta, bool only_without_number);

public:
	platform_number_editor_t(halthandle_t halt, koord3d anchor, uint8 dir);
	~platform_number_editor_t();

	/// opens the editor on that platform (or turns the open one to it), by its plate if it is drawn
	static void open(halthandle_t halt, koord3d anchor, uint8 dir);

	bool infowin_event(const event_t *ev) OVERRIDE;
	void draw(scr_coord pos, scr_size size) OVERRIDE;
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
	const char *get_help_filename() const OVERRIDE { return "station.txt"; }
};


/// Fork: the Platforms tab of the stop window: numbers, the card settings, every platform with its lines
class gui_platforms_tab_t : public gui_aligned_container_t, private action_listener_t
{
	halthandle_t halt;
	button_t chosen, tracks_form, stands_form, renumber, number_tool, empty_stands, stands_digits, stands_letters;
	gui_aligned_container_t list;
	struct row_t {
		gui_textinput_t *input;
		gui_platform_where_t *where;
		char text[8];
		koord3d anchor;
		uint8 dir;
		uint32 index;      ///< into the stop's platforms when the rows were made
	};
	bool empty_shown;          ///< show_empty_stands when the rows were made
	vector_tpl<row_t *> rows;
	uint32 last_signature;     ///< of the platforms the rows were made for
	sint32 hover_row;

	void rebuild();
	uint32 signature();
	void update_buttons();

public:
	gui_platforms_tab_t();
	~gui_platforms_tab_t();

	void set_halt(halthandle_t h);

	/// puts the cursor into the number input of the platform with that anchor and direction
	void focus_platform(koord3d anchor, uint8 dir);

	void draw(scr_coord offset) OVERRIDE;
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


#endif
