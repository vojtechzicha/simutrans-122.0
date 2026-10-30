/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Fork: headless test harness, the batch mode -forktest SCRIPT (see tests/README.md).
 * Reads a command script, builds layouts with the game's own tools (or works on a game given with
 * -load), runs the simulation headless, logs what the trains do to SCRIPT.log and checks the
 * expect commands; the log ends with RESULT PASS or RESULT FAIL. tests/run.sh runs the scenarios.
 * It is compiled into every build, so it keeps compiling with the code it tests.
 */

#include "fork_test_harness.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <map>
#include "../utils/cbuffer_t.h"
#include "../simdebug.h"
#include <stdarg.h>

#include "../simworld.h"
#include "../simmenu.h"
#include "../simconvoi.h"
#include "../simline.h"
#include "../simlinemgmt.h"
#include "../simhalt.h"
#include "../simdepot.h"
#include "../player/simplay.h"
#include "../bauer/wegbauer.h"
#include "../bauer/hausbauer.h"
#include "../bauer/vehikelbauer.h"
#include "../boden/grund.h"
#include "../boden/wege/schiene.h"
#include "../obj/roadsign.h"
#include "../obj/signal.h"
#include "../obj/wayobj.h"
#include "../descriptor/way_obj_desc.h"
#include "../descriptor/roadsign_desc.h"
#include "../descriptor/building_desc.h"
#include "../descriptor/vehicle_desc.h"
#include "../descriptor/way_desc.h"
#include "../dataobj/schedule.h"
#include "../dataobj/environment.h"
#include "../dataobj/settings.h"
#include "../dataobj/translator.h"
#include "../vehicle/simvehicle.h"
#include "../utils/simrandom.h"
#include "../display/simview.h"
#include "../display/viewport.h"
#include "../display/simgraph.h"
#include "../sys/simsys.h"
#include "../vehicle/simroadtraffic.h"
#include "../boden/wege/strasse.h"

#define IS_H(d) (d)->is_block_post()
#define IS_P(d) (d)->is_platform_signal()
#define IS_LT(d) (d)->is_station_boundary()
#define HAS_CLAIM(c) (c)->has_claim()
#define CLAIM_B(c) (c)->get_claim_boundary()
#define SWAIT(c) ((int)(c)->get_section_wait())
static bool sign_applies(const roadsign_t *rs, ribi_t::ribi d)
{
	return rs->applies_to(d);
}

static const char *state_name(int s)
{
	static const char *n[] = { "INITIAL","EDIT_SCHEDULE","ROUTING_1","?","?","NO_ROUTE","DRIVING","LOADING",
		"WAIT_CLEAR","WAIT_CLEAR_1M","CAN_START","CAN_START_1M","SELF_DESTRUCT","WAIT_CLEAR_2M","CAN_START_2M",
		"LEAVING_DEPOT","ENTERING_DEPOT","COUPLED","UNCOUPLING" };
	return s>=0  &&  s<(int)(sizeof(n)/sizeof(n[0])) ? n[s] : "??";
}

static karte_t *W;
static player_t *PL;
static FILE *LOG;
static std::vector<std::string> keep; // keeps default_param strings alive
static const way_desc_t *track_desc = NULL;
static std::vector<const vehicle_desc_t *> vehicles;
static schedule_t *cur_sched = NULL;
static std::string cur_line_name;
static bool cur_line_tram = false;
static bool cur_line_bus = false;
static std::map<std::string, linehandle_t> lines;
static int failures = 0;

static void logf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(LOG, fmt, ap);
	va_end(ap);
	fflush(LOG);
}

static const char *keepstr(const char *s)
{
	keep.push_back(s);
	return strdup(s);
}

static grund_t *ground(int x, int y)
{
	grund_t *gr = W->lookup_kartenboden(koord(x, y));
	if(  !gr  ) {
		logf("ERROR no ground at %d,%d\n", x, y);
		failures++;
	}
	return gr;
}

static ribi_t::ribi dir_of(const char *d)
{
	switch(  d[0]  ) {
		case 'N': return ribi_t::north;
		case 'S': return ribi_t::south;
		case 'E': return ribi_t::east;
		case 'W': return ribi_t::west;
	}
	return ribi_t::none;
}

static const char *use_tool(int id, const char *param, koord3d a, koord3d b = koord3d::invalid)
{
	tool_t *t = tool_t::general_tool[id];
	t->set_default_param( keepstr(param) );
	t->flags = 0;
	t->init( PL );
	const char *err = t->work( PL, a );
	if(  b!=koord3d::invalid  &&  (err==NULL  ||  !*err)  ) {
		err = t->work( PL, b );
	}
	t->exit( PL );
	return err;
}


// --- first-leaving test helpers ---
#include "../simware.h"
#include "../bauer/goods_manager.h"
static void cmd_pax(int x, int y, int dx, int dy, int n)
{
	halthandle_t h = haltestelle_t::get_halt( ground(x,y)->get_pos(), PL );
	halthandle_t d = haltestelle_t::get_halt( ground(dx,dy)->get_pos(), PL );
	if(  !h.is_bound()  ||  !d.is_bound()  ) { logf("ERROR pax: no halt\n"); failures++; return; }
	ware_t w( goods_manager_t::passengers );
	w.menge = n;
	w.set_ziel( d );
	w.set_zwischenziel( d );
	w.set_zielpos( d->get_basis_pos() );
	h->starte_mit_route( w );
	logf("  pax %d at %s for %s\n", n, h->get_name(), d->get_name());
}
static void cmd_loads(const char *tag)
{
	logf("LOADS %s at minute %lld\n", tag ? tag : "", (long long)W->get_calendar_minutes());
	FOR(vector_tpl<convoihandle_t>, cnv, W->convoys()) {
		std::map<std::string, int> by, boarded;
		int tot = 0, brd = 0;
		for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) {
			FOR(slist_tpl<ware_t>, const &w, cnv->get_vehikel(i)->get_cargo()) {
				std::string k = w.get_ziel().is_bound() ? w.get_ziel()->get_name() : "?";
				by[k] += w.menge; tot += w.menge;
				if(  w.boarded_here  ) { boarded[k] += w.menge; brd += w.menge; }
			}
		}
		halthandle_t at = haltestelle_t::get_halt( cnv->front()->get_pos(), PL );
		logf("  %s %s at %s: %d aboard (%d boarded here)", cnv->get_name(), state_name(cnv->get_state()), at.is_bound() ? at->get_name() : "-", tot, brd);
		for(  std::map<std::string,int>::iterator it=by.begin();  it!=by.end();  ++it  ) logf(" | %s %d (b %d)", it->first.c_str(), it->second, boarded[it->first]);
		logf("\n");
	}
	FOR(vector_tpl<halthandle_t>, h, haltestelle_t::get_alle_haltestellen()) {
		logf("  halt %s waiting pax %u\n", h->get_name(), (unsigned)h->get_ware_summe( goods_manager_t::passengers ));
	}
}
// --- commands --------------------------------------------------------------------------------

static void cmd_newworld(int w, int h)
{
	settings_t sets = env_t::default_settings;
	sets.set_size( w, h );
	sets.set_city_count( 0 );
	sets.set_factory_count( 0 );
	sets.set_tourist_attractions( 0 );
	sets.set_traffic_level( 0 );
	sets.set_tree( 0 );
	sets.set_use_timeline( 0 );
	sets.set_default_climates();
	sets.heightfield = "flat";
	sint8 *hf = new sint8[w*h];
	memset( hf, sets.get_groundwater()+4, w*h );
	W->init( &sets, hf );
	delete [] hf;
	PL = W->get_player(0);
	W->switch_active_player( 0, true );
	logf("newworld %dx%d groundwater %d minutes_per_month %d bits_per_month %d\n", w, h, sets.get_groundwater(),
		(int)W->get_settings().get_minutes_per_month(), (int)W->get_settings().get_bits_per_month());
}

static void cmd_list(const char *what)
{
	if(  !strcmp(what, "signs")  ) {
		FOR(vector_tpl<const roadsign_desc_t*>, d, roadsign_t::get_available_signs(track_wt)) {
			logf("sign %-40s flags %04x count %d signal %d choose %d eoc %d P %d LT %d\n", d->get_name(), d->get_flags(), d->get_count(),
				d->is_signal_type(), d->is_choose_sign(), d->is_end_choose_signal(), IS_P(d), IS_LT(d));
		}
	}
	else if(  !strcmp(what, "stations")  ) {
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::generic_stop)) {
			if(  d->get_extra()==track_wt  ) {
				logf("station %-40s enables %d size %dx%d level %d\n", d->get_name(), d->get_enabled(), d->get_x(), d->get_y(), d->get_level());
			}
		}
	}
	else if(  !strcmp(what, "depots")  ) {
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::depot)) {
			if(  d->get_extra()==track_wt  ) {
				logf("depot %s\n", d->get_name());
			}
		}
	}
	else if(  !strcmp(what, "vehicles")  ) {
		FOR(slist_tpl<vehicle_desc_t const*>, d, vehicle_builder_t::get_info(track_wt)) {
			logf("vehicle %-40s power %5d cap %4d goods %-12s speed %3d engine %d len %d\n", d->get_name(), d->get_power(), d->get_capacity(),
				d->get_freight_type() ? d->get_freight_type()->get_name() : "-", d->get_topspeed(), (int)d->get_engine_type(), d->get_length());
		}
	}
	else if(  !strcmp(what, "tram")  ) {
		FOR(slist_tpl<vehicle_desc_t const*>, d, vehicle_builder_t::get_info(tram_wt)) {
			logf("tramvehicle %-40s power %5d cap %4d speed %3d len %d\n", d->get_name(), d->get_power(), d->get_capacity(), d->get_topspeed(), d->get_length());
		}
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::generic_stop)) {
			if(  d->get_extra()==tram_wt  ) logf("tramstop %s enables %d\n", d->get_name(), d->get_enabled());
		}
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::depot)) {
			if(  d->get_extra()==tram_wt  ) logf("tramdepot %s\n", d->get_name());
		}
		FOR(vector_tpl<const roadsign_desc_t*>, d, roadsign_t::get_available_signs(tram_wt)) {
			logf("tramsign %-40s flags %04x count %d\n", d->get_name(), d->get_flags(), d->get_count());
		}
	}
	else if(  !strcmp(what, "road")  ) {
		FOR(slist_tpl<vehicle_desc_t const*>, d, vehicle_builder_t::get_info(road_wt)) {
			logf("roadvehicle %-40s power %5d cap %4d goods %-12s speed %3d len %d\n", d->get_name(), d->get_power(), d->get_capacity(),
				d->get_freight_type() ? d->get_freight_type()->get_name() : "-", d->get_topspeed(), d->get_length());
		}
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::generic_stop)) {
			if(  d->get_extra()==road_wt  ) logf("roadstop %s enables %d size %dx%d\n", d->get_name(), d->get_enabled(), d->get_x(), d->get_y());
		}
		FOR(vector_tpl<const building_desc_t*>, d, *hausbauer_t::get_list(building_desc_t::depot)) {
			if(  d->get_extra()==road_wt  ) logf("roaddepot %s\n", d->get_name());
		}
		FOR(vector_tpl<const way_desc_t *>, d, way_builder_t::get_way_list(road_wt, type_flat)) {
			logf("road %-40s speed %d\n", d->get_name(), d->get_topspeed());
		}
	}
	else if(  !strcmp(what, "ways")  ) {
		FOR(vector_tpl<const way_desc_t *>, d, way_builder_t::get_way_list(track_wt, type_flat)) {
			logf("way %-40s speed %d\n", d->get_name(), d->get_topspeed());
		}
		FOR(vector_tpl<const way_desc_t *>, d, way_builder_t::get_way_list(track_wt, type_tram)) {
			logf("tramway %-40s speed %d\n", d->get_name(), d->get_topspeed());
		}
	}
}

static void cmd_track(int x1, int y1, int x2, int y2)
{
	grund_t *a = ground(x1, y1), *b = ground(x2, y2);
	if(  !a  ||  !b  ) {
		return;
	}
	way_builder_t bau( PL );
	bau.init_builder( track_desc->get_wtyp()==road_wt ? way_builder_t::strasse : track_desc->get_styp()==type_tram ? way_builder_t::schiene_tram : way_builder_t::schiene, track_desc );
	if(  x1==x2  ||  y1==y2  ) {
		bau.calc_straight_route( a->get_pos(), b->get_pos() );
	}
	else {
		bau.calc_route( a->get_pos(), b->get_pos() );
	}
	if(  bau.get_count() < 2  ) {
		logf("ERROR no track route %d,%d -> %d,%d\n", x1, y1, x2, y2);
		failures++;
		return;
	}
	bau.build();
}

static void cmd_wire(const char *wt, int x1, int y1, int x2, int y2)
{
	const way_obj_desc_t *d = wayobj_t::get_overhead_line( !strcmp(wt, "tram") ? tram_wt : track_wt, 0 );
	grund_t *a = ground(x1, y1), *b = ground(x2, y2);
	if(  !d  ||  !a  ||  !b  ) {
		logf("ERROR no overhead line for %s\n", wt);
		failures++;
		return;
	}
	const char *err = NULL;
	for(  int y=min(y1,y2);  y<=max(y1,y2);  y++  ) {
		for(  int x=min(x1,x2);  x<=max(x1,x2);  x++  ) {
			grund_t *gr = ground(x, y);
			if(  gr  &&  gr->get_weg(track_wt)  &&  !gr->get_depot()  ) {
				const char *e = use_tool( TOOL_BUILD_WAYOBJ, d->get_name(), gr->get_pos(), gr->get_pos() );
				if(  e  &&  *e  ) err = e;
			}
		}
	}
	if(  err  &&  *err  ) {
		logf("ERROR wire %s: %s\n", d->get_name(), translator::translate(err));
		failures++;
	}
}

static void cmd_station(const char *name, int x, int y)
{
	grund_t *gr = ground(x, y);
	if(  !gr  ) return;
	const char *err = use_tool( TOOL_BUILD_STATION, name, gr->get_pos() );
	if(  err  &&  *err  ) {
		logf("ERROR station %s at %d,%d: %s\n", name, x, y, translator::translate(err));
		failures++;
	}
}

static roadsign_t *find_sign(grund_t *gr)
{
	if(  signal_t *s = gr->find<signal_t>()  ) {
		return s;
	}
	return gr->find<roadsign_t>();
}

