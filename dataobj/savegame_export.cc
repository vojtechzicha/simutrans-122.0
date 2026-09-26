/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "savegame_export.h"

#include "../simconst.h"
#include "../simconvoi.h"
#include "../simdebug.h"
#include "../simfab.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../simunits.h"
#include "../simlinemgmt.h"
#include "../simcity.h"
#include "../simversion.h"
#include "../simworld.h"

#include "../bauer/goods_manager.h"
#include "../boden/grund.h"
#include "../descriptor/goods_desc.h"
#include "../descriptor/vehicle_desc.h"
#include "../player/finance.h"
#include "../player/simplay.h"
#include "../vehicle/simvehicle.h"

#include "environment.h"
#include "koord.h"
#include "koord3d.h"
#include "schedule.h"
#include "settings.h"
#include "translator.h"

#include "../macros.h"
#include "../utils/simstring.h"

#include "../tpl/inthashtable_tpl.h"
#include "../tpl/minivec_tpl.h"
#include "../tpl/slist_tpl.h"
#include "../tpl/vector_tpl.h"
#include "../tpl/weighted_vector_tpl.h"


/// at most this many direct connections are written per stop, to keep the file bounded
#define EXPORT_MAX_CONNECTIONS_PER_HALT (512)


/**
 * Minimal streaming JSON writer.
 * Tracks a single "a value was just finished" flag; that is enough since every
 * container is itself a value.
 */
class json_writer_t
{
private:
	FILE *f;
	bool need_comma;

	void comma()
	{
		if(  need_comma  ) {
			fputc( ',', f );
		}
		need_comma = false;
	}

	/**
	 * Length of the complete, well-formed UTF-8 sequence starting at p, or 0 if
	 * the bytes there are not one (invalid lead byte, missing or wrong continuation).
	 * Names in the game are cut to a fixed byte length, so a string can end in the
	 * middle of a multibyte character; such a tail is dropped, since a JSON file
	 * must be valid UTF-8.
	 */
	static size_t utf8_sequence_length( const unsigned char *p )
	{
		size_t len;
		if(  *p < 0x80  ) {
			return 1;
		}
		else if(  (*p & 0xE0) == 0xC0  &&  *p >= 0xC2  ) {
			len = 2;
		}
		else if(  (*p & 0xF0) == 0xE0  ) {
			len = 3;
		}
		else if(  (*p & 0xF8) == 0xF0  &&  *p <= 0xF4  ) {
			len = 4;
		}
		else {
			return 0;
		}
		for(  size_t i = 1;  i < len;  i++  ) {
			if(  (p[i] & 0xC0) != 0x80  ) {
				return 0;
			}
		}
		return len;
	}

	void raw_string( const char *s )
	{
		fputc( '"', f );
		if(  s  ) {
			for(  const unsigned char *p = (const unsigned char *)s;  *p;  ) {
				switch(  *p  ) {
					case '"':  fputs( "\\\"", f ); p++; break;
					case '\\': fputs( "\\\\", f ); p++; break;
					case '\b': fputs( "\\b", f );  p++; break;
					case '\f': fputs( "\\f", f );  p++; break;
					case '\n': fputs( "\\n", f );  p++; break;
					case '\r': fputs( "\\r", f );  p++; break;
					case '\t': fputs( "\\t", f );  p++; break;
					default:
						if(  *p < 0x20  ||  *p == 0x7F  ) {
							fprintf( f, "\\u%04x", (unsigned)*p );
							p++;
						}
						else if(  *p < 0x80  ) {
							fputc( *p, f );
							p++;
						}
						else {
							const size_t len = utf8_sequence_length( p );
							if(  len == 0  ) {
								// broken or truncated multibyte character: skip the byte
								p++;
							}
							else {
								fwrite( p, 1, len, f );
								p += len;
							}
						}
						break;
				}
			}
		}
		fputc( '"', f );
	}

public:
	json_writer_t( FILE *file ) : f(file), need_comma(false) {}

	bool ok() const { return ferror( f ) == 0; }

	void newline() { fputc( '\n', f ); }

	void start_object() { comma(); fputc( '{', f ); need_comma = false; }
	void end_object()   { fputc( '}', f ); need_comma = true; }
	void start_array()  { comma(); fputc( '[', f ); need_comma = false; }
	void end_array()    { fputc( ']', f ); need_comma = true; }

	void key( const char *k ) { comma(); raw_string( k ); fputc( ':', f ); }

	void value_null()               { comma(); fputs( "null", f ); need_comma = true; }
	void value_bool( bool b )       { comma(); fputs( b ? "true" : "false", f ); need_comma = true; }
	void value_int( sint64 v )      { comma(); fprintf( f, "%lld", (long long)v ); need_comma = true; }
	void value_string( const char *s ) { comma(); raw_string( s ); need_comma = true; }

	void value_double( double d )
	{
		comma();
		// guard against inf/nan, which are not valid JSON
		if(  !(d > -1e300  &&  d < 1e300)  ) {
			fputs( "null", f );
		}
		else {
			fprintf( f, "%.2f", d );
		}
		need_comma = true;
	}

	/// money is stored as 1/100 credits
	void value_money( sint64 cents ) { value_double( (double)cents / 100.0 ); }

