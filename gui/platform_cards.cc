/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork: platform cards on the map, see platform_cards.h and documentation/fork-platforms-plan.md.
 */

#include <string.h>

#include "platform_cards.h"
#include <ctype.h>
#include "../unicode.h"
#include "../descriptor/roadsign_desc.h"
#include "../obj/roadsign.h"
#include "platform_info.h"
#include "simwin.h"

#include "../simhalt.h"
#include "../simplatform.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simtool.h"
#include "../boden/grund.h"
#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../obj/zeiger.h"
#include "../player/simplay.h"
#include "../sys/simsys.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"
#include "../gui/departure_board.h"


static karte_ptr_t welt;


// colours (as the mockups, see tools/mockups/cards.py)
#define CARD_BG      0xD6EBF7
#define CARD_EDGE    0x395D7B
#define CARD_DIV     0x8CC7E7
#define NUM_BG       0x183C4A
#define NAME_COL     0x000000
#define DEST_COL     0x10284A
#define TRAIN_CHIP   0x0B1F4B

#define CHIPS_PER_ROW (6)
#define MAX_CHIP_ROWS (3)
#define MAX_CELLS     (4)

static PIXVAL rgb(uint32 c) { return get_system_color( c >> 16, (c >> 8) & 0xFF, c & 0xFF ); }

static scr_coord_val row_h() { return LINESPACE + 2; }

static void display_objects(halthandle_t halt);


// ---------------------------------------------------------------------------------------------
// small pieces

scr_coord_val platform_cards_t::draw_chip(scr_coord_val x, scr_coord_val y, const char *text, waytype_t wt, bool draw)
{
	const scr_coord_val w = proportional_string_width( text ) + 4;
	if(  draw  ) {
		const bool more = text[0] == '+';
		uint32 bg;
		switch(  wt  ) {
			case track_wt:
			case monorail_wt:
			case maglev_wt:
			case narrowgauge_wt:
				bg = TRAIN_CHIP;
				break;
			default:
				bg = gui_departure_board_view_t::get_badge_color( wt );
				break;
		}
		display_fillbox_wh_clip_rgb( x, y, w, LINESPACE, rgb( more ? CARD_BG : bg ), true );
		display_proportional_clip_rgb( x + 2, y, text, ALIGN_LEFT, rgb( more ? CARD_EDGE : 0xFFFFFF ), true );
	}
	return w;
}


void platform_cards_t::draw_arrow(scr_coord_val x, scr_coord_val y, uint8 dir, uint32 c)
{
	// north goes up right on the screen, east down right, south down left, west up left
	sint8 sx, sy;
	switch(  dir  ) {
		case ribi_t::north: sx =  1; sy = -1; break;
		case ribi_t::east:  sx =  1; sy =  1; break;
		case ribi_t::south: sx = -1; sy =  1; break;
		case ribi_t::west:  sx = -1; sy = -1; break;
		default: return;
	}
	const PIXVAL col = rgb( c );
	for(  int a = 0;  a < 7;  a++  ) {
		const int px = sx > 0 ? a : 6 - a;
		const int py = sy > 0 ? a : 6 - a;
		display_fillbox_wh_clip_rgb( x + px, y + py, 1, 1, col, true );
	}
	// the head: two strokes back from the tip
	const int tx = sx > 0 ? 6 : 0, ty = sy > 0 ? 6 : 0;
	display_fillbox_wh_clip_rgb( x + (sx > 0 ? 2 : 1), y + ty, 4, 1, col, true );
	display_fillbox_wh_clip_rgb( x + tx, y + (sy > 0 ? 2 : 1), 1, 4, col, true );
}


// ---------------------------------------------------------------------------------------------
// a card: cells of items that flow and wrap

enum { IT_CHIP, IT_DEST, IT_ARROW, IT_NAME, IT_PRE, IT_ROW, IT_CELL };

struct card_item_t
{
	uint8 type;
	uint8 dir;
	waytype_t wt;
	const char *text;     ///< points into the platform model (stable while the frame draws), or NULL: own
	char own[160];        ///< text made for the card (a run's destinations): kept with the item

	const char *get_text() const { return text ? text : own; }
};


static scr_coord_val item_width(const card_item_t &it)
{
	switch(  it.type  ) {
		case IT_CHIP:  return platform_cards_t::draw_chip( 0, 0, it.get_text(), it.wt, false );
		case IT_DEST:  return proportional_string_width( it.get_text() ) + 6;
		case IT_ARROW: return 9;
		case IT_NAME:  return proportional_string_width( it.get_text() );
		case IT_PRE:   return proportional_string_width( it.get_text() ) + 2;
		default:       return 0;
	}
}


static char more_buf[64][8];
static uint32 more_used = 0;

static const char *more_text(uint32 n)
{
	char *b = more_buf[more_used++ % lengthof(more_buf)];
	sprintf( b, "+%u", n );
	return b;
}


static void add(vector_tpl<card_item_t> &items, uint8 type, const char *text = NULL, waytype_t wt = invalid_wt, uint8 dir = 0, bool copy = false)
{
	card_item_t it;
	it.type = type;
	it.wt = wt;
	it.dir = dir;
	it.own[0] = 0;
	it.text = copy ? NULL : (text ? text : "");
	if(  copy  ) {
		tstrncpy( it.own, text, lengthof(it.own) );
	}
	items.append( it );
}


// chips of a run: six to a row, more than three rows end in "+N"
static void add_chips(vector_tpl<card_item_t> &items, const platform_run_t &run, uint32 &in_row)
{
	const uint32 n = run.chips.get_count();
	const uint32 room = CHIPS_PER_ROW * MAX_CHIP_ROWS;
	for(  uint32 i = 0;  i < n;  i++  ) {
		if(  in_row == CHIPS_PER_ROW  ) {
			add( items, IT_ROW );
			in_row = 0;
		}
		if(  n > room  &&  i == room - 1  ) {
			add( items, IT_CHIP, more_text( n - i ), invalid_wt, 0, true );
			in_row++;
			return;
		}
		add( items, IT_CHIP, run.chips[i]->number, run.chips[i]->wt );
		in_row++;
	}
}


/**
 * The items of a platform's card: per group a cell (chips, or runs of chips with their destination, then the
 * arrow and the next stop when it is named), the arrivals, at most four cells. compact: chips and arrows only.
 */
