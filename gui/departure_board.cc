/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork: departure board of a stop, drawn like the boards at real stations and stops.
 *
 * Names follow the owner's conventions:
 * - lines: "R14B !! [R14] Ústí n/L hl.n. - Liberec", "Os041/1 !! Hr.Králové hl.n. - Turnov",
 *   "100167 Praha,Na Knížecí-Praha,Nem.Na Homolce", "[C] Praha,Letňany - Praha,Háje": a code, markers
 *   (!, !!, <SK>, {EC}) and the public line number in brackets, then the route;
 * - convoys: "R 802 "Moravan": Olomouc ... - Brno ...", "Os 3521: Rýmařov - Opava východ";
 * - stops: "Šumperk [M1,R12] <290,291> / Šumperk, aut.nádraží", "Borotice [PB], Čelina, rozcestí":
 *   names for each mode joined by " / ", with lines in [] and (), timetable tracks in <> and the
 *   district in [].
 */

#include <algorithm>
#include <string.h>

#include "departure_board.h"

#include "gui_theme.h"

#include "../simworld.h"
#include "../simconvoi.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../simevent.h"
#include "../simintr.h"

#include "../bauer/goods_manager.h"
#include "../dataobj/schedule.h"
#include "../dataobj/route.h"
#include "../dataobj/translator.h"
#include "../display/simgraph.h"
#include "../player/simplay.h"
#include "../sys/simsys.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"
#include "../vehicle/simvehicle.h"


// rows of one line (or one convoy without a line) on a board
#define MAX_ROWS_PER_LINE (5)
// vias shown after the destination
#define MAX_VIAS_TRAIN (3)
#define MAX_VIAS_OTHER (2)
// slots of a timetable looked at, and how far ahead (calendar minutes)
#define MAX_SLOTS (64)
#define SLOT_HORIZON (1440)


static karte_ptr_t welt;


// the later of two board times (min/max of simtypes.h take int)
static sint64 later(sint64 a, sint64 b)
{
	return a > b ? a : b;
}


/* ---------------------------------------------------------------------------------------------
 * names
 */

static bool is_alpha(char c) { return (c >= 'A'  &&  c <= 'Z')  ||  (c >= 'a'  &&  c <= 'z'); }
static bool is_digit(char c) { return c >= '0'  &&  c <= '9'; }
static bool is_upper(char c) { return c >= 'A'  &&  c <= 'Z'; }


// the character after the group that starts at p (the closing one), or the end of the string
static const char *skip_group(const char *p, char close)
{
	const char *end = strchr( p, close );
	return end ? end + 1 : p + strlen( p );
}


static const char *skip_spaces(const char *p)
{
	while(  *p == ' '  ) {
		p++;
	}
	return p;
}


/**
 * The header of a line name: the code (first word) and the bracket text, after skipping the
 * markers <XX>, {XX}, ! and !!. The header ends at the first other word (the route begins).
 */
static void parse_line_header(const char *name, const char *&code, size_t &code_len, const char *&bracket, size_t &bracket_len)
{
	code = NULL;
	code_len = 0;
	bracket = NULL;
	bracket_len = 0;
	const char *p = skip_spaces( name );
	while(  *p  ) {
		if(  *p == '<'  ) {
			p = skip_spaces( skip_group( p, '>' ) );
		}
		else if(  *p == '{'  ) {
			p = skip_spaces( skip_group( p, '}' ) );
		}
		else if(  *p == '!'  ) {
			while(  *p == '!'  ) {
				p++;
			}
			p = skip_spaces( p );
		}
		else if(  *p == '['  ) {
			const char *end = strchr( p, ']' );
			if(  end  ) {
				const char *b = skip_spaces( p + 1 );
				const char *e = end;
				while(  e > b  &&  e[-1] == ' '  ) {
					e--;
				}
				if(  e > b  ) {
					bracket = b;
					bracket_len = e - b;
				}
			}
			return;
		}
		else if(  code == NULL  ) {
			code = p;
			while(  *p  &&  *p != ' '  ) {
				p++;
			}
			code_len = p - code;
			p = skip_spaces( p );
		}
		else {
			return;
		}
	}
}


void board_names_t::get_line_number(cbuffer_t &buf, const char *line_name)
{
	const char *code, *bracket;
	size_t code_len, bracket_len;
	parse_line_header( line_name, code, code_len, bracket, bracket_len );
	if(  bracket  ) {
		buf.append( bracket, bracket_len );
		return;
	}
	if(  code == NULL  ) {
		return;
	}
	// licence number of a bus or tram line: 6 digits, the last three are the line (100167 -> 167),
	// maybe a variant (100370V, 800046Na, 800001/10, 100242/243)
	size_t i = 0;
	while(  i < code_len  &&  is_digit( code[i] )  ) {
		i++;
	}
	if(  i == 6  ) {
		size_t j = i;
		while(  j < code_len  &&  is_alpha( code[j] )  ) {
			j++;
		}
		if(  j == code_len  ||  code[j] == '/'  ) {
			buf.printf( "%d", (code[3]-'0')*100 + (code[4]-'0')*10 + (code[5]-'0') );
			return;
		}
	}
	// train category with a line number of one or two digits (R10/1 -> R10, Ex1/1 -> Ex1, R08 -> R8);
	// three or more digits are a timetable table (Os041/1) and no line
	size_t letters = 0;
	while(  letters < code_len  &&  is_alpha( code[letters] )  ) {
		letters++;
	}
	if(  letters < 1  ||  letters > 3  ) {
		return;
	}
	size_t digits = 0;
	while(  letters + digits < code_len  &&  is_digit( code[letters + digits] )  ) {
		digits++;
	}
	if(  digits >= 1  &&  digits <= 2  ) {
		buf.append( code, letters );
		buf.printf( "%d", digits == 1 ? code[letters]-'0' : (code[letters]-'0')*10 + (code[letters+1]-'0') );
	}
	else if(  digits == 0  &&  letters >= 2  &&  letters < code_len  &&  code[letters] == '/'  ) {
		// a category of its own (SC/1, Nex/001)
		buf.append( code, letters );
	}
}


void board_names_t::get_line_category(cbuffer_t &buf, const char *line_name)
{
	const char *code, *bracket;
	size_t code_len, bracket_len;
	parse_line_header( line_name, code, code_len, bracket, bracket_len );
	if(  code == NULL  ) {
		return;
	}
	size_t letters = 0;
	while(  letters < code_len  &&  is_alpha( code[letters] )  ) {
		letters++;
	}
	if(  letters >= 1  &&  letters <= 4  &&  letters < code_len  &&  (is_digit( code[letters] )  ||  code[letters] == '/')  ) {
		buf.append( code, letters );
	}
}


void board_names_t::get_train_number(cbuffer_t &buf, convoihandle_t cnv)
{
	// category, space, number (with /N parts), then maybe the train name in quotes
	const char *s = cnv->get_internal_name();
	const char *p = s;
	while(  is_alpha( *p )  ) {
		p++;
	}
	const size_t letters = p - s;
	if(  letters >= 1  &&  letters <= 8  &&  *p == ' '  &&  is_digit( p[1] )  ) {
		const char *num = p + 1;
		const char *q = num;
		while(  is_digit( *q )  ||  (*q == '/'  &&  is_digit( q[1] ))  ) {
			q++;
		}
		if(  *q == 0  ||  *q == ' '  ||  *q == ':'  ||  *q == ','  ||  *q == '"'  ) {
			buf.append( s, letters );
			buf.append( " " );
			buf.append( num, q - num );
			if(  q[0] == ' '  &&  q[1] == '"'  ) {
				const char *name = q + 2;
				const char *end = strchr( name, '"' );
				if(  end  &&  end > name  ) {
					buf.append( " " );
					buf.append( name, end - name );
				}
			}
			return;
		}
	}
	buf.printf( "%u", (unsigned)cnv.get_id() );
}


/* stop names */

#define MAX_NAME_PARTS (8)
#define MAX_PART_LEN (256)

struct name_part_t {
	char text[MAX_PART_LEN];
	bool has_tracks; ///< the part had a <timetable track> list: the rail name
};


// a token of a parenthesis that names lines (Ex1, R14A, LE, PRG, Os016/1), not a place
static bool is_line_code(const char *t, size_t len)
{
	if(  len == 0  ||  !is_upper( t[0] )  ) {
		return false;
	}
	size_t i = 1;
	while(  i < len  &&  i < 4  &&  is_alpha( t[i] )  ) {
		i++;
	}
	while(  i < len  &&  is_digit( t[i] )  ) {
		i++;
	}
	if(  i < len  &&  is_alpha( t[i] )  ) {
		i++;
	}
	if(  i < len  &&  t[i] == '/'  ) {
		i++;
		if(  i >= len  ||  !is_digit( t[i] )  ) {
			return false;
		}
		while(  i < len  &&  is_digit( t[i] )  ) {
			i++;
		}
	}
	return i == len;
}


// does the parenthesis from p (at '(') to end (at ')') hold only line codes?
static bool is_line_list(const char *p, const char *end)
{
	const char *t = p + 1;
	bool any = false;
	while(  t < end  ) {
		const char *c = t;
		while(  c < end  &&  *c != ','  ) {
			c++;
		}
		const char *a = t, *b = c;
		while(  a < b  &&  *a == ' '  ) {
			a++;
		}
		while(  b > a  &&  b[-1] == ' '  ) {
			b--;
		}
		if(  b > a  ) {
			if(  !is_line_code( a, b - a )  ) {
				return false;
			}
			any = true;
		}
		t = c + 1;
	}
	return any;
}


