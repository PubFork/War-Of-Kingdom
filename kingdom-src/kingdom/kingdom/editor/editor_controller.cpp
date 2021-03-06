/* $Id: editor_controller.cpp 47755 2010-11-29 12:57:31Z shadowmaster $ */
/*
   Copyright (C) 2008 - 2010 by Tomasz Sniatowski <kailoran@gmail.com>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#define GETTEXT_DOMAIN "kingdom-lib"

#include "asserts.hpp"
#include "action.hpp"
#include "editor_controller.hpp"
#include "editor_palettes.hpp"
#include "editor_preferences.hpp"
#include "mouse_action.hpp"

#include "gui/dialogs/editor_new_map.hpp"
#include "gui/dialogs/editor_generate_map.hpp"
#include "gui/dialogs/editor_resize_map.hpp"
#include "gui/dialogs/editor_settings.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/transient_message.hpp"
#include "gui/dialogs/combo_box.hpp"
#include "gui/dialogs/system.hpp"
#include "gui/dialogs/preferences.hpp"
#include "gui/dialogs/browse.hpp"
#include "gui/dialogs/theme2.hpp"
#include "gui/widgets/window.hpp"
#include "gui/widgets/settings.hpp"

#include "clipboard.hpp"
#include "filesystem.hpp"
#include "../game_preferences.hpp"
#include "gettext.hpp"
#include "../map_create.hpp"
#include "../mapgen.hpp"
#include "preferences_display.hpp"
#include "rng.hpp"
#include "sound.hpp"
#include "integrate.hpp"
#include "../multiplayer.hpp"
#include "hotkeys.hpp"

#include "formula_string_utils.hpp"

#include <boost/foreach.hpp>
#include <boost/bind.hpp>

namespace {
static std::vector<std::string> saved_windows_;
}

namespace editor {

bool tmap_type::can_modify(const gamemap& map, const map_location& loc, const t_translation::t_terrain& t, const t_translation::t_terrain& old)
{
	utils::string_map symbols;

	err_str_.clear();
	for (std::vector<titem>::const_iterator it = items_.begin(); it != items_.end(); ++ it) {
		const titem& item = *it;
		if (t_translation::terrain_matches(t, item.list)) {
			if (!item.avoid.empty() && !allow_if_modify(map, loc, item.avoid)) {
				symbols["count"] = tintegrate::generate_format(item.unite, "yellow");
				err_str_ = vgettext2("This terrain can not place at this area!", symbols);
			} else /* if (!t_translation::terrain_matches(old, item.list)) */ {
				if (item.total >= 0 && calculate_total_if_modify(item.list, old) > item.total) {
					if (item.total) {
						symbols["count"] = tintegrate::generate_format(item.total, "yellow");
						err_str_ = vgettext2("This terrain may draw on $count grid at most!", symbols);
					} else {
						err_str_ = _("When drawing map, can not use this terrain!");
					}
				}
				if (err_str_.empty() && item.unite >= 0 && calculate_unite_if_modify(map, loc, item.list) > item.unite) {
					symbols["count"] = tintegrate::generate_format(item.unite, "yellow");
					err_str_ = vgettext2("Combining this terrain can not exceed $count grid!", symbols);
				}
			}
			if (!err_str_.empty()) {
				return false;
			}
		}
	}

	return true;
}

void tmap_type::handle_err()
{
	if (!err_str_.empty()) {
		std::stringstream err;
		err << err_str_;
		if (!summary_.empty()) {
			err << "\n\n" << summary_; 
		}
		gui2::show_message(disp_->video(), "", err.str());
		err_str_.clear();
	}
}

/**
 * Utility class to properly refresh the display when the map context object is replaced
 * without duplicating code.
 */
class map_context_refresher
{
public:
	map_context_refresher(editor_controller& ec, const map_context& other_mc)
	: ec_(ec), size_changed_(!ec.get_map().same_size_as(other_mc.get_map())), refreshed_(false)
	{
	}
	~map_context_refresher() {
		if (!refreshed_) refresh();
	}
	void refresh() {
		ec_.gui().change_map(&ec_.get_map());
		ec_.reload_map();
	}
private:
	editor_controller& ec_;
	bool size_changed_;
	bool refreshed_;
};

editor_controller::editor_controller(const config &game_config, CVideo& video, hero_map& heros, int mode)
	: controller_base(SDL_GetTicks(), game_config, video)
	, heros_(heros)
	, mode_(mode)
	, mouse_handler_base()
	, rng_(NULL)
	, rng_setter_(NULL)
	, map_contexts_()
	, current_context_index_(0)
	, gui_(NULL)
	, map_generators_()
	, tods_()
	, palette_()
	, floating_label_manager_(NULL)
	, do_quit_(false)
	, quit_mode_(EXIT_ERROR)
	, brushes_()
	, brush_(NULL)
	, mouse_actions_()
	, mouse_action_hints_()
	, mouse_action_(NULL)
	, toolbar_dirty_(true)
	, foreground_terrain_(t_translation::MOUNTAIN)
	, background_terrain_(t_translation::GRASS_LAND)
	, clipboard_()
	, auto_update_transitions_(preferences::editor::auto_update_transitions())
	, use_mdi_(preferences::editor::use_mdi())
	, default_dir_(preferences::editor::default_dir())
{
	create_default_context();

	if (default_dir_.empty()) {
		default_dir_ = get_dir(get_dir(get_user_data_dir() + "/editor") + "/maps");
	}
	init_gui(video);
	init_brushes(game_config);
	init_mouse_actions(game_config);
	init_map_generators(game_config);
	init_tods(game_config);
	init_sidebar(game_config);
	init_music(game_config);
	hotkey_set_mouse_action(gui2::teditor_theme::HOTKEY_EDITOR_TOOL_PAINT);
	rng_.reset(new rand_rng::rng());
	rng_setter_.reset(new rand_rng::set_random_generator(rng_.get()));
	get_map_context().set_starting_position_labels(gui());
	cursor::set(cursor::NORMAL);
	image::set_color_adjustment(preferences::editor::tod_r(), preferences::editor::tod_g(), preferences::editor::tod_b());
	refresh_all();
	events::raise_draw_event();

	map_type = tmap_type();
	if (mode_ != NONE) {
		BOOST_FOREACH (const config& type, game_config.child_range("map_type")) {
			const std::string& id = type["id"];
			if (mode == SIEGE && id == "siege") {
				map_type = tmap_type(*gui_, type);
			}
		}
	}
}