// aspect watch: station boundaries and choose signals placed by 'sign'
struct aspect_watch_t { koord3d pos; int last; };
static vector_tpl<aspect_watch_t> aspect_watch;
static const char *aspect_name(int s) { return s==0 ? "red" : s==1 ? "green" : s==2 ? "yellow" : "?"; }

// DIR: travel direction the object applies to (N/S/E/W), or B for both
static void cmd_sign(const char *name, int x, int y, const char *d)
{
	grund_t *gr = ground(x, y);
	if(  !gr  ) return;
	const ribi_t::ribi want = dir_of(d);
	for(  int click=0;  click<4;  click++  ) {
		roadsign_t *rs = find_sign(gr);
		if(  rs  ) {
			bool ok;
			if(  want==ribi_t::none  ) {
				ok = ribi_t::is_twoway( rs->get_dir() )  ||  rs->get_dir()==gr->get_weg_ribi_unmasked(track_wt);
			}
			else {
				// the other way through the tile: backward on a straight tile, the other leg on a curve
				const ribi_t::ribi tr = gr->get_weg_ribi_unmasked(track_wt);
				const ribi_t::ribi other = ribi_t::is_bend(tr)  &&  (tr & want) ? (ribi_t::ribi)(tr & ~want) : ribi_t::backward(want);
				ok = sign_applies( rs, want )  &&  !sign_applies( rs, other );
			}
			if(  ok  ) {
				if(  rs->get_desc()->is_station_boundary()  ||  rs->get_desc()->is_choose_sign()  ) {
					aspect_watch_t a;
					a.pos = gr->get_pos();
					a.last = rs->get_state();
					aspect_watch.append( a );
				}
				return;
			}
		}
		const char *err = use_tool( TOOL_BUILD_ROADSIGN, name, gr->get_pos() );
		if(  err  &&  *err  ) {
			logf("ERROR sign %s at %d,%d: %s\n", name, x, y, translator::translate(err));
			failures++;
			return;
		}
	}
	logf("ERROR sign %s at %d,%d could not be turned to %s\n", name, x, y, d);
	failures++;
}

static void cmd_depot(const char *name, int x, int y)
{
	grund_t *gr = ground(x, y);
	if(  !gr  ) return;
	const char *err = use_tool( TOOL_BUILD_DEPOT, name, gr->get_pos() );
	if(  err  &&  *err  ) {
		logf("ERROR depot %s at %d,%d: %s\n", name, x, y, translator::translate(err));
		failures++;
	}
}

static uint8 stop_type_of(const char *t)
{
	switch(  t ? t[0] : 'R'  ) {
		case 'T': return schedule_entry_t::terminal;
		case 'A': return schedule_entry_t::all_off;
		case 'L': return schedule_entry_t::only_load;
		case 'U': return schedule_entry_t::only_unload;
		case 'H': return schedule_entry_t::hold;
	}
	return schedule_entry_t::regular;
}

static void cmd_stop(int x, int y, int minload, int waitmin, const char *type)
{
	grund_t *gr = ground(x, y);
	if(  !gr  ||  !cur_sched  ) return;
	cur_sched->append( gr, minload, 0 );
	schedule_entry_t &e = cur_sched->entries.back();
	e.waiting_time = waitmin;
	e.stop_type = stop_type_of(type);
}

static void cmd_couple(const char *linename, int wait)
{
	if(  !cur_sched  ||  cur_sched->empty()  ||  lines.find(linename)==lines.end()  ) {
		logf("ERROR couple %s\n", linename ? linename : "-");
		failures++;
		return;
	}
	schedule_entry_t &e = cur_sched->entries.back();
	e.couple_line_id = lines[linename].get_id();
	e.couple_max_wait = wait;
}

static void cmd_endline()
{
	if(  !cur_sched  ) return;
	cur_sched->set_current_stop(0);
	cur_sched->finish_editing();
	linehandle_t l = PL->simlinemgmt.create_line( cur_line_bus ? simline_t::truckline : cur_line_tram ? simline_t::tramline : simline_t::trainline, PL, cur_sched );
	l->set_name( cur_line_name.c_str() );
	lines[cur_line_name] = l;
	delete cur_sched;
	cur_sched = NULL;
}

static void cmd_convoy(int dx, int dy, const char *linename, int count)
{
	grund_t *gr = ground(dx, dy);
	depot_t *dep = gr ? gr->get_depot() : NULL;
	if(  !dep  ) {
		logf("ERROR no depot at %d,%d\n", dx, dy);
		failures++;
		return;
	}
	if(  lines.find(linename)==lines.end()  ) {
		logf("ERROR no line %s\n", linename);
		failures++;
		return;
	}
	for(  int n=0;  n<count;  n++  ) {
		convoihandle_t cnv = dep->add_convoi( true );
		FOR(std::vector<const vehicle_desc_t *>, d, vehicles) {
			vehicle_t *v = dep->buy_vehicle( d );
			dep->append_vehicle( cnv, v, false, true );
		}
		cnv->set_line( lines[linename] );
		char buf[128];
		static std::map<std::string, int> cnv_count;
		sprintf( buf, "%s#%d", linename, ++cnv_count[linename] );
		cnv->set_name( buf );
		const bool complete = cnv->get_sum_power()>0  &&  cnv->pruefe_alle();
		const koord3d target = cnv->get_schedule()->get_current_entry().pos;
		route_t probe;
		const bool routable = cnv->get_vehicle_count()>0  &&  cnv->front()->calc_route( dep->get_pos(), target, cnv->get_min_top_speed(), &probe );
		if(  !dep->start_convoi( cnv, true )  ) {
			logf("  convoy %s: vehicles %d power %d complete %d route to %s %d\n", buf, (int)cnv->get_vehicle_count(), (int)cnv->get_sum_power(), (int)complete, target.get_str(), (int)routable);
			logf("ERROR cannot start convoy %s\n", buf);
			failures++;
		}
	}
}

static void check(bool verbose);
static void poll_aspects();
static std::map<std::string, schedule_t *> pending_edit;

static convoihandle_t find_cnv(const char *name)
{
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  strstr(cnv->get_name(), name)  ) {
			return cnv;
		}
	}
	logf("ERROR no convoy %s\n", name);
	failures++;
	return convoihandle_t();
}

// lineedit LINE insert IDX X Y | remove IDX | wait IDX MIN : edit the line's schedule like the line tool ('g') does
static void cmd_lineedit(const char *linename, const char *op, int idx, int a1, int a2)
{
	if(  lines.find(linename)==lines.end()  ) {
		logf("ERROR no line %s\n", linename);
		failures++;
		return;
	}
	linehandle_t line = lines[linename];
	const bool batch = pending_edit.find(linename)!=pending_edit.end();
	schedule_t *s = batch ? pending_edit[linename] : line->get_schedule()->copy();
	if(  !strcmp(op, "insert")  ) {
		grund_t *gr = ground(a1, a2);
		s->set_current_stop( idx );
		s->insert( gr, 0, 0 );
	}
	else if(  !strcmp(op, "remove")  ) {
		s->set_current_stop( idx );
		s->remove();
	}
	else if(  !strcmp(op, "wait")  ) {
		s->entries[idx].waiting_time = a1;
	}
	else if(  !strcmp(op, "offset")  ) {
		s->entries[idx].departure_offset = a1;
	}
	logf("  [%lld] LINEEDIT %s %s %d -> %d entries%s\n", (long long)W->get_calendar_minutes(), linename, op, idx, (int)s->get_count(), batch ? " (pending)" : "");
	if(  !batch  ) {
		s->finish_editing();
		line->set_schedule( s );
		line->get_owner()->simlinemgmt.update_line( line );
	}
}

// linebegin LINE / linecommit LINE: several lineedits applied as one change
static void cmd_linebegin(const char *linename)
{
	pending_edit[linename] = lines[linename]->get_schedule()->copy();
}

static void cmd_linecommit(const char *linename)
{
	schedule_t *s = pending_edit[linename];
	pending_edit.erase(linename);
	linehandle_t line = lines[linename];
	s->finish_editing();
	line->set_schedule( s );
	line->get_owner()->simlinemgmt.update_line( line );
	logf("  [%lld] LINECOMMIT %s -> %d entries\n", (long long)W->get_calendar_minutes(), linename, (int)s->get_count());
}

// desync CONVOY IDX : points a convoy's schedule at entry IDX (what a bad match after a line edit does)
static void cmd_desync(const char *name, int idx)
{
	convoihandle_t cnv = find_cnv(name);
	if(  !cnv.is_bound()  ) return;
	cnv->get_schedule()->set_current_stop( idx );
	logf("  [%lld] DESYNC %s -> entry %d state %s\n", (long long)W->get_calendar_minutes(), cnv->get_name(), idx, state_name(cnv->get_state()));
}

// cnvsched CONVOY IDX : applies the convoy's own schedule with current stop IDX (convoy tool 'g')
static void cmd_cnvsched(const char *name, int idx)
{
	convoihandle_t cnv = find_cnv(name);
	if(  !cnv.is_bound()  ) return;
	schedule_t *s = cnv->get_schedule()->copy();
	s->finish_editing();
	s->set_current_stop( idx );
	const int before = cnv->get_state();
	cnv->set_schedule( s );
	logf("  [%lld] CNVSCHED %s -> entry %d state %s -> %s line %d\n", (long long)W->get_calendar_minutes(), cnv->get_name(), idx, state_name(before), state_name(cnv->get_state()), (int)cnv->get_line().is_bound());
}

// entries: current stop of every convoy (the joined one too)
static void cmd_entries()
{
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_schedule()  ) {
			logf("  [%lld] ENTRY %-10s %-14s stop %d/%d %s wait_lock %d editing_finished %d pending %d\n", (long long)W->get_calendar_minutes(), cnv->get_name(), state_name(cnv->get_state()),
				cnv->get_schedule()->get_current_stop(), cnv->get_schedule()->get_count(), cnv->get_schedule()->get_current_entry().pos.get_str(),
				0, (int)cnv->get_schedule()->is_editing_finished(), 0);
		}
	}
}

// run_until CONVOY STATE MAXMIN : run until the convoy is in that state (by name)
static void cmd_run_until(const char *name, const char *state, int maxmin, int stop_idx)
{
	const sint64 end = W->get_calendar_minutes() + maxmin;
	while(  W->get_calendar_minutes() < end  ) {
		W->sync_step( 100, true, false );
		set_random_mode( STEP_RANDOM );
		W->step();
		clear_random_mode( STEP_RANDOM );
		poll_aspects();
		check( false );
		FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
			if(  strstr(cnv->get_name(), name)  &&  !strcmp(state_name(cnv->get_state()), state)  &&  (stop_idx<0  ||  cnv->get_schedule()->get_current_stop()==stop_idx)  ) {
				logf("  [%lld] REACHED %s %s stop %d at %s\n", (long long)W->get_calendar_minutes(), cnv->get_name(), state, cnv->get_schedule()->get_current_stop(), cnv->front()->get_pos().get_str());
				return;
			}
		}
	}
	logf("ERROR %s never %s within %d min\n", name, state, maxmin);
	failures++;
}

// --- running and checking -----------------------------------------------------------------------

struct watch_t {
	koord3d pos;
	sint64 since;       // calendar minute the convoy stopped at pos (outside a station)
	bool reported;
	int stops_done;
	uint8 last_stop;
	sint64 max_off_station;
	uint16 last_hold;
	int last_state;
	int couplings;
	koord3d last_claim;
	int holds;
	int claims;
	koord3d last_sec;
	sint64 sw_since;
	sint64 sw_max;
	int sw_max_kind;
	int follows;
	sint64 progress_at;
	std::string last_wait;
	int parks;          // times it pulled into a bay of a bus layover (baywatch on)
	bool parked;
};
static bool log_wait = false;
static bool log_events = true;
static bool log_stops = false;
static std::vector<int> stop_lines; // only these lines in the STOP log (empty: all)
static bool stop_line_ok(convoihandle_t c) { if(  stop_lines.empty()  ) return true; for(  size_t i=0;  i<stop_lines.size();  i++  ) if(  c->get_line().is_bound()  &&  c->get_line().get_id()==stop_lines[i]  ) return true; return false; }
static std::map<uint16, watch_t> watch;
static std::string watch_filter;
static bool watched(convoihandle_t c) { return watch_filter.empty()  ||  strstr(c->get_name(), watch_filter.c_str())!=NULL; }
static int report_after = 3; // calendar minutes standing outside a station before it is logged

static bool on_station(const koord3d &p)
{
	const grund_t *gr = W->lookup(p);
	if(  gr  &&  gr->get_halt().is_bound()  ) {
		return true;
	}
	// waiting at a platform signal right after the platform counts as in the station
	const signal_t *s = gr ? gr->find<signal_t>() : NULL;
	return s  &&  IS_P(s->get_desc());
}

static void cmd_res(int x0, int y0, int x1, int y1)
{
	for(  int y=y0;  y<=y1;  y++  ) {
		char row[1024];
		int n = sprintf(row, "%4d ", y);
		for(  int x=x0;  x<=x1  &&  n<1000;  x++  ) {
			const grund_t *gr = W->lookup_kartenboden(koord(x, y));
			const schiene_t *sch = gr ? (const schiene_t *)gr->get_weg(track_wt) : NULL;
			char c = ' ';
			if(  sch  ) {
				const convoihandle_t r = sch->get_reserved_convoi();
				c = r.is_bound() ? (char)('0' + r.get_id() % 10) : '.';
			}
			row[n++] = c;
		}
		row[n] = 0;
		logf("%s\n", row);
	}
}

static void poll_aspects()
{
	const sint64 now = W->get_calendar_minutes();
	FOR(vector_tpl<aspect_watch_t>, &a, aspect_watch) {
		grund_t *gr = W->lookup( a.pos );
		roadsign_t *rs = gr ? find_sign( gr ) : NULL;
		if(  rs  &&  rs->get_state()!=a.last  ) {
			const schiene_t *sch = (const schiene_t *)gr->get_weg( track_wt );
			convoihandle_t res = sch ? sch->get_reserved_convoi() : convoihandle_t();
			logf("  [%lld] ASPECT %s %s: %s -> %s (tile reserved by %s)\n", (long long)now, a.pos.get_2d().get_str(), rs->get_desc()->get_name(),
				aspect_name(a.last), aspect_name(rs->get_state()), res.is_bound() ? res->get_name() : "-");
			if(  res.is_bound()  ) {
				logf("      to %s\n", res->get_route()->back().get_str()); if(  res->get_line().is_bound()  ) logf("      line entry %s\n", res->get_line()->get_schedule()->entries[res->get_schedule()->get_current_stop()].pos.get_str());
			}
			a.last = rs->get_state();
		}
	}
}