static void build_items(const platform_t *p, bool with_destinations, bool compact, vector_tpl<card_item_t> &items)
{
	items.clear();
	vector_tpl<platform_run_t> runs;
	uint32 cells = 0;
	const uint32 total = p->groups.get_count() + (p->arrivals.empty() ? 0 : 1);
	FOR( vector_tpl<platform_group_t *>, const g, p->groups ) {
		if(  cells == MAX_CELLS - 1  &&  total > MAX_CELLS  ) {
			add( items, IT_CELL );
			add( items, IT_CHIP, more_text( total - cells ), invalid_wt, 0, true );
			return;
		}
		if(  cells  ) {
			add( items, IT_CELL );
		}
		cells++;
		platforms_t::get_runs( g->members, with_destinations  &&  !compact, runs );
		uint32 in_row = 0;
		FOR( vector_tpl<platform_run_t>, const &r, runs ) {
			add_chips( items, r, in_row );
			if(  r.destination[0]  ) {
				// the runs are gone once the items are made: the text goes with the item
				add( items, IT_DEST, r.destination, invalid_wt, 0, true );
			}
		}
		if(  g->name_shown  ||  (compact  &&  p->groups.get_count() > 1)  ) {
			add( items, IT_ARROW, NULL, invalid_wt, g->out );
			if(  !compact  ) {
				add( items, IT_NAME, g->heading_name );
			}
		}
	}
	if(  !p->arrivals.empty()  ) {
		if(  cells  ) {
			add( items, IT_CELL );
		}
		add( items, IT_PRE, translator::translate( "arr." ) );
		platforms_t::get_runs( p->arrivals, false, runs );
		uint32 in_row = 0;
		add_chips( items, runs[0], in_row );
	}
}


struct cell_box_t
{
	uint32 first, last;      ///< items [first, last)
	scr_coord_val x, y, w, h;
};


static scr_coord_val number_width(const char *number)
{
	return number  &&  number[0] ? proportional_string_width( number ) + 6 : 0;
}


// lays the cells out: positions relative to the card, returns the card's size
static scr_size layout_flow(const vector_tpl<card_item_t> &items, const char *number, scr_coord_val max_w, vector_tpl<cell_box_t> &boxes)
{
	boxes.clear();
	const scr_coord_val x0 = 4 + (number_width( number ) ? number_width( number ) + 5 : 0);
	scr_coord_val x = x0, y = 3, line_h = 0, w_max = x0 + 4;
	uint32 i = 0;
	while(  i <= items.get_count()  ) {
		// one cell
		cell_box_t b;
		b.first = i;
		scr_coord_val row_w = 0, cell_w = 0;
		uint32 rows = 1;
		while(  i < items.get_count()  &&  items[i].type != IT_CELL  ) {
			if(  items[i].type == IT_ROW  ) {
				cell_w = max( cell_w, row_w - 2 );
				row_w = 0;
				rows++;
			}
			else {
				row_w += item_width( items[i] ) + 2;
			}
			i++;
		}
		cell_w = max( cell_w, row_w - 2 );
		b.last = i;
		b.w = cell_w;
		b.h = rows * row_h() - 2;
		if(  x > x0  &&  x + cell_w > max_w  ) {
			x = x0;
			y += line_h + 4;
			line_h = 0;
		}
		b.x = x;
		b.y = y;
		boxes.append( b );
		x += cell_w + 11;
		line_h = max( line_h, b.h );
		w_max = max( w_max, x - 11 + 4 );
		i++;    // past the IT_CELL (or the end)
	}
	return scr_size( w_max, y + max( line_h, LINESPACE + 2 ) + 3 );
}


static void draw_number_box(scr_coord_val x, scr_coord_val y, const char *number)
{
	if(  number  &&  number[0]  ) {
		display_fillbox_wh_clip_rgb( x + 2, y + 2, number_width( number ), LINESPACE + 2, rgb( NUM_BG ), true );
		display_proportional_clip_rgb( x + 5, y + 3, number, ALIGN_LEFT, rgb( 0xFFFFFF ), true );
	}
}


static void draw_frame(scr_coord_val x, scr_coord_val y, scr_size s)
{
	display_fillbox_wh_clip_rgb( x, y, s.w, s.h, rgb( CARD_EDGE ), true );
	display_fillbox_wh_clip_rgb( x + 1, y + 1, s.w - 2, s.h - 2, rgb( CARD_BG ), true );
}


static void draw_flow(scr_coord_val x, scr_coord_val y, scr_size s, const vector_tpl<card_item_t> &items, const char *number, const vector_tpl<cell_box_t> &boxes)
{
	draw_frame( x, y, s );
	draw_number_box( x, y, number );
	const scr_coord_val x0 = 4 + (number_width( number ) ? number_width( number ) + 5 : 0);
	FOR( vector_tpl<cell_box_t>, const &b, boxes ) {
		if(  b.x > x0  ) {
			display_fillbox_wh_clip_rgb( x + b.x - 6, y + b.y, 1, b.h, rgb( CARD_DIV ), true );
		}
		scr_coord_val cx = x + b.x, cy = y + b.y;
		for(  uint32 i = b.first;  i < b.last;  i++  ) {
			const card_item_t &it = items[i];
			switch(  it.type  ) {
				case IT_ROW:
					cx = x + b.x;
					cy += row_h();
					continue;
				case IT_CHIP:
					platform_cards_t::draw_chip( cx, cy, it.get_text(), it.wt, true );
					break;
				case IT_DEST:
					display_proportional_clip_rgb( cx + 1, cy, it.get_text(), ALIGN_LEFT, rgb( DEST_COL ), true );
					break;
				case IT_ARROW:
					platform_cards_t::draw_arrow( cx + 1, cy + (LINESPACE - 7) / 2, it.dir, NAME_COL );
					break;
				case IT_NAME:
					display_proportional_clip_rgb( cx, cy, it.get_text(), ALIGN_LEFT, rgb( NAME_COL ), true );
					break;
				case IT_PRE:
					display_proportional_clip_rgb( cx, cy, it.get_text(), ALIGN_LEFT, rgb( DEST_COL ), true );
					break;
			}
			cx += item_width( it ) + 2;
		}
	}
}


// ---------------------------------------------------------------------------------------------
// the full form: a table, a row per next stop