void editor_controller::init_gui(CVideo& video)
{
	const config &theme_cfg = gui2::get_theme("editor");
	gui_.reset(new editor_display(*this, video, get_map(), theme_cfg, config()));
	gui_->set_grid(preferences::grid());
	gui_->add_redraw_observer(boost::bind(&editor_controller::display_redraw_callback, this, _1));
	floating_label_manager_.reset(new font::floating_label_context(video.getSurface()));
	gui().set_draw_coordinates(preferences::editor::draw_hex_coordinates());
	gui().set_draw_terrain_codes(preferences::editor::draw_terrain_codes());
}

void editor_controller::init_sidebar(const config& game_config)
{
	palette_.reset(new terrain_palette(gui(), game_config,
		foreground_terrain_, background_terrain_));
	gui_->reload_terrain_palette(palette_->terrains());
}

void editor_controller::reload_terrain_palette()
{
	gui_->reload_terrain_palette(palette_->terrains());
}

void editor_controller::init_brushes(const config& game_config)
{
	BOOST_FOREACH (const config &i, game_config.child_range("brush")) {
		brushes_.push_back(brush(i));
	}
	if (brushes_.size() == 0) {
		ERR_ED << "No brushes defined!";
		brushes_.push_back(brush());
		brushes_[0].add_relative_location(0, 0);
	}
	brush_ = &brushes_[0];
}

void editor_controller::init_mouse_actions(const config& game_config)
{
	mouse_actions_.insert(std::make_pair(gui2::teditor_theme::HOTKEY_EDITOR_TOOL_PAINT,
		new mouse_action_paint(foreground_terrain_, background_terrain_, &brush_, key_)));
	mouse_actions_.insert(std::make_pair(gui2::teditor_theme::HOTKEY_EDITOR_TOOL_FILL,
		new mouse_action_fill(foreground_terrain_, background_terrain_, key_)));
	mouse_actions_.insert(std::make_pair(gui2::teditor_theme::HOTKEY_EDITOR_TOOL_SELECT,
		new mouse_action_select(&brush_, key_)));
	mouse_actions_.insert(std::make_pair(gui2::teditor_theme::HOTKEY_EDITOR_TOOL_STARTING_POSITION,
		new mouse_action_starting_position(key_)));
	mouse_actions_.insert(std::make_pair(gui2::teditor_theme::HOTKEY_EDITOR_PASTE,
		new mouse_action_paste(clipboard_, key_)));
	BOOST_FOREACH (const config &c, game_config.child_range("editor_tool_hint")) {
		mouse_action_map::iterator i =
			mouse_actions_.find(hotkey::get_hotkey(c["id"].str()).get_id());
		if (i != mouse_actions_.end()) {
			mouse_action_hints_.insert(std::make_pair(i->first, c["text"]));
		}
	}
}

void editor_controller::init_map_generators(const config& game_config)
{
	BOOST_FOREACH (const config &i, game_config.child_range("multiplayer"))
	{
		if (i["map_generation"] == "default") {
			const config &generator_cfg = i.child("generator");
			if (!generator_cfg) {
				ERR_ED << "Scenario \"" << i["name"] << "\" with id " << i["id"]
					<< " has map_generation=default but no [generator] tag";
			} else {
				map_generator* m = create_map_generator("", generator_cfg);
				map_generators_.push_back(m);
			}
		}
	}
}

void editor_controller::init_tods(const config& game_config)
{
	const config &cfg = game_config.child("editor_times");
	if (!cfg) {
		ERR_ED << "No editor time-of-day defined\n";
		return;
	}
	BOOST_FOREACH (const config &i, cfg.child_range("time")) {
		tods_.push_back(time_of_day(i));
	}
}

void editor_controller::init_music(const config& game_config)
{
	const config &cfg = game_config.child("editor_music");
	if (!cfg) {
		ERR_ED << "No editor music defined\n";
		return;
	}
	BOOST_FOREACH (const config &i, cfg.child_range("music")) {
		sound::play_music_config(i);
	}
	sound::commit_music_changes();
}


editor_controller::~editor_controller()
{
	BOOST_FOREACH (const mouse_action_map::value_type a, mouse_actions_) {
		delete a.second;
	}
	BOOST_FOREACH (map_generator* m, map_generators_) {
		delete m;
	}
	BOOST_FOREACH (map_context* mc, map_contexts_) {
		delete mc;
	}
}