// the part [from, to) without its <tracks>, [lines]/[district] and (lines), spaces tidied
static void clean_part(const char *from, const char *to, char *out)
{
	char tmp[MAX_PART_LEN];
	size_t n = 0;
	for(  const char *p = from;  p < to;  ) {
		if(  *p == '<'  ||  *p == '['  ) {
			const char *end = (const char *)memchr( p, *p == '<' ? '>' : ']', to - p );
			p = end ? end + 1 : to;
			continue;
		}
		if(  *p == '('  ) {
			const char *end = (const char *)memchr( p, ')', to - p );
			if(  end  &&  is_line_list( p, end )  ) {
				p = end + 1;
				continue;
			}
		}
		if(  n + 1 < MAX_PART_LEN  ) {
			tmp[n++] = *p;
		}
		p++;
	}
	tmp[n] = 0;
	// collapse spaces, no space before a comma, one after it (not between digits: "1,0")
	size_t m = 0;
	for(  size_t i = 0;  i < n;  i++  ) {
		const char c = tmp[i];
		if(  c == ' '  ||  c == '\t'  ) {
			if(  m > 0  &&  out[m-1] != ' '  ) {
				out[m++] = ' ';
			}
			continue;
		}
		if(  c == ','  ) {
			while(  m > 0  &&  out[m-1] == ' '  ) {
				m--;
			}
			out[m++] = ',';
			if(  i + 1 < n  &&  tmp[i+1] != ' '  &&  !is_digit( tmp[i+1] )  &&  m + 1 < MAX_PART_LEN  ) {
				out[m++] = ' ';
			}
			continue;
		}
		if(  m + 1 < MAX_PART_LEN  ) {
			out[m++] = c;
		}
	}
	// no spaces or commas at either end
	while(  m > 0  &&  (out[m-1] == ' '  ||  out[m-1] == ',')  ) {
		m--;
	}
	out[m] = 0;
	size_t lead = 0;
	while(  out[lead] == ' '  ||  out[lead] == ','  ) {
		lead++;
	}
	if(  lead  ) {
		memmove( out, out + lead, m - lead + 1 );
	}
}


// splits a stop name at " / " (a slash with a space on either side; "n/L" and "<127, CZ/320>" stay)
static int split_name(const char *name, name_part_t *parts)
{
	int count = 0;
	const char *start = name;
	for(  const char *p = name;  ;  p++  ) {
		const bool split = *p == '/'  &&  ((p > name  &&  p[-1] == ' ')  ||  p[1] == ' ');
		if(  *p == 0  ||  split  ) {
			if(  count < MAX_NAME_PARTS  ) {
				clean_part( start, p, parts[count].text );
				const char *lt = (const char *)memchr( start, '<', p - start );
				parts[count].has_tracks = lt != NULL;
				if(  parts[count].text[0]  ) {
					count++;
				}
			}
			if(  *p == 0  ) {
				break;
			}
			start = p + 1;
		}
	}
	return count;
}


// trains: the part with timetable tracks, else the first without a comma; others: the first "Town, Stop" part
static const char *pick_part(const name_part_t *parts, int count, bool train)
{
	if(  train  ) {
		for(  int i = 0;  i < count;  i++  ) {
			if(  parts[i].has_tracks  ) {
				return parts[i].text;
			}
		}
		for(  int i = 0;  i < count;  i++  ) {
			if(  !strchr( parts[i].text, ',' )  ) {
				return parts[i].text;
			}
		}
		return parts[0].text;
	}
	for(  int i = 0;  i < count;  i++  ) {
		if(  strchr( parts[i].text, ',' )  ) {
			return parts[i].text;
		}
	}
	return parts[0].text;
}


void board_names_t::get_stop_name(cbuffer_t &buf, const char *halt_name, bool train, const char *board_town)
{
	name_part_t parts[MAX_NAME_PARTS];
	const int count = split_name( halt_name, parts );
	if(  count == 0  ) {
		buf.append( halt_name );
		return;
	}
	const char *p = pick_part( parts, count, train );
	const char *comma = strchr( p, ',' );
	if(  comma  &&  board_town  &&  *board_town  &&  strlen( board_town ) == (size_t)(comma - p)  &&  strncmp( p, board_town, comma - p ) == 0  ) {
		const char *rest = skip_spaces( comma + 1 );
		if(  *rest  ) {
			buf.append( rest );
			return;
		}
	}
	buf.append( p );
}


void board_names_t::get_stop_town(cbuffer_t &buf, const char *halt_name, bool train)
{
	name_part_t parts[MAX_NAME_PARTS];
	const int count = split_name( halt_name, parts );
	if(  count == 0  ) {
		return;
	}
	const char *p = pick_part( parts, count, train );
	if(  !strchr( p, ',' )  ) {
		p = NULL;
		for(  int i = 0;  i < count  &&  !p;  i++  ) {
			if(  strchr( parts[i].text, ',' )  ) {
				p = parts[i].text;
			}
		}
		if(  !p  ) {
			return;
		}
	}
	buf.append( p, strchr( p, ',' ) - p );
}


/* ---------------------------------------------------------------------------------------------
 * kinds of departures
 */

gui_departure_board_view_t::kind_t gui_departure_board_view_t::get_kind(const schedule_t *schedule, const minivec_tpl<uint8> &catg_index)
{
	if(  !catg_index.is_contained( goods_manager_t::passengers->get_catg_index() )  ) {
		return KIND_FREIGHT;
	}
	switch(  schedule ? schedule->get_waytype() : road_wt  ) {
		case track_wt:
		case monorail_wt:
		case maglev_wt:
		case narrowgauge_wt:
			return KIND_TRAINS;
		default:
			return KIND_OTHERS;
	}
}


gui_departure_board_view_t::kind_t gui_departure_board_view_t::get_kind(convoihandle_t cnv)
{
	if(  cnv->get_line().is_bound()  ) {
		return get_kind( cnv->get_line() );
	}
	return get_kind( cnv->get_schedule(), cnv->get_goods_catg_index() );
}


gui_departure_board_view_t::kind_t gui_departure_board_view_t::get_kind(linehandle_t line)
{
	return get_kind( line->get_schedule(), line->get_goods_catg_index() );
}


uint8 gui_departure_board_view_t::get_kinds_at(halthandle_t halt)
{
	uint8 kinds = 0;
	FOR( vector_tpl<linehandle_t>, line, halt->registered_lines ) {
		if(  line.is_bound()  ) {
			kinds |= 1 << get_kind( line );
		}
	}
	FOR( vector_tpl<convoihandle_t>, cnv, halt->registered_convoys ) {
		if(  cnv.is_bound()  &&  cnv->get_schedule()  ) {
			kinds |= 1 << get_kind( cnv );
		}
	}
	return kinds;
}


/* ---------------------------------------------------------------------------------------------
 * departures
 */

// a guess of the ticks until the convoy arrives at the end of its route, braking included
static uint32 ticks_until_arrival(const convoi_t *cnv)
{
	/* calculate the time needed:
	 *   tiles << (8+12) / (kmh_to_speed(max_kmh) = ticks
	 */
	uint32 delta_t = 0;
	sint32 delta_tiles = cnv->get_route()->get_count() - cnv->front()->get_route_index();
	uint32 kmh_average = (cnv->get_average_kmh()*900 ) / 1024u;
	if(  kmh_average < 5  ) {
		kmh_average = 5;
	}

	// last braking tile
	if(  delta_tiles > 1  &&  kmh_average > 25  ) {
		delta_tiles --;
		delta_t += 3276; // ( (1 << (8+12)) / kmh_to_speed(25) );
	}
	// second last braking tile
	if(  delta_tiles > 1  &&  kmh_average > 50  ) {
		delta_tiles --;
		delta_t += 1638; // ( (1 << (8+12)) / kmh_to_speed(50) );
	}
	// third last braking tile
	if(  delta_tiles > 1  &&  kmh_average > 100  ) {
		delta_tiles --;
		delta_t += 819; // ( (1 << (8+12)) / kmh_to_speed(100) );
	}
	// waiting at signal?
	if(  cnv->get_state() != convoi_t::DRIVING  ) {
		// extra time for acceleration
		delta_t += kmh_average * 25;
	}
	if(  delta_tiles > 0  ) {
		delta_t += (uint32)( ((sint64)delta_tiles << (8+12) ) / kmh_to_speed( kmh_average ) );
	}
	return delta_t;
}


// the convoy that drives: a joined train rides with its primary
static const convoi_t *get_mover(convoihandle_t cnv)
{
	if(  cnv->is_coupled()  &&  cnv->get_coupled_convoi().is_bound()  ) {
		return cnv->get_coupled_convoi().get_rep();
	}
	return cnv.get_rep();
}


gui_departure_board_view_t::gui_departure_board_view_t() :
	gui_component_t(),
	kind(KIND_TRAINS),
	last_refresh(-1),
	last_line_count(0),
	last_convoy_count(0),
	row_h(0),
	header_h(0),
	now(0)
{
	for(  int i = 0;  i < 5;  i++  ) {
		col_w[i] = 0;
	}
}


void gui_departure_board_view_t::set_halt(halthandle_t h)
{
	halt = h;
	update( true );
}


void gui_departure_board_view_t::set_kind(kind_t k)
{
	if(  k != kind  ) {
		kind = k;
		update( true );
	}
}


sint64 gui_departure_board_view_t::ticks_to_board_time(sint64 delta_ticks) const
{
	if(  !welt->has_calendar()  ) {
		return now + delta_ticks;
	}
	return welt->get_calendar_minutes_at( welt->get_ticks() + (uint32)delta_ticks );
}


