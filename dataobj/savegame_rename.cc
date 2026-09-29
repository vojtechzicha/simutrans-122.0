/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "savegame_rename.h"

#include "../simconvoi.h"
#include "../simdebug.h"
#include "../simfab.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../simcity.h"
#include "../simmenu.h"
#include "../simworld.h"

#include "../boden/grund.h"
#include "../obj/label.h"
#include "../player/simplay.h"
#include "../sys/simsys.h"

#include "koord3d.h"

#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"


/// one object to rename: what the rename tool needs, and how to read its name
struct rename_target_t
{
	player_t *player;   ///< the rename tool runs as this player
	cbuffer_t param;    ///< rename tool parameter without the new name, e.g. "h12,"
	const char *name;   ///< current name, NULL if the object has none
};


/// the handle with that id, unbound if there is none (set_id alone does not check the table size)
template<class T> static quickstone_tpl<T> handle_of( const char *id )
{
	quickstone_tpl<T> h;
	const long nr = atol(id);
	if(  nr>0  &&  nr<(long)quickstone_tpl<T>::get_size()  ) {
		h.set_id( (uint16)nr );
	}
	return h;
}


static bool find_target( karte_t *welt, const char *kind, const char *id, rename_target_t &t )
{
	t.player = NULL;
	t.name = NULL;
	int x, y, z;
	if(  strcmp( kind, "halt" )==0  ) {
		halthandle_t halt = handle_of<haltestelle_t>( id );
		if(  !halt.is_bound()  ) {
			return false;
		}
		t.player = halt->get_owner() ? halt->get_owner() : welt->get_public_player();
		t.param.printf( "h%u,", halt.get_id() );
		t.name = halt->get_name();
		return true;
	}
	if(  strcmp( kind, "line" )==0  ) {
		linehandle_t line = handle_of<simline_t>( id );
		if(  !line.is_bound()  ) {
			return false;
		}
		t.player = line->get_owner();
		t.param.printf( "l%u,", line.get_id() );
		t.name = line->get_name();
		return true;
	}
	if(  strcmp( kind, "convoy" )==0  ) {
		convoihandle_t cnv = handle_of<convoi_t>( id );
		if(  !cnv.is_bound()  ) {
			return false;
		}
		t.player = cnv->get_owner();
		t.param.printf( "c%u,", cnv.get_id() );
		t.name = cnv->get_internal_name();
		return true;
	}
	if(  strcmp( kind, "player" )==0  ) {
		const int nr = atoi(id);
		if(  nr<0  ||  nr>=MAX_PLAYER_COUNT  ||  !welt->get_player(nr)  ) {
			return false;
		}
		t.player = welt->get_player(nr);
		t.param.printf( "p%d,", nr );
		t.name = t.player->get_name();
		return true;
	}
	if(  strcmp( kind, "city" )==0  ) {
		if(  sscanf( id, "%d,%d", &x, &y )!=2  ) {
			return false;
		}
		// the rename tool takes the index in the city list
		for(  uint32 i=0;  i<welt->get_cities().get_count();  i++  ) {
			stadt_t *city = welt->get_cities()[i];
			if(  city  &&  city->get_pos()==koord(x,y)  ) {
				t.player = welt->get_public_player();
				t.param.printf( "t%u,", i );
				t.name = city->get_name();
				return true;
			}
		}
		return false;
	}
	if(  strcmp( kind, "factory" )==0  ) {
		if(  sscanf( id, "%d,%d,%d", &x, &y, &z )!=3  ) {
			return false;
		}
		fabrik_t *fab = fabrik_t::get_fab( koord(x,y) );
		if(  !fab  ) {
			return false;
		}
		t.player = welt->get_public_player();
		t.param.printf( "f%d,%d,%d,", x, y, z );
		t.name = fab->get_name();
		return true;
	}
	if(  strcmp( kind, "marker" )==0  ) {
		if(  sscanf( id, "%d,%d,%d", &x, &y, &z )!=3  ) {
			return false;
		}
		grund_t *gr = welt->lookup( koord3d(x,y,z) );
		if(  !gr  ||  !gr->find<label_t>()  ) {
			return false;
		}
		t.player = gr->find<label_t>()->get_owner();
		if(  !t.player  ) {
			t.player = welt->get_public_player();
		}
		t.param.printf( "m%d,%d,%d,", x, y, z );
		t.name = gr->get_text();
		return true;
	}
	return false;
}


