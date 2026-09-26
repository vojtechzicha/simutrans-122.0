/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "convoi_detail_t.h"
#include "components/gui_divider.h"
#include "components/gui_image.h"
#include "components/gui_textarea.h"

#include "../display/simgraph.h"
#include "../simconvoi.h"
#include "../vehicle/simvehicle.h"
#include "../simcolor.h"
#include "../simworld.h"
#include "../simware.h"

#include "../dataobj/translator.h"
#include "../dataobj/loadsave.h"

#include "../player/simplay.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

karte_ptr_t convoi_detail_t:: welt;

class gui_vehicleinfo_t : public gui_aligned_container_t
{
	vehicle_t *v;
	cbuffer_t freight_info;
	gui_label_buf_t label_resale, label_friction;
	gui_label_minw_t label_power; // fork: keeps room for the idle mark
	gui_label_minw_t label_cargo; // fork: keeps room for standing and overcrowded passengers
	gui_textarea_t freight;

public:

	gui_vehicleinfo_t(vehicle_t *v, sint32 cnv_kmh)
	: freight(&freight_info)
	{
		this->v = v;
		set_table_layout(2,0);
		set_alignment(ALIGN_TOP | ALIGN_LEFT);

		// image
		new_component<gui_image_t>(v->get_loaded_image())->enable_offset_removal(true);
		add_table(1,0);
		{
			// name
			new_component<gui_label_t>( v->get_desc()->get_name() );
			// age
			gui_label_buf_t* l = new_component<gui_label_buf_t>();
			const sint32 month = v->get_purchase_time();
			l->buf().printf("%s %s %i", translator::translate("Manufactured:"), translator::get_month_name(month%12), month/12 );
			l->update();
			// value
			add_component(&label_resale);
			// max income
			sint64 max_income = - v->get_operating_cost();
			if(v->get_cargo_max() > 0) {
				max_income += (v->get_cargo_max() * ware_t::calc_revenue(v->get_cargo_type(), v->get_waytype(), cnv_kmh) )/3000;
			}
			add_table(2,1);
			{
				new_component<gui_label_t>("Max income:");
				l = new_component<gui_label_buf_t>();
				l->buf().append_money(max_income/100.0);
				l->update();
			}
			end_table();
			// power
			if(v->get_desc()->get_power()>0) {
				if(  v->get_convoi()  &&  v->get_convoi()->has_mixed_traction()  ) {
					cbuffer_t widest;
					widest.printf("%s %i kW, %s %.2f (%s)", translator::translate("Power:"), v->get_desc()->get_power(), translator::translate("Gear:"), v->get_desc()->get_gear()/64.0, translator::translate("idle") );
					label_power.set_min_width( proportional_string_width(widest) );
				}
				add_component(&label_power);
			}
			// friction
			add_component(&label_friction);
			if(v->get_cargo_max() > 0) {
				// freight type and load, see update_labels(); room for the fullest load
				cbuffer_t widest;
				if(  v->can_carry_crowd()  ) {
					print_cargo_load( widest, v->get_overcrowded_max(), v->get_standing_max() - v->get_cargo_max(), v->get_overcrowded_max() - v->get_standing_max() );
				}
				else {
					print_cargo_load( widest, v->get_cargo_max(), 0, 0 );
				}
				label_cargo.set_min_width( proportional_string_width(widest) );
				add_component(&label_cargo);
				// freight
				add_component(&freight);
			}
		}
		end_table();
		update_labels();
	}

	// fork: load and capacity, with the passengers standing or overcrowded beyond the seats
	void print_cargo_load(cbuffer_t &buf, uint32 total, uint32 standing, uint32 overcrowded) const
	{
		goods_desc_t const& g    = *v->get_cargo_type();
		char const*  const  name = translator::translate(g.get_catg() == 0 ? g.get_name() : g.get_catg_name());
		buf.printf("%u/%u%s %s", total, v->get_cargo_max(), translator::translate(v->get_cargo_mass()), name);
		if(  overcrowded > 0  ) {
			buf.printf( translator::translate(" (%u standing, %u overcrowded)"), standing, overcrowded );
		}
		else if(  standing > 0  ) {
			buf.printf( translator::translate(" (%u standing)"), standing );
		}
	}

