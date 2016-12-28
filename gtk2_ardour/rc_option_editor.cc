/*
    Copyright (C) 2001-2011 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#if !defined USE_CAIRO_IMAGE_SURFACE && !defined NDEBUG
#define OPTIONAL_CAIRO_IMAGE_SURFACE
#endif

#include <cairo/cairo.h>

#include <boost/algorithm/string.hpp>

#include <gtkmm/liststore.h>
#include <gtkmm/stock.h>
#include <gtkmm/scale.h>

#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/slider_controller.h>
#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/paths_dialog.h>
#include <gtkmm2ext/window_title.h>

#include "pbd/fpu.h"
#include "pbd/cpus.h"
#include "pbd/i18n.h"

#include "ardour/audio_backend.h"
#include "ardour/audioengine.h"
#include "ardour/profile.h"
#include "ardour/dB.h"
#include "ardour/rc_configuration.h"
#include "ardour/control_protocol_manager.h"
#include "ardour/port_manager.h"
#include "ardour/plugin_manager.h"
#include "control_protocol/control_protocol.h"

#include "canvas/wave_view.h"

#include "ardour_dialog.h"
#include "ardour_ui.h"
#include "ardour_window.h"
#include "color_theme_manager.h"
#include "gui_thread.h"
#include "keyboard.h"
#include "meter_patterns.h"
#include "midi_tracer.h"
#include "rc_option_editor.h"
#include "sfdb_ui.h"
#include "theme_manager.h"
#include "tooltips.h"
#include "ui_config.h"
#include "utils.h"

using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace PBD;
using namespace ARDOUR;
using namespace ARDOUR_UI_UTILS;

class ClickOptions : public OptionEditorPageBox, public OptionEditorPage
{
public:
	ClickOptions (RCConfiguration* c)
		: _rc_config (c)
		, _click_browse_button (_("Browse..."))
		, _click_emphasis_browse_button (_("Browse..."))
	{
		Table* t = &table;

		Label* l = manage (left_aligned_label (_("Emphasis on first beat:")));
		_use_emphasis_on_click_check_button.add (*l);
		t->attach (_use_emphasis_on_click_check_button, 1, 3, 0, 1, FILL);
		_use_emphasis_on_click_check_button.signal_toggled().connect (
		    sigc::mem_fun (*this, &ClickOptions::use_emphasis_on_click_toggled));

		l = manage (left_aligned_label (_("Use default Click:")));
		_use_default_click_check_button.add (*l);
		t->attach (_use_default_click_check_button, 1, 3, 1, 2, FILL);
		_use_default_click_check_button.signal_toggled().connect (
		    sigc::mem_fun (*this, &ClickOptions::use_default_click_toggled));

		l = manage (left_aligned_label (_("Click audio file:")));
		t->attach (*l, 1, 2, 2, 3, FILL);
		t->attach (_click_path_entry, 2, 3, 2, 3, FILL);
		_click_browse_button.signal_clicked ().connect (
		    sigc::mem_fun (*this, &ClickOptions::click_browse_clicked));
		t->attach (_click_browse_button, 3, 4, 2, 3, FILL);

		l = manage (left_aligned_label (_("Click emphasis audio file:")));
		t->attach (*l, 1, 2, 3, 4, FILL);
		t->attach (_click_emphasis_path_entry, 2, 3, 3, 4, FILL);
		_click_emphasis_browse_button.signal_clicked ().connect (
		    sigc::mem_fun (*this, &ClickOptions::click_emphasis_browse_clicked));
		t->attach (_click_emphasis_browse_button, 3, 4, 3, 4, FILL);

		FaderOption* fo = new FaderOption (
				"click-gain",
				_("Click gain level"),
				sigc::mem_fun (*_rc_config, &RCConfiguration::get_click_gain),
				sigc::mem_fun (*_rc_config, &RCConfiguration::set_click_gain)
				);

		fo->add_to_page (this);
		fo->set_state_from_config ();

		_box->pack_start (table, true, true);

		_click_path_entry.signal_activate().connect (sigc::mem_fun (*this, &ClickOptions::click_changed));
		_click_emphasis_path_entry.signal_activate().connect (sigc::mem_fun (*this, &ClickOptions::click_emphasis_changed));

		if (_rc_config->get_click_sound ().empty() &&
		    _rc_config->get_click_emphasis_sound().empty()) {
			_use_default_click_check_button.set_active (true);
			_use_emphasis_on_click_check_button.set_active (true);

		} else {
			_use_default_click_check_button.set_active (false);
			_use_emphasis_on_click_check_button.set_active (false);
		}
	}

	void parameter_changed (string const & p)
	{
		if (p == "click-sound") {
			_click_path_entry.set_text (_rc_config->get_click_sound());
		} else if (p == "click-emphasis-sound") {
			_click_emphasis_path_entry.set_text (_rc_config->get_click_emphasis_sound());
		} else if (p == "use-click-emphasis") {
			bool x = _rc_config->get_use_click_emphasis ();
			_use_emphasis_on_click_check_button.set_active (x);
		}
	}

	void set_state_from_config ()
	{
		parameter_changed ("click-sound");
		parameter_changed ("click-emphasis-sound");
		parameter_changed ("use-click-emphasis");
	}

private:

	void click_browse_clicked ()
	{
		SoundFileChooser sfdb (_("Choose Click"));

		sfdb.show_all ();
		sfdb.present ();

		if (sfdb.run () == RESPONSE_OK) {
			click_chosen (sfdb.get_filename());
		}
	}

	void click_chosen (string const & path)
	{
		_click_path_entry.set_text (path);
		_rc_config->set_click_sound (path);
	}

	void click_changed ()
	{
		click_chosen (_click_path_entry.get_text ());
	}

	void click_emphasis_browse_clicked ()
	{
		SoundFileChooser sfdb (_("Choose Click Emphasis"));

		sfdb.show_all ();
		sfdb.present ();

		if (sfdb.run () == RESPONSE_OK) {
			click_emphasis_chosen (sfdb.get_filename());
		}
	}

	void click_emphasis_chosen (string const & path)
	{
		_click_emphasis_path_entry.set_text (path);
		_rc_config->set_click_emphasis_sound (path);
	}

	void click_emphasis_changed ()
	{
		click_emphasis_chosen (_click_emphasis_path_entry.get_text ());
	}

	void use_default_click_toggled ()
	{
		if (_use_default_click_check_button.get_active ()) {
			_rc_config->set_click_sound ("");
			_rc_config->set_click_emphasis_sound ("");
			_click_path_entry.set_sensitive (false);
			_click_emphasis_path_entry.set_sensitive (false);
			_click_browse_button.set_sensitive (false);
			_click_emphasis_browse_button.set_sensitive (false);
		} else {
			_click_path_entry.set_sensitive (true);
			_click_emphasis_path_entry.set_sensitive (true);
			_click_browse_button.set_sensitive (true);
			_click_emphasis_browse_button.set_sensitive (true);
		}
	}

	void use_emphasis_on_click_toggled ()
	{
		if (_use_emphasis_on_click_check_button.get_active ()) {
			_rc_config->set_use_click_emphasis(true);
		} else {
			_rc_config->set_use_click_emphasis(false);
		}
	}

	RCConfiguration* _rc_config;
	CheckButton _use_default_click_check_button;
	CheckButton _use_emphasis_on_click_check_button;
	Entry _click_path_entry;
	Entry _click_emphasis_path_entry;
	Button _click_browse_button;
	Button _click_emphasis_browse_button;
};

class UndoOptions : public OptionEditorBox
{
public:
	UndoOptions (RCConfiguration* c) :
		_rc_config (c),
		_limit_undo_button (_("Limit undo history to")),
		_save_undo_button (_("Save undo history of"))
	{
		Table* t = new Table (2, 3);
		t->set_spacings (4);

		t->attach (_limit_undo_button, 0, 1, 0, 1, FILL);
		_limit_undo_spin.set_range (0, 512);
		_limit_undo_spin.set_increments (1, 10);
		t->attach (_limit_undo_spin, 1, 2, 0, 1, FILL | EXPAND);
		Label* l = manage (left_aligned_label (_("commands")));
		t->attach (*l, 2, 3, 0, 1);

		t->attach (_save_undo_button, 0, 1, 1, 2, FILL);
		_save_undo_spin.set_range (0, 512);
		_save_undo_spin.set_increments (1, 10);
		t->attach (_save_undo_spin, 1, 2, 1, 2, FILL | EXPAND);
		l = manage (left_aligned_label (_("commands")));
		t->attach (*l, 2, 3, 1, 2);

		_box->pack_start (*t);

		_limit_undo_button.signal_toggled().connect (sigc::mem_fun (*this, &UndoOptions::limit_undo_toggled));
		_limit_undo_spin.signal_value_changed().connect (sigc::mem_fun (*this, &UndoOptions::limit_undo_changed));
		_save_undo_button.signal_toggled().connect (sigc::mem_fun (*this, &UndoOptions::save_undo_toggled));
		_save_undo_spin.signal_value_changed().connect (sigc::mem_fun (*this, &UndoOptions::save_undo_changed));
	}

	void parameter_changed (string const & p)
	{
		if (p == "history-depth") {
			int32_t const d = _rc_config->get_history_depth();
			_limit_undo_button.set_active (d != 0);
			_limit_undo_spin.set_sensitive (d != 0);
			_limit_undo_spin.set_value (d);
		} else if (p == "save-history") {
			bool const x = _rc_config->get_save_history ();
			_save_undo_button.set_active (x);
			_save_undo_spin.set_sensitive (x);
		} else if (p == "save-history-depth") {
			_save_undo_spin.set_value (_rc_config->get_saved_history_depth());
		}
	}

	void set_state_from_config ()
	{
		parameter_changed ("save-history");
		parameter_changed ("history-depth");
		parameter_changed ("save-history-depth");
	}

	void limit_undo_toggled ()
	{
		bool const x = _limit_undo_button.get_active ();
		_limit_undo_spin.set_sensitive (x);
		int32_t const n = x ? 16 : 0;
		_limit_undo_spin.set_value (n);
		_rc_config->set_history_depth (n);
	}

	void limit_undo_changed ()
	{
		_rc_config->set_history_depth (_limit_undo_spin.get_value_as_int ());
	}

	void save_undo_toggled ()
	{
		bool const x = _save_undo_button.get_active ();
		_rc_config->set_save_history (x);
	}

	void save_undo_changed ()
	{
		_rc_config->set_saved_history_depth (_save_undo_spin.get_value_as_int ());
	}

private:
	RCConfiguration* _rc_config;
	CheckButton _limit_undo_button;
	SpinButton _limit_undo_spin;
	CheckButton _save_undo_button;
	SpinButton _save_undo_spin;
};



static const struct {
	const char *name;
	guint modifier;
} modifiers[] = {

	{ "Unmodified", 0 },

#ifdef __APPLE__

	/* Command = Meta
	   Option/Alt = Mod1
	*/
	{ "Key|Shift", GDK_SHIFT_MASK },
	{ "Command", GDK_MOD2_MASK },
	{ "Control", GDK_CONTROL_MASK },
	{ "Option", GDK_MOD1_MASK },
	{ "Command-Shift", GDK_MOD2_MASK|GDK_SHIFT_MASK },
	{ "Command-Option", GDK_MOD2_MASK|GDK_MOD1_MASK },
	{ "Command-Control", GDK_MOD2_MASK|GDK_CONTROL_MASK },
	{ "Command-Option-Control", GDK_MOD2_MASK|GDK_MOD1_MASK|GDK_CONTROL_MASK },
	{ "Option-Control", GDK_MOD1_MASK|GDK_CONTROL_MASK },
	{ "Option-Shift", GDK_MOD1_MASK|GDK_SHIFT_MASK },
	{ "Control-Shift", GDK_CONTROL_MASK|GDK_SHIFT_MASK },
	{ "Shift-Command-Option", GDK_MOD5_MASK|GDK_SHIFT_MASK|GDK_MOD2_MASK },

#else
	{ "Key|Shift", GDK_SHIFT_MASK },
	{ "Control", GDK_CONTROL_MASK },
	{ "Alt", GDK_MOD1_MASK },
	{ "Control-Shift", GDK_CONTROL_MASK|GDK_SHIFT_MASK },
	{ "Control-Alt", GDK_CONTROL_MASK|GDK_MOD1_MASK },
	{ "Control-Windows", GDK_CONTROL_MASK|GDK_MOD4_MASK },
	{ "Control-Shift-Alt", GDK_CONTROL_MASK|GDK_SHIFT_MASK|GDK_MOD1_MASK },
	{ "Alt-Windows", GDK_MOD1_MASK|GDK_MOD4_MASK },
	{ "Alt-Shift", GDK_MOD1_MASK|GDK_SHIFT_MASK },
	{ "Alt-Shift-Windows", GDK_MOD1_MASK|GDK_SHIFT_MASK|GDK_MOD4_MASK },
	{ "Mod2", GDK_MOD2_MASK },
	{ "Mod3", GDK_MOD3_MASK },
	{ "Windows", GDK_MOD4_MASK },
	{ "Mod5", GDK_MOD5_MASK },
#endif
	{ 0, 0 }
};


class KeyboardOptions : public OptionEditorBox
{
public:
	KeyboardOptions () :
		  _delete_button_adjustment (3, 1, 12),
		  _delete_button_spin (_delete_button_adjustment),
		  _edit_button_adjustment (3, 1, 5),
		  _edit_button_spin (_edit_button_adjustment),
		  _insert_note_button_adjustment (3, 1, 5),
		  _insert_note_button_spin (_insert_note_button_adjustment)
	{
		const std::string restart_msg = _("\nChanges to this setting will only persist after your project has been saved.");
		/* internationalize and prepare for use with combos */

		vector<string> dumb;
		for (int i = 0; modifiers[i].name; ++i) {
			dumb.push_back (S_(modifiers[i].name));
		}

		set_popdown_strings (_edit_modifier_combo, dumb);
		_edit_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::edit_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_edit_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1 + button 3 (right mouse button)</b>%2"),  Keyboard::primary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == Keyboard::edit_modifier ()) {
				_edit_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		Table* t = manage (new Table (5, 11));
		t->set_spacings (4);

		int row = 0;
		int col = 0;

		Label* l = manage (left_aligned_label (_("Select Keyboard layout:")));
		l->set_name ("OptionsLabel");

		vector<string> strs;

		for (map<string,string>::iterator bf = Keyboard::binding_files.begin(); bf != Keyboard::binding_files.end(); ++bf) {
			strs.push_back (bf->first);
		}

		set_popdown_strings (_keyboard_layout_selector, strs);
		_keyboard_layout_selector.set_active_text (Keyboard::current_binding_name());
		_keyboard_layout_selector.signal_changed().connect (sigc::mem_fun (*this, &KeyboardOptions::bindings_changed));

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_keyboard_layout_selector, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 0;

		l = manage (left_aligned_label (_("When Clicking:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, col, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		l = manage (left_aligned_label (_("Edit using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_edit_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		l = manage (new Label (_("+ button")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col + 3, col + 4, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_edit_button_spin, col + 4, col + 5, row, row + 1, FILL | EXPAND, FILL);

		_edit_button_spin.set_name ("OptionsEntry");
		_edit_button_adjustment.set_value (Keyboard::edit_button());
		_edit_button_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::edit_button_changed));

		++row;
		col = 1;

		set_popdown_strings (_delete_modifier_combo, dumb);
		_delete_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::delete_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_delete_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1 + button 3 (right mouse button)</b>%2"), Keyboard::tertiary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == Keyboard::delete_modifier ()) {
				_delete_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Delete using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_delete_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		l = manage (new Label (_("+ button")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col + 3, col + 4, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_delete_button_spin, col + 4, col + 5, row, row + 1, FILL | EXPAND, FILL);

		_delete_button_spin.set_name ("OptionsEntry");
		_delete_button_adjustment.set_value (Keyboard::delete_button());
		_delete_button_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::delete_button_changed));

		++row;
		col = 1;

		set_popdown_strings (_insert_note_modifier_combo, dumb);
		_insert_note_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::insert_note_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_insert_note_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1 + button 1 (left mouse button)</b>%2"), Keyboard::primary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == Keyboard::insert_note_modifier ()) {
				_insert_note_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Insert note using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_insert_note_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		l = manage (new Label (_("+ button")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col + 3, col + 4, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_insert_note_button_spin, col + 4, col + 5, row, row + 1, FILL | EXPAND, FILL);

		_insert_note_button_spin.set_name ("OptionsEntry");
		_insert_note_button_adjustment.set_value (Keyboard::insert_note_button());
		_insert_note_button_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::insert_note_button_changed));

		++row;

		l = manage (left_aligned_label (_("When Beginning a Drag:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, 0, 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* copy modifier */
		set_popdown_strings (_copy_modifier_combo, dumb);
		_copy_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::copy_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_copy_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"),
#ifdef __APPLE__
								     Keyboard::secondary_modifier_name (),
#else
								     Keyboard::primary_modifier_name (),
#endif
								     restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) Keyboard::CopyModifier) {
				_copy_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Copy items using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_copy_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

				++row;
		col = 1;

		/* constraint modifier */
		set_popdown_strings (_constraint_modifier_combo, dumb);
		_constraint_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::constraint_modifier_chosen));
		std::string mod_str = string_compose (X_("%1-%2"), Keyboard::primary_modifier_name (), Keyboard::level4_modifier_name ());
		Gtkmm2ext::UI::instance()->set_tip (_constraint_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"),
#ifdef __APPLE__
								     Keyboard::primary_modifier_name (),
#else
								     Keyboard::tertiary_modifier_name (),
#endif
								     restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::constraint_modifier ()) {
				_constraint_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Constrain drag using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_constraint_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* push points */
		set_popdown_strings (_push_points_combo, dumb);
		_push_points_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::push_points_modifier_chosen));