	void object_key( const char *k )  { key( k ); start_object(); }
	void array_key( const char *k )   { key( k ); start_array(); }

	void kv_null( const char *k )                 { key( k ); value_null(); }
	void kv_bool( const char *k, bool b )         { key( k ); value_bool( b ); }
	void kv_int( const char *k, sint64 v )        { key( k ); value_int( v ); }
	void kv_double( const char *k, double d )     { key( k ); value_double( d ); }
	void kv_money( const char *k, sint64 cents )  { key( k ); value_money( cents ); }

	void kv_string( const char *k, const char *s )
	{
		key( k );
		if(  s  ) {
			value_string( s );
		}
		else {
			value_null();
		}
	}

	void kv_koord( const char *k, const koord &p )
	{
		key( k );
		start_object();
		kv_int( "x", p.x );
		kv_int( "y", p.y );
		end_object();
	}

	void kv_koord3d( const char *k, const koord3d &p )
	{
		key( k );
		start_object();
		kv_int( "x", p.x );
		kv_int( "y", p.y );
		kv_int( "z", p.z );
		end_object();
	}

	void koord3d_value( const koord3d &p )
	{
		start_object();
		kv_int( "x", p.x );
		kv_int( "y", p.y );
		kv_int( "z", p.z );
		end_object();
	}
};


static const char *export_linetype_name( simline_t::linetype lt )
{
	switch(  lt  ) {
		case simline_t::truckline:      return "road";
		case simline_t::trainline:      return "rail";
		case simline_t::shipline:       return "ship";
		case simline_t::airline:        return "air";
		case simline_t::monorailline:   return "monorail";
		case simline_t::tramline:       return "tram";
		case simline_t::maglevline:     return "maglev";
		case simline_t::narrowgaugeline:return "narrowgauge";
		default:                        return "other";
	}
}


static const char *export_convoi_state_name( int state )
{
	switch(  state  ) {
		case convoi_t::INITIAL:                          return "initial";
		case convoi_t::EDIT_SCHEDULE:                    return "edit_schedule";
		case convoi_t::ROUTING_1:                        return "routing";
		case convoi_t::NO_ROUTE:                         return "no_route";
		case convoi_t::DRIVING:                          return "driving";
		case convoi_t::LOADING:                          return "loading";
		case convoi_t::WAITING_FOR_CLEARANCE:            return "waiting_for_clearance";
		case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:  return "waiting_for_clearance_one_month";
		case convoi_t::CAN_START:                        return "can_start";
		case convoi_t::CAN_START_ONE_MONTH:              return "can_start_one_month";
		case convoi_t::SELF_DESTRUCT:                    return "self_destruct";
		case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS: return "waiting_for_clearance_two_months";
		case convoi_t::CAN_START_TWO_MONTHS:             return "can_start_two_months";
		case convoi_t::LEAVING_DEPOT:                    return "leaving_depot";
		case convoi_t::ENTERING_DEPOT:                   return "entering_depot";
		default:                                         return "unknown";
	}
}


static const char *export_ai_type_name( const player_t *player )
{
	switch(  player->get_ai_id()  ) {
		case player_t::AI_GOODS:     return "goods_ai";
		case player_t::AI_PASSENGER: return "passenger_ai";
		case player_t::AI_SCRIPTED:  return "script";
		default:                     return NULL;
	}
}


/**
 * Recomputes the line status the same way simline_t::recalc_status() does,
 * but as a stable name instead of a theme colour.
 */
static const char *export_line_state_name( karte_t *welt, linehandle_t line )
{
	if(  line->get_finance_history( 0, LINE_CONVOIS ) == 0  ) {
		return "no_convoys";
	}
	if(  line->get_finance_history( 0, LINE_PROFIT ) < 0  ) {
		return "loss";
	}
	if(  (line->get_finance_history( 0, LINE_OPERATIONS ) | line->get_finance_history( 1, LINE_OPERATIONS )) == 0  ) {
		return "nothing_moved";
	}
	if(  welt->use_timeline()  ) {
		FOR( vector_tpl<convoihandle_t>, const cnv, line->get_convoys() ) {
			if(  cnv.is_bound()  &&  cnv->has_obsolete_vehicles()  ) {
				return "obsolete";
			}
		}
	}
	return "ok";
}


static void export_schedule( json_writer_t &w, const schedule_t *schedule, player_t *owner )
{
	if(  schedule == NULL  ) {
		w.value_null();
		return;
	}
	w.start_object();
	w.kv_int( "current_stop", schedule->get_current_stop() );
	// 122.0 has no bidirectional/mirrored schedules
	w.kv_null( "bidirectional" );
	w.kv_null( "mirrored" );
	w.array_key( "entries" );
	FOR( minivec_tpl<schedule_entry_t>, const &entry, schedule->entries ) {
		w.start_object();
		w.kv_koord3d( "pos", entry.pos );
		halthandle_t halt = haltestelle_t::get_halt( entry.pos, owner );
		if(  halt.is_bound()  ) {
			w.kv_int( "halt_id", halt.get_id() );
		}
		else {
			w.kv_null( "halt_id" );
		}
		w.kv_int( "minimum_loading", entry.minimum_loading );
		w.kv_int( "waiting_time", entry.waiting_time_shift );
		w.kv_int( "waiting_time_minutes", entry.waiting_time );
		w.kv_int( "departure_interval", entry.departure_interval );
		w.kv_int( "departure_offset", entry.departure_offset );
		w.array_key( "departure_offsets" );
		{
			uint16 offsets[schedule_entry_t::MAX_EXTRA_OFFSETS + 1];
			const uint8 n = entry.get_departure_offsets( offsets );
			for(  uint8 k=0;  k<n;  k++  ) {
				w.value_int( offsets[k] );
			}
		}
		w.end_array();
		w.kv_int( "stop_type", entry.stop_type );
		w.end_object();
	}
	w.end_array();
	w.end_object();
}