scr_size platform_cards_t::draw_table(scr_coord_val x, scr_coord_val y, const platform_t *p, scr_coord_val max_w, bool draw, bool with_number)
{
	const char *number = with_number ? p->number : NULL;
	const scr_coord_val x0 = 4 + (number_width( number ) ? number_width( number ) + 5 : 0);
	scr_coord_val col1 = 0;
	FOR( vector_tpl<platform_group_t *>, const g, p->groups ) {
		cbuffer_t name;
		platforms_t::get_group_name( name, g, true );
		col1 = max( col1, 9 + 3 + proportional_string_width( name ) );
	}
	if(  !p->arrivals.empty()  ) {
		col1 = max( col1, proportional_string_width( translator::translate( "arr." ) ) + 2 );
	}
	const scr_coord_val x1 = x0 + col1 + 10;
	scr_coord_val w = x1 + 4, ry = 3;
	vector_tpl<platform_run_t> runs;
	scr_size size;
	for(  int pass = draw ? 0 : 1;  pass < 2;  pass++  ) {
		// pass 0 measures (to draw the frame first), pass 1 draws or only measures
		const bool paint = pass == 1  &&  draw;
		if(  paint  ) {
			draw_frame( x, y, size );
			draw_number_box( x, y, number );
			display_fillbox_wh_clip_rgb( x + x1 - 6, y + 3, 1, size.h - 6, rgb( CARD_DIV ), true );
		}
		ry = 3;
		w = x1 + 4;
		for(  uint32 gi = 0;  gi <= p->groups.get_count();  gi++  ) {
			const vector_tpl<platform_member_t> *members;
			if(  gi < p->groups.get_count()  ) {
				const platform_group_t *g = p->groups[gi];
				if(  paint  ) {
					draw_arrow( x + x0 + 1, y + ry + (LINESPACE - 7) / 2, g->out, NAME_COL );
					cbuffer_t name;
					platforms_t::get_group_name( name, g, true );
					display_proportional_clip_rgb( x + x0 + 12, y + ry, name, ALIGN_LEFT, rgb( NAME_COL ), true );
				}
				members = &g->members;
				platforms_t::get_runs( *members, true, runs );
			}
			else {
				if(  p->arrivals.empty()  ) {
					break;
				}
				if(  paint  ) {
					display_proportional_clip_rgb( x + x0, y + ry, translator::translate( "arr." ), ALIGN_LEFT, rgb( DEST_COL ), true );
				}
				platforms_t::get_runs( p->arrivals, false, runs );
			}
			scr_coord_val rx = x1;
			FOR( vector_tpl<platform_run_t>, const &r, runs ) {
				scr_coord_val rw = 0;
				FOR( vector_tpl<const platform_member_t *>, const c, r.chips ) {
					rw += draw_chip( 0, 0, c->number, c->wt, false ) + 2;
				}
				rw += r.destination[0] ? proportional_string_width( r.destination ) + 2 : 0;
				if(  rx > x1  &&  rx + rw > max_w  ) {
					rx = x1;
					ry += row_h();
				}
				FOR( vector_tpl<const platform_member_t *>, const c, r.chips ) {
					rx += draw_chip( x + rx, y + ry, c->number, c->wt, paint ) + 2;
				}
				if(  r.destination[0]  ) {
					if(  paint  ) {
						display_proportional_clip_rgb( x + rx + 1, y + ry, r.destination, ALIGN_LEFT, rgb( DEST_COL ), true );
					}
					rx += proportional_string_width( r.destination ) + 2;
				}
				w = max( w, rx + 4 );
				rx += 8;
			}
			ry += row_h();
		}
		size = scr_size( w, max( ry + 1, LINESPACE + 6 ) );
	}
	return size;
}


// ---------------------------------------------------------------------------------------------
// which stops show cards, and where

struct drawn_card_t
{
	scr_rect rect;
	halthandle_t halt;
	uint16 platform;
	bool plate;          ///< a numbering plate, not a card
};

static vector_tpl<drawn_card_t> drawn;
static vector_tpl<halthandle_t> window_halts;
static halthandle_t hover_halt;      // the stop of the card under the mouse in the last frame
static sint32 hover_platform = -1;

bool platform_cards_t::show_empty_stands = false;

static halthandle_t numbering_halt;
// counted in frames, not time: a window in the background may draw less often than any fixed time
static uint32 frames = 0;              // platform_cards_t::display() calls
static uint32 numbering_frame = 0;     // the frame in which the tab or editor last asked for numbering


void platform_cards_t::touch_numbering(halthandle_t halt)
{
	numbering_halt = halt;
	numbering_frame = frames;
}


halthandle_t platform_cards_t::get_numbering_halt()
{
	if(  tool_number_platform_t *t = dynamic_cast<tool_number_platform_t *>( welt->get_tool( welt->get_active_player_nr() ) )  ) {
		const halthandle_t h = t->get_target_halt();
		if(  h.is_bound()  ) {
			return h;
		}
	}
	// windows draw after the map: asked for in the frame before this one (or this one) means still shown
	if(  numbering_halt.is_bound()  &&  frames - numbering_frame <= 1  ) {
		return numbering_halt;
	}
	return halthandle_t();
}


sint32 platform_cards_t::get_hover_platform(halthandle_t halt)
{
	return halt.is_bound()  &&  hover_halt == halt ? hover_platform : -1;
}


scr_rect platform_cards_t::get_plate_rect(halthandle_t halt, uint32 platform)
{
	FOR( vector_tpl<drawn_card_t>, const &d, drawn ) {
		if(  d.plate  &&  d.halt == halt  &&  d.platform == platform  ) {
			return d.rect;
		}
	}
	return scr_rect( 0, 0, 0, 0 );
}


void platform_cards_t::window_opened(halthandle_t halt)
{
	window_halts.append( halt );
}


void platform_cards_t::window_closed(halthandle_t halt)
{
	window_halts.remove( halt );
}


bool platform_cards_t::is_over_card(scr_coord_val mx, scr_coord_val my)
{
	FOR( vector_tpl<drawn_card_t>, const &d, drawn ) {
		if(  d.rect.contains( scr_coord( mx, my ) )  ) {
			return true;
		}
	}
	return false;
}


static bool overlaps(const scr_rect &a, const vector_tpl<scr_rect> &taken)
{
	FOR( vector_tpl<scr_rect>, const &t, taken ) {
		if(  a.x < t.x + t.w  &&  t.x < a.x + a.w  &&  a.y < t.y + t.h  &&  t.y < a.y + a.h  ) {
			return true;
		}
	}
	return false;
}


// screen offsets for the side of a kerb, right of the travel direction, at the neutral zoom of 128 px
static void kerb_offsets(uint8 dir, sint16 &kx, sint16 &ky, sint16 &cx, sint16 &cy)
{
	switch(  dir  ) {
		case ribi_t::west:  kx =  10; ky = -5; cx =  26; cy = -34; break;   // kerb up right, card above right
		case ribi_t::east:  kx = -10; ky =  5; cx = -26; cy =  30; break;   // kerb down left, card below left
		case ribi_t::north: kx =  10; ky =  5; cx =  26; cy =  30; break;   // kerb down right, card below right
		default:            kx = -10; ky = -5; cx = -26; cy = -34; break;   // south: kerb up left, card above left
	}
}


static void leader(scr_coord_val x0, scr_coord_val y0, scr_coord_val x1, scr_coord_val y1)
{
	const PIXVAL col = rgb( CARD_EDGE );
	const int n = max( max( abs( x1 - x0 ), abs( y1 - y0 ) ), 1 );
	for(  int i = 0;  i <= n;  i++  ) {
		display_fillbox_wh_clip_rgb( x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, 1, 1, col, true );
	}
	display_fillbox_wh_clip_rgb( x0 - 1, y0 - 1, 3, 3, rgb( 0xFFFFFF ), true );
	display_fillbox_wh_clip_rgb( x0, y0, 1, 1, col, true );
}


// the cards and name tags placed this frame: a card moves until it covers none of them
static vector_tpl<scr_rect> taken;