void gui_departure_board_view_t::update(bool force)
{
	if(  !halt.is_bound()  ) {
		return;
	}
	// rebuild once per calendar minute, or every 1/1024 month without the calendar
	const sint64 stamp = welt->has_calendar() ? welt->get_calendar_minutes() : (sint64)(welt->get_ticks() >> (welt->ticks_per_world_month_shift - 10));
	if(  !force  &&  stamp == last_refresh  &&  halt->registered_lines.get_count() == last_line_count  &&  halt->registered_convoys.get_count() == last_convoy_count  ) {
		return;
	}
	last_refresh = stamp;
	last_line_count = halt->registered_lines.get_count();
	last_convoy_count = halt->registered_convoys.get_count();
	rebuild();
}


// the halt of a schedule entry; unbound for a waypoint or depot
static halthandle_t entry_halt(const schedule_t *schedule, uint8 i, const player_t *owner)
{
	return haltestelle_t::get_halt( schedule->entries[i].pos, owner );
}


// the next entry after i at a halt (waypoints and depots skipped); i if there is none
static uint8 next_stop(const schedule_t *schedule, uint8 i, const player_t *owner)
{
	const uint8 n = schedule->get_count();
	for(  uint8 k = 1;  k < n;  k++  ) {
		const uint8 j = (i + k) % n;
		if(  entry_halt( schedule, j, owner ).is_bound()  ) {
			return j;
		}
	}
	return i;
}


// stops count as the same on a board when they show the same name (a stop in each direction is often
// a halt of its own): a hash of that name
static uint32 stop_key(halthandle_t h, bool train)
{
	cbuffer_t buf;
	board_names_t::get_stop_name( buf, h->get_name(), train, NULL );
	uint32 key = 2166136261u;
	for(  const char *p = buf;  *p;  p++  ) {
		key = (key ^ (uint8)*p) * 16777619u;
	}
	return key;
}


/**
 * Does the train load at the stop of entry j again (j or the entries of the same stop right after it, e.g.
 * an arrival track and then the departure track)?
 */
static bool loads_at_stop_from(const schedule_t *schedule, uint8 j, const player_t *owner, bool train)
{
	const uint8 n = schedule->get_count();
	const halthandle_t first = entry_halt( schedule, j, owner );
	if(  !first.is_bound()  ) {
		return false;
	}
	const uint32 key = stop_key( first, train );
	for(  uint8 k = 0;  k < n;  k++  ) {
		const uint8 i = (j + k) % n;
		const halthandle_t h = entry_halt( schedule, i, owner );
		if(  !h.is_bound()  ) {
			continue;
		}
		if(  h != first  &&  stop_key( h, train ) != key  ) {
			return false;
		}
		if(  schedule->entries[i].loads()  ) {
			return true;
		}
	}
	return false;
}


/**
 * Where a departure from entry e of the schedule goes, and the stops on the way.
 * With Terminal or All off entries in the schedule: the next one. An All off stop the train runs to
 * from a stop T and then comes back to T from, loading there again, is a siding: the trip ends at T. Is
 * T this stop, the departure is only the run to the siding and nobody can ride it (false). Coming back to
 * T without loading (T is All off too) it is the terminus, T only the place where the train turns.
 * Without (or when that is this stop again): the last stop before the schedule comes back to a stop
 * it passed already (the train turned there) or to this one (a ring).
 */
static bool find_destination(const schedule_t *schedule, uint8 e, const player_t *owner, halthandle_t here, bool train, halthandle_t &dest, vector_tpl<halthandle_t> &on_the_way)
{
	const uint8 n = schedule->get_count();
	const uint32 here_key = stop_key( here, train );
	bool has_terminus = false;
	for(  uint8 i = 0;  i < n;  i++  ) {
		if(  schedule->entries[i].unloads_all()  &&  entry_halt( schedule, i, owner ).is_bound()  ) {
			has_terminus = true;
		}
	}

	if(  has_terminus  ) {
		halthandle_t last = here;
		bool moved = false; // passed a stop other than this one
		for(  uint8 k = 1;  k <= n;  k++  ) {
			const uint8 i = (e + k) % n;
			const schedule_entry_t &en = schedule->entries[i];
			const halthandle_t h = entry_halt( schedule, i, owner );
			if(  !h.is_bound()  ||  en.stop_type == schedule_entry_t::hold  ) {
				continue;
			}
			if(  en.unloads_all()  ) {
				if(  en.stop_type == schedule_entry_t::all_off  ) {
					const uint8 back_entry = next_stop( schedule, i, owner );
					const halthandle_t back = entry_halt( schedule, back_entry, owner );
					if(  back.is_bound()  &&  (back == last  ||  stop_key( back, train ) == stop_key( last, train ))  &&  loads_at_stop_from( schedule, back_entry, owner, train )  ) {
						// a siding: the passengers' trip ends at the stop before it
						if(  last == here  ||  stop_key( last, train ) == here_key  ) {
							if(  !moved  ) {
								// right after this stop: only the run to the siding
								return false;
							}
							// round the whole schedule back to here: look again without the stop types
							break;
						}
						dest = last;
						on_the_way.remove( last );
						return true;
					}
				}
				if(  h != here  &&  stop_key( h, train ) != here_key  ) {
					dest = h;
					return true;
				}
				// the terminus is this stop: the trip is round, look again without the stop types
				break;
			}
			if(  h != here  &&  h != last  ) {
				on_the_way.append_unique( h );
			}
			if(  h != here  &&  stop_key( h, train ) != here_key  ) {
				moved = true;
			}
			last = h;
		}
		on_the_way.clear();
	}

	vector_tpl<uint32> passed;
	passed.append( here_key );
	halthandle_t last;
	uint32 last_key = 0;
	for(  uint8 k = 1;  k <= n;  k++  ) {
		const uint8 i = (e + k) % n;
		const halthandle_t h = entry_halt( schedule, i, owner );
		if(  !h.is_bound()  ||  schedule->entries[i].stop_type == schedule_entry_t::hold  ) {
			continue;
		}
		const uint32 key = stop_key( h, train );
		if(  h == last  ||  (last.is_bound()  &&  key == last_key)  ) {
			// the next tile of the same stop
			continue;
		}
		if(  h == here  ||  passed.is_contained( key )  ) {
			// back to a stop it passed: it turned at the last one
			break;
		}
		passed.append( key );
		if(  last.is_bound()  ) {
			on_the_way.append_unique( last );
		}
		last = h;
		last_key = key;
	}
	if(  !last.is_bound()  ) {
		return false;
	}
	dest = last;
	return true;
}


/**
 * Destination and vias of a departure from entry e: see find_destination. Vias are the stops served on
 * the way there, the most important ones (most lines of this kind) in schedule order. False if nobody
 * can ride it (a run to a siding).
 */
bool gui_departure_board_view_t::fill_stream_texts(stream_t &s, const schedule_t *schedule, player_t *owner)
{
	const bool train = is_train_board();
	halthandle_t dest;
	vector_tpl<halthandle_t> on_the_way;
	if(  !find_destination( schedule, s.entry, owner, halt, train, dest, on_the_way )  ) {
		return false;
	}

	cbuffer_t buf;
	s.line_number[0] = 0;
	s.category[0] = 0;
	if(  s.line.is_bound()  ) {
		board_names_t::get_line_number( buf, s.line->get_name() );
		tstrncpy( s.line_number, buf, lengthof(s.line_number) );
		buf.clear();
		board_names_t::get_line_category( buf, s.line->get_name() );
		tstrncpy( s.category, buf, lengthof(s.category) );
		buf.clear();
	}

	cbuffer_t town;
	board_names_t::get_stop_town( town, halt->get_name(), train );

	board_names_t::get_stop_name( buf, dest->get_name(), train, town );
	tstrncpy( s.destination, buf, lengthof(s.destination) );
	s.dest_halt = dest;
	buf.clear();

	// the line number and destination the schedule sets for departures from here
	if(  const char *t = schedule->get_board_text( s.entry, schedule_entry_t::BOARD_LINE )  ) {
		tstrncpy( s.line_number, t, lengthof(s.line_number) );
	}
	if(  const char *t = schedule->get_board_text( s.entry, schedule_entry_t::BOARD_DESTINATION )  ) {
		tstrncpy( s.destination, t, lengthof(s.destination) );
	}

	// the most important stops on the way: served by the most lines of this kind
	const uint32 max_vias = train ? MAX_VIAS_TRAIN : MAX_VIAS_OTHER;
	vector_tpl<halthandle_t> vias;
	if(  on_the_way.get_count() <= max_vias  ) {
		FOR( vector_tpl<halthandle_t>, h, on_the_way ) {
			vias.append( h );
		}
	}
	else {
		vector_tpl<uint32> weight;
		FOR( vector_tpl<halthandle_t>, h, on_the_way ) {
			uint32 w = 0;
			FOR( vector_tpl<linehandle_t>, l, h->registered_lines ) {
				if(  l.is_bound()  &&  get_kind( l ) == kind  ) {
					w++;
				}
			}
			weight.append( w );
		}
		vector_tpl<bool> chosen( on_the_way.get_count() );
		for(  uint32 i = 0;  i < on_the_way.get_count();  i++  ) {
			chosen.append( false );
		}
		for(  uint32 v = 0;  v < max_vias;  v++  ) {
			sint32 best = -1;
			for(  uint32 i = 0;  i < on_the_way.get_count();  i++  ) {
				if(  !chosen[i]  &&  (best < 0  ||  weight[i] > weight[best])  ) {
					best = i;
				}
			}
			if(  best >= 0  ) {
				chosen[best] = true;
			}
		}
		for(  uint32 i = 0;  i < on_the_way.get_count();  i++  ) {
			if(  chosen[i]  ) {
				vias.append( on_the_way[i] );
			}
		}
	}
	s.via[0] = 0;
	if(  !vias.empty()  ) {
		cbuffer_t list;
		FOR( vector_tpl<halthandle_t>, h, vias ) {
			if(  list.len() > 0  ) {
				list.append( ", " );
			}
			board_names_t::get_stop_name( list, h->get_name(), train, town );
		}
		buf.printf( translator::translate("via %s"), (const char *)list );
		tstrncpy( s.via, buf, lengthof(s.via) );
	}
	return true;
}