EXIT_STATUS editor_controller::main_loop()
{
	try {
		while (!do_quit_) {
			play_slice();
		}
	} catch (editor_exception& e) {
		gui2::show_transient_message(gui().video(), _("Fatal error"), e.what());
		return EXIT_ERROR;
	} catch (twml_exception& e) {
		e.show(gui());
	}
	return quit_mode_;
}

void editor_controller::do_screenshot(const std::string& screenshot_filename /* = "map_screenshot.bmp" */)
{
	try {
		gui().screenshot(screenshot_filename,true);
	} catch (twml_exception& e) {
		e.show(gui());
	}
}

std::vector<std::string> editor_controller::map_modified() const
{
	std::vector<std::string> modified;
	BOOST_FOREACH (map_context* mc, map_contexts_) {
		if (mc->modified()) {
			if (!mc->get_filename().empty()) {
				modified.push_back(mc->get_filename());
			} else {
				modified.push_back(_("(New Map)"));
			}
		}
	}
	return modified;
}

void editor_controller::quit_confirm(EXIT_STATUS mode)
{
	std::vector<std::string> modified = map_modified();
	std::string message;
	if (modified.empty()) {
		message = _("Do you really want to quit?");
	} else if (modified.size() == 1) {
		message = _("Do you really want to quit? Changes in the map since the last save will be lost.");
	} else {
		message = _("Do you really want to quit? The following maps were modified and all changes since the last save will be lost:");
		BOOST_FOREACH (std::string& str, modified) {
			message += "\n" + str;
		}
	}
	const int res = gui2::show_message(gui().video(), _("Quit"), message, gui2::tmessage::yes_no_buttons);
	if (res != gui2::twindow::CANCEL) {
		do_quit_ = true;
		quit_mode_ = mode;
	}
}

int editor_controller::add_map_context(map_context* mc)
{
	map_contexts_.push_back(mc);
	return map_contexts_.size() - 1;
}

void editor_controller::create_default_context()
{
	if(saved_windows_.empty()) {
		map_context* mc = new map_context(editor_map(game_config_, 44, 33, t_translation::GRASS_LAND));
		add_map_context(mc);
	} else {
		BOOST_FOREACH (const std::string& filename, saved_windows_) {
			map_context* mc = new map_context(game_config_, filename);
			add_map_context(mc);
		}
		saved_windows_.clear();
	}
}

void editor_controller::close_current_context()
{
	if (!confirm_discard()) return;
	map_context* current = map_contexts_[current_context_index_];
	if (map_contexts_.size() == 1) {
		create_default_context();
		map_contexts_.erase(map_contexts_.begin());
	} else if (current_context_index_ == static_cast<int>(map_contexts_.size()) - 1) {
		map_contexts_.pop_back();
		current_context_index_--;
	} else {
		map_contexts_.erase(map_contexts_.begin() + current_context_index_);
	}
	map_context_refresher(*this, *current);
	delete current;
}

void editor_controller::switch_context(const int index)
{
	if (index < 0 || static_cast<size_t>(index) >= map_contexts_.size()) {
		WRN_ED << "Invalid index in switch map context: " << index << "\n";
		return;
	}
	map_context_refresher mcr(*this, *map_contexts_[index]);
	current_context_index_ = index;
}

void editor_controller::replace_map_context(map_context* new_mc)
{
	boost::scoped_ptr<map_context> del(map_contexts_[current_context_index_]);
	map_context_refresher mcr(*this, *new_mc);
	map_contexts_[current_context_index_] = new_mc;
}

void editor_controller::editor_settings_dialog()
{
	if (tods_.empty()) {
		gui2::show_error_message(gui().video(),
				_("No editor time-of-day found."));
		return;
	}

	image::color_adjustment_resetter adjust_resetter;
	if(!gui2::teditor_settings::execute(&(gui()), tods_, gui().video())) {
		adjust_resetter.reset();
	}
	refresh_all();
}

void editor_controller::editor_settings_dialog_redraw_callback(int r, int g, int b)
{
	SCOPE_ED;
	image::set_color_adjustment(r, g, b);
	gui().redraw_everything();
}

bool editor_controller::confirm_discard()
{
	if (get_map_context().modified()) {
		const int res = gui2::show_message(gui().video(), _("Unsaved Changes"), _("Do you want to discard all changes you made to the map since the last save?"), gui2::tmessage::yes_no_buttons);
		return gui2::twindow::CANCEL != res;
	} else {
		return true;
	}
}

void editor_controller::set_default_dir(const std::string& str)
{
	default_dir_ = str;
}

void editor_controller::load_map_dialog(bool force_same_context /* = false */)
{
	if (!use_mdi_ && !confirm_discard()) return;
	std::string fn = directory_name(get_map_context().get_filename());
	if (fn.empty()) {
		fn = default_dir_;
	}
	{
		gui2::tbrowse::tparam param(gui2::tbrowse::TYPE_FILE, true, fn, _("Choose a Map to Open"));
		gui2::tbrowse dlg(*gui_, param);
		dlg.show(gui_->video());
		int res = dlg.get_retval();
		if (res != gui2::twindow::OK) {
			return;
		}
		fn = param.result;
	}
	load_map(fn, force_same_context ? false : use_mdi_);
}

void editor_controller::new_map_dialog()
{
	if (!preferences::editor::use_mdi() && !confirm_discard()) {
		return;
	}

	int w = get_map().w();
	int h = get_map().h();
	if(gui2::teditor_new_map::execute(w, h, gui().video())) {
		const t_translation::t_terrain fill = t_translation::GRASS_LAND;
		new_map(w, h, fill, preferences::editor::use_mdi());
	}
}

