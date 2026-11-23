/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork: platforms of a stop and the lines that board on each (see simplatform.h and
 * documentation/fork-platforms-plan.md).
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simplatform.h"
#include "simhalt.h"
#include "simline.h"
#include "simconvoi.h"
#include "simworld.h"

#include "bauer/goods_manager.h"
#include "boden/grund.h"
#include "boden/wege/weg.h"
#include "boden/wege/schiene.h"
#include "dataobj/schedule.h"
#include "dataobj/translator.h"
#include "descriptor/building_desc.h"
#include "gui/departure_board.h"
#include "obj/gebaeude.h"
#include "player/simplay.h"
#include "utils/cbuffer_t.h"
#include "utils/simstring.h"
#include "vehicle/simvehicle.h"


uint32 platforms_t::epoch = 0;


template<class T> static void copy_vector(vector_tpl<T> &dst, const vector_tpl<T> &src)
{
	dst.clear();
	for(  uint32 i = 0;  i < src.get_count();  i++  ) {
		dst.append( src[i] );
	}
}


void halt_platforms_t::clear()
{
	FOR( vector_tpl<platform_t *>, p, list ) {
		delete p;
	}
	list.clear();
}


const char *platform_t::get_kind_name() const
{
	switch(  kind  ) {
		case TRACK: return translator::translate( "Track" );
		case STAND: return translator::translate( "Stand" );
		default:    return translator::translate( wt == air_wt ? "Gate" : "Berth" );
	}
}


// ---------------------------------------------------------------------------------------------
// rows of tiles

// the waytype of the stop on this tile (its building); invalid_wt without a stop building
static waytype_t stop_waytype(const grund_t *gr)
{
	const gebaeude_t *gb = gr->find<gebaeude_t>();
	if(  gb == NULL  ||  gb->get_tile() == NULL  ) {
		return invalid_wt;
	}
	const building_desc_t *d = gb->get_tile()->get_desc();
	if(  d->get_type() != building_desc_t::generic_stop  &&  d->get_type() != building_desc_t::dock  &&  d->get_type() != building_desc_t::flat_dock  ) {
		return invalid_wt;
	}
	return (waytype_t)d->get_extra();
}


// the owner's parking tiles, road stops that only stretch a stop's catchment (see savegame_strip_parking)
static const char PARKING_PREFIX[] = "parking_(catchemnt_area_ext)";

/**
 * A road or tram stop tile that is no stand:
 * - a layover bay (waiting bay), always;
 * - a loading bay (a building for goods only, e.g. pak128.cs Zasobovani and CarStop) or a parking tile, unless a
 *   passenger or mail line stops on it: the owner's game also has bus stands on such buildings (the underground UAN
 *   Florenc).
 * Bus and tram stop poles that enable nothing themselves (the stop's main building enables passengers) stay stands.
 * Station tracks stay tracks whatever they take: stations number all their tracks.
 */
static bool is_no_stand(const grund_t *gr, const vector_tpl<koord3d> &passenger_stops)
{
	const gebaeude_t *gb = gr->find<gebaeude_t>();
	if(  gb == NULL  ||  gb->get_tile() == NULL  ) {
		return false;
	}
	const building_desc_t *d = gb->get_tile()->get_desc();
	if(  d->get_layover_kind() != 0  ) {
		return true;
	}
	const bool goods_only = (d->get_enabled() & 3) == 0  &&  (d->get_enabled() & 4) != 0;
	const bool parking = strncmp( d->get_name(), PARKING_PREFIX, strlen( PARKING_PREFIX ) ) == 0;
	return (goods_only  ||  parking)  &&  !passenger_stops.is_contained( gr->get_pos() );
}


static uint8 kind_of(waytype_t wt)
{
	switch(  wt  ) {
		case track_wt:
		case monorail_wt:
		case maglev_wt:
		case narrowgauge_wt:
			return platform_t::TRACK;
		case road_wt:
		case tram_wt:
			return platform_t::STAND;
		case water_wt:
		case air_wt:
			return platform_t::OTHER;
		default:
			return 255;
	}
}


// the way vehicles of the stop use on its tile (trams run on track_wt)
static const weg_t *stop_way(const grund_t *gr, waytype_t wt)
{
	if(  wt == tram_wt  ) {
		const weg_t *w = gr->get_weg( road_wt );
		return w ? w : gr->get_weg( track_wt );
	}
	return gr->get_weg( wt );
}


struct cell_t
{
	grund_t *gr;
	uint8 kind;
	waytype_t wt;         ///< the stop's waytype
	waytype_t way_wt;     ///< the waytype of the way the row runs along
	ribi_t::ribi ribi;    ///< the way's directions
	ribi_t::ribi axis;    ///< straight: both directions of it; 0: a tile on its own
	sint32 row;
};


static sint32 find_cell(const vector_tpl<cell_t> &cells, koord3d pos)
{
	for(  uint32 i = 0;  i < cells.get_count();  i++  ) {
		if(  cells[i].gr->get_pos() == pos  ) {
			return i;
		}
	}
	return -1;
}


// position along the axis, for sorting a row
static sint32 along(koord3d k, ribi_t::ribi axis)
{
	return axis == ribi_t::northsouth ? k.y : k.x;
}


// position across the axis, for numbering tracks across the station
static sint32 across(koord3d k, ribi_t::ribi axis)
{
	return axis == ribi_t::northsouth ? k.x : k.y;
}


static void sort_tiles(vector_tpl<koord3d> &tiles, ribi_t::ribi axis)
{
	// insertion sort: rows are short
	for(  uint32 i = 1;  i < tiles.get_count();  i++  ) {
		const koord3d k = tiles[i];
		uint32 j = i;
		while(  j > 0  &&  along( tiles[j-1], axis ) > along( k, axis )  ) {
			tiles[j] = tiles[j-1];
			j--;
		}
		tiles[j] = k;
	}
}


// the travel directions of a row's two ends: "low" is west or north, "high" east or south
static ribi_t::ribi low_dir(ribi_t::ribi axis) { return axis == ribi_t::northsouth ? ribi_t::north : ribi_t::west; }
static ribi_t::ribi high_dir(ribi_t::ribi axis) { return axis == ribi_t::northsouth ? ribi_t::south : ribi_t::east; }


// ---------------------------------------------------------------------------------------------
// line numbers