// a train known to leave from a stream: loading there or on its way
struct known_t {
	convoihandle_t cnv;
	sint64 ready;      ///< board time it can leave
	bool planned;      ///< ready is its planned slot (timetable, loading here)
	bool loading;
};


// is the convoy on the map and running its schedule? (mover: the one that drives, see get_mover)
static bool is_running(const convoi_t *mover)
{
	const convoi_t::states state = (convoi_t::states)mover->get_state();
	return state != convoi_t::INITIAL  &&  state != convoi_t::EDIT_SCHEDULE  &&  state != convoi_t::SELF_DESTRUCT  &&  state != convoi_t::ENTERING_DEPOT  &&  state != convoi_t::UNCOUPLING;
}


// is it driving or waiting at a signal (so its route leads to its next stop)?
static bool is_on_its_way(const convoi_t *mover)
{
	const convoi_t::states state = (convoi_t::states)mover->get_state();
	return state == convoi_t::DRIVING  ||  (state >= convoi_t::WAITING_FOR_CLEARANCE  &&  state <= convoi_t::CAN_START_TWO_MONTHS  &&  state != convoi_t::SELF_DESTRUCT);
}


// calendar minutes for a number of ticks
static sint64 ticks_to_minutes(sint64 ticks)
{
	if(  !welt->has_calendar()  ||  welt->ticks_per_world_month == 0  ) {
		return 0;
	}
	return ticks * welt->get_settings().get_minutes_per_month() / (sint64)welt->ticks_per_world_month;
}


/**
 * Rows of the departures from schedule entry e at halt `at` (line, or a convoy without a line).
 * Entries right before e at which nobody boards (an All off arrival track, a siding, a layover) or
 * at this halt lead in to it: trains there count as coming to e. The timetable is e's own; an entry
 * without one of a line with a timetable elsewhere is timed from the last one before it (add_timed_rows).
 */
void gui_departure_board_view_t::add_stream_rows(const schedule_t *schedule, linehandle_t line, convoihandle_t lineless, uint8 e, halthandle_t at, vector_tpl<row_t> &out)
{
	const uint8 n = schedule->get_count();
	const schedule_entry_t &entry = schedule->entries[e];

	if(  line.is_bound()  &&  !entry.has_timetable()  &&  welt->has_calendar()  ) {
		const uint8 anchor = line->get_timetable_anchor( e );
		if(  anchor != 255  ) {
			add_timed_rows( schedule, line, e, anchor, out );
			return;
		}
	}

	// how many entries before e the lead-in starts (0 = e itself): entries nobody boards at, waypoints,
	// and the entries of this stop before (an arrival track, a siding run)
	const player_t *owner = line.is_bound() ? line->get_owner() : lineless->get_owner();
	uint8 lead_in = 0;
	while(  lead_in + 1 < n  ) {
		const uint8 i = (e + n - lead_in - 1) % n;
		const halthandle_t h = entry_halt( schedule, i, owner );
		if(  schedule->entries[i].loads()  &&  h.is_bound()  &&  h != at  ) {
			break;
		}
		lead_in++;
	}

	vector_tpl<convoihandle_t> convoys;
	if(  line.is_bound()  ) {
		FOR( vector_tpl<convoihandle_t>, c, line->get_convoys() ) {
			convoys.append( c );
		}
	}
	else {
		convoys.append( lineless );
	}

	const uint16 stream_idx = streams.get_count() - 1;
	const bool timetable = line.is_bound()  &&  entry.has_timetable()  &&  welt->has_calendar();
	const sint64 dwell = welt->has_calendar() ? 1 : 2000;

	vector_tpl<known_t> known;
	bool any_running = false;

	FOR( vector_tpl<convoihandle_t>, cnv, convoys ) {
		if(  !cnv.is_bound()  ||  cnv->get_schedule() == NULL  ||  cnv->get_schedule()->get_count() != n  ) {
			continue;
		}
		const convoi_t *mover = get_mover( cnv );
		const convoi_t::states state = (convoi_t::states)mover->get_state();
		if(  !is_running( mover )  ) {
			continue;
		}
		any_running = true;
		const uint8 cur = cnv->get_schedule()->get_current_stop();
		const uint8 dist = (e + n - cur) % n;   // entries until e

		row_t row;
		row.cnv = cnv;
		row.line = line;
		row.stream = stream_idx;

		if(  mover->get_leaving_halt() == at  &&  (cur + n - 1) % n == e  ) {
			// finished loading here, still standing at the platform or before its exit signal
			row.expected = now;
			row.booked = true;
			if(  timetable  ) {
				const sint32 d = cnv->get_departure_delay();
				row.delay = d > 0 ? d : 0;
				row.scheduled = now - row.delay;
			}
			out.append( row );
			continue;
		}
		if(  dist > lead_in  ) {
			continue;
		}
		known_t k;
		k.cnv = cnv;
		k.planned = false;
		k.loading = false;
		if(  state == convoi_t::LOADING  ) {
			sint64 minutes;
			bool latest;
			const bool known_departure = welt->has_calendar()  &&  mover->get_planned_departure( minutes, latest );
			if(  dist == 0  ) {
				k.loading = true;
				if(  known_departure  ) {
					k.ready = minutes;
					k.planned = timetable  &&  !cnv->is_coupled();
				}
				else {
					k.ready = now;
				}
			}
			else {
				// at a stop leading in here (an arrival track, a siding with a timetable of its own)
				k.ready = later( known_departure ? minutes : now, now ) + dist * dwell;
			}
		}
		else if(  is_on_its_way( mover )  ) {
			k.ready = ticks_to_board_time( ticks_until_arrival( mover ) ) + (dist + 1) * dwell;
		}
		else {
			// finding its route: no telling yet
			continue;
		}
		known.append( k );
	}

	if(  !any_running  ) {
		// a line without a train running shows nothing
		return;
	}

	// loading trains first (in the order they came), then the coming ones by their arrival
	for(  uint32 i = 1;  i < known.get_count();  i++  ) {
		for(  uint32 j = i;  j > 0;  j--  ) {
			known_t &a = known[j-1], &b = known[j];
			bool swap;
			if(  a.loading != b.loading  ) {
				swap = b.loading;
			}
			else if(  a.loading  ) {
				swap = get_mover( b.cnv )->arrived_before( get_mover( a.cnv ) );
			}
			else {
				swap = b.ready < a.ready;
			}
			if(  !swap  ) {
				break;
			}
			const known_t t = a;
			a = b;
			b = t;
		}
	}

	if(  !timetable  ) {
		FOR( vector_tpl<known_t>, const &k, known ) {
			row_t row;
			row.cnv = k.cnv;
			row.line = line;
			row.stream = stream_idx;
			row.expected = k.ready > now ? k.ready : now;
			out.append( row );
		}
		return;
	}

	// the slots to come: the open one (if not used yet) and the following ones
	const sint64 last = line->get_last_departure_slot( e );
	vector_tpl<sint64> slots;
	vector_tpl<bool> taken;
	sint64 s = simline_t::get_first_departure_slot( entry, now );
	for(  int i = 0;  i < MAX_SLOTS  &&  s <= now + SLOT_HORIZON;  i++  ) {
		bool usable = s > last;
		if(  last < -1  ) {
			// a new line: its first train never leaves late, so only slots to come count
			usable = s >= -2 - last  &&  s >= now;
		}
		if(  usable  ) {
			slots.append( s );
			taken.append( false );
		}
		s = simline_t::get_next_departure_slot( entry, s );
	}

	FOR( vector_tpl<known_t>, const &k, known ) {
		sint32 found = -1;
		for(  uint32 i = 0;  i < slots.get_count()  &&  found < 0;  i++  ) {
			if(  taken[i]  ) {
				continue;
			}
			if(  k.planned  ) {
				if(  slots[i] == k.ready  ) {
					found = i;
				}
				continue;
			}
			// the first slot still open when it is ready
			const sint64 gap = simline_t::get_next_departure_slot( entry, slots[i] ) - slots[i];
			if(  k.ready <= slots[i]  ||  entry.is_slot_open( k.ready - slots[i], gap )  ) {
				found = i;
			}
		}
		row_t row;
		row.cnv = k.cnv;
		row.line = line;
		row.stream = stream_idx;
		if(  found >= 0  ) {
			taken[found] = true;
			row.scheduled = slots[found];
		}
		else if(  k.planned  ) {
			// a slot no other train may take (late running, the slot of the train ahead)
			row.scheduled = k.ready;
		}
		else {
			continue;
		}
		const sint64 ready = k.planned ? now : k.ready;
		row.expected = later( later( row.scheduled, ready ), now );
		row.delay = (sint32)(row.expected - row.scheduled);
		out.append( row );
	}

	// slots no train is known for: the line number only, late while the slot is still open
	for(  uint32 i = 0;  i < slots.get_count();  i++  ) {
		if(  taken[i]  ) {
			continue;
		}
		row_t row;
		row.line = line;
		row.stream = stream_idx;
		row.scheduled = slots[i];
		row.expected = later( slots[i], now );
		row.delay = (sint32)(row.expected - row.scheduled);
		out.append( row );
	}
}


/**
 * Running times from the anchor (an entry with a timetable) to each entry of the stretch after it, in
 * calendar minutes from being ready to leave there to being ready to leave the entry: run[k] for the k-th
 * entry after the anchor. Learned by the line where its trains have shown them, else estimated: the
 * distance (a quarter more than straight across the tiles) at three quarters of the average top speed of
 * the line's trains, and a minute at every stop.
 */