static void display_halt(halthandle_t halt, bool full_zoom)
{
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const viewport_t *vp = welt->get_viewport();
	const scr_coord_val R = get_tile_raster_width();
	const uint8 flags = halt->get_platform_flags();

	vector_tpl<card_item_t> items;
	vector_tpl<cell_box_t> boxes;
	for(  uint32 pi = 0;  pi < list.get_count();  pi++  ) {
		const platform_t *p = list[pi];
		if(  !platforms_t::has_card( p )  ||  !p->has_lines()  ) {
			continue;
		}
		const bool hovered = hover_halt == halt  &&  hover_platform == (sint32)pi;
		const bool track = p->kind == platform_t::TRACK;
		const bool table = full_zoom  &&  (hovered  ||  (track  &&  !(flags & haltestelle_t::PLATFORMS_TRACKS_COMPACT)));
		const scr_coord_val max_w = (track ? 420 : 190) * LINESPACE / 11;
		scr_size size;
		if(  table  ) {
			size = platform_cards_t::draw_table( 0, 0, p, hovered ? 560 * LINESPACE / 11 : max_w, false, true );
		}
		else {
			build_items( p, !track  &&  (flags & haltestelle_t::PLATFORMS_STANDS_DEST), !full_zoom, items );
			size = layout_flow( items, p->number, max_w, boxes );
		}

		// where: a track at its end further left on the screen, a stand beside its kerb
		scr_coord_val x, y;
		bool down = true;
		scr_coord_val lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
		bool has_leader = false;
		if(  track  ) {
			const koord3d a = p->tiles[0], b = p->tiles.back();
			const koord3d end = (a.x - a.y) <= (b.x - b.y) ? a : b;
			const grund_t *gr = welt->lookup( end );
			if(  gr == NULL  ||  !gr->is_visible()  ) {
				continue;
			}
			const scr_coord s = vp->get_screen_coord( end );
			x = s.x + R * 56 / 128;
			y = s.y + R * 57 / 128;
		}
		else {
			const koord3d t = p->tiles[p->tiles.get_count() / 2];
			const grund_t *gr = welt->lookup( t );
			if(  gr == NULL  ||  !gr->is_visible()  ) {
				continue;
			}
			const scr_coord s = vp->get_screen_coord( t );
			sint16 kx, ky, cx, cy;
			kerb_offsets( p->dir, kx, ky, cx, cy );
			lx0 = s.x + R / 2 + kx * R / 128;
			ly0 = s.y + R * 3 / 4 + ky * R / 128;
			lx1 = lx0 + cx * R / 128;
			ly1 = ly0 + cy * R / 128;
			x = cx >= 0 ? lx1 : lx1 - size.w;
			y = cy >= 0 ? ly1 : ly1 - size.h;
			down = cy >= 0;
			has_leader = true;
		}
		scr_rect r( x, y, size.w, size.h );
		for(  int k = 0;  k < 40  &&  overlaps( r, taken );  k++  ) {
			r.y += down ? row_h() + 2 : -(row_h() + 2);
		}
		if(  r.x > display_get_width()  ||  r.y > display_get_height()  ||  r.x + r.w < 0  ||  r.y + r.h < 0  ) {
			taken.append( r );
			continue;
		}
		if(  has_leader  ) {
			leader( lx0, ly0, lx1 + (down ? 0 : 0), down ? r.y : r.y + r.h - 1 );
		}
		if(  table  ) {
			platform_cards_t::draw_table( r.x, r.y, p, hovered ? 560 * LINESPACE / 11 : max_w, true, true );
		}
		else {
			draw_flow( r.x, r.y, size, items, p->number, boxes );
		}
		taken.append( r );
		drawn_card_t d;
		d.rect = r;
		d.halt = halt;
		d.platform = pi;
		d.plate = false;
		drawn.append( d );
	}
}


// ---------------------------------------------------------------------------------------------
// numbering plates

#define PLATE_EMPTY_BG   0xE8EEF2
#define PLATE_DIM_BG     0x6E8796
#define PLATE_MARK       0xFFD030

// a plate: the number (or "?") and, for a stand, the arrow of its travel direction; returns its size
static scr_size draw_plate(scr_coord_val x, scr_coord_val y, const platform_t *p, bool mark, bool draw)
{
	const bool has_number = p->number[0] != 0;
	const char *text = has_number ? p->number : "?";
	const bool stand = p->kind == platform_t::STAND  &&  p->dir != 0;
	// big enough to hit with the mouse also for a one-digit track
	const scr_coord_val w = max( proportional_string_width( text ) + 10 + (stand ? 11 : 0), LINESPACE + 10 );
	const scr_coord_val h = LINESPACE + 6;
	if(  draw  ) {
		// a used platform dark, one no line uses greyed, one without a number light with a dashed edge
		const uint32 bg = !has_number ? PLATE_EMPTY_BG : (p->has_lines() ? NUM_BG : PLATE_DIM_BG);
		const uint32 fg = !has_number ? CARD_EDGE : 0xFFFFFF;
		if(  mark  ) {
			display_fillbox_wh_clip_rgb( x - 2, y - 2, w + 4, h + 4, rgb( PLATE_MARK ), true );
		}
		display_fillbox_wh_clip_rgb( x, y, w, h, rgb( has_number ? CARD_EDGE : PLATE_EMPTY_BG ), true );
		display_fillbox_wh_clip_rgb( x + 1, y + 1, w - 2, h - 2, rgb( bg ), true );
		if(  !has_number  ) {
			for(  scr_coord_val k = 0;  k < w;  k += 4  ) {
				display_fillbox_wh_clip_rgb( x + k, y, 2, 1, rgb( CARD_EDGE ), true );
				display_fillbox_wh_clip_rgb( x + k, y + h - 1, 2, 1, rgb( CARD_EDGE ), true );
			}
			for(  scr_coord_val k = 0;  k < h;  k += 4  ) {
				display_fillbox_wh_clip_rgb( x, y + k, 1, 2, rgb( CARD_EDGE ), true );
				display_fillbox_wh_clip_rgb( x + w - 1, y + k, 1, 2, rgb( CARD_EDGE ), true );
			}
		}
		display_proportional_clip_rgb( x + 5, y + 3, text, ALIGN_LEFT, rgb( fg ), true );
		if(  stand  ) {
			platform_cards_t::draw_arrow( x + w - 11, y + (h - 7) / 2, p->dir, fg );
		}
	}
	return scr_size( w, h );
}