int platforms_t::compare_numbers(const char *a, const char *b)
{
	// letters
	const char *pa = a, *pb = b;
	while(  *pa  &&  !isdigit( (uint8)*pa )  ) {
		pa++;
	}
	while(  *pb  &&  !isdigit( (uint8)*pb )  ) {
		pb++;
	}
	const size_t la = pa - a, lb = pb - b;
	if(  la == 0  &&  lb != 0  ) {
		return -1;
	}
	if(  lb == 0  &&  la != 0  ) {
		return 1;
	}
	const int c = STRNICMP( a, b, min( la, lb ) );
	if(  c != 0  ) {
		return c;
	}
	if(  la != lb  ) {
		return la < lb ? -1 : 1;
	}
	// then the number
	const long na = *pa ? strtol( pa, NULL, 10 ) : -1;
	const long nb = *pb ? strtol( pb, NULL, 10 ) : -1;
	if(  na != nb  ) {
		return na < nb ? -1 : 1;
	}
	return strcmp( a, b );
}


static void sort_members(vector_tpl<platform_member_t> &m)
{
	for(  uint32 i = 1;  i < m.get_count();  i++  ) {
		const platform_member_t k = m[i];
		uint32 j = i;
		while(  j > 0  &&  platforms_t::compare_numbers( m[j-1].number, k.number ) > 0  ) {
			m[j] = m[j-1];
			j--;
		}
		m[j] = k;
	}
}


// the next entry after i that has a halt (waypoints, depots and Hold entries skipped); n if none
static uint8 next_halt_entry(const schedule_t *schedule, uint8 i, const player_t *owner, bool get_off)
{
	const uint8 n = schedule->get_count();
	for(  uint8 k = 1;  k < n;  k++  ) {
		const uint8 j = (i + k) % n;
		const schedule_entry_t &e = schedule->entries[j];
		if(  e.stop_type == schedule_entry_t::hold  ||  !haltestelle_t::get_halt( e.pos, owner ).is_bound()  ) {
			continue;
		}
		if(  get_off  &&  !e.plans_arrival()  ) {
			continue;
		}
		return j;
	}
	return n;
}


// ---------------------------------------------------------------------------------------------
// directions

// the travel direction on arrival: what the line's vehicles did, else a guess from where it comes from
static ribi_t::ribi arrival_dir(const schedule_t *schedule, uint8 entry, linehandle_t line, ribi_t::ribi axis)
{
	if(  line.is_bound()  ) {
		const ribi_t::ribi learned = line->get_entry_arrival( entry );
		if(  learned  &&  (learned & axis)  ) {
			return learned;
		}
	}
	const uint8 n = schedule->get_count();
	const koord3d here = schedule->entries[entry].pos;
	for(  uint8 k = 1;  k < n;  k++  ) {
		const koord3d from = schedule->entries[(entry + n - k) % n].pos;
		const sint32 d = along( here, axis ) - along( from, axis );
		if(  d != 0  ) {
			return d > 0 ? high_dir( axis ) : low_dir( axis );
		}
	}
	return high_dir( axis );
}


// the way out: what the line's vehicles did, else a guess from where it goes next
static ribi_t::ribi departure_dir(const platform_t *p, const schedule_t *schedule, uint8 entry, uint8 next, linehandle_t line)
{
	if(  line.is_bound()  ) {
		const ribi_t::ribi learned = line->get_entry_departure( entry );
		if(  learned  ) {
			return learned;
		}
	}
	if(  p->kind == platform_t::STAND  ) {
		return p->dead_end ? ribi_t::backward( p->dir ) : p->dir;
	}
	if(  p->kind == platform_t::TRACK  &&  p->axis  &&  next < schedule->get_count()  ) {
		const sint32 d = along( schedule->entries[next].pos, p->axis ) - along( schedule->entries[entry].pos, p->axis );
		return d >= 0 ? high_dir( p->axis ) : low_dir( p->axis );
	}
	return 0;
}


// ---------------------------------------------------------------------------------------------
// finding platforms

sint32 platforms_t::find(const vector_tpl<platform_t *> &list, koord3d pos, ribi_t::ribi dir)
{
	sint32 first = -1;
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		const platform_t *p = list[i];
		if(  !p->contains( pos )  ) {
			continue;
		}
		if(  p->kind != platform_t::STAND  ||  dir == 0  ||  p->dir == dir  ||  p->dead_end  ) {
			return i;
		}
		if(  first < 0  ) {
			first = i;
		}
	}
	return first;
}


sint32 platforms_t::find_schedule_platform(const vector_tpl<platform_t *> &list, const schedule_t *schedule, uint8 entry, linehandle_t line)
{
	if(  schedule == NULL  ||  entry >= schedule->get_count()  ) {
		return -1;
	}
	const koord3d pos = schedule->entries[entry].pos;
	sint32 found = -1, count = 0;
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		if(  list[i]->contains( pos )  ) {
			if(  found < 0  ) {
				found = i;
			}
			count++;
		}
	}
	if(  count <= 1  ) {
		return found;
	}
	// a through stand: the one of the travel direction
	const ribi_t::ribi dir = arrival_dir( schedule, entry, line, list[found]->axis );
	return find( list, pos, dir );
}


sint32 platforms_t::find_entry_platform(haltestelle_t *halt, linehandle_t line, uint8 entry)
{
	if(  halt == NULL  ||  !line.is_bound()  ) {
		return -1;
	}
	return find_schedule_platform( halt->get_platforms(), line->get_schedule(), entry, line );
}