		mod_str = string_compose (X_("%1-%2"), Keyboard::primary_modifier_name (), Keyboard::level4_modifier_name ());
		Gtkmm2ext::UI::instance()->set_tip (_push_points_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), mod_str, restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::push_points_modifier ()) {
				_push_points_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Push points using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_push_points_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;

		l = manage (left_aligned_label (_("When Beginning a Trim:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, 0, 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* trim_contents */
		set_popdown_strings (_trim_contents_combo, dumb);
		_trim_contents_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::trim_contents_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_trim_contents_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), Keyboard::primary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::trim_contents_modifier ()) {
				_trim_contents_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Trim contents using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_trim_contents_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* anchored trim */
		set_popdown_strings (_trim_anchored_combo, dumb);
		_trim_anchored_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::trim_anchored_modifier_chosen));

		mod_str = string_compose (X_("%1-%2"), Keyboard::primary_modifier_name (), Keyboard::tertiary_modifier_name ());
		Gtkmm2ext::UI::instance()->set_tip (_trim_anchored_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), mod_str, restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::trim_anchored_modifier ()) {
				_trim_anchored_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Anchored trim using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		++col;
		t->attach (_trim_anchored_combo, col, col + 1, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* jump trim disabled for now
		set_popdown_strings (_trim_jump_combo, dumb);
		_trim_jump_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::trim_jump_modifier_chosen));

		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) Keyboard::trim_jump_modifier ()) {
				_trim_jump_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Jump after trim using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		++col;
		t->attach (_trim_jump_combo, col, col + 1, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;
		*/

		/* note resize relative */
		set_popdown_strings (_note_size_relative_combo, dumb);
		_note_size_relative_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::note_size_relative_modifier_chosen));
		Gtkmm2ext::UI::instance()->set_tip (_note_size_relative_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), Keyboard::tertiary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::note_size_relative_modifier ()) {
				_note_size_relative_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Resize notes relatively using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		++col;
		t->attach (_note_size_relative_combo, col, col + 1, row, row + 1, FILL | EXPAND, FILL);

		++row;

		l = manage (left_aligned_label (_("While Dragging:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, 0, 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* ignore snap */
		set_popdown_strings (_snap_modifier_combo, dumb);
		_snap_modifier_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::snap_modifier_chosen));
#ifdef __APPLE__
		mod_str = string_compose (X_("%1-%2"), Keyboard::level4_modifier_name (), Keyboard::tertiary_modifier_name ());
#else
		mod_str = Keyboard::secondary_modifier_name();
#endif
		Gtkmm2ext::UI::instance()->set_tip (_snap_modifier_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), mod_str, restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) Keyboard::snap_modifier ()) {
				_snap_modifier_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Ignore snap using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_snap_modifier_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* snap delta */
		set_popdown_strings (_snap_delta_combo, dumb);
		_snap_delta_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::snap_delta_modifier_chosen));
#ifdef __APPLE__
		mod_str = Keyboard::level4_modifier_name ();
#else
		mod_str = string_compose (X_("%1-%2"), Keyboard::secondary_modifier_name (), Keyboard::level4_modifier_name ());
