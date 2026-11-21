/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork, platforms: the owner's markers at stops to platform numbers, and away (see the header).
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <set>
#include <map>

#include "savegame_platform_markers.h"

#include "../simworld.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../simplatform.h"
#include "../simdebug.h"
#include "../boden/grund.h"
#include "../obj/label.h"
#include "../player/simplay.h"
#include "../dataobj/schedule.h"
#include "../utils/cbuffer_t.h"
#include "../gui/departure_board.h"


// ---------------------------------------------------------------------------------------------
// reading marker texts

static bool is_word_byte(uint8 c)
{
	return isalnum( c )  ||  c >= 0x80;
}


// "–" (en dash, UTF-8) or "-" or ":" after spaces; returns the text after it (and its spaces), NULL if none
static const char *after_separator(const char *p)
{
	while(  *p == ' '  ) {
		p++;
	}
	if(  *p == ':'  ||  *p == '-'  ) {
		p++;
	}
	else if(  (uint8)p[0] == 0xE2  &&  (uint8)p[1] == 0x80  &&  ((uint8)p[2] == 0x93  ||  (uint8)p[2] == 0x94)  ) {
		p += 3;
	}
	else {
		return NULL;
	}
	if(  *p != ' '  &&  *p != 0  ) {
		return NULL;
	}
	while(  *p == ' '  ) {
		p++;
	}
	return p;
}


/**
 * The platform number a part of a marker starts with: "N3/7:" and "K2:" (track), "14 -", "3:", "J -". Returns the
 * text after it, or NULL without one.
 */
static const char *parse_prefix(const char *p, std::string &number, bool &track)
{
	number.clear();
	track = false;
	while(  *p == ' '  ) {
		p++;
	}
	const char *q = p;
	if(  *q == 'N'  &&  isdigit( (uint8)q[1] )  ) {
		q++;
		while(  isdigit( (uint8)*q )  ||  islower( (uint8)*q )  ) {
			q++;
		}
		if(  *q == '/'  ) {
			const char *n = ++q;
			while(  isalnum( (uint8)*q )  ) {
				q++;
			}
			if(  q > n  &&  q - n <= 4  ) {
				if(  const char *r = after_separator( q )  ) {
					number.assign( n, q - n );
					track = true;
					return r;
				}
			}
		}
		return NULL;
	}
	if(  *q == 'K'  &&  isdigit( (uint8)q[1] )  ) {
		const char *n = ++q;
		while(  isdigit( (uint8)*q )  ||  islower( (uint8)*q )  ) {
			q++;
		}
		if(  q - n <= 4  ) {
			if(  const char *r = after_separator( q )  ) {
				number.assign( n, q - n );
				track = true;
				return r;
			}
		}
		return NULL;
	}
	if(  isdigit( (uint8)*q )  &&  *q != '0'  ) {
		const char *n = q;
		while(  isdigit( (uint8)*q )  ) {
			q++;
		}
		if(  q - n > 2  ) {
			// "280: směr Olomouc" is a railway line, no stand
			return NULL;
		}
		if(  islower( (uint8)*q )  &&  !is_word_byte( (uint8)q[1] )  ) {
			q++;
		}
		if(  const char *r = after_separator( q )  ) {
			number.assign( n, q - n );
			return r;
		}
		return NULL;
	}
	if(  isupper( (uint8)*q )  &&  !is_word_byte( (uint8)q[1] )  ) {
		if(  const char *r = after_separator( q + 1 )  ) {
			number.assign( q, 1 );
			return r;
		}
	}
	return NULL;
}


/// splits a marker at " | " into its parts
static void split_parts(const char *text, std::vector<std::string> &parts)
{
	parts.clear();
	std::string s( text );
	size_t start = 0;
	while(  true  ) {
		const size_t bar = s.find( " | ", start );
		parts.push_back( s.substr( start, bar == std::string::npos ? std::string::npos : bar - start ) );
		if(  bar == std::string::npos  ) {
			break;
		}
		start = bar + 3;
	}
}


// "RJ1/1-7": RJ1/1 ... RJ1/7; a range that is reversed or longer than 20 is no line anybody has, so a marker
// naming it is never taken for one whose lines the cards show
static void expand_range(const std::string &base, int from, int to, const std::string &word, std::vector<std::string> &out)
{
	if(  to < from  ||  to - from >= 20  ) {
		out.push_back( "?range " + word );
		return;
	}
	for(  int k = from;  k <= to;  k++  ) {
		char b[32];
		sprintf( b, "%s/%d", base.c_str(), k );
		out.push_back( b );
	}
}