void gui_departure_board_view_t::estimate_run_times(const schedule_t *schedule, linehandle_t line, uint8 anchor, uint8 stretch, sint64 *run) const
{
	const uint8 n = schedule->get_count();
	sint64 speed_sum = 0;
	sint64 speeds = 0;
	FOR( vector_tpl<convoihandle_t>, c, line->get_convoys() ) {
		if(  c.is_bound()  &&  c->get_min_top_speed() > 0  ) {
			speed_sum += c->get_min_top_speed();
			speeds ++;
		}
	}
	sint64 speed = speeds > 0 ? speed_sum * 3 / (speeds * 4) : kmh_to_speed( 50 );
	if(  speed < 1  ) {
		speed = 1;
	}

	run[0] = 0;
	sint64 base = 0;      // learned time of the last learned entry before
	sint64 ticks = 0;     // estimated driving since then
	sint64 stops = 0;     // stops since then (this one included)
	koord last = schedule->entries[anchor].pos.get_2d();
	for(  uint8 k = 1;  k <= stretch;  k++  ) {
		const uint8 i = (anchor + k) % n;
		const koord p = schedule->entries[i].pos.get_2d();
		const sint64 tiles = (sint64)koord_distance( last, p ) * 5 / 4;
		ticks += (tiles << (8+12)) / speed;
		last = p;
		if(  entry_halt( schedule, i, line->get_owner() ).is_bound()  ) {
			stops ++;
		}
		sint64 learned;
		if(  line->get_run_time( anchor, i, learned )  ) {
			run[k] = learned;
			base = learned;
			ticks = 0;
			stops = 0;
		}
		else {
			run[k] = base + ticks_to_minutes( ticks ) + stops;
		}
		if(  run[k] < run[k-1]  ) {
			run[k] = run[k-1];
		}
	}
}


/**
 * Departures from entry e (no timetable) of a line with a timetable at `anchor`, the last entry with one
 * before e: its slots, each with the running time from there on. Slots no train booked yet come from
 * the anchor's rows, without a train (it shows once it left there); trains that booked their slot there
 * and have not left here yet show with their own slot and when they are expected here, from where
 * they are now.
 */
void gui_departure_board_view_t::add_timed_rows(const schedule_t *schedule, linehandle_t line, uint8 e, uint8 anchor, vector_tpl<row_t> &out)
{
	const uint8 n = schedule->get_count();
	const player_t *owner = line->get_owner();
	const uint16 stream_idx = streams.get_count() - 1;
	const uint8 stretch = (e + n - anchor) % n;

	sint64 run[256];
	estimate_run_times( schedule, line, anchor, stretch, run );
	const sint64 to_here = run[stretch];

	// the anchor's slots nobody booked yet, moved on by the running time
	vector_tpl<row_t> at_anchor;
	add_stream_rows( schedule, line, convoihandle_t(), anchor, entry_halt( schedule, anchor, owner ), at_anchor );
	FOR( vector_tpl<row_t>, r, at_anchor ) {
		if(  r.booked  ||  r.scheduled < 0  ) {
			continue;
		}
		r.cnv = convoihandle_t();
		r.stream = stream_idx;
		r.scheduled += to_here;
		r.expected += to_here;
		out.append( r );
	}

	// trains that booked their slot at the anchor and have not left here yet
	FOR( vector_tpl<convoihandle_t>, cnv, line->get_convoys() ) {
		if(  !cnv.is_bound()  ||  cnv->get_schedule() == NULL  ||  cnv->get_schedule()->get_count() != n  ) {
			continue;
		}
		const convoi_t *mover = get_mover( cnv );
		if(  !is_running( mover )  ) {
			continue;
		}
		const uint8 cur = cnv->get_schedule()->get_current_stop();
		const uint8 after_anchor = (cur + n - anchor) % n;   // 0: on its way to the anchor or still there
		const bool recorded = cnv->get_anchor_entry() == anchor  &&  cnv->get_anchor_slot() >= 0;
		const sint32 delay = cnv->get_departure_delay();

		row_t row;
		row.cnv = cnv;
		row.line = line;
		row.stream = stream_idx;

		sint64 expected;
		if(  mover->get_leaving_halt() == halt  &&  (cur + n - 1) % n == e  ) {
			// finished loading here, still standing at the platform or before its exit signal
			expected = now;
			row.booked = true;
		}
		else if(  after_anchor == 0  ||  after_anchor > stretch  ) {
			continue;
		}
		else if(  mover->get_state() == convoi_t::LOADING  ) {
			// at a stop of the stretch (here, or on the way): when it may leave, then on from there
			sint64 minutes;
			bool latest;
			const sint64 ready = mover->get_planned_departure( minutes, latest ) ? later( minutes, now ) : now;
			expected = ready + to_here - run[after_anchor];
		}
		else if(  is_on_its_way( mover )  ) {
			// arriving at its next stop, ready to leave it a minute later, then on from there
			expected = ticks_to_board_time( ticks_until_arrival( mover ) ) + 1 + to_here - run[after_anchor];
		}
		else {
			// finding its route: no telling yet
			continue;
		}

		row.scheduled = recorded ? cnv->get_anchor_slot() + to_here : expected - (delay > 0 ? delay : 0);
		row.expected = later( expected, now );
		row.delay = row.expected > row.scheduled ? (sint32)(row.expected - row.scheduled) : 0;
		out.append( row );
	}
}


void gui_departure_board_view_t::add_line_streams(linehandle_t line, vector_tpl<row_t> &out)
{
	const schedule_t *schedule = line->get_schedule();
	if(  schedule == NULL  ||  schedule->empty()  ) {
		return;
	}
	for(  uint8 e = 0;  e < schedule->get_count();  e++  ) {
		const schedule_entry_t &entry = schedule->entries[e];
		if(  !entry.loads()  ||  haltestelle_t::get_halt( entry.pos, line->get_owner() ) != halt  ) {
			continue;
		}
		if(  entry_halt( schedule, next_stop( schedule, e, line->get_owner() ), line->get_owner() ) == halt  ) {
			// it goes on to another track of this stop and leaves from there
			continue;
		}
		stream_t s;
		s.line = line;
		s.entry = e;
		if(  !fill_stream_texts( s, schedule, line->get_owner() )  ) {
			continue;
		}
		streams.append( s );
		add_stream_rows( schedule, line, convoihandle_t(), e, halt, out );
	}
}


void gui_departure_board_view_t::add_convoy_stream(convoihandle_t cnv, vector_tpl<row_t> &out)
{
	const schedule_t *schedule = cnv->get_schedule();
	if(  schedule == NULL  ||  schedule->empty()  ) {
		return;
	}
	for(  uint8 e = 0;  e < schedule->get_count();  e++  ) {
		const schedule_entry_t &entry = schedule->entries[e];
		if(  !entry.loads()  ||  haltestelle_t::get_halt( entry.pos, cnv->get_owner() ) != halt  ) {
			continue;
		}
		if(  entry_halt( schedule, next_stop( schedule, e, cnv->get_owner() ), cnv->get_owner() ) == halt  ) {
			// it goes on to another track of this stop and leaves from there
			continue;
		}
		stream_t s;
		s.cnv = cnv;
		s.entry = e;
		if(  !fill_stream_texts( s, schedule, cnv->get_owner() )  ) {
			continue;
		}
		streams.append( s );
		add_stream_rows( schedule, linehandle_t(), cnv, e, halt, out );
	}
}


// the time a row is listed by: its slot, or when it is expected to leave
static sint64 row_time(sint64 scheduled, sint64 expected)
{
	return scheduled >= 0 ? scheduled : expected;
}


// a train board lists by the timetable (late trains keep their place), a stop that counts the minutes by when they leave
static sint64 list_time(const gui_departure_board_view_t::row_t &r, bool train)
{
	return train ? row_time( r.scheduled, r.expected ) : r.expected;
}