#endif
		Gtkmm2ext::UI::instance()->set_tip (_snap_delta_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), mod_str, restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) Keyboard::snap_delta_modifier ()) {
				_snap_delta_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Snap relatively using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_snap_delta_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;

		l = manage (left_aligned_label (_("While Trimming:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, 0, 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* trim_overlap */
		set_popdown_strings (_trim_overlap_combo, dumb);
		_trim_overlap_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::trim_overlap_modifier_chosen));

		Gtkmm2ext::UI::instance()->set_tip (_trim_overlap_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), Keyboard::tertiary_modifier_name (), restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::trim_overlap_modifier ()) {
				_trim_overlap_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Resize overlapped regions using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_trim_overlap_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		++row;

		l = manage (left_aligned_label (_("While Dragging Control Points:")));
		l->set_name ("OptionEditorHeading");
		t->attach (*l, 0, 2, row, row + 1, FILL | EXPAND, FILL);

		++row;
		col = 1;

		/* fine adjust */
		set_popdown_strings (_fine_adjust_combo, dumb);
		_fine_adjust_combo.signal_changed().connect (sigc::mem_fun(*this, &KeyboardOptions::fine_adjust_modifier_chosen));

		mod_str = string_compose (X_("%1-%2"), Keyboard::primary_modifier_name (), Keyboard::secondary_modifier_name ());
		Gtkmm2ext::UI::instance()->set_tip (_fine_adjust_combo,
						    (string_compose (_("<b>Recommended Setting: %1</b>%2"), mod_str, restart_msg)));
		for (int x = 0; modifiers[x].name; ++x) {
			if (modifiers[x].modifier == (guint) ArdourKeyboard::fine_adjust_modifier ()) {
				_fine_adjust_combo.set_active_text (S_(modifiers[x].name));
				break;
			}
		}

		l = manage (left_aligned_label (_("Fine adjust using:")));
		l->set_name ("OptionsLabel");

		t->attach (*l, col, col + 1, row, row + 1, FILL | EXPAND, FILL);
		t->attach (_fine_adjust_combo, col + 1, col + 2, row, row + 1, FILL | EXPAND, FILL);

		_box->pack_start (*t, false, false);
	}

	void parameter_changed (string const &)
	{
		/* XXX: these aren't really config options... */
	}

	void set_state_from_config ()
	{
		/* XXX: these aren't really config options... */
	}

private:

	void bindings_changed ()
	{
		string const txt = _keyboard_layout_selector.get_active_text();

		/* XXX: config...?  for all this keyboard stuff */

		for (map<string,string>::iterator i = Keyboard::binding_files.begin(); i != Keyboard::binding_files.end(); ++i) {
			if (txt == i->first) {
				if (Keyboard::load_keybindings (i->second)) {
					Keyboard::save_keybindings ();
				}
			}
		}
	}

	void edit_modifier_chosen ()
	{
		string const txt = _edit_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_edit_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void delete_modifier_chosen ()
	{
		string const txt = _delete_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_delete_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void copy_modifier_chosen ()
	{
		string const txt = _copy_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_copy_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void insert_note_modifier_chosen ()
	{
		string const txt = _insert_note_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_insert_note_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void snap_modifier_chosen ()
	{
		string const txt = _snap_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_snap_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void snap_delta_modifier_chosen ()
	{
		string const txt = _snap_delta_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				Keyboard::set_snap_delta_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void constraint_modifier_chosen ()
	{
		string const txt = _constraint_modifier_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_constraint_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void trim_contents_modifier_chosen ()
	{
		string const txt = _trim_contents_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_trim_contents_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void trim_overlap_modifier_chosen ()
	{
		string const txt = _trim_overlap_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_trim_overlap_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void trim_anchored_modifier_chosen ()
	{
		string const txt = _trim_anchored_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_trim_anchored_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void fine_adjust_modifier_chosen ()
	{
		string const txt = _fine_adjust_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_fine_adjust_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void push_points_modifier_chosen ()
	{
		string const txt = _push_points_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_push_points_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void note_size_relative_modifier_chosen ()
	{
		string const txt = _note_size_relative_combo.get_active_text();

		for (int i = 0; modifiers[i].name; ++i) {
			if (txt == S_(modifiers[i].name)) {
				ArdourKeyboard::set_note_size_relative_modifier (modifiers[i].modifier);
				break;
			}
		}
	}

	void delete_button_changed ()
	{
		Keyboard::set_delete_button (_delete_button_spin.get_value_as_int());
	}

	void edit_button_changed ()
	{
		Keyboard::set_edit_button (_edit_button_spin.get_value_as_int());
	}

	void insert_note_button_changed ()
	{
		Keyboard::set_insert_note_button (_insert_note_button_spin.get_value_as_int());
	}

	ComboBoxText _keyboard_layout_selector;
	ComboBoxText _edit_modifier_combo;
	ComboBoxText _delete_modifier_combo;
	ComboBoxText _copy_modifier_combo;
	ComboBoxText _insert_note_modifier_combo;
	ComboBoxText _snap_modifier_combo;
	ComboBoxText _snap_delta_combo;
	ComboBoxText _constraint_modifier_combo;
	ComboBoxText _trim_contents_combo;
	ComboBoxText _trim_overlap_combo;
	ComboBoxText _trim_anchored_combo;
	ComboBoxText _trim_jump_combo;
	ComboBoxText _fine_adjust_combo;
	ComboBoxText _push_points_combo;
	ComboBoxText _note_size_relative_combo;
	Adjustment _delete_button_adjustment;
	SpinButton _delete_button_spin;
	Adjustment _edit_button_adjustment;
	SpinButton _edit_button_spin;
	Adjustment _insert_note_button_adjustment;
	SpinButton _insert_note_button_spin;

};

class FontScalingOptions : public OptionEditorBox
{
public:
	FontScalingOptions () :
		_dpi_adjustment (100, 50, 250, 1, 5),
		_dpi_slider (_dpi_adjustment)
	{
		_dpi_adjustment.set_value (UIConfiguration::instance().get_font_scale() / 1024.);

		Label* l = manage (new Label (_("GUI and Font scaling:")));
		l->set_name ("OptionsLabel");

		const std::string dflt = _("Default");
		const std::string empty = X_(""); // despite gtk-doc saying so, NULL does not work as reference

		_dpi_slider.set_name("FontScaleSlider");
		_dpi_slider.set_update_policy (UPDATE_DISCONTINUOUS);
		_dpi_slider.set_draw_value(false);
		_dpi_slider.add_mark(50,  Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(60,  Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(70,  Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(80,  Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(90,  Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(100, Gtk::POS_TOP, dflt);
		_dpi_slider.add_mark(125, Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(150, Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(175, Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(200, Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(225, Gtk::POS_TOP, empty);
		_dpi_slider.add_mark(250, Gtk::POS_TOP, empty);

		HBox* h = manage (new HBox);
		h->set_spacing (4);
		h->pack_start (*l, false, false);
		h->pack_start (_dpi_slider, true, true);

		_box->pack_start (*h, false, false);

		set_note (_("Adjusting the scale requires an application restart to re-layout."));

		_dpi_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &FontScalingOptions::dpi_changed));
	}

	void parameter_changed (string const & p)
	{
		if (p == "font-scale") {
			_dpi_adjustment.set_value (UIConfiguration::instance().get_font_scale() / 1024.);
		}
	}

	void set_state_from_config ()
	{
		parameter_changed ("font-scale");
	}

private:

	void dpi_changed ()
	{
		UIConfiguration::instance().set_font_scale ((long) floor (_dpi_adjustment.get_value() * 1024.));
		/* XXX: should be triggered from the parameter changed signal */
		UIConfiguration::instance().reset_dpi ();
	}

	Adjustment _dpi_adjustment;
	HScale _dpi_slider;
};

class VstTimeOutSliderOption : public OptionEditorBox
{
public:
	VstTimeOutSliderOption (RCConfiguration* c)
		: _rc_config (c)
		, _timeout_adjustment (0, 0, 3000, 50, 50)
		, _timeout_slider (_timeout_adjustment)
	{
		_timeout_slider.set_digits (0);
		_timeout_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &VstTimeOutSliderOption::timeout_changed));

		_timeout_slider.set_draw_value(false);
		_timeout_slider.add_mark(   0,  Gtk::POS_TOP, _("\u221e")); // infinity
		_timeout_slider.add_mark( 300,  Gtk::POS_TOP, _("30 sec"));
		_timeout_slider.add_mark( 600,  Gtk::POS_TOP, _("1 min"));
		_timeout_slider.add_mark(1200,  Gtk::POS_TOP, _("2 mins"));
		_timeout_slider.add_mark(1800,  Gtk::POS_TOP, _("3 mins"));
		_timeout_slider.add_mark(2400,  Gtk::POS_TOP, _("4 mins"));
		_timeout_slider.add_mark(3000,  Gtk::POS_TOP, _("5 mins"));

		Gtkmm2ext::UI::instance()->set_tip(_timeout_slider,
			 _("Specify the default timeout for plugin instantiation. Plugins that require more time to load will be blacklisted. A value of 0 disables the timeout."));

		Label* l = manage (left_aligned_label (_("Scan Time Out:")));
		HBox* h = manage (new HBox);
		h->set_spacing (4);
		h->pack_start (*l, false, false);
		h->pack_start (_timeout_slider, true, true);

		_box->pack_start (*h, false, false);
	}

	void parameter_changed (string const & p)
	{
		if (p == "vst-scan-timeout") {
			int const x = _rc_config->get_vst_scan_timeout();
			_timeout_adjustment.set_value (x);
		}
	}

	void set_state_from_config ()
	{
		parameter_changed ("vst-scan-timeout");
	}

private:

	void timeout_changed ()
	{
		int x = floor(_timeout_adjustment.get_value());
		_rc_config->set_vst_scan_timeout(x);
	}

	RCConfiguration* _rc_config;
	Adjustment _timeout_adjustment;
	HScale _timeout_slider;
};





class ClipLevelOptions : public OptionEditorBox
{
public:
	ClipLevelOptions ()
		: _clip_level_adjustment (-.5, -50.0, 0.0, 0.1, 1.0) /* units of dB */
		, _clip_level_slider (_clip_level_adjustment)
	{
		_clip_level_adjustment.set_value (UIConfiguration::instance().get_waveform_clip_level ());

		Label* l = manage (new Label (_("Waveform Clip Level (dBFS):")));
		l->set_name ("OptionsLabel");

		_clip_level_slider.set_update_policy (UPDATE_DISCONTINUOUS);
		HBox* h = manage (new HBox);
		h->set_spacing (4);
		h->pack_start (*l, false, false);
		h->pack_start (_clip_level_slider, true, true);

		_box->pack_start (*h, false, false);

		_clip_level_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &ClipLevelOptions::clip_level_changed));
	}

	void parameter_changed (string const & p)
	{
		if (p == "waveform-clip-level") {
			_clip_level_adjustment.set_value (UIConfiguration::instance().get_waveform_clip_level());
		}
	}

	void set_state_from_config ()
	{
		parameter_changed ("waveform-clip-level");
	}

private:

	void clip_level_changed ()
	{
		UIConfiguration::instance().set_waveform_clip_level (_clip_level_adjustment.get_value());
		/* XXX: should be triggered from the parameter changed signal */
		ArdourCanvas::WaveView::set_clip_level (_clip_level_adjustment.get_value());
	}

	Adjustment _clip_level_adjustment;
	HScale _clip_level_slider;
};

class BufferingOptions : public OptionEditorBox
{
	public:
		BufferingOptions (RCConfiguration* c)
			: _rc_config (c)
			, _playback_adjustment (5, 1, 60, 1, 4)
			, _capture_adjustment (5, 1, 60, 1, 4)
			, _playback_slider (_playback_adjustment)
			, _capture_slider (_capture_adjustment)
	{
		vector<string> presets;

		/* these must match the order of the enums for BufferingPreset */

		presets.push_back (_("Small sessions (4-16 tracks)"));
		presets.push_back (_("Medium sessions (16-64 tracks)"));
		presets.push_back (_("Large sessions (64+ tracks)"));
		presets.push_back (_("Custom (set by sliders below)"));

		set_popdown_strings (_buffering_presets_combo, presets);

		Label* l = manage (new Label (_("Preset:")));
		l->set_name ("OptionsLabel");
		HBox* h = manage (new HBox);
		h->set_spacing (12);
		h->pack_start (*l, false, false);
		h->pack_start (_buffering_presets_combo, true, true);
		_box->pack_start (*h, false, false);

		_buffering_presets_combo.signal_changed().connect (sigc::mem_fun (*this, &BufferingOptions::preset_changed));

		_playback_adjustment.set_value (_rc_config->get_audio_playback_buffer_seconds());

		l = manage (new Label (_("Playback (seconds of buffering):")));
		l->set_name ("OptionsLabel");

		_playback_slider.set_update_policy (UPDATE_DISCONTINUOUS);
		h = manage (new HBox);
		h->set_spacing (4);
		h->pack_start (*l, false, false);
		h->pack_start (_playback_slider, true, true);

		_box->pack_start (*h, false, false);

		_capture_adjustment.set_value (_rc_config->get_audio_capture_buffer_seconds());

		l = manage (new Label (_("Recording (seconds of buffering):")));
		l->set_name ("OptionsLabel");

		_capture_slider.set_update_policy (UPDATE_DISCONTINUOUS);
		h = manage (new HBox);
		h->set_spacing (4);
		h->pack_start (*l, false, false);
		h->pack_start (_capture_slider, true, true);

		_box->pack_start (*h, false, false);

		_capture_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &BufferingOptions::capture_changed));
		_playback_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &BufferingOptions::playback_changed));
	}

		void parameter_changed (string const & p)
		{
			if (p == "buffering-preset") {
				switch (_rc_config->get_buffering_preset()) {
					case Small:
						_playback_slider.set_sensitive (false);
						_capture_slider.set_sensitive (false);
						_buffering_presets_combo.set_active (0);
						break;
					case Medium:
						_playback_slider.set_sensitive (false);
						_capture_slider.set_sensitive (false);
						_buffering_presets_combo.set_active (1);
						break;
					case Large:
						_playback_slider.set_sensitive (false);
						_capture_slider.set_sensitive (false);
						_buffering_presets_combo.set_active (2);
						break;
					case Custom:
						_playback_slider.set_sensitive (true);
						_capture_slider.set_sensitive (true);
						_buffering_presets_combo.set_active (3);
						break;
				}
			}

			if (p == "playback-buffer-seconds") {
				_playback_adjustment.set_value (_rc_config->get_audio_playback_buffer_seconds());
			} else if (p == "capture-buffer-seconds") {
				_capture_adjustment.set_value (_rc_config->get_audio_capture_buffer_seconds());
			}
		}

		void set_state_from_config ()
		{
			parameter_changed ("buffering-preset");
			parameter_changed ("playback-buffer-seconds");
			parameter_changed ("capture-buffer-seconds");
		}

	private:

		void preset_changed ()
		{
			int index = _buffering_presets_combo.get_active_row_number ();
			if (index < 0) {
				return;
			}
			switch (index) {
				case 0:
					_rc_config->set_buffering_preset (Small);
					break;
				case 1:
					_rc_config->set_buffering_preset (Medium);
					break;
				case 2:
					_rc_config->set_buffering_preset (Large);
					break;
				case 3:
					_rc_config->set_buffering_preset (Custom);
					break;
				default:
					error << string_compose (_("programming error: unknown buffering preset string, index = %1"), index) << endmsg;
					break;
			}
		}

		void playback_changed ()
		{
			_rc_config->set_audio_playback_buffer_seconds ((long) _playback_adjustment.get_value());
		}

		void capture_changed ()
		{
			_rc_config->set_audio_capture_buffer_seconds ((long) _capture_adjustment.get_value());
		}

		RCConfiguration* _rc_config;
		Adjustment _playback_adjustment;
		Adjustment _capture_adjustment;
		HScale _playback_slider;
		HScale _capture_slider;
		ComboBoxText _buffering_presets_combo;
};

class ControlSurfacesOptions : public OptionEditorPageBox
{
	public:
		ControlSurfacesOptions ()
			: _ignore_view_change (0)
		{
			_store = ListStore::create (_model);
			_view.set_model (_store);
			_view.append_column (_("Control Surface Protocol"), _model.name);
			_view.get_column(0)->set_resizable (true);
			_view.get_column(0)->set_expand (true);
			_view.append_column_editable (_("Enabled"), _model.enabled);
			_view.append_column_editable (_("Feedback"), _model.feedback);

			Label* l = manage (new Label (string_compose ("<b>%1</b>", _("Control Surfaces"))));
			l->set_alignment (0, 0.5);
			l->set_use_markup (true);
			_box->pack_start (*l, false, false);
			_box->pack_start (_view, false, false);

			Gtk::HBox* edit_box = manage (new Gtk::HBox);
			edit_box->set_spacing(3);
			_box->pack_start (*edit_box, false, false);
			edit_box->show ();

			Label* label = manage (new Label);
			label->set_text (_("Edit the settings for selected protocol (it must be ENABLED first):"));
			edit_box->pack_start (*label, false, false);
			label->show ();

			edit_button = manage (new Button(_("Show Protocol Settings")));
			edit_button->signal_clicked().connect (sigc::mem_fun(*this, &ControlSurfacesOptions::edit_btn_clicked));
			edit_box->pack_start (*edit_button, true, true);
			edit_button->set_sensitive (false);
			edit_button->show ();

			ControlProtocolManager& m = ControlProtocolManager::instance ();
			m.ProtocolStatusChange.connect (protocol_status_connection, MISSING_INVALIDATOR,
					boost::bind (&ControlSurfacesOptions::protocol_status_changed, this, _1), gui_context());

			_store->signal_row_changed().connect (sigc::mem_fun (*this, &ControlSurfacesOptions::view_changed));
			_view.signal_button_press_event().connect_notify (sigc::mem_fun(*this, &ControlSurfacesOptions::edit_clicked));
			_view.get_selection()->signal_changed().connect (sigc::mem_fun (*this, &ControlSurfacesOptions::selection_changed));
		}

		void parameter_changed (std::string const &)
		{

		}

		void set_state_from_config ()
		{
			_store->clear ();

			ControlProtocolManager& m = ControlProtocolManager::instance ();
			for (list<ControlProtocolInfo*>::iterator i = m.control_protocol_info.begin(); i != m.control_protocol_info.end(); ++i) {

				if (!(*i)->mandatory) {
					TreeModel::Row r = *_store->append ();
					r[_model.name] = (*i)->name;
					r[_model.enabled] = ((*i)->protocol || (*i)->requested);
					r[_model.feedback] = ((*i)->protocol && (*i)->protocol->get_feedback ());
					r[_model.protocol_info] = *i;
				}
			}
		}

	private:

		void protocol_status_changed (ControlProtocolInfo* cpi) {
			/* find the row */
			TreeModel::Children rows = _store->children();

			for (TreeModel::Children::iterator x = rows.begin(); x != rows.end(); ++x) {
				string n = ((*x)[_model.name]);

				if ((*x)[_model.protocol_info] == cpi) {
					_ignore_view_change++;
					(*x)[_model.enabled] = (cpi->protocol || cpi->requested);
					_ignore_view_change--;
					break;
				}
			}
		}

		void selection_changed ()
		{
			//enable the Edit button when a row is selected for editing
			TreeModel::Row row = *(_view.get_selection()->get_selected());
			if (row && row[_model.enabled])
				edit_button->set_sensitive (true);
			else
				edit_button->set_sensitive (false);
		}

		void view_changed (TreeModel::Path const &, TreeModel::iterator const & i)
		{
			TreeModel::Row r = *i;

			if (_ignore_view_change) {
				return;
			}

			ControlProtocolInfo* cpi = r[_model.protocol_info];
			if (!cpi) {
				return;
			}

			bool const was_enabled = (cpi->protocol != 0);
			bool const is_enabled = r[_model.enabled];


			if (was_enabled != is_enabled) {

				if (!was_enabled) {
					ControlProtocolManager::instance().activate (*cpi);
				} else {
					ControlProtocolManager::instance().deactivate (*cpi);
				}
			}

			bool const was_feedback = (cpi->protocol && cpi->protocol->get_feedback ());
			bool const is_feedback = r[_model.feedback];

			if (was_feedback != is_feedback && cpi->protocol) {
				cpi->protocol->set_feedback (is_feedback);
			}
			selection_changed ();
		}

		void edit_btn_clicked ()
		{
			std::string name;
			ControlProtocolInfo* cpi;
			TreeModel::Row row;

			row = *(_view.get_selection()->get_selected());
			if (!row[_model.enabled]) {
				return;
			}
			cpi = row[_model.protocol_info];
			if (!cpi || !cpi->protocol || !cpi->protocol->has_editor ()) {
				return;
			}
			Box* box = (Box*) cpi->protocol->get_gui ();
			if (!box) {
				return;
			}
			if (box->get_parent()) {
				static_cast<ArdourWindow*>(box->get_parent())->present();
				return;
			}
			WindowTitle title (Glib::get_application_name());
			title += row[_model.name];
			title += _("Configuration");
			/* once created, the window is managed by the surface itself (as ->get_parent())
			 * Surface's tear_down_gui() is called on session close, when de-activating
			 * or re-initializing a surface.
			 * tear_down_gui() hides an deletes the Window if it exists.
			 */
			ArdourWindow* win = new ArdourWindow (*((Gtk::Window*) _view.get_toplevel()), title.get_string());
			win->set_title ("Control Protocol Options");
			win->add (*box);
			box->show ();
			win->present ();
		}

		void edit_clicked (GdkEventButton* ev)
		{
			if (ev->type != GDK_2BUTTON_PRESS) {
				return;
			}

			edit_btn_clicked();
		}

		class ControlSurfacesModelColumns : public TreeModelColumnRecord
	{
		public:

			ControlSurfacesModelColumns ()
			{
				add (name);
				add (enabled);
				add (feedback);
				add (protocol_info);
			}

			TreeModelColumn<string> name;
			TreeModelColumn<bool> enabled;
			TreeModelColumn<bool> feedback;
			TreeModelColumn<ControlProtocolInfo*> protocol_info;
	};

		Glib::RefPtr<ListStore> _store;
		ControlSurfacesModelColumns _model;
		TreeView _view;
		PBD::ScopedConnection protocol_status_connection;
		uint32_t _ignore_view_change;
		Gtk::Button* edit_button;
};

class VideoTimelineOptions : public OptionEditorPageBox, public OptionEditorPage
{
	public:
		VideoTimelineOptions (RCConfiguration* c)
			: _rc_config (c)
			, _show_video_export_info_button (_("Show Video Export Info before export"))
			, _show_video_server_dialog_button (_("Show Video Server Startup Dialog"))
			, _video_advanced_setup_button (_("Advanced Setup (remote video server)"))
			, _xjadeo_browse_button (_("Browse..."))
		{
			Table* t = &table;

			Label* l = manage (new Label (string_compose ("<b>%1</b>", _("Video Server"))));
			l->set_use_markup (true);
			l->set_alignment (0, 0.5);
			t->attach (*l, 0, 4, 0, 1, EXPAND | FILL, FILL | EXPAND, 0, 0);

			t->attach (_show_video_export_info_button, 1, 4, 1, 2);
			_show_video_export_info_button.signal_toggled().connect (sigc::mem_fun (*this, &VideoTimelineOptions::show_video_export_info_toggled));
			Gtkmm2ext::UI::instance()->set_tip (_show_video_export_info_button,
					_("<b>When enabled</b> an information window with details is displayed before the video-export dialog."));

			t->attach (_show_video_server_dialog_button, 1, 4, 2, 3);
			_show_video_server_dialog_button.signal_toggled().connect (sigc::mem_fun (*this, &VideoTimelineOptions::show_video_server_dialog_toggled));
			Gtkmm2ext::UI::instance()->set_tip (_show_video_server_dialog_button,
					_("<b>When enabled</b> the video server is never launched automatically without confirmation"));

			t->attach (_video_advanced_setup_button, 1, 4, 3, 4, FILL);
			_video_advanced_setup_button.signal_toggled().connect (sigc::mem_fun (*this, &VideoTimelineOptions::video_advanced_setup_toggled));
			Gtkmm2ext::UI::instance()->set_tip (_video_advanced_setup_button,
					_("<b>When enabled</b> you can speficify a custom video-server URL and docroot. - Do not enable this option unless you know what you are doing."));

			l = manage (new Label (_("Video Server URL:")));
			l->set_alignment (0, 0.5);
			t->attach (*l, 1, 2, 4, 5, FILL);
			t->attach (_video_server_url_entry, 2, 4, 4, 5, FILL);
			Gtkmm2ext::UI::instance()->set_tip (_video_server_url_entry,
					_("Base URL of the video-server including http prefix. This is usually 'http://hostname.example.org:1554/' and defaults to 'http://localhost:1554/' when the video-server is running locally"));

			l = manage (new Label (_("Video Folder:")));
			l->set_alignment (0, 0.5);
			t->attach (*l, 1, 2, 5, 6, FILL);
			t->attach (_video_server_docroot_entry, 2, 4, 5, 6);
			Gtkmm2ext::UI::instance()->set_tip (_video_server_docroot_entry,
					_("Local path to the video-server document-root. Only files below this directory will be accessible by the video-server. If the server run on a remote host, it should point to a network mounted folder of the server's docroot or be left empty if it is unvailable. It is used for the local video-monitor and file-browsing when opening/adding a video file."));

			l = manage (new Label (""));
			t->attach (*l, 0, 4, 6, 7, EXPAND | FILL);

			l = manage (new Label (string_compose ("<b>%1</b>", _("Video Monitor"))));
			l->set_use_markup (true);
			l->set_alignment (0, 0.5);
			t->attach (*l, 0, 4, 7, 8, EXPAND | FILL);

			l = manage (new Label (string_compose (_("Custom Path to Video Monitor (%1) - leave empty for default:"),
#ifdef __APPLE__
							"Jadeo.app"
#elif defined PLATFORM_WINDOWS
							"xjadeo.exe"
#else
							"xjadeo"
#endif
							)));
			l->set_alignment (0, 0.5);
			t->attach (*l, 1, 4, 8, 9, FILL);
			t->attach (_custom_xjadeo_path, 2, 3, 9, 10, EXPAND|FILL);
			Gtkmm2ext::UI::instance()->set_tip (_custom_xjadeo_path, _("Set a custom path to the Video Monitor Executable, changing this requires a restart."));
			t->attach (_xjadeo_browse_button, 3, 4, 9, 10, FILL);

			_video_server_url_entry.signal_changed().connect (sigc::mem_fun(*this, &VideoTimelineOptions::server_url_changed));
			_video_server_url_entry.signal_activate().connect (sigc::mem_fun(*this, &VideoTimelineOptions::server_url_changed));
			_video_server_docroot_entry.signal_changed().connect (sigc::mem_fun(*this, &VideoTimelineOptions::server_docroot_changed));
			_video_server_docroot_entry.signal_activate().connect (sigc::mem_fun(*this, &VideoTimelineOptions::server_docroot_changed));
			_custom_xjadeo_path.signal_changed().connect (sigc::mem_fun (*this, &VideoTimelineOptions::custom_xjadeo_path_changed));
			_xjadeo_browse_button.signal_clicked ().connect (sigc::mem_fun (*this, &VideoTimelineOptions::xjadeo_browse_clicked));

			// xjadeo-path is a UIConfig parameter
			UIConfiguration::instance().ParameterChanged.connect (sigc::mem_fun (*this, &VideoTimelineOptions::parameter_changed));

			_box->pack_start (*t, true, true);
		}

		void server_url_changed ()
		{
			_rc_config->set_video_server_url (_video_server_url_entry.get_text());
		}

		void server_docroot_changed ()
		{
			_rc_config->set_video_server_docroot (_video_server_docroot_entry.get_text());
		}

		void show_video_export_info_toggled ()
		{
			bool const x = _show_video_export_info_button.get_active ();
			_rc_config->set_show_video_export_info (x);
		}

		void show_video_server_dialog_toggled ()
		{
			bool const x = _show_video_server_dialog_button.get_active ();
			_rc_config->set_show_video_server_dialog (x);
		}

		void video_advanced_setup_toggled ()
		{
			bool const x = _video_advanced_setup_button.get_active ();
			_rc_config->set_video_advanced_setup(x);
		}

		void custom_xjadeo_path_changed ()
		{
			UIConfiguration::instance().set_xjadeo_binary (_custom_xjadeo_path.get_text());
		}

		void xjadeo_browse_clicked ()
		{
			Gtk::FileChooserDialog dialog(_("Set Video Monitor Executable"), Gtk::FILE_CHOOSER_ACTION_OPEN);
			dialog.set_filename (UIConfiguration::instance().get_xjadeo_binary());
			dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
			dialog.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);
			if (dialog.run () == Gtk::RESPONSE_OK) {
				const std::string& filename = dialog.get_filename();
				if (!filename.empty() && (
#ifdef __APPLE__
							Glib::file_test (filename + "/Contents/MacOS/xjadeo", Glib::FILE_TEST_EXISTS|Glib::FILE_TEST_IS_EXECUTABLE) ||
#endif
							Glib::file_test (filename, Glib::FILE_TEST_EXISTS|Glib::FILE_TEST_IS_EXECUTABLE)
							)) {
					UIConfiguration::instance().set_xjadeo_binary (filename);
				}
			}
		}

		void parameter_changed (string const & p)
		{
			if (p == "video-server-url") {
				_video_server_url_entry.set_text (_rc_config->get_video_server_url());
			} else if (p == "video-server-docroot") {
				_video_server_docroot_entry.set_text (_rc_config->get_video_server_docroot());
			} else if (p == "show-video-export-info") {
				bool const x = _rc_config->get_show_video_export_info();
				_show_video_export_info_button.set_active (x);
			} else if (p == "show-video-server-dialog") {
				bool const x = _rc_config->get_show_video_server_dialog();
				_show_video_server_dialog_button.set_active (x);
			} else if (p == "video-advanced-setup") {
				bool const x = _rc_config->get_video_advanced_setup();
				_video_advanced_setup_button.set_active(x);
				_video_server_docroot_entry.set_sensitive(x);
				_video_server_url_entry.set_sensitive(x);
			} else if (p == "xjadeo-binary") {
				_custom_xjadeo_path.set_text (UIConfiguration::instance().get_xjadeo_binary());
			}
		}

		void set_state_from_config ()
		{
			parameter_changed ("video-server-url");
			parameter_changed ("video-server-docroot");
			parameter_changed ("video-monitor-setup-dialog");
			parameter_changed ("show-video-export-info");
			parameter_changed ("show-video-server-dialog");
			parameter_changed ("video-advanced-setup");
			parameter_changed ("xjadeo-binary");
		}

	private:
		RCConfiguration* _rc_config;
		Entry _video_server_url_entry;
		Entry _video_server_docroot_entry;
		Entry _custom_xjadeo_path;
		CheckButton _show_video_export_info_button;
		CheckButton _show_video_server_dialog_button;
		CheckButton _video_advanced_setup_button;
		Button _xjadeo_browse_button;
};

class ColumVisibilityOption : public Option
{
	public:
	ColumVisibilityOption (string id, string name, uint32_t n_col, sigc::slot<uint32_t> get, sigc::slot<bool, uint32_t> set)
		: Option (id, name)
		, _heading (name)
		, _n_col (n_col)
		, _get (get)
		, _set (set)
	{
		cb = (CheckButton**) malloc (sizeof (CheckButton*) * n_col);
		for (uint32_t i = 0; i < n_col; ++i) {
			CheckButton* col = manage (new CheckButton (string_compose (_("Column %1"), i + 1)));
			col->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &ColumVisibilityOption::column_toggled), i));
			_hbox.pack_start (*col);
			cb[i] = col;
		}
		parameter_changed (id);
	}

	~ColumVisibilityOption () {
		free (cb);
	}

	Gtk::Widget& tip_widget() { return _hbox; }

	void set_state_from_config ()
	{
		uint32_t c = _get();
		for (uint32_t i = 0; i < _n_col; ++i) {
			bool en = (c & (1<<i)) ? true : false;
			if (cb[i]->get_active () != en) {
				cb[i]->set_active (en);
			}
		}
	}

	void add_to_page (OptionEditorPage* p)
	{
		_heading.add_to_page (p);
		add_widget_to_page (p, &_hbox);
	}
	private:

	void column_toggled (int b) {
		uint32_t c = _get();
		uint32_t cc = c;
		if (cb[b]->get_active ()) {
			c |= (1<<b);
		} else {
			c &= ~(1<<b);
		}
		if (cc != c) {
			_set (c);
		}
	}

	HBox _hbox;
	OptionEditorHeading _heading;

	CheckButton** cb;
	uint32_t _n_col;
	sigc::slot<uint32_t> _get;
	sigc::slot<bool, uint32_t> _set;
};


/** A class which allows control of visibility of some editor components usign
 *  a VisibilityGroup.  The caller should pass in a `dummy' VisibilityGroup
 *  which has the correct members, but with null widget pointers.  This
 *  class allows the user to set visibility of the members, the details
 *  of which are stored in a configuration variable which can be watched
 *  by parts of the editor that actually contain the widgets whose visibility
 *  is being controlled.
 */

class VisibilityOption : public Option
{
public:
	/** @param name User-visible name for this group.
	 *  @param g `Dummy' VisibilityGroup (as described above).
	 *  @param get Method to get the value of the appropriate configuration variable.
	 *  @param set Method to set the value of the appropriate configuration variable.
	 */
	VisibilityOption (string name, VisibilityGroup* g, sigc::slot<string> get, sigc::slot<bool, string> set)
		: Option (g->get_state_name(), name)
		, _heading (name)
		, _visibility_group (g)
		, _get (get)
		, _set (set)
	{
		/* Watch for changes made by the user to our members */
		_visibility_group->VisibilityChanged.connect_same_thread (
			_visibility_group_connection, sigc::bind (&VisibilityOption::changed, this)
			);
	}

	void set_state_from_config ()
	{
		/* Set our state from the current configuration */
		_visibility_group->set_state (_get ());
	}

	void add_to_page (OptionEditorPage* p)
	{
		_heading.add_to_page (p);
		add_widget_to_page (p, _visibility_group->list_view ());
	}

	Gtk::Widget& tip_widget() { return *_visibility_group->list_view (); }

private:
	void changed ()
	{
		/* The user has changed something, so reflect this change
		   in the RCConfiguration.
		*/
		_set (_visibility_group->get_state_value ());
	}

	OptionEditorHeading _heading;
	VisibilityGroup* _visibility_group;
	sigc::slot<std::string> _get;
	sigc::slot<bool, std::string> _set;
	PBD::ScopedConnection _visibility_group_connection;
};


class MidiPortOptions : public OptionEditorPageBox, public sigc::trackable
{
	public:
		MidiPortOptions() {

			setup_midi_port_view (midi_output_view, false);
			setup_midi_port_view (midi_input_view, true);


			_box->pack_start (*manage (new Label("")), false, false);
			input_label.set_markup (string_compose ("<span size=\"large\" weight=\"bold\">%1</span>", _("MIDI Inputs")));
			_box->pack_start (input_label, false, false);

			Gtk::ScrolledWindow* scroller = manage (new Gtk::ScrolledWindow);
			scroller->add (midi_input_view);
			scroller->set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
			scroller->set_size_request (-1, 180);
			_box->pack_start (*scroller, false, false);

			_box->pack_start (*manage (new Label("")), false, false);
			output_label.set_markup (string_compose ("<span size=\"large\" weight=\"bold\">%1</span>", _("MIDI Outputs")));
			_box->pack_start (output_label, false, false);

			scroller = manage (new Gtk::ScrolledWindow);
			scroller->add (midi_output_view);
			scroller->set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
			scroller->set_size_request (-1, 180);
			_box->pack_start (*scroller, false, false);

			midi_output_view.show ();
			midi_input_view.show ();

			_box->signal_show().connect (sigc::mem_fun (*this, &MidiPortOptions::on_show));
		}

		void parameter_changed (string const&) {}
		void set_state_from_config() {}

		void on_show () {
			refill ();

			AudioEngine::instance()->PortRegisteredOrUnregistered.connect (connections,
					invalidator (*this),
					boost::bind (&MidiPortOptions::refill, this),
					gui_context());
			AudioEngine::instance()->MidiPortInfoChanged.connect (connections,
					invalidator (*this),
					boost::bind (&MidiPortOptions::refill, this),
					gui_context());
			AudioEngine::instance()->MidiSelectionPortsChanged.connect (connections,
					invalidator (*this),
					boost::bind (&MidiPortOptions::refill, this),
					gui_context());
		}

		void refill () {

			if (refill_midi_ports (true, midi_input_view)) {
				input_label.show ();
			} else {
				input_label.hide ();
			}
			if (refill_midi_ports (false, midi_output_view)) {
				output_label.show ();
			} else {
				output_label.hide ();
			}
		}

	private:
		PBD::ScopedConnectionList connections;

		/* MIDI port management */
		struct MidiPortColumns : public Gtk::TreeModel::ColumnRecord {

			MidiPortColumns () {
				add (pretty_name);
				add (music_data);
				add (control_data);
				add (selection);
				add (name);
				add (filler);
			}

			Gtk::TreeModelColumn<std::string> pretty_name;
			Gtk::TreeModelColumn<bool> music_data;
			Gtk::TreeModelColumn<bool> control_data;
			Gtk::TreeModelColumn<bool> selection;
			Gtk::TreeModelColumn<std::string> name;
			Gtk::TreeModelColumn<std::string> filler;
		};

		MidiPortColumns midi_port_columns;
		Gtk::TreeView midi_input_view;
		Gtk::TreeView midi_output_view;
		Gtk::Label input_label;
		Gtk::Label output_label;

		void setup_midi_port_view (Gtk::TreeView&, bool with_selection);
		bool refill_midi_ports (bool for_input, Gtk::TreeView&);
		void pretty_name_edit (std::string const & path, std::string const & new_text, Gtk::TreeView*);
		void midi_music_column_toggled (std::string const & path, Gtk::TreeView*);
		void midi_control_column_toggled (std::string const & path, Gtk::TreeView*);
		void midi_selection_column_toggled (std::string const & path, Gtk::TreeView*);
};

void
MidiPortOptions::setup_midi_port_view (Gtk::TreeView& view, bool with_selection)
{
	int pretty_name_column;
	int music_column;
	int control_column;
	int selection_column;
	TreeViewColumn* col;
	Gtk::Label* l;

	pretty_name_column = view.append_column_editable (_("Name (click to edit)"), midi_port_columns.pretty_name) - 1;

	col = manage (new TreeViewColumn ("", midi_port_columns.music_data));
	col->set_alignment (ALIGN_CENTER);
	l = manage (new Label (_("Music Data")));
	set_tooltip (*l, string_compose (_("If ticked, %1 will consider this port to be a source of music performance data."), PROGRAM_NAME));
	col->set_widget (*l);
	l->show ();
	music_column = view.append_column (*col) - 1;

	col = manage (new TreeViewColumn ("", midi_port_columns.control_data));
	col->set_alignment (ALIGN_CENTER);
	l = manage (new Label (_("Control Data")));
	set_tooltip (*l, string_compose (_("If ticked, %1 will consider this port to be a source of control data."), PROGRAM_NAME));
	col->set_widget (*l);
	l->show ();
	control_column = view.append_column (*col) - 1;

	if (with_selection) {
		col = manage (new TreeViewColumn (_("Follow Selection"), midi_port_columns.selection));
		selection_column = view.append_column (*col) - 1;
		l = manage (new Label (_("Follow Selection")));
		set_tooltip (*l, string_compose (_("If ticked, and \"MIDI input follows selection\" is enabled,\n%1 will automatically connect the first selected MIDI track to this port.\n"), PROGRAM_NAME));
		col->set_widget (*l);
		l->show ();
	}

	/* filler column so that the last real column doesn't expand */
	view.append_column ("", midi_port_columns.filler);

	CellRendererText* pretty_name_cell = dynamic_cast<CellRendererText*> (view.get_column_cell_renderer (pretty_name_column));
	pretty_name_cell->property_editable() = true;
	pretty_name_cell->signal_edited().connect (sigc::bind (sigc::mem_fun (*this, &MidiPortOptions::pretty_name_edit), &view));

	CellRendererToggle* toggle_cell;

	toggle_cell = dynamic_cast<CellRendererToggle*> (view.get_column_cell_renderer (music_column));
	toggle_cell->property_activatable() = true;
	toggle_cell->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &MidiPortOptions::midi_music_column_toggled), &view));

	toggle_cell = dynamic_cast<CellRendererToggle*> (view.get_column_cell_renderer (control_column));
	toggle_cell->property_activatable() = true;
	toggle_cell->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &MidiPortOptions::midi_control_column_toggled), &view));

	if (with_selection) {
		toggle_cell = dynamic_cast<CellRendererToggle*> (view.get_column_cell_renderer (selection_column));
		toggle_cell->property_activatable() = true;
		toggle_cell->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &MidiPortOptions::midi_selection_column_toggled), &view));
	}

	view.get_selection()->set_mode (SELECTION_NONE);
	view.set_tooltip_column (4); /* port "real" name */
	view.get_column(0)->set_resizable (true);
	view.get_column(0)->set_expand (true);
}