static void export_meta( json_writer_t &w, karte_t *welt, const char *save_name )
{
	w.object_key( "meta" );
	w.kv_string( "save_file", save_name );
	w.kv_string( "game_version", SAVEGAME_VER_NR );
	w.kv_string( "program_version", VERSION_NUMBER );

	// strip the trailing slash the pakset directory name carries
	char pakset[256];
	tstrncpy( pakset, env_t::objfilename.c_str(), lengthof( pakset ) );
	size_t const plen = strlen( pakset );
	if(  plen > 0  &&  pakset[plen-1] == '/'  ) {
		pakset[plen-1] = 0;
	}
	w.kv_string( "pakset", pakset );

	char stamp[32];
	time_t const now = time( NULL );
	struct tm utc;
#ifdef _WIN32
	gmtime_s( &utc, &now );
#else
	gmtime_r( &now, &utc );
#endif
	strftime( stamp, lengthof( stamp ), "%Y-%m-%dT%H:%M:%SZ", &utc );
	w.kv_string( "exported_at", stamp );

	w.object_key( "map" );
	w.kv_int( "width", welt->get_size().x );
	w.kv_int( "height", welt->get_size().y );
	w.end_object();

	w.object_key( "date" );
	w.kv_int( "year", welt->get_last_year() );
	w.kv_int( "month", welt->get_last_month() + 1 );
	w.kv_int( "ticks", welt->get_ticks() );
	w.kv_int( "absolute_month", welt->get_current_month() );
	if(  welt->has_calendar()  ) {
		// world calendar date (fork): what the status bar clock shows
		const karte_t::calendar_date_t date = welt->get_calendar_date( welt->get_calendar_minutes() );
		w.object_key( "calendar" );
		w.kv_int( "year", date.year );
		w.kv_int( "month", date.month + 1 );
		w.kv_int( "day", date.day );
		w.kv_int( "weekday", date.weekday + 1 );
		w.kv_int( "hour", date.hour );
		w.kv_int( "minute", date.minute );
		w.end_object();
	}
	w.end_object();

	int player_count = 0;
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		if(  welt->get_player( i ) != NULL  ) {
			player_count++;
		}
	}
	w.kv_int( "player_count", player_count );
	w.end_object();
}