void editor_controller::save_map_as_dialog()
{
	std::string input_name = get_map_context().get_filename();
	if (input_name.empty()) {
		input_name = default_dir_;
	}

	{
		gui2::tbrowse::tparam param(gui2::tbrowse::TYPE_FILE, false, input_name, _("Save the Map As"), _("Save"));
		gui2::tbrowse dlg(*gui_, param);
		dlg.show(gui_->video());
		int res = dlg.get_retval();
		if (res != gui2::twindow::OK) {
			return;
		}
		input_name = param.result;
	}

	save_map_as(input_name);
}

void editor_controller::generate_map_dialog()
{
	if (map_generators_.empty()) {
		gui2::show_error_message(gui().video(),
				_("No random map generators found."));
		return;
	}
	gui2::teditor_generate_map dialog;
	dialog.set_map_generators(map_generators_);
	dialog.set_gui(&gui());
	dialog.show(gui().video());
	if (dialog.get_retval() == gui2::twindow::OK) {
		std::string map_string;
		try {
			map_string = dialog.get_selected_map_generator()
				->create_map(std::vector<std::string>());
		} catch (mapgen_exception& e) {
			gui2::show_transient_message(gui().video(), _("Map creation failed."), e.what());
			return;
		}
		if (map_string.empty()) {
			gui2::show_transient_message(gui().video(), "", _("Map creation failed."));
		} else {
			editor_map new_map(game_config_, map_string);
			editor_action_whole_map a(new_map);
			perform_refresh(a);
		}
	}
}

void editor_controller::apply_mask_dialog()
{
/*
	std::string fn = get_map_context().get_filename();
	if (fn.empty()) {
		fn = default_dir_;
	}
	int res = dialogs::show_file_chooser_dialog(gui(), fn, _("Choose a mask to apply"));
	if (res == 0) {
		try {
			map_context mask(game_config_, fn);
			editor_action_apply_mask a(mask.get_map());
			perform_refresh(a);
		} catch (editor_map_load_exception& e) {
			gui2::show_transient_message(gui().video(), _("Error loading mask"), e.what());
			return;
		} catch (editor_action_exception& e) {
			gui2::show_error_message(gui().video(), e.what());
			return;
		}
	}
*/
}

void editor_controller::create_mask_to_dialog()
{
/*
	std::string fn = get_map_context().get_filename();
	if (fn.empty()) {
		fn = default_dir_;
	}
	int res = dialogs::show_file_chooser_dialog(gui(), fn, _("Choose target map"));
	if (res == 0) {
		try {
			map_context map(game_config_, fn);
			editor_action_create_mask a(map.get_map());
			perform_refresh(a);
		} catch (editor_map_load_exception& e) {
			gui2::show_transient_message(gui().video(), _("Error loading map"), e.what());
			return;
		} catch (editor_action_exception& e) {
			gui2::show_error_message(gui().video(), e.what());
			return;
		}
	}
*/
}

void editor_controller::resize_map_dialog()
{
	int w = get_map().w();
	int h = get_map().h();
	gui2::teditor_resize_map::EXPAND_DIRECTION dir;
	bool copy = false;
	if(gui2::teditor_resize_map::execute(w, h, dir, copy, gui().video())) {

		if (w != get_map().w() || h != get_map().h()) {
			t_translation::t_terrain fill = background_terrain_;
			if (copy) {
				fill = t_translation::NONE_TERRAIN;
			}
			int x_offset = get_map().w() - w;
			int y_offset = get_map().h() - h;
			switch (dir) {
				case gui2::teditor_resize_map::EXPAND_BOTTOM_RIGHT:
				case gui2::teditor_resize_map::EXPAND_BOTTOM:
				case gui2::teditor_resize_map::EXPAND_BOTTOM_LEFT:
					y_offset = 0;
					break;
				case gui2::teditor_resize_map::EXPAND_RIGHT:
				case gui2::teditor_resize_map::EXPAND_CENTER:
				case gui2::teditor_resize_map::EXPAND_LEFT:
					y_offset /= 2;
					break;
				case gui2::teditor_resize_map::EXPAND_TOP_RIGHT:
				case gui2::teditor_resize_map::EXPAND_TOP:
				case gui2::teditor_resize_map::EXPAND_TOP_LEFT:
					break;
				default:
					y_offset = 0;
					WRN_ED << "Unknown resize expand direction\n";
			}
			switch (dir) {
				case gui2::teditor_resize_map::EXPAND_BOTTOM_RIGHT:
				case gui2::teditor_resize_map::EXPAND_RIGHT:
				case gui2::teditor_resize_map::EXPAND_TOP_RIGHT:
					x_offset = 0;
					break;
				case gui2::teditor_resize_map::EXPAND_BOTTOM:
				case gui2::teditor_resize_map::EXPAND_CENTER:
				case gui2::teditor_resize_map::EXPAND_TOP:
					x_offset /= 2;
					break;
				case gui2::teditor_resize_map::EXPAND_BOTTOM_LEFT:
				case gui2::teditor_resize_map::EXPAND_LEFT:
				case gui2::teditor_resize_map::EXPAND_TOP_LEFT:
					break;
				default:
					x_offset = 0;
			}
			editor_action_resize_map a(w, h, x_offset, y_offset, fill);
			perform_refresh(a);
		}
	}
}