static void display_plates(halthandle_t halt)
{
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const viewport_t *vp = welt->get_viewport();
	const scr_coord_val R = get_tile_raster_width();
	for(  uint32 pi = 0;  pi < list.get_count();  pi++  ) {
		const platform_t *p = list[pi];
		if(  !platforms_t::has_card( p )  ||  (p->kind == platform_t::STAND  &&  !p->has_lines()  &&  !platform_cards_t::show_empty_stands)  ) {
			continue;
		}
		const bool mark = platform_marker_t::is_marked( halt, p->get_anchor(), p->kind == platform_t::STAND ? p->dir : 0 )  ||  (hover_halt == halt  &&  hover_platform == (sint32)pi);
		const scr_size size = draw_plate( 0, 0, p, false, false );
		scr_coord_val lx0, ly0, lx1, ly1;
		bool down;
		if(  p->kind == platform_t::TRACK  ) {
			// at its end further left on the screen, as the card
			const koord3d a = p->tiles[0], b = p->tiles.back();
			const koord3d end = (a.x - a.y) <= (b.x - b.y) ? a : b;
			const grund_t *gr = welt->lookup( end );
			if(  gr == NULL  ||  !gr->is_visible()  ) {
				continue;
			}
			const scr_coord s = vp->get_screen_coord( end );
			lx0 = s.x + R / 2;
			ly0 = s.y + R * 3 / 4;
			lx1 = s.x + R * 20 / 128;
			ly1 = s.y + R * 57 / 128;
			down = false;
		}
		else {
			const koord3d t = p->tiles[0];
			const grund_t *gr = welt->lookup( t );
			if(  gr == NULL  ||  !gr->is_visible()  ) {
				continue;
			}
			const scr_coord s = vp->get_screen_coord( t );
			sint16 kx, ky, cx, cy;
			kerb_offsets( p->dir, kx, ky, cx, cy );
			lx0 = s.x + R / 2 + kx * R / 128;
			ly0 = s.y + R * 3 / 4 + ky * R / 128;
			// closer than a card: the plates of one tile's two kerbs stay with their kerb
			lx1 = lx0 + cx * R / 256;
			ly1 = ly0 + cy * R / 256;
			down = cy >= 0;
		}
		scr_rect r( lx1 >= lx0 ? lx1 : lx1 - size.w, down ? ly1 : ly1 - size.h, size.w, size.h );
		for(  int k = 0;  k < 20  &&  overlaps( r, taken );  k++  ) {
			r.y += down ? size.h + 2 : -(size.h + 2);
		}
		if(  r.x > display_get_width()  ||  r.y > display_get_height()  ||  r.x + r.w < 0  ||  r.y + r.h < 0  ) {
			continue;
		}
		leader( lx0, ly0, lx1, down ? r.y : r.y + r.h - 1 );
		draw_plate( r.x, r.y, p, mark, true );
		taken.append( scr_rect( r.x - 2, r.y - 2, r.w + 4, r.h + 4 ) );
		drawn_card_t d;
		d.rect = r;
		d.halt = halt;
		d.platform = pi;
		d.plate = true;
		drawn.append( d );
	}
}


void platform_cards_t::display()
{
	// a Platforms tab that is no longer shown gives up the platform it marked
	platform_marker_t::expire();
	frames++;

	// the card under the mouse in the last frame is drawn in full now
	const scr_coord mouse( get_mouse_x(), get_mouse_y() );
	hover_halt = halthandle_t();
	hover_platform = -1;
	for(  uint32 i = drawn.get_count();  i-- > 0;  ) {
		if(  drawn[i].rect.contains( mouse )  ) {
			hover_halt = drawn[i].halt;
			hover_platform = drawn[i].platform;
			break;
		}
	}
	drawn.clear();

	const scr_coord_val R = get_tile_raster_width();
	const scr_coord_val base = get_base_tile_raster_width();
	if(  R * 4 < base * 3  ) {
		// zoomed out further than one step: no cards
		return;
	}
	const bool full_zoom = R >= base;

	// the displays and signs (physical objects: in every view), at the normal zoom and closer
	if(  full_zoom  &&  roadsign_t::any_platform_object  ) {
		const viewport_t *vpo = welt->get_viewport();
		const scr_coord_val dw = display_get_width(), dh = display_get_height();
		FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
			const scr_coord s = vpo->get_screen_coord( halt->get_basis_pos3d() );
			if(  s.x > -R * 8  &&  s.x < dw + R * 8  &&  s.y > -R * 8  &&  s.y < dh + R * 8  ) {
				display_objects( halt );
			}
		}
	}

	vector_tpl<halthandle_t> shown;
	const viewport_t *vp = welt->get_viewport();
	const scr_coord_val w = display_get_width(), h = display_get_height();

	// numbering a stop: its plates only, no other cards in the way
	const halthandle_t numbering = get_numbering_halt();
	if(  numbering.is_bound()  ) {
		taken.clear();
		if(  env_t::show_names & 1  ) {
			const koord3d basis = numbering->get_basis_pos3d();
			const grund_t *gr = welt->lookup( basis );
			if(  gr  &&  gr->get_text()  ) {
				const scr_coord s = vp->get_screen_coord( basis );
				const scr_coord_val tw = proportional_string_width( gr->get_text() ) + 7;
				const scr_coord_val half = LINESPACE / 2 + 1;
				taken.append( scr_rect( s.x - (tw - R) / 2 - 2, s.y - half - 1, tw, 2 * half + 2 ) );
			}
		}
		display_plates( numbering );
		return;
	}

	if(  env_t::platform_cards != VIEW_OFF  ) {
		FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
			if(  env_t::platform_cards == VIEW_CHOSEN  &&  !(halt->get_platform_flags() & haltestelle_t::PLATFORMS_CHOSEN)  ) {
				continue;
			}
			const scr_coord s = vp->get_screen_coord( halt->get_basis_pos3d() );
			if(  s.x > -R * 6  &&  s.x < w + R * 4  &&  s.y > -R * 6  &&  s.y < h + R * 4  ) {
				shown.append( halt );
			}
		}
	}
	// the stop under the mouse, the stop of the card under the mouse, stops with an open window
	if(  const zeiger_t *z = welt->get_zeiger()  ) {
		if(  const grund_t *gr = welt->lookup( z->get_pos() )  ) {
			if(  gr->get_halt().is_bound()  ) {
				shown.append_unique( gr->get_halt() );
			}
		}
	}
	if(  hover_halt.is_bound()  ) {
		shown.append_unique( hover_halt );
	}
	for(  uint32 i = window_halts.get_count();  i-- > 0;  ) {
		if(  window_halts[i].is_bound()  ) {
			shown.append_unique( window_halts[i] );
		}
		else {
			// its stop was removed while the window was open
			window_halts.remove_at( i );
		}
	}

	// first every shown stop's name tag, so no card covers one
	taken.clear();
	if(  env_t::show_names & 1  ) {
		FOR( vector_tpl<halthandle_t>, const halt, shown ) {
			if(  !halt.is_bound()  ) {
				continue;
			}
			const koord3d basis = halt->get_basis_pos3d();
			const grund_t *gr = welt->lookup( basis );
			if(  gr  &&  gr->get_text()  ) {
				const scr_coord s = vp->get_screen_coord( basis );
				const scr_coord_val tw = proportional_string_width( gr->get_text() ) + 7;
				const scr_coord_val half = LINESPACE / 2 + 1;
				taken.append( scr_rect( s.x - (tw - R) / 2 - 2, s.y - half - 1, tw, 2 * half + 2 ) );
			}
		}
	}
	FOR( vector_tpl<halthandle_t>, const halt, shown ) {
		if(  halt.is_bound()  ) {
			display_halt( halt, full_zoom );
		}
	}
}