/**
 * The line numbers a text names: words with a digit ("S3", "930", "Ex1/1"); after "Ex1/1" the bare numbers that
 * follow in the same list ("Ex1/1,2,4") are Ex1/2, Ex1/4, and "RJ1/1-7" is RJ1/1 ... RJ1/7. Anything in <> is left
 * out (lines the owner marks as not running there all the time).
 */
static void line_tokens(const char *text, std::vector<std::string> &out)
{
	out.clear();
	std::string base;     // "Ex1" of the last "Ex1/1"
	const char *p = text;
	while(  *p  ) {
		if(  *p == '<'  ) {
			while(  *p  &&  *p != '>'  ) {
				p++;
			}
			continue;
		}
		if(  !is_word_byte( (uint8)*p )  ) {
			if(  *p != ','  &&  *p != ' '  ) {
				base.clear();
			}
			p++;
			continue;
		}
		const char *s = p;
		while(  is_word_byte( (uint8)*p )  ||  *p == '/'  ||  (*p == '-'  &&  isdigit( (uint8)p[1] )  &&  isdigit( (uint8)p[-1] ))  ) {
			p++;
		}
		std::string w( s, p - s );
		bool digit = false, high = false;
		for(  size_t i = 0;  i < w.size();  i++  ) {
			digit |= isdigit( (uint8)w[i] ) != 0;
			high |= (uint8)w[i] >= 0x80;
		}
		if(  digit  &&  !high  &&  w.size() > 14  ) {
			// too long for a line number ("RJ1/12345-12344"): no line on any card, so the marker stays
			out.push_back( "?long " + w );
			base.clear();
			continue;
		}
		if(  !digit  ||  high  ) {
			base.clear();
			continue;
		}
		const size_t slash = w.find( '/' );
		const size_t dash = w.find( '-' );
		bool all_digits = true;
		for(  size_t i = 0;  i < w.size();  i++  ) {
			all_digits &= isdigit( (uint8)w[i] ) != 0;
		}
		if(  slash != std::string::npos  ) {
			base = w.substr( 0, slash );
			const std::string rest = w.substr( slash + 1 );
			const size_t d = rest.find( '-' );
			if(  d != std::string::npos  ) {
				const int from = atoi( rest.c_str() ), to = atoi( rest.c_str() + d + 1 );
				expand_range( base, from, to, w, out );
			}
			else {
				out.push_back( w );
			}
		}
		else if(  all_digits  &&  !base.empty()  ) {
			out.push_back( base + "/" + w );
		}
		else if(  dash != std::string::npos  &&  !base.empty()  ) {
			const int from = atoi( w.c_str() ), to = atoi( w.c_str() + dash + 1 );
			expand_range( base, from, to, w, out );
		}
		else {
			base.clear();
			out.push_back( w );
		}
	}
}


// a line number followed by a place name ("345 Olomouc"): the marker names destinations
static bool names_destination(const char *text)
{
	const char *p = text;
	while(  *p  ) {
		if(  isdigit( (uint8)*p )  &&  (p == text  ||  !is_word_byte( (uint8)p[-1] ))  ) {
			const char *q = p;
			while(  is_word_byte( (uint8)*q )  ) {
				q++;
			}
			if(  *q == ' '  ) {
				const uint8 c = (uint8)q[1];
				if(  (isupper( c )  ||  c == 0xC4  ||  c == 0xC5  ||  c == 0xC3)  &&  strncmp( q + 1, "NAD", 3 ) != 0  ) {
					return true;
				}
			}
			p = q;
			continue;
		}
		p++;
	}
	return false;
}


// ---------------------------------------------------------------------------------------------

struct near_stop_t
{
	halthandle_t halt;
	std::set<std::string> known;   ///< chips and line codes of its platforms
};


// the codes of every line of the owner whose schedule stops at a halt, with vehicles or not (made once)
static std::map<uint16, std::set<std::string> > schedule_lines;

