/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CONVOI_DETAIL_T_H
#define GUI_CONVOI_DETAIL_T_H


#include "components/gui_aligned_container.h"
#include "components/gui_scrollpane.h"
#include "components/gui_button.h"
#include "components/gui_label.h"
#include "components/action_listener.h"
#include "../convoihandle_t.h"
#include "../tpl/vector_tpl.h"

class scr_coord;
class karte_ptr_t;
class vehicle_t;

/**
 * Convoi details component
 * Fills information table for convoi
 */
class convoi_detail_t : public gui_aligned_container_t, private action_listener_t
{
public:
	enum sort_mode_t { by_destination=0, by_via=1, by_amount_via=2, by_amount=3, SORT_MODES=4 };

private:
	gui_aligned_container_t container;
	gui_scrollpane_t scrolly;

	gui_label_minw_t label_power; // fork: keeps room for the longest traction text
	gui_label_buf_t label_odometer, label_resale, label_length, label_speed;

	convoihandle_t cnv;
	button_t sale_button;
	button_t withdraw_button;

	// fork: the vehicles listed; coupling changes a convoy's vehicles outside a depot
	vector_tpl<const vehicle_t *> shown_vehicles;

	static karte_ptr_t welt;

	/// fork, coupling: the convoy whose vehicles are listed, the primary of a coupled pair
	convoihandle_t get_train() const;
public:
	convoi_detail_t(convoihandle_t cnv = convoihandle_t());

	/**
	 * Initializes layout, @p cnv needs to be valid.
	 */
	void init(convoihandle_t cnv);

	/**
	 * Rebuilds the list when the convoy's vehicles changed (coupling, uncoupling).
	 * @return true if it did, the window must then be laid out again
	 */
	bool update_vehicles();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void update_labels();

	void rdwr( loadsave_t *file );
};

#endif
