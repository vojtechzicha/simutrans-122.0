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
	/// owner: who marked it (a Platforms tab: it must keep touching the mark while shown, else expire drops it)
	static void mark(halthandle_t halt, koord3d anchor, uint8 dir, const void *owner = NULL);
	static void unmark();
	static bool is_marked(halthandle_t halt, koord3d anchor, uint8 dir);
	static const void *get_owner();
	static void touch();
	/// every frame: a mark whose owner stopped touching it for 300 ms goes
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
	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return scr_size( scr_size::inf.w, get_min_size().h ); }
	bool infowin_event(const event_t *ev) OVERRIDE;
};


/// Fork: the Platforms tab of the stop window: numbers, the card settings, every platform with its lines
class gui_platforms_tab_t : public gui_aligned_container_t, private action_listener_t
{
	halthandle_t halt;
	button_t chosen, tracks_form, stands_form, renumber, number_tool;
	gui_aligned_container_t list;
	struct row_t {
		gui_textinput_t *input;
		char text[8];
		koord3d anchor;
		uint8 dir;
	};
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