static void add_line_codes(linehandle_t line, std::set<std::string> &known)
{
	const char *n = line->get_name();
	const char *e = n;
	while(  *e  &&  *e != ' '  ) {
		e++;
	}
	known.insert( std::string( n, e - n ) );
	cbuffer_t buf;
	board_names_t::get_line_number( buf, n );
	if(  buf.len()  ) {
		known.insert( (const char *)buf );
	}
}

static void collect_schedule_lines(karte_t *welt)
{
	schedule_lines.clear();
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		player_t *pl = welt->get_player( i );
		if(  pl == NULL  ) {
			continue;
		}
		vector_tpl<linehandle_t> lines;
		pl->simlinemgmt.get_lines( simline_t::line, &lines );
		FOR( vector_tpl<linehandle_t>, const line, lines ) {
			const schedule_t *sch = line->get_schedule();
			for(  uint8 k = 0;  sch  &&  k < sch->get_count();  k++  ) {
				const halthandle_t h = haltestelle_t::get_halt( sch->entries[k].pos, pl );
				if(  h.is_bound()  ) {
					add_line_codes( line, schedule_lines[h.get_id()] );
				}
			}
		}
	}
}

static void collect_known(halthandle_t halt, std::set<std::string> &known)
{
	std::map<uint16, std::set<std::string> >::const_iterator it = schedule_lines.find( halt.get_id() );
	if(  it != schedule_lines.end()  ) {
		known.insert( it->second.begin(), it->second.end() );
	}
	FOR( vector_tpl<platform_t *>, const p, halt->get_platforms() ) {
		for(  uint32 g = 0;  g <= p->groups.get_count();  g++  ) {
			const vector_tpl<platform_member_t> &members = g < p->groups.get_count() ? p->groups[g]->members : p->arrivals;
			FOR( vector_tpl<platform_member_t>, const &m, members ) {
				known.insert( m.number );
				if(  m.line.is_bound()  ) {
					// the code the line name starts with: "Ex1/1 ! Praha hl.n. - ...", "R08 [R8] ..."
					const char *n = m.line->get_name();
					const char *e = n;
					while(  *e  &&  *e != ' '  ) {
						e++;
					}
					known.insert( std::string( n, e - n ) );
				}
			}
		}
	}
}


// the platforms of the stops near pos within one tile (a track: also the one pos is on)
struct candidate_t
{
	halthandle_t halt;
	const platform_t *p;
	bool on;    ///< pos is a tile of it
	uint8 tier; ///< 0 on it, 1 next to it in a row or column, 2 diagonally
};


static void find_candidates(const std::vector<near_stop_t> &stops, koord3d pos, std::vector<candidate_t> &out)
{
	out.clear();
	for(  size_t s = 0;  s < stops.size();  s++  ) {
		FOR( vector_tpl<platform_t *>, const p, stops[s].halt->get_platforms() ) {
			if(  !platforms_t::has_card( p )  ) {
				continue;
			}
			bool on = false;
			uint8 tier = 255;
			FOR( vector_tpl<koord3d>, const &k, p->tiles ) {
				const koord d = k.get_2d() - pos.get_2d();
				if(  k == pos  ) {
					// on it: the same tile, at the same height (not a road under a viaduct)
					on = true;
					tier = 0;
				}
				else if(  d == koord( 0, 0 )  ) {
					tier = min( tier, (uint8)1 );
				}
				else if(  koord_distance( k.get_2d(), pos.get_2d() ) <= 1  ) {
					tier = min( tier, (d.x == 0  ||  d.y == 0) ? 1 : 2 );
				}
			}
			if(  tier != 255  ) {
				candidate_t c;
				c.halt = stops[s].halt;
				c.p = p;
				c.on = on;
				c.tier = tier;
				out.push_back( c );
			}
		}
	}
}


// how many of the tokens are lines of the platform
static int overlap(const platform_t *p, const std::vector<std::string> &tokens)
{
	std::set<std::string> mine;
	for(  uint32 g = 0;  g <= p->groups.get_count();  g++  ) {
		const vector_tpl<platform_member_t> &members = g < p->groups.get_count() ? p->groups[g]->members : p->arrivals;
		FOR( vector_tpl<platform_member_t>, const &m, members ) {
			mine.insert( m.number );
		}
	}
	int n = 0;
	for(  size_t i = 0;  i < tokens.size();  i++  ) {
		n += mine.count( tokens[i] ) ? 1 : 0;
	}
	return n;
}