bool
MidiPortOptions::refill_midi_ports (bool for_input, Gtk::TreeView& view)
{
	using namespace ARDOUR;

	std::vector<string> ports;

	AudioEngine::instance()->get_known_midi_ports (ports);

	if (ports.empty()) {
		view.hide ();
		return false;
	}

	Glib::RefPtr<ListStore> model = Gtk::ListStore::create (midi_port_columns);

	for (vector<string>::const_iterator s = ports.begin(); s != ports.end(); ++s) {

		if (AudioEngine::instance()->port_is_mine (*s)) {
			continue;
		}

		PortManager::MidiPortInformation mpi (AudioEngine::instance()->midi_port_information (*s));

		if (mpi.pretty_name.empty()) {
			/* vanished since get_known_midi_ports() */
			continue;
		}

		if (for_input != mpi.input) {
			continue;
		}

		TreeModel::Row row = *(model->append());

		row[midi_port_columns.pretty_name] = mpi.pretty_name;
		row[midi_port_columns.music_data] = mpi.properties & MidiPortMusic;
		row[midi_port_columns.control_data] = mpi.properties & MidiPortControl;
		row[midi_port_columns.selection] = mpi.properties & MidiPortSelection;
		row[midi_port_columns.name] = *s;
	}

	view.set_model (model);

	return true;
}

void
MidiPortOptions::midi_music_column_toggled (string const & path, TreeView* view)
{
	TreeIter iter = view->get_model()->get_iter (path);

	if (!iter) {
		return;
	}

	bool new_value = ! bool ((*iter)[midi_port_columns.music_data]);

	/* don't reset model - wait for MidiPortInfoChanged signal */

	if (new_value) {
		ARDOUR::AudioEngine::instance()->add_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortMusic);
	} else {
		ARDOUR::AudioEngine::instance()->remove_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortMusic);
	}
}

void
MidiPortOptions::midi_control_column_toggled (string const & path, TreeView* view)
{
	TreeIter iter = view->get_model()->get_iter (path);

	if (!iter) {
		return;
	}

	bool new_value = ! bool ((*iter)[midi_port_columns.control_data]);

	/* don't reset model - wait for MidiPortInfoChanged signal */

	if (new_value) {
		ARDOUR::AudioEngine::instance()->add_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortControl);
	} else {
		ARDOUR::AudioEngine::instance()->remove_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortControl);
	}
}

