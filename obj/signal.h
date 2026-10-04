/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef OBJ_SIGNAL_H
#define OBJ_SIGNAL_H


#include "roadsign.h"

#include "../simobj.h"


/**
 * Signals for rail tracks.
 *
 * @see blockstrecke_t
 * @see blockmanager
 */
class signal_t : public roadsign_t
{
public:
	signal_t(loadsave_t *file);
	signal_t(player_t *player, koord3d pos, ribi_t::ribi dir,const roadsign_desc_t *desc, bool preview = false) : roadsign_t(player,pos,dir,desc,preview) { state = rot;}

	/// @copydoc obj_t::info
	void info(cbuffer_t & buf) const OVERRIDE;

	void finish_rd() OVERRIDE;

	typ get_typ() const OVERRIDE { return obj_t::signal; }
	const char *get_name() const OVERRIDE {return "Signal";}

	/**
	* Calculate the actual image
	*/
	void calc_image() OVERRIDE;

	/**
	 * fork: aspect of an automatic block signal (autoblok), display only: red while the block ahead,
	 * up to the next signal or station boundary, is reserved by a train other than the one that
	 * reserved this signal's tile, or once the head of that train is past it; else yellow if the
	 * signal at the end of the block is red (or the block ends at a buffer stop), green if it is not
	 * (green without yellow images). Trains still stop only when their reservation fails.
	 * tell_behind false: the autoblocks behind need no refresh (red anyway, as when a head passes)
	 */
	void refresh_autoblock(bool tell_behind = true);

	/// fork: the aspect refresh_autoblock sets
	signalstate get_autoblock_aspect() const;

	/// fork: refreshes the autoblocks whose block ends at pos, a signal or station boundary a train
	/// just left in direction exit_dir, walking back over the track to the previous signals
	static void refresh_autoblocks_behind(koord3d pos, ribi_t::ribi exit_dir, waytype_t wt);

	/// fork: a train left the map from tiles front to rear (depot, deletion): the autoblocks of the
	/// blocks it held, both ways (there may be no signal behind it to do this)
	static void refresh_autoblocks_around(koord3d front, koord3d rear, waytype_t wt);

	/// fork: aspects of the autoblocks loaded or built since the last call (after loading: once the
	/// convoys have reserved their routes again)
	static void refresh_loaded_autoblocks();
};

#endif
