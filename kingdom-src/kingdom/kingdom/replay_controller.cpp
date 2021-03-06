/* $Id: replay_controller.cpp 47511 2010-11-07 21:14:49Z shadowmaster $ */
/*
   Copyright (C) 2006 - 2010 by Joerg Hinrichs <joerg.hinrichs@alice-dsl.de>
   wesnoth playlevel Copyright (C) 2003 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include "global.hpp"

#include "game_end_exceptions.hpp"
#include "game_events.hpp"
#include "gettext.hpp"
#include "log.hpp"
#include "map_label.hpp"
#include "replay_controller.hpp"
#include "resources.hpp"
#include "savegame.hpp"
#include "unit_display.hpp"
#include "artifical.hpp"
#include "replay.hpp"
#include "wml_exception.hpp"
#include "gui/dialogs/theme2.hpp"

#include <boost/foreach.hpp>

static lg::log_domain log_engine("engine");
#define DBG_NG LOG_STREAM(debug, log_engine)

static lg::log_domain log_replay("replay");
#define DBG_REPLAY LOG_STREAM(debug, log_replay)
#define LOG_REPLAY LOG_STREAM(info, log_replay)

LEVEL_RESULT play_replay_level(const config& game_config,
		const config* level, CVideo& video, game_state& state_of_game, hero_map& heros, hero_map& heros_start, 
		card_map& cards)
{
	try {
		const int ticks = SDL_GetTicks();
		int num_turns = (*level)["turns"].to_int();

		// !!equal 
		VALIDATE(heros_start.size() == heros.size(), "play_replay_level, heros_start and heros must be same size!");
		runtime_groups::redirect_hero_map(heros_start);
		heros = heros_start;
		runtime_groups::redirect_hero_map(heros);

		replay_controller replaycontroller(*level, state_of_game, heros, heros_start, cards, ticks, num_turns, game_config, video);
		const events::command_disabler disable_commands;

		//replay event-loop
		for (; ;) {
			if (replaycontroller.is_playing()) {
				replaycontroller.play_replay2();
			}
			replaycontroller.play_slice();
		}
	}
	catch(end_level_exception&){
		DBG_NG << "play_replay_level: end_level_exception\n";
	}

	return VICTORY;
}

replay_controller::replay_controller(const config& level,
		game_state& state_of_game, hero_map& heros, hero_map& heros_start, 
		card_map& cards, 
		const int ticks, const int num_turns,
		const config& game_config, CVideo& video) :
	play_controller(level, state_of_game, heros, heros_start, cards, ticks, num_turns, game_config, video, false, true),
	teams_start_(),
	gamestate_start_(gamestate_),
	tod_manager_start_(level, num_turns, &state_of_game),
	current_turn_(1),
	delay_(0),
	is_playing_(false),
	show_everything_(false),
	show_team_(state_of_game.classification().campaign_type == "multiplayer" ? 0 : 1)
{
	// teams_start_ = teams_;

	init();

	gui_->show_context_menu();
}

replay_controller::~replay_controller()
{
	//YogiHH
	//not absolutely sure if this is needed, but it makes me feel a lot better ;-)
	//feel free to delete this if it is not necessary
}

void replay_controller::init(){
	DBG_REPLAY << "in replay_controller::init()...\n";

	//guarantee the cursor goes back to 'normal' at the end of the level
	const cursor::setter cursor_setter(cursor::NORMAL);

	teams_start_ = teams_;
	for (std::vector<team>::iterator itor = teams_start_.begin(); itor != teams_start_.end(); ++ itor) {
		std::vector<artifical*>& holden_cities = itor->holden_cities();
		holden_cities.clear();
		itor->clear_troop();
	}

	gamestate_start_ = gamestate_;

	fire_prestart(true);

	init_gui();
	statistics::fresh_stats();
	set_victory_when_enemy_no_city(
		utils::string_bool(level_["victory_when_enemy_no_city"], true));

	DBG_REPLAY << "first_time..." << (recorder.is_skipping() ? "skipping" : "no skip") << "\n";

	update_gui();
	fire_start(!loading_game_);
}

void replay_controller::init_gui(){
	DBG_NG << "Initializing GUI... " << (SDL_GetTicks() - ticks_) << "\n";
	play_controller::init_gui();

	if (show_team_)
		gui_->set_team(show_team_ - 1, show_everything_);
	else
		gui_->set_team(0, show_everything_);

	update_locker lock_display((*gui_).video(),false);
	for(std::vector<team>::iterator t = teams_.begin(); t != teams_.end(); ++t) {
		t->reset_objectives_changed();
	}
}

void replay_controller::stop_replay()
{
	is_playing_ = false;
}

void replay_controller::replay_next_turn()
{
	is_playing_ = true;
	play_turn();

	is_playing_ = false;
}

void replay_controller::replay_next_side()
{
	is_playing_ = true;
	play_side();

	if (static_cast<size_t>(player_number_) > teams_.size()) {
		player_number_ = 1;
		current_turn_++;
	}

	is_playing_ = false;
}

void replay_controller::process_oos(const std::string& msg) const
{
	std::stringstream message;
	message << _("The replay is corrupt/out of sync. It might not make much sense to continue. Do you want to save the game?");
	message << "\n\n" << _("Error details:") << "\n\n" << msg;

	config snapshot;
	to_config(snapshot);
	savegame::oos_savegame save(heros_, heros_start_, snapshot);
	save.save_game_interactive(resources::screen->video(), message.str(), false); // can throw end_level_exception
}

void replay_controller::replay_show_everything(){
	show_everything_ = true;
	show_team_ = 0;
	update_teams();
	update_gui();
}

void replay_controller::replay_show_each(){
	show_everything_ = false;
	show_team_ = 0;
	update_teams();
	update_gui();
}

void replay_controller::replay_show_team1(){
	show_everything_ = false;
	show_team_ = 1;
	update_teams();
	update_gui();
}

void replay_controller::replay_skip_animation(){
	
}

void replay_controller::play_replay()
{
	is_playing_ = true;
}

void replay_controller::play_replay2()
{
	VALIDATE(is_playing_, "is_playing_ must be true!");

	if (recorder.at_end()) {
		is_playing_ = false;
		return;
	}

	try {
		for (; !recorder.at_end() && is_playing_ && !recorder.unexpected; first_player_ = 1) {
			play_turn();
		} // end for loop
		is_playing_ = false;
	}
	catch (end_level_exception& e) {
		if (e.result == QUIT) throw;
	}
}

void replay_controller::play_turn()
{
	if (recorder.at_end()){
		return;
	}

	LOG_REPLAY << "turn: " << current_turn_ << "\n";

	gui_->new_turn();
	gui_->invalidate_game_status();
	events::raise_draw_event();

	while (!recorder.at_end() && is_playing_ && !recorder.unexpected){
		play_side();
		play_slice();
	}
}

void replay_controller::play_side()
{
	if (recorder.at_end()){
		return;
	}

	DBG_REPLAY << "Status turn number: " << turn() << "\n";
	DBG_REPLAY << "Replay_Controller turn number: " << current_turn_ << "\n";
	DBG_REPLAY << "Player number: " << player_number_ << "\n";

	try {

		int end_ticks = calculate_end_ticks();
		while (!recorder.at_end() && unit_map::main_ticks < end_ticks && !recorder.unexpected) {
			do_replay(player_number_);
		}

		if (unit_map::main_ticks == end_ticks) {
			tod_manager_.next_turn();
		}

		update_teams();
		update_gui();
	}
	catch (end_level_exception& e){
		//VICTORY/DEFEAT end_level_exception shall not return to title screen
		get_end_level_data().result = e.result;
		if (e.result == VICTORY || e.result == DEFEAT) {
			linger();
			return;
		}
		throw;
	}
}

void replay_controller::linger()
{
	browse_ = true;
	linger_ = true;
	// If we need to set the status depending on the completion state
	// we're needed here.
	gui_->set_game_mode(game_display::LINGER_MP);
	gui_->redraw_everything();

	start_pass_scenario_anim(get_end_level_data().result);
}

void replay_controller::update_teams(){
	int next_team = player_number_;
	if(static_cast<size_t>(next_team) > teams_.size()) {
		next_team = 1;
	}

	if (!show_team_)
		gui_->set_team(next_team - 1, show_everything_);

	::clear_shroud(next_team);
	gui_->set_playing_team(next_team - 1);
	gui_->invalidate_all();
}

void replay_controller::update_gui(){
	(*gui_).recalculate_minimap();
	(*gui_).redraw_minimap();
	(*gui_).invalidate_all();
	events::raise_draw_event();
	(*gui_).draw();
}

void replay_controller::preferences()
{
	play_controller::preferences();
	update_gui();
}

void replay_controller::execute_command2(int command, const std::string& sparam)
{
	using namespace gui2;

	switch(command) {
	case tgame_theme::HOTKEY_PLAY_REPLAY:
		play_replay();
		break;
	case tgame_theme::HOTKEY_STOP_REPLAY:
		stop_replay();
		break;
	default:
		play_controller::execute_command2(command, sparam);
	}
}