bool platform_cards_t::click(scr_coord_val mx, scr_coord_val my, bool shift)
{
	for(  uint32 i = drawn.get_count();  i-- > 0;  ) {
		const drawn_card_t &d = drawn[i];
		if(  !d.rect.contains( scr_coord( mx, my ) )  ||  !d.halt.is_bound()  ) {
			continue;
		}
		const vector_tpl<platform_t *> &list = d.halt->get_platforms();
		if(  d.platform >= list.get_count()  ) {
			return false;
		}
		const platform_t *p = list[d.platform];
		if(  d.plate  ) {
			platform_number_editor_t::open( d.halt, p->get_anchor(), p->kind == platform_t::STAND ? p->dir : 0 );
		}
		else if(  shift  ) {
			// the other form for this kind of platform at the stop
			const uint8 bit = p->kind == platform_t::TRACK ? haltestelle_t::PLATFORMS_TRACKS_COMPACT : haltestelle_t::PLATFORMS_STANDS_DEST;
			send_flags( d.halt, d.halt->get_platform_flags() ^ bit );
		}
		else {
			platform_info_t::open( d.halt, p->get_anchor(), p->kind == platform_t::STAND ? p->dir : 0 );
		}
		return true;
	}
	return false;
}


// ---------------------------------------------------------------------------------------------
// displays and track signs on the platforms (pak objects with the flags PLATFORM_DISPLAY and TRACK_SIGN): the game
// draws the panel and its text, a 3x5 LED font with Czech accents, every letter a pixel lower for every two pixels
// along (the slope of the tile edge), rising instead for platforms running north-south

struct led_glyph_t
{
	char c;
	const char *rows[5];
};