void gui_departure_board_view_t::rebuild()
{
	streams.clear();
	rows.clear();
	now = welt->has_calendar() ? welt->get_calendar_minutes() : 0;

	// every departure to come, per line (or convoy without a line): group
	vector_tpl<row_t> all;
	uint16 groups = 0;
	FOR( vector_tpl<linehandle_t>, line, halt->registered_lines ) {
		if(  line.is_bound()  &&  get_kind( line ) == kind  ) {
			const uint32 first = all.get_count();
			add_line_streams( line, all );
			for(  uint32 i = first;  i < all.get_count();  i++  ) {
				all[i].group = groups;
			}
			groups++;
		}
	}
	FOR( vector_tpl<convoihandle_t>, cnv, halt->registered_convoys ) {
		if(  cnv.is_bound()  &&  cnv->get_schedule()  &&  get_kind( cnv ) == kind  ) {
			const uint32 first = all.get_count();
			add_convoy_stream( cnv, all );
			for(  uint32 i = first;  i < all.get_count();  i++  ) {
				all[i].group = groups;
			}
			groups++;
		}
	}

	// the next departure of every line and direction sets the time span of the board
	vector_tpl<sint64> first_time( streams.get_count() );
	vector_tpl<sint32> first_row( streams.get_count() );
	for(  uint32 i = 0;  i < streams.get_count();  i++  ) {
		first_time.append( 0 );
		first_row.append( -1 );
	}
	for(  uint32 i = 0;  i < all.get_count();  i++  ) {
		const sint64 t = list_time( all[i], is_train_board() );
		const uint16 st = all[i].stream;
		if(  first_row[st] < 0  ||  t < first_time[st]  ) {
			first_time[st] = t;
			first_row[st] = i;
		}
	}
	sint64 horizon = now;
	for(  uint32 i = 0;  i < streams.get_count();  i++  ) {
		if(  first_row[i] >= 0  &&  first_time[i] > horizon  ) {
			horizon = first_time[i];
		}
	}

	// every line: its first departures, then the others within the span, at most five rows
	vector_tpl<bool> chosen( all.get_count() );
	for(  uint32 i = 0;  i < all.get_count();  i++  ) {
		chosen.append( false );
	}
	vector_tpl<uint32> per_group( groups );
	for(  uint16 g = 0;  g < groups;  g++  ) {
		per_group.append( 0 );
	}
	for(  uint32 i = 0;  i < streams.get_count();  i++  ) {
		if(  first_row[i] >= 0  ) {
			chosen[first_row[i]] = true;
			per_group[all[first_row[i]].group]++;
		}
	}
	vector_tpl<uint32> order( all.get_count() );
	for(  uint32 i = 0;  i < all.get_count();  i++  ) {
		order.append( i );
	}
	// by time (insertion sort: a few dozen rows)
	for(  uint32 i = 1;  i < order.get_count();  i++  ) {
		for(  uint32 j = i;  j > 0;  j--  ) {
			const row_t &a = all[order[j-1]], &b = all[order[j]];
			if(  list_time( b, is_train_board() ) >= list_time( a, is_train_board() )  ) {
				break;
			}
			const uint32 t = order[j-1];
			order[j-1] = order[j];
			order[j] = t;
		}
	}
	FOR( vector_tpl<uint32>, i, order ) {
		const row_t &r = all[i];
		if(  !chosen[i]  &&  list_time( r, is_train_board() ) <= horizon  &&  per_group[r.group] < MAX_ROWS_PER_LINE  ) {
			chosen[i] = true;
			per_group[r.group]++;
		}
	}
	alt_rows.clear();
	FOR( vector_tpl<uint32>, i, order ) {
		if(  chosen[i]  ) {
			row_t r = all[i];
			cbuffer_t buf;
			if(  r.cnv.is_bound()  &&  is_train_board()  ) {
				board_names_t::get_train_number( buf, r.cnv );
			}
			tstrncpy( r.train, buf, lengthof(r.train) );
			rows.append( r );
		}
	}

	merge_coupled_rows();
	layout();
}


/**
 * A coupled pair that leaves here together is one train on a real board, which shows each part's
 * number and destination in turn: the joined train's row goes into alt_rows, behind its primary's.
 * Not when they part here (each leaves on its own then).
 */
void gui_departure_board_view_t::merge_coupled_rows()
{
	for(  uint32 i = 0;  i < rows.get_count();  ) {
		const convoihandle_t cnv = rows[i].cnv;
		if(  cnv.is_bound()  &&  cnv->is_coupled()  &&  cnv->get_coupled_convoi().is_bound()  ) {
			const convoihandle_t primary = cnv->get_coupled_convoi();
			sint32 p = -1;
			for(  uint32 j = 0;  j < rows.get_count()  &&  p < 0;  j++  ) {
				if(  rows[j].cnv == primary  &&  rows[j].alt < 0  ) {
					p = j;
				}
			}
			if(  p >= 0  &&  primary->get_uncouple_halt() != halt  ) {
				rows[p].alt = alt_rows.get_count();
				alt_rows.append( rows[i] );
				rows.remove_at( i );
				continue;
			}
		}
		i++;
	}

	// trains not known yet: a line whose schedule couples it with another line (here or before, riding
	// on coupled) leaves in the same slot as that one, if both go on to the same next stop
	for(  uint32 i = 0;  i < rows.get_count();  ) {
		const stream_t &s = streams[rows[i].stream];
		sint32 p = -1;
		if(  rows[i].alt < 0  &&  rows[i].scheduled >= 0  &&  s.line.is_bound()  ) {
			const schedule_t *sch = s.line->get_schedule();
			vector_tpl<uint16> partners;
			for(  uint8 k = 0;  k < sch->get_count();  k++  ) {
				if(  sch->entries[k].couple_line_id  ) {
					partners.append_unique( sch->entries[k].couple_line_id );
				}
			}
			const halthandle_t next = entry_halt( sch, next_stop( sch, s.entry, s.line->get_owner() ), s.line->get_owner() );
			for(  uint32 j = 0;  j < rows.get_count()  &&  !partners.empty()  &&  p < 0;  j++  ) {
				const stream_t &ps = streams[rows[j].stream];
				if(  j != i  &&  rows[j].alt < 0  &&  rows[j].scheduled == rows[i].scheduled  &&  ps.line.is_bound()  &&  partners.is_contained( ps.line.get_id() )  ) {
					const schedule_t *psch = ps.line->get_schedule();
					if(  entry_halt( psch, next_stop( psch, ps.entry, ps.line->get_owner() ), ps.line->get_owner() ) == next  ) {
						p = j;
					}
				}
			}
		}
		if(  p >= 0  ) {
			rows[p].alt = alt_rows.get_count();
			alt_rows.append( rows[i] );
			rows.remove_at( i );
			continue;
		}
		i++;
	}
}


/* ---------------------------------------------------------------------------------------------
 * drawing
 */

// colours of the boards: a blue train board, a dark bus and tram board with amber lights
static PIXVAL rgb(uint32 c) { return get_system_color( c >> 16, (c >> 8) & 0xFF, c & 0xFF ); }

#define TRAIN_BG      0x0B1F4B
#define TRAIN_STRIPE  0x0F2858
#define TRAIN_HEAD_BG 0x16367A
#define TRAIN_HEAD    0xA9C4F0
#define TRAIN_TEXT    0xFFFFFF
#define TRAIN_VIA     0x9FB8E0
#define TRAIN_DELAY   0xFFD030
#define TRAIN_HOVER   0x1E4590

#define OTHER_BG      0x121212
#define OTHER_STRIPE  0x1A1A1A
#define OTHER_HEAD_BG 0x262626
#define OTHER_HEAD    0x9A9A9A
#define OTHER_TEXT    0xFFB41E
#define OTHER_VIA     0xA77C2E
#define OTHER_DELAY   0xFF5A3C
#define OTHER_HOVER   0x2C2C2C

static uint32 badge_color(waytype_t wt)
{
	switch(  wt  ) {
		case tram_wt:  return 0xC81E1E;
		case road_wt:  return 0x1E64C8;
		case water_wt: return 0x008C8C;
		case air_wt:   return 0x6E3CB4;
		default:       return 0x505050;
	}
}


void gui_departure_board_view_t::format_time(cbuffer_t &buf, const row_t &row) const
{
	const sint64 t = is_train_board() ? row_time( row.scheduled, row.expected ) : row.expected;
	if(  !welt->has_calendar()  ) {
		buf.append( tick_to_string( (sint32)((welt->get_ticks() % welt->ticks_per_world_month) + (t - now)), false ) );
		return;
	}
	if(  !is_train_board()  &&  t - now < 60  ) {
		// a stop shows the minutes to go
		if(  t <= now  ) {
			buf.append( translator::translate("now") );
		}
		else {
			buf.printf( translator::translate("%d min"), (int)(t - now) );
		}
		return;
	}
	const karte_t::calendar_date_t date = welt->get_calendar_date( t );
	buf.printf( "%02d:%02d", date.hour, date.minute );
}


void gui_departure_board_view_t::measure_row(const row_t &r, bool &any_via)
{
	const bool train = is_train_board();
	const scr_coord_val pad = D_H_SPACE;
	const stream_t &s = streams[r.stream];
	cbuffer_t buf;

	format_time( buf, r );
	const scr_coord_val tw = proportional_string_width( buf );
	buf.clear();
	if(  r.delay > 0  ) {
		buf.printf( translator::translate(train ? "+%d min" : "+%d"), (int)r.delay );
	}
	const scr_coord_val dw = proportional_string_width( buf );
	const scr_coord_val lw = s.line_number[0] ? proportional_string_width( s.line_number ) + 2*pad : 0;
	const scr_coord_val destw = proportional_string_width( s.destination );
	any_via |= s.via[0] != 0;
	if(  train  ) {
		const char *tr = r.train[0] ? r.train : s.category;
		// room for the "1/2" of a coupled pair
		const scr_coord_val pair_w = r.alt >= 0 ? pad + proportional_string_width( "2/2" ) : 0;
		col_w[0] = max( col_w[0], tw );
		col_w[1] = max( col_w[1], proportional_string_width( tr ) + pair_w );
		col_w[2] = max( col_w[2], lw );
		col_w[3] = max( col_w[3], destw );
		col_w[4] = max( col_w[4], dw );
	}
	else {
		col_w[0] = max( col_w[0], lw );
		col_w[1] = max( col_w[1], destw );
		col_w[2] = max( col_w[2], tw );
		col_w[3] = max( col_w[3], dw );
	}
}


