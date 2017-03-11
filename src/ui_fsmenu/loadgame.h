/*
 * Copyright (C) 2002-2017 by the Widelands Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef WL_UI_FSMENU_LOADGAME_H
#define WL_UI_FSMENU_LOADGAME_H

#include "ui_fsmenu/base.h"

#include <memory>

#include "graphic/image.h"
#include "logic/game_controller.h"
#include "ui_basic/box.h"
#include "ui_basic/button.h"
#include "ui_basic/textarea.h"
#include "ui_fsmenu/load_map_or_game.h"
#include "wui/gamedetails.h"
#include "wui/load_or_save_game.h"

/// Select a Saved Game in Fullscreen Mode. It's a modal fullscreen menu.
class FullscreenMenuLoadGame : public FullscreenMenuLoadMapOrGame {
public:
	FullscreenMenuLoadGame(Widelands::Game&,
	                       GameSettingsProvider* gsp,
	                       GameController* gc = nullptr,
	                       bool is_replay = false);

	const std::string& filename() {
		return filename_;
	}

	void think() override;

	bool handle_key(bool down, SDL_Keysym code) override;

protected:
	void clicked_ok() override;
	void entry_selected() override;
	void fill_table() override;

private:
	void layout() override;

	/// Updates buttons and text labels and returns whether a table entry is selected.
	bool compare_date_descending(uint32_t, uint32_t);
	void clicked_delete();

	UI::Box main_box_;
	UI::Box info_box_;
	UI::Textarea title_;
	LoadOrSaveGame load_or_save_;

	UI::Button delete_;
	std::string filename_;

	bool is_replay_;
	Widelands::Game& game_;
	GameSettingsProvider* settings_;
	GameController* ctrl_;
};

#endif  // end of include guard: WL_UI_FSMENU_LOADGAME_H