void editor_controller::save_all_maps(bool auto_save_windows)
{
	int current = current_context_index_;
	saved_windows_.clear();
	for(size_t i = 0; i < map_contexts_.size(); ++i) {
		switch_context(i);
		std::string name = get_map_context().get_filename();
		if(auto_save_windows) {
			if(name.empty() || is_directory(name)) {
				std::ostringstream s;
				s << default_dir_ << "/" << "window_" << i;
				name = s.str();
				get_map_context().set_filename(name);
			}
		}
		saved_windows_.push_back(name);
		save_map();
	}
	switch_context(current);
}

void editor_controller::save_map()
{
	const std::string& name = get_map_context().get_filename();
	if (name.empty() || is_directory(name)) {
		save_map_as_dialog();
	} else {
		write_map();
	}
}

bool editor_controller::save_map_as(const std::string& filename)
{
	size_t is_open = check_open_map(filename);
	if (is_open < map_contexts_.size()
			&& is_open != static_cast<unsigned>(current_context_index_)) {

		gui2::show_transient_message(gui().video(), _("This map is already open."), filename);
		return false;
	}
	std::string old_filename = get_map_context().get_filename();
	bool embedded = get_map_context().is_embedded();
	get_map_context().set_filename(filename);
	get_map_context().set_embedded(false);
	if (!write_map(true)) {
		get_map_context().set_filename(old_filename);
		get_map_context().set_embedded(embedded);
		return false;
	} else {
		return true;
	}
}

bool editor_controller::write_map(bool display_confirmation)
{
	try {
		get_map_context().save();
		if (display_confirmation) {
			gui2::show_transient_message(gui().video(), "", _("Map saved."));
		}
	} catch (editor_map_save_exception& e) {
		gui2::show_transient_message(gui().video(), "", e.what());
		return false;
	}
	return true;
}

size_t editor_controller::check_open_map(const std::string& fn) const
{
	size_t i = 0;
	while (i < map_contexts_.size() && map_contexts_[i]->get_filename() != fn) ++i;
	return i;
}


bool editor_controller::check_switch_open_map(const std::string& fn)
{
	size_t i = check_open_map(fn);
	if (i < map_contexts_.size()) {
		gui2::show_transient_message(gui().video(), _("This map is already open."), fn);
		switch_context(i);
		return true;
	}
	return false;
}

void editor_controller::load_map(const std::string& filename, bool new_context)
{
	if (new_context && check_switch_open_map(filename)) return;
	LOG_ED << "Load map: " << filename << (new_context ? " (new)" : " (same)") << "\n";
	try {
		util::unique_ptr<map_context> mc(new map_context(game_config_, filename));
		if (mc->get_filename() != filename) {
			if (new_context && check_switch_open_map(mc->get_filename())) return;
		}
		if (new_context) {
			int new_id = add_map_context(mc.release());
			switch_context(new_id);
		} else {
			replace_map_context(mc.release());
		}
		if (get_map_context().is_embedded()) {
			const char* msg = _("Loaded embedded map data");
			gui2::show_transient_message(gui().video(), _("Map loaded from scenario"), msg);
		} else {
			if (get_map_context().get_filename() != filename) {
				if (get_map_context().get_map_data_key().empty()) {
					ERR_ED << "Internal error, map context filename changed: "
						<< filename << " -> " << get_map_context().get_filename()
						<< " with no apparent scenario load\n";
				} else {
					utils::string_map symbols;
					symbols["old"] = filename;
					const char* msg = _("Loaded referenced map file:\n"
						"$new");
					symbols["new"] = get_map_context().get_filename();
					symbols["map_data"] = get_map_context().get_map_data_key();
					gui2::show_transient_message(gui().video(), _("Map loaded from scenario"),
						vgettext2(msg, symbols));
				}
			}
		}
	} catch (editor_map_load_exception& e) {
		gui2::show_transient_message(gui().video(), _("Error loading map"), e.what());
		return;
	}
}

void editor_controller::revert_map()
{
	if (!confirm_discard()) return;
	std::string filename = get_map_context().get_filename();
	if (filename.empty()) {
		ERR_ED << "Empty filename in map revert\n";
		return;
	}
	load_map(filename, false);
}

void editor_controller::new_map(int width, int height, t_translation::t_terrain fill, bool new_context)
{
	editor_map m(game_config_, width, height, fill);
	if (new_context) {
		int new_id = add_map_context(new map_context(m));
		switch_context(new_id);
	} else {
		replace_map_context(new map_context(m));
	}
}

void editor_controller::reload_map()
{
	gui().reload_map();
	get_map_context().set_needs_reload(false);
	get_map_context().reset_starting_position_labels(gui());
	refresh_all();
}

void editor_controller::refresh_all()
{
	gui().rebuild_all();
	get_map_context().set_needs_terrain_rebuild(false);
	gui().redraw_everything();
	get_map_context().clear_changed_locations();
	gui().recalculate_minimap();
}

void editor_controller::refresh_after_action(bool drag_part)
{
	if (get_map_context().needs_reload()) {
		reload_map();
		return;
	} else {
		if (get_map_context().needs_terrain_rebuild()) {
			if ((auto_update_transitions_ == preferences::editor::TransitionUpdateMode::on)
			|| ((auto_update_transitions_ == preferences::editor::TransitionUpdateMode::partial)
			&& (!drag_part || get_map_context().everything_changed()))) {
				gui().rebuild_all();
				get_map_context().set_needs_terrain_rebuild(false);
				gui().invalidate_all();
			} else {
				BOOST_FOREACH (const map_location& loc, get_map_context().changed_locations()) {
					gui().rebuild_terrain(loc);
				}
				gui().invalidate(get_map_context().changed_locations());
			}
		} else {
			if (get_map_context().everything_changed()) {
				gui().invalidate_all();
			} else {
				gui().invalidate(get_map_context().changed_locations());
			}
		}
		if (get_map_context().needs_labels_reset()) {
			get_map_context().reset_starting_position_labels(gui());
		}
	}
	get_map_context().clear_changed_locations();
	gui().recalculate_minimap();
}