static void tsv_text(std::string &s, const char *t)
{
	for(  ;  *t;  t++  ) {
		s += (*t == '\t'  ||  *t == '\n') ? ' ' : *t;
	}
}


bool savegame_platform_markers_t::apply( karte_t *welt, const char *mode, const char *keep_path, const char *report_path )
{
	const bool numbers = mode  &&  (!strcmp( mode, "numbers" )  ||  !strcmp( mode, "both" ));
	const bool strip = mode  &&  (!strcmp( mode, "strip" )  ||  !strcmp( mode, "both" ));
	if(  !numbers  &&  !strip  ) {
		dbg->error( "savegame_platform_markers_t::apply()", "-platform-markers needs numbers, strip or both, not '%s'", mode ? mode : "" );
		return false;
	}

	std::set<std::string> keep;
	if(  keep_path  ) {
		FILE *f = fopen( keep_path, "r" );
		if(  f == NULL  ) {
			dbg->error( "savegame_platform_markers_t::apply()", "cannot read the keep file '%s'", keep_path );
			return false;
		}
		char line[512];
		while(  fgets( line, sizeof(line), f )  ) {
			size_t l = strlen( line );
			while(  l > 0  &&  (line[l-1] == '\n'  ||  line[l-1] == '\r')  ) {
				line[--l] = 0;
			}
			if(  l > 0  &&  line[0] != '#'  ) {
				keep.insert( line );
			}
		}
		fclose( f );
	}

	FILE *report = fopen( report_path, "w" );
	if(  report  ) {
		fprintf( report, "x\ty\tz\taction\tdetail\tstop\ttext\n" );
	}
	int n_numbered = 0, n_removed = 0, n_kept = 0, n_unclear = 0, n_dest = 0;

	if(  strip  ) {
		collect_schedule_lines( welt );
	}

	// the markers, copied first: stripping removes from the list
	vector_tpl<koord> positions;
	FOR( slist_tpl<koord>, const &k, welt->get_label_list() ) {
		positions.append( k );
	}
	std::set<std::string> numbered;    // halt,anchor,dir of the platforms numbered here
	FOR( vector_tpl<koord>, const &k, positions ) {
		grund_t *gr = welt->lookup_kartenboden( k );
		label_t *label = gr ? gr->find<label_t>() : NULL;
		if(  label == NULL  ||  gr->get_text() == NULL  ) {
			continue;
		}
		const std::string text = gr->get_text();
		const koord3d pos = gr->get_pos();

		// stops within one tile, on any level
		std::vector<near_stop_t> stops;
		bool kept_by_file = false;
		for(  int dy = -1;  dy <= 1;  dy++  ) {
			for(  int dx = -1;  dx <= 1;  dx++  ) {
				const planquadrat_t *pl = welt->access( k.x + dx, k.y + dy );
				for(  uint8 i = 0;  pl  &&  i < pl->get_boden_count();  i++  ) {
					const halthandle_t h = pl->get_boden_bei( i )->get_halt();
					if(  !h.is_bound()  ) {
						continue;
					}
					bool listed = false;
					for(  size_t s = 0;  s < stops.size();  s++  ) {
						listed |= stops[s].halt == h;
					}
					if(  !listed  ) {
						near_stop_t ns;
						ns.halt = h;
						stops.push_back( ns );
						kept_by_file |= keep.count( h->get_name() ) > 0;
					}
				}
			}
		}
		if(  stops.empty()  ) {
			continue;
		}
		char where[64];
		sprintf( where, "%d\t%d\t%d\t", pos.x, pos.y, pos.z );
		std::string stop_name;
		tsv_text( stop_name, stops[0].halt->get_name() );
		std::string text_tsv;
		tsv_text( text_tsv, text.c_str() );
		if(  kept_by_file  ) {
			if(  report  ) {
				fprintf( report, "%skept\tkeep file\t%s\t%s\n", where, stop_name.c_str(), text_tsv.c_str() );
			}
			n_kept++;
			continue;
		}

		// numbers
		std::vector<std::string> parts;
		split_parts( text.c_str(), parts );
		if(  numbers  ) {
			// the platforms other parts of this marker took (by key), and the parts in the order that settles most:
			// first those that name lines, then the others (they take what is left)
			std::set<std::string> taken_here;
			std::vector<size_t> order;
			for(  int pass = 0;  pass < 2;  pass++  ) {
				for(  size_t pi = 0;  pi < parts.size();  pi++  ) {
					std::string nb;
					bool tr;
					const char *r = parse_prefix( parts[pi].c_str(), nb, tr );
					std::vector<std::string> t;
					if(  r  ) {
						line_tokens( r, t );
					}
					if(  (pass == 0) == !t.empty()  ) {
						order.push_back( pi );
					}
				}
			}
			for(  size_t oi = 0;  oi < order.size();  oi++  ) {
				const size_t pi = order[oi];
				std::string number;
				bool track;
				const char *rest = parse_prefix( parts[pi].c_str(), number, track );
				if(  rest == NULL  ) {
					continue;
				}
				std::vector<std::string> tokens;
				line_tokens( rest, tokens );
				std::vector<candidate_t> cands;
				find_candidates( stops, pos, cands );
				candidate_t chosen;
				bool have = false;
				std::string why;
				// a track marker on its track
				for(  size_t c = 0;  c < cands.size()  &&  !have;  c++  ) {
					if(  cands[c].on  &&  cands[c].p->kind == platform_t::TRACK  ) {
						chosen = cands[c];
						have = true;
					}
				}
				// else the nearest ones (on it, then beside it, then diagonally), without those other parts of this
				// marker took; one left: that one, several: the one whose lines the text names most
				for(  uint8 tier = 0;  tier <= 2  &&  !have  &&  why.empty();  tier++  ) {
					std::vector<candidate_t> fit;
					for(  size_t c = 0;  c < cands.size();  c++  ) {
						if(  cands[c].tier != tier  ||  (track  &&  cands[c].p->kind != platform_t::TRACK)  ) {
							continue;
						}
						const koord3d a = cands[c].p->get_anchor();
						char k2[64];
						sprintf( k2, "%u,%d,%d,%d,%u", cands[c].halt.get_id(), a.x, a.y, a.z, cands[c].p->kind == platform_t::STAND ? cands[c].p->dir : 0 );
						if(  taken_here.count( k2 )  ) {
							continue;
						}
						fit.push_back( cands[c] );
					}
					if(  fit.size() == 1  ) {
						chosen = fit[0];
						have = true;
					}
					else if(  fit.size() > 1  ) {
						int best = 0, best_count = 0;
						size_t best_i = 0;
						for(  size_t c = 0;  c < fit.size();  c++  ) {
							const int o = overlap( fit[c].p, tokens );
							if(  o > best  ) {
								best = o;
								best_count = 1;
								best_i = c;
							}
							else if(  o == best  &&  o > 0  ) {
								best_count++;
							}
						}
						if(  best > 0  &&  best_count == 1  ) {
							chosen = fit[best_i];
							have = true;
						}
						else {
							why = "several platforms near it";
						}
					}
				}
				if(  !have  &&  why.empty()  ) {
					why = "no platform near it";
				}
				std::string part_tsv;
				tsv_text( part_tsv, parts[pi].c_str() );
				char key[64] = "";
				if(  have  ) {
					const koord3d a = chosen.p->get_anchor();
					sprintf( key, "%u,%d,%d,%d,%u", chosen.halt.get_id(), a.x, a.y, a.z, chosen.p->kind == platform_t::STAND ? chosen.p->dir : 0 );
					if(  numbered.count( key )  ) {
						if(  strcmp( chosen.p->number, number.c_str() ) == 0  ) {
							// the same number again (a marker at each end)
							continue;
						}
						why = std::string( "the platform got " ) + chosen.p->number + " from another marker";
						have = false;
					}
				}
				if(  have  ) {
					const platform_t *p = chosen.p;
					halthandle_t h = chosen.halt;
					const koord3d anchor = p->get_anchor();
					const uint8 dir = p->kind == platform_t::STAND ? p->dir : 0;
					const bool stand = p->kind == platform_t::STAND;
					h->set_platform_number( anchor, dir, number.c_str() );
					numbered.insert( key );
					taken_here.insert( key );
					n_numbered++;
					if(  report  ) {
						std::string hn;
						tsv_text( hn, h->get_name() );
						fprintf( report, "%snumber\t%s %s at %d,%d%s\t%s\t%s\n", where, stand ? "stand" : "track", number.c_str(),
							anchor.x, anchor.y, dir ? (dir == ribi_t::north ? " N" : dir == ribi_t::east ? " E" : dir == ribi_t::south ? " S" : " W") : "", hn.c_str(), part_tsv.c_str() );
					}
					if(  stand  &&  names_destination( rest )  &&  !(h->get_platform_flags() & haltestelle_t::PLATFORMS_STANDS_DEST)  ) {
						h->set_platform_flags( h->get_platform_flags() | haltestelle_t::PLATFORMS_STANDS_DEST );
						n_dest++;
						if(  report  ) {
							std::string hn;
							tsv_text( hn, h->get_name() );
							fprintf( report, "%sdestinations\tstand cards show destinations\t%s\t%s\n", where, hn.c_str(), part_tsv.c_str() );
						}
					}
				}
				else {
					n_unclear++;
					if(  report  ) {
						fprintf( report, "%sno number\t%s %s: %s\t%s\t%s\n", where, track ? "track" : "platform", number.c_str(), why.c_str(), stop_name.c_str(), part_tsv.c_str() );
					}
				}
			}
		}

		// strip
		if(  strip  ) {
			std::vector<std::string> tokens;
			for(  size_t pi = 0;  pi < parts.size();  pi++  ) {
				std::string number;
				bool track;
				const char *rest = parse_prefix( parts[pi].c_str(), number, track );
				std::vector<std::string> t;
				line_tokens( rest ? rest : parts[pi].c_str(), t );
				tokens.insert( tokens.end(), t.begin(), t.end() );
			}
			if(  tokens.empty()  ) {
				if(  report  ) {
					fprintf( report, "%skept\tno line numbers\t%s\t%s\n", where, stop_name.c_str(), text_tsv.c_str() );
				}
				n_kept++;
				continue;
			}
			// a platform number the marker gives must be on a platform near it (taken now or before), else the
			// marker is its only record and stays
			std::string lost;
			{
				std::vector<candidate_t> cands;
				find_candidates( stops, pos, cands );
				for(  size_t pi = 0;  pi < parts.size();  pi++  ) {
					std::string number;
					bool track;
					if(  parse_prefix( parts[pi].c_str(), number, track ) == NULL  ) {
						continue;
					}
					bool found = false;
					for(  size_t c = 0;  c < cands.size()  &&  !found;  c++  ) {
						found = number == cands[c].p->number;
					}
					if(  !found  ) {
						lost += lost.empty() ? number : ", " + number;
					}
				}
			}
			if(  !lost.empty()  ) {
				n_kept++;
				if(  report  ) {
					fprintf( report, "%skept\tits number %s is on no platform near it\t%s\t%s\n", where, lost.c_str(), stop_name.c_str(), text_tsv.c_str() );
				}
				continue;
			}
			std::set<std::string> known;
			for(  size_t s = 0;  s < stops.size();  s++  ) {
				collect_known( stops[s].halt, known );
			}
			std::string unknown;
			for(  size_t i = 0;  i < tokens.size();  i++  ) {
				if(  !known.count( tokens[i] )  ) {
					if(  !unknown.empty()  ) {
						unknown += ", ";
					}
					unknown += tokens[i];
				}
			}
			if(  unknown.empty()  ) {
				delete label;
				n_removed++;
				if(  report  ) {
					fprintf( report, "%sremoved\t\t%s\t%s\n", where, stop_name.c_str(), text_tsv.c_str() );
				}
			}
			else {
				n_kept++;
				if(  report  ) {
					fprintf( report, "%skept\tnot on the cards: %s\t%s\t%s\n", where, unknown.c_str(), stop_name.c_str(), text_tsv.c_str() );
				}
			}
		}
	}
	if(  report  ) {
		fclose( report );
	}
	dbg->message( "savegame_platform_markers_t::apply()", "%d numbers, %d stops with destinations, %d markers removed, %d kept, %d numbers unclear; report %s",
		n_numbered, n_dest, n_removed, n_kept, n_unclear, report_path );
	printf( "platform markers: %d numbers, %d stops with destinations, %d markers removed, %d kept, %d numbers unclear; report %s\n",
		n_numbered, n_dest, n_removed, n_kept, n_unclear, report_path );
	return true;
}