static void check(bool verbose)
{
	const sint64 now = W->get_calendar_minutes();
	poll_aspects();
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  cnv->in_depot()  ||  !watched(cnv)  ) continue;
		watch_t &w = watch[cnv.get_id()];
		const int st = cnv->get_state();
		if(  (st==convoi_t::COUPLED  ||  st==convoi_t::UNCOUPLING)  !=  (w.last_state==convoi_t::COUPLED  ||  w.last_state==convoi_t::UNCOUPLING)  ||  (st!=w.last_state  &&  (st==convoi_t::COUPLED  ||  st==convoi_t::UNCOUPLING))  ) {
			if(  st==convoi_t::COUPLED  ) w.couplings++;
			if(  log_events  ) logf("  [%lld] STATE %s %s -> %s at %s\n", (long long)now, cnv->get_name(), state_name(w.last_state), state_name(st), cnv->get_vehicle_count() ? cnv->front()->get_pos().get_str() : "-");
		}
		w.last_state = st;
		if(  st==convoi_t::COUPLED  ||  st==convoi_t::UNCOUPLING  ) {
			if(  cnv->get_schedule()  &&  w.last_stop!=cnv->get_schedule()->get_current_stop()  ) {
				w.last_stop = cnv->get_schedule()->get_current_stop();
				char at[32];
				strcpy( at, cnv->get_coupled_convoi().is_bound()  &&  cnv->get_coupled_convoi()->get_vehicle_count() ? cnv->get_coupled_convoi()->front()->get_pos().get_str() : "-" );
				if(  log_stops  &&  stop_line_ok(cnv)  ) logf("  [%lld] STOP (joined) %s, pair at %s -> entry %d%s\n", (long long)W->get_calendar_minutes(), cnv->get_name(), at, (int)w.last_stop, cnv->is_waypoint( cnv->get_schedule()->get_current_entry().pos ) ? " (waypoint)" : "");
			}
			continue;
		}
		const koord3d p = cnv->front()->get_pos();
		{ static std::map<int,int> ms; const int m = cnv->has_missed_slot() ? 1 : 0; if(  ms[cnv.get_id()] != m  ) { ms[cnv.get_id()] = m; logf("  [%lld] MISSED %s %s entry %d delay=%d ahead=%d\n", (long long)now, cnv->get_name(), m ? "on" : "off", cnv->get_schedule() ? (int)cnv->get_schedule()->get_current_stop() : -1, (int)cnv->get_departure_delay(), cnv->get_line().is_bound() ? (int)cnv->get_line()->count_earlier_waiting( cnv ) : -1); } }
		if(  log_wait  ) {
			cbuffer_t wb;
			cnv->append_wait_reason( wb );
			std::string wt = (const char *)wb;
			if(  wt!=w.last_wait  ) {
				halthandle_t hh = haltestelle_t::get_halt( p, PL );
				logf("  [%lld] WAITTXT %s %s at %s (%s) loading %d%%: %s\n", (long long)now, cnv->get_name(), state_name(st), p.get_str(), hh.is_bound() ? hh->get_name() : "-", cnv->get_loading_level(), wt.empty() ? "(none)" : wt.c_str());
				w.last_wait = wt;
			}
		}
		if(  cnv->get_schedule()  &&  w.last_stop!=cnv->get_schedule()->get_current_stop()  ) {
			w.last_stop = cnv->get_schedule()->get_current_stop();
			w.stops_done++;
			w.progress_at = now;
			if(  log_stops  &&  stop_line_ok(cnv)  ) logf("  [%lld] STOP %s at %s -> entry %d%s delay=%d\n", (long long)now, cnv->get_name(), p.get_str(), (int)w.last_stop, cnv->is_waypoint( cnv->get_schedule()->get_current_entry().pos ) ? " (waypoint)" : "", (int)cnv->get_departure_delay());
		}
		// PR #1 waiting for a passing train, and PR #4 claims
		const convoihandle_t hold = cnv->get_passing_hold_for();
		if(  hold.get_id()!=w.last_hold  ) {
			if(  hold.is_bound()  ) {
				w.holds++;
				if(  log_events  ) logf("  [%lld] HOLD %s at %s waits for %s to pass\n", (long long)now, cnv->get_name(), p.get_str(), hold->get_name());
			}
			w.last_hold = hold.get_id();
		}
		const koord3d claim = HAS_CLAIM(cnv) ? CLAIM_B(cnv) : koord3d::invalid;
		if(  claim!=w.last_claim  ) {
			if(  claim!=koord3d::invalid  &&  w.last_claim==koord3d::invalid  ) {
				w.claims++;
				if(  log_events  ) logf("  [%lld] CLAIM %s at %s\n", (long long)now, cnv->get_name(), p.get_str());
			}
			w.last_claim = claim;
		}
		if(  SWAIT(cnv)!=0  &&  cnv->get_akt_speed()==0  ) {
			if(  w.sw_since<=0  ) w.sw_since = now;
			if(  now-w.sw_since > w.sw_max  ) { w.sw_max = now-w.sw_since; w.sw_max_kind = SWAIT(cnv); }
		}
		else w.sw_since = 0;
		// block posts: entering a single-track section, and with whom
		if(  cnv->get_section_to()!=w.last_sec  ) {
			if(  cnv->in_section()  ) {
				char others[512] = "";
				int same = 0;
				FOR(vector_tpl<convoihandle_t>, const o, W->convoys()) {
					if(  o!=cnv  &&  o->in_section()  &&  (o->get_section_to()==cnv->get_section_to()  ||  o->get_section_from()==cnv->get_section_to())  ) {
						if(  strlen(others)<400  ) { strcat( others, " " ); strcat( others, o->get_name() ); strcat( others, o->get_section_to()==cnv->get_section_to() ? "(same)" : "(opp)" ); }
						same += o->get_section_to()==cnv->get_section_to()  &&  o->get_section_from()==cnv->get_section_from();
					}
				}
				if(  same>0  ) w.follows++;
				if(  log_events  ) { char f[32], t[32]; strcpy( f, cnv->get_section_from().get_str() ); strcpy( t, cnv->get_section_to().get_str() ); logf("  [%lld] SECTION %s %s -> %s%s%s\n", (long long)now, cnv->get_name(), f, t, others[0] ? " with" : "", others); }
			}
			w.last_sec = cnv->get_section_to();
		}
		const bool standing = cnv->get_akt_speed()==0;
		if(  standing  &&  !on_station(p)  &&  st!=convoi_t::LOADING  ) {
			if(  w.pos!=p  ) {
				w.pos = p;
				w.since = now;
				w.reported = false;
			}
			const sint64 stood = now - w.since;
			if(  stood > w.max_off_station  ) {
				w.max_off_station = stood;
			}
			if(  stood >= report_after  &&  !w.reported  ) {
				w.reported = true;
				logf("  [%lld] OFF-STATION %s stands at %s state %s section_wait %d\n", (long long)now, cnv->get_name(), p.get_str(), state_name(st), SWAIT(cnv));
			}
		}
		else {
			w.pos = koord3d::invalid;
		}
		if(  verbose  ) {
			char to[32], entry[32];
			strcpy( to, cnv->get_route()->empty() ? "-" : cnv->get_route()->back().get_str() );
			strcpy( entry, cnv->get_schedule() ? cnv->get_schedule()->get_current_entry().pos.get_str() : "-" );
			logf("  [%lld] %-24s %-14s at %-12s %3d km/h stop %d/%d entry %-12s route->%-12s claim %s sw %d since %lld\n", (long long)now, cnv->get_name(), state_name(st), p.get_str(),
				speed_to_kmh(cnv->get_akt_speed()), cnv->get_schedule() ? cnv->get_schedule()->get_current_stop() : -1,
				cnv->get_schedule() ? cnv->get_schedule()->get_count() : 0, entry, to,
				HAS_CLAIM(cnv) ? CLAIM_B(cnv).get_str() : "-", SWAIT(cnv), (long long)(SWAIT(cnv) ? (sint64)(W->get_ticks()-cnv->get_section_wait_since())*W->get_settings().get_minutes_per_month()/W->ticks_per_world_month : -1));
		}
	}
}

// bus layover trace: one line per change of a road convoy's state, position or bay
static bool bay_watch = false;
static int bay_errors = 0;
static std::map<int, std::string> bay_last;
static std::string bay_sig(convoihandle_t cnv)
{
	char b[256];
	const char *ph = cnv->is_parked_in_bay() ? "PARKED" : cnv->is_queued_for_bay() ? "QUEUED" : cnv->get_layover_pos()!=koord3d::invalid ? "APPROACH" : "-";
	sprintf( b, "%s pos=%d,%d dir=%d entry=%d bay=%s", state_name(cnv->get_state()), cnv->front()->get_pos().x, cnv->front()->get_pos().y, (int)cnv->front()->get_direction(),
		cnv->get_schedule() ? (int)cnv->get_schedule()->get_current_stop() : -1, ph );
	std::string s = b;
	if(  cnv->get_layover_pos()!=koord3d::invalid  ) {
		sprintf( b, "@%d,%d#%d", cnv->get_layover_pos().x, cnv->get_layover_pos().y, (int)cnv->get_layover_bay() );
		s += b;
	}
	return s;
}
static void bay_poll()
{
	std::map<std::string, int> parked;
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=road_wt  ) continue;
		const std::string s = bay_sig(cnv);
		if(  bay_last[cnv.get_id()] != s  ) {
			bay_last[cnv.get_id()] = s;
			logf("  [%lld] BAY %s %s\n", (long long)W->get_calendar_minutes(), cnv->get_name(), s.c_str());
		}
		watch_t &w = watch[cnv.get_id()];
		if(  cnv->is_parked_in_bay()  &&  !w.parked  ) {
			w.parks++;
		}
		w.parked = cnv->is_parked_in_bay();
		if(  cnv->is_parked_in_bay()  ) {
			char k[64];
			sprintf( k, "%d,%d,%d#%d", cnv->get_layover_pos().x, cnv->get_layover_pos().y, cnv->get_layover_pos().z, (int)cnv->get_layover_bay() );
			if(  parked.count(k)  ) { logf("  [%lld] BAY-ERROR two convoys parked in bay %s\n", (long long)W->get_calendar_minutes(), k); bay_errors++; failures++; }
			parked[k] = 1;
			if(  cnv->front()->get_pos()!=cnv->get_layover_pos()  ) { logf("  [%lld] BAY-ERROR %s parked away from its bay\n", (long long)W->get_calendar_minutes(), cnv->get_name()); bay_errors++; failures++; }
		}
	}
}

static void cmd_bays()
{
	logf("bays at %lld:\n", (long long)W->get_calendar_minutes());
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=road_wt  ) continue;
		cbuffer_t wb;
		cnv->append_wait_reason( wb );
		logf("  %-20s %s %s\n", cnv->get_name(), bay_sig(cnv).c_str(), wb.get_str());
	}
	logf("  bay errors so far: %d\n", bay_errors);
}

static void cmd_run(int minutes, int verbose_every)
{
	const sint64 start = W->get_calendar_minutes();
	sint64 last = start, last_verbose = start;
	const sint64 end = start + minutes;
	while(  W->get_calendar_minutes() < end  ) {
		W->sync_step( 100, true, false );
		set_random_mode( STEP_RANDOM );
		W->step();
		clear_random_mode( STEP_RANDOM );
		poll_aspects();
		if(  bay_watch  ) bay_poll();
		const sint64 now = W->get_calendar_minutes();
		if(  now != last  ) {
			last = now;
			const bool verbose = verbose_every > 0  &&  now - last_verbose >= verbose_every;
			if(  verbose  ) {
				last_verbose = now;
			}
			check( verbose );
		}
	}
	logf("run %d min done at calendar minute %lld\n", minutes, (long long)W->get_calendar_minutes());
}

static void diag_tile(const koord3d &pos, char *buf)
{
	grund_t *gr = W->lookup(pos);
	buf[0] = 0;
	if(  !gr  ) { strcpy(buf, "NOGROUND"); return; }
	const schiene_t *sch = (const schiene_t *)gr->get_weg(track_wt);
	if(  sch  ) {
		convoihandle_t r = sch->get_reserved_convoi();
		if(  r.is_bound()  ) sprintf(buf+strlen(buf), " res=%d", r->self.get_id());
		sprintf(buf+strlen(buf), " ribi=%d mask=%d", sch->get_ribi_unmasked(), sch->get_ribi_maske());
	}
	if(  gr->get_halt().is_bound()  ) sprintf(buf+strlen(buf), " halt=%s", gr->get_halt()->get_name());
	roadsign_t *rs = gr->find<signal_t>();
	if(  !rs  ) rs = gr->find<roadsign_t>();
	if(  rs  ) {
		char d[8] = "";
		if(  sign_applies(rs, ribi_t::north)  ) strcat(d, "N");
		if(  sign_applies(rs, ribi_t::south)  ) strcat(d, "S");
		if(  sign_applies(rs, ribi_t::east)  ) strcat(d, "E");
		if(  sign_applies(rs, ribi_t::west)  ) strcat(d, "W");
		sprintf(buf+strlen(buf), " SIGN %s applies %s dir %d blockpost %d", rs->get_desc()->get_name(), d, rs->get_dir(), (int)IS_H(rs->get_desc()));
	}
}