	void update_labels()
	{
		label_resale.buf().printf("%s ", translator::translate("Restwert:"));
		label_resale.buf().append_money(v->calc_sale_value() / 100.0);
		if(  sint64 fix_cost = world()->scale_with_month_length((sint64)v->get_desc()->get_maintenance())  ) {
			cbuffer_t temp_buf;
			temp_buf.printf( translator::translate("(%.2f$/km %.2f$/m)"), (double)v->get_desc()->get_running_cost()/100.0, (double)fix_cost/100.0 );
			label_resale.buf().append( temp_buf );
		}
		else {
			cbuffer_t temp_buf;
			temp_buf.printf( translator::translate("(%.2f$/km)"), (double)v->get_desc()->get_running_cost()/100.0 );
			label_resale.buf().append( temp_buf );
		}
		label_resale.update();
		label_friction.buf().printf( "%s %i", translator::translate("Friction:"), v->get_frictionfactor() );
		label_friction.update();
		if(v->get_desc()->get_power()>0) {
			label_power.buf().printf("%s %i kW, %s %.2f", translator::translate("Power:"), v->get_desc()->get_power(), translator::translate("Gear:"), v->get_desc()->get_gear()/64.0 );
			if(  v->is_idle()  ) {
				// fork, mixed traction: hauled without pulling
				label_power.buf().printf(" (%s)", translator::translate("idle"));
			}
			label_power.update();
		}
		if(v->get_cargo_max() > 0) {
			uint16 seated = v->get_total_cargo(), standing = 0, overcrowded = 0;
			if(  v->can_carry_crowd()  ) {
				v->get_crowd_split( seated, standing, overcrowded );
			}
			print_cargo_load( label_cargo.buf(), v->get_total_cargo(), standing, overcrowded );
			label_cargo.update();
			freight_info.clear();
			v->get_cargo_info(freight_info);
		}
	}


	void draw(scr_coord offset) OVERRIDE
	{
		update_labels();
		gui_aligned_container_t::draw(offset);
	}

};





convoi_detail_t::convoi_detail_t(convoihandle_t cnv)
: scrolly(&container)
{
	if (cnv.is_bound()) {
		init(cnv);
	}
}

void convoi_detail_t::init(convoihandle_t cnv)
{
	this->cnv = cnv;

	// fork: also called again when the vehicles changed
	remove_all();
	container.remove_all();
	shown_vehicles.clear();

	set_table_layout(1,0);


	add_table(3,1);
	{
		if(  cnv->has_mixed_traction()  ) {
			// fork: room for the longest traction text, it changes while driving
			scr_coord_val w = 0;
			static const char *traction_texts[] = { "off wires", "under wires, all engines", "under wires, electric only" };
			for(  uint8 i=0;  i<3;  i++  ) {
				cbuffer_t buf;
				buf.printf( translator::translate("Leistung: %d kW"), cnv->get_sum_power() );
				buf.append(" ");
				buf.printf( translator::translate("(pulling %d kW, %s)"), cnv->get_sum_power(), translator::translate(traction_texts[i]) );
				w = max( w, proportional_string_width(buf) );
			}
			label_power.set_min_width( w );
		}
		add_component(&label_power);

		new_component<gui_fill_t>();

		add_table(2,1)->set_force_equal_columns(true);
		{
			sale_button.init(button_t::roundbox| button_t::flexible, "Verkauf");
			sale_button.set_tooltip("Remove vehicle from map. Use with care!");
			sale_button.add_listener(this);
			add_component(&sale_button);

			withdraw_button.init(button_t::roundbox| button_t::flexible, "withdraw");
			withdraw_button.set_tooltip("Convoi is sold when all wagons are empty.");
			withdraw_button.add_listener(this);
			add_component(&withdraw_button);
		}
		end_table();
	}
	end_table();

	add_component(&label_odometer);
	add_component(&label_length);
	add_component(&label_resale);
	add_component(&label_speed);
	add_component(&scrolly);

	const sint32 cnv_kmh = (cnv->front()->get_waytype() == air_wt) ? speed_to_kmh(cnv->get_min_top_speed()) : cnv->get_speedbonus_kmh();

	container.set_table_layout(1,0);
	for(unsigned veh=0;  veh<cnv->get_vehicle_count(); veh++ ) {
		vehicle_t *v = cnv->get_vehikel(veh);
		container.new_component<gui_vehicleinfo_t>(v, cnv_kmh);
		container.new_component<gui_divider_t>();
		shown_vehicles.append( v );
	}
	update_labels();
}