void platforms_t::append_entry_platform(cbuffer_t &buf, const schedule_t *schedule, uint8 entry, linehandle_t line, const player_t *owner)
{
	if(  schedule == NULL  ||  entry >= schedule->get_count()  ) {
		return;
	}
	halthandle_t halt = haltestelle_t::get_halt( schedule->entries[entry].pos, owner );
	if(  !halt.is_bound()  ) {
		return;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	const sint32 i = find_schedule_platform( list, schedule, entry, line );
	if(  i >= 0  &&  list[i]->number[0]  ) {
		buf.append( ", " );
		get_platform_text( buf, list[i] );
	}
}


void platforms_t::get_platform_text(cbuffer_t &buf, const platform_t *p)
{
	if(  p == NULL  ||  p->number[0] == 0  ) {
		return;
	}
	switch(  p->kind  ) {
		case platform_t::TRACK: buf.printf( translator::translate( "track %s" ), p->number ); break;
		case platform_t::STAND: buf.printf( translator::translate( "stand %s" ), p->number ); break;
		default:                buf.printf( translator::translate( "berth %s" ), p->number ); break;
	}
}


// the first word of a line name, for a line without a number ("NAD 280 M7 ..." -> NAD)
static void first_word(char *buf, size_t len, const char *name)
{
	size_t i = 0;
	while(  name[i]  &&  name[i] != ' '  &&  i < 7  &&  i + 1 < len  ) {
		buf[i] = name[i];
		i++;
	}
	buf[i] = 0;
	if(  i == 0  ) {
		tstrncpy( buf, "?", len );
	}
}


void platforms_t::get_runs(const vector_tpl<platform_member_t> &members, bool with_destinations, vector_tpl<platform_run_t> &runs)
{
	runs.clear();
	if(  !with_destinations  ) {
		runs.append( platform_run_t() );
		FOR( vector_tpl<platform_member_t>, const &m, members ) {
			bool seen = false;
			FOR( vector_tpl<const platform_member_t *>, const c, runs[0].chips ) {
				seen |= strcmp( c->number, m.number ) == 0;
			}
			if(  !seen  ) {
				runs[0].chips.append( &m );
			}
		}
		return;
	}
	// each number with its destinations, in order
	FOR( vector_tpl<platform_member_t>, const &m, members ) {
		sint32 r = -1;
		for(  uint32 i = 0;  i < runs.get_count();  i++  ) {
			if(  strcmp( runs[i].chips[0]->number, m.number ) == 0  ) {
				r = i;
			}
		}
		if(  r < 0  ) {
			runs.append( platform_run_t() );
			r = runs.get_count() - 1;
			runs[r].chips.append( &m );
			tstrncpy( runs[r].destination, m.destination, lengthof(runs[r].destination) );
			continue;
		}
		// another place this number goes
		platform_run_t &run = runs[r];
		bool listed = false;
		const size_t l = strlen( m.destination );
		for(  const char *p = run.destination;  (p = strstr( p, m.destination )) != NULL;  p++  ) {
			if(  (p == run.destination  ||  (p >= run.destination + 2  &&  p[-2] == ',')) && (p[l] == 0  ||  p[l] == ',')  ) {
				listed = true;
				break;
			}
		}
		if(  !listed  &&  strlen( run.destination ) + l + 3 < lengthof(run.destination)  ) {
			strcat( run.destination, ", " );
			strcat( run.destination, m.destination );
		}
	}
	// numbers going to the same places share a run
	for(  uint32 i = 1;  i < runs.get_count();  ) {
		sint32 same = -1;
		for(  uint32 k = 0;  k < i  &&  same < 0;  k++  ) {
			if(  strcmp( runs[k].destination, runs[i].destination ) == 0  ) {
				same = k;
			}
		}
		if(  same >= 0  ) {
			runs[same].chips.append( runs[i].chips[0] );
			runs.remove_at( i );
		}
		else {
			i++;
		}
	}
}


// ---------------------------------------------------------------------------------------------
// building

// labels: each platform takes the first label on one of its tiles; a label whose tile went takes a platform
// next to it; other labels go; tracks without one get the next free number (plan 2)
static void apply_labels(haltestelle_t *halt, vector_tpl<platform_t *> &list, bool mutate)
{
	vector_tpl<platform_label_t> &labels = halt->access_platform_labels();
	vector_tpl<sint32> owner_of( list.get_count() );
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		owner_of.append( -1 );
	}
	vector_tpl<bool> used( labels.get_count() );
	for(  uint32 k = 0;  k < labels.get_count();  k++  ) {
		used.append( false );
	}
	// on a tile of the platform
	for(  uint32 k = 0;  k < labels.get_count();  k++  ) {
		for(  uint32 i = 0;  i < list.get_count();  i++  ) {
			const platform_t *p = list[i];
			if(  owner_of[i] < 0  &&  p->contains( labels[k].anchor )  &&  (p->kind != platform_t::STAND  ||  p->dead_end  ||  labels[k].dir == p->dir)  ) {
				owner_of[i] = k;
				used[k] = true;
				break;
			}
		}
	}
	// the tile went: a platform with a tile next to it
	for(  uint32 k = 0;  k < labels.get_count();  k++  ) {
		if(  used[k]  ) {
			continue;
		}
		bool on_some = false;
		for(  uint32 i = 0;  i < list.get_count()  &&  !on_some;  i++  ) {
			on_some = list[i]->contains( labels[k].anchor );
		}
		if(  on_some  ) {
			// a second label of a platform (two rows joined): the older one won
			continue;
		}
		const grund_t *at = world()->lookup( labels[k].anchor );
		if(  at  &&  at->get_halt().is_bound()  &&  at->get_halt().get_rep() == halt  ) {
			// the tile is still the stop's but no platform now (a layover bay, or a goods building no passenger
			// line stops at any more): the label stays there, for when it is a stand again, and moves nowhere
			used[k] = true;
			continue;
		}
		for(  uint32 i = 0;  i < list.get_count();  i++  ) {
			platform_t *p = list[i];
			if(  owner_of[i] >= 0  ||  p->kind == platform_t::OTHER  ||  (p->kind == platform_t::STAND  &&  !p->dead_end  &&  labels[k].dir != p->dir)  ) {
				continue;
			}
			bool next_to = false;
			FOR( vector_tpl<koord3d>, const &t, p->tiles ) {
				next_to |= koord_distance( t.get_2d(), labels[k].anchor.get_2d() ) == 1;
			}
			if(  next_to  ) {
				owner_of[i] = k;
				used[k] = true;
				if(  mutate  ) {
					labels[k].anchor = p->get_anchor();
				}
				break;
			}
		}
	}
	// the numbers, and which labels stay
	vector_tpl<platform_label_t> kept;
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		if(  owner_of[i] >= 0  ) {
			tstrncpy( list[i]->number, labels[owner_of[i]].text, lengthof(list[i]->number) );
		}
	}
	if(  !mutate  ) {
		// only reading: tracks without a number stay so until update_labels gives them one
		return;
	}
	// tracks without a label: the next free number, across the station
	vector_tpl<uint32> unnumbered;
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		if(  owner_of[i] < 0  &&  list[i]->kind == platform_t::TRACK  ) {
			unnumbered.append( i );
		}
	}
	for(  uint32 a = 1;  a < unnumbered.get_count();  a++  ) {
		const uint32 v = unnumbered[a];
		uint32 b = a;
		while(  b > 0  ) {
			const platform_t *p = list[unnumbered[b-1]], *q = list[v];
			// the highest level first (a metro under a station gets the numbers after it), then by axis and across
			const sint32 kp = (-p->get_anchor().z) * 2 + (p->axis == ribi_t::northsouth ? 1 : 0);
			const sint32 kq = (-q->get_anchor().z) * 2 + (q->axis == ribi_t::northsouth ? 1 : 0);
			const bool after = kp > kq  ||  (kp == kq  &&  (across( p->get_anchor(), p->axis ) > across( q->get_anchor(), q->axis )  ||  (across( p->get_anchor(), p->axis ) == across( q->get_anchor(), q->axis )  &&  along( p->get_anchor(), p->axis ) > along( q->get_anchor(), q->axis ))));
			if(  !after  ) {
				break;
			}
			unnumbered[b] = unnumbered[b-1];
			b--;
		}
		unnumbered[b] = v;
	}
	FOR( vector_tpl<uint32>, const i, unnumbered ) {
		uint32 n = 1;
		bool taken = true;
		while(  taken  ) {
			taken = false;
			FOR( vector_tpl<platform_t *>, const p, list ) {
				if(  p->number[0]  &&  strtoul( p->number, NULL, 10 ) == n  &&  p->number[strspn( p->number, "0123456789" )] == 0  ) {
					taken = true;
					n++;
					break;
				}
			}
		}
		sprintf( list[i]->number, "%u", n );
		platform_label_t l;
		l.anchor = list[i]->get_anchor();
		l.dir = 0;
		tstrncpy( l.text, list[i]->number, lengthof(l.text) );
		labels.append( l );
		owner_of[i] = labels.get_count() - 1;
		used.append( true );
	}
	for(  uint32 k = 0;  k < labels.get_count();  k++  ) {
		if(  used[k]  ) {
			kept.append( labels[k] );
		}
	}
	copy_vector( labels, kept );
}