void
MidiPortOptions::midi_selection_column_toggled (string const & path, TreeView* view)
{
	TreeIter iter = view->get_model()->get_iter (path);

	if (!iter) {
		return;
	}

	bool new_value = ! bool ((*iter)[midi_port_columns.selection]);

	/* don't reset model - wait for MidiSelectionPortsChanged signal */

	if (new_value) {
		ARDOUR::AudioEngine::instance()->add_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortSelection);
	} else {
		ARDOUR::AudioEngine::instance()->remove_midi_port_flags ((*iter)[midi_port_columns.name], MidiPortSelection);
	}
}

void
MidiPortOptions::pretty_name_edit (std::string const & path, string const & new_text, Gtk::TreeView* view)
{
	TreeIter iter = view->get_model()->get_iter (path);

	if (!iter) {
		return;
	}

	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	if (backend) {
		ARDOUR::PortEngine::PortHandle ph = backend->get_port_by_name ((*iter)[midi_port_columns.name]);
		if (ph) {
			backend->set_port_property (ph, "http://jackaudio.org/metadata/pretty-name", new_text, "");
			(*iter)[midi_port_columns.pretty_name] = new_text;
		}
	}
}

/*============*/


RCOptionEditor::RCOptionEditor ()
	: OptionEditorContainer (Config, string_compose (_("%1 Preferences"), PROGRAM_NAME))
	, Tabbable (*this, _("Preferences")) /* pack self-as-vbox into tabbable */
	, _rc_config (Config)
	, _mixer_strip_visibility ("mixer-element-visibility")
{
	UIConfiguration::instance().ParameterChanged.connect (sigc::mem_fun (*this, &RCOptionEditor::parameter_changed));

	/* MISC */

	uint32_t hwcpus = hardware_concurrency ();
	BoolOption* bo;
	BoolComboOption* bco;

	if (hwcpus > 1) {
		add_option (_("Misc"), new OptionEditorHeading (_("DSP CPU Utilization")));

		ComboOption<int32_t>* procs = new ComboOption<int32_t> (
				"processor-usage",
				_("Signal processing uses"),
				sigc::mem_fun (*_rc_config, &RCConfiguration::get_processor_usage),
				sigc::mem_fun (*_rc_config, &RCConfiguration::set_processor_usage)
				);

		procs->add (-1, _("all but one processor"));
		procs->add (0, _("all available processors"));

		for (uint32_t i = 1; i <= hwcpus; ++i) {
			procs->add (i, string_compose (P_("%1 processor", "%1 processors", i), i));
		}

		procs->set_note (string_compose (_("This setting will only take effect when %1 is restarted."), PROGRAM_NAME));

		add_option (_("Misc"), procs);
	}

	add_option (_("Misc"), new OptionEditorHeading (S_("Options|Undo")));

	add_option (_("Misc"), new UndoOptions (_rc_config));

	add_option (_("Misc"),
	     new BoolOption (
		     "verify-remove-last-capture",
		     _("Verify removal of last capture"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_verify_remove_last_capture),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_verify_remove_last_capture)
		     ));

	add_option (_("Misc"), new OptionEditorHeading (_("Session Management")));

	add_option (_("Misc"),
	     new BoolOption (
		     "periodic-safety-backups",
		     _("Make periodic backups of the session file"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_periodic_safety_backups),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_periodic_safety_backups)
		     ));

	add_option (_("Misc"),
	     new BoolOption (
		     "only-copy-imported-files",
		     _("Always copy imported files"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_only_copy_imported_files),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_only_copy_imported_files)
		     ));

	add_option (_("Misc"), new DirectoryOption (
			    X_("default-session-parent-dir"),
			    _("Default folder for new sessions:"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_default_session_parent_dir),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_default_session_parent_dir)
			    ));

	add_option (_("Misc"),
	     new SpinOption<uint32_t> (
		     "max-recent-sessions",
		     _("Maximum number of recent sessions"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_max_recent_sessions),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_max_recent_sessions),
		     0, 1000, 1, 20
		     ));

	add_option (_("Misc/Click"), new OptionEditorHeading (_("Click")));

	add_option (_("Misc/Click"), new ClickOptions (_rc_config));

	add_option (_("Misc"), new OptionEditorHeading (_("Automation")));

	add_option (_("Misc"),
	     new SpinOption<double> (
		     "automation-thinning-factor",
		     _("Thinning factor (larger value => less data)"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_automation_thinning_factor),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_automation_thinning_factor),
		     0, 1000, 1, 20
		     ));

	add_option (_("Misc"),
	     new SpinOption<double> (
		     "automation-interval-msecs",
		     _("Automation sampling interval (milliseconds)"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_automation_interval_msecs),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_automation_interval_msecs),
		     1, 1000, 1, 20
		     ));

	add_option (_("Misc"), new OptionEditorHeading (_("Tempo")));

	BoolOption* tsf;

	tsf = new BoolOption (
		"allow-non-quarter-pulse",
		_("Allow non quarter-note pulse"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_allow_non_quarter_pulse),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_allow_non_quarter_pulse)
		);
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(),
					    string_compose (_("<b>When enabled</b> %1 will allow tempo to be expressed in divisions per minute\n"
							      "<b>When disabled</b> %1 will only allow tempo to be expressed in quarter notes per minute"),
							    PROGRAM_NAME));
	add_option (_("Misc"), tsf);

	/* TRANSPORT */

	add_option (_("Transport"), new OptionEditorHeading (S_("Transport Options")));

	tsf = new BoolOption (
		     "latched-record-enable",
		     _("Keep record-enable engaged on stop"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_latched_record_enable),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_latched_record_enable)
		     );
	// Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(), _(""));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "loop-is-mode",
		     _("Play loop is a transport mode"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_loop_is_mode),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_loop_is_mode)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(),
					    (_("<b>When enabled</b> the loop button does not start playback but forces playback to always play the loop\n\n"
					       "<b>When disabled</b> the loop button starts playing the loop, but stop then cancels loop playback")));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "stop-recording-on-xrun",
		     _("Stop recording when an xrun occurs"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_stop_recording_on_xrun),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_stop_recording_on_xrun)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(),
					    string_compose (_("<b>When enabled</b> %1 will stop recording if an over- or underrun is detected by the audio engine"),
							    PROGRAM_NAME));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "create-xrun-marker",
		     _("Create markers where xruns occur"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_create_xrun_marker),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_create_xrun_marker)
		     );
	// Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(), _(""));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "stop-at-session-end",
		     _("Stop at the end of the session"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_stop_at_session_end),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_stop_at_session_end)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(),
					    string_compose (_("<b>When enabled</b> if %1 is <b>not recording</b>, it will stop the transport "
							      "when it reaches the current session end marker\n\n"
							      "<b>When disabled</b> %1 will continue to roll past the session end marker at all times"),
							    PROGRAM_NAME));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "seamless-loop",
		     _("Do seamless looping (not possible when slaved to MTC, LTC etc)"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_seamless_loop),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_seamless_loop)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(),
					    string_compose (_("<b>When enabled</b> this will loop by reading ahead and wrapping around at the loop point, "
							      "preventing any need to do a transport locate at the end of the loop\n\n"
							      "<b>When disabled</b> looping is done by locating back to the start of the loop when %1 reaches the end "
							      "which will often cause a small click or delay"), PROGRAM_NAME));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "disable-disarm-during-roll",
		     _("Disable per-track record disarm while rolling"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_disable_disarm_during_roll),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_disable_disarm_during_roll)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(), _("<b>When enabled</b> this will prevent you from accidentally stopping specific tracks recording during a take"));
	add_option (_("Transport"), tsf);

	tsf = new BoolOption (
		     "quieten_at_speed",
		     _("12dB gain reduction during fast-forward and fast-rewind"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_quieten_at_speed),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_quieten_at_speed)
		     );
	Gtkmm2ext::UI::instance()->set_tip (tsf->tip_widget(), _("This will reduce the unpleasant increase in perceived volume "
						   "that occurs when fast-forwarding or rewinding through some kinds of audio"));
	add_option (_("Transport"), tsf);

	ComboOption<float>* psc = new ComboOption<float> (
		     "preroll-seconds",
		     _("Preroll"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_preroll_seconds),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_preroll_seconds)
		     );
	Gtkmm2ext::UI::instance()->set_tip (psc->tip_widget(),
					    (_("The amount of preroll (in seconds) to apply when <b>Play with Preroll</b> is initiated.\n\n"
					       "If <b>Follow Edits</b> is enabled, the preroll is applied to the playhead position when a region is selected or trimmed.")));
	psc->add (0.0, _("0 (no pre-roll)"));
	psc->add (0.1, _("0.1 second"));
	psc->add (0.25, _("0.25 second"));
	psc->add (0.5, _("0.5 second"));
	psc->add (1.0, _("1.0 second"));
	psc->add (2.0, _("2.0 seconds"));
	add_option (_("Transport"), psc);

	add_option (_("Transport/Sync"), new OptionEditorHeading (S_("Synchronization and Slave Options")));

	_sync_source = new ComboOption<SyncSource> (
		"sync-source",
		_("External timecode source"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_sync_source),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_sync_source)
		);

	add_option (_("Transport/Sync"), _sync_source);

	_sync_framerate = new BoolOption (
		     "timecode-sync-frame-rate",
		     _("Match session video frame rate to external timecode"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_timecode_sync_frame_rate),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_timecode_sync_frame_rate)
		     );
	Gtkmm2ext::UI::instance()->set_tip
		(_sync_framerate->tip_widget(),
		 string_compose (_("This option controls the value of the video frame rate <i>while chasing</i> an external timecode source.\n\n"
				   "<b>When enabled</b> the session video frame rate will be changed to match that of the selected external timecode source.\n\n"
				   "<b>When disabled</b> the session video frame rate will not be changed to match that of the selected external timecode source."
				   "Instead the frame rate indication in the main clock will flash red and %1 will convert between the external "
				   "timecode standard and the session standard."), PROGRAM_NAME));

	add_option (_("Transport/Sync"), _sync_framerate);

	_sync_genlock = new BoolOption (
		"timecode-source-is-synced",
		_("Sync-lock timecode to clock (disable drift compensation)"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_timecode_source_is_synced),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_timecode_source_is_synced)
		);
	Gtkmm2ext::UI::instance()->set_tip
		(_sync_genlock->tip_widget(),
		 string_compose (_("<b>When enabled</b> %1 will never varispeed when slaved to external timecode. "
				   "Sync Lock indicates that the selected external timecode source shares clock-sync "
				   "(Black &amp; Burst, Wordclock, etc) with the audio interface. "
				   "This option disables drift compensation. The transport speed is fixed at 1.0. "
				   "Vari-speed LTC will be ignored and cause drift."
				   "\n\n"
				   "<b>When disabled</b> %1 will compensate for potential drift, regardless if the "
				   "timecode sources shares clock sync."
				  ), PROGRAM_NAME));


	add_option (_("Transport/Sync"), _sync_genlock);

	_sync_source_2997 = new BoolOption (
		"timecode-source-2997",
		_("Lock to 29.9700 fps instead of 30000/1001"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_timecode_source_2997),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_timecode_source_2997)
		);
	Gtkmm2ext::UI::instance()->set_tip
		(_sync_source_2997->tip_widget(),
		 _("<b>When enabled</b> the external timecode source is assumed to use 29.97 fps instead of 30000/1001.\n"
			 "SMPTE 12M-1999 specifies 29.97df as 30000/1001. The spec further mentions that "
			 "drop-frame timecode has an accumulated error of -86ms over a 24-hour period.\n"
			 "Drop-frame timecode would compensate exactly for a NTSC color frame rate of 30 * 0.9990 (ie 29.970000). "
			 "That is not the actual rate. However, some vendors use that rate - despite it being against the specs - "
			 "because the variant of using exactly 29.97 fps has zero timecode drift.\n"
			 ));

	add_option (_("Transport/Sync"), _sync_source_2997);

	add_option (_("Transport/Sync"), new OptionEditorHeading (S_("LTC Reader")));

	_ltc_port = new ComboStringOption (
		"ltc-source-port",
		_("LTC incoming port"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_ltc_source_port),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_ltc_source_port)
		);

	vector<string> physical_inputs;
	physical_inputs.push_back (_("None"));
	AudioEngine::instance()->get_physical_inputs (DataType::AUDIO, physical_inputs);
	_ltc_port->set_popdown_strings (physical_inputs);

	populate_sync_options ();
	AudioEngine::instance()->Running.connect (engine_started_connection, MISSING_INVALIDATOR, boost::bind (&RCOptionEditor::populate_sync_options, this), gui_context());

	add_option (_("Transport/Sync"), _ltc_port);

	// TODO; rather disable this button than not compile it..
	add_option (_("Transport/Sync"), new OptionEditorHeading (S_("LTC Generator")));

	add_option (_("Transport/Sync"),
		    new BoolOption (
			    "send-ltc",
			    _("Enable LTC generator"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_send_ltc),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_send_ltc)
			    ));

	_ltc_send_continuously = new BoolOption (
			    "ltc-send-continuously",
			    _("Send LTC while stopped"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_ltc_send_continuously),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_ltc_send_continuously)
			    );
	Gtkmm2ext::UI::instance()->set_tip
		(_ltc_send_continuously->tip_widget(),
		 string_compose (_("<b>When enabled</b> %1 will continue to send LTC information even when the transport (playhead) is not moving"), PROGRAM_NAME));
	add_option (_("Transport/Sync"), _ltc_send_continuously);

	_ltc_volume_adjustment = new Gtk::Adjustment(-18, -50, 0, .5, 5);
	_ltc_volume_adjustment->set_value (20 * log10(_rc_config->get_ltc_output_volume()));
	_ltc_volume_adjustment->signal_value_changed().connect (sigc::mem_fun (*this, &RCOptionEditor::ltc_generator_volume_changed));
	_ltc_volume_slider = new HSliderOption("ltcvol", _("LTC generator level"), *_ltc_volume_adjustment);

	Gtkmm2ext::UI::instance()->set_tip
		(_ltc_volume_slider->tip_widget(),
		 _("Specify the Peak Volume of the generated LTC signal in dBFS. A good value is  0dBu ^= -18dBFS in an EBU calibrated system"));

	add_option (_("Transport/Sync"), _ltc_volume_slider);

	/* EDITOR */

	add_option (_("Editor"), new OptionEditorHeading (_("Editor Settings")));

	add_option (_("Editor"),
	     new BoolOption (
		     "rubberbanding-snaps-to-grid",
		     _("Make rubberband selection rectangle snap to the grid"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_rubberbanding_snaps_to_grid),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_rubberbanding_snaps_to_grid)
		     ));

	bo = new BoolOption (
		     "name-new-markers",
		     _("Name new markers"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_name_new_markers),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_name_new_markers)
		);
	add_option (_("Editor"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(), _("If enabled, popup a dialog when a new marker is created to allow its name to be set as it is created."
								"\n\nYou can always rename markers by right-clicking on them"));

	add_option (S_("Editor"),
	     new BoolOption (
		     "draggable-playhead",
		     _("Allow dragging of playhead"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_draggable_playhead),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_draggable_playhead)
		     ));