bool convoi_detail_t::update_vehicles()
{
	bool changed = shown_vehicles.get_count() != cnv->get_vehicle_count();
	for(  uint32 i=0;  !changed  &&  i<shown_vehicles.get_count();  i++  ) {
		changed = shown_vehicles[i] != cnv->get_vehikel(i);
	}
	if(  changed  ) {
		// the rows hold vehicle pointers, which may belong to another convoy or be gone by now
		init( cnv );
	}
	return changed;
}


void convoi_detail_t::update_labels()
{
	char number[128];
	number_to_string( number, (double)cnv->get_total_distance_traveled(), 0 );
	label_odometer.buf().printf(translator::translate("Odometer: %s km"), number );
	label_odometer.update();
	label_power.buf().printf( translator::translate("Leistung: %d kW"), cnv->get_sum_power() );
	if(  cnv->has_mixed_traction()  ) {
		// fork: installed power first, then the power of the engines that pull now and why
		label_power.buf().append(" ");
		label_power.buf().printf( translator::translate("(pulling %d kW, %s)"), cnv->get_active_power(),
			translator::translate( cnv->is_traction_off_wire() ? "off wires" : (cnv->get_traction_both_under_wire() ? "under wires, all engines" : "under wires, electric only") ) );
	}
	label_power.update();
	label_length.buf().printf("%s %i %s %i", translator::translate("Vehicle count:"), cnv->get_vehicle_count(), translator::translate("Station tiles:"), cnv->get_tile_length());
	label_length.update();
	label_resale.buf().printf("%s ", translator::translate("Restwert:"));
	label_resale.buf().append_money( cnv->calc_restwert() / 100.0 );
	label_resale.update();
	label_speed.buf().printf(translator::translate("Bonusspeed: %i km/h"), cnv->get_speedbonus_kmh() );
	label_speed.update();
}


void convoi_detail_t::draw(scr_coord offset)
{
	if(cnv->get_owner()==welt->get_active_player()  &&  !welt->get_active_player()->is_locked()) {
		withdraw_button.enable();
		sale_button.enable();
	}
	else {
		sale_button.disable();
		withdraw_button.disable();
	}
	withdraw_button.pressed = cnv->get_withdraw();
	update_labels();

	scrolly.set_size(scrolly.get_size());

	gui_aligned_container_t::draw(offset);
}



/**
 * This method is called if an action is triggered
 */
bool convoi_detail_t::action_triggered(gui_action_creator_t *comp,value_t /* */)
{
	if(cnv.is_bound()) {
		if(comp==&sale_button) {
			cnv->call_convoi_tool( 'x', NULL );
			return true;
		}
		else if(comp==&withdraw_button) {
			cnv->call_convoi_tool( 'w', NULL );
			return true;
		}
	}
	return false;
}


void convoi_detail_t::rdwr(loadsave_t *file)
{
	scrolly.rdwr(file);
}