static void cmd_diag(const char *name)
{
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  (!strstr(cnv->get_name(), name)  &&  atoi(name)!=(int)cnv.get_id())  ||  cnv->get_vehicle_count()==0  ) continue;
		char a[32], b[32], c[32], d[32], e[32];
		strcpy(a, cnv->get_section_from().get_str()); strcpy(b, cnv->get_section_to().get_str());
		strcpy(c, cnv->get_section_wait_boundary().get_str()); strcpy(d, cnv->get_section_wait_from().get_str()); strcpy(e, cnv->get_section_wait_tile().get_str());
		logf("DIAG %s id %d state %s front %s rear %s\n", cnv->get_name(), cnv.get_id(), state_name(cnv->get_state()), cnv->front()->get_pos().get_str(), cnv->back()->get_pos().get_str());
		{ cbuffer_t wb; cnv->append_wait_reason( wb ); logf("  waittext: %s\n", (const char *)wb); }
		logf("  section %s -> %s ; wait %d boundary %s from %s tile %s ; since %u now %u\n", a, b, (int)cnv->get_section_wait(), c, d, e, cnv->get_section_wait_since(), W->get_ticks());
		logf("  claim %s stops %d ; claim path:", cnv->has_claim() ? cnv->get_claim_boundary().get_str() : "-", (int)cnv->get_claim_stops());
		FOR(vector_tpl<koord3d>, const &k, cnv->get_claim_path()) logf(" %s", k.get_str());
		logf("\n");
		if(  cnv->get_schedule()  ) {
			const schedule_t *sch = cnv->get_schedule();
			logf("  schedule current %d:", sch->get_current_stop());
			for(  uint8 i=0;  i<sch->get_count();  i++  ) {
				halthandle_t h = haltestelle_t::get_halt(sch->entries[i].pos, cnv->get_owner());
				logf(" [%d %s %s]", i, sch->entries[i].pos.get_str(), h.is_bound() ? h->get_name() : "wp");
			}
			logf("\n");
		}
		const route_t *r = cnv->get_route();
		logf("  route %d tiles, next_stop_index %d\n", (int)r->get_count(), (int)cnv->get_next_stop_index());
		for(  uint32 i=0;  i<r->get_count();  i++  ) {
			char buf[512];
			diag_tile(r->at(i), buf);
			logf("   %4d %s%s\n", (int)i, r->at(i).get_str(), buf);
		}
	}
}

static void cmd_tiles(int x0, int y0, int x1, int y1)
{
	for(  int y=y0;  y<=y1;  y++  ) for(  int x=x0;  x<=x1;  x++  ) {
		const planquadrat_t *pl = W->access(koord(x, y));
		if(  !pl  ) continue;
		for(  uint8 bi=0;  bi<pl->get_boden_count();  bi++  ) {
			grund_t *gr = pl->get_boden_bei(bi);
			if(  !gr  ||  !gr->get_weg(track_wt)  ) continue;
			char buf[512];
			diag_tile(gr->get_pos(), buf);
			logf("  %s%s\n", gr->get_pos().get_str(), buf);
		}
	}
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) {
			koord3d p = cnv->get_vehikel(i)->get_pos();
			if(  p.x>=x0 && p.x<=x1 && p.y>=y0 && p.y<=y1  ) {
				char nm[16]; sprintf(nm, "%d", cnv.get_id());
				logf("NEAR convoy %d vehicles %d\n", cnv.get_id(), cnv->get_vehicle_count());
				cmd_diag(nm);
				break;
			}
		}
	}
}

// rail convoys whose front has not moved since the previous call (first call only records)
static int last_stuck = -1; // count of the last call, for expect_stuck_max
static void cmd_stuck()
{
	static std::map<int,std::string> last;
	std::map<int,std::string> now;
	int n = 0;
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=track_wt  ||  cnv->in_depot()  ||  !watched(cnv)  ) continue;
		const grund_t *gr = W->lookup( cnv->front()->get_pos() );
		const char *sign = "";
		if(  gr  &&  gr->find<signal_t>()  ) sign = gr->find<signal_t>()->get_desc()->is_choose_sign() ? " ON-CHOOSE" : " on-signal";
		char buf[256];
		sprintf( buf, "%s %s%s", cnv->front()->get_pos().get_str(), state_name(cnv->get_state()), sign );
		now[cnv.get_id()] = buf;
		if(  last.count(cnv.get_id())  &&  last[cnv.get_id()]==now[cnv.get_id()]  &&  cnv->get_state()!=convoi_t::LOADING  ) {
			logf("STUCK %d %s : %s\n", cnv.get_id(), buf, cnv->get_name());
			n++;
		}
	}
	logf("stuck: %d\n", n);
	last_stuck = n;
	last = now;
}

static void cmd_route(const char *name)
{
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  strstr(cnv->get_name(), name)  ) {
			const route_t *r = cnv->get_route();
			logf("route of %s (%d tiles):", cnv->get_name(), (int)r->get_count());
			for(  uint32 i=0;  i<r->get_count();  i++  ) {
				logf(" %d,%d", r->at(i).x, r->at(i).y);
			}
			logf("\n");
		}
	}
}

static void cmd_run_until_claim(int maxmin)
{
	const sint64 end = W->get_calendar_minutes() + maxmin;
	while(  W->get_calendar_minutes() < end  ) {
		W->sync_step( 100, true, false );
		set_random_mode( STEP_RANDOM );
		W->step();
		clear_random_mode( STEP_RANDOM );
		poll_aspects();
		FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
			if(  cnv->get_vehicle_count()>0  &&  HAS_CLAIM(cnv)  &&  cnv->get_state()==convoi_t::DRIVING  &&  !on_station(cnv->front()->get_pos())  ) {
				logf("claim held mid-section by %s at %s, calendar minute %lld\n", cnv->get_name(), cnv->front()->get_pos().get_str(), (long long)W->get_calendar_minutes());
				return;
			}
		}
	}
	logf("ERROR no claim mid-section within %d min\n", maxmin);
	failures++;
}

static void cmd_summary()
{
	logf("summary:\n");
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  !watched(cnv)  ) continue;
		watch_t &w = watch[cnv.get_id()];
		logf("  %-24s state %-14s stops passed %4d longest stand outside a station %lld min, held %d, claims %d, coupled %d, follows %d, last stop %lld min ago, longest section wait %lld (kind %d)\n", cnv->get_name(), state_name(cnv->get_state()), w.stops_done, (long long)w.max_off_station, w.holds, w.claims, w.couplings, w.follows, (long long)(W->get_calendar_minutes()-w.progress_at), (long long)w.sw_max, w.sw_max_kind);
	}
}

static void cmd_dump(int x0, int y0, int x1, int y1)
{
	for(  int y=y0;  y<=y1;  y++  ) {
		char row[1024];
		int n = 0;
		n += sprintf(row, "%4d ", y);
		for(  int x=x0;  x<=x1  &&  n<1000;  x++  ) {
			const grund_t *gr = W->lookup_kartenboden(koord(x, y));
			char c = ' ';
			if(  gr  ) {
				const weg_t *w = gr->get_weg(track_wt);
				if(  gr->get_depot()  ) c = 'D';
				else if(  w  ) {
					const ribi_t::ribi r = w->get_ribi_unmasked();
					c = ribi_t::is_threeway(r) ? '+' : (r==ribi_t::northsouth || r==ribi_t::north || r==ribi_t::south) ? '|' : (r==ribi_t::eastwest || r==ribi_t::east || r==ribi_t::west) ? '-' : '/';
					if(  gr->get_halt().is_bound()  ) c = '=';
					if(  const signal_t *s = gr->find<signal_t>()  ) {
						c = IS_P(s->get_desc()) ? 'P' : IS_H(s->get_desc()) ? 'H' : s->get_desc()->is_choose_sign() ? 'C' : 'S';
					}
					else if(  const roadsign_t *rs = gr->find<roadsign_t>()  ) {
						c = IS_LT(rs->get_desc()) ? 'L' : rs->get_desc()->is_end_choose_signal() ? 'E' : 's';
					}
				}
				else if(  gr->get_typ()==grund_t::wasser  ) c = '~';
				else c = '.';
			}
			row[n++] = c;
		}
		row[n] = 0;
		logf("%s\n", row);
	}
}

static void cmd_signs()
{
	// all fork objects with the direction they apply to
	for(  int y=0;  y<W->get_size().y;  y++  ) {
		for(  int x=0;  x<W->get_size().x;  x++  ) {
			const grund_t *gr = W->lookup_kartenboden(koord(x, y));
			const weg_t *w = gr ? gr->get_weg(track_wt) : NULL;
			if(  !w  ||  (!w->has_sign()  &&  !w->has_signal())  ) continue;
			const roadsign_t *rs = gr->find<signal_t>();
			if(  !rs  ) rs = gr->find<roadsign_t>();
			if(  !rs  ) continue;
			char d[8] = "";
			if(  sign_applies(rs, ribi_t::north)  ) strcat(d, "N");
			if(  sign_applies(rs, ribi_t::south)  ) strcat(d, "S");
			if(  sign_applies(rs, ribi_t::east)  ) strcat(d, "E");
			if(  sign_applies(rs, ribi_t::west)  ) strcat(d, "W");
			logf("  %3d,%3d %-28s applies %-4s way ribi %d mask %d\n", x, y, rs->get_desc()->get_name(), d, w->get_ribi_unmasked(), w->get_ribi_maske());
		}
	}
}

// render the main view centred on x,y at zoom level z (0 = closest) into FILE (png)
// run until the sign at x,y shows aspect state (0 red, 1 green, 2 yellow)
static void cmd_run_until_aspect(int x, int y, int state, int maxmin)
{
	const sint64 end = W->get_calendar_minutes() + maxmin;
	grund_t *gr = W->lookup_kartenboden(koord(x, y));
	while(  W->get_calendar_minutes() < end  ) {
		W->sync_step( 100, true, false );
		set_random_mode( STEP_RANDOM );
		W->step();
		clear_random_mode( STEP_RANDOM );
		poll_aspects();
		roadsign_t *rs = gr ? find_sign( gr ) : NULL;
		if(  rs  &&  rs->get_state()==state  ) {
			logf("  [%lld] REACHED aspect %d at %d,%d\n", (long long)W->get_calendar_minutes(), state, x, y);
			return;
		}
	}
	logf("ERROR aspect %d at %d,%d not within %d min\n", state, x, y, maxmin);
	failures++;
}

static void cmd_snap(int x, int y, int z, const char *file)
{
	extern void set_zoom_factor(int z);
	set_zoom_factor( z );
	grund_t *gr = W->lookup_kartenboden(koord(x, y));
	W->get_viewport()->change_world_position( gr ? gr->get_pos() : koord3d(x, y, 0) );
	W->get_view()->display( true );
	dr_screenshot( file, 0, 0, display_get_width(), display_get_height() );
	const scr_coord sc = W->get_viewport()->get_screen_coord( gr ? gr->get_pos() : koord3d(x, y, 0) );
	logf("snap %d,%d zoom %d -> %s (%dx%d) tile at %d,%d width %d\n", x, y, z, file, display_get_width(), display_get_height(), sc.x, sc.y, get_tile_raster_width());
}

// --- script ------------------------------------------------------------------------------------

// ---- linelayout NAME: the layout along every leg of a line's schedule ----

struct ll_less { bool operator()(const koord3d &a, const koord3d &b) const { return a.x!=b.x ? a.x<b.x : a.y!=b.y ? a.y<b.y : a.z<b.z; } };

static const ribi_t::ribi ll_nesw[4] = { ribi_t::north, ribi_t::east, ribi_t::south, ribi_t::west };

static const char *ll_sign_kind(const roadsign_desc_t *d)
{
	if(  d->is_station_boundary()  ) return "LT station boundary";
	if(  d->is_platform_signal()  ) return "P platform signal";
	if(  d->is_block_post()  ) return "block post";
	if(  d->is_autoblock()  ) return "autoblock signal";
	if(  d->is_end_choose_signal()  ) return "end of choose";
	if(  d->is_choose_sign()  ) return "choose signal";
	if(  d->is_pre_signal()  ) return "presignal";
	if(  d->is_priority_signal()  ) return "priority signal";
	if(  d->is_longblock_signal()  ) return "long block signal";
	if(  d->is_signal_type()  ) return "signal";
	if(  d->is_single_way()  ) return "one-way sign";
	return "sign";
}

static const char *ll_dir(ribi_t::ribi d)
{
	switch(  d  ) {
		case ribi_t::north: return "N";
		case ribi_t::south: return "S";
		case ribi_t::east: return "E";
		case ribi_t::west: return "W";
		default: return "?";
	}
}

static void ll_dirs(ribi_t::ribi r, char *out)
{
	out[0] = 0;
	if(  r & ribi_t::north  ) strcat(out, "N");
	if(  r & ribi_t::east  ) strcat(out, "E");
	if(  r & ribi_t::south  ) strcat(out, "S");
	if(  r & ribi_t::west  ) strcat(out, "W");
}

// sign on this tile, described for a train leaving it towards exit_dir (0 = no direction known)
static bool ll_sign(const grund_t *gr, ribi_t::ribi exit_dir, char *out)
{
	out[0] = 0;
	const roadsign_t *rs = gr->find<signal_t>();
	if(  !rs  ) rs = gr->find<roadsign_t>();
	if(  !rs  ) return false;
	char ap[8] = "";
	if(  rs->applies_to(ribi_t::north)  ) strcat(ap, "N");
	if(  rs->applies_to(ribi_t::east)  ) strcat(ap, "E");
	if(  rs->applies_to(ribi_t::south)  ) strcat(ap, "S");
	if(  rs->applies_to(ribi_t::west)  ) strcat(ap, "W");
	const char *rel = "";
	if(  exit_dir  ) rel = rs->applies_to(exit_dir) ? " -> FOR US" : " -> other direction only";
	sprintf(out, "%s '%s' (applies to trains leaving %s)%s", ll_sign_kind(rs->get_desc()), rs->get_desc()->get_name(), ap[0] ? ap : "-", rel);
	return true;
}

static void ll_way(const grund_t *gr, char *out)
{
	const schiene_t *sch = (const schiene_t *)gr->get_weg(track_wt);
	out[0] = 0;
	if(  !sch  ) { strcpy(out, "no track"); return; }
	sprintf(out, "%s %dkm/h%s%s%s", sch->get_desc()->get_name(), sch->get_max_speed(), sch->is_electrified() ? " wired" : " unwired",
		gr->ist_bruecke() ? " bridge" : "", gr->ist_tunnel() ? " tunnel" : "");
}

struct ll_branch_t {
	int tiles;
	int rejoin_min, rejoin_max;   // route indices reached
	bool dead_end, depot, too_far;
	std::vector<std::string> halts, signs, notes;
	std::vector<int> sign_dist, note_dist;
	std::vector<int> halt_dist;
};