if (!Profile->get_mixbus()) {
	add_option (_("Editor"),
		    new BoolOption (
			    "show-zoom-tools",
			    _("Show zoom toolbar (if torn off)"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_zoom_tools),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_zoom_tools)
			    ));

	add_option (_("Editor"),
		    new BoolOption (
			    "use-mouse-position-as-zoom-focus-on-scroll",
			    _("Always use mouse cursor position as zoom focus when zooming using mouse scroll wheel"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_use_mouse_position_as_zoom_focus_on_scroll),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_use_mouse_position_as_zoom_focus_on_scroll)
			    ));
}  // !mixbus

	add_option (_("Editor"),
		    new BoolOption (
			    "use-time-rulers-to-zoom-with-vertical-drag",
			    _("Use time rulers area to zoom when clicking and dragging vertically"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_use_time_rulers_to_zoom_with_vertical_drag),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_use_time_rulers_to_zoom_with_vertical_drag)
			    ));

	add_option (_("Editor"),
		    new BoolOption (
			    "use-double-click-to-zoom-to-selection",
			    _("Use double mouse click to zoom to selection"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_use_double_click_to_zoom_to_selection),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_use_double_click_to_zoom_to_selection)
			    ));

	add_option (_("Editor"),
		    new BoolOption (
			    "update-editor-during-summary-drag",
			    _("Update editor window during drags of the summary"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_update_editor_during_summary_drag),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_update_editor_during_summary_drag)
			    ));

	add_option (_("Editor"),
	    new BoolOption (
		    "autoscroll-editor",
		    _("Auto-scroll editor window when dragging near its edges"),
		    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_autoscroll_editor),
		    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_autoscroll_editor)
		    ));

	add_option (_("Editor"),
	     new BoolComboOption (
		     "show-region-gain-envelopes",
		     _("Show gain envelopes in audio regions"),
		     _("in all modes"),
		     _("only in Draw and Internal Edit modes"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_region_gain),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_region_gain)
		     ));

	add_option (_("Editor"), new OptionEditorHeading (_("Editor Behavior")));

	add_option (_("Editor"),
	     new BoolOption (
		     "automation-follows-regions",
		     _("Move relevant automation when audio regions are moved"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_automation_follows_regions),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_automation_follows_regions)
		     ));

	ComboOption<FadeShape>* fadeshape = new ComboOption<FadeShape> (
			"default-fade-shape",
			_("Default fade shape"),
			sigc::mem_fun (*_rc_config,
				&RCConfiguration::get_default_fade_shape),
			sigc::mem_fun (*_rc_config,
				&RCConfiguration::set_default_fade_shape)
			);

	fadeshape->add (FadeLinear,
			_("Linear (for highly correlated material)"));
	fadeshape->add (FadeConstantPower, _("Constant power"));
	fadeshape->add (FadeSymmetric, _("Symmetric"));
	fadeshape->add (FadeSlow, _("Slow"));
	fadeshape->add (FadeFast, _("Fast"));

	add_option (_("Editor"), fadeshape);

#if 1

	bco = new BoolComboOption (
		     "use-overlap-equivalency",
		     _("Regions in edit groups are edited together"),
		     _("whenever they overlap in time"),
		     _("only if they have identical length and position"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_use_overlap_equivalency),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_use_overlap_equivalency)
		     );

	add_option (_("Editor"), bco);

#endif
	ComboOption<LayerModel>* lm = new ComboOption<LayerModel> (
		"layer-model",
		_("Layering model"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_layer_model),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_layer_model)
		);

	lm->add (LaterHigher, _("later is higher"));
	lm->add (Manual, _("manual layering"));
	add_option (_("Editor"), lm);

	ComboOption<RegionSelectionAfterSplit> *rsas = new ComboOption<RegionSelectionAfterSplit> (
		    "region-selection-after-split",
		    _("After splitting selected regions, select"),
		    sigc::mem_fun (*_rc_config, &RCConfiguration::get_region_selection_after_split),
		    sigc::mem_fun (*_rc_config, &RCConfiguration::set_region_selection_after_split));

	// TODO: decide which of these modes are really useful
	rsas->add(None, _("no regions"));
	// rsas->add(NewlyCreatedLeft, _("newly-created regions before the split"));
	// rsas->add(NewlyCreatedRight, _("newly-created regions after the split"));
	rsas->add(NewlyCreatedBoth, _("newly-created regions"));
	// rsas->add(Existing, _("unmodified regions in the existing selection"));
	// rsas->add(ExistingNewlyCreatedLeft, _("existing selection and newly-created regions before the split"));
	// rsas->add(ExistingNewlyCreatedRight, _("existing selection and newly-created regions after the split"));
	rsas->add(ExistingNewlyCreatedBoth, _("existing selection and newly-created regions"));

	add_option (_("Editor"), rsas);

	add_option (_("Editor/Waveforms"), new OptionEditorHeading (_("Waveforms")));

if (!Profile->get_mixbus()) {
	add_option (_("Editor/Waveforms"),
	     new BoolOption (
		     "show-waveforms",
		     _("Show waveforms in regions"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_waveforms),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_waveforms)
		     ));
}  // !mixbus

	add_option (_("Editor/Waveforms"),
	     new BoolOption (
		     "show-waveforms-while-recording",
		     _("Show waveforms for audio while it is being recorded"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_waveforms_while_recording),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_waveforms_while_recording)
		     ));

	ComboOption<WaveformScale>* wfs = new ComboOption<WaveformScale> (
		"waveform-scale",
		_("Waveform scale"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_waveform_scale),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_waveform_scale)
		);

	wfs->add (Linear, _("linear"));
	wfs->add (Logarithmic, _("logarithmic"));

	add_option (_("Editor/Waveforms"), wfs);

	ComboOption<WaveformShape>* wfsh = new ComboOption<WaveformShape> (
		"waveform-shape",
		_("Waveform shape"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_waveform_shape),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_waveform_shape)
		);

	wfsh->add (Traditional, _("traditional"));
	wfsh->add (Rectified, _("rectified"));

	add_option (_("Editor/Waveforms"), wfsh);

	add_option (_("Editor/Waveforms"), new ClipLevelOptions ());


	/* AUDIO */

	add_option (_("Audio"), new OptionEditorHeading (_("Buffering")));

	add_option (_("Audio"), new BufferingOptions (_rc_config));

	add_option (_("Audio"), new OptionEditorHeading (_("Monitoring")));

	ComboOption<MonitorModel>* mm = new ComboOption<MonitorModel> (
		"monitoring-model",
		_("Record monitoring handled by"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_monitoring_model),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_monitoring_model)
		);

	if (AudioEngine::instance()->port_engine().can_monitor_input()) {
		mm->add (HardwareMonitoring, _("via Audio Driver"));
	}

	string prog (PROGRAM_NAME);
	boost::algorithm::to_lower (prog);
	mm->add (SoftwareMonitoring, string_compose (_("%1"), prog));
	mm->add (ExternalMonitoring, _("audio hardware"));

	add_option (_("Audio"), mm);

	add_option (_("Audio"),
	     new BoolOption (
		     "tape-machine-mode",
		     _("Tape machine mode"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_tape_machine_mode),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_tape_machine_mode)
		     ));

	add_option (_("Audio"), new OptionEditorHeading (_("Connection of tracks and busses")));
if (!Profile->get_mixbus()) {

	add_option (_("Audio"),
		    new BoolOption (
			    "auto-connect-standard-busses",
			    _("Auto-connect master/monitor busses"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_auto_connect_standard_busses),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_auto_connect_standard_busses)
			    ));

	ComboOption<AutoConnectOption>* iac = new ComboOption<AutoConnectOption> (
		"input-auto-connect",
		_("Connect track inputs"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_input_auto_connect),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_input_auto_connect)
		);

	iac->add (AutoConnectPhysical, _("automatically to physical inputs"));
	iac->add (ManualConnect, _("manually"));

	add_option (_("Audio"), iac);

	ComboOption<AutoConnectOption>* oac = new ComboOption<AutoConnectOption> (
		"output-auto-connect",
		_("Connect track and bus outputs"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_output_auto_connect),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_output_auto_connect)
		);

	oac->add (AutoConnectPhysical, _("automatically to physical outputs"));
	oac->add (AutoConnectMaster, _("automatically to master bus"));
	oac->add (ManualConnect, _("manually"));

	add_option (_("Audio"), oac);

	bo = new BoolOption (
			"strict-io",
			_("Use 'Strict-I/O' for new tracks or Busses"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_strict_io),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_strict_io)
			);

	add_option (_("Audio"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
			_("With strict-i/o enabled, Effect Processors will not modify the number of channels on a track. The number of output channels will always match the number of input channels."));

}  // !mixbus

	add_option (_("Audio"), new OptionEditorHeading (_("Denormals")));

	add_option (_("Audio"),
	     new BoolOption (
		     "denormal-protection",
		     _("Use DC bias to protect against denormals"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_denormal_protection),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_denormal_protection)
		     ));

	ComboOption<DenormalModel>* dm = new ComboOption<DenormalModel> (
		"denormal-model",
		_("Processor handling"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_denormal_model),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_denormal_model)
		);

	int dmsize = 1;
	dm->add (DenormalNone, _("no processor handling"));

	FPU* fpu = FPU::instance();

	if (fpu->has_flush_to_zero()) {
		++dmsize;
		dm->add (DenormalFTZ, _("use FlushToZero"));
	} else if (_rc_config->get_denormal_model() == DenormalFTZ) {
		_rc_config->set_denormal_model(DenormalNone);
	}

	if (fpu->has_denormals_are_zero()) {
		++dmsize;
		dm->add (DenormalDAZ, _("use DenormalsAreZero"));
	} else if (_rc_config->get_denormal_model() == DenormalDAZ) {
		_rc_config->set_denormal_model(DenormalNone);
	}

	if (fpu->has_flush_to_zero() && fpu->has_denormals_are_zero()) {
		++dmsize;
		dm->add (DenormalFTZDAZ, _("use FlushToZero and DenormalsAreZero"));
	} else if (_rc_config->get_denormal_model() == DenormalFTZDAZ) {
		_rc_config->set_denormal_model(DenormalNone);
	}

	if (dmsize == 1) {
		dm->set_sensitive(false);
	}

	add_option (_("Audio"), dm);

	add_option (_("Audio"), new OptionEditorHeading (_("Regions")));

	add_option (_("Audio"),
	     new BoolOption (
		     "auto-analyse-audio",
		     _("Enable automatic analysis of audio"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_auto_analyse_audio),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_auto_analyse_audio)
		     ));

	add_option (_("Audio"),
	     new BoolOption (
		     "replicate-missing-region-channels",
		     _("Replicate missing region channels"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_replicate_missing_region_channels),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_replicate_missing_region_channels)
		     ));

	/* SOLO AND MUTE */

	add_option (_("Solo & mute"), new OptionEditorHeading (_("Solo")));

	_solo_control_is_listen_control = new BoolOption (
		"solo-control-is-listen-control",
		_("Solo controls are Listen controls"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_solo_control_is_listen_control),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_solo_control_is_listen_control)
		);

	add_option (_("Solo & mute"), _solo_control_is_listen_control);

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "exclusive-solo",
		     _("Exclusive solo"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_exclusive_solo),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_exclusive_solo)
		     ));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "show-solo-mutes",
		     _("Show solo muting"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_show_solo_mutes),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_show_solo_mutes)
		     ));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "solo-mute-override",
		     _("Soloing overrides muting"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_solo_mute_override),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_solo_mute_override)
		     ));

	add_option (_("Solo & mute"),
	     new FaderOption (
		     "solo-mute-gain",
		     _("Solo-in-place mute cut (dB)"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_solo_mute_gain),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_solo_mute_gain)
		     ));

	_listen_position = new ComboOption<ListenPosition> (
		"listen-position",
		_("Listen Position"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_listen_position),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_listen_position)
		);

	_listen_position->add (AfterFaderListen, _("after-fader (AFL)"));
	_listen_position->add (PreFaderListen, _("pre-fader (PFL)"));

	add_option (_("Solo & mute"), _listen_position);

	ComboOption<PFLPosition>* pp = new ComboOption<PFLPosition> (
		"pfl-position",
		_("PFL signals come from"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_pfl_position),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_pfl_position)
		);

	pp->add (PFLFromBeforeProcessors, _("before pre-fader processors"));
	pp->add (PFLFromAfterProcessors, _("pre-fader but after pre-fader processors"));

	add_option (_("Solo & mute"), pp);

	ComboOption<AFLPosition>* pa = new ComboOption<AFLPosition> (
		"afl-position",
		_("AFL signals come from"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_afl_position),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_afl_position)
		);

	pa->add (AFLFromBeforeProcessors, _("immediately post-fader"));
	pa->add (AFLFromAfterProcessors, _("after post-fader processors (before pan)"));

	add_option (_("Solo & mute"), pa);

	add_option (_("Solo & mute"), new OptionEditorHeading (_("Default track / bus muting options")));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "mute-affects-pre-fader",
		     _("Mute affects pre-fader sends"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mute_affects_pre_fader),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mute_affects_pre_fader)
		     ));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "mute-affects-post-fader",
		     _("Mute affects post-fader sends"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mute_affects_post_fader),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mute_affects_post_fader)
		     ));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "mute-affects-control-outs",
		     _("Mute affects control outputs"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mute_affects_control_outs),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mute_affects_control_outs)
		     ));

	add_option (_("Solo & mute"),
	     new BoolOption (
		     "mute-affects-main-outs",
		     _("Mute affects main outputs"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mute_affects_main_outs),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mute_affects_main_outs)
		     ));


if (!ARDOUR::Profile->get_mixbus()) {
	add_option (_("Solo & mute"), new OptionEditorHeading (_("Send Routing")));
	add_option (_("Solo & mute"),
	     new BoolOption (
		     "link-send-and-route-panner",
		     _("Link panners of Aux and External Sends with main panner by default"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_link_send_and_route_panner),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_link_send_and_route_panner)
		     ));
}

	add_option (_("MIDI"), new OptionEditorHeading (_("MIDI Preferences")));

	add_option (_("MIDI"),
		    new SpinOption<float> (
			    "midi-readahead",
			    _("MIDI read-ahead time (seconds)"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_midi_readahead),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_midi_readahead),
			    0.1, 10, 0.1, 1,
			    "", 1.0, 1
			    ));

	add_option (_("MIDI"),
	     new SpinOption<int32_t> (
		     "initial-program-change",
		     _("Initial program change"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_initial_program_change),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_initial_program_change),
		     -1, 65536, 1, 10
		     ));

	add_option (_("MIDI"),
		    new BoolOption (
			    "display-first-midi-bank-as-zero",
			    _("Display first MIDI bank/program as 0"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_first_midi_bank_is_zero),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_first_midi_bank_is_zero)
			    ));

	add_option (_("MIDI"),
	     new BoolOption (
		     "never-display-periodic-midi",
		     _("Never display periodic MIDI messages (MTC, MIDI Clock)"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_never_display_periodic_midi),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_never_display_periodic_midi)
		     ));

	add_option (_("MIDI"),
	     new BoolOption (
		     "sound-midi-notes",
		     _("Sound MIDI notes as they are selected in the editor"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_sound_midi_notes),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_sound_midi_notes)
		     ));

	add_option (_("MIDI"),
		    new BoolOption (
			    "midi-feedback",
			    _("Send MIDI control feedback"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_midi_feedback),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_midi_feedback)
			    ));

	add_option (_("MIDI/Ports"), new OptionEditorHeading (_("MIDI Port Options")));

	add_option (_("MIDI/Ports"),
		    new BoolOption (
			    "get-midi-input-follows-selection",
			    _("MIDI input follows MIDI track selection"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_midi_input_follows_selection),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_midi_input_follows_selection)
			    ));

	add_option (_("MIDI/Ports"), new MidiPortOptions ());

	add_option (_("MIDI/Sync"), new OptionEditorHeading (_("MIDI Clock")));

	add_option (_("MIDI/Sync"),
		    new BoolOption (
			    "send-midi-clock",
			    _("Send MIDI Clock"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_send_midi_clock),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_send_midi_clock)
			    ));

	add_option (_("MIDI/Sync"), new OptionEditorHeading (_("MIDI Time Code (MTC)")));

	add_option (_("MIDI/Sync"),
		    new BoolOption (
			    "send-mtc",
			    _("Send MIDI Time Code"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_send_mtc),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_send_mtc)
			    ));

	add_option (_("MIDI/Sync"),
		    new SpinOption<int> (
			    "mtc-qf-speed-tolerance",
			    _("Percentage either side of normal transport speed to transmit MTC"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_mtc_qf_speed_tolerance),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_mtc_qf_speed_tolerance),
			    0, 20, 1, 5
			    ));

	add_option (_("MIDI/Sync"), new OptionEditorHeading (_("Midi Machine Control (MMC)")));

	add_option (_("MIDI/Sync"),
		    new BoolOption (
			    "mmc-control",
			    _("Obey MIDI Machine Control commands"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_mmc_control),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_mmc_control)
			    ));

	add_option (_("MIDI/Sync"),
		    new BoolOption (
			    "send-mmc",
			    _("Send MIDI Machine Control commands"),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::get_send_mmc),
			    sigc::mem_fun (*_rc_config, &RCConfiguration::set_send_mmc)
			    ));

	add_option (_("MIDI/Sync"),
	     new SpinOption<uint8_t> (
		     "mmc-receive-device-id",
		     _("Inbound MMC device ID"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mmc_receive_device_id),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mmc_receive_device_id),
		     0, 128, 1, 10
		     ));

	add_option (_("MIDI/Sync"),
	     new SpinOption<uint8_t> (
		     "mmc-send-device-id",
		     _("Outbound MMC device ID"),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::get_mmc_send_device_id),
		     sigc::mem_fun (*_rc_config, &RCConfiguration::set_mmc_send_device_id),
		     0, 128, 1, 10
		     ));

	add_option (_("MIDI"), new OptionEditorHeading (_("Midi Audition")));

	ComboOption<std::string>* audition_synth = new ComboOption<std::string> (
		"midi-audition-synth-uri",
		_("Midi Audition Synth (LV2)"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_midi_audition_synth_uri),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_midi_audition_synth_uri)
		);

	audition_synth->add(X_(""), _("None"));
	PluginInfoList all_plugs;
	PluginManager& manager (PluginManager::instance());
