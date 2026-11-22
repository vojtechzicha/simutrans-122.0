/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork: the platform window and the Platforms tab of the stop window (documentation/fork-platforms-plan.md,
 * section 5).
 */

#include <string.h>

#include "platform_info.h"
#include "platform_cards.h"
#include "departure_board.h"
#include "halt_info.h"
#include "simwin.h"

#include "../simevent.h"
#include "../simhalt.h"
#include "../simplatform.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simtool.h"
#include "../boden/grund.h"
#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../display/font.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../player/simplay.h"
#include "../sys/simsys.h"
#include "../utils/simstring.h"
#include "../unicode.h"


static karte_ptr_t welt;

static PIXVAL rgb(uint32 c) { return get_system_color( c >> 16, (c >> 8) & 0xFF, c & 0xFF ); }

// the departure board's colours
#define TRAIN_BG      0x0B1F4B
#define TRAIN_HEAD_BG 0x16367A
#define TRAIN_HEAD    0xA9C4F0
#define TRAIN_TEXT    0xFFFFFF
#define TRAIN_VIA     0x9FB8E0
#define TRAIN_DELAY   0xFFD030


// ---------------------------------------------------------------------------------------------
// marking a platform's tiles

static halthandle_t marked_halt;
static koord3d marked_anchor = koord3d::invalid;
static uint8 marked_dir = 0;
static vector_tpl<koord3d> marked_tiles;
static const void *marked_owner = NULL;
// in frames (expire() runs once a frame, before the windows draw), not time: a window in the background may draw less
// often than any fixed time
static uint32 marked_frames = 0;
static uint32 marked_touched = 0;


const void *platform_marker_t::get_owner()
{
	return marked_owner;
}


void platform_marker_t::touch()
{
	marked_touched = marked_frames;
}


void platform_marker_t::expire()
{
	marked_frames++;
	if(  marked_owner  &&  marked_frames - marked_touched > 1  ) {
		unmark();
	}
}


void platform_marker_t::unmark()
{
	FOR( vector_tpl<koord3d>, const &k, marked_tiles ) {
		if(  grund_t *gr = welt->lookup( k )  ) {
			gr->clear_flag( grund_t::marked );
			gr->set_flag( grund_t::dirty );
		}
	}
	marked_tiles.clear();
	marked_halt = halthandle_t();
	marked_anchor = koord3d::invalid;
	marked_owner = NULL;
}