// one line under two numbers (board texts: Praha 195 that runs on as 158) is two members, else the first entry hid the other
static bool same_member(const platform_member_t &a, const platform_member_t &b)
{
	return a.line == b.line  &&  strcmp( a.number, b.number ) == 0  &&  strcmp( a.destination, b.destination ) == 0;
}

static void add_member(platform_t *p, halthandle_t next, const char *next_name, ribi_t::ribi out, const platform_member_t &m)
{
	FOR( vector_tpl<platform_group_t *>, g, p->groups ) {
		if(  g->next == next  ) {
			FOR( vector_tpl<platform_member_t>, const &o, g->members ) {
				if(  same_member( o, m )  ) {
					return;
				}
			}
			g->members.append( m );
			if(  g->out == 0  ) {
				g->out = out;
			}
			return;
		}
	}
	platform_group_t *g = new platform_group_t();
	g->next = next;
	tstrncpy( g->next_name, next_name, lengthof(g->next_name) );
	// until add_lines finds a better one
	g->heading = next;
	tstrncpy( g->heading_name, next_name, lengthof(g->heading_name) );
	g->out = out;
	g->members.append( m );
	p->groups.append( g );
}


static void build_rows(haltestelle_t *halt, vector_tpl<platform_t *> &list);


// ---------------------------------------------------------------------------------------------
// headings: the stop a group's card names

#define HEADING_LOOK_STANDS (6)   // stops looked at ahead of a stand
#define HEADING_LOOK_TRACKS (4)   // of a track: trains stop less often, and a metro shares the halts of main stations

static bool is_rail_wt(waytype_t wt)
{
	return wt == track_wt  ||  wt == monorail_wt  ||  wt == maglev_wt  ||  wt == narrowgauge_wt;
}


/**
 * How important a stop is for a heading: the passenger lines with vehicles that serve it, each line number once
 * (variants of one line, Ex1/1 and Ex1/2, count once). For a stand a train or metro line counts as much as a bus or
 * tram line (the transfer people go there for); for a track the other kinds count a quarter, so a big bus station does
 * not outweigh a railway junction.
 */
static double stop_weight(halthandle_t h, bool train, vector_tpl<halthandle_t> &cache_halt, vector_tpl<double> &cache_weight)
{
	for(  uint32 i = 0;  i < cache_halt.get_count();  i++  ) {
		if(  cache_halt[i] == h  ) {
			return cache_weight[i];
		}
	}
	vector_tpl<const char *> same, other;
	vector_tpl<char *> owned;
	FOR( vector_tpl<linehandle_t>, const line, h->registered_lines ) {
		if(  !line.is_bound()  ||  line->count_convoys() == 0  ||  !line->get_goods_catg_index().is_contained( goods_manager_t::INDEX_PAS )  ) {
			continue;
		}
		cbuffer_t number;
		board_names_t::get_line_number( number, line->get_name() );
		char *key = strdup( number.len() ? (const char *)number : line->get_name() );
		owned.append( key );
		vector_tpl<const char *> &list = is_rail_wt( line->get_schedule()->get_waytype() ) == train ? same : other;
		bool seen = false;
		FOR( vector_tpl<const char *>, const k, list ) {
			seen |= strcmp( k, key ) == 0;
		}
		if(  !seen  ) {
			list.append( key );
		}
	}
	double w = same.get_count();
	FOR( vector_tpl<const char *>, const k, other ) {
		bool also_same = false;
		FOR( vector_tpl<const char *>, const s, same ) {
			also_same |= strcmp( s, k ) == 0;
		}
		if(  !also_same  ) {
			w += train ? 0.25 : 1.0;
		}
	}
	FOR( vector_tpl<char *>, k, owned ) {
		free( k );
	}
	cache_halt.append( h );
	cache_weight.append( w );
	return w;
}


// the stops after entry of the schedule, as far as the trip goes (a Terminal or All off entry ends it, or coming back
// here), at most look of them; waypoints, depots, Hold entries and further tiles of the same stop are left out
static void stops_ahead(const schedule_t *schedule, uint8 entry, const player_t *owner, halthandle_t self, uint32 look, vector_tpl<halthandle_t> &out)
{
	out.clear();
	const uint8 n = schedule->get_count();
	halthandle_t last = self;
	for(  uint8 k = 1;  k < n  &&  out.get_count() < look;  k++  ) {
		const schedule_entry_t &e = schedule->entries[(entry + k) % n];
		const halthandle_t h = haltestelle_t::get_halt( e.pos, owner );
		if(  !h.is_bound()  ||  e.stop_type == schedule_entry_t::hold  ||  h == last  ) {
			continue;
		}
		if(  h == self  ) {
			break;
		}
		out.append( h );
		last = h;
		if(  e.unloads_all()  ) {
			break;
		}
	}
}