#ifdef LV2_SUPPORT
	all_plugs.insert (all_plugs.end(), manager.lv2_plugin_info().begin(), manager.lv2_plugin_info().end());

	for (PluginInfoList::const_iterator i = all_plugs.begin(); i != all_plugs.end(); ++i) {
		if (manager.get_status (*i) == PluginManager::Hidden) continue;
		if (!(*i)->is_instrument()) continue;
		if ((*i)->type != ARDOUR::LV2) continue;
		if ((*i)->name.length() > 46) {
			audition_synth->add((*i)->unique_id, (*i)->name.substr (0, 44) + "...");
		} else {
			audition_synth->add((*i)->unique_id, (*i)->name);
		}
	}
#endif

	add_option (_("MIDI"), audition_synth);

	/* Control Surfaces */

	add_option (_("Control Surfaces"), new ControlSurfacesOptions ());

	/* VIDEO Timeline */
	add_option (_("Video"), new VideoTimelineOptions (_rc_config));

#if (defined WINDOWS_VST_SUPPORT || defined LXVST_SUPPORT || defined MACVST_SUPPORT || defined AUDIOUNIT_SUPPORT)
	add_option (_("Plugins"), new OptionEditorHeading (_("Scan/Discover")));
	add_option (_("Plugins"),
			new RcActionButton (_("Scan for Plugins"),
				sigc::mem_fun (*this, &RCOptionEditor::plugin_scan_refresh)));

#endif

	add_option (_("Plugins"), new OptionEditorHeading (_("General")));

#if (defined WINDOWS_VST_SUPPORT || defined LXVST_SUPPORT || defined MACVST_SUPPORT || defined AUDIOUNIT_SUPPORT)
	bo = new BoolOption (
			"show-plugin-scan-window",
			_("Always Display Plugin Scan Progress"),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_plugin_scan_window),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_plugin_scan_window)
			);
	add_option (_("Plugins"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
			_("<b>When enabled</b> a popup window showing plugin scan progress is displayed for indexing (cache load) and discovery (detect new plugins)"));
#endif

	bo = new BoolOption (
		"plugins-stop-with-transport",
		_("Silence plugins when the transport is stopped"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_plugins_stop_with_transport),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_plugins_stop_with_transport)
		);
	add_option (_("Plugins"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
					    _("<b>When enabled</b> plugins will be reset at transport stop. When disabled plugins will be left unchanged at transport stop.\n\nThis mostly affects plugins with a \"tail\" like Reverbs."));

	bo = new BoolOption (
		"new-plugins-active",
			_("Make new plugins active"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_new_plugins_active),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_new_plugins_active)
			);
	add_option (_("Plugins"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
					    _("<b>When enabled</b> plugins will be activated when they are added to tracks/busses. When disabled plugins will be left inactive when they are added to tracks/busses"));

#if (defined WINDOWS_VST_SUPPORT || defined MACVST_SUPPORT || defined LXVST_SUPPORT)
	add_option (_("Plugins/VST"), new OptionEditorHeading (_("VST")));
	add_option (_("Plugins/VST"),
			new RcActionButton (_("Scan for Plugins"),
				sigc::mem_fun (*this, &RCOptionEditor::plugin_scan_refresh)));

#if (defined AUDIOUNIT_SUPPORT && defined MACVST_SUPPORT)
	bo = new BoolOption (
			"",
			_("Enable Mac VST support (requires restart or re-scan)"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_use_macvst),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_use_macvst)
			);
	add_option (_("Plugins/VST"), bo);
#endif

	bo = new BoolOption (
			"discover-vst-on-start",
			_("Scan for [new] VST Plugins on Application Start"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_discover_vst_on_start),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_discover_vst_on_start)
			);
	add_option (_("Plugins/VST"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
					    _("<b>When enabled</b> new VST plugins are searched, tested and added to the cache index on application start. When disabled new plugins will only be available after triggering a 'Scan' manually"));

#ifdef WINDOWS_VST_SUPPORT
	// currently verbose logging is only implemented for Windows VST.
	bo = new BoolOption (
			"verbose-plugin-scan",
			_("Verbose Plugin Scan"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_verbose_plugin_scan),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_verbose_plugin_scan)
			);
	add_option (_("Plugins/VST"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
					    _("<b>When enabled</b> additional information for every plugin is added to the Log Window."));
#endif

	add_option (_("Plugins/VST"), new VstTimeOutSliderOption (_rc_config));

	add_option (_("Plugins/VST"),
			new RcActionButton (_("Clear"),
				sigc::mem_fun (*this, &RCOptionEditor::clear_vst_cache),
				_("VST Cache:")));

	add_option (_("Plugins/VST"),
			new RcActionButton (_("Clear"),
				sigc::mem_fun (*this, &RCOptionEditor::clear_vst_blacklist),
				_("VST Blacklist:")));
#endif

#ifdef LXVST_SUPPORT
	add_option (_("Plugins/VST"),
			new RcActionButton (_("Edit"),
				sigc::mem_fun (*this, &RCOptionEditor::edit_lxvst_path),
			_("Linux VST Path:")));

	add_option (_("Plugins/VST"),
			new RcConfigDisplay (
				"plugin-path-lxvst",
				_("Path:"),
				sigc::mem_fun (*_rc_config, &RCConfiguration::get_plugin_path_lxvst),
				0));
#endif

#ifdef WINDOWS_VST_SUPPORT
	add_option (_("Plugins/VST"),
			new RcActionButton (_("Edit"),
				sigc::mem_fun (*this, &RCOptionEditor::edit_vst_path),
			_("Windows VST Path:")));
	add_option (_("Plugins"),
			new RcConfigDisplay (
				"plugin-path-vst",
				_("Path:"),
				sigc::mem_fun (*_rc_config, &RCConfiguration::get_plugin_path_vst),
				';'));
#endif

#ifdef AUDIOUNIT_SUPPORT

	add_option (_("Plugins/Audio Unit"), new OptionEditorHeading (_("Audio Unit")));
	add_option (_("Plugins/Audio Unit"),
			new RcActionButton (_("Scan for Plugins"),
				sigc::mem_fun (*this, &RCOptionEditor::plugin_scan_refresh)));

	bo = new BoolOption (
			"discover-audio-units",
			_("Scan for [new] AudioUnit Plugins on Application Start"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_discover_audio_units),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_discover_audio_units)
			);
	add_option (_("Plugins/Audio Unit"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
					    _("<b>When enabled</b> Audio Unit Plugins are discovered on application start. When disabled AU plugins will only be available after triggering a 'Scan' manually. The first successful scan will enable AU auto-scan, Any crash during plugin discovery will disable it."));

	add_option (_("Plugins/Audio Unit"),
			new RcActionButton (_("Clear"),
				sigc::mem_fun (*this, &RCOptionEditor::clear_au_cache),
				_("AU Cache:")));

	add_option (_("Plugins/Audio Unit"),
			new RcActionButton (_("Clear"),
				sigc::mem_fun (*this, &RCOptionEditor::clear_au_blacklist),
				_("AU Blacklist:")));
#endif

#if (defined WINDOWS_VST_SUPPORT || defined LXVST_SUPPORT || defined MACVST_SUPPORT || defined AUDIOUNIT_SUPPORT || defined HAVE_LV2)
	add_option (_("Plugins"), new OptionEditorHeading (_("Plugin GUI")));
	add_option (_("Plugins"),
	     new BoolOption (
		     "open-gui-after-adding-plugin",
		     _("Automatically open the plugin GUI when adding a new plugin"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_open_gui_after_adding_plugin),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_open_gui_after_adding_plugin)
		     ));

#if (defined LV2_SUPPORT && defined LV2_EXTENDED)
	add_option (_("Plugins"),
	     new BoolOption (
		     "show-inline-display-by-default",
		     _("Show Plugin Inline Display on Mixerstrip by default"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_inline_display_by_default),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_inline_display_by_default)
		     ));

	_plugin_prefer_inline = new BoolOption (
			"prefer-inline-over-gui",
			_("Don't automatically open the plugin GUI when the plugin has an inline display mode"),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_prefer_inline_over_gui),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_prefer_inline_over_gui)
			);
	add_option (_("Plugins"), _plugin_prefer_inline);
#endif

	add_option (_("Plugins"), new OptionEditorHeading (_("Instrument")));

	bo = new BoolOption (
			"ask-replace-instrument",
			_("Ask to replace existing instrument plugin"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_ask_replace_instrument),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_ask_replace_instrument)
			);
	add_option (_("Plugins"), bo);

	bo = new BoolOption (
			"ask-setup_instrument",
			_("Interactively configure instrument plugins on insert"),
			sigc::mem_fun (*_rc_config, &RCConfiguration::get_ask_setup_instrument),
			sigc::mem_fun (*_rc_config, &RCConfiguration::set_ask_setup_instrument)
			);
	add_option (_("Plugins"), bo);
	Gtkmm2ext::UI::instance()->set_tip (bo->tip_widget(),
			_("<b>When enabled</b> show a dialog to select instrument channel configuration before adding a multichannel plugin."));

#endif

	/* INTERFACE */
#if (defined OPTIONAL_CAIRO_IMAGE_SURFACE || defined CAIRO_SUPPORTS_FORCE_BUGGY_GRADIENTS_ENVIRONMENT_VARIABLE)
	add_option (S_("Preferences|GUI"), new OptionEditorHeading (_("Graphics Acceleration")));
#endif

#ifdef OPTIONAL_CAIRO_IMAGE_SURFACE
	BoolOption* bgc = new BoolOption (
		"cairo-image-surface",
		_("Disable Graphics Hardware Acceleration (requires restart)"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_cairo_image_surface),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_cairo_image_surface)
		);

	Gtkmm2ext::UI::instance()->set_tip (bgc->tip_widget(), string_compose (
				_("Render large parts of the application user-interface in software, instead of using 2D-graphics acceleration.\nThis requires restarting %1 before having an effect"), PROGRAM_NAME));
	add_option (S_("Preferences|GUI"), bgc);
#endif

#ifdef CAIRO_SUPPORTS_FORCE_BUGGY_GRADIENTS_ENVIRONMENT_VARIABLE
	BoolOption* bgo = new BoolOption (
		"buggy-gradients",
		_("Possibly improve slow graphical performance (requires restart)"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_buggy_gradients),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_buggy_gradients)
		);

	Gtkmm2ext::UI::instance()->set_tip (bgo->tip_widget(), string_compose (_("Disables hardware gradient rendering on buggy video drivers (\"buggy gradients patch\").\nThis requires restarting %1 before having an effect"), PROGRAM_NAME));
	add_option (S_("Preferences|GUI"), bgo);
#endif

	add_option (S_("Preferences|GUI"), new OptionEditorHeading (_("Graphical User Interface")));
	add_option (S_("Preferences|GUI"),
	     new BoolOption (
		     "use-wm-visibility",
		     _("Use Window Manager/Desktop visibility information"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_use_wm_visibility),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_use_wm_visibility)
		     ));

	add_option (S_("Preferences|GUI"),
	     new BoolOption (
		     "widget-prelight",
		     _("Graphically indicate mouse pointer hovering over various widgets"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_widget_prelight),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_widget_prelight)
		     ));

	add_option (S_("Preferences|GUI"),
	     new BoolOption (
		     "use-tooltips",
		     _("Show tooltips if mouse hovers over a control"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_use_tooltips),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_use_tooltips)
		     ));

	add_option (S_("Preferences|GUI"),
	     new BoolOption (
		     "show-name-highlight",
		     _("Use name highlight bars in region displays (requires a restart)"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_name_highlight),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_name_highlight)
		     ));

	add_option (S_("Preferences|GUI"),
		    new BoolOption (
			    "super-rapid-clock-update",
			    _("Update transport clock display at FPS instead of every 100ms"),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_super_rapid_clock_update),
			    sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_super_rapid_clock_update)
			    ));


#ifndef __APPLE__
	/* font scaling does nothing with GDK/Quartz */
	add_option (S_("Preferences|GUI"), new FontScalingOptions ());
#endif

	/* Image cache size */

	Gtk::Adjustment *ics = manage (new Gtk::Adjustment(0, 1, 1024, 10)); /* 1 MB to 1GB in steps of 10MB */
	HSliderOption *sics = new HSliderOption("waveform-cache-size",
						_("Waveform image cache size (megabytes)"),
						ics,
						sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_waveform_cache_size),
						sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_waveform_cache_size)
			);
	sics->scale().set_digits (0);
	Gtkmm2ext::UI::instance()->set_tip
		(sics->tip_widget(),
		 _("Increasing the cache size uses more memory to store waveform images, which can improve graphical performance."));
	add_option (S_("Preferences|GUI"), sics);

if (!ARDOUR::Profile->get_mixbus()) {
	/* Lock GUI timeout */

	Gtk::Adjustment *lts = manage (new Gtk::Adjustment(0, 0, 1000, 1, 10));
	HSliderOption *slts = new HSliderOption("lock-gui-after-seconds",
						_("Lock timeout (seconds)"),
						lts,
						sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_lock_gui_after_seconds),
						sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_lock_gui_after_seconds)
			);
	slts->scale().set_digits (0);
	Gtkmm2ext::UI::instance()->set_tip
		(slts->tip_widget(),
		 _("Lock GUI after this many idle seconds (zero to never lock)"));
	add_option (S_("Preferences|GUI"), slts);
} // !mixbus

	/* The names of these controls must be the same as those given in MixerStrip
	   for the actual widgets being controlled.
	*/
	_mixer_strip_visibility.add (0, X_("Input"), _("Input"));
	_mixer_strip_visibility.add (0, X_("PhaseInvert"), _("Phase Invert"));
	_mixer_strip_visibility.add (0, X_("RecMon"), _("Record & Monitor"));
	_mixer_strip_visibility.add (0, X_("SoloIsoLock"), _("Solo Iso / Lock"));
	_mixer_strip_visibility.add (0, X_("Output"), _("Output"));
	_mixer_strip_visibility.add (0, X_("Comments"), _("Comments"));
	_mixer_strip_visibility.add (0, X_("VCA"), _("VCA Assigns"));

	add_option (
		S_("Preferences|GUI"),
		new VisibilityOption (
			_("Mixer Strip"),
			&_mixer_strip_visibility,
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_mixer_strip_visibility),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_mixer_strip_visibility)
			)
		);

	add_option (S_("Preferences|GUI"),
	     new BoolOption (
		     "default-narrow_ms",
		     _("Use narrow strips in the mixer by default"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_default_narrow_ms),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_default_narrow_ms)
		     ));