void platform_marker_t::mark(halthandle_t halt, koord3d anchor, uint8 dir, const void *owner)
{
	if(  is_marked( halt, anchor, dir )  ) {
		marked_owner = owner;
		marked_touched = marked_frames;
		return;
	}
	unmark();
	marked_owner = owner;
	marked_touched = marked_frames;
	if(  !halt.is_bound()  ) {
		return;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const sint32 i = platforms_t::find( list, anchor, dir );
	if(  i < 0  ) {
		return;
	}
	FOR( vector_tpl<koord3d>, const &k, list[i]->tiles ) {
		if(  grund_t *gr = welt->lookup( k )  ) {
			gr->set_flag( grund_t::marked );
			gr->set_flag( grund_t::dirty );
			marked_tiles.append( k );
		}
	}
	marked_halt = halt;
	marked_anchor = anchor;
	marked_dir = dir;
}


bool platform_marker_t::is_marked(halthandle_t halt, koord3d anchor, uint8 dir)
{
	return halt.is_bound()  &&  marked_halt == halt  &&  marked_anchor == anchor  &&  marked_dir == dir;
}


// ---------------------------------------------------------------------------------------------
// text twice as large: the font's glyphs drawn as 2x2 boxes, a pixel more between letters

static scr_coord_val draw_text_2x(scr_coord_val x, scr_coord_val y, const char *text, PIXVAL col, bool draw)
{
	const font_t *font = display_get_default_font();
	if(  font == NULL  ||  !font->is_loaded()  ) {
		if(  draw  ) {
			display_proportional_clip_rgb( x, y, text, ALIGN_LEFT, col, true );
		}
		return proportional_string_width( text );
	}
	const scr_coord_val x0 = x;
	const utf8 *p = (const utf8 *)text;
	while(  *p  ) {
		utf32 c = utf8_decoder_t::decode( p );
		if(  c == 0  ) {
			break;
		}
		if(  !font->is_valid_glyph( c )  ) {
			c = 0;
		}
		const uint8 *bitmap = font->get_glyph_bitmap( c );
		const uint8 w = font->get_glyph_width( c );
		if(  draw  &&  bitmap  ) {
			for(  uint32 gy = 0;  gy < GLYPH_BITMAP_HEIGHT;  gy++  ) {
				for(  uint8 gx = 0;  gx < w  &&  gx < 16;  gx++  ) {
					const uint8 bits = gx < 8 ? bitmap[gy] : bitmap[gy + GLYPH_BITMAP_HEIGHT];
					if(  bits & (0x80 >> (gx & 7))  ) {
						display_fillbox_wh_clip_rgb( x + 2 * gx, y + 2 * gy, 2, 2, col, true );
					}
				}
			}
		}
		x += 2 * font->get_glyph_advance( c ) + 1;
	}
	return x - x0;
}


// ---------------------------------------------------------------------------------------------
// the platform window

const platform_t *gui_platform_view_t::get_platform() const
{
	if(  !halt.is_bound()  ) {
		return NULL;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const sint32 i = platforms_t::find( list, anchor, dir );
	return i >= 0 ? list[i] : NULL;
}


scr_size gui_platform_view_t::get_min_size() const
{
	const platform_t *p = get_platform();
	const scr_coord_val panel_h = 2 * LINESPACE + 2 * LINESPACE + 16;
	if(  p == NULL  ) {
		return scr_size( 300, panel_h );
	}
	const scr_size t = platform_cards_t::draw_table( 0, 0, p, 600 * LINESPACE / 11, false, false );
	return scr_size( max( (scr_coord_val)(360 * LINESPACE / 11), t.w ), panel_h + D_V_SPACE + LINESPACE + D_V_SPACE + t.h );
}


void gui_platform_view_t::draw(scr_coord offset)
{
	const platform_t *p = get_platform();
	const scr_coord pos = offset + get_pos();
	const scr_coord_val w = get_size().w;
	if(  p == NULL  ) {
		display_proportional_clip_rgb( pos.x, pos.y, translator::translate( "This platform is gone." ), ALIGN_LEFT, SYSCOL_TEXT, true );
		return;
	}
	// the panel: the number large, then the next departure (as the board shows it) or the lines
	const scr_coord_val panel_h = 2 * LINESPACE + 2 * LINESPACE + 16;
	display_fillbox_wh_clip_rgb( pos.x, pos.y, w, panel_h, rgb( TRAIN_BG ), true );
	const scr_coord_val box = 2 * LINESPACE + 8;
	scr_coord_val tx = pos.x + 6;
	if(  p->number[0]  ) {
		const scr_coord_val nw = max( box, draw_text_2x( 0, 0, p->number, 0, false ) + 8 );
		display_fillbox_wh_clip_rgb( pos.x + 6, pos.y + 6, nw, box, rgb( TRAIN_TEXT ), true );
		draw_text_2x( pos.x + 6 + (nw - draw_text_2x( 0, 0, p->number, 0, false )) / 2, pos.y + 6 + 4, p->number, rgb( TRAIN_BG ), true );
		tx += nw + 10;
	}
	gui_departure_board_view_t::platform_departure_t dep;
	if(  gui_departure_board_view_t::get_next_platform_departure( halt, p, dep )  ) {
		scr_coord_val x = tx;
		const scr_coord_val y = pos.y + 6;
		x += display_proportional_clip_rgb( x, y, dep.time, ALIGN_LEFT, rgb( TRAIN_TEXT ), true ) + 8;
		if(  dep.train[0]  ) {
			x += display_proportional_clip_rgb( x, y, dep.train, ALIGN_LEFT, rgb( TRAIN_TEXT ), true ) + 8;
		}
		if(  dep.line_number[0]  ) {
			// the board's badge: inverted on the train look
			const scr_coord_val bw = proportional_string_width( dep.line_number ) + 2 * D_H_SPACE;
			display_fillbox_wh_clip_rgb( x, y - 1, bw, LINESPACE + 2, rgb( TRAIN_TEXT ), true );
			display_proportional_clip_rgb( x + D_H_SPACE, y, dep.line_number, ALIGN_LEFT, rgb( TRAIN_BG ), true );
		}
		if(  dep.status[0]  ) {
			display_proportional_clip_rgb( pos.x + w - 6, y, dep.status, ALIGN_RIGHT, rgb( dep.late ? TRAIN_DELAY : TRAIN_VIA ), true );
		}
		draw_text_2x( tx, y + LINESPACE + 2, dep.destination, rgb( TRAIN_TEXT ), true );
		if(  dep.via[0]  ) {
			display_proportional_clip_rgb( tx, y + 3 * LINESPACE + 4, dep.via, ALIGN_LEFT, rgb( TRAIN_VIA ), true );
		}
	}
	else {
		display_proportional_clip_rgb( tx, pos.y + 6, translator::translate( "No departures" ), ALIGN_LEFT, rgb( TRAIN_VIA ), true );
	}

	// the lines by next stop, with where they end
	scr_coord_val y = pos.y + panel_h + D_V_SPACE;
	display_proportional_clip_rgb( pos.x, y, translator::translate( "Lines boarding here" ), ALIGN_LEFT, SYSCOL_TEXT_TITLE, true );
	y += LINESPACE + D_V_SPACE;
	if(  p->has_lines()  ) {
		platform_cards_t::draw_table( pos.x, y, p, w, true, false );
	}
	else {
		display_proportional_clip_rgb( pos.x, y, translator::translate( "No line boards here." ), ALIGN_LEFT, rgb( 0x5A5A5A ), true );
	}
}


static vector_tpl<platform_info_t *> open_windows;


platform_info_t::platform_info_t(halthandle_t h, koord3d a, uint8 d) :
	gui_frame_t( "", h->get_owner() ),
	halt( h ),
	anchor( a ),
	dir( d ),
	last_stamp( 0 )
{
	open_windows.append( this );
	view.set_platform( h, a, d );
	set_table_layout( 1, 0 );
	add_component( &view );
	add_table( 2, 1 );
	{
		show_on_map.init( button_t::roundbox_state, "Show on the map" );
		show_on_map.add_listener( this );
		add_component( &show_on_map );
		stop_window.init( button_t::roundbox, "Stop window" );
		stop_window.add_listener( this );
		add_component( &stop_window );
	}
	end_table();
	update_title();
	reset_min_windowsize();
	set_windowsize( get_min_windowsize() );
	set_resizemode( diagonal_resize );
}


platform_info_t::~platform_info_t()
{
	open_windows.remove( this );
	if(  platform_marker_t::is_marked( halt, anchor, dir )  ) {
		platform_marker_t::unmark();
	}
}


void platform_info_t::open(halthandle_t halt, koord3d anchor, uint8 dir)
{
	FOR( vector_tpl<platform_info_t *>, w, open_windows ) {
		if(  w->halt == halt  &&  w->anchor == anchor  &&  w->dir == dir  ) {
			top_win( w );
			return;
		}
	}
	create_win( new platform_info_t( halt, anchor, dir ), w_info, magic_none );
}


void platform_info_t::update_title()
{
	title.clear();
	if(  !halt.is_bound()  ) {
		return;
	}
	const platform_t *p = view.get_platform();
	cbuffer_t name;
	board_names_t::get_stop_name( name, halt->get_name(), p  &&  p->kind == platform_t::TRACK, NULL );
	title.append( name );
	if(  p  ) {
		cbuffer_t what;
		platforms_t::get_platform_text( what, p );
		title.printf( " - %s", what.len() ? (const char *)what : p->get_kind_name() );
	}
	set_name( title );
}


void platform_info_t::draw(scr_coord pos, scr_size size)
{
	if(  !halt.is_bound()  ) {
		destroy_win( this );
		return;
	}
	update_title();
	show_on_map.pressed = platform_marker_t::is_marked( halt, anchor, dir );
	const scr_size min = get_min_size();
	if(  min.w > get_windowsize().w  ||  min.h + D_TITLEBAR_HEIGHT > get_windowsize().h  ) {
		reset_min_windowsize();
		set_windowsize( scr_size( max( min.w, get_windowsize().w ), max( (scr_coord_val)(min.h + D_TITLEBAR_HEIGHT), get_windowsize().h ) ) );
	}
	gui_frame_t::draw( pos, size );
}


bool platform_info_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(  comp == &show_on_map  ) {
		if(  platform_marker_t::is_marked( halt, anchor, dir )  ) {
			platform_marker_t::unmark();
		}
		else {
			platform_marker_t::mark( halt, anchor, dir );
		}
	}
	else if(  comp == &stop_window  ) {
		halt->open_info_window();
	}
	return true;
}


// ---------------------------------------------------------------------------------------------
// where a platform is

scr_size gui_platform_where_t::get_min_size() const
{
	return scr_size( proportional_string_width( "Stand" ) + 12 + proportional_string_width( "northbound" ) + 8 + proportional_string_width( "8888,8888" ) + 4, LINESPACE + 2 );
}


void gui_platform_where_t::draw(scr_coord offset)
{
	if(  !halt.is_bound()  ) {
		return;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const sint32 i = platforms_t::find( list, anchor, dir );
	if(  i < 0  ) {
		return;
	}
	const platform_t *p = list[i];
	scr_coord_val x = offset.x + get_pos().x;
	const scr_coord_val y = offset.y + get_pos().y + 1;
	x += display_proportional_clip_rgb( x, y, p->get_kind_name(), ALIGN_LEFT, SYSCOL_TEXT, true ) + 6;
	if(  p->kind == platform_t::STAND  &&  p->dir  ) {
		platform_cards_t::draw_arrow( x, y + (LINESPACE - 7) / 2, p->dir, 0x000000 );
		x += 11;
		x += display_proportional_clip_rgb( x, y, platforms_t::get_direction_name( p->dir ), ALIGN_LEFT, SYSCOL_TEXT, true ) + 8;
	}
	const koord3d a = p->get_anchor();
	char pos[32];
	sprintf( pos, "%d,%d", a.x, a.y );
	// looks like a link: a click centres the map there
	const scr_coord_val w = display_proportional_clip_rgb( x, y, pos, ALIGN_LEFT, SYSCOL_TEXT_HIGHLIGHT, true );
	display_fillbox_wh_clip_rgb( x, y + LINESPACE - 1, w, 1, SYSCOL_TEXT_HIGHLIGHT, true );
}


bool gui_platform_where_t::infowin_event(const event_t *ev)
{
	if(  IS_LEFTRELEASE( ev )  &&  halt.is_bound()  ) {
		const sint32 i = platforms_t::find( halt->get_platforms(), anchor, dir );
		if(  i >= 0  ) {
			welt->get_viewport()->change_world_position( halt->get_platforms()[i]->get_anchor() );
			return true;
		}
	}
	return false;
}


// ---------------------------------------------------------------------------------------------
// the number editor

static platform_number_editor_t *the_editor = NULL;


platform_number_editor_t::platform_number_editor_t(halthandle_t h, koord3d a, uint8 d) :
	gui_frame_t( "", h->get_owner() ),
	halt( h ),
	anchor( a ),
	dir( d ),
	lines( h, 0 )
{
	the_editor = this;
	text[0] = 0;
	loaded[0] = 0;
	set_table_layout( 1, 0 );
	add_table( 2, 1 );
	{
		add_component( &where );
		add_component( &position );
	}
	end_table();
	add_component( &lines );
	number_row = add_table( 2, 1 );
	{
		new_component<gui_label_t>( "Number:" );
		input.set_text( text, lengthof(text) );
		input.add_listener( this );
		add_component( &input );
	}
	end_table();
	new_component<gui_label_t>( "Enter: keep and next, Shift+Enter: back, Esc: close without keeping", SYSCOL_TEXT );
	add_table( 4, 1 );
	{
		prev.init( button_t::roundbox, "< Back" );
		prev.add_listener( this );
		add_component( &prev );
		next.init( button_t::roundbox, "Next >" );
		next.add_listener( this );
		add_component( &next );
		next_free.init( button_t::roundbox, "Next empty" );
		next_free.set_tooltip( "The next platform without a number" );
		next_free.add_listener( this );
		add_component( &next_free );
		close.init( button_t::roundbox, "Done" );
		close.add_listener( this );
		add_component( &close );
	}
	end_table();
	set_target( a, d );
	reset_min_windowsize();
	set_windowsize( get_min_windowsize() );
	focus_input();
}


void platform_number_editor_t::focus_input()
{
	// the input sits in a row of its own: the focus goes down both levels
	number_row->set_focus( &input );
	set_focus( number_row );
}


platform_number_editor_t::~platform_number_editor_t()
{
	if(  the_editor == this  ) {
		the_editor = NULL;
	}
	if(  platform_marker_t::get_owner() == this  ) {
		platform_marker_t::unmark();
	}
}


void platform_number_editor_t::open(halthandle_t halt, koord3d anchor, uint8 dir)
{
	if(  !halt.is_bound()  ) {
		return;
	}
	if(  the_editor  &&  the_editor->halt == halt  ) {
		the_editor->save();
		the_editor->set_target( anchor, dir );
		top_win( the_editor );
		the_editor->focus_input();
		return;
	}
	if(  the_editor  ) {
		destroy_win( the_editor );
	}
	platform_number_editor_t *e = new platform_number_editor_t( halt, anchor, dir );
	// by the plate, else by the mouse, on the screen
	const sint32 i = e->get_index();
	const scr_rect r = i >= 0 ? platform_cards_t::get_plate_rect( halt, i ) : scr_rect( 0, 0, 0, 0 );
	scr_coord_val x = r.w ? r.x + r.w + 12 : get_mouse_x() + 16;
	scr_coord_val y = r.w ? r.y - D_TITLEBAR_HEIGHT : get_mouse_y() + 8;
	const scr_size s = e->get_windowsize();
	x = max( 0, min( x, display_get_width() - s.w ) );
	y = max( (scr_coord_val)env_t::iconsize.h, min( y, display_get_height() - s.h - win_get_statusbar_height() ) );
	create_win( x, y, e, w_info, magic_none );
	// opening the window takes the focus from the components: give it back, so typing goes into the number
	e->focus_input();
}


sint32 platform_number_editor_t::get_index() const
{
	return halt.is_bound() ? platforms_t::find( halt->get_platforms(), anchor, dir ) : -1;
}


void platform_number_editor_t::set_target(koord3d a, uint8 d)
{
	anchor = a;
	dir = d;
	where.set_platform( halt, a, d );
	const sint32 i = get_index();
	lines.set_platform( halt, i >= 0 ? i : 0 );
	tstrncpy( text, i >= 0 ? halt->get_platforms()[i]->number : "", lengthof(text) );
	tstrncpy( loaded, text, lengthof(loaded) );
	// (selects the whole number, so typing replaces it)
	input.set_text( text, lengthof(text) );
	if(  i >= 0  ) {
		platform_marker_t::mark( halt, a, d, this );
	}
	// bring it into view when it is off the screen
	const scr_coord s = welt->get_viewport()->get_screen_coord( a );
	const scr_coord_val R = get_tile_raster_width();
	// (the ground point of the tile: the middle of its image, three quarters down)
	const scr_coord_val gx = s.x + R / 2, gy = s.y + R * 3 / 4;
	if(  gx < 0  ||  gy < 0  ||  gx > display_get_width()  ||  gy > display_get_height() - win_get_statusbar_height()  ) {
		welt->get_viewport()->change_world_position( a );
	}
}


void platform_number_editor_t::save()
{
	const sint32 i = get_index();
	if(  i < 0  ) {
		return;
	}
	const platform_t *p = halt->get_platforms()[i];
	if(  strcmp( p->number, text ) != 0  ) {
		platform_cards_t::send_number( halt, p, text );
	}
	tstrncpy( loaded, text, lengthof(loaded) );
}


void platform_number_editor_t::step(int delta, bool only_without_number)
{
	save();
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	vector_tpl<uint32> order;
	platforms_t::get_walk_order( list, platform_cards_t::show_empty_stands, order );
	if(  order.empty()  ) {
		return;
	}
	const sint32 cur = get_index();
	sint32 at = -1;
	for(  uint32 k = 0;  k < order.get_count();  k++  ) {
		if(  (sint32)order[k] == cur  ) {
			at = k;
		}
	}
	const sint32 n = order.get_count();
	for(  sint32 k = 1;  k <= n;  k++  ) {
		const sint32 j = at < 0 ? (delta > 0 ? k - 1 : n - k) : ((at + delta * k) % n + n) % n;
		const platform_t *p = list[order[j]];
		if(  !only_without_number  ||  p->number[0] == 0  ) {
			set_target( p->get_anchor(), p->kind == platform_t::STAND ? p->dir : 0 );
			return;
		}
	}
}


bool platform_number_editor_t::infowin_event(const event_t *ev)
{
	if(  ev->ev_class == EVENT_KEYBOARD  ) {
		switch(  ev->ev_code  ) {
			case SIM_KEY_ENTER:
			case SIM_KEY_TAB:
				step( IS_SHIFT_PRESSED( ev ) ? -1 : 1, false );
				return true;
			case SIM_KEY_DOWN:
				step( 1, false );
				return true;
			case SIM_KEY_UP:
				step( -1, false );
				return true;
			default:
				break;
		}
	}
	return gui_frame_t::infowin_event( ev );
}


void platform_number_editor_t::draw(scr_coord pos, scr_size size)
{
	if(  !halt.is_bound()  ) {
		destroy_win( this );
		return;
	}
	platform_cards_t::touch_numbering( halt );
	// a click on a button takes the focus; typing always goes into the number (not allowed while handling the click)
	if(  get_focus() != &input  ) {
		focus_input();
	}
	// keep the mark on our platform, unless the Platforms tab marks the row under the mouse
	if(  platform_marker_t::get_owner() == NULL  ||  platform_marker_t::get_owner() == this  ) {
		platform_marker_t::mark( halt, anchor, dir, this );
	}
	if(  platform_marker_t::get_owner() == this  ) {
		platform_marker_t::touch();
	}
	const sint32 i = get_index();
	vector_tpl<uint32> order;
	platforms_t::get_walk_order( halt->get_platforms(), platform_cards_t::show_empty_stands, order );
	sint32 at = -1;
	for(  uint32 k = 0;  k < order.get_count();  k++  ) {
		if(  (sint32)order[k] == i  ) {
			at = k;
		}
	}
	position.buf().printf( at >= 0 ? translator::translate( "%d of %d" ) : "", at + 1, order.get_count() );
	position.update();
	lines.set_platform( halt, i >= 0 ? i : 0 );
	// the number changed elsewhere ("Stands A, B, C", the tab, another client) while nothing was typed here: show it,
	// else Enter would put the old one back
	if(  i >= 0  &&  strcmp( halt->get_platforms()[i]->number, loaded ) != 0  &&  strcmp( text, loaded ) == 0  ) {
		tstrncpy( text, halt->get_platforms()[i]->number, lengthof(text) );
		tstrncpy( loaded, text, lengthof(loaded) );
		input.set_text( text, lengthof(text) );
	}
	title.clear();
	title.printf( translator::translate( "Numbering %s" ), halt->get_name() );
	set_name( title );
	gui_frame_t::draw( pos, size );
}


bool platform_number_editor_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(  comp == &prev  ) {
		step( -1, false );
	}
	else if(  comp == &next  ) {
		step( 1, false );
	}
	else if(  comp == &next_free  ) {
		step( 1, true );
	}
	else if(  comp == &close  ) {
		save();
		destroy_win( this );
	}
	else if(  comp == &input  ) {
		// the input lost the focus with a changed number
		save();
	}
	return true;
}


// ---------------------------------------------------------------------------------------------
// the Platforms tab


scr_size gui_platform_lines_t::get_min_size() const
{
	return scr_size( 200, LINESPACE + 2 );
}


void gui_platform_lines_t::draw(scr_coord offset)
{
	if(  !halt.is_bound()  ) {
		return;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	if(  index >= list.get_count()  ) {
		return;
	}
	const platform_t *p = list[index];
	scr_coord_val x = offset.x + get_pos().x;
	const scr_coord_val y = offset.y + get_pos().y + 1;
	const scr_coord_val right = x + get_size().w;
	if(  !p->has_lines()  ) {
		display_proportional_clip_rgb( x, y, translator::translate( "no line boards here" ), ALIGN_LEFT, rgb( 0x5A5A5A ), true );
		return;
	}
	vector_tpl<platform_run_t> runs;
	bool first = true;
	FOR( vector_tpl<platform_group_t *>, const g, p->groups ) {
		if(  !first  ) {
			display_fillbox_wh_clip_rgb( x + 4, y, 1, LINESPACE, rgb( 0x8CC7E7 ), true );
			x += 10;
		}
		first = false;
		platforms_t::get_runs( g->members, false, runs );
		FOR( vector_tpl<const platform_member_t *>, const c, runs[0].chips ) {
			if(  x + 30 > right  ) {
				display_proportional_clip_rgb( x, y, "...", ALIGN_LEFT, SYSCOL_TEXT, true );
				return;
			}
			x += platform_cards_t::draw_chip( x, y, c->number, c->wt, true ) + 2;
		}
		platform_cards_t::draw_arrow( x + 2, y + (LINESPACE - 7) / 2, g->out, 0x000000 );
		x += 12;
		cbuffer_t name;
		platforms_t::get_group_name( name, g, true );
		x += display_proportional_clip_rgb( x, y, name, ALIGN_LEFT, SYSCOL_TEXT, true ) + 2;
		if(  x > right  ) {
			return;
		}
	}
	if(  !p->arrivals.empty()  ) {
		if(  !first  ) {
			display_fillbox_wh_clip_rgb( x + 4, y, 1, LINESPACE, rgb( 0x8CC7E7 ), true );
			x += 10;
		}
		x += display_proportional_clip_rgb( x, y, translator::translate( "arr." ), ALIGN_LEFT, SYSCOL_TEXT, true ) + 4;
		platforms_t::get_runs( p->arrivals, false, runs );
		FOR( vector_tpl<const platform_member_t *>, const c, runs[0].chips ) {
			x += platform_cards_t::draw_chip( x, y, c->number, c->wt, true ) + 2;
		}
	}
}


bool gui_platform_lines_t::infowin_event(const event_t *ev)
{
	if(  IS_LEFTRELEASE( ev )  &&  halt.is_bound()  ) {
		const vector_tpl<platform_t *> &list = halt->get_platforms();
		if(  index < list.get_count()  ) {
			const platform_t *p = list[index];
			platform_info_t::open( halt, p->get_anchor(), p->kind == platform_t::STAND ? p->dir : 0 );
			return true;
		}
	}
	return false;
}


gui_platforms_tab_t::gui_platforms_tab_t() :
	empty_shown( false ),
	last_signature( 0 ),
	hover_row( -1 )
{
	set_table_layout( 1, 0 );
	new_component<gui_label_t>( "Click a plate on the map to number it.", SYSCOL_TEXT );
	add_table( 3, 3 );
	{
		chosen.init( button_t::square_state, "Map labels: this stop" );
		chosen.set_tooltip( "Show this stop's platform cards on the map in the view of chosen stops" );
		chosen.add_listener( this );
		add_component( &chosen );
		tracks_form.init( button_t::roundbox, "Tracks: next stop + destination" );
		tracks_form.set_tooltip( "What the track cards on the map show (Shift+click a card switches it too)" );
		tracks_form.add_listener( this );
		add_component( &tracks_form );
		stands_form.init( button_t::roundbox, "Stands: next stop + destination" );
		stands_form.set_tooltip( "What the stand cards on the map show (Shift+click a card switches it too)" );
		stands_form.add_listener( this );
		add_component( &stands_form );
		renumber.init( button_t::roundbox, "Number tracks across" );
		renumber.set_tooltip( "Give the tracks the numbers 1, 2, 3 ... across the station" );
		renumber.add_listener( this );
		new_component<gui_empty_t>();
		add_component( &renumber );
		number_tool.init( button_t::roundbox_state, "Number on map" );
		number_tool.set_tooltip( "Click a track or stand of this stop on the map to number it" );
		number_tool.add_listener( this );
		add_component( &number_tool );
		empty_stands.init( button_t::square_state, "Unused stands" );
		empty_stands.set_tooltip( "Also list, show and number the stands no line stops at" );
		empty_stands.add_listener( this );
		add_component( &empty_stands );
		stands_digits.init( button_t::roundbox, "Stands 1, 2, 3" );
		stands_digits.set_tooltip( "Stands without a number get the next free number 1, 2, 3 ... as you read the map; Shift+click numbers every stand anew" );
		stands_digits.add_listener( this );
		add_component( &stands_digits );
		stands_letters.init( button_t::roundbox, "Stands A, B, C" );
		stands_letters.set_tooltip( "Stands without a number get the next free letter A, B, C ... as you read the map; Shift+click numbers every stand anew" );
		stands_letters.add_listener( this );
		add_component( &stands_letters );
	}
	end_table();
	add_component( &list );
}


gui_platforms_tab_t::~gui_platforms_tab_t()
{
	// the inputs were added, not made by the list: it does not delete them
	list.remove_all();
	FOR( vector_tpl<row_t *>, r, rows ) {
		delete r->input;
		delete r->where;
		delete r;
	}
	if(  platform_marker_t::get_owner() == this  ) {
		platform_marker_t::unmark();
	}
}




void gui_platforms_tab_t::set_halt(halthandle_t h)
{
	halt = h;
	last_signature = 0;
	rebuild();
}


uint32 gui_platforms_tab_t::signature()
{
	uint32 s = 2166136261u;
	if(  !halt.is_bound()  ) {
		return s;
	}
	const vector_tpl<platform_t *> &platforms = halt->get_platforms();
	FOR( vector_tpl<platform_t *>, const p, platforms ) {
		const koord3d a = p->get_anchor();
		// (with whether a line uses it: the tab hides stands no line uses)
		s = (s ^ (uint32)(a.x * 7919 + a.y * 104729 + a.z * 31 + p->dir + (p->has_lines() ? 1u << 20 : 0))) * 16777619u;
		for(  const char *c = p->number;  *c;  c++  ) {
			s = (s ^ (uint8)*c) * 16777619u;
		}
	}
	return s ^ platforms.get_count();
}


void gui_platforms_tab_t::update_buttons()
{
	if(  !halt.is_bound()  ) {
		return;
	}
	const uint8 f = halt->get_platform_flags();
	chosen.pressed = (f & haltestelle_t::PLATFORMS_CHOSEN) != 0;
	tracks_form.set_text( (f & haltestelle_t::PLATFORMS_TRACKS_COMPACT) ? "Tracks: next stop" : "Tracks: next stop + destination" );
	stands_form.set_text( (f & haltestelle_t::PLATFORMS_STANDS_DEST) ? "Stands: next stop + destination" : "Stands: next stop" );
	bool has_tracks = false, has_stands = false;
	FOR( vector_tpl<platform_t *>, const p, halt->get_platforms() ) {
		has_tracks |= p->kind == platform_t::TRACK;
		has_stands |= p->kind == platform_t::STAND;
	}
	tracks_form.set_visible( has_tracks );
	renumber.set_visible( has_tracks );
	stands_form.set_visible( has_stands );
	empty_stands.set_visible( has_stands );
	stands_digits.set_visible( has_stands );
	stands_letters.set_visible( has_stands );
	const bool may = !env_t::networkmode  ||  player_t::check_owner( halt->get_owner(), welt->get_active_player() );
	chosen.enable( may );
	tracks_form.enable( may );
	stands_form.enable( may );
	renumber.enable( may );
	number_tool.enable( may );
	stands_digits.enable( may );
	stands_letters.enable( may );
	number_tool.pressed = tool_number_platform_t::is_working_for( halt );
	empty_stands.pressed = platform_cards_t::show_empty_stands;
}


void gui_platforms_tab_t::rebuild()
{
	list.remove_all();
	FOR( vector_tpl<row_t *>, r, rows ) {
		delete r->input;
		delete r->where;
		delete r;
	}
	rows.clear();
	if(  !halt.is_bound()  ) {
		return;
	}
	const vector_tpl<platform_t *> &platforms = halt->get_platforms();
	empty_shown = platform_cards_t::show_empty_stands;
	list.set_table_layout( 3, 0 );
	list.new_component<gui_label_t>( "No.", SYSCOL_TEXT_TITLE );
	list.new_component<gui_label_t>( "Where", SYSCOL_TEXT_TITLE );
	list.new_component<gui_label_t>( "Lines boarding here, by next stop", SYSCOL_TEXT_TITLE );
	const bool may = !env_t::networkmode  ||  player_t::check_owner( halt->get_owner(), welt->get_active_player() );
	// in the order the numbering walks them: tracks by number, then the stands as you read the map
	vector_tpl<uint32> order;
	platforms_t::get_walk_order( platforms, empty_shown, order );
	FOR( vector_tpl<uint32>, const i, order ) {
		const platform_t *p = platforms[i];
		row_t *r = new row_t();
		tstrncpy( r->text, p->number, lengthof(r->text) );
		r->anchor = p->get_anchor();
		r->dir = p->kind == platform_t::STAND ? p->dir : 0;
		r->index = i;
		r->input = new gui_platform_number_input_t();
		r->input->set_text( r->text, lengthof(r->text) );
		r->input->add_listener( this );
		r->input->set_focusable( may );
		r->where = new gui_platform_where_t();
		r->where->set_platform( halt, r->anchor, r->dir );
		rows.append( r );
		list.add_component( r->input );
		list.add_component( r->where );
		list.new_component<gui_platform_lines_t>( halt, i );
	}
	if(  order.get_count() < platforms.get_count()  &&  !empty_shown  ) {
		uint32 hidden = 0;
		FOR( vector_tpl<platform_t *>, const p, platforms ) {
			hidden += p->kind == platform_t::STAND  &&  !p->has_lines();
		}
		if(  hidden  ) {
			list.new_component<gui_empty_t>();
			gui_label_buf_t *l = list.new_component_span<gui_label_buf_t>( SYSCOL_TEXT, 2 );
			l->buf().printf( translator::translate( "%u stands no line uses are hidden" ), hidden );
			l->update();
		}
	}
	last_signature = signature();
	update_buttons();
	set_size( get_min_size() );
}


void gui_platforms_tab_t::focus_platform(koord3d anchor, uint8 dir)
{
	FOR( vector_tpl<row_t *>, r, rows ) {
		if(  halt.is_bound()  ) {
			const sint32 i = platforms_t::find( halt->get_platforms(), anchor, dir );
			const sint32 j = platforms_t::find( halt->get_platforms(), r->anchor, r->dir );
			if(  i >= 0  &&  i == j  ) {
				list.set_focus( r->input );
				set_focus( &list );
				platform_marker_t::mark( halt, r->anchor, r->dir, this );
				return;
			}
		}
	}
}


void gui_platforms_tab_t::draw(scr_coord offset)
{
	if(  !halt.is_bound()  ) {
		return;
	}
	// shown: the map shows this stop's numbering plates
	platform_cards_t::touch_numbering( halt );
	if(  signature() != last_signature  ||  empty_shown != platform_cards_t::show_empty_stands  ) {
		// keep a number being typed: do not rebuild while an input has the focus
		bool typing = false;
		FOR( vector_tpl<row_t *>, r, rows ) {
			typing |= list.get_focus() == r->input;
		}
		if(  !typing  ) {
			rebuild();
		}
	}
	update_buttons();
	// the row of the plate or card under the mouse on the map
	const sint32 on_map = platform_cards_t::get_hover_platform( halt );
	FOR( vector_tpl<row_t *>, const r, rows ) {
		if(  on_map >= 0  &&  r->index == (uint32)on_map  ) {
			const scr_coord_val y = offset.y + get_pos().y + list.get_pos().y + r->input->get_pos().y;
			display_fillbox_wh_clip_rgb( offset.x + get_pos().x + list.get_pos().x - 2, y - 1, list.get_size().w + 4, r->input->get_size().h + 2, rgb( 0xFFE38A ), true );
		}
	}
	gui_aligned_container_t::draw( offset );

	// pointing at a row marks its platform on the map
	const scr_coord mouse( get_mouse_x() - offset.x - get_pos().x - list.get_pos().x, get_mouse_y() - offset.y - get_pos().y - list.get_pos().y );
	sint32 over = -1;
	for(  uint32 i = 0;  i < rows.get_count();  i++  ) {
		const gui_textinput_t *in = rows[i]->input;
		const scr_coord_val y0 = in->get_pos().y, y1 = y0 + in->get_size().h;
		if(  mouse.y >= y0  &&  mouse.y < y1  &&  mouse.x >= 0  &&  mouse.x < list.get_size().w  ) {
			over = i;
		}
	}
	const bool ours = platform_marker_t::get_owner() == this;
	if(  over != hover_row  ||  (over >= 0  &&  !ours)  ) {
		// a new row, or the row's mark expired while the tab was hidden
		if(  over >= 0  ) {
			platform_marker_t::mark( halt, rows[over]->anchor, rows[over]->dir, this );
		}
		else if(  ours  ) {
			platform_marker_t::unmark();
		}
		hover_row = over;
	}
	if(  platform_marker_t::get_owner() == this  ) {
		// still shown: keep the mark
		platform_marker_t::touch();
	}
}


bool gui_platforms_tab_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(  !halt.is_bound()  ) {
		return true;
	}
	const uint8 f = halt->get_platform_flags();
	if(  comp == &chosen  ) {
		platform_cards_t::send_flags( halt, f ^ haltestelle_t::PLATFORMS_CHOSEN );
	}
	else if(  comp == &tracks_form  ) {
		platform_cards_t::send_flags( halt, f ^ haltestelle_t::PLATFORMS_TRACKS_COMPACT );
	}
	else if(  comp == &stands_form  ) {
		platform_cards_t::send_flags( halt, f ^ haltestelle_t::PLATFORMS_STANDS_DEST );
	}
	else if(  comp == &renumber  ) {
		platform_cards_t::send_renumber( halt );
	}
	else if(  comp == &empty_stands  ) {
		platform_cards_t::show_empty_stands = !platform_cards_t::show_empty_stands;
	}
	else if(  comp == &stands_digits  ||  comp == &stands_letters  ) {
		// Shift+click: every stand anew; a click keeps the numbers already there
		platform_cards_t::send_stand_numbers( halt, comp == &stands_letters, platform_cards_t::show_empty_stands, (event_get_last_control_shift() & 1) != 0 );
	}
	else if(  comp == &number_tool  ) {
		if(  tool_number_platform_t::is_working_for( halt )  ) {
			welt->set_tool( tool_t::general_tool[TOOL_QUERY], welt->get_active_player() );
		}
		else {
			// the tool keeps the pointer to this text
			static char param[16];
			sprintf( param, "%u", halt.get_id() );
			tool_t *tool = tool_t::general_tool[TOOL_NUMBER_PLATFORM];
			tool->set_default_param( param );
			welt->set_tool( tool, welt->get_active_player() );
		}
	}
	else {
		// a number typed (Enter, or the input lost the focus)
		FOR( vector_tpl<row_t *>, r, rows ) {
			if(  comp == r->input  ) {
				const vector_tpl<platform_t *> &platforms = halt->get_platforms();
				const sint32 i = platforms_t::find( platforms, r->anchor, r->dir );
				if(  i >= 0  &&  strcmp( platforms[i]->number, r->text ) != 0  ) {
					platform_cards_t::send_number( halt, platforms[i], r->text );
				}
				break;
			}
		}
	}
	return true;
}