/**
 * The heading of a group: the nearest stop ahead that every line of the group reaches within the lookahead and on its
 * trip, and that is clearly more important than the next stop (at least twice its weight and two lines more), e.g.
 * Praha, Poliklinika Černý Most towards Ronešova -> Rajská zahrada (metro B and trains). None: the next stop.
 */
static halthandle_t find_heading(const platform_t *p, const platform_group_t *g, halthandle_t self, vector_tpl<halthandle_t> &cache_halt, vector_tpl<double> &cache_weight)
{
	if(  !g->next.is_bound()  ||  g->members.empty()  ) {
		return g->next;
	}
	const bool train = p->kind == platform_t::TRACK;
	const uint32 look = train ? HEADING_LOOK_TRACKS : HEADING_LOOK_STANDS;
	// the stops ahead of every line, one after the other: line k has ahead[start[k] .. start[k+1])
	vector_tpl<halthandle_t> ahead, part;
	vector_tpl<uint32> start;
	FOR( vector_tpl<platform_member_t>, const &m, g->members ) {
		if(  !m.line.is_bound()  ) {
			return g->next;
		}
		start.append( ahead.get_count() );
		stops_ahead( m.line->get_schedule(), m.entry, m.line->get_owner(), self, look, part );
		FOR( vector_tpl<halthandle_t>, const h, part ) {
			ahead.append( h );
		}
	}
	start.append( ahead.get_count() );
	const double w_next = stop_weight( g->next, train, cache_halt, cache_weight );
	// the stops every line reaches, nearest first (by the furthest any of the lines has to go)
	halthandle_t best;
	sint32 best_dist = 0x7FFFFFFF;
	for(  uint32 c = start[0];  c < start[1];  c++  ) {
		const halthandle_t h = ahead[c];
		if(  h == g->next  ) {
			continue;
		}
		sint32 dist = 0;
		bool everyone = true;
		for(  uint32 k = 0;  k + 1 < start.get_count()  &&  everyone;  k++  ) {
			sint32 found = -1;
			for(  uint32 i = start[k];  i < start[k+1]  &&  found < 0;  i++  ) {
				if(  ahead[i] == h  ) {
					found = i - start[k];
				}
			}
			everyone = found >= 0;
			dist = max( dist, found );
		}
		if(  !everyone  ||  dist >= best_dist  ) {
			continue;
		}
		const double w = stop_weight( h, train, cache_halt, cache_weight );
		if(  w >= 2.0 * w_next  &&  w >= w_next + 2.0  ) {
			best = h;
			best_dist = dist;
		}
	}
	return best.is_bound() ? best : g->next;
}


static vector_tpl<halthandle_t> labels_dirty;

void platforms_t::labels_changed(halthandle_t halt)
{
	labels_dirty.append_unique( halt );
}


void platforms_t::clear_pending()
{
	labels_dirty.clear();
}


void platforms_t::step()
{
	while(  !labels_dirty.empty()  ) {
		const halthandle_t halt = labels_dirty.pop_back();
		if(  halt.is_bound()  ) {
			update_labels( halt.get_rep() );
		}
	}
	// tracks also change without a stop tile coming or going (rails laid or removed between two rows of a stop):
	// one stop per step in turn, the same on every client
	// (by the world's step count, which a client joining a network game gets with the save)
	const vector_tpl<halthandle_t> &all = haltestelle_t::get_alle_haltestellen();
	if(  !all.empty()  ) {
		update_labels( all[(uint32)world()->get_steps() % all.get_count()].get_rep() );
	}
}


static bool same_labels(const vector_tpl<platform_label_t> &a, const vector_tpl<platform_label_t> &b)
{
	if(  a.get_count() != b.get_count()  ) {
		return false;
	}
	for(  uint32 i = 0;  i < a.get_count();  i++  ) {
		if(  a[i].anchor != b[i].anchor  ||  a[i].dir != b[i].dir  ||  strcmp( a[i].text, b[i].text )  ) {
			return false;
		}
	}
	return true;
}


void platforms_t::update_labels(haltestelle_t *halt)
{
	const vector_tpl<platform_label_t> &labels = halt->access_platform_labels();
	vector_tpl<platform_label_t> before;
	FOR( vector_tpl<platform_label_t>, const &l, labels ) {
		before.append( l );
	}
	halt_platforms_t rows;
	build_rows( halt, rows.list );
	apply_labels( halt, rows.list, true );
	if(  !same_labels( before, labels )  ) {
		// only then: the sweep must not make every stop work out its platforms again
		halt->platforms_changed();
	}
}


void platforms_t::build(haltestelle_t *halt, halt_platforms_t &out)
{
	out.clear();
	vector_tpl<platform_t *> &list = out.list;
	build_rows( halt, list );
	apply_labels( halt, list, false );
	add_lines( halt, list );
}