void editor_controller::execute_command2(int command, const std::string& sparam)
{
	using namespace gui2;
	switch (command) {
		case HOTKEY_UNDO:
			undo();
			return;
		case HOTKEY_REDO:
			redo();
			return;
		
		case teditor_theme::HOTKEY_EDITOR_MAP:
			do_map();
			return;
		case teditor_theme::HOTKEY_UP:
			gui_->scroll_up();
			return;
		case teditor_theme::HOTKEY_DOWN:
			gui_->scroll_down();
			return;

		case teditor_theme::HOTKEY_EDITOR_TERRAIN_GROUP:
			change_terrain_group(palette_->current_group_id());
			return;

		case teditor_theme::HOTKEY_EDITOR_BRUSH:
			change_brush();
			return;

		case HOTKEY_SYSTEM:
			system();
			return;

		default:
			controller_base::execute_command2(command, sparam);
	}
}

void editor_controller::change_terrain_group(const std::string& id) const
{
	std::vector<gui2::tval_str> items;
	int actived_index = 0;
	
	const std::vector<terrain_group>& groups = palette_->terrain_groups();
	for (std::vector<terrain_group>::const_iterator it = groups.begin(); it != groups.end(); ++ it) {
		items.push_back(gui2::tval_str(std::distance(groups.begin(), it), it->name));
		if (id == it->id) {
			actived_index = std::distance(groups.begin(), it);
		}
	}
	
	gui2::tcombo_box dlg(items, actived_index);
	dlg.show(gui_->video());

	int selected = dlg.selected_index();
	palette_->set_group(groups[selected].id);
	gui_->reload_terrain_palette(palette_->terrains());

	gui_->widget_set_pip_image("editor-terrain-group", "buttons/button_selectable_45_border-pressed-both.png", groups[selected].icon);
}

void editor_controller::change_brush()
{
	std::vector<gui2::tval_str> items;
	int actived_index = 0;
	
	for (std::vector<brush>::iterator it = brushes_.begin(); it != brushes_.end(); ++ it) {
		items.push_back(gui2::tval_str(std::distance(brushes_.begin(), it), it->name()));
		if (brush_ == &*it) {
			actived_index = std::distance(brushes_.begin(), it);
		}
	}
	
	gui2::tcombo_box dlg(items, actived_index);
	dlg.show(gui_->video());

	int selected = dlg.selected_index();
	brush_ = &brushes_[selected];

	gui_->widget_set_pip_image("editor-brush", "buttons/button_selectable_45_border-pressed-both.png", brush_->image());
}

void editor_controller::system()
{
	enum {_LOAD, _SAVE, _PREFERENCES, _SETTINGS, _QUIT};

	int retval;
	std::vector<gui2::tsystem::titem> items;
	std::vector<int> rets;
	bool map_is_temporary = false, map_is_dirty = false;
	{
		if (mode_ != SIEGE) {
			items.push_back(gui2::tsystem::titem(_("Load Map")));
			rets.push_back(_LOAD);
		}

		std::string str = _("Save Map");
		if (mode_ == SIEGE) {
			str = tintegrate::generate_format(str, "blue");
		}

		const std::string& name = get_map_context().get_filename();
		map_is_temporary = name.empty() || is_directory(name);
		map_is_dirty = !map_modified().empty();
		items.push_back(gui2::tsystem::titem(str, map_is_temporary || map_is_dirty));
		rets.push_back(_SAVE);

		items.push_back(gui2::tsystem::titem(_("Preferences")));
		rets.push_back(_PREFERENCES);

		items.push_back(gui2::tsystem::titem(_("Editor Settings")));
		rets.push_back(_SETTINGS);
		
		items.push_back(gui2::tsystem::titem(_("Quit")));
		rets.push_back(_QUIT);

		gui2::tsystem dlg(items);
		try {
			dlg.show(gui_->video());
			retval = dlg.get_retval();
		} catch(twml_exception& e) {
			e.show(*gui_);
			return;
		}
		if (retval == gui2::twindow::OK) {
			return;
		}
	}
	if (rets[retval] == _LOAD) {
		load_map_dialog();

	} else if (rets[retval] == _SAVE) {
		if (mode_ == SIEGE) {
			std::map<int, std::string> block;
			std::string map_data;
			block.insert(std::make_pair((int)http::block_tag_map, get_map_context().get_map().write()));
			http::membership m = http::upload_data(*gui_, heros_, block, false);
			if (m.uid >= 0) {
				group.from_local_membership(*gui_, heros_, m, true);
				// for siege mode, it pass memory block to map-editor subsystem, filename is invalid.
				// don't call save_map(), it will write data to filename, of course will reault fail.
				// but requrie call clear_modified, so tell other clean.
				// save_map();
				get_map_context().clear_modified();
			}
		} else {
			save_map();
		}

	} else if (rets[retval] == _PREFERENCES) {
		preferences();

	} else if (rets[retval] == _SETTINGS) {
		editor_settings_dialog();

	} else if (rets[retval] == _QUIT) {
		quit_confirm(EXIT_NORMAL);
	}
}