static void export_settings( json_writer_t &w, karte_t *welt )
{
	settings_t const &s = welt->get_settings();

	w.object_key( "settings" );
	w.kv_int( "starting_year", s.get_starting_year() );
	w.kv_int( "starting_month", s.get_starting_month() );
	w.kv_int( "bits_per_month", s.get_bits_per_month() );
	w.kv_int( "minutes_per_month", s.get_minutes_per_month() );
	w.kv_bool( "calendar_seasons", s.get_calendar_seasons() );
	w.kv_int( "use_timeline", s.get_use_timeline() );
	w.kv_bool( "freeplay", s.is_freeplay() );
	w.kv_money( "starting_money", s.get_starting_money( s.get_starting_year() ) );
	w.kv_int( "pay_for_total_distance", s.get_pay_for_total_distance_mode() );
	w.kv_int( "factory_worker_percentage", s.get_factory_worker_percentage() );
	w.kv_int( "factory_worker_radius", s.get_factory_worker_radius() );
	w.kv_int( "tourist_percentage", s.get_tourist_percentage() );
	w.kv_int( "passenger_factor", s.get_passenger_factor() );
	w.kv_int( "passenger_multiplier", s.get_passenger_multiplier() );
	w.kv_int( "mail_multiplier", s.get_mail_multiplier() );
	w.kv_int( "goods_multiplier", s.get_goods_multiplier() );
	w.kv_int( "electricity_multiplier", s.get_electricity_multiplier() );
	w.kv_int( "max_route_steps", s.get_max_route_steps() );
	w.kv_int( "max_choose_route_steps", s.get_max_choose_route_steps() );
	w.kv_int( "max_transfers", s.get_max_transfers() );
	w.kv_int( "max_hops", s.get_max_hops() );
	w.kv_int( "traffic_level", s.get_traffic_level() );
	w.kv_bool( "separate_halt_capacities", s.is_separate_halt_capacities() );
	w.kv_bool( "avoid_overcrowding", s.is_avoid_overcrowding() );
	w.kv_bool( "no_routing_over_overcrowding", s.is_no_routing_over_overcrowding() );
	w.kv_int( "station_coverage", s.get_station_coverage() );
	w.kv_bool( "allow_buying_obsolete_vehicles", s.get_allow_buying_obsolete_vehicles() );
	w.kv_int( "just_in_time", s.get_just_in_time() );
	w.kv_int( "industry_increase_every", s.get_industry_increase_every() );
	w.kv_int( "minimum_city_distance", s.get_minimum_city_distance() );
	w.kv_int( "groundwater", s.get_groundwater() );
	w.kv_int( "way_toll_runningcost_percentage", s.get_way_toll_runningcost_percentage() );
	w.kv_int( "way_toll_waycost_percentage", s.get_way_toll_waycost_percentage() );
	w.kv_int( "bonus_basefactor", s.get_bonus_basefactor() );
	w.kv_money( "maintenance_building", s.maint_building );
	w.kv_bool( "numbered_stations", s.get_numbered_stations() );
	w.kv_bool( "beginner_mode", s.get_beginner_mode() );
	w.kv_int( "beginner_price_factor", s.get_beginner_price_factor() );
	w.kv_int( "city_count", s.get_city_count() );
	w.kv_int( "factory_count", s.get_factory_count() );
	w.kv_int( "tourist_attractions", s.get_tourist_attractions() );
	w.kv_int( "mean_citizen_count", s.get_mean_citizen_count() );
	w.kv_int( "crossconnect_factor", s.get_crossconnect_factor() );
	w.kv_int( "factory_maximum_intransit_percentage", s.get_factory_maximum_intransit_percentage() );
	w.kv_bool( "factory_enforce_demand", s.get_factory_enforce_demand() );
	w.kv_int( "pak_diagonal_multiplier", s.get_pak_diagonal_multiplier() );
	w.kv_int( "way_height_clearance", s.get_way_height_clearance() );
	w.kv_int( "used_vehicle_reduction", s.get_used_vehicle_reduction() );
	w.kv_int( "stadtauto_duration", s.get_stadtauto_duration() );
	w.kv_bool( "with_private_paks", s.get_with_private_paks() );
	w.kv_int( "rotation", s.get_rotation() );
	w.kv_money( "cst_multiply_dock", s.cst_multiply_dock );
	w.kv_money( "cst_multiply_station", s.cst_multiply_station );
	w.kv_money( "cst_multiply_roadstop", s.cst_multiply_roadstop );
	w.kv_money( "cst_multiply_airterminal", s.cst_multiply_airterminal );
	w.kv_money( "cst_multiply_post", s.cst_multiply_post );
	w.kv_money( "cst_multiply_headquarter", s.cst_multiply_headquarter );
	w.kv_money( "cst_depot_rail", s.cst_depot_rail );
	w.kv_money( "cst_depot_road", s.cst_depot_road );
	w.kv_money( "cst_depot_ship", s.cst_depot_ship );
	w.kv_money( "cst_depot_air", s.cst_depot_air );
	w.kv_money( "cst_buy_land", s.cst_buy_land );
	w.kv_money( "cst_alter_land", s.cst_alter_land );
	w.kv_money( "cst_set_slope", s.cst_set_slope );
	w.kv_money( "cst_found_city", s.cst_found_city );
	w.kv_money( "cst_multiply_found_industry", s.cst_multiply_found_industry );
	w.kv_money( "cst_remove_tree", s.cst_remove_tree );
	w.kv_money( "cst_multiply_remove_haus", s.cst_multiply_remove_haus );
	w.kv_money( "cst_transformer", s.cst_transformer );
	w.kv_money( "cst_maintain_transformer", s.cst_maintain_transformer );
	w.kv_string( "language", env_t::language_iso );
	w.end_object();
}