// the platforms of the stop's tiles, without numbers and lines
static void build_rows(haltestelle_t *halt, vector_tpl<platform_t *> &list)
{
	const halthandle_t self = haltestelle_t::get_halt( halt->get_basis_pos3d(), halt->get_owner() );

	// the tiles where passenger or mail lines stop (a stand on a building for goods counts then)
	vector_tpl<koord3d> passenger_stops;
	FOR( vector_tpl<linehandle_t>, const line, halt->registered_lines ) {
		if(  !line.is_bound()  ||  !(line->get_goods_catg_index().is_contained( goods_manager_t::INDEX_PAS )  ||  line->get_goods_catg_index().is_contained( goods_manager_t::INDEX_MAIL ))  ) {
			continue;
		}
		FOR( minivec_tpl<schedule_entry_t>, const &e, line->get_schedule()->entries ) {
			const grund_t *gr = world()->lookup( e.pos );
			if(  gr  &&  gr->get_halt() == self  ) {
				passenger_stops.append_unique( e.pos );
			}
		}
	}

	// the stop's tiles with a way of a stop
	vector_tpl<cell_t> cells;
	FOR( slist_tpl<haltestelle_t::tile_t>, const &t, halt->get_tiles() ) {
		const waytype_t wt = stop_waytype( t.grund );
		const uint8 kind = kind_of( wt );
		if(  kind == 255  ||  (kind == platform_t::STAND  &&  is_no_stand( t.grund, passenger_stops ))  ) {
			continue;
		}
		cell_t c;
		c.gr = t.grund;
		c.kind = kind;
		c.wt = wt;
		c.row = -1;
		c.ribi = 0;
		c.axis = 0;
		c.way_wt = wt;
		if(  kind != platform_t::OTHER  ) {
			const weg_t *w = stop_way( t.grund, wt );
			if(  w == NULL  ) {
				continue;
			}
			c.way_wt = w->get_waytype();
			c.ribi = w->get_ribi_unmasked();
			if(  ribi_t::is_straight( c.ribi )  ||  ribi_t::is_single( c.ribi )  ) {
				c.axis = ribi_t::doubles( c.ribi );
			}
		}
		cells.append( c );
	}

	// rows: tiles joined by their way along the same axis
	sint32 rows = 0;
	for(  uint32 i = 0;  i < cells.get_count();  i++  ) {
		if(  cells[i].row >= 0  ) {
			continue;
		}
		cells[i].row = rows;
		vector_tpl<uint32> todo;
		todo.append( i );
		while(  !todo.empty()  ) {
			const cell_t c = cells[todo.pop_back()];
			if(  c.kind == platform_t::OTHER  ||  c.axis == 0  ) {
				continue;
			}
			for(  int d = 0;  d < 4;  d++  ) {
				const ribi_t::ribi r = ribi_t::nsew[d];
				if(  (c.ribi & r) == 0  ||  (c.axis & r) == 0  ) {
					continue;
				}
				grund_t *to = NULL;
				if(  !c.gr->get_neighbour( to, c.way_wt, r )  ||  to->get_halt() != self  ) {
					continue;
				}
				const sint32 k = find_cell( cells, to->get_pos() );
				if(  k >= 0  &&  cells[k].row < 0  &&  cells[k].kind == c.kind  &&  cells[k].axis == c.axis  &&  cells[k].way_wt == c.way_wt  ) {
					cells[k].row = rows;
					todo.append( k );
				}
			}
		}
		rows++;
	}

	// platforms of the rows
	for(  sint32 r = 0;  r < rows;  r++  ) {
		vector_tpl<koord3d> tiles;
		const cell_t *first = NULL;
		FOR( vector_tpl<cell_t>, const &c, cells ) {
			if(  c.row == r  ) {
				tiles.append( c.gr->get_pos() );
				if(  first == NULL  ) {
					first = &c;
				}
			}
		}
		if(  first == NULL  ) {
			continue;
		}
		const ribi_t::ribi axis = first->axis;
		sort_tiles( tiles, axis );
		if(  first->kind != platform_t::STAND  ||  axis == 0  ) {
			platform_t *p = new platform_t();
			p->kind = first->kind;
			p->wt = first->wt;
			copy_vector( p->tiles, tiles );
			p->axis = axis;
			list.append( p );
			continue;
		}
		// stands: each tile of the row is a stand of its own (a bus stops at one tile; at a bus station the owner
		// numbers the tiles of a row as separate stands), one per travel direction, a dead end one
		FOR( vector_tpl<koord3d>, const &t, tiles ) {
			const sint32 c = find_cell( cells, t );
			const ribi_t::ribi r = cells[c].ribi;
			const bool lo_open = (r & low_dir( axis )) != 0;
			const bool hi_open = (r & high_dir( axis )) != 0;
			if(  lo_open  &&  hi_open  ) {
				for(  int k = 0;  k < 2;  k++  ) {
					platform_t *p = new platform_t();
					p->kind = platform_t::STAND;
					p->wt = first->wt;
					p->tiles.append( t );
					p->axis = axis;
					p->dir = k == 0 ? high_dir( axis ) : low_dir( axis );
					list.append( p );
				}
			}
			else {
				platform_t *p = new platform_t();
				p->kind = platform_t::STAND;
				p->wt = first->wt;
				p->tiles.append( t );
				p->axis = axis;
				p->dead_end = lo_open != hi_open;
				p->dir = !p->dead_end ? 0 : (hi_open ? low_dir( axis ) : high_dir( axis ));
				list.append( p );
			}
		}
	}

}