void gui_departure_board_view_t::layout()
{
	const bool train = is_train_board();
	const scr_coord_val pad = D_H_SPACE;

	for(  int i = 0;  i < 5;  i++  ) {
		col_w[i] = 0;
	}
	// trains: departure, train, line, destination, delay; others: line, destination, departure, delay
	const char *head[5];
	if(  train  ) {
		head[0] = "Departure"; head[1] = "Train"; head[2] = "Line"; head[3] = "Board destination"; head[4] = "Delay";
	}
	else {
		head[0] = "Line"; head[1] = "Board destination"; head[2] = "Departure"; head[3] = "Delay"; head[4] = NULL;
	}
	for(  int i = 0;  i < 5;  i++  ) {
		if(  head[i]  ) {
			col_w[i] = proportional_string_width( translator::translate( head[i] ) );
		}
	}
	bool any_via = false;
	FOR( vector_tpl<row_t>, const &r, rows ) {
		measure_row( r, any_via );
	}
	FOR( vector_tpl<row_t>, r, alt_rows ) {
		r.alt = 0; // a pair: room for "2/2"
		measure_row( r, any_via );
	}
	// the destination column takes what is left; long names are cut when the window is narrow
	const int dest_col = train ? 3 : 1;
	if(  train  ) {
		// long train names ("railjet 275 Metropolitan") are cut too
		col_w[1] = min( col_w[1], proportional_string_width( "MMMMMMMMMMMM" ) );
	}
	col_w[dest_col] = min( max( col_w[dest_col], proportional_string_width( "MMMMMMMMMMMM" ) ), proportional_string_width( "MMMMMMMMMMMMMMMMMM" ) );

	header_h = LINESPACE + D_V_SPACE;
	row_h = (train  &&  any_via ? 2*LINESPACE : LINESPACE) + D_V_SPACE;

	scr_coord_val w = pad;
	for(  int i = 0;  i < 5;  i++  ) {
		if(  col_w[i] > 0  ) {
			w += col_w[i] + 2*pad;
		}
	}
	const scr_coord_val h = header_h + max( 1, (int)rows.get_count() ) * row_h + D_V_SPACE;
	min_size = scr_size( w, h );
	set_size( scr_size( max( get_size().w, w ), h ) );
}


scr_size gui_departure_board_view_t::get_max_size() const
{
	return scr_size( scr_size::inf.w, min_size.h );
}


sint32 gui_departure_board_view_t::get_row_at(scr_coord_val y) const
{
	if(  y < header_h  ||  row_h <= 0  ) {
		return -1;
	}
	const sint32 i = (y - header_h) / row_h;
	return i < (sint32)rows.get_count() ? i : -1;
}


// a coupled pair shows each train for this long (milliseconds)
#define PAIR_PERIOD (3000)

// the row as shown now: the second train of a coupled pair every other period
static bool show_second(sint16 alt)
{
	return alt >= 0  &&  (dr_time() / PAIR_PERIOD) & 1;
}


bool gui_departure_board_view_t::infowin_event(const event_t *ev)
{
	if(  IS_LEFTRELEASE( ev )  &&  getroffen( ev->mx + pos.x, ev->my + pos.y )  ) {
		const sint32 i = get_row_at( ev->my );
		if(  i >= 0  ) {
			const row_t &r = show_second( rows[i].alt ) ? alt_rows[rows[i].alt] : rows[i];
			if(  r.cnv.is_bound()  ) {
				r.cnv->open_info_window();
				return true;
			}
		}
	}
	return false;
}


