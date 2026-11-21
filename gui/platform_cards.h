/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_PLATFORM_CARDS_H
#define GUI_PLATFORM_CARDS_H


#include "../halthandle_t.h"
#include "../simtypes.h"
#include "../display/scr_coord.h"
#include "../tpl/vector_tpl.h"


class platform_t;
struct platform_group_t;


/**
 * Fork: platform cards on the map (documentation/fork-platforms-plan.md, section 4). Every track and stand of a
 * shown stop gets a card with its number and the lines that board there by next stop, with an arrow the way they
 * leave. Shown are the stops of the view (env_t::platform_cards: off, chosen stops, all stops), the stop under the
 * mouse and stops with an open window. Pointing at a card draws it in full; a click (query tool) opens the
 * platform window, Shift+click switches the stop's form for that kind of platform.
 */
class platform_cards_t
{
public:
	enum { VIEW_OFF = 0, VIEW_CHOSEN = 1, VIEW_ALL = 2 };

	/// draws the cards of the shown stops (main view, after the overlays)
	static void display();

	/// a click on the map with the query tool; true if it hit a card (and was used)
	static bool click(scr_coord_val mx, scr_coord_val my, bool shift);

	/// is the mouse over a card drawn in the last frame
	static bool is_over_card(scr_coord_val mx, scr_coord_val my);

	/// stop windows open (their stops always show cards)
	static void window_opened(halthandle_t halt);
	static void window_closed(halthandle_t halt);

	/// sends a change of the stop's platforms through the tool (network safe): see tool_change_platforms_t
	static void send_flags(halthandle_t halt, uint8 flags);
	static void send_number(halthandle_t halt, const platform_t *p, const char *text);
	static void send_renumber(halthandle_t halt);

	/// draws a line chip at x, y (LINESPACE high); returns its width; draw false: only measures
	static scr_coord_val draw_chip(scr_coord_val x, scr_coord_val y, const char *text, waytype_t wt, bool draw);

	/// draws a 7x7 arrow pointing the way dir goes on the screen (one ribi direction) with its top left at x, y
	static void draw_arrow(scr_coord_val x, scr_coord_val y, uint8 dir, uint32 rgb);

	/// draws the full form of a platform (a row per next stop, every line with where it ends) at x, y as a
	/// table; returns its size, draw false only measures
	static scr_size draw_table(scr_coord_val x, scr_coord_val y, const platform_t *p, scr_coord_val max_w, bool draw, bool with_number);
};


#endif