// the lines that board at each platform, grouped by next stop (plan 3)
void platforms_t::add_lines(haltestelle_t *halt, vector_tpl<platform_t *> &list)
{
	const halthandle_t self = haltestelle_t::get_halt( halt->get_basis_pos3d(), halt->get_owner() );

	// the lines
	cbuffer_t town;
	board_names_t::get_stop_town( town, halt->get_name(), false );
	cbuffer_t train_town;
	board_names_t::get_stop_town( train_town, halt->get_name(), true );
	struct departs_t { linehandle_t line; sint32 platform; halthandle_t next; };
	vector_tpl<departs_t> departs;
	struct arrives_t { linehandle_t line; uint8 entry; sint32 platform; };
	vector_tpl<arrives_t> arrives;

	FOR( vector_tpl<linehandle_t>, const line, halt->registered_lines ) {
		if(  !line.is_bound()  ||  line->count_convoys() == 0  ||  !line->get_goods_catg_index().is_contained( goods_manager_t::INDEX_PAS )  ) {
			continue;
		}
		const schedule_t *schedule = line->get_schedule();
		const player_t *owner = line->get_owner();
		const uint8 n = schedule->get_count();
		const waytype_t wt = schedule->get_waytype();
		const bool train = wt == track_wt  ||  wt == monorail_wt  ||  wt == maglev_wt  ||  wt == narrowgauge_wt;
		for(  uint8 i = 0;  i < n;  i++  ) {
			const schedule_entry_t &e = schedule->entries[i];
			if(  e.stop_type == schedule_entry_t::hold  ||  haltestelle_t::get_halt( e.pos, owner ) != self  ) {
				continue;
			}
			const sint32 pi = find_schedule_platform( list, schedule, i, line );
			if(  pi < 0  ) {
				continue;
			}
			// a departure: it loads here and goes on to another stop where passengers may get off. Further stands or
			// tracks of this stop on the way are skipped: a bus that boards at two bays of one stop (both loading)
			// shows in full at both, with the next stop after them. An All off arrival stand does not load, so it
			// stays an arrival.
			uint8 next = n;
			if(  e.loads()  ) {
				uint8 at = i;
				for(  uint8 k = 1;  k < n;  k++  ) {
					const uint8 j = next_halt_entry( schedule, at, owner, true );
					if(  j >= n  ||  j == i  ) {
						break;
					}
					if(  haltestelle_t::get_halt( schedule->entries[j].pos, owner ) != self  ) {
						next = j;
						break;
					}
					at = j;
				}
			}
			const bool departure = next < n;
			if(  departure  ) {
				cbuffer_t number, category, destination;
				halthandle_t dest;
				if(  !gui_departure_board_view_t::get_departure_texts( schedule, i, owner, line, self, number, category, destination, dest )  ) {
					continue;
				}
				platform_member_t m;
				m.line = line;
				m.entry = i;
				m.wt = wt;
				m.numbered = number.len() > 0;
				if(  number.len() > 0  ) {
					tstrncpy( m.number, number, lengthof(m.number) );
				}
				else if(  category.len() > 0  ) {
					tstrncpy( m.number, category, lengthof(m.number) );
				}
				else {
					first_word( m.number, lengthof(m.number), line->get_name() );
				}
				tstrncpy( m.destination, destination, lengthof(m.destination) );
				m.dest_halt = dest;
				const halthandle_t next_halt = haltestelle_t::get_halt( schedule->entries[next].pos, owner );
				cbuffer_t next_name;
				board_names_t::get_stop_name( next_name, next_halt->get_name(), train, train ? train_town : town );
				add_member( list[pi], next_halt, next_name, departure_dir( list[pi], schedule, i, next, line ), m );
				departs_t d;
				d.line = line;
				d.platform = pi;
				d.next = next_halt;
				departs.append( d );
			}
			else if(  e.unloads()  &&  e.plans_arrival()  ) {
				arrives_t a;
				a.line = line;
				a.entry = i;
				a.platform = pi;
				arrives.append( a );
			}
		}
	}

	// arrivals: lines that only let passengers off at a platform
	FOR( vector_tpl<arrives_t>, const &a, arrives ) {
		platform_t *p = list[a.platform];
		bool departs_here = false;
		FOR( vector_tpl<departs_t>, const &d, departs ) {
			departs_here |= d.line == a.line  &&  d.platform == a.platform;
		}
		if(  departs_here  ) {
			continue;
		}
		const schedule_t *schedule = a.line->get_schedule();
		cbuffer_t number, category, destination;
		board_names_t::get_line_number( number, a.line->get_name() );
		if(  const char *t = schedule->get_board_text( a.entry, schedule_entry_t::BOARD_LINE )  ) {
			number.clear();
			number.append( t );
		}
		board_names_t::get_line_category( category, a.line->get_name() );
		platform_member_t m;
		m.line = a.line;
		m.entry = a.entry;
		m.wt = schedule->get_waytype();
		m.numbered = number.len() > 0;
		if(  number.len() > 0  ) {
			tstrncpy( m.number, number, lengthof(m.number) );
		}
		else if(  category.len() > 0  ) {
			tstrncpy( m.number, category, lengthof(m.number) );
		}
		else {
			first_word( m.number, lengthof(m.number), a.line->get_name() );
		}
		bool listed = false;
		FOR( vector_tpl<platform_member_t>, const &o, p->arrivals ) {
			listed |= same_member( o, m );
		}
		if(  !listed  ) {
			p->arrivals.append( m );
		}
	}

	// order, and when a group names its next stop (plan 3, rules 4 and 8)
	for(  uint32 pi = 0;  pi < list.get_count();  pi++  ) {
		platform_t *p = list[pi];
		FOR( vector_tpl<platform_group_t *>, g, p->groups ) {
			sort_members( g->members );
		}
		sort_members( p->arrivals );
		for(  uint32 a = 1;  a < p->groups.get_count();  a++  ) {
			platform_group_t *g = p->groups[a];
			uint32 b = a;
			while(  b > 0  &&  compare_numbers( p->groups[b-1]->members[0].number, g->members[0].number ) > 0  ) {
				p->groups[b] = p->groups[b-1];
				b--;
			}
			p->groups[b] = g;
		}
		FOR( vector_tpl<platform_group_t *>, g, p->groups ) {
			g->name_shown = p->groups.get_count() > 1;
			for(  uint32 k = 0;  k < g->members.get_count()  &&  !g->name_shown;  k++  ) {
				FOR( vector_tpl<departs_t>, const &d, departs ) {
					if(  d.line == g->members[k].line  &&  (uint32)d.platform != pi  &&  d.next != g->next  ) {
						g->name_shown = true;
						break;
					}
				}
			}
		}
	}

	// headings: what the cards call each group (the nearest clearly more important stop ahead, else the next stop);
	// one that another group of the platform also leads to, or goes to next, would not tell them apart: the next stop
	vector_tpl<halthandle_t> cache_halt;
	vector_tpl<double> cache_weight;
	FOR( vector_tpl<platform_t *>, p, list ) {
		vector_tpl<halthandle_t> found;
		FOR( vector_tpl<platform_group_t *>, const g, p->groups ) {
			found.append( find_heading( p, g, self, cache_halt, cache_weight ) );
		}
		for(  uint32 gi = 0;  gi < p->groups.get_count();  gi++  ) {
			platform_group_t *g = p->groups[gi];
			halthandle_t h = found[gi];
			for(  uint32 o = 0;  o < p->groups.get_count()  &&  h != g->next;  o++  ) {
				if(  o != gi  &&  (found[o] == h  ||  p->groups[o]->next == h)  ) {
					h = g->next;
				}
			}
			if(  h.is_bound()  &&  h != g->next  ) {
				const bool train = p->kind == platform_t::TRACK;
				cbuffer_t name;
				board_names_t::get_stop_name( name, h->get_name(), train, train ? train_town : town );
				g->heading = h;
				tstrncpy( g->heading_name, name, lengthof(g->heading_name) );
			}
		}
	}
}


// ---------------------------------------------------------------------------------------------
// convoys