static void export_goods( json_writer_t &w )
{
	w.array_key( "goods" );
	for(  uint8 i = 0;  i < goods_manager_t::get_count();  i++  ) {
		const goods_desc_t *ware = goods_manager_t::get_info( i );
		if(  ware == NULL  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_int( "index", ware->get_index() );
		w.kv_string( "name", ware->get_name() );
		w.kv_int( "catg", ware->get_catg() );
		w.kv_int( "catg_index", ware->get_catg_index() );
		// special freight (catg 0) has one category per good; the game shows the good's name for it
		const char *catg_name = ware->get_catg() == 0 ? ware->get_name() : ware->get_catg_name();
		w.kv_string( "catg_name", catg_name );
		// names as the game shows them in its current language (the keys above stay the pak ids)
		w.kv_string( "label", translator::translate( ware->get_name() ) );
		w.kv_string( "catg_label", translator::translate( catg_name ) );
		w.kv_int( "speed_bonus", ware->get_speed_bonus() );
		w.kv_int( "weight_per_unit", ware->get_weight_per_unit() );
		w.kv_int( "value", ware->get_value() );
		w.end_object();
	}
	w.end_array();
}


static void export_players( json_writer_t &w, karte_t *welt )
{
	// count convoys and halts per player first
	sint32 convoy_count[MAX_PLAYER_COUNT];
	sint32 halt_count[MAX_PLAYER_COUNT];
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		convoy_count[i] = 0;
		halt_count[i] = 0;
	}
	FOR( vector_tpl<convoihandle_t>, const cnv, welt->convoys() ) {
		if(  cnv.is_bound()  &&  cnv->get_owner()  ) {
			const sint8 nr = cnv->get_owner()->get_player_nr();
			if(  nr >= 0  &&  nr < MAX_PLAYER_COUNT  ) {
				convoy_count[nr]++;
			}
		}
	}
	FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
		if(  halt.is_bound()  &&  halt->get_owner()  ) {
			const sint8 nr = halt->get_owner()->get_player_nr();
			if(  nr >= 0  &&  nr < MAX_PLAYER_COUNT  ) {
				halt_count[nr]++;
			}
		}
	}

	w.array_key( "players" );
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		player_t *player = welt->get_player( i );
		if(  player == NULL  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_int( "id", i );
		w.kv_string( "name", player->get_name() );
		w.kv_bool( "is_human", player->get_ai_id() == player_t::HUMAN );
		w.kv_string( "ai_type", export_ai_type_name( player ) );
		w.kv_bool( "active", player->is_active() );
		w.kv_bool( "locked", player->is_locked() );
		w.kv_int( "color1", player->get_player_color1() );
		w.kv_int( "color2", player->get_player_color2() );

		finance_t *finance = player->get_finance();
		if(  finance  ) {
			w.kv_money( "cash", finance->get_account_balance() );
			w.kv_money( "net_wealth", finance->get_netwealth() );
			w.object_key( "finance" );
			w.kv_money( "revenue_this_month", finance->get_history_veh_month( TT_ALL, 0, ATV_REVENUE ) );
			w.kv_money( "profit_this_month", finance->get_history_veh_month( TT_ALL, 0, ATV_PROFIT ) );
			w.kv_money( "operating_profit_this_month", finance->get_history_veh_month( TT_ALL, 0, ATV_OPERATING_PROFIT ) );
			w.kv_money( "revenue_last_year", finance->get_history_veh_year( TT_ALL, 1, ATV_REVENUE ) );
			w.kv_money( "profit_last_year", finance->get_history_veh_year( TT_ALL, 1, ATV_PROFIT ) );
			w.kv_money( "maintenance", finance->get_maintenance( TT_ALL ) );
			w.kv_money( "assets", finance->get_history_veh_month( TT_ALL, 0, ATV_NON_FINANCIAL_ASSETS ) );
			w.kv_int( "transported_this_month", finance->get_history_veh_month( TT_ALL, 0, ATV_TRANSPORTED ) );
			w.end_object();
		}
		else {
			w.kv_null( "cash" );
			w.kv_null( "net_wealth" );
			w.kv_null( "finance" );
		}

		const koord hq = player->get_headquarter_pos();
		if(  hq == koord::invalid  ) {
			w.kv_null( "headquarters" );
		}
		else {
			w.kv_koord( "headquarters", hq );
		}

		w.kv_int( "convoy_count", convoy_count[i] );
		w.kv_int( "line_count", player->simlinemgmt.get_line_count() );
		w.kv_int( "halt_count", halt_count[i] );
		w.end_object();
	}
	w.end_array();
}


static void export_lines( json_writer_t &w, karte_t *welt )
{
	w.array_key( "lines" );
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		player_t *player = welt->get_player( i );
		if(  player == NULL  ) {
			continue;
		}
		vector_tpl<linehandle_t> lines;
		player->simlinemgmt.get_lines( simline_t::line, &lines );
		FOR( vector_tpl<linehandle_t>, const line, lines ) {
			if(  !line.is_bound()  ) {
				continue;
			}
			w.newline();
			w.start_object();
			w.kv_int( "id", line.get_id() );
			w.kv_string( "name", line->get_name() );
			w.kv_int( "owner_id", i );
			w.kv_string( "type", export_linetype_name( line->get_linetype() ) );
			w.kv_string( "state", export_line_state_name( welt, line ) );
			w.kv_bool( "withdraw", line->get_withdraw() );

			w.array_key( "goods_categories" );
			FOR( minivec_tpl<uint8>, const catg, line->get_goods_catg_index() ) {
				w.value_int( catg );
			}
			w.end_array();

			w.array_key( "convoy_ids" );
			FOR( vector_tpl<convoihandle_t>, const cnv, line->get_convoys() ) {
				if(  cnv.is_bound()  ) {
					w.value_int( cnv.get_id() );
				}
			}
			w.end_array();

			w.key( "schedule" );
			export_schedule( w, line->get_schedule(), player );

			w.object_key( "stats" );
			static const char *const line_stat_key[MAX_LINE_COST] = {
				"capacity", "transported", "convoys", "revenue",
				"operations", "profit", "distance", "maxspeed", "waytoll"
			};
			static const bool line_stat_money[MAX_LINE_COST] = {
				false, false, false, true, true, true, false, false, true
			};
			for(  int cost = 0;  cost < MAX_LINE_COST;  cost++  ) {
				w.array_key( line_stat_key[cost] );
				for(  int month = 0;  month < MAX_MONTHS;  month++  ) {
					if(  line_stat_money[cost]  ) {
						w.value_money( line->get_finance_history( month, cost ) );
					}
					else {
						w.value_int( line->get_finance_history( month, cost ) );
					}
				}
				w.end_array();
			}
			w.end_object();

			w.end_object();
		}
	}
	w.end_array();
}