void editor_controller::do_map()
{
	enum {_RESIZE, _ROTATE, _GENERATE};

	int retval;
	std::vector<gui2::tsystem::titem> items;
	std::vector<int> rets;
	bool map_is_temporary = false, map_is_dirty = false;
	{
		items.push_back(gui2::tsystem::titem(_("Resize Map")));
		rets.push_back(_RESIZE);

		// items.push_back(gui2::tsystem::titem(_("Rotate Map")));
		// rets.push_back(_ROTATE);

		items.push_back(gui2::tsystem::titem(_("Generate Map")));
		rets.push_back(_GENERATE);

		gui2::tsystem dlg(items);
		try {
			dlg.show(gui_->video());
			retval = dlg.get_retval();
		} catch(twml_exception& e) {
			e.show(*gui_);
			return;
		}
		if (retval == gui2::twindow::OK) {
			return;
		}
	}
	if (rets[retval] == _RESIZE) {
		resize_map_dialog();

	} else if (rets[retval] == _ROTATE) {
		
	} else if (rets[retval] == _GENERATE) {
		generate_map_dialog();
	}
}

void editor_controller::click_terrain(int tselect)
{
	palette_->click_terrain(tselect);
	set_mouseover_overlay();
}

void editor_controller::expand_open_maps_menu(std::vector<std::string>& items)
{
	for (unsigned int i = 0; i < items.size(); ++i) {
		if (items[i] == "editor-switch-map") {
			items.erase(items.begin() + i);
			std::vector<std::string> contexts;
			for (size_t mci = 0; mci < map_contexts_.size(); ++mci) {
				std::string filename = map_contexts_[mci]->get_filename();
				if (filename.empty()) {
					filename = _("(New Map)");
				}
				std::string label = "[" + lexical_cast<std::string>(mci) + "] "
					+ filename;
				if (map_contexts_[mci]->is_embedded()) {
					label += " (E)";
				}
				contexts.push_back(label);
			}
			items.insert(items.begin() + i, contexts.begin(), contexts.end());
			break;
		}
	}
}

void editor_controller::cycle_brush()
{
	if (brush_ == &brushes_.back()) {
		brush_ = &brushes_.front();
	} else {
		++brush_;
	}
	update_mouse_action_highlights();
}

void editor_controller::preferences()
{
	preferences::show_preferences_dialog(*gui_, true);
	gui_->redraw_everything();
}

void editor_controller::toggle_grid()
{
	preferences::set_grid(*gui_, !preferences::grid());
	gui_->invalidate_all();
}

void editor_controller::copy_selection()
{
	if (!get_map().selection().empty()) {
		clipboard_ = map_fragment(get_map(), get_map().selection());
		clipboard_.center_by_mass();
	}
}

void editor_controller::cut_selection()
{
	copy_selection();
	perform_refresh(editor_action_paint_area(get_map().selection(), background_terrain_));
}

void editor_controller::export_selection_coords()
{
	std::stringstream ssx, ssy;
	std::set<map_location>::const_iterator i = get_map().selection().begin();
	if (i != get_map().selection().end()) {
		ssx << "x = " << i->x + 1;
		ssy << "y = " << i->y + 1;
		++i;
		while (i != get_map().selection().end()) {
			ssx << ", " << i->x + 1;
			ssy << ", " << i->y + 1;
			++i;
		}
		ssx << "\n" << ssy.str() << "\n";
		copy_to_clipboard(ssx.str(), false);
	}
}

void editor_controller::fill_selection()
{
	perform_refresh(editor_action_paint_area(get_map().selection(), foreground_terrain_));
}

std::string left_button_function;

void editor_controller::hotkey_set_mouse_action(int command)
{
	std::map<int, mouse_action*>::iterator i = mouse_actions_.find(command);
	if (i != mouse_actions_.end()) {
		mouse_action_ = i->second;
		set_mouseover_overlay();
		redraw_toolbar();
		left_button_function = hotkey::get_hotkey(command).get_description();
		gui().invalidate_game_status();
	} else {
		ERR_ED << "Invalid hotkey command ("
			<< static_cast<int>(command) << ") passed to set_mouse_action\n";
	}
}

bool editor_controller::is_mouse_action_set(int command) const
{
	std::map<int, mouse_action*>::const_iterator i = mouse_actions_.find(command);
	return (i != mouse_actions_.end()) && (i->second == mouse_action_);
}

void editor_controller::update_mouse_action_highlights()
{
	DBG_ED << __func__ << "\n";
	int x, y;
	SDL_GetMouseState(&x, &y);
	map_location hex_clicked = gui().hex_clicked_on(x,y);
	get_mouse_action()->update_brush_highlights(gui(), hex_clicked);
}


events::mouse_handler_base& editor_controller::get_mouse_handler_base()
{
	return *this;
}

mouse_action* editor_controller::get_mouse_action()
{
	return mouse_action_;
}

void editor_controller::perform_delete(editor_action* action)
{
	if (action) {
		boost::scoped_ptr<editor_action> action_auto(action);
		get_map_context().perform_action(*action);
	}
}

void editor_controller::perform_refresh_delete(editor_action* action, bool drag_part /* =false */)
{
	if (action) {
		boost::scoped_ptr<editor_action> action_auto(action);
		perform_refresh(*action, drag_part);
	}
}

void editor_controller::perform_refresh(const editor_action& action, bool drag_part /* =false */)
{
	get_map_context().perform_action(action);
	refresh_after_action(drag_part);
}