void platforms_t::get_platforms_of(halthandle_t halt, const schedule_t *schedule, uint8 entry, linehandle_t line, const convoi_t *cnv, sint32 &planned, sint32 &now)
{
	planned = -1;
	now = -1;
	if(  !halt.is_bound()  ||  schedule == NULL  ||  entry >= schedule->get_count()  ) {
		return;
	}
	const vector_tpl<platform_t *> &list = halt->get_platforms();
	planned = find_schedule_platform( list, schedule, entry, line );
	if(  cnv == NULL  ||  cnv->get_vehicle_count() == 0  ) {
		return;
	}
	if(  cnv->is_coupled()  &&  cnv->get_coupled_convoi().is_bound()  ) {
		// a joined train rides with its primary
		cnv = cnv->get_coupled_convoi().get_rep();
	}
	const vehicle_t *front = cnv->front();
	const schedule_t *own = cnv->get_schedule();
	if(  haltestelle_t::get_halt( front->get_pos(), cnv->get_owner() ) == halt  &&  (cnv->get_state() == convoi_t::LOADING  ||  cnv->get_state() == convoi_t::ROUTING_1  ||  cnv->get_state() == convoi_t::CAN_START  ||  cnv->get_state() == convoi_t::CAN_START_ONE_MONTH  ||  cnv->get_state() == convoi_t::CAN_START_TWO_MONTHS  ||  cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE  ||  cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH  ||  cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS)  ) {
		// standing at the stop
		now = find( list, front->get_pos(), front->get_direction() );
		return;
	}
	if(  own == NULL  ||  own->get_count() == 0  ) {
		return;
	}
	if(  haltestelle_t::get_halt( own->get_current_entry().pos, cnv->get_owner() ) != halt  ) {
		// not on its way here yet
		return;
	}
	const route_t *route = cnv->get_route();
	const waytype_t wt = own->get_waytype();
	const bool rail = wt == track_wt  ||  wt == monorail_wt  ||  wt == maglev_wt  ||  wt == narrowgauge_wt;
	if(  route  &&  route->get_count() > 1  &&  haltestelle_t::get_halt( route->back(), cnv->get_owner() ) == halt  ) {
		const koord3d end = route->back();
		const ribi_t::ribi in = ribi_type( route->at( route->get_count() - 2 ), end );
		if(  rail  ) {
			// the way in is reserved (a choose signal chose, or the block reaches it)
			const grund_t *gr = world()->lookup( end );
			const weg_t *w = gr ? gr->get_weg( wt == tram_wt ? track_wt : wt ) : NULL;
			const schiene_t *sch = w  &&  (w->get_waytype() == track_wt  ||  w->get_waytype() == monorail_wt  ||  w->get_waytype() == maglev_wt  ||  w->get_waytype() == narrowgauge_wt) ? (const schiene_t *)w : NULL;
			if(  sch  &&  sch->get_reserved_convoi() == cnv->self  ) {
				now = find( list, end, in );
			}
		}
		else if(  end != own->get_current_entry().pos  ) {
			// a choose sign sent it to another place of the stop
			now = find( list, end, in );
		}
	}
	if(  now < 0  &&  rail  &&  cnv->has_claim()  ) {
		// a track claimed at a single-track station
		FOR( vector_tpl<koord3d>, const &k, cnv->get_claim_path() ) {
			if(  haltestelle_t::get_halt( k, cnv->get_owner() ) == halt  ) {
				now = find( list, k, 0 );
				if(  now >= 0  ) {
					break;
				}
			}
		}
	}
}


// ---------------------------------------------------------------------------------------------
// numbering

// where a platform sits on the screen at the neutral zoom (128 px tiles): stands at their kerb (right of the
// travel direction, as the cards place them), tracks at their first tile
static void screen_key(const platform_t *p, sint32 &sx, sint32 &sy)
{
	const koord3d a = p->get_anchor();
	sx = (a.x - a.y) * 64;
	sy = (a.x + a.y) * 32 - a.z * 16;
	if(  p->kind == platform_t::STAND  ) {
		switch(  p->dir  ) {
			case ribi_t::west:  sx += 10; sy -= 5; break;
			case ribi_t::east:  sx -= 10; sy += 5; break;
			case ribi_t::north: sx += 10; sy += 5; break;
			case ribi_t::south: sx -= 10; sy -= 5; break;
			default: break;
		}
	}
}


void platforms_t::get_walk_order(const vector_tpl<platform_t *> &list, bool with_empty_stands, vector_tpl<uint32> &order)
{
	order.clear();
	for(  uint32 i = 0;  i < list.get_count();  i++  ) {
		const platform_t *p = list[i];
		if(  !has_card( p )  ||  (p->kind == platform_t::STAND  &&  !with_empty_stands  &&  !p->has_lines())  ) {
			continue;
		}
		order.append( i );
	}
	// insertion sort: a stop has few platforms
	for(  uint32 a = 1;  a < order.get_count();  a++  ) {
		const uint32 v = order[a];
		uint32 b = a;
		while(  b > 0  ) {
			const platform_t *p = list[order[b-1]], *q = list[v];
			bool after;
			if(  p->kind != q->kind  ) {
				after = p->kind == platform_t::STAND;
			}
			else if(  p->kind == platform_t::TRACK  &&  (p->number[0] != 0) != (q->number[0] != 0)  ) {
				after = p->number[0] == 0;
			}
			else if(  p->kind == platform_t::TRACK  &&  p->number[0]  &&  compare_numbers( p->number, q->number ) != 0  ) {
				after = compare_numbers( p->number, q->number ) > 0;
			}
			else {
				sint32 px, py, qx, qy;
				screen_key( p, px, py );
				screen_key( q, qx, qy );
				after = py > qy  ||  (py == qy  &&  px > qx);
			}
			if(  !after  ) {
				break;
			}
			order[b] = order[b-1];
			b--;
		}
		order[b] = v;
	}
}


void platforms_t::get_group_name(cbuffer_t &buf, const platform_group_t *g, bool with_next)
{
	if(  with_next  &&  g->has_heading()  ) {
		buf.printf( translator::translate( "%s via %s" ), g->heading_name, g->next_name );
	}
	else {
		buf.append( g->heading_name );
	}
}


void platforms_t::get_stand_label(char *buf, size_t len, uint32 n, bool letters)
{
	if(  !letters  ) {
		snprintf( buf, len, "%u", n + 1 );
		return;
	}
	// A..Z, then AA..ZZ
	if(  n < 26  ) {
		snprintf( buf, len, "%c", 'A' + n );
	}
	else {
		n -= 26;
		snprintf( buf, len, "%c%c", 'A' + (n / 26) % 26, 'A' + n % 26 );
	}
}


const char *platforms_t::get_direction_name(ribi_t::ribi dir)
{
	switch(  dir  ) {
		case ribi_t::north: return translator::translate( "northbound" );
		case ribi_t::south: return translator::translate( "southbound" );
		case ribi_t::east:  return translator::translate( "eastbound" );
		case ribi_t::west:  return translator::translate( "westbound" );
		default:            return "";
	}
}