// halt id -> ids of the lines whose schedule stops there.
// registered_lines on a halt only knows lines that have convoys (simline_t::finish_rd),
// so lines without convoys would otherwise be missing from the stop side.
// vector_tpl cannot be copied, so the table holds pointers; free_schedule_lines() releases them
typedef inthashtable_tpl<uint16, vector_tpl<uint16>*> halt_lines_map_t;

static void collect_schedule_lines( karte_t *welt, halt_lines_map_t &halt_lines )
{
	for(  int i = 0;  i < MAX_PLAYER_COUNT;  i++  ) {
		player_t *player = welt->get_player( i );
		if(  player == NULL  ) {
			continue;
		}
		vector_tpl<linehandle_t> lines;
		player->simlinemgmt.get_lines( simline_t::line, &lines );
		FOR( vector_tpl<linehandle_t>, const line, lines ) {
			if(  !line.is_bound()  ||  line->get_schedule() == NULL  ) {
				continue;
			}
			FOR( minivec_tpl<schedule_entry_t>, const &entry, line->get_schedule()->entries ) {
				halthandle_t halt = haltestelle_t::get_halt( entry.pos, player );
				if(  !halt.is_bound()  ) {
					continue;
				}
				vector_tpl<uint16> *ids = halt_lines.get( halt.get_id() );
				if(  ids == NULL  ) {
					ids = new vector_tpl<uint16>();
					halt_lines.put( halt.get_id(), ids );
				}
				ids->append_unique( line.get_id() );
			}
		}
	}
}


static void free_schedule_lines( halt_lines_map_t &halt_lines )
{
	FOR( halt_lines_map_t, const &entry, halt_lines ) {
		delete entry.value;
	}
	halt_lines.clear();
}