// walk a branch from tile 'from' in direction d, over track not on the route, up to max tiles
static void ll_walk_branch(const grund_t *from, ribi_t::ribi d, const std::map<koord3d,int,ll_less> &on_route, ll_branch_t &b, int max)
{
	b.tiles = 0; b.rejoin_min = -1; b.rejoin_max = -1; b.dead_end = b.depot = b.too_far = false;
	std::map<koord3d,int,ll_less> seen;
	std::vector<std::pair<const grund_t *, ribi_t::ribi> > todo;   // tile, direction we entered it with
	std::map<koord3d,int,ll_less> dist;
	grund_t *to;
	if(  !from->get_neighbour(to, track_wt, d)  ) { b.notes.push_back("no connected track"); return; }
	todo.push_back( std::make_pair((const grund_t *)to, d) );
	dist[to->get_pos()] = 1;
	std::map<std::string,int> halt_tiles, halt_d;
	std::vector<std::string> halt_order;
	while(  !todo.empty()  ) {
		const grund_t *gr = todo.front().first;
		const ribi_t::ribi came = todo.front().second;
		todo.erase( todo.begin() );
		const int dd = dist[gr->get_pos()];
		if(  seen.count(gr->get_pos())  ) continue;
		std::map<koord3d,int,ll_less>::const_iterator it = on_route.find(gr->get_pos());
		if(  it!=on_route.end()  ) {
			if(  b.rejoin_min<0  ||  it->second<b.rejoin_min  ) b.rejoin_min = it->second;
			if(  it->second>b.rejoin_max  ) b.rejoin_max = it->second;
			continue;
		}
		seen[gr->get_pos()] = 1;
		if(  (int)seen.size()>max  ) { b.too_far = true; break; }
		b.tiles++;
		if(  gr->get_halt().is_bound()  ) {
			std::string n = gr->get_halt()->get_name();
			if(  !halt_tiles.count(n)  ) { halt_order.push_back(n); halt_d[n] = dd; }
			halt_tiles[n]++;
		}
		if(  gr->get_depot()  ) { b.depot = true; char s[96]; sprintf(s, "depot at %s", gr->get_pos().get_str()); b.notes.push_back(s); b.note_dist.push_back(dd); }
		char sg[256];
		if(  ll_sign(gr, came, sg)  ) {
			char s[320]; sprintf(s, "%s %s (walking %s)", gr->get_pos().get_str(), sg, ll_dir(came));
			b.signs.push_back(s); b.sign_dist.push_back(dd);
		}
		const schiene_t *sch = (const schiene_t *)gr->get_weg(track_wt);
		if(  !sch  ) continue;
		ribi_t::ribi r = sch->get_ribi_unmasked() & ~ribi_t::backward(came);
		if(  r==0  &&  !gr->get_depot()  ) {
			b.dead_end = true;
			char s[96]; sprintf(s, "buffer stop at %s", gr->get_pos().get_str()); b.notes.push_back(s); b.note_dist.push_back(dd);
		}
		if(  ribi_t::is_threeway(sch->get_ribi_unmasked())  ) {
			char s[96]; sprintf(s, "switch at %s", gr->get_pos().get_str()); b.notes.push_back(s); b.note_dist.push_back(dd);
		}
		for(  int i=0;  i<4;  i++  ) {
			const ribi_t::ribi nd = ll_nesw[i];
			if(  (r & nd)  &&  gr->get_neighbour(to, track_wt, nd)  &&  !dist.count(to->get_pos())  ) { dist[to->get_pos()] = dd+1; todo.push_back( std::make_pair((const grund_t *)to, nd) ); }
		}
	}
	for(  size_t i=0;  i<halt_order.size();  i++  ) {
		char s[256]; sprintf(s, "%s (%d tiles, first after %d)", halt_order[i].c_str(), halt_tiles[halt_order[i]], halt_d[halt_order[i]]);
		b.halts.push_back(s); b.halt_dist.push_back(halt_d[halt_order[i]]);
	}
}

static void cmd_linelayout(const char *name, int branch_max)
{
	if(  branch_max<=0  ) branch_max = 150;
	vector_tpl<linehandle_t> lines;
	for(  int p=0;  p<MAX_PLAYER_COUNT;  p++  ) {
		player_t *pl = W->get_player(p);
		if(  !pl  ) continue;
		vector_tpl<linehandle_t> l;
		pl->simlinemgmt.get_lines(simline_t::line, &l);
		FOR(vector_tpl<linehandle_t>, const line, l) if(  strstr(line->get_name(), name)  ) lines.append(line);
	}
	logf("LINES matching '%s': %d\n", name, (int)lines.get_count());
	FOR(vector_tpl<linehandle_t>, const line, lines) logf("  line %d '%s' type %d, %d convoys\n", line.get_id(), line->get_name(), (int)line->get_linetype(), (int)line->count_convoys());
	FOR(vector_tpl<linehandle_t>, const line, lines) {
		if(  line->get_linetype()!=simline_t::trainline  ) continue;
		const schedule_t *sch = line->get_schedule();
		player_t *owner = line->get_owner();
		logf("\n================ LINE %d '%s' (owner %s) ================\n", line.get_id(), line->get_name(), owner->get_name());
		logf("schedule flags: no_standing %d no_overcrowding %d; hold marker %d\n", (int)sch->is_no_standing(), (int)sch->is_no_overcrowding(), (int)line->get_hold_marker());
		logf("\nSCHEDULE (%d entries):\n", (int)sch->get_count());
		for(  uint8 i=0;  i<sch->get_count();  i++  ) {
			const schedule_entry_t &e = sch->entries[i];
			halthandle_t h = haltestelle_t::get_halt(e.pos, owner);
			const grund_t *gr = W->lookup(e.pos);
			const char *what = h.is_bound() ? h->get_name() : (gr && gr->get_depot() ? "DEPOT" : "waypoint");
			logf("  [%2d] %-16s %s", i, e.pos.get_str(), what);
			if(  h.is_bound()  ) logf(" | %s", schedule_entry_t::get_stop_type_name(e.stop_type));
			if(  e.minimum_loading  ) logf(" | min load %d%%", e.minimum_loading);
			if(  e.waiting_time  ) logf(" | max wait %d min", e.waiting_time);
			else if(  e.waiting_time_shift  ) logf(" | wait shift %d", e.waiting_time_shift);
			if(  e.departure_interval  ) {
				uint16 offs[16];
				const uint8 n = e.get_departure_offsets(offs);
				logf(" | every %d min at", e.departure_interval);
				for(  uint8 k=0;  k<n;  k++  ) logf(" +%d", offs[k]);
				if(  e.departure_window!=schedule_entry_t::WINDOW_AUTO  ) logf(" window %d", e.departure_window);
			}
			if(  e.couple_line_id  ) {
				linehandle_t cl; cl.set_id(e.couple_line_id);
				logf(" | couple with line %d '%s' max wait %d", e.couple_line_id, cl.is_bound() ? cl->get_name() : "?", e.couple_max_wait);
			}
			logf("\n");
		}
		convoihandle_t drv;
		logf("\nCONVOYS (%d):\n", (int)line->count_convoys());
		for(  uint32 c=0;  c<line->count_convoys();  c++  ) {
			convoihandle_t cnv = line->get_convoy(c);
			if(  cnv->get_vehicle_count()==0  ) continue;
			if(  !drv.is_bound()  &&  !cnv->in_depot()  ) drv = cnv;
			cbuffer_t wb; cnv->append_wait_reason( wb );
			logf("  %d '%s' state %s, %d vehicles, %d tiles long, max %d km/h, needs wire %d; front %s rear %s; going to entry %d; %s\n",
				cnv.get_id(), cnv->get_name(), state_name(cnv->get_state()), (int)cnv->get_vehicle_count(), (int)cnv->get_tile_length(),
				speed_to_kmh(cnv->get_min_top_speed()), (int)cnv->needs_electrification(), cnv->front()->get_pos().get_str(), cnv->back()->get_pos().get_str(),
				(int)cnv->get_schedule()->get_current_stop(), (const char *)wb);
		}
		{
			char key[32] = ""; sscanf(line->get_name(), "%31s", key);
			FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
				if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=track_wt  ) continue;
				const schedule_t *cs = cnv->get_schedule();
				bool shares = false;
				if(  cs  ) for(  uint8 i=0;  i<cs->get_count()  &&  !shares;  i++  ) for(  uint8 j=0;  j<sch->get_count();  j++  ) if(  cs->entries[i].pos==sch->entries[j].pos  ) { shares = true; break; }
				if(  strstr(cnv->get_name(), name)  ||  (key[0]  &&  strstr(cnv->get_name(), key))  ||  shares  ) {
					cbuffer_t wb; cnv->append_wait_reason( wb );
					linehandle_t cl = cnv->get_line();
					logf("  related convoy %d '%s' line '%s' state %s, %d vehicles, %d tiles, %d km/h, needs wire %d, front %s%s\n", cnv.get_id(), cnv->get_name(), cl.is_bound() ? cl->get_name() : "-",
						state_name(cnv->get_state()), (int)cnv->get_vehicle_count(), (int)cnv->get_tile_length(), speed_to_kmh(cnv->get_min_top_speed()), (int)cnv->needs_electrification(), cnv->front()->get_pos().get_str(), cnv->in_depot() ? " (in depot)" : "");
				}
				if(  !drv.is_bound()  &&  !cnv->needs_electrification()  &&  !cnv->in_depot()  &&  cnv->get_owner()==owner  ) drv = cnv;
			}
		}
		if(  !drv.is_bound()  ) { logf("no convoy outside a depot to route with\n"); continue; }

		// the whole cycle as one route, legs remembered
		route_t full;
		std::vector<int> leg_start;
		std::vector<int> leg_entry;
		for(  uint8 i=0;  i<sch->get_count();  i++  ) {
			const uint8 j = (i+1) % sch->get_count();
			route_t leg;
			const koord3d from = full.get_count()>0 ? full.back() : sch->entries[i].pos;
			if(  leg.calc_route( W, from, sch->entries[j].pos, drv->front(), speed_to_kmh(drv->get_min_top_speed()), 8888 )==route_t::no_route  ) {
				logf("NO ROUTE from entry %d to %d\n", i, j);
				continue;
			}
			leg_start.push_back( (int)full.get_count() );
			leg_entry.push_back( i );
			for(  uint32 k=(full.get_count()>0  &&  full.back()==leg.at(0)) ? 1 : 0;  k<leg.get_count();  k++  ) full.append( leg.at(k) );
		}
		std::map<koord3d,int,ll_less> on_route;
		for(  uint32 k=0;  k<full.get_count();  k++  ) if(  !on_route.count(full.at(k))  ) on_route[full.at(k)] = (int)k;
		{
			logf("\nOTHER TRAINS whose current route or position uses this line's tracks (tiles shared):\n");
			FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
				if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=track_wt  ) continue;
				int shared = 0, on = 0;
				const route_t *r = cnv->get_route();
				for(  uint32 k=0;  k<r->get_count();  k++  ) if(  on_route.count(r->at(k))  ) shared++;
				for(  uint8 v=0;  v<cnv->get_vehicle_count();  v++  ) if(  on_route.count(cnv->get_vehikel(v)->get_pos())  ) on++;
				if(  shared<5  &&  on==0  ) continue;
				linehandle_t cl = cnv->get_line();
				cbuffer_t wb; cnv->append_wait_reason( wb );
				logf("  %d '%s' line '%s': %d route tiles shared, %d vehicles on it now; state %s front %s; %s\n", cnv.get_id(), cnv->get_name(), cl.is_bound() ? cl->get_name() : "-",
					shared, on, state_name(cnv->get_state()), cnv->front()->get_pos().get_str(), (const char *)wb);
			}
		}
		logf("\nROUTE: %d tiles for the whole cycle (as routed now, ignoring reservations, by convoy %d)\n", (int)full.get_count(), drv.get_id());

		size_t leg = 0;
		std::string cur_halt;
		int halt_from = -1, plain = 0;
		std::string last_way;
		for(  uint32 k=0;  k<full.get_count();  k++  ) {
			if(  leg<leg_start.size()  &&  (int)k==leg_start[leg]  ) {
				if(  plain  ) { logf("        ... %d plain track tiles\n", plain); plain = 0; }
				const uint8 i = leg_entry[leg], j = (i+1) % sch->get_count();
				halthandle_t a = haltestelle_t::get_halt(sch->entries[i].pos, owner), b = haltestelle_t::get_halt(sch->entries[j].pos, owner);
				logf("\n---- LEG entry %d %s -> entry %d %s ----\n", i, a.is_bound() ? a->get_name() : "(waypoint)", j, b.is_bound() ? b->get_name() : "(waypoint)");
				leg++;
			}
			const grund_t *gr = W->lookup(full.at(k));
			if(  !gr  ) { logf("  #%d %s NO GROUND\n", k, full.at(k).get_str()); continue; }
			ribi_t::ribi in = k>0 ? ribi_type(full.at(k-1), full.at(k)) : 0;
			ribi_t::ribi out = k+1<full.get_count() ? ribi_type(full.at(k), full.at(k+1)) : 0;
			if(  k+1<full.get_count()  &&  full.at(k+1)==full.at(k)  ) out = 0;
			std::vector<std::string> notes;
			// halt runs
			std::string hn = gr->get_halt().is_bound() ? gr->get_halt()->get_name() : "";
			if(  hn!=cur_halt  ) {
				if(  !cur_halt.empty()  ) { char s[300]; sprintf(s, "end of platform '%s' (%d tiles from #%d)", cur_halt.c_str(), (int)k-halt_from, halt_from); notes.push_back(s); }
				if(  !hn.empty()  ) { char s[300]; sprintf(s, "PLATFORM '%s' starts", hn.c_str()); notes.push_back(s); halt_from = (int)k; }
				cur_halt = hn;
			}
			for(  uint8 i=0;  i<sch->get_count();  i++  ) if(  sch->entries[i].pos==full.at(k)  ) { char s[64]; sprintf(s, "SCHEDULE ENTRY %d tile", i); notes.push_back(s); }
			char sg[300];
			if(  ll_sign(gr, out ? out : in, sg)  ) notes.push_back( std::string("SIGN ")+sg );
			if(  gr->get_depot()  ) notes.push_back("depot");
			if(  gr->has_two_ways()  ) notes.push_back("crossing");
			char wd[160]; ll_way(gr, wd);
			if(  last_way!=wd  ) { notes.push_back( std::string("way: ")+wd ); last_way = wd; }
			const schiene_t *sch_w = (const schiene_t *)gr->get_weg(track_wt);
			const ribi_t::ribi rb = sch_w ? sch_w->get_ribi_unmasked() : 0;
			if(  sch_w  &&  sch_w->get_ribi_maske()  ) { char m[8]; ll_dirs(sch_w->get_ribi_maske(), m); notes.push_back( std::string("one-way mask ")+m ); }
			const ribi_t::ribi other = rb & ~(out|ribi_t::backward(in));
			std::vector<std::string> branch_lines;
			if(  in  &&  out  &&  in==ribi_t::backward(out)  ) notes.push_back("REVERSES here");
			for(  int q=0;  q<4;  q++  ) {
				const ribi_t::ribi d = ll_nesw[q];
				if(  !(other & d)  ) continue;
				ll_branch_t b;
				ll_walk_branch(gr, d, on_route, b, branch_max);
				char s[600];
				if(  b.tiles==0  ) sprintf(s, "SWITCH: branch %s: crossover, straight onto the route", ll_dir(d));
				else sprintf(s, "SWITCH: branch %s: %d tiles", ll_dir(d), b.tiles);
				std::string t = s;
				if(  b.rejoin_min>=0  ) {
					if(  b.rejoin_min==b.rejoin_max  ) sprintf(s, ", rejoins route at #%d %s", b.rejoin_min, full.at(b.rejoin_min).get_str());
					else sprintf(s, ", rejoins route at #%d..#%d", b.rejoin_min, b.rejoin_max);
					t += s;
					if(  b.rejoin_max<(int)k  &&  b.rejoin_min<(int)k  ) t += " (loop seen from its other end)";
				}
				if(  b.too_far  ) { sprintf(s, ", goes on beyond %d tiles (other line)", branch_max); t += s; }
				if(  b.dead_end  ) t += ", has buffer stop(s)";
				branch_lines.push_back(t);
				if(  b.rejoin_max<(int)k  &&  b.rejoin_min>=0  &&  !b.too_far  ) continue;   // already listed at the first end
				const int lim = b.too_far ? 30 : 100000;
				if(  b.too_far  ) branch_lines.push_back( "    (first 30 tiles of it:)" );
				for(  size_t z=0;  z<b.halts.size();  z++  ) if(  b.halt_dist[z]<=lim  ) branch_lines.push_back( "    halt "+b.halts[z] );
				for(  size_t z=0;  z<b.signs.size();  z++  ) if(  b.sign_dist[z]<=lim  ) branch_lines.push_back( "    sign "+b.signs[z] );
				for(  size_t z=0;  z<b.notes.size();  z++  ) if(  b.note_dist[z]<=lim  ) branch_lines.push_back( "    "+b.notes[z] );
			}
			if(  notes.empty()  &&  branch_lines.empty()  ) { plain++; continue; }
			if(  plain  ) { logf("        ... %d plain track tiles\n", plain); plain = 0; }
			char ds[8]; ll_dirs(rb, ds);
			logf("  #%-5d %-16s %s->%s track %s", k, full.at(k).get_str(), in ? ll_dir(ribi_t::backward(in)) : "-", out ? ll_dir(out) : "-", ds);
			for(  size_t z=0;  z<notes.size();  z++  ) logf(" | %s", notes[z].c_str());
			logf("\n");
			for(  size_t z=0;  z<branch_lines.size();  z++  ) logf("          %s\n", branch_lines[z].c_str());
		}
		if(  plain  ) logf("        ... %d plain track tiles\n", plain);
	}
}