void editor_controller::redraw_toolbar()
{
	toolbar_dirty_ = false;
}

void editor_controller::refresh_image_cache()
{
	image::flush_cache();
	refresh_all();
}

void editor_controller::display_redraw_callback(display&)
{
	gui().invalidate_all();

	if (!palette_->terrain_groups().empty()) {
		gui_->widget_set_pip_image("editor-terrain-group", "buttons/button_selectable_45_border-pressed-both.png", palette_->terrain_groups().front().icon);
	}
	if (!brushes_.empty()) {
		gui_->widget_set_pip_image("editor-brush", "buttons/button_selectable_45_border-pressed-both.png", brush_->image());
	}
	if (mode_ == SIEGE) {
		gui_->set_theme_object_active("editor-map", false);
		gui_->set_theme_object_active("editor-brush", false);
	}
}

void editor_controller::undo()
{
	get_map_context().undo();
	refresh_after_action();
}

void editor_controller::redo()
{
	get_map_context().redo();
	refresh_after_action();
}

void editor_controller::mouse_motion(int x, int y, const bool browse)
{
	mouse_handler_base::mouse_motion(x, y, browse);

	if (mouse_handler_base::mouse_motion_default(x, y)) return;
	map_location hex_clicked = gui().hex_clicked_on(x, y);
	if (get_map().on_board_with_border(drag_from_hex_) && is_dragging()) {
		editor_action* a = NULL;
		bool partial = false;
		editor_action* last_undo = get_map_context().last_undo_action();
		if (dragging_left_ && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(1)) != 0) {
			if (!get_map().on_board_with_border(hex_clicked)) return;
			a = get_mouse_action()->drag_left(*gui_, x, y, partial, last_undo);
		} else if (dragging_right_ && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(3)) != 0) {
			if (!get_map().on_board_with_border(hex_clicked)) return;
			a = get_mouse_action()->drag_right(*gui_, x, y, partial, last_undo);
		}
		//Partial means that the mouse action has modified the
		//last undo action and the controller shouldn't add
		//anything to the undo stack (hence a different
		//perform_ call)
		if (a != NULL) {
			boost::scoped_ptr<editor_action> aa(a);
			if (partial) {
				get_map_context().perform_partial_action(*a);
			} else {
				get_map_context().perform_action(*a);
			}
			refresh_after_action(true);
		}
	} else {
		get_mouse_action()->move(*gui_, hex_clicked);
	}
	gui().highlight_hex(hex_clicked);
}

bool editor_controller::allow_mouse_wheel_scroll(int x, int y)
{
	return get_map().on_board_with_border(gui().hex_clicked_on(x,y));
}

bool editor_controller::right_click_show_menu(int /*x*/, int /*y*/, const bool /*browse*/)
{
	return get_mouse_action()->has_context_menu();
}

bool editor_controller::left_mouse_down(int x, int y, const bool browse)
{
	clear_mouseover_overlay();
	if (mouse_handler_base::left_mouse_down(x, y, browse)) return true;
	LOG_ED << "Left click, after generic handling\n";
	map_location hex_clicked = gui().hex_clicked_on(x, y);
	if (!get_map().on_board_with_border(hex_clicked)) return true;
	LOG_ED << "Left click action " << hex_clicked.x << " " << hex_clicked.y << "\n";
	editor_action* a = get_mouse_action()->click_left(*gui_, x, y);
	perform_refresh_delete(a, true);
	return false;
}

void editor_controller::left_drag_end(int x, int y, const bool /*browse*/)
{
	editor_action* a = get_mouse_action()->drag_end(*gui_, x, y);
	perform_delete(a);
}

void editor_controller::left_mouse_up(int x, int y, bool motions, const bool /*browse*/)
{
	editor_action* a = get_mouse_action()->up_left(*gui_, x, y);
	perform_delete(a);
	refresh_after_action();
	set_mouseover_overlay();
	map_type.handle_err();
}

bool editor_controller::right_click(int x, int y, const bool browse)
{
	clear_mouseover_overlay();
	if (mouse_handler_base::right_click(x, y, browse)) return true;
	LOG_ED << "Right click, after generic handling\n";
	map_location hex_clicked = gui().hex_clicked_on(x, y);
	if (!get_map().on_board_with_border(hex_clicked)) return true;
	LOG_ED << "Right click action " << hex_clicked.x << " " << hex_clicked.y << "\n";
	editor_action* a = get_mouse_action()->click_right(*gui_, x, y);
	perform_refresh_delete(a, true);
	return false;
}

void editor_controller::right_drag_end(int x, int y, const bool /*browse*/)
{
	editor_action* a = get_mouse_action()->drag_end(*gui_, x, y);
	perform_delete(a);
}

void editor_controller::right_mouse_up(int x, int y, const bool /*browse*/)
{
	editor_action* a = get_mouse_action()->up_right(*gui_, x, y);
	perform_delete(a);
	refresh_after_action();
	set_mouseover_overlay();
}

void editor_controller::process_keyup_event(const SDL_Event& event)
{
	editor_action* a = get_mouse_action()->key_event(gui(), event);
	perform_refresh_delete(a);
	set_mouseover_overlay();
}

void editor_controller::set_mouseover_overlay()
{
	get_mouse_action()->set_mouse_overlay(gui());
}

void editor_controller::clear_mouseover_overlay()
{
	gui().clear_mouseover_hex_overlay();
}

} //end namespace editor