static void export_stops( json_writer_t &w, const halt_lines_map_t &halt_lines )
{
	const uint8 max_catg = goods_manager_t::get_max_catg_index();

	w.array_key( "stops" );
	FOR( vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen() ) {
		if(  !halt.is_bound()  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_int( "id", halt.get_id() );
		w.kv_string( "name", halt->get_name() );
		if(  halt->get_owner()  ) {
			w.kv_int( "owner_id", halt->get_owner()->get_player_nr() );
		}
		else {
			w.kv_null( "owner_id" );
		}
		w.kv_koord3d( "pos", halt->get_basis_pos3d() );

		w.array_key( "tiles" );
		FOR( slist_tpl<haltestelle_t::tile_t>, const &tile, halt->get_tiles() ) {
			if(  tile.grund  ) {
				w.koord3d_value( tile.grund->get_pos() );
			}
		}
		w.end_array();

		const haltestelle_t::stationtyp st = halt->get_station_type();
		w.array_key( "station_types" );
		if(  st & haltestelle_t::railstation  )     { w.value_string( "rail" ); }
		if(  st & haltestelle_t::busstop  )         { w.value_string( "bus" ); }
		if(  st & haltestelle_t::loadingbay  )      { w.value_string( "truck" ); }
		if(  st & haltestelle_t::dock  )            { w.value_string( "ship" ); }
		if(  st & haltestelle_t::airstop  )         { w.value_string( "air" ); }
		if(  st & haltestelle_t::monorailstop  )    { w.value_string( "monorail" ); }
		if(  st & haltestelle_t::tramstop  )        { w.value_string( "tram" ); }
		if(  st & haltestelle_t::maglevstop  )      { w.value_string( "maglev" ); }
		if(  st & haltestelle_t::narrowgaugestop  ) { w.value_string( "narrowgauge" ); }
		w.end_array();

		w.object_key( "enabled" );
		w.kv_bool( "passengers", halt->get_pax_enabled() );
		w.kv_bool( "mail", halt->get_mail_enabled() );
		w.kv_bool( "goods", halt->get_ware_enabled() );
		w.end_object();

		w.object_key( "capacity" );
		w.kv_int( "passengers", halt->get_capacity( 0 ) );
		w.kv_int( "mail", halt->get_capacity( 1 ) );
		w.kv_int( "goods", halt->get_capacity( 2 ) );
		w.end_object();

		sint64 waiting_pax = 0;
		sint64 waiting_mail = 0;
		sint64 waiting_goods = 0;
		w.array_key( "waiting" );
		for(  uint8 i = 0;  i < goods_manager_t::get_count();  i++  ) {
			const goods_desc_t *ware = goods_manager_t::get_info( i );
			if(  ware == NULL  ||  ware == goods_manager_t::none  ) {
				continue;
			}
			const uint32 amount = halt->get_ware_summe( ware );
			if(  amount == 0  ) {
				continue;
			}
			if(  ware->get_catg_index() == goods_manager_t::INDEX_PAS  ) {
				waiting_pax += amount;
			}
			else if(  ware->get_catg_index() == goods_manager_t::INDEX_MAIL  ) {
				waiting_mail += amount;
			}
			else {
				waiting_goods += amount;
			}
			w.start_object();
			w.kv_int( "goods_index", ware->get_index() );
			w.kv_string( "goods_name", ware->get_name() );
			w.kv_int( "catg_index", ware->get_catg_index() );
			w.kv_int( "amount", amount );
			w.end_object();
		}
		w.end_array();

		w.object_key( "waiting_total" );
		w.kv_int( "passengers", waiting_pax );
		w.kv_int( "mail", waiting_mail );
		w.kv_int( "goods", waiting_goods );
		w.end_object();

		w.object_key( "pax" );
		w.kv_int( "happy", halt->get_pax_happy() );
		w.kv_int( "unhappy", halt->get_pax_unhappy() );
		w.kv_int( "no_route", halt->get_pax_no_route() );
		w.end_object();

		vector_tpl<uint16> line_ids;
		FOR( vector_tpl<linehandle_t>, const line, halt->registered_lines ) {
			if(  line.is_bound()  ) {
				line_ids.append_unique( line.get_id() );
			}
		}
		if(  const vector_tpl<uint16> *scheduled = halt_lines.get( halt.get_id() )  ) {
			FOR( vector_tpl<uint16>, const id, *scheduled ) {
				line_ids.append_unique( id );
			}
		}
		w.array_key( "line_ids" );
		FOR( vector_tpl<uint16>, const id, line_ids ) {
			w.value_int( id );
		}
		w.end_array();

		w.array_key( "convoy_ids" );
		FOR( vector_tpl<convoihandle_t>, const cnv, halt->registered_convoys ) {
			if(  cnv.is_bound()  ) {
				w.value_int( cnv.get_id() );
			}
		}
		w.end_array();

		uint32 written_connections = 0;
		w.array_key( "connections" );
		for(  uint8 catg = 0;  catg < max_catg;  catg++  ) {
			FOR( vector_tpl<haltestelle_t::connection_t>, const &conn, halt->get_connections( catg ) ) {
				if(  written_connections >= EXPORT_MAX_CONNECTIONS_PER_HALT  ) {
					break;
				}
				if(  !conn.halt.is_bound()  ) {
					continue;
				}
				w.start_object();
				w.kv_int( "halt_id", conn.halt.get_id() );
				w.kv_int( "catg_index", catg );
				w.kv_int( "weight", conn.weight );
				w.kv_bool( "is_transfer", conn.is_transfer );
				w.end_object();
				written_connections++;
			}
			if(  written_connections >= EXPORT_MAX_CONNECTIONS_PER_HALT  ) {
				break;
			}
		}
		w.end_array();
		w.kv_bool( "connections_truncated", written_connections >= EXPORT_MAX_CONNECTIONS_PER_HALT );

		w.object_key( "stats" );
		static const char *const halt_stat_key[MAX_HALT_COST] = {
			"arrived", "departed", "waiting", "happy",
			"unhappy", "no_route", "convoys_arrived", "walked"
		};
		for(  int cost = 0;  cost < MAX_HALT_COST;  cost++  ) {
			w.array_key( halt_stat_key[cost] );
			for(  int month = 0;  month < MAX_MONTHS;  month++  ) {
				w.value_int( halt->get_finance_history( month, cost ) );
			}
			w.end_array();
		}
		w.end_object();

		w.end_object();
	}
	w.end_array();
}


static void export_convoys( json_writer_t &w, karte_t *welt )
{
	w.array_key( "convoys" );
	FOR( vector_tpl<convoihandle_t>, const cnv, welt->convoys() ) {
		if(  !cnv.is_bound()  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_int( "id", cnv.get_id() );
		w.kv_string( "name", cnv->get_name() );
		if(  cnv->get_owner()  ) {
			w.kv_int( "owner_id", cnv->get_owner()->get_player_nr() );
		}
		else {
			w.kv_null( "owner_id" );
		}
		if(  cnv->get_line().is_bound()  ) {
			w.kv_int( "line_id", cnv->get_line().get_id() );
		}
		else {
			w.kv_null( "line_id" );
		}
		w.kv_string( "state", export_convoi_state_name( cnv->get_state() ) );
		w.kv_koord3d( "pos", cnv->get_pos() );
		w.kv_int( "vehicle_count", cnv->get_vehicle_count() );

		sint64 total_capacity = 0;
		sint64 total_loaded = 0;
		w.array_key( "vehicles" );
		for(  uint8 i = 0;  i < cnv->get_vehicle_count();  i++  ) {
			const vehicle_t *v = cnv->get_vehikel( i );
			if(  v == NULL  ) {
				continue;
			}
			const vehicle_desc_t *desc = v->get_desc();
			const goods_desc_t *freight = v->get_cargo_type();
			total_capacity += v->get_cargo_max();
			total_loaded += v->get_total_cargo();
			w.start_object();
			w.kv_string( "name", desc ? desc->get_name() : NULL );
			w.kv_int( "capacity", v->get_cargo_max() );
			if(  freight  ) {
				w.kv_int( "goods_catg", freight->get_catg_index() );
				w.kv_string( "goods_name", freight->get_name() );
			}
			else {
				w.kv_null( "goods_catg" );
				w.kv_null( "goods_name" );
			}
			w.kv_int( "loaded", v->get_total_cargo() );
			if(  desc  ) {
				w.kv_int( "max_speed", desc->get_topspeed() );
				w.kv_int( "power", desc->get_power() );
			}
			else {
				w.kv_null( "max_speed" );
				w.kv_null( "power" );
			}
			// fork, mixed traction: engine hauled without pulling right now
			w.kv_bool( "idle", v->is_idle() );
			w.end_object();
		}
		w.end_array();

		// min_top_speed is in internal speed units, the dialogs show it as km/h
		w.kv_int( "max_speed", speed_to_kmh( cnv->get_min_top_speed() ) );
		w.kv_int( "sum_power", cnv->get_sum_power() );
		// fork, mixed traction (electric and other engines): which engines pull now, null otherwise
		if(  cnv->has_mixed_traction()  ) {
			w.kv_string( "traction", cnv->is_traction_off_wire() ? "off_wire" : (cnv->get_traction_both_under_wire() ? "under_wire_all" : "under_wire_electric") );
		}
		else {
			w.kv_null( "traction" );
		}
		w.kv_int( "loading_level", cnv->get_loading_level() );
		w.kv_int( "loading_limit", cnv->get_loading_limit() );
		w.kv_int( "total_capacity", total_capacity );
		w.kv_int( "total_loaded", total_loaded );
		w.kv_bool( "has_obsolete_vehicles", cnv->has_obsolete_vehicles() );

		const koord3d depot = cnv->get_home_depot();
		if(  depot == koord3d::invalid  ) {
			w.kv_null( "home_depot" );
		}
		else {
			w.kv_koord3d( "home_depot", depot );
		}

		w.kv_money( "profit_this_month", cnv->get_finance_history( 0, convoi_t::CONVOI_PROFIT ) );
		w.kv_money( "profit_last_year", cnv->get_finance_history( 1, convoi_t::CONVOI_PROFIT ) );
		w.kv_money( "revenue_this_month", cnv->get_finance_history( 0, convoi_t::CONVOI_REVENUE ) );
		w.kv_money( "operations_this_month", cnv->get_finance_history( 0, convoi_t::CONVOI_OPERATIONS ) );
		w.kv_int( "distance_this_month", cnv->get_finance_history( 0, convoi_t::CONVOI_DISTANCE ) );
		w.kv_int( "total_distance_traveled", cnv->get_total_distance_traveled() );

		// the schedule of a line convoy is the line schedule, so only dump lineless ones
		w.key( "schedule" );
		if(  cnv->get_line().is_bound()  ) {
			w.value_null();
		}
		else {
			export_schedule( w, cnv->get_schedule(), cnv->get_owner() );
		}

		w.end_object();
	}
	w.end_array();
}


static void export_cities( json_writer_t &w, karte_t *welt )
{
	w.array_key( "cities" );
	FOR( weighted_vector_tpl<stadt_t*>, const city, welt->get_cities() ) {
		if(  city == NULL  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_string( "name", city->get_name() );
		w.kv_koord( "pos", city->get_pos() );
		w.kv_koord( "center", city->get_center() );
		w.kv_int( "population", city->get_einwohner() );
		w.kv_int( "buildings", city->get_buildings() );
		w.kv_int( "growth", city->get_wachstum() );
		w.end_object();
	}
	w.end_array();
}


static void export_factories( json_writer_t &w, karte_t *welt )
{
	w.array_key( "factories" );
	FOR( slist_tpl<fabrik_t*>, const fab, welt->get_fab_list() ) {
		if(  fab == NULL  ) {
			continue;
		}
		w.newline();
		w.start_object();
		w.kv_string( "name", fab->get_name() );
		w.kv_koord3d( "pos", fab->get_pos() );
		player_t *owner = fab->get_owner();
		if(  owner  ) {
			w.kv_int( "owner_id", owner->get_player_nr() );
		}
		else {
			w.kv_null( "owner_id" );
		}
		w.kv_int( "production", fab->get_current_production() );
		w.kv_int( "base_production", fab->get_base_production() );
		w.end_object();
	}
	w.end_array();
}


bool savegame_export_t::write_json( karte_t *welt, const char *save_name, const char *path )
{
	if(  welt == NULL  ||  path == NULL  ) {
		return false;
	}

	FILE *f = fopen( path, "wb" );
	if(  f == NULL  ) {
		dbg->error( "savegame_export_t::write_json()", "cannot open '%s' for writing", path );
		return false;
	}

	dbg->message( "savegame_export_t::write_json()", "exporting game state to '%s'", path );

	json_writer_t w( f );
	w.start_object();
	w.kv_int( "format", 1 );
	export_meta( w, welt, save_name );
	export_settings( w, welt );
	export_goods( w );
	export_players( w, welt );
	export_lines( w, welt );
	halt_lines_map_t halt_lines;
	collect_schedule_lines( welt, halt_lines );
	export_stops( w, halt_lines );
	free_schedule_lines( halt_lines );
	export_convoys( w, welt );
	export_cities( w, welt );
	export_factories( w, welt );
	w.end_object();
	w.newline();

	const bool ok = w.ok();
	if(  fclose( f ) != 0  ||  !ok  ) {
		dbg->error( "savegame_export_t::write_json()", "error while writing '%s'", path );
		return false;
	}

	dbg->message( "savegame_export_t::write_json()", "export of '%s' finished", path );
	return true;
}