// ---- stationmaps NAME MARGIN: ASCII maps of each station area of a line ----

static const char *ll_track_glyph(ribi_t::ribi r, bool platform)
{
	if(  platform  ) {
		if(  r==ribi_t::east  ||  r==ribi_t::west  ||  r==(ribi_t::east|ribi_t::west)  ) return "\xe2\x95\x90";   // ═
		if(  r==ribi_t::north  ||  r==ribi_t::south  ||  r==(ribi_t::north|ribi_t::south)  ) return "\xe2\x95\x91";   // ║
	}
	switch(  r  ) {
		case ribi_t::north|ribi_t::south: return "\xe2\x94\x82";   // │
		case ribi_t::east|ribi_t::west: return "\xe2\x94\x80";   // ─
		case ribi_t::north|ribi_t::east: return "\xe2\x94\x94";   // └
		case ribi_t::north|ribi_t::west: return "\xe2\x94\x98";   // ┘
		case ribi_t::south|ribi_t::east: return "\xe2\x94\x8c";   // ┌
		case ribi_t::south|ribi_t::west: return "\xe2\x94\x90";   // ┐
		case ribi_t::north|ribi_t::south|ribi_t::east: return "\xe2\x94\x9c";   // ├
		case ribi_t::north|ribi_t::south|ribi_t::west: return "\xe2\x94\xa4";   // ┤
		case ribi_t::east|ribi_t::west|ribi_t::south: return "\xe2\x94\xac";   // ┬
		case ribi_t::east|ribi_t::west|ribi_t::north: return "\xe2\x94\xb4";   // ┴
		case ribi_t::all: return "\xe2\x94\xbc";   // ┼
		case ribi_t::north: return "\xe2\x95\xb5";   // ╵
		case ribi_t::south: return "\xe2\x95\xb7";   // ╷
		case ribi_t::east: return "\xe2\x95\xb6";   // ╶
		case ribi_t::west: return "\xe2\x95\xb4";   // ╴
		default: return "?";
	}
}

static char ll_sign_letter(const roadsign_desc_t *d)
{
	if(  d->is_station_boundary()  ) return 'L';
	if(  d->is_platform_signal()  ) return 'P';
	if(  d->is_block_post()  ) return 'B';
	if(  d->is_autoblock()  ) return 'A';
	if(  d->is_end_choose_signal()  ) return 'E';
	if(  d->is_choose_sign()  ) return 'C';
	if(  d->is_pre_signal()  ) return 'V';
	if(  d->is_priority_signal()  ) return 'R';
	if(  d->is_longblock_signal()  ) return 'G';
	if(  d->is_signal_type()  ) return 'S';
	if(  d->is_single_way()  ) return 'O';
	return 'X';
}

// ---- road jam diagnosis (session 570822f1) ----

static void describe_obj(const obj_t *o, char *buf)
{
	buf[0] = 0;
	if(  !o  ) { strcpy(buf, "-"); return; }
	if(  const road_vehicle_t *v = obj_cast<road_vehicle_t>(o)  ) {
		const convoi_t *c = v->get_convoi();
		sprintf(buf, "BUS %d %s dir %d", c ? c->self.get_id() : -1, c ? state_name(c->get_state()) : "?", (int)v->get_direction());
	}
	else if(  const private_car_t *car = obj_cast<private_car_t>(o)  ) {
		sprintf(buf, "CAR %p speed %d dir %d", (const void *)car, (int)car->get_current_speed(), (int)car->get_direction());
	}
	else sprintf(buf, "OBJ typ %d", (int)o->get_typ());
}

static void road_tile_info(const koord3d &pos, char *buf)
{
	buf[0] = 0;
	const grund_t *gr = W->lookup(pos);
	if(  !gr  ) { strcpy(buf, " NOGROUND"); return; }
	const strasse_t *str = (const strasse_t *)gr->get_weg(road_wt);
	if(  !str  ) { strcpy(buf, " NOROAD"); return; }
	sprintf(buf+strlen(buf), " ribi %d/%d", (int)str->get_ribi_unmasked(), (int)str->get_ribi());
	if(  gr->ist_uebergang()  ) strcat(buf, " CROSSING");
	if(  gr->get_halt().is_bound()  ) {
		const halthandle_t h = gr->get_halt();
		sprintf(buf+strlen(buf), " HALT[%s]%s", h->get_name(), "");
	}
	if(  const roadsign_t *rs = gr->find<roadsign_t>()  ) {
		const roadsign_desc_t *d = rs->get_desc();
		sprintf(buf+strlen(buf), " SIGN %s%s%s%s dir %d state %d", d->get_name(), d->is_traffic_light() ? " TL" : "", d->is_choose_sign() ? " CHOOSE" : "", d->is_single_way() ? " ONEWAY" : "", (int)rs->get_dir(), (int)rs->get_state());
	}
	for(  uint8 k=0;  k<gr->get_top();  k++  ) {
		const obj_t *o = gr->obj_bei(k);
		if(  o->get_typ()==obj_t::road_vehicle  ||  o->get_typ()==obj_t::road_user  ) {
			char d[128]; describe_obj(o, d);
			sprintf(buf+strlen(buf), " {%s}", d);
		}
	}
}

static void cmd_roadjam(int x0, int y0, int x1, int y1)
{
	logf("ROADJAM %d,%d - %d,%d at tick %u calmin %lld\n", x0, y0, x1, y1, W->get_ticks(), (long long)W->get_calendar_minutes());
	// tiles
	for(  int y=y0;  y<=y1;  y++  ) for(  int x=x0;  x<=x1;  x++  ) {
		const planquadrat_t *pl = W->access(koord(x, y));
		if(  !pl  ) continue;
		for(  uint8 bi=0;  bi<pl->get_boden_count();  bi++  ) {
			grund_t *gr = pl->get_boden_bei(bi);
			if(  !gr  ||  !gr->get_weg(road_wt)  ) continue;
			char buf[2048];
			road_tile_info(gr->get_pos(), buf);
			logf("  T %s%s\n", gr->get_pos().get_str(), buf);
		}
	}
	// convoys
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=road_wt  ) continue;
		const koord3d p = cnv->front()->get_pos();
		if(  p.x<x0 || p.x>x1 || p.y<y0 || p.y>y1  ) continue;
		char b[256]; describe_obj(cnv->get_blocked_by(), b);
		const vehicle_t *f = cnv->front();
		const route_t *r = cnv->get_route();
		halthandle_t tgt = cnv->get_schedule() ? haltestelle_t::get_halt(cnv->get_schedule()->get_current_entry().pos, cnv->get_owner()) : halthandle_t();
		char bp[32]; strcpy(bp, cnv->get_blocked_by_pos().get_str());
		logf("C %d [%s] %s front %s dir %d speed %d len %d ridx %d/%d target %s blocked_by %s at %s overtaking %d waiting %d\n",
			cnv.get_id(), cnv->get_name(), state_name(cnv->get_state()), p.get_str(), (int)f->get_direction(), (int)cnv->get_akt_speed(), (int)cnv->get_tile_length(),
			(int)f->get_route_index(), (int)r->get_count(), tgt.is_bound() ? tgt->get_name() : "-", b, bp, (int)cnv->is_overtaking(), (int)cnv->is_waiting());
		for(  uint32 i=f->get_route_index() > 0 ? f->get_route_index()-1 : 0;  i<r->get_count()  &&  i<f->get_route_index()+6u;  i++  ) {
			char buf[2048];
			road_tile_info(r->at(i), buf);
			logf("    %s %s%s\n", i==f->get_route_index() ? ">" : " ", r->at(i).get_str(), buf);
		}
		if(  r->get_count()>0  ) logf("    route end %s\n", r->back().get_str());
	}
}
// ---- end road jam ----