#ifdef ENABLE_NLS
	OptionEditorHeading* i18n_head = new OptionEditorHeading (_("Internationalization"));
	i18n_head->set_note (string_compose (_("These settings will only take effect after %1 is restarted (if available for your language preferences)."), PROGRAM_NAME));

	add_option (_("GUI/Translation"), i18n_head);

	bo = new BoolOption (
			"enable-translation",
			_("Use translations"),
			sigc::ptr_fun (ARDOUR::translations_are_enabled),
			sigc::ptr_fun (ARDOUR::set_translations_enabled)
			);

	add_option (_("GUI/Translation"), bo);

	_l10n = new ComboOption<ARDOUR::LocaleMode> (
		"locale-mode",
		_("Localization"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_locale_mode),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_locale_mode)
		);

	_l10n->add (ARDOUR::SET_LC_ALL, _("Set complete locale"));
	_l10n->add (ARDOUR::SET_LC_MESSAGES, _("Enable only message translation"));
	_l10n->add (ARDOUR::SET_LC_MESSAGES_AND_LC_NUMERIC, _("Translate messages and format numeric format"));
	_l10n->set_note (_("This setting is provided for plugin compatibility. e.g. some plugins on some systems expect the decimal point to be a dot."));

	add_option (_("GUI/Translation"), _l10n);
	parameter_changed ("enable-translation");
#endif // ENABLE_NLS

	add_option (_("GUI/Keyboard"), new OptionEditorHeading (_("Keyboard")));
	add_option (_("GUI/Keyboard"), new KeyboardOptions);

	add_option (_("GUI/Toolbar"), new OptionEditorHeading (_("Main Transport Toolbar Items")));

	add_option (_("GUI/Toolbar"),
	     new BoolOption (
		     "show-toolbar-selclock",
		     _("Display Selection Clock in the Toolbar"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_toolbar_selclock),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_toolbar_selclock)
		     ));

	if (!ARDOUR::Profile->get_small_screen()) {
		add_option (_("GUI/Toolbar"),
				new BoolOption (
					"show-secondary-clock",
					_("Display Secondary Clock in the Toolbar"),
					sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_secondary_clock),
					sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_secondary_clock)
					));
	}

	add_option (_("GUI/Toolbar"),
	     new BoolOption (
		     "show-mini-timeline",
		     _("Display Navigation Timeline in the Toolbar"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_mini_timeline),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_mini_timeline)
		     ));

	add_option (_("GUI/Toolbar"),
	     new BoolOption (
		     "show-editor-meter",
		     _("Display Master Level Meter in the Toolbar"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_editor_meter),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_editor_meter)
		     ));

	add_option (_("GUI/Toolbar"),
			new ColumVisibilityOption (
				"action-table-columns", _("Lua Action Script Button Visibility"), 4,
				sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_action_table_columns),
				sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_action_table_columns)
				)
			);

	add_option (S_("Preferences|Metering"), new OptionEditorHeading (_("Metering")));

	ComboOption<float>* mht = new ComboOption<float> (
		"meter-hold",
		_("Peak hold time"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_hold),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_hold)
		);

	mht->add (MeterHoldOff, _("off"));
	mht->add (MeterHoldShort, _("short"));
	mht->add (MeterHoldMedium, _("medium"));
	mht->add (MeterHoldLong, _("long"));

	add_option (S_("Preferences|Metering"), mht);

	ComboOption<float>* mfo = new ComboOption<float> (
		"meter-falloff",
		_("DPM fall-off"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_meter_falloff),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_meter_falloff)
		);

	mfo->add (METER_FALLOFF_OFF,      _("off"));
	mfo->add (METER_FALLOFF_SLOWEST,  _("slowest [6.6dB/sec]"));
	mfo->add (METER_FALLOFF_SLOW,     _("slow [8.6dB/sec] (BBC PPM, EBU PPM)"));
	mfo->add (METER_FALLOFF_SLOWISH,  _("moderate [12.0dB/sec] (DIN)"));
	mfo->add (METER_FALLOFF_MODERATE, _("medium [13.3dB/sec] (EBU Digi PPM, IRT Digi PPM)"));
	mfo->add (METER_FALLOFF_MEDIUM,   _("fast [20dB/sec]"));
	mfo->add (METER_FALLOFF_FAST,     _("very fast [32dB/sec]"));

	add_option (S_("Preferences|Metering"), mfo);

	ComboOption<MeterLineUp>* mlu = new ComboOption<MeterLineUp> (
		"meter-line-up-level",
		_("Meter line-up level; 0dBu"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_line_up_level),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_line_up_level)
		);

	mlu->add (MeteringLineUp24, _("-24dBFS (SMPTE US: 4dBu = -20dBFS)"));
	mlu->add (MeteringLineUp20, _("-20dBFS (SMPTE RP.0155)"));
	mlu->add (MeteringLineUp18, _("-18dBFS (EBU, BBC)"));
	mlu->add (MeteringLineUp15, _("-15dBFS (DIN)"));

	Gtkmm2ext::UI::instance()->set_tip (mlu->tip_widget(), _("Configure meter-marks and color-knee point for dBFS scale DPM, set reference level for IEC1/Nordic, IEC2 PPM and VU meter."));

	add_option (S_("Preferences|Metering"), mlu);

	ComboOption<MeterLineUp>* mld = new ComboOption<MeterLineUp> (
		"meter-line-up-din",
		_("IEC1/DIN Meter line-up level; 0dBu"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_line_up_din),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_line_up_din)
		);

	mld->add (MeteringLineUp24, _("-24dBFS (SMPTE US: 4dBu = -20dBFS)"));
	mld->add (MeteringLineUp20, _("-20dBFS (SMPTE RP.0155)"));
	mld->add (MeteringLineUp18, _("-18dBFS (EBU, BBC)"));
	mld->add (MeteringLineUp15, _("-15dBFS (DIN)"));

	Gtkmm2ext::UI::instance()->set_tip (mld->tip_widget(), _("Reference level for IEC1/DIN meter."));

	add_option (S_("Preferences|Metering"), mld);

	ComboOption<VUMeterStandard>* mvu = new ComboOption<VUMeterStandard> (
		"meter-vu-standard",
		_("VU Meter standard"),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_vu_standard),
		sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_vu_standard)
		);

	mvu->add (MeteringVUfrench,   _("0VU = -2dBu (France)"));
	mvu->add (MeteringVUamerican, _("0VU = 0dBu (North America, Australia)"));
	mvu->add (MeteringVUstandard, _("0VU = +4dBu (standard)"));
	mvu->add (MeteringVUeight,    _("0VU = +8dBu"));

	add_option (S_("Preferences|Metering"), mvu);

	Gtk::Adjustment *mpk = manage (new Gtk::Adjustment(0, -10, 0, .1, .1));
	HSliderOption *mpks = new HSliderOption("meter-peak",
			_("Peak threshold [dBFS]"),
			mpk,
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_peak),
			sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_peak)
			);


	ComboOption<MeterType>* mtm = new ComboOption<MeterType> (
		"meter-type-master",
		_("Default Meter Type for Master Bus"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_meter_type_master),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_meter_type_master)
		);
	mtm->add (MeterPeak,    ArdourMeter::meter_type_string(MeterPeak));
	mtm->add (MeterK20,     ArdourMeter::meter_type_string(MeterK20));
	mtm->add (MeterK14,     ArdourMeter::meter_type_string(MeterK14));
	mtm->add (MeterK12,     ArdourMeter::meter_type_string(MeterK12));
	mtm->add (MeterIEC1DIN, ArdourMeter::meter_type_string(MeterIEC1DIN));
	mtm->add (MeterIEC1NOR, ArdourMeter::meter_type_string(MeterIEC1NOR));
	mtm->add (MeterIEC2BBC, ArdourMeter::meter_type_string(MeterIEC2BBC));
	mtm->add (MeterIEC2EBU, ArdourMeter::meter_type_string(MeterIEC2EBU));

	add_option (S_("Preferences|Metering"), mtm);


	ComboOption<MeterType>* mtb = new ComboOption<MeterType> (
		"meter-type-bus",
		_("Default Meter Type for Busses"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_meter_type_bus),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_meter_type_bus)
		);
	mtb->add (MeterPeak,    ArdourMeter::meter_type_string(MeterPeak));
	mtb->add (MeterK20,     ArdourMeter::meter_type_string(MeterK20));
	mtb->add (MeterK14,     ArdourMeter::meter_type_string(MeterK14));
	mtb->add (MeterK12,     ArdourMeter::meter_type_string(MeterK12));
	mtb->add (MeterIEC1DIN, ArdourMeter::meter_type_string(MeterIEC1DIN));
	mtb->add (MeterIEC1NOR, ArdourMeter::meter_type_string(MeterIEC1NOR));
	mtb->add (MeterIEC2BBC, ArdourMeter::meter_type_string(MeterIEC2BBC));
	mtb->add (MeterIEC2EBU, ArdourMeter::meter_type_string(MeterIEC2EBU));

	add_option (S_("Preferences|Metering"), mtb);

	ComboOption<MeterType>* mtt = new ComboOption<MeterType> (
		"meter-type-track",
		_("Default Meter Type for Tracks"),
		sigc::mem_fun (*_rc_config, &RCConfiguration::get_meter_type_track),
		sigc::mem_fun (*_rc_config, &RCConfiguration::set_meter_type_track)
		);
	mtt->add (MeterPeak,    ArdourMeter::meter_type_string(MeterPeak));
	mtt->add (MeterPeak0dB, ArdourMeter::meter_type_string(MeterPeak0dB));

	add_option (S_("Preferences|Metering"), mtt);


	Gtkmm2ext::UI::instance()->set_tip
		(mpks->tip_widget(),
		 _("Specify the audio signal level in dBFS at and above which the meter-peak indicator will flash red."));

	add_option (S_("Preferences|Metering"), mpks);

	add_option (S_("Preferences|Metering"),
	     new BoolOption (
		     "meter-style-led",
		     _("LED meter style"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_meter_style_led),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_meter_style_led)
		     ));

	add_option (S_("Preferences|Metering"), new OptionEditorHeading (_("Editor Meters")));

	add_option (S_("Preferences|Metering"),
	     new BoolOption (
		     "show-track-meters",
		     _("Show meters on tracks in the editor"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_show_track_meters),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_show_track_meters)
		     ));

	add_option (S_("Preferences|Metering"),
	     new BoolOption (
		     "editor-stereo-only-meters",
		     _("Show at most stereo meters in the track-header"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_editor_stereo_only_meters),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_editor_stereo_only_meters)
		     ));

	add_option (S_("Preferences|Metering"), new OptionEditorHeading (_("Post Export Analysis")));

	add_option (S_("Preferences|Metering"),
	     new BoolOption (
		     "save-export-analysis-image",
		     _("Save loudness analysis as image file"),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::get_save_export_analysis_image),
		     sigc::mem_fun (UIConfiguration::instance(), &UIConfiguration::set_save_export_analysis_image)
		     ));

	/* and now the theme manager */

	ThemeManager* tm = manage (new ThemeManager);
	add_option (_("Theme"), new OptionEditorHeading (_("Theme")));
	add_page (_("Theme"), *tm);

	add_option (_("Theme/Colors"), new ColorThemeManager);

	Widget::show_all ();

	//trigger some parameter-changed messages which affect widget-visibility or -sensitivity
	parameter_changed ("send-ltc");
	parameter_changed ("sync-source");
	parameter_changed ("use-monitor-bus");
	parameter_changed ("open-gui-after-adding-plugin");

	XMLNode* node = ARDOUR_UI::instance()->preferences_settings();
	if (node) {
		/* gcc4 complains about ambiguity with Gtk::Widget::set_state
		   (Gtk::StateType) here !!!
		*/
		Tabbable::set_state (*node, Stateful::loading_state_version);
	}
}

void
RCOptionEditor::parameter_changed (string const & p)
{
	OptionEditor::parameter_changed (p);

	if (p == "use-monitor-bus") {
		bool const s = Config->get_use_monitor_bus ();
		if (!s) {
			/* we can't use this if we don't have a monitor bus */
			Config->set_solo_control_is_listen_control (false);
		}
		_solo_control_is_listen_control->set_sensitive (s);
		_listen_position->set_sensitive (s);
	} else if (p == "sync-source") {
		_sync_source->set_sensitive (true);
		if (_session) {
			_sync_source->set_sensitive (!_session->config.get_external_sync());
		}
		switch(Config->get_sync_source()) {
		case ARDOUR::MTC:
		case ARDOUR::LTC:
			_sync_genlock->set_sensitive (true);
			_sync_framerate->set_sensitive (true);
			_sync_source_2997->set_sensitive (true);
			break;
		default:
			_sync_genlock->set_sensitive (false);
			_sync_framerate->set_sensitive (false);
			_sync_source_2997->set_sensitive (false);
			break;
		}
	} else if (p == "send-ltc") {
		bool const s = Config->get_send_ltc ();
		_ltc_send_continuously->set_sensitive (s);
		_ltc_volume_slider->set_sensitive (s);
	} else if (p == "open-gui-after-adding-plugin" || p == "show-inline-display-by-default") {
#if (defined LV2_SUPPORT && defined LV2_EXTENDED)
		_plugin_prefer_inline->set_sensitive (UIConfiguration::instance().get_open_gui_after_adding_plugin() && UIConfiguration::instance().get_show_inline_display_by_default());
#endif
#ifdef ENABLE_NLS
	} else if (p == "enable-translation") {
		_l10n->set_sensitive (ARDOUR::translations_are_enabled ());
#endif
	}
}

void RCOptionEditor::ltc_generator_volume_changed () {
	_rc_config->set_ltc_output_volume (pow(10, _ltc_volume_adjustment->get_value() / 20));
}

void RCOptionEditor::plugin_scan_refresh () {
	PluginManager::instance().refresh();
}

void RCOptionEditor::clear_vst_cache () {
	PluginManager::instance().clear_vst_cache();
}

void RCOptionEditor::clear_vst_blacklist () {
	PluginManager::instance().clear_vst_blacklist();
}

void RCOptionEditor::clear_au_cache () {
	PluginManager::instance().clear_au_cache();
}

void RCOptionEditor::clear_au_blacklist () {
	PluginManager::instance().clear_au_blacklist();
}

void RCOptionEditor::edit_lxvst_path () {
	Glib::RefPtr<Gdk::Window> win = get_parent_window ();
	Gtkmm2ext::PathsDialog *pd = new Gtkmm2ext::PathsDialog (
		*current_toplevel(), _("Set Linux VST Search Path"),
		_rc_config->get_plugin_path_lxvst(),
		PluginManager::instance().get_default_lxvst_path()
		);
	ResponseType r = (ResponseType) pd->run ();
	pd->hide();
	if (r == RESPONSE_ACCEPT) {
		_rc_config->set_plugin_path_lxvst(pd->get_serialized_paths());
	}
	delete pd;
}

void RCOptionEditor::edit_vst_path () {
	Gtkmm2ext::PathsDialog *pd = new Gtkmm2ext::PathsDialog (
		*current_toplevel(), _("Set Windows VST Search Path"),
		_rc_config->get_plugin_path_vst(),
		PluginManager::instance().get_default_windows_vst_path()
		);
	ResponseType r = (ResponseType) pd->run ();
	pd->hide();
	if (r == RESPONSE_ACCEPT) {
		_rc_config->set_plugin_path_vst(pd->get_serialized_paths());
	}
	delete pd;
}


void
RCOptionEditor::populate_sync_options ()
{
	vector<SyncSource> sync_opts = ARDOUR::get_available_sync_options ();

	_sync_source->clear ();

	for (vector<SyncSource>::iterator i = sync_opts.begin(); i != sync_opts.end(); ++i) {
		_sync_source->add (*i, sync_source_to_string (*i));
	}

	if (sync_opts.empty()) {
		_sync_source->set_sensitive(false);
	} else {
		if (std::find(sync_opts.begin(), sync_opts.end(), _rc_config->get_sync_source()) == sync_opts.end()) {
			_rc_config->set_sync_source(sync_opts.front());
		}
	}

	parameter_changed ("sync-source");
}

Gtk::Window*
RCOptionEditor::use_own_window (bool and_fill_it)
{
	bool new_window = !own_window ();

	Gtk::Window* win = Tabbable::use_own_window (and_fill_it);

	if (win && new_window) {
		win->set_name ("PreferencesWindow");
		ARDOUR_UI::instance()->setup_toplevel_window (*win, _("Preferences"), this);
	}

	return win;
}

XMLNode&
RCOptionEditor::get_state ()
{
	XMLNode* node = new XMLNode (X_("Preferences"));
	node->add_child_nocopy (Tabbable::get_state());
	return *node;
}