static const led_glyph_t led_glyphs[] = {
	{'A', {".#.", "#.#", "###", "#.#", "#.#"}}, {'B', {"##.", "#.#", "##.", "#.#", "##."}},
	{'C', {".##", "#..", "#..", "#..", ".##"}}, {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
	{'E', {"###", "#..", "##.", "#..", "###"}}, {'F', {"###", "#..", "##.", "#..", "#.."}},
	{'G', {".##", "#..", "#.#", "#.#", ".##"}}, {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
	{'I', {"#", "#", "#", "#", "#"}},           {'J', {"..#", "..#", "..#", "#.#", ".#."}},
	{'K', {"#.#", "#.#", "##.", "#.#", "#.#"}}, {'L', {"#..", "#..", "#..", "#..", "###"}},
	{'M', {"#...#", "##.##", "#.#.#", "#...#", "#...#"}}, {'N', {"#..#", "##.#", "#.##", "#..#", "#..#"}},
	{'O', {".#.", "#.#", "#.#", "#.#", ".#."}}, {'P', {"##.", "#.#", "##.", "#..", "#.."}},
	{'Q', {".#.", "#.#", "#.#", ".#.", "..#"}}, {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
	{'S', {".##", "#..", ".#.", "..#", "##."}}, {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
	{'U', {"#.#", "#.#", "#.#", "#.#", "###"}}, {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
	{'W', {"#...#", "#...#", "#.#.#", "##.##", "#...#"}}, {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
	{'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}}, {'Z', {"###", "..#", ".#.", "#..", "###"}},
	{'0', {"###", "#.#", "#.#", "#.#", "###"}}, {'1', {".#", "##", ".#", ".#", ".#"}},
	{'2', {"##.", "..#", ".#.", "#..", "###"}}, {'3', {"##.", "..#", ".#.", "..#", "##."}},
	{'4', {"#.#", "#.#", "###", "..#", "..#"}}, {'5', {"###", "#..", "##.", "..#", "##."}},
	{'6', {".##", "#..", "###", "#.#", "###"}}, {'7', {"###", "..#", ".#.", ".#.", ".#."}},
	{'8', {"###", "#.#", "###", "#.#", "###"}}, {'9', {"###", "#.#", "###", "..#", "##."}},
	{':', {".", "#", ".", "#", "."}},           {'.', {".", ".", ".", ".", "#"}},
	{',', {".", ".", ".", "#", "#"}},           {'+', {"...", ".#.", "###", ".#.", "..."}},
	{'-', {"..", "..", "##", "..", ".."}},      {'/', {"..#", "..#", ".#.", "#..", "#.."}},
	{'(', {".#", "#.", "#.", "#.", ".#"}},      {')', {"#.", ".#", ".#", ".#", "#."}},
	{'?', {"##.", "..#", ".#.", "...", ".#."}},
};

enum { ACC_NONE, ACC_ACUTE, ACC_CARON, ACC_RING, ACC_DOTS };

// a character as an LED glyph: the capital letter and its accent
static const led_glyph_t *led_find(utf32 c, uint8 &accent)
{
	accent = ACC_NONE;
	struct map_t { utf32 c; char base; uint8 acc; };
	static const map_t letters[] = {
		{0xE1,'A',ACC_ACUTE}, {0xC1,'A',ACC_ACUTE}, {0x10D,'C',ACC_CARON}, {0x10C,'C',ACC_CARON}, {0x10F,'D',ACC_CARON}, {0x10E,'D',ACC_CARON},
		{0xE9,'E',ACC_ACUTE}, {0xC9,'E',ACC_ACUTE}, {0x11B,'E',ACC_CARON}, {0x11A,'E',ACC_CARON}, {0xED,'I',ACC_ACUTE}, {0xCD,'I',ACC_ACUTE},
		{0x148,'N',ACC_CARON}, {0x147,'N',ACC_CARON}, {0xF3,'O',ACC_ACUTE}, {0xD3,'O',ACC_ACUTE}, {0x159,'R',ACC_CARON}, {0x158,'R',ACC_CARON},
		{0x161,'S',ACC_CARON}, {0x160,'S',ACC_CARON}, {0x165,'T',ACC_CARON}, {0x164,'T',ACC_CARON}, {0xFA,'U',ACC_ACUTE}, {0xDA,'U',ACC_ACUTE},
		{0x16F,'U',ACC_RING}, {0x16E,'U',ACC_RING}, {0xFD,'Y',ACC_ACUTE}, {0xDD,'Y',ACC_ACUTE}, {0x17E,'Z',ACC_CARON}, {0x17D,'Z',ACC_CARON},
		{0xE4,'A',ACC_DOTS}, {0xC4,'A',ACC_DOTS}, {0xF6,'O',ACC_DOTS}, {0xD6,'O',ACC_DOTS}, {0xFC,'U',ACC_DOTS}, {0xDC,'U',ACC_DOTS},
		{0x105,'A',ACC_NONE}, {0x107,'C',ACC_ACUTE}, {0x119,'E',ACC_NONE}, {0x142,'L',ACC_NONE}, {0x141,'L',ACC_NONE}, {0x144,'N',ACC_ACUTE},
		{0x15B,'S',ACC_ACUTE}, {0x15A,'S',ACC_ACUTE}, {0x17A,'Z',ACC_ACUTE}, {0x17C,'Z',ACC_ACUTE}, {0x17B,'Z',ACC_ACUTE}, {0x151,'O',ACC_DOTS},
		{0x171,'U',ACC_DOTS}, {0x13A,'L',ACC_ACUTE}, {0x13E,'L',ACC_CARON}, {0xF4,'O',ACC_NONE}, {0xDF,'S',ACC_NONE},
	};
	char base = 0;
	if(  c < 128  ) {
		base = (char)toupper( (int)c );
	}
	else {
		for(  uint32 i = 0;  i < lengthof(letters);  i++  ) {
			if(  letters[i].c == c  ) {
				base = letters[i].base;
				accent = letters[i].acc;
				break;
			}
		}
	}
	for(  uint32 i = 0;  i < lengthof(led_glyphs);  i++  ) {
		if(  led_glyphs[i].c == base  ) {
			return &led_glyphs[i];
		}
	}
	return NULL;
}


static scr_coord_val led_advance(const led_glyph_t *g)
{
	return g ? (scr_coord_val)strlen( g->rows[0] ) + 1 : 2;
}


static scr_coord_val led_width(const char *text)
{
	scr_coord_val w = 0;
	const utf8 *p = (const utf8 *)text;
	while(  *p  ) {
		uint8 acc;
		w += led_advance( led_find( utf8_decoder_t::decode( p ), acc ) );
	}
	return w > 0 ? w - 1 : 0;
}


// how much lower column c of a panel starting at x0 is: half a pixel per pixel along, up for north-south platforms
static inline scr_coord_val slope_off(scr_coord_val c, sint8 slope)
{
	return slope > 0 ? c / 2 : -(c / 2);
}


// LED text from x on a panel whose first column is x0; y is the top of the capitals at x0; returns the x after it
static scr_coord_val led_draw(scr_coord_val x, scr_coord_val y, scr_coord_val x0, sint8 slope, const char *text, PIXVAL col, scr_coord_val max_x)
{
	const utf8 *p = (const utf8 *)text;
	while(  *p  ) {
		uint8 acc;
		const led_glyph_t *g = led_find( utf8_decoder_t::decode( p ), acc );
		const scr_coord_val adv = led_advance( g );
		if(  x + adv - 1 > max_x  ) {
			break;
		}
		if(  g  ) {
			const scr_coord_val dy = y + slope_off( x - x0, slope );
			const int w = strlen( g->rows[0] );
			for(  int r = 0;  r < 5;  r++  ) {
				for(  int c = 0;  c < w;  c++  ) {
					if(  g->rows[r][c] == '#'  ) {
						display_fillbox_wh_clip_rgb( x + c, dy + r, 1, 1, col, true );
					}
				}
			}
			const int m = w / 2;
			switch(  acc  ) {
				case ACC_ACUTE:
					display_fillbox_wh_clip_rgb( x + min( w - 1, m + 1 ), dy - 2, 1, 1, col, true );
					display_fillbox_wh_clip_rgb( x + m, dy - 1, 1, 1, col, true );
					break;
				case ACC_CARON:
					display_fillbox_wh_clip_rgb( x + max( 0, m - 1 ), dy - 2, 1, 1, col, true );
					display_fillbox_wh_clip_rgb( x + min( w - 1, m + 1 ), dy - 2, 1, 1, col, true );
					display_fillbox_wh_clip_rgb( x + m, dy - 1, 1, 1, col, true );
					break;
				case ACC_RING:
					display_fillbox_wh_clip_rgb( x + m, dy - 2, 1, 1, col, true );
					break;
				case ACC_DOTS:
					display_fillbox_wh_clip_rgb( x, dy - 2, 1, 1, col, true );
					display_fillbox_wh_clip_rgb( x + w - 1, dy - 2, 1, 1, col, true );
					break;
			}
		}
		x += adv;
	}
	return x;
}


// a parallelogram along the slope
static void slope_box(scr_coord_val x, scr_coord_val y, scr_coord_val w, scr_coord_val h, sint8 slope, PIXVAL col)
{
	for(  scr_coord_val c = 0;  c < w;  c++  ) {
		display_fillbox_wh_clip_rgb( x + c, y + slope_off( c, slope ), 1, h, col, true );
	}
}


// an inverted box (number or line) of LED text at x, the text's capitals at y; returns its width
static scr_coord_val led_box(scr_coord_val x, scr_coord_val y, scr_coord_val x0, sint8 slope, const char *text, PIXVAL box, PIXVAL col)
{
	const scr_coord_val w = led_width( text ) + 2;
	for(  scr_coord_val c = 0;  c < w;  c++  ) {
		display_fillbox_wh_clip_rgb( x + c, y - 1 + slope_off( x + c - x0, slope ), 1, 7, box, true );
	}
	led_draw( x + 1, y, x0, slope, text, col, x + w );
	return w;
}


static void post(scr_coord_val x, scr_coord_val top, scr_coord_val bottom)
{
	if(  bottom > top  ) {
		display_fillbox_wh_clip_rgb( x, top, 1, bottom - top, rgb( 0x9A9A9A ), true );
		display_fillbox_wh_clip_rgb( x + 1, top, 1, bottom - top, rgb( 0x5A5A5A ), true );
	}
}


/**
 * A display at x, y (the ground point it stands on) for the platforms of its tile: a row each with the number, the
 * time, the line and (every 3 s in turn) the destination or "VIA" and the stops on the way.
 */
static void draw_display(scr_coord_val gx, scr_coord_val gy, sint8 slope, halthandle_t halt, const vector_tpl<const platform_t *> &platforms)
{
	if(  platforms.empty()  ) {
		return;
	}
	const bool rail = platforms[0]->kind == platform_t::TRACK;
	const PIXVAL bg = rgb( rail ? 0x0B1F4B : 0x121212 );
	const PIXVAL text = rgb( rail ? 0xFFFFFF : 0xFFB41E );
	const PIXVAL dim = rgb( rail ? 0x9FB8E0 : 0xA77C2E );
	const PIXVAL late = rgb( rail ? 0xFFD030 : 0xFF5A3C );
	const PIXVAL frame = rgb( 0x2A2A2A );
	const PIXVAL edge = rgb( 0x4A4A4A );
	const bool via_turn = (dr_time() / 3000) & 1;
	const scr_coord_val w = 96;
	const scr_coord_val n = platforms.get_count();
	const scr_coord_val h = 3 + 8 * n;
	const scr_coord_val x0 = gx - w / 2;
	const scr_coord_val y0 = gy - 26 - h - slope_off( w / 2, slope );
	post( x0 + 2, y0 + h, gy - slope_off( w / 2, slope ) );
	post( x0 + w - 4, y0 + h + slope_off( w - 4, slope ), gy + slope_off( w / 2 - 4, slope ) );
	slope_box( x0, y0, w, h, slope, frame );
	slope_box( x0 + 1, y0 + 1 + slope_off( 1, slope ), w - 2, h - 2, slope, bg );
	for(  scr_coord_val c = 0;  c < w;  c++  ) {
		display_fillbox_wh_clip_rgb( x0 + c, y0 - 1 + slope_off( c, slope ), 1, 1, edge, true );
	}
	for(  scr_coord_val i = 0;  i < n;  i++  ) {
		const platform_t *p = platforms[i];
		scr_coord_val x = x0 + 3;
		const scr_coord_val ty = y0 + 3 + 8 * i + 2;
		const scr_coord_val right = x0 + w - 3;
		if(  p->number[0]  ) {
			x += led_box( x, ty, x0, slope, p->number, text, bg ) + 3;
		}
		gui_departure_board_view_t::platform_departure_t dep;
		if(  !gui_departure_board_view_t::get_next_platform_departure( halt, p, dep )  ) {
			led_draw( x, ty, x0, slope, "-", dim, right );
			continue;
		}
		x = led_draw( x, ty, x0, slope, dep.time, text, right ) + 3;
		if(  dep.line_number[0]  &&  x + led_width( dep.line_number ) + 2 < right  ) {
			x += led_box( x, ty, x0, slope, dep.line_number, text, bg ) + 3;
		}
		if(  via_turn  &&  dep.via[0]  ) {
			led_draw( x, ty, x0, slope, dep.via, dim, right );
		}
		else if(  dep.late  &&  via_turn  ) {
			led_draw( x, ty, x0, slope, dep.status, late, right );
		}
		else {
			led_draw( x, ty, x0, slope, dep.destination, text, right );
		}
	}
}


/// a track or stand sign at x, y: the number on a plate, then up to three of its lines
static void draw_track_sign(scr_coord_val gx, scr_coord_val gy, sint8 slope, const platform_t *p)
{
	const bool rail = p->kind == platform_t::TRACK;
	const PIXVAL plate = rgb( rail ? 0x0057A8 : 0x1E64C8 );
	const PIXVAL white = rgb( 0xFFFFFF );
	const PIXVAL strip = rgb( 0x2A2A2A );
	const char *number = p->number[0] ? p->number : "?";
	const scr_coord_val nw = led_width( number ) + 6;
	vector_tpl<platform_run_t> runs;
	vector_tpl<const char *> lines;
	FOR( vector_tpl<platform_group_t *>, const g, p->groups ) {
		platforms_t::get_runs( g->members, false, runs );
		FOR( vector_tpl<const platform_member_t *>, const c, runs[0].chips ) {
			bool seen = false;
			FOR( vector_tpl<const char *>, const l, lines ) {
				seen |= strcmp( l, c->number ) == 0;
			}
			if(  !seen  &&  lines.get_count() < 3  ) {
				lines.append( c->number );
			}
		}
	}
	scr_coord_val sw = 0;
	FOR( vector_tpl<const char *>, const l, lines ) {
		sw += led_width( l ) + 2 + 3;
	}
	sw = sw ? sw + 1 : 0;
	const scr_coord_val h = 11;
	const scr_coord_val x0 = gx - (nw + sw) / 2;
	const scr_coord_val y0 = gy - 20 - h - slope_off( (nw + sw) / 2, slope );
	post( x0 + 3, y0 + h + slope_off( 3, slope ), gy - slope_off( (nw + sw) / 2 - 3, slope ) );
	slope_box( x0, y0, nw, h, slope, white );
	slope_box( x0 + 1, y0 + 1 + slope_off( 1, slope ), nw - 2, h - 2, slope, plate );
	led_draw( x0 + 3, y0 + 3, x0, slope, number, white, x0 + nw );
	if(  sw  ) {
		for(  scr_coord_val c = 0;  c < sw;  c++  ) {
			display_fillbox_wh_clip_rgb( x0 + nw + c, y0 + 1 + slope_off( nw + c, slope ), 1, h - 2, strip, true );
		}
		scr_coord_val x = x0 + nw + 2;
		FOR( vector_tpl<const char *>, const l, lines ) {
			x += led_box( x, y0 + 3, x0, slope, l, white, rgb( rail ? 0x0B1F4B : 0x1E64C8 ) ) + 3;
		}
	}
}


// the displays and signs at the tiles of a stop on the screen
static void display_objects(halthandle_t halt)
{
	const viewport_t *vp = welt->get_viewport();
	const scr_coord_val R = get_tile_raster_width();
	// the platforms only for a stop with such an object (most have none: no model is built for them)
	const vector_tpl<platform_t *> *plist = NULL;
	FOR( slist_tpl<haltestelle_t::tile_t>, const &t, halt->get_tiles() ) {
		const grund_t *gr = t.grund;
		if(  !gr->is_visible()  ) {
			continue;
		}
		const roadsign_t *rs = gr->find<roadsign_t>();
		if(  rs == NULL  ||  !rs->get_desc()->is_platform_object()  ) {
			continue;
		}
		const koord3d pos = gr->get_pos();
		const scr_coord s0 = vp->get_screen_coord( pos );
		if(  s0.x < -R  ||  s0.x > display_get_width() + R  ||  s0.y < -R  ||  s0.y > display_get_height() + R  ) {
			continue;
		}
		if(  plist == NULL  ) {
			plist = &halt->get_platforms();
		}
		const vector_tpl<platform_t *> &list = *plist;
		vector_tpl<const platform_t *> here;
		FOR( vector_tpl<platform_t *>, const p, list ) {
			if(  p->contains( pos )  &&  (p->kind != platform_t::STAND  ||  p->dead_end  ||  (p->dir & rs->get_dir()))  ) {
				here.append( p );
			}
		}
		if(  here.empty()  ) {
			continue;
		}
		const scr_coord s = vp->get_screen_coord( pos );
		if(  s.x < -R  ||  s.x > display_get_width() + R  ||  s.y < -R  ||  s.y > display_get_height() + R  ) {
			continue;
		}
		const sint8 slope = (here[0]->axis == ribi_t::northsouth) ? -1 : 1;
		const scr_coord_val gx = s.x + R / 2, gy = s.y + R * 3 / 4;
		if(  rs->get_desc()->is_platform_display()  ) {
			draw_display( gx, gy, slope, halt, here );
		}
		else {
			// a sign for each platform of the tile, side by side
			for(  uint32 i = 0;  i < here.get_count();  i++  ) {
				draw_track_sign( gx + (scr_coord_val)(i * 2 - (here.get_count() - 1)) * R / 8, gy, slope, here[i] );
			}
		}
	}
}


// ---------------------------------------------------------------------------------------------
// changes go through a tool, so they work in network games

static void send(const char *param)
{
	tool_t *tool = create_tool( TOOL_CHANGE_PLATFORMS | SIMPLE_TOOL );
	tool->set_default_param( param );
	welt->set_tool( tool, welt->get_active_player() );
	// since init() of this tool returns always false, it is safe to delete it
	delete tool;
}


void platform_cards_t::send_flags(halthandle_t halt, uint8 flags)
{
	cbuffer_t buf;
	buf.printf( "f,%u,%u", halt.get_id(), flags );
	send( buf );
}


void platform_cards_t::send_number(halthandle_t halt, const platform_t *p, const char *text)
{
	cbuffer_t buf;
	const koord3d a = p->get_anchor();
	buf.printf( "n,%u,%d,%d,%d,%u,%s", halt.get_id(), a.x, a.y, a.z, p->kind == platform_t::STAND ? p->dir : 0, text );
	send( buf );
}


void platform_cards_t::send_renumber(halthandle_t halt)
{
	cbuffer_t buf;
	buf.printf( "a,%u", halt.get_id() );
	send( buf );
}


void platform_cards_t::send_stand_numbers(halthandle_t halt, bool letters, bool with_empty_stands, bool all)
{
	cbuffer_t buf;
	buf.printf( "s,%u,%d,%d,%d", halt.get_id(), letters ? 1 : 0, with_empty_stands ? 1 : 0, all ? 1 : 0 );
	send( buf );
}