static void cmd_stationmaps(const char *name, int margin)
{
	if(  margin<=0  ) margin = 6;
	linehandle_t line;
	for(  int p=0;  p<MAX_PLAYER_COUNT  &&  !line.is_bound();  p++  ) {
		player_t *pl = W->get_player(p);
		if(  !pl  ) continue;
		vector_tpl<linehandle_t> l;
		pl->simlinemgmt.get_lines(simline_t::trainline, &l);
		FOR(vector_tpl<linehandle_t>, const ln, l) if(  strstr(ln->get_name(), name)  ) { line = ln; break; }
	}
	if(  !line.is_bound()  ) { logf("no train line '%s'\n", name); return; }
	const schedule_t *sch = line->get_schedule();
	player_t *owner = line->get_owner();
	std::map<koord3d,int,ll_less> stops;
	for(  uint8 i=0;  i<sch->get_count();  i++  ) stops[sch->entries[i].pos] = i;
	vector_tpl<halthandle_t> halts;
	for(  uint8 i=0;  i<sch->get_count();  i++  ) {
		halthandle_t h = haltestelle_t::get_halt(sch->entries[i].pos, owner);
		if(  h.is_bound()  ) halts.append_unique(h);
	}
	logf("Legend: one cell = one tile, two characters: track, then a sign letter or '*' where a line stop entry lies.\n");
	logf("  track: ─ │ └ ┘ ┌ ┐ straight and curves, ├ ┤ ┬ ┴ ┼ switches, ╴╵╶╷ buffer stop/end, ═ ║ platform tile of a station,\n");
	logf("         ≡ platform tile of another halt, D depot, # more than one track level on this square\n");
	logf("  signs: P platform signal, L station boundary (LT), C choose signal, E end of choose, A autoblock, S plain signal,\n");
	logf("         B block post, V presignal, R priority signal, G long block, O one-way sign. Directions are listed under each map.\n");
	logf("  north is up, x grows to the right, y grows downwards.\n");
	FOR(vector_tpl<halthandle_t>, const h, halts) {
		sint16 x0 = 32000, y0 = 32000, x1 = -1, y1 = -1;
		int rail_tiles = 0;
		FOR(slist_tpl<haltestelle_t::tile_t>, const &t, h->get_tiles()) {
			const grund_t *gr = t.grund;
			if(  !gr->get_weg(track_wt)  ) continue;
			rail_tiles++;
			const koord k = gr->get_pos().get_2d();
			x0 = min(x0, k.x); y0 = min(y0, k.y); x1 = max(x1, k.x); y1 = max(y1, k.y);
		}
		if(  !rail_tiles  ) continue;
		x0 -= margin; y0 -= margin; x1 += margin; y1 += margin;
		if(  x1-x0>110  ) { const int c = (x0+x1)/2; x0 = c-55; x1 = c+55; }
		if(  y1-y0>70  ) { const int c = (y0+y1)/2; y0 = c-35; y1 = c+35; }
		logf("\n==== %s  (%d rail tiles, map x %d..%d, y %d..%d) ====\n", h->get_name(), rail_tiles, x0, x1, y0, y1);
		// ruler
		logf("      ");
		for(  int x=x0;  x<=x1;  x++  ) logf("%s", x%10==0 ? "| " : "  ");
		logf("\n      ");
		for(  int x=x0;  x<=x1;  x++  ) { if(  x%10==0  ) { char b[8]; sprintf(b, "%d", x); logf("%-20s", b); x += 9; } else logf("  "); }
		logf("\n");
		std::vector<std::string> legend;
		for(  int y=y0;  y<=y1;  y++  ) {
			std::string row;
			bool any = false;
			for(  int x=x0;  x<=x1;  x++  ) {
				const planquadrat_t *pl = W->access(koord(x, y));
				const grund_t *gr = NULL;
				int levels = 0;
				if(  pl  ) for(  uint8 bi=0;  bi<pl->get_boden_count();  bi++  ) {
					grund_t *g = pl->get_boden_bei(bi);
					if(  g  &&  g->get_weg(track_wt)  ) { levels++; if(  !gr  ||  g->get_halt()==h  ) gr = g; }
				}
				if(  !gr  ) { row += "  "; continue; }
				any = true;
				const schiene_t *sw = (const schiene_t *)gr->get_weg(track_wt);
				const ribi_t::ribi r = sw->get_ribi_unmasked();
				if(  gr->get_depot()  ) row += "D";
				else if(  levels>1  ) row += "#";
				else if(  gr->get_halt().is_bound()  &&  gr->get_halt()!=h  ) row += "\xe2\x89\xa1";   // ≡
				else row += ll_track_glyph(r, gr->get_halt()==h);
				const roadsign_t *rs = gr->find<signal_t>();
				if(  !rs  ) rs = gr->find<roadsign_t>();
				if(  rs  ) {
					row += ll_sign_letter(rs->get_desc());
					char ap[8] = "";
					if(  rs->applies_to(ribi_t::north)  ) strcat(ap, "N");
					if(  rs->applies_to(ribi_t::east)  ) strcat(ap, "E");
					if(  rs->applies_to(ribi_t::south)  ) strcat(ap, "S");
					if(  rs->applies_to(ribi_t::west)  ) strcat(ap, "W");
					char s[256]; sprintf(s, "  %c at %d,%d: %s '%s', applies to trains leaving %s", ll_sign_letter(rs->get_desc()), x, y, ll_sign_kind(rs->get_desc()), rs->get_desc()->get_name(), ap[0] ? ap : "-");
					legend.push_back(s);
				}
				else if(  stops.count(gr->get_pos())  ) row += "*";
				else row += " ";
				if(  sw->get_ribi_maske()  ) { char m[8]; ll_dirs(sw->get_ribi_maske(), m); char s[96]; sprintf(s, "  one-way mask %s at %d,%d", m, x, y); legend.push_back(s); }
				if(  levels>1  ) { char s[96]; sprintf(s, "  # at %d,%d: %d track levels (bridge/tunnel)", x, y, levels); legend.push_back(s); }
			}
			if(  any  ) { while(  !row.empty()  &&  row[row.size()-1]==' '  ) row.erase(row.size()-1); logf("%5d %s\n", y, row.c_str()); }
		}
		for(  size_t i=0;  i<legend.size();  i++  ) logf("%s\n", legend[i].c_str());
		logf("  lines serving this stop:");
		FOR(vector_tpl<linehandle_t>, const rl, h->registered_lines) logf(" '%s' (%d trains)", rl->get_name(), (int)rl->count_convoys());
		logf("\n");
		// stop entries of the line here
		for(  uint8 i=0;  i<sch->get_count();  i++  ) if(  haltestelle_t::get_halt(sch->entries[i].pos, owner)==h  ) logf("  * schedule entry %d at %s\n", i, sch->entries[i].pos.get_str());
	}
}


static void cmd_mergehalt(int x1, int y1, int x2, int y2)
{
	halthandle_t a = haltestelle_t::get_halt( ground(x1,y1)->get_pos(), PL );
	halthandle_t b = haltestelle_t::get_halt( ground(x2,y2)->get_pos(), PL );
	if(  !a.is_bound()  ||  !b.is_bound()  ) { logf("ERROR mergehalt: no halt\n"); failures++; return; }
	if(  a!=b  ) a->merge_halt( b );
	logf("mergehalt %s <- %d,%d\n", a->get_name(), x2, y2);
}

static void cmd_halts()
{
	FOR(vector_tpl<halthandle_t>, h, haltestelle_t::get_alle_haltestellen()) {
		const koord3d p = h->get_basis_pos3d();
		logf("HALT %d '%s' tiles %d at %s\n", h.get_id(), h->get_name(), (int)h->get_tiles().get_count(), p.get_str());
	}
}

// --- line 062 test helpers ---
static void cmd_useline(int id, const char *name)
{
	for(  int i=0;  i<MAX_PLAYER_COUNT;  i++  ) {
		player_t *pl = W->get_player(i);
		if(  !pl  ) continue;
		vector_tpl<linehandle_t> all;
		pl->simlinemgmt.get_lines( simline_t::line, &all );
		FOR(vector_tpl<linehandle_t>, l, all) {
			if(  l.get_id()==id  ) {
				lines[name] = l;
				PL = pl;
				logf("useline %d '%s' as %s, %d convoys, %d entries\n", id, l->get_name(), name, (int)l->count_convoys(), (int)l->get_schedule()->get_count());
				return;
			}
		}
	}
	logf("ERROR no line %d\n", id);
	failures++;
}

static void cmd_cnvveh(const char *pat)
{
	int n = 0;
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		const char *ln = cnv->get_line().is_bound() ? cnv->get_line()->get_name() : "";
		if(  cnv->get_vehicle_count()==0  ||  cnv->front()->get_waytype()!=track_wt  ) continue;
		if(  !strstr(cnv->get_name(), pat)  &&  !strstr(ln, pat)  ) continue;
		logf("CNV %d '%s' line '%s' len %d tiles %d power %d speed %d electric-only %d:", cnv.get_id(), cnv->get_name(), ln, (int)cnv->get_length(), (int)cnv->get_tile_length(), (int)cnv->get_sum_power(), speed_to_kmh(cnv->get_min_top_speed()), (int)cnv->needs_electrification());
		for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) logf(" %s", cnv->get_vehikel(i)->get_desc()->get_name());
		logf("\n");
		if(  ++n>=60  ) break;
	}
}

static void cmd_copyveh(int id)
{
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv.get_id()==id  ) {
			vehicles.clear();
			for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) vehicles.push_back( cnv->get_vehikel(i)->get_desc() );
			logf("copyveh %d: %d vehicles, length %d (%d tiles)\n", id, (int)vehicles.size(), (int)cnv->get_length(), (int)cnv->get_tile_length());
			return;
		}
	}
	logf("ERROR no convoy %d\n", id);
	failures++;
}

// --- assertions: a failed one is logged as FAIL and counts as a failure ------------------------

// pattern: * for every convoy, else part of the convoy's name or the whole name of its line
static bool cnv_matches(convoihandle_t cnv, const char *pat)
{
	if(  pat==NULL  ) {
		return false;
	}
	if(  !strcmp( pat, "*" )  ) {
		return true;
	}
	return strstr( cnv->get_name(), pat )!=NULL  ||  (cnv->get_line().is_bound()  &&  !strcmp( cnv->get_line()->get_name(), pat ));
}

static bool compare(sint64 value, const char *op, sint64 limit)
{
	if(  !strcmp( op, ">=" )  ) return value >= limit;
	if(  !strcmp( op, "<=" )  ) return value <= limit;
	if(  !strcmp( op, "==" )  ) return value == limit;
	if(  !strcmp( op, ">" )  ) return value > limit;
	if(  !strcmp( op, "<" )  ) return value < limit;
	logf("ERROR unknown comparison %s\n", op);
	failures++;
	return false;
}

// a counter of the run so far (see check): stops, couplings, claims, holds, follows, parks add up over the
// convoys, offstation (longest stand outside a station, minutes) and sectionwait (longest wait at a
// platform signal or station boundary, minutes) take the largest
static bool watch_value(const watch_t &w, const char *field, sint64 &value, bool &sum)
{
	sum = true;
	if(  !strcmp( field, "stops" )  ) value = w.stops_done;
	else if(  !strcmp( field, "couplings" )  ) value = w.couplings;
	else if(  !strcmp( field, "claims" )  ) value = w.claims;
	else if(  !strcmp( field, "holds" )  ) value = w.holds;
	else if(  !strcmp( field, "follows" )  ) value = w.follows;
	else if(  !strcmp( field, "parks" )  ) value = w.parks;
	else {
		sum = false;
		if(  !strcmp( field, "offstation" )  ) value = w.max_off_station;
		else if(  !strcmp( field, "sectionwait" )  ) value = w.sw_max;
		else return false;
	}
	return true;
}

// expect PAT FIELD OP N: the convoys matching PAT together; expect_each: every one of them
static void cmd_expect(const char *pat, const char *field, const char *op, int limit, bool each)
{
	if(  pat==NULL  ||  field==NULL  ||  op==NULL  ) {
		logf("ERROR expect needs PATTERN FIELD OP NUMBER\n");
		failures++;
		return;
	}
	int n = 0;
	sint64 total = 0;
	bool sum = true;
	std::string bad;
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  !cnv_matches( cnv, pat )  ) continue;
		sint64 v = 0;
		if(  !watch_value( watch[cnv.get_id()], field, v, sum )  ) {
			logf("ERROR unknown expect field %s\n", field);
			failures++;
			return;
		}
		n++;
		total = sum ? total + v : (v > total ? v : total);
		if(  each  &&  !compare( v, op, limit )  ) {
			char b[64];
			sprintf( b, " %s=%lld", cnv->get_name(), (long long)v );
			bad += b;
		}
	}
	const bool ok = n>0  &&  (each ? bad.empty() : compare( total, op, limit ));
	if(  ok  ) {
		logf("ok   expect%s %s %s %s %d (%s %lld over %d convoys)\n", each ? "_each" : "", pat, field, op, limit, sum ? "sum" : "max", (long long)total, n);
	}
	else {
		if(  n==0  ) {
			logf("FAIL expect%s %s %s %s %d: no convoy matches\n", each ? "_each" : "", pat, field, op, limit);
		}
		else if(  each  ) {
			logf("FAIL expect_each %s %s %s %d:%s\n", pat, field, op, limit, bad.c_str());
		}
		else {
			logf("FAIL expect %s %s %s %d: %s %lld over %d convoys\n", pat, field, op, limit, sum ? "sum" : "max", (long long)total, n);
		}
		failures++;
	}
}

// expect_stuck OP N: the count of the last stuck command
static void cmd_expect_stuck(const char *op, int limit)
{
	if(  last_stuck<0  ) {
		logf("FAIL expect_stuck: no stuck count yet (call stuck twice)\n");
		failures++;
	}
	else if(  op==NULL  ||  !compare( last_stuck, op, limit )  ) {
		logf("FAIL expect_stuck %s %d: %d stuck\n", op ? op : "?", limit, last_stuck);
		failures++;
	}
	else {
		logf("ok   expect_stuck %s %d (%d)\n", op, limit, last_stuck);
	}
}

// expect_state PAT STATE: some convoy matching PAT is in that state now (state_name)
// expect_wait PAT TEXT...: the wait text of some convoy matching PAT (convoy window) contains TEXT
static void cmd_expect_now(const char *pat, const std::string &what, bool wait_text)
{
	int n = 0;
	std::string seen;
	FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) {
		if(  cnv->get_vehicle_count()==0  ||  !cnv_matches( cnv, pat )  ) continue;
		n++;
		std::string have;
		if(  wait_text  ) {
			cbuffer_t wb;
			cnv->append_wait_reason( wb );
			have = (const char *)wb;
		}
		else {
			have = state_name( cnv->get_state() );
		}
		if(  wait_text ? have.find( what )!=std::string::npos : have==what  ) {
			logf("ok   expect_%s %s %s (%s)\n", wait_text ? "wait" : "state", pat, what.c_str(), cnv->get_name());
			return;
		}
		if(  seen.size()<400  ) seen += std::string(" ") + cnv->get_name() + ": " + (have.empty() ? "-" : have) + ";";
	}
	logf("FAIL expect_%s %s %s:%s\n", wait_text ? "wait" : "state", pat ? pat : "?", what.c_str(), n==0 ? " no convoy matches" : seen.c_str());
	failures++;
}

// --- the script ------------------------------------------------------------------------------

static void run_script(const std::string &path);
static std::vector<std::string> script_dirs; // the directory of each script being read (include)