void gui_departure_board_view_t::draw(scr_coord offset)
{
	const bool train = is_train_board();
	const scr_coord_val pad = D_H_SPACE;
	const scr_coord p = offset + pos;
	const scr_coord_val w = get_size().w;

	const PIXVAL bg = rgb( train ? TRAIN_BG : OTHER_BG );
	const PIXVAL stripe = rgb( train ? TRAIN_STRIPE : OTHER_STRIPE );
	const PIXVAL head_bg = rgb( train ? TRAIN_HEAD_BG : OTHER_HEAD_BG );
	const PIXVAL head_col = rgb( train ? TRAIN_HEAD : OTHER_HEAD );
	const PIXVAL text_col = rgb( train ? TRAIN_TEXT : OTHER_TEXT );
	const PIXVAL via_col = rgb( train ? TRAIN_VIA : OTHER_VIA );
	const PIXVAL delay_col = rgb( train ? TRAIN_DELAY : OTHER_DELAY );
	const PIXVAL hover_col = rgb( train ? TRAIN_HOVER : OTHER_HOVER );

	display_fillbox_wh_clip_rgb( p.x, p.y, w, get_size().h, bg, true );
	display_fillbox_wh_clip_rgb( p.x, p.y, w, header_h, head_bg, true );

	// column positions; the destination column gets the room left over
	const int dest_col = train ? 3 : 1;
	scr_coord_val fixed = pad;
	for(  int i = 0;  i < 5;  i++  ) {
		if(  col_w[i] > 0  &&  i != dest_col  ) {
			fixed += col_w[i] + 2*pad;
		}
	}
	scr_coord_val cw[5];
	scr_coord_val cx[5];
	scr_coord_val x = p.x + pad;
	for(  int i = 0;  i < 5;  i++  ) {
		cw[i] = i == dest_col ? max( col_w[i], w - fixed - 2*pad ) : col_w[i];
		cx[i] = x + pad;
		if(  col_w[i] > 0  ) {
			x += cw[i] + 2*pad;
		}
	}

	// header
	const scr_coord_val hy = p.y + D_V_SPACE/2;
	if(  train  ) {
		display_proportional_clip_rgb( cx[0], hy, translator::translate("Departure"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[1], hy, translator::translate("Train"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[2], hy, translator::translate("Line"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[3], hy, translator::translate("Board destination"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[4] + cw[4], hy, translator::translate("Delay"), ALIGN_RIGHT, head_col, true );
	}
	else {
		display_proportional_clip_rgb( cx[0], hy, translator::translate("Line"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[1], hy, translator::translate("Board destination"), ALIGN_LEFT, head_col, true );
		display_proportional_clip_rgb( cx[2] + cw[2], hy, translator::translate("Departure"), ALIGN_RIGHT, head_col, true );
	}

	if(  rows.empty()  ) {
		display_proportional_clip_rgb( cx[0], p.y + header_h + D_V_SPACE/2, translator::translate("No departures"), ALIGN_LEFT, via_col, true );
		return;
	}

	// the row under the mouse opens its train
	const sint32 hover = getroffen( get_mouse_x() - offset.x, get_mouse_y() - offset.y ) ? get_row_at( get_mouse_y() - p.y ) : -1;

	cbuffer_t buf;
	for(  uint32 i = 0;  i < rows.get_count();  i++  ) {
		const bool second = show_second( rows[i].alt );
		const row_t &r = second ? alt_rows[rows[i].alt] : rows[i];
		const stream_t &s = streams[r.stream];
		const scr_coord_val ry = p.y + header_h + i * row_h;
		const scr_coord_val ty = ry + D_V_SPACE/2;

		if(  (sint32)i == hover  &&  r.cnv.is_bound()  ) {
			display_fillbox_wh_clip_rgb( p.x, ry, w, row_h, hover_col, true );
		}
		else if(  i & 1  ) {
			display_fillbox_wh_clip_rgb( p.x, ry, w, row_h, stripe, true );
		}

		buf.clear();
		format_time( buf, r );
		const scr_coord_val time_x = train ? cx[0] : cx[2] + cw[2];
		display_proportional_clip_rgb( time_x, ty, buf, (train ? ALIGN_LEFT : ALIGN_RIGHT), text_col, true );

		buf.clear();
		if(  r.delay > 0  ) {
			buf.printf( translator::translate(train ? "+%d min" : "+%d"), (int)r.delay );
			const int dc = train ? 4 : 3;
			display_proportional_clip_rgb( cx[dc] + cw[dc], ty, buf, ALIGN_RIGHT, delay_col, true );
		}

		// the line number on a badge: inverted on the train board, in the colour of the vehicle type on the other
		if(  s.line_number[0]  ) {
			const int lc = train ? 2 : 0;
			const scr_coord_val bw = proportional_string_width( s.line_number ) + 2*pad;
			const waytype_t wt = s.line.is_bound() ? s.line->get_schedule()->get_waytype() : s.cnv->get_schedule()->get_waytype();
			const PIXVAL badge_bg = train ? text_col : rgb( badge_color( wt ) );
			const PIXVAL badge_text = train ? bg : rgb( 0xFFFFFF );
			display_fillbox_wh_clip_rgb( cx[lc], ty - 1, bw, LINESPACE + 2, badge_bg, true );
			display_proportional_clip_rgb( cx[lc] + pad, ty, s.line_number, ALIGN_LEFT, badge_text, true );
		}

		if(  train  ) {
			// train number once a train is known, else the category of the line
			const scr_coord_val pair_w = rows[i].alt >= 0 ? pad + proportional_string_width( "2/2" ) : 0;
			const scr_coord_val tw = display_proportional_ellipsis_rgb( scr_rect( cx[1], ty, cw[1] - pair_w, LINESPACE ), r.train[0] ? r.train : s.category, ALIGN_LEFT, r.train[0] ? text_col : via_col, true );
			if(  rows[i].alt >= 0  ) {
				// a coupled pair: which of the two is shown
				display_proportional_clip_rgb( cx[1] + tw + pad, ty, second ? "2/2" : "1/2", ALIGN_LEFT, via_col, true );
			}
		}

		// destination, vias below it (trains) or after it (others)
		const scr_rect dest_rect( cx[dest_col], ty, cw[dest_col], LINESPACE );
		const scr_coord_val dest_w = display_proportional_ellipsis_rgb( dest_rect, s.destination, ALIGN_LEFT, text_col, true );
		if(  s.via[0]  ) {
			if(  train  ) {
				display_proportional_ellipsis_rgb( scr_rect( cx[dest_col], ty + LINESPACE, cw[dest_col], LINESPACE ), s.via, ALIGN_LEFT, via_col, true );
			}
			else if(  dest_w + pad < cw[dest_col]  ) {
				display_proportional_ellipsis_rgb( scr_rect( cx[dest_col] + dest_w + pad, ty, cw[dest_col] - dest_w - pad, LINESPACE ), s.via, ALIGN_LEFT, via_col, true );
			}
		}
	}
}


/* ---------------------------------------------------------------------------------------------
 * destination sign of a convoy
 */

// changes once a calendar minute (or every 1/1024 month without the calendar)
static sint64 board_stamp()
{
	return welt->has_calendar() ? welt->get_calendar_minutes() : (sint64)(welt->get_ticks() >> (welt->ticks_per_world_month_shift - 10));
}


bool convoy_sign_t::update(convoihandle_t cnv)
{
	if(  !cnv.is_bound()  ||  cnv->get_schedule() == NULL  ||  cnv->get_schedule()->empty()  ) {
		return false;
	}

	// a sign is asked for every frame (the window, the vehicle under the mouse): keep it for a minute
	struct cached_t {
		convoihandle_t cnv;
		sint64 stamp;
		convoy_sign_t sign;
	};
	static cached_t cache[8];
	static uint8 next_slot = 0;
	const sint64 stamp = board_stamp();
	for(  int i = 0;  i < 8;  i++  ) {
		if(  cache[i].cnv == cnv  &&  cache[i].stamp == stamp  ) {
			*this = cache[i].sign;
			return true;
		}
	}

	const schedule_t *schedule = cnv->get_schedule();
	const uint8 n = schedule->get_count();
	const player_t *owner = cnv->get_owner();
	const convoi_t *mover = get_mover( cnv );

	waytype = schedule->get_waytype();
	train_style = waytype == track_wt  ||  waytype == monorail_wt  ||  waytype == maglev_wt  ||  waytype == narrowgauge_wt;

	cbuffer_t buf;
	line_number[0] = 0;
	if(  cnv->get_line().is_bound()  ) {
		board_names_t::get_line_number( buf, cnv->get_line()->get_name() );
		tstrncpy( line_number, buf, lengthof(line_number) );
		buf.clear();
	}
	train[0] = 0;
	if(  train_style  ) {
		board_names_t::get_train_number( buf, cnv );
		tstrncpy( train, buf, lengthof(train) );
		buf.clear();
	}

	// the trip: from the stop it boards at now, else from the last one it boarded at; once it left an All off
	// stop after that it is empty until it loads again (e.g. arrival All off, a siding, then the departure stop)
	const uint8 cur = schedule->get_current_stop();
	uint8 e = cur;
	bool emptied = false;
	if(  !(mover->get_state() == convoi_t::LOADING  &&  schedule->entries[cur].loads()  &&  entry_halt( schedule, cur, owner ).is_bound())  ) {
		for(  uint8 k = 1;  k <= n;  k++  ) {
			const uint8 i = (cur + n - k) % n;
			if(  !entry_halt( schedule, i, owner ).is_bound()  ) {
				continue;
			}
			if(  schedule->entries[i].loads()  ) {
				e = i;
				break;
			}
			if(  schedule->entries[i].stop_type == schedule_entry_t::all_off  ) {
				e = i;
				emptied = true;
				break;
			}
		}
	}
	destination[0] = 0;
	dest_halt = halthandle_t();
	const halthandle_t start = entry_halt( schedule, e, owner );
	halthandle_t dest;
	vector_tpl<halthandle_t> on_the_way;
	if(  !emptied  &&  start.is_bound()  &&  find_destination( schedule, e, owner, start, train_style, dest, on_the_way )  ) {
		cbuffer_t town;
		board_names_t::get_stop_town( town, start->get_name(), train_style );
		board_names_t::get_stop_name( buf, dest->get_name(), train_style, town );
		tstrncpy( destination, buf, lengthof(destination) );
		dest_halt = dest;
		// the destination the schedule sets for this trip
		if(  const char *t = schedule->get_board_text( e, schedule_entry_t::BOARD_DESTINATION )  ) {
			tstrncpy( destination, t, lengthof(destination) );
		}
	}
	if(  const char *t = schedule->get_board_text( e, schedule_entry_t::BOARD_LINE )  ) {
		tstrncpy( line_number, t, lengthof(line_number) );
	}

	cache[next_slot].cnv = cnv;
	cache[next_slot].stamp = stamp;
	cache[next_slot].sign = *this;
	next_slot = (next_slot + 1) % 8;
	return true;
}


// the line number on a badge: inverted on the train look, in the colour of the vehicle type on the other
static scr_coord_val draw_line_badge(scr_coord_val x, scr_coord_val y, const char *number, bool train, waytype_t wt, bool draw)
{
	if(  !number[0]  ) {
		return 0;
	}
	const scr_coord_val pad = D_H_SPACE;
	const scr_coord_val w = proportional_string_width( number ) + 2*pad;
	if(  draw  ) {
		display_fillbox_wh_clip_rgb( x, y - 1, w, LINESPACE + 2, train ? rgb( TRAIN_TEXT ) : rgb( badge_color( wt ) ), true );
		display_proportional_clip_rgb( x + pad, y, number, ALIGN_LEFT, train ? rgb( TRAIN_BG ) : rgb( 0xFFFFFF ), true );
	}
	return w;
}


scr_coord_val convoy_sign_t::draw_row(scr_coord_val x, scr_coord_val y, bool draw) const
{
	const scr_coord_val pad = D_H_SPACE;
	const PIXVAL bg = rgb( train_style ? TRAIN_BG : OTHER_BG );
	const PIXVAL border = rgb( train_style ? TRAIN_HEAD_BG : OTHER_HEAD_BG );
	const PIXVAL text_col = rgb( train_style ? TRAIN_TEXT : OTHER_TEXT );
	const PIXVAL dim_col = rgb( train_style ? TRAIN_VIA : OTHER_VIA );
	const char *dest = destination[0] ? destination : translator::translate("Not in service");

	// measure: [train] [line] destination
	const scr_coord_val badge_w = draw_line_badge( 0, 0, line_number, train_style, waytype, false );
	const scr_coord_val train_w = train[0] ? proportional_string_width( train ) : 0;
	const scr_coord_val dest_w = proportional_string_width( dest );
	scr_coord_val w = pad + dest_w + pad;
	if(  train_w  ) {
		w += train_w + pad;
	}
	if(  badge_w  ) {
		w += badge_w + pad;
	}
	if(  !draw  ) {
		return w;
	}

	const scr_coord_val h = LINESPACE + 4;
	display_fillbox_wh_clip_rgb( x, y, w, h, border, true );
	display_fillbox_wh_clip_rgb( x + 1, y + 1, w - 2, h - 2, bg, true );
	scr_coord_val cx = x + pad;
	const scr_coord_val ty = y + 2;
	if(  train_w  ) {
		display_proportional_clip_rgb( cx, ty, train, ALIGN_LEFT, text_col, true );
		cx += train_w + pad;
	}
	if(  badge_w  ) {
		draw_line_badge( cx, ty, line_number, train_style, waytype, true );
		cx += badge_w + pad;
	}
	display_proportional_clip_rgb( cx, ty, dest, ALIGN_LEFT, destination[0] ? text_col : dim_col, true );
	return w;
}


void convoy_sign_t::draw_sign(scr_rect area) const
{
	const scr_coord_val pad = D_H_SPACE;
	const PIXVAL bg = rgb( train_style ? TRAIN_BG : OTHER_BG );
	const PIXVAL border = rgb( train_style ? TRAIN_HEAD_BG : OTHER_HEAD_BG );
	const PIXVAL text_col = rgb( train_style ? TRAIN_TEXT : OTHER_TEXT );
	const PIXVAL dim_col = rgb( train_style ? TRAIN_VIA : OTHER_VIA );

	display_fillbox_wh_clip_rgb( area.x, area.y, area.w, area.h, border, true );
	display_fillbox_wh_clip_rgb( area.x + 1, area.y + 1, area.w - 2, area.h - 2, bg, true );

	// one line, as on the vehicle: the line badge, then where it goes
	const scr_coord_val ty = area.y + (area.h - LINESPACE) / 2;
	const scr_coord_val badge_w = draw_line_badge( area.x + pad, ty, line_number, train_style, waytype, true );
	const scr_coord_val tx = area.x + pad + (badge_w ? badge_w + pad : 0);
	const char *dest = destination[0] ? destination : translator::translate("Not in service");
	display_proportional_ellipsis_rgb( scr_rect( tx, ty, area.x + area.w - pad - tx, LINESPACE ), dest, ALIGN_LEFT, destination[0] ? text_col : dim_col, true );
}


void gui_convoy_sign_t::draw(scr_coord offset)
{
	convoy_sign_t sign;
	if(  sign.update( cnv )  ) {
		sign.draw_sign( scr_rect( offset + pos, get_size() ) );
	}
}


scr_size gui_convoy_sign_t::get_min_size() const
{
	const scr_coord_val w = width_of ? width_of->get_min_size().w : proportional_string_width( "MMMMMMMMMM" );
	return scr_size( w, LINESPACE + D_V_SPACE + 2 );
}


void gui_departure_board_view_t::get_auto_texts(const schedule_t *schedule, uint8 entry, const player_t *owner, linehandle_t line, cbuffer_t &line_number, cbuffer_t &destination)
{
	if(  line.is_bound()  ) {
		board_names_t::get_line_number( line_number, line->get_name() );
	}
	if(  schedule == NULL  ||  entry >= schedule->get_count()  ) {
		return;
	}
	const waytype_t wt = schedule->get_waytype();
	const bool train = wt == track_wt  ||  wt == monorail_wt  ||  wt == maglev_wt  ||  wt == narrowgauge_wt;
	const halthandle_t start = entry_halt( schedule, entry, owner );
	halthandle_t dest;
	vector_tpl<halthandle_t> on_the_way;
	if(  start.is_bound()  &&  find_destination( schedule, entry, owner, start, train, dest, on_the_way )  ) {
		cbuffer_t town;
		board_names_t::get_stop_town( town, start->get_name(), train );
		board_names_t::get_stop_name( destination, dest->get_name(), train, town );
	}
}