/// true if old names the object: its name, or for a convoy also the name shown with its id
static bool matches_old_name( const char *kind, const char *id, const char *old_name, const char *name )
{
	if(  strcmp( old_name, "*" )==0  ) {
		return true;
	}
	if(  strcmp( old_name, name ? name : "" )==0  ) {
		return true;
	}
	if(  strcmp( kind, "convoy" )==0  ) {
		convoihandle_t cnv = handle_of<convoi_t>( id );
		return cnv.is_bound()  &&  strcmp( old_name, cnv->get_name() )==0;
	}
	return false;
}


static bool halt_name_used( const char *name, halthandle_t except )
{
	FOR( vector_tpl<halthandle_t>, const other, haltestelle_t::get_alle_haltestellen() ) {
		if(  other!=except  &&  strcmp( other->get_name(), name )==0  ) {
			return true;
		}
	}
	return false;
}


static void strip_line_end( char *s )
{
	size_t len = strlen(s);
	while(  len>0  &&  (s[len-1]=='\n'  ||  s[len-1]=='\r')  ) {
		s[--len] = 0;
	}
}


int savegame_rename_t::apply( karte_t *welt, const char *list_path, const char *report_path )
{
	FILE *in = dr_fopen( list_path, "rb" );
	if(  !in  ) {
		dbg->error( "savegame_rename_t::apply", "cannot read '%s'", list_path );
		return -1;
	}
	FILE *out = report_path ? dr_fopen( report_path, "wb" ) : NULL;
	if(  out  ) {
		fprintf( out, "status\tkind\tid\tname before\tname after\n" );
	}

	int failed = 0, done = 0;
	char line[2048];
	int line_nr = 0;
	while(  fgets( line, sizeof(line), in )  ) {
		line_nr++;
		char *s = line;
		if(  line_nr==1  &&  (uint8)s[0]==0xEF  &&  (uint8)s[1]==0xBB  &&  (uint8)s[2]==0xBF  ) {
			s += 3; // UTF-8 byte order mark
		}
		strip_line_end( s );
		if(  s[0]==0  ||  s[0]=='#'  ) {
			continue;
		}
		// KIND \t ID \t OLD \t NEW
		char *field[4];
		int n = 0;
		field[n++] = s;
		for(  char *p=s;  *p  &&  n<4;  p++  ) {
			if(  *p=='\t'  ) {
				*p = 0;
				field[n++] = p+1;
			}
		}
		const char *status = NULL;
		const char *before = "";
		const char *after = "";
		char before_buf[256] = "";
		rename_target_t t;
		if(  n<4  ||  field[3][0]==0  ) {
			status = "bad-row";
		}
		else if(  !find_target( welt, field[0], field[1], t )  ) {
			status = "not-found";
		}
		else {
			tstrncpy( before_buf, t.name ? t.name : "", lengthof(before_buf) );
			before = before_buf;
			if(  !matches_old_name( field[0], field[1], field[2], t.name )  ) {
				status = "old-name-differs";
			}
			else if(  strcmp( field[0], "halt" )==0  &&  halt_name_used( field[3], handle_of<haltestelle_t>( field[1] ) )  ) {
				// the game allows it, but loading renames the second stop of a name
				status = "name-used-by-other-halt";
			}
			else {
				t.param.append( field[3] );
				tool_t *tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
				tool->set_default_param( t.param );
				// what welt->set_tool does in a local game, without its password and scenario checks
				tool->init( t.player );
				delete tool;
				// read the name back: some renames are refused (a halt with a marker on its tile)
				rename_target_t check;
				find_target( welt, field[0], field[1], check );
				after = check.name ? check.name : "";
				status = strcmp( after, field[3] )==0 ? "ok" : "failed";
			}
		}
		if(  strncmp( status, "ok", 2 )==0  ) {
			done++;
		}
		else {
			failed++;
			dbg->warning( "savegame_rename_t::apply", "line %d: %s (%s %s)", line_nr, status, field[0], n>1 ? field[1] : "" );
		}
		if(  out  ) {
			fprintf( out, "%s\t%s\t%s\t%s\t%s\n", status, field[0], n>1 ? field[1] : "", before, after );
			fflush( out );
		}
	}
	fclose( in );
	if(  out  ) {
		fclose( out );
	}
	dbg->message( "savegame_rename_t::apply", "%d renamed, %d not", done, failed );
	return failed;
}