static void run_command(std::vector<char *> &a)
{
	const std::string c = a[0];
	#define I(n) (a.size()>(n) ? atoi(a[n]) : 0)
	#define S(n) (a.size()>(n) ? a[n] : NULL)
	if(  c=="include"  ) {
		if(  !S(1)  ) { logf("ERROR include needs a file\n"); failures++; return; }
		std::string p = S(1);
		const bool absolute = p[0]=='/'  ||  p[0]=='\\'  ||  (p.size()>1  &&  p[1]==':');
		run_script( absolute  ||  script_dirs.empty() ? p : script_dirs.back() + p );
	}
	else if(  c=="expect"  ) cmd_expect( S(1), S(2), S(3), I(4), false );
	else if(  c=="expect_each"  ) cmd_expect( S(1), S(2), S(3), I(4), true );
	else if(  c=="expect_stuck"  ) cmd_expect_stuck( S(1), I(2) );
	else if(  c=="expect_state"  ||  c=="expect_wait"  ) {
		std::string what;
		for(  size_t i=2;  i<a.size();  i++  ) what += std::string(i>2 ? " " : "") + a[i];
		cmd_expect_now( S(1), what, c=="expect_wait" );
	}
	else if(  c=="newworld"  ) cmd_newworld( I(1), I(2) );
	else if(  c=="list"  ) cmd_list( S(1) );
	else if(  c=="waydesc"  ) {
		track_desc = way_builder_t::get_desc( S(1) );
		if(  !track_desc  ) { logf("ERROR no way %s\n", S(1)); failures++; }
	}
	else if(  c=="track"  ) cmd_track( I(1), I(2), I(3), I(4) );
	else if(  c=="wire"  ) cmd_wire( S(1), I(2), I(3), I(4), I(5) );
	else if(  c=="station"  ) cmd_station( S(1), I(2), I(3) );
	else if(  c=="stations"  ) { for(  int x=I(2);  x<=I(3);  x++  ) cmd_station( S(1), x, I(4) ); }       // stations NAME x0 x1 y
	else if(  c=="vstations"  ) { for(  int y=I(3);  y<=I(4);  y++  ) cmd_station( S(1), I(2), y ); }      // vstations NAME x y0 y1
	else if(  c=="sign"  ) cmd_sign( S(1), I(2), I(3), S(4) ? S(4) : "B" );
	else if(  c=="depot"  ) cmd_depot( S(1), I(2), I(3) );
	else if(  c=="vehicles"  ) {
		vehicles.clear();
		for(  size_t i=1;  i<a.size();  i++  ) {
			const vehicle_desc_t *d = vehicle_builder_t::get_info( a[i] );
			if(  d  ) vehicles.push_back(d);
			else { logf("ERROR no vehicle %s\n", a[i]); failures++; }
		}
	}
	else if(  c=="line"  ) { cur_sched = new train_schedule_t(); cur_line_name = S(1); cur_line_tram = false; cur_line_bus = false; }
	else if(  c=="tramline"  ) { cur_sched = new tram_schedule_t(); cur_line_name = S(1); cur_line_tram = true; cur_line_bus = false; }
	else if(  c=="busline"  ) { cur_sched = new truck_schedule_t(); cur_line_name = S(1); cur_line_tram = false; cur_line_bus = true; }
	else if(  c=="stop"  ) cmd_stop( I(1), I(2), I(3), I(4), S(5) );
	else if(  c=="couple"  ) cmd_couple( S(1), I(2) );
	else if(  c=="missed"  ) { lines[S(1)]->add_missed_coupling( I(2), I(3) ); logf("  missed coupling %s entry %d slot %d added\n", S(1), I(2), I(3)); }
	else if(  c=="takemissed"  ) { sint64 sl = -1; const bool ok = lines[S(1)]->take_missed_coupling( I(2), sl ); logf("  take missed coupling %s entry %d: %s slot %lld\n", S(1), I(2), ok ? "found" : "none", (long long)sl); }
	else if(  c=="holdline"  ) { lines[S(1)]->set_hold_marker( I(2)!=0 ); logf("holdline %s %d\n", S(1), I(2)); }
	else if(  c=="vstates"  ) env_t::show_vehicle_states = I(1);
	else if(  c=="timetable"  ) { cur_sched->entries.back().departure_interval = I(1); cur_sched->entries.back().departure_offset = I(2); }
	else if(  c=="endline"  ) cmd_endline();
	else if(  c=="convoy"  ) cmd_convoy( I(1), I(2), S(3), a.size()>4 ? I(4) : 1 );
	else if(  c=="run"  ) cmd_run( I(1), I(2) );
	else if(  c=="report_after"  ) report_after = I(1);
	else if(  c=="events"  ) log_events = S(1) && !strcmp(S(1), "on");
	else if(  c=="stopline"  ) { for(  size_t i=1;  i<a.size();  i++  ) stop_lines.push_back( atoi(a[i]) ); }
	else if(  c=="waits"  ) log_wait = S(1) && !strcmp(S(1), "on");
	else if(  c=="stops"  ) log_stops = S(1) && !strcmp(S(1), "on");
	else if(  c=="summary"  ) cmd_summary();
	else if(  c=="status"  ) check( true );
	else if(  c=="run_until_claim"  ) cmd_run_until_claim( I(1) );
	else if(  c=="route"  ) cmd_route( S(1) );
	else if(  c=="dump"  ) cmd_dump( I(1), I(2), I(3), I(4) );
	else if(  c=="signs"  ) cmd_signs();
	else if(  c=="res"  ) cmd_res( I(1), I(2), I(3), I(4) );
	else if(  c=="save"  ) { W->save( S(1), false, env_t::savegame_version_str, true ); logf("saved %s\n", S(1)); }
	else if(  c=="saveas"  ) { W->save( S(1), false, S(2), true ); logf("saved %s as %s\n", S(1), S(2)); }
	else if(  c=="load"  ) { if(  W->load( S(1) )  ) { PL = W->get_active_player(); watch.clear(); logf("loaded %s\n", S(1)); } else { logf("ERROR load %s\n", S(1)); failures++; } }
	else if(  c=="raise"  ) {   // raise x0 y0 x1 y1: raise every grid point in the box once
		for(  int y=I(2);  y<=I(4);  y++  ) for(  int x=I(1);  x<=I(3);  x++  ) {
			grund_t *gr = W->lookup_kartenboden(koord(x, y));
			const char *err = use_tool( TOOL_RAISE_LAND, "", koord3d(x, y, gr ? gr->get_hoehe() : 0) );
			if(  err  &&  *err  ) logf("raise %d,%d: %s\n", x, y, translator::translate(err));
		}
	}
	else if(  c=="heights"  ) {
		for(  int y=I(2);  y<=I(4);  y++  ) { for(  int x=I(1);  x<=I(3);  x++  ) { grund_t *gr = W->lookup_kartenboden(koord(x, y)); logf("%c", gr->get_grund_hang() ? '/' : '0'+gr->get_hoehe()%10); } logf("\n"); }
	}
	else if(  c=="snap"  ) cmd_snap( I(1), I(2), I(3), S(4) );
	else if(  c=="run_until_aspect"  ) cmd_run_until_aspect( I(1), I(2), I(3), I(4) );
	else if(  c=="pax"  ) cmd_pax( I(1), I(2), I(3), I(4), I(5) );
	else if(  c=="loads"  ) cmd_loads( S(1) );
	else if(  c=="lineedit"  ) cmd_lineedit( S(1), S(2), I(3), I(4), I(5) );
	else if(  c=="linebegin"  ) cmd_linebegin( S(1) );
	else if(  c=="linecommit"  ) cmd_linecommit( S(1) );
	else if(  c=="desync"  ) cmd_desync( S(1), I(2) );
	else if(  c=="cnvsched"  ) cmd_cnvsched( S(1), I(2) );
	else if(  c=="entries"  ) cmd_entries();
	else if(  c=="run_until"  ) cmd_run_until( S(1), S(2), I(3), a.size()>4 ? I(4) : -1 );
	else if(  c=="diag"  ) cmd_diag( S(1) );
	else if(  c=="stationmaps"  ) cmd_stationmaps( S(1), a.size()>2 ? I(2) : 0 );
	else if(  c=="linelayout"  ) cmd_linelayout( S(1), a.size()>2 ? I(2) : 0 );
	else if(  c=="tiles"  ) cmd_tiles( I(1), I(2), I(3), I(4) );
	else if(  c=="stuck"  ) cmd_stuck();
	else if(  c=="delays"  ) { FOR(vector_tpl<convoihandle_t>, const cnv, W->convoys()) { logf("  [%lld] DELAY %s delay=%d state=%d entry=%d\n", (long long)W->get_calendar_minutes(), cnv->get_name(), (int)cnv->get_departure_delay(), (int)cnv->get_state(), cnv->get_schedule() ? (int)cnv->get_schedule()->get_current_stop() : -1); } }
	else if(  c=="mergehalt"  ) cmd_mergehalt( I(1), I(2), I(3), I(4) );
	else if(  c=="halts"  ) cmd_halts();
	else if(  c=="useline"  ) cmd_useline( I(1), S(2) );
	else if(  c=="cnvveh"  ) cmd_cnvveh( S(1) );
	else if(  c=="copyveh"  ) cmd_copyveh( I(1) );
	else if(  c=="watchonly"  ) watch_filter = S(1) ? S(1) : "";
	else if(  c=="roadjam"  ) cmd_roadjam( I(1), I(2), I(3), I(4) );
	else if(  c=="rotate"  ) { W->rotate90(); logf("rotated the map\n"); }
	else if(  c=="delcnv"  ) { convoihandle_t cv = find_cnv( S(1) ); if(  cv.is_bound()  ) { cv->self_destruct(); logf("  [%lld] DELETE %s\n", (long long)W->get_calendar_minutes(), S(1)); } else { logf("ERROR no convoy %s\n", S(1)); failures++; } }
	else if(  c=="todepot"  ) { convoihandle_t cv = find_cnv( S(1) ); if(  cv.is_bound()  ) { const char *m = cv->send_to_depot( true ); logf("  [%lld] TODEPOT %s: %s\n", (long long)W->get_calendar_minutes(), S(1), m ? translator::translate(m) : "-"); } else { logf("ERROR no convoy %s\n", S(1)); failures++; } }
	else if(  c=="stoptype"  ) {   // stoptype LINE IDX TYPE: change the stop type of a line entry
		linehandle_t l = lines[S(1)];
		schedule_t *s = l->get_schedule()->copy();
		s->entries[I(2)].stop_type = stop_type_of( S(3) );
		s->finish_editing();
		l->set_schedule( s );
		l->get_owner()->simlinemgmt.update_line( l );
		logf("  [%lld] STOPTYPE %s %d %s\n", (long long)W->get_calendar_minutes(), S(1), I(2), S(3));
	}
	else if(  c=="retarget"  ) {   // retarget LINE IDX X Y: move a line entry to another tile
		linehandle_t l = lines[S(1)];
		schedule_t *s = l->get_schedule()->copy();
		grund_t *g = ground( I(3), I(4) );
		s->entries[I(2)].pos = g->get_pos();
		s->finish_editing();
		l->set_schedule( s );
		l->get_owner()->simlinemgmt.update_line( l );
		logf("  [%lld] RETARGET %s %d -> %d,%d\n", (long long)W->get_calendar_minutes(), S(1), I(2), I(3), I(4));
	}
	else if(  c=="offs"  ) {
		FOR(vector_tpl<convoihandle_t>, const cv, W->convoys()) {
			if(  cv->get_vehicle_count()==0  ||  cv->front()->get_waytype()!=road_wt  ||  !cv->is_parked_in_bay()  ) continue;
			vehicle_t *v = cv->front();
			int x1=0, y1=0, x0=0, y0=0;
			v->get_screen_offset( x1, y1, 256 );
			v->vehicle_base_t::get_screen_offset( x0, y0, 256 );
			logf("  OFFS %s dir=%d steps=%d full=(%d,%d) base=(%d,%d) diff=(%d,%d)\n", cv->get_name(), (int)v->get_direction(), (int)v->get_steps(), x1, y1, x0, y0, x1-x0, y1-y0);
		}
	}
	else if(  c=="roadstat"  ) {
		static std::map<int, koord3d> rs_last;
		int n = 0, unmoved = 0, waiting = 0, loading = 0, parked = 0, queued = 0;
		FOR(vector_tpl<convoihandle_t>, const cv, W->convoys()) {
			if(  cv->get_vehicle_count()==0  ||  cv->front()->get_waytype()!=road_wt  ||  cv->in_depot()  ) continue;
			n++;
			const koord3d p = cv->front()->get_pos();
			if(  rs_last.count(cv.get_id())  &&  rs_last[cv.get_id()]==p  ) unmoved++;
			rs_last[cv.get_id()] = p;
			if(  cv->is_waiting()  ) waiting++;
			if(  cv->get_state()==convoi_t::LOADING  ) loading++;
			if(  cv->is_parked_in_bay()  ) parked++;
			if(  cv->is_queued_for_bay()  ) queued++;
		}
		logf("  [%lld] ROADSTAT road convoys %d unmoved since last %d waiting %d loading %d parked %d queued %d\n", (long long)W->get_calendar_minutes(), n, unmoved, waiting, loading, parked, queued);
	}
	else if(  c=="remove"  ) {   // remove X Y: the remover tool on that tile
		grund_t *g = ground( I(1), I(2) );
		const char *err = g ? use_tool( TOOL_REMOVER, "", g->get_pos() ) : "no ground";
		logf("  [%lld] REMOVE %d,%d: %s\n", (long long)W->get_calendar_minutes(), I(1), I(2), err ? translator::translate(err) : "done");
	}
	else if(  c=="trybuild"  ) {   // trybuild NAME X Y: like station, but an error is expected
		grund_t *g = ground( I(2), I(3) );
		const char *err = g ? use_tool( TOOL_BUILD_STATION, S(1), g->get_pos() ) : "no ground";
		logf("  [%lld] BUILD %s %d,%d: %s\n", (long long)W->get_calendar_minutes(), S(1), I(2), I(3), err && *err ? translator::translate(err) : "done");
	}
	else if(  c=="baywatch"  ) bay_watch = S(1) && !strcmp(S(1), "on");
	else if(  c=="bays"  ) cmd_bays();
	else if(  c=="echo"  ) { for(  size_t i=1;  i<a.size();  i++  ) logf("%s ", a[i]); logf("\n"); }
	else { logf("ERROR unknown command %s\n", c.c_str()); failures++; }
	#undef I
	#undef S
}

static void run_script(const std::string &path)
{
	if(  script_dirs.size()>16  ) {
		logf("ERROR include nested too deep at %s\n", path.c_str());
		failures++;
		return;
	}
	FILE *f = fopen( path.c_str(), "r" );
	if(  !f  ) {
		logf("ERROR cannot open %s\n", path.c_str());
		failures++;
		return;
	}
	const size_t slash = path.find_last_of( "/\\" );
	script_dirs.push_back( slash==std::string::npos ? std::string() : path.substr( 0, slash+1 ) );
	char line[1024];
	while(  fgets(line, sizeof(line), f)  ) {
		char *hash = strchr(line, '#');
		if(  hash  ) *hash = 0;
		std::vector<char *> a;
		for(  char *t = strtok(line, " \t\r\n");  t;  t = strtok(NULL, " \t\r\n")  ) a.push_back(t);
		if(  !a.empty()  ) {
			run_command( a );
		}
	}
	fclose(f);
	script_dirs.pop_back();
}

int fork_test_run(karte_t *welt, const char *script)
{
	W = welt;
	PL = W->get_active_player();
	std::string logname = std::string(script) + ".log";
	LOG = fopen( logname.c_str(), "w" );
	if(  !LOG  ) {
		dbg->error( "fork_test_run()", "cannot write %s", logname.c_str() );
		return 1;
	}
	run_script( script );
	logf("failures: %d\n", failures);
	logf("RESULT %s\n", failures ? "FAIL" : "PASS");
	fclose(LOG);
	return failures;
}
