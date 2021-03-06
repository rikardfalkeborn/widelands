/*
 * Copyright (C) 2002-2019 by the Widelands Development Team
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

#include "logic/map_objects/immovable.h"

#include <memory>

#include "graphic/animation/animation_manager.h"
#include "graphic/rendertarget.h"
#include "io/fileread.h"
#include "io/filewrite.h"
#include "logic/game_data_error.h"
#include "logic/map_objects/immovable_program.h"
#include "logic/map_objects/terrain_affinity.h"
#include "logic/map_objects/world/world.h"
#include "logic/player.h"
#include "logic/widelands_geometry_io.h"
#include "map_io/world_legacy_lookup_table.h"

namespace Widelands {

BaseImmovable::BaseImmovable(const MapObjectDescr& mo_descr) : MapObject(&mo_descr) {
}

int32_t BaseImmovable::string_to_size(const std::string& size) {
	if (size == "none")
		return BaseImmovable::NONE;
	if (size == "small")
		return BaseImmovable::SMALL;
	if (size == "medium")
		return BaseImmovable::MEDIUM;
	if (size == "big")
		return BaseImmovable::BIG;
	throw GameDataError("Unknown size %s.", size.c_str());
}

std::string BaseImmovable::size_to_string(int32_t size) {
	switch (size) {
	case BaseImmovable::NONE:
		return "none";
	case BaseImmovable::SMALL:
		return "small";
	case BaseImmovable::MEDIUM:
		return "medium";
	case BaseImmovable::BIG:
		return "big";
	default:
		NEVER_HERE();
	}
}

static std::string const base_immovable_name = "unknown";

/**
 * Associate the given field with this immovable. Recalculate if necessary.
 *
 * Only call this during init.
 *
 * \note this function will remove the immovable (if existing) currently connected to this position.
 */
void BaseImmovable::set_position(EditorGameBase& egbase, const Coords& c) {
	assert(c);

	Map* map = egbase.mutable_map();
	FCoords f = map->get_fcoords(c);
	if (f.field->immovable && f.field->immovable != this)
		f.field->immovable->remove(egbase);

	f.field->immovable = this;

	if (get_size() >= SMALL) {
		map->recalc_for_field_area(egbase, Area<FCoords>(f, 2));
	}
}

/**
 * Remove the link to the given field.
 *
 * Only call this during cleanup.
 */
void BaseImmovable::unset_position(EditorGameBase& egbase, const Coords& c) {
	Map* map = egbase.mutable_map();
	FCoords const f = map->get_fcoords(c);

	// this is to help to debug failing assertion below (see bug 1542238)
	if (f.field->immovable != this) {
		log(" Internal error: Immovable at %3dx%3d does not match: is %s but %s was expected.\n", c.x,
		    c.y, (f.field->immovable) ? f.field->immovable->descr().name().c_str() : "None",
		    descr().name().c_str());
	}

	assert(f.field->immovable == this);

	f.field->immovable = nullptr;
	egbase.inform_players_about_immovable(f.field - &(*map)[0], nullptr);

	if (get_size() >= SMALL) {
		map->recalc_for_field_area(egbase, Area<FCoords>(f, 2));
	}
}

/*
==============================================================================

ImmovableDescr IMPLEMENTATION

==============================================================================
*/

/**
 * Parse a common immovable functions from init file.
 */
ImmovableDescr::ImmovableDescr(const std::string& init_descname,
                               const LuaTable& table,
                               MapObjectDescr::OwnerType input_type)
   : MapObjectDescr(MapObjectType::IMMOVABLE, table.get_string("name"), init_descname, table),
     size_(BaseImmovable::NONE),
     owner_type_(input_type),
     editor_category_(nullptr) {
	if (!is_animation_known("idle")) {
		throw GameDataError("Immovable %s has no idle animation", table.get_string("name").c_str());
	}
	if (input_type == MapObjectDescr::OwnerType::kTribe && helptext_script().empty()) {
		throw GameDataError("Tribe immovable %s has no helptext script", name().c_str());
	}

	if (table.has_key("size")) {
		size_ = BaseImmovable::string_to_size(table.get_string("size"));
	}

	if (table.has_key("terrain_affinity")) {
		terrain_affinity_.reset(new TerrainAffinity(*table.get_table("terrain_affinity"), name()));
	}

	if (table.has_key("attributes")) {
		std::vector<std::string> attributes =
		   table.get_table("attributes")->array_entries<std::string>();
		add_attributes(attributes, {MapObject::Attribute::RESI});

		// All resource indicators must have a menu icon
		for (const std::string& attribute : attributes) {
			if (attribute == "resi") {
				if (icon_filename().empty()) {
					throw GameDataError("Resource indicator %s has no menu icon", name().c_str());
				}
				break;
			}
		}

		// Old trees get an extra species name so we can use it in help lists.
		bool is_tree = false;
		for (const std::string& attribute : attributes) {
			if (attribute == "tree") {
				is_tree = true;
				break;
			}
		}
		if (is_tree) {
			if (!table.has_key("species")) {
				throw wexception(
				   "Immovable '%s' with type 'tree' must define a species", name().c_str());
			}
			species_ = table.get_string("species");
		}
	}

	std::unique_ptr<LuaTable> programs = table.get_table("programs");
	for (std::string program_name : programs->keys<std::string>()) {
		std::transform(program_name.begin(), program_name.end(), program_name.begin(), tolower);
		if (programs_.count(program_name)) {
			throw GameDataError("Program '%s' has already been declared for immovable '%s'",
			                    program_name.c_str(), name().c_str());
		}
		try {
			programs_[program_name] = new ImmovableProgram(
			   program_name, programs->get_table(program_name)->array_entries<std::string>(), *this);
		} catch (const std::exception& e) {
			throw GameDataError("%s: Error in immovable program %s: %s", name().c_str(),
			                    program_name.c_str(), e.what());
		}
	}

	make_sure_default_program_is_there();
}

/**
 * Parse a world immovable from its init file.
 */
ImmovableDescr::ImmovableDescr(const std::string& init_descname,
                               const LuaTable& table,
                               const World& world)
   : ImmovableDescr(init_descname, table, MapObjectDescr::OwnerType::kWorld) {

	const DescriptionIndex editor_category_index =
	   world.editor_immovable_categories().get_index(table.get_string("editor_category"));
	if (editor_category_index == Widelands::INVALID_INDEX) {
		throw GameDataError(
		   "Unknown editor_category: %s\n", table.get_string("editor_category").c_str());
	}
	editor_category_ = world.editor_immovable_categories().get_mutable(editor_category_index);
}

/**
 * Parse a tribes immovable from its init file.
 *
 * The contents of 'table' are documented in
 * /data/tribes/immovables/ashes/init.lua
 */
ImmovableDescr::ImmovableDescr(const std::string& init_descname,
                               const LuaTable& table,
                               const Tribes& tribes)
   : ImmovableDescr(init_descname, table, MapObjectDescr::OwnerType::kTribe) {
	if (table.has_key("buildcost")) {
		buildcost_ = Buildcost(table.get_table("buildcost"), tribes);
	}
}

const EditorCategory* ImmovableDescr::editor_category() const {
	return editor_category_;
}

bool ImmovableDescr::has_terrain_affinity() const {
	return terrain_affinity_ != nullptr;
}

const TerrainAffinity& ImmovableDescr::terrain_affinity() const {
	return *terrain_affinity_;
}

void ImmovableDescr::make_sure_default_program_is_there() {
	if (!programs_.count("program")) {  //  default program
		assert(is_animation_known("idle"));
		std::vector<std::string> arguments{"idle"};
		programs_["program"] =
		   new ImmovableProgram("program", std::unique_ptr<ImmovableProgram::Action>(
		                                      new ImmovableProgram::ActAnimate(arguments, *this)));
	}
}

/**
 * Cleanup
 */
ImmovableDescr::~ImmovableDescr() {
	while (programs_.size()) {
		delete programs_.begin()->second;
		programs_.erase(programs_.begin());
	}
}

/**
 * Find the program of the given name.
 */
ImmovableProgram const* ImmovableDescr::get_program(const std::string& program_name) const {
	Programs::const_iterator const it = programs_.find(program_name);

	if (it == programs_.end())
		throw GameDataError(
		   "immovable %s has no program \"%s\"", name().c_str(), program_name.c_str());

	return it->second;
}

/**
 * Create an immovable of this type
 * If this immovable was created by a building, 'former_building' can be set
 * in order to display information about it.
 */
Immovable& ImmovableDescr::create(EditorGameBase& egbase,
                                  const Coords& coords,
                                  const BuildingDescr* former_building_descr) const {
	Immovable& result = *new Immovable(*this, former_building_descr);
	result.position_ = coords;
	result.init(egbase);
	return result;
}

/*
==============================

IMPLEMENTATION

==============================
*/

Immovable::Immovable(const ImmovableDescr& imm_descr,
                     const Widelands::BuildingDescr* former_building_descr)
   : BaseImmovable(imm_descr),
     former_building_descr_(former_building_descr),
     anim_(0),
     animstart_(0),
     program_(nullptr),
     program_ptr_(0),
     anim_construction_total_(0),
     anim_construction_done_(0),
     program_step_(0) {
}

Immovable::~Immovable() {
}

BaseImmovable::PositionList Immovable::get_positions(const EditorGameBase&) const {
	PositionList rv;

	rv.push_back(position_);
	return rv;
}

void BaseImmovable::set_owner(Player* player) {
	assert(owner_ == nullptr);
	owner_ = player;
}

int32_t Immovable::get_size() const {
	return descr().get_size();
}

bool Immovable::get_passable() const {
	return descr().get_size() < BIG;
}

void Immovable::start_animation(const EditorGameBase& egbase, uint32_t const anim) {
	anim_ = anim;
	animstart_ = egbase.get_gametime();
	anim_construction_done_ = anim_construction_total_ = 0;
}

void Immovable::increment_program_pointer() {
	program_ptr_ = (program_ptr_ + 1) % program_->size();
	action_data_.reset(nullptr);
}

/**
 * Actually initialize the immovable.
 */
bool Immovable::init(EditorGameBase& egbase) {
	BaseImmovable::init(egbase);

	set_position(egbase, position_);

	//  Set animation data according to current program state.
	ImmovableProgram const* prog = program_;
	if (!prog) {
		prog = descr().get_program("program");
	}
	assert(prog != nullptr);

	if (upcast(ImmovableProgram::ActAnimate const, act_animate, &(*prog)[program_ptr_])) {
		start_animation(egbase, act_animate->animation());
	}

	if (upcast(Game, game, &egbase)) {
		switch_program(*game, "program");
	}
	return true;
}

/**
 * Cleanup before destruction
 */
void Immovable::cleanup(EditorGameBase& egbase) {
	unset_position(egbase, position_);

	BaseImmovable::cleanup(egbase);
}

/**
 * Switch the currently running program.
 */
void Immovable::switch_program(Game& game, const std::string& program_name) {
	program_ = descr().get_program(program_name);
	assert(program_ != nullptr);
	program_ptr_ = 0;
	program_step_ = 0;
	action_data_.reset(nullptr);
	schedule_act(game, 1);
}

/**
 * Run program timer.
 */
void Immovable::act(Game& game, uint32_t const data) {
	BaseImmovable::act(game, data);

	if (program_step_ <= game.get_gametime()) {
		//  Might delete itself!
		(*program_)[program_ptr_].execute(game, *this);
	}
}

void Immovable::draw(uint32_t gametime,
                     const InfoToDraw info_to_draw,
                     const Vector2f& point_on_dst,
                     const Widelands::Coords& coords,
                     float scale,
                     RenderTarget* dst) {
	if (!anim_) {
		return;
	}
	if (!anim_construction_total_) {
		dst->blit_animation(point_on_dst, coords, scale, anim_, gametime - animstart_);
		if (former_building_descr_) {
			do_draw_info(
			   info_to_draw, former_building_descr_->descname(), "", point_on_dst, scale, dst);
		}
	} else {
		draw_construction(gametime, info_to_draw, point_on_dst, coords, scale, dst);
	}
}

void Immovable::draw_construction(const uint32_t gametime,
                                  const InfoToDraw info_to_draw,
                                  const Vector2f& point_on_dst,
                                  const Widelands::Coords& coords,
                                  const float scale,
                                  RenderTarget* dst) {
	const ImmovableProgram::ActConstruct* constructionact = nullptr;
	if (program_ptr_ < program_->size())
		constructionact =
		   dynamic_cast<const ImmovableProgram::ActConstruct*>(&(*program_)[program_ptr_]);

	const uint32_t steptime = constructionact ? constructionact->buildtime() : 5000;

	uint32_t done = 0;
	if (anim_construction_done_ > 0) {
		done = steptime * (anim_construction_done_ - 1);
		done += std::min(steptime, gametime - animstart_);
	}

	uint32_t total = anim_construction_total_ * steptime;
	if (done > total)
		done = total;

	const Animation& anim = g_gr->animations().get_animation(anim_);
	const size_t nr_frames = anim.nr_frames();
	uint32_t frametime = g_gr->animations().get_animation(anim_).frametime();
	uint32_t units_per_frame = (total + nr_frames - 1) / nr_frames;
	const size_t current_frame = done / units_per_frame;

	assert(get_owner() != nullptr);  // Who would build something they do not own?
	const RGBColor& player_color = get_owner()->get_playercolor();
	if (current_frame > 0) {
		// Not the first pic, so draw the previous one in the back
		dst->blit_animation(point_on_dst, Widelands::Coords::null(), scale, anim_,
		                    (current_frame - 1) * frametime, &player_color);
	}

	const int percent = ((done % units_per_frame) * 100) / units_per_frame;

	dst->blit_animation(
	   point_on_dst, coords, scale, anim_, current_frame * frametime, &player_color, percent);

	// Additionally, if statistics are enabled, draw a progression string
	do_draw_info(
	   info_to_draw, descr().descname(),
	   g_gr->styles().color_tag((boost::format(_("%i%% built")) % (100 * done / total)).str(),
	                            g_gr->styles().building_statistics_style().construction_color()),
	   point_on_dst, scale, dst);
}

/**
 * Set the current action's data to \p data.
 *
 * \warning \p data must not be equal to the currently set data, but it may be 0.
 */
void Immovable::set_action_data(ImmovableActionData* data) {
	action_data_.reset(data);
}

/*
==============================

Load/save support

==============================
*/

// We neeed 2 packet versions for map loading: Packet version 7 will load in older versions of
// Widelands, so we have a dynamic version number - it is only set higher than
// kCurrentPacketVersionImmovableNoFormerBuildings during saving if we have an immovable with
// a former building assigned to it.
constexpr uint8_t kCurrentPacketVersionImmovableNoFormerBuildings = 7;
constexpr uint8_t kCurrentPacketVersionImmovable = 8;

// Supporting older versions for map loading
void Immovable::Loader::load(FileRead& fr, uint8_t const packet_version) {
	BaseImmovable::Loader::load(fr);

	Immovable& imm = dynamic_cast<Immovable&>(*get_object());

	// Supporting older versions for map loading
	if (packet_version >= 5) {
		PlayerNumber pn = fr.unsigned_8();
		if (pn && pn <= kMaxPlayers) {
			Player* plr = egbase().get_player(pn);
			if (!plr)
				throw GameDataError("Immovable::load: player %u does not exist", pn);
			imm.set_owner(plr);
		}
	}

	// Position
	imm.position_ = read_coords_32(&fr, egbase().map().extent());
	imm.set_position(egbase(), imm.position_);

	if (packet_version > kCurrentPacketVersionImmovableNoFormerBuildings) {
		Player* owner = imm.get_owner();
		if (owner) {
			DescriptionIndex idx = owner->tribe().building_index(fr.string());
			if (owner->tribe().has_building(idx)) {
				imm.set_former_building(*owner->tribe().get_building_descr(idx));
			}
		}
	}

	// Animation. If the animation is no longer known, pick the main animation instead.
	char const* const animname = fr.c_string();
	if (imm.descr().is_animation_known(animname)) {
		imm.anim_ = imm.descr().get_animation(animname, &imm);
	} else {
		log("Unknown animation '%s' for immovable '%s', using main animation instead.\n", animname,
		    imm.descr().name().c_str());
		imm.anim_ = imm.descr().main_animation();
	}

	imm.animstart_ = fr.signed_32();
	if (packet_version >= 4) {
		imm.anim_construction_total_ = fr.unsigned_32();
		if (imm.anim_construction_total_)
			imm.anim_construction_done_ = fr.unsigned_32();
	}

	{  //  program
		std::string program_name;
		if (1 == packet_version) {
			program_name = fr.unsigned_8() ? fr.c_string() : "program";
			std::transform(program_name.begin(), program_name.end(), program_name.begin(), tolower);
		} else {
			program_name = fr.c_string();
			if (program_name.empty())
				program_name = "program";
		}
		imm.program_ = imm.descr().get_program(program_name);
	}
	imm.program_ptr_ = fr.unsigned_32();

	if (!imm.program_) {
		imm.program_ptr_ = 0;
	} else {
		if (imm.program_ptr_ >= imm.program_->size()) {
			// Try to not fail if the program of some immovable has changed
			// significantly.
			// Note that in some cases, the immovable may end up broken despite
			// the fixup, but there isn't really anything we can do against that.
			log("Warning: Immovable '%s', size of program '%s' seems to have "
			    "changed.\n",
			    imm.descr().name().c_str(), imm.program_->name().c_str());
			imm.program_ptr_ = 0;
		}
	}

	if (packet_version > 6) {
		imm.program_step_ = fr.unsigned_32();
	} else {
		imm.program_step_ = fr.signed_32();
	}

	if (packet_version >= 3 && packet_version <= 5) {
		imm.reserved_by_worker_ = fr.unsigned_8();
	}
	if (packet_version >= 4) {
		std::string dataname = fr.c_string();
		if (!dataname.empty()) {
			imm.set_action_data(ImmovableActionData::load(fr, imm, dataname));
		}
	}
}

void Immovable::Loader::load_pointers() {
	BaseImmovable::Loader::load_pointers();
}

void Immovable::Loader::load_finish() {
	BaseImmovable::Loader::load_finish();

	Immovable& imm = dynamic_cast<Immovable&>(*get_object());
	if (upcast(Game, game, &egbase()))
		imm.schedule_act(*game, 1);

	egbase().inform_players_about_immovable(
	   Map::get_index(imm.position_, egbase().map().get_width()), &imm.descr());
}

void Immovable::save(EditorGameBase& egbase, MapObjectSaver& mos, FileWrite& fw) {
	// This is in front because it is required to obtain the description
	// necessary to create the Immovable
	fw.unsigned_8(HeaderImmovable);
	const uint8_t packet_version = former_building_descr_ == nullptr ?
	                                  kCurrentPacketVersionImmovableNoFormerBuildings :
	                                  kCurrentPacketVersionImmovable;
	fw.unsigned_8(packet_version);

	if (descr().owner_type() == MapObjectDescr::OwnerType::kTribe) {
		if (get_owner() == nullptr)
			log(" Tribe immovable '%s' has no owner!! ", descr().name().c_str());
		fw.c_string("tribes");
	} else {
		fw.c_string("world");
	}

	fw.string(descr().name());

	// The main loading data follows
	BaseImmovable::save(egbase, mos, fw);

	fw.unsigned_8(get_owner() ? get_owner()->player_number() : 0);
	write_coords_32(&fw, position_);
	if (get_owner() && former_building_descr_) {
		assert(packet_version > kCurrentPacketVersionImmovableNoFormerBuildings);
		fw.string(former_building_descr_->name());
	} else {
		assert(packet_version == kCurrentPacketVersionImmovableNoFormerBuildings);
	}

	// Animations
	fw.string(descr().get_animation_name(anim_));
	fw.signed_32(animstart_);
	fw.unsigned_32(anim_construction_total_);
	if (anim_construction_total_)
		fw.unsigned_32(anim_construction_done_);

	// Program Stuff
	fw.string(program_ ? program_->name() : "");

	fw.unsigned_32(program_ptr_);
	fw.unsigned_32(program_step_);

	if (action_data_) {
		fw.c_string(action_data_->name());
		action_data_->save(fw, *this);
	} else {
		fw.c_string("");
	}
}

MapObject::Loader* Immovable::load(EditorGameBase& egbase,
                                   MapObjectLoader& mol,
                                   FileRead& fr,
                                   const WorldLegacyLookupTable& world_lookup_table,
                                   const TribesLegacyLookupTable& tribes_lookup_table) {
	std::unique_ptr<Loader> loader(new Loader);

	try {
		// The header has been peeled away by the caller
		uint8_t const packet_version = fr.unsigned_8();
		// Supporting older versions for map loading
		if (1 <= packet_version && packet_version <= kCurrentPacketVersionImmovable) {

			const std::string owner_type = fr.c_string();
			Immovable* imm = nullptr;

			if (owner_type != "world") {  //  It is a tribe immovable.
				const std::string name = tribes_lookup_table.lookup_immovable(fr.c_string());
				const DescriptionIndex idx = egbase.tribes().immovable_index(name);
				if (idx != Widelands::INVALID_INDEX) {
					imm = new Immovable(*egbase.tribes().get_immovable_descr(idx));
				} else {
					throw GameDataError("tribes do not define immovable type \"%s\"", name.c_str());
				}
			} else {  //  world immovable
				const World& world = egbase.world();
				const std::string name = world_lookup_table.lookup_immovable(fr.c_string());
				const DescriptionIndex idx = world.get_immovable_index(name.c_str());
				if (idx == Widelands::INVALID_INDEX) {
					throw GameDataError("world does not define immovable type \"%s\"", name.c_str());
				}
				imm = new Immovable(*world.get_immovable_descr(idx));
			}

			loader->init(egbase, mol, *imm);
			loader->load(fr, packet_version);
		} else {
			throw UnhandledVersionError("Immovable", packet_version, kCurrentPacketVersionImmovable);
		}
	} catch (const std::exception& e) {
		throw wexception("immovable type: %s", e.what());
	}

	return loader.release();
}

/**
 * For an immovable that is currently in construction mode, return \c true and
 * compute the remaining buildcost.
 *
 * If the immovable is not currently in construction mode, return \c false.
 */
bool Immovable::construct_remaining_buildcost(Game& /* game */, Buildcost* buildcost) {
	ActConstructData* d = get_action_data<ActConstructData>();
	if (!d)
		return false;

	const Buildcost& total = descr().buildcost();
	for (Buildcost::const_iterator it = total.begin(); it != total.end(); ++it) {
		uint32_t delivered = d->delivered[it->first];
		if (delivered < it->second)
			(*buildcost)[it->first] = it->second - delivered;
	}

	return true;
}

/**
 * For an immovable that is currently in construction mode, return \c true and
 * consume the given ware type as delivered.
 *
 * If the immovable is not currently in construction mode, return \c false.
 */
bool Immovable::construct_ware(Game& game, DescriptionIndex index) {
	ActConstructData* d = get_action_data<ActConstructData>();
	if (!d)
		return false;

	molog("construct_ware: index %u", index);

	Buildcost::iterator it = d->delivered.find(index);
	if (it != d->delivered.end())
		it->second++;
	else
		d->delivered[index] = 1;

	anim_construction_done_ = d->delivered.total();
	animstart_ = game.get_gametime();

	molog("construct_ware: total %u delivered: %u", index, d->delivered[index]);

	Buildcost remaining;
	construct_remaining_buildcost(game, &remaining);

	const ImmovableProgram::ActConstruct* action =
	   dynamic_cast<const ImmovableProgram::ActConstruct*>(&(*program_)[program_ptr_]);
	assert(action != nullptr);

	if (remaining.empty()) {
		// Wait for the last building animation to finish.
		program_step_ = schedule_act(game, action->buildtime());
	} else {
		program_step_ = schedule_act(game, action->decaytime());
	}

	return true;
}

/*
==============================================================================

PlayerImmovable IMPLEMENTATION

==============================================================================
*/

/**
 * Zero-initialize
 */
PlayerImmovable::PlayerImmovable(const MapObjectDescr& mo_descr)
   : BaseImmovable(mo_descr), ware_economy_(nullptr), worker_economy_(nullptr) {
}

/**
 * Cleanup
 */
PlayerImmovable::~PlayerImmovable() {
	if (workers_.size())
		log("PlayerImmovable::~PlayerImmovable: %" PRIuS " workers left!\n", workers_.size());
}

/**
 * Change the economy, transfer the workers
 */
void PlayerImmovable::set_economy(Economy* const e, WareWorker type) {
	if (get_economy(type) == e) {
		return;
	}

	(type == wwWARE ? ware_economy_ : worker_economy_) = e;

	for (uint32_t i = 0; i < workers_.size(); ++i) {
		workers_[i]->set_economy(e, type);
	}
}

/**
 * Associate the given worker with this immovable.
 * The worker will be transferred along to another economy, and it will be
 * released when the immovable is destroyed.
 *
 * This should only be called from Worker::set_location.
 */
void PlayerImmovable::add_worker(Worker& w) {
	workers_.push_back(&w);
}

/**
 * Disassociate the given worker with this building.
 *
 * This should only be called from Worker::set_location.
 */
void PlayerImmovable::remove_worker(Worker& w) {
	for (Workers::iterator worker_iter = workers_.begin(); worker_iter != workers_.end();
	     ++worker_iter)
		if (*worker_iter == &w) {
			*worker_iter = *(workers_.end() - 1);
			return workers_.pop_back();
		}

	throw wexception("PlayerImmovable::remove_worker: not in list");
}

void Immovable::set_former_building(const BuildingDescr& building) {
	if (descr().owner_type() == MapObjectDescr::OwnerType::kTribe && get_owner() == nullptr)
		throw wexception("Set '%s' as former building for Tribe immovable '%s', but it has no owner.",
		                 building.name().c_str(), descr().name().c_str());
	former_building_descr_ = &building;
}

/**
 * Set the immovable's owner. Currently, it can only be set once.
 */
void PlayerImmovable::set_owner(Player* new_owner) {
	assert(owner_ == nullptr);
	owner_ = new_owner;
	Notifications::publish(NoteImmovable(this, NoteImmovable::Ownership::GAINED));
}

/**
 * Initialize the immovable.
 */
bool PlayerImmovable::init(EditorGameBase& egbase) {
	return BaseImmovable::init(egbase);
}

/**
 * Release workers
 */
void PlayerImmovable::cleanup(EditorGameBase& egbase) {
	while (!workers_.empty())
		workers_[0]->set_location(nullptr);

	Notifications::publish(NoteImmovable(this, NoteImmovable::Ownership::LOST));

	BaseImmovable::cleanup(egbase);
}

/**
 * We are the destination of the given ware's transfer, which is not associated
 * with any request.
 */
void PlayerImmovable::receive_ware(Game&, DescriptionIndex ware) {
	throw wexception("MO(%u): Received a ware(%u), do not know what to do with it", serial(), ware);
}

/**
 * We are the destination of the given worker's transfer, which is not
 * associated with any request.
 */
void PlayerImmovable::receive_worker(Game&, Worker& worker) {
	throw wexception(
	   "MO(%u): Received a worker(%u), do not know what to do with it", serial(), worker.serial());
}

/**
 * Dump general information
 */
void PlayerImmovable::log_general_info(const EditorGameBase& egbase) const {
	BaseImmovable::log_general_info(egbase);

	FORMAT_WARNINGS_OFF
	molog("this: %p\n", this);
	molog("owner_: %p\n", owner_);
	FORMAT_WARNINGS_ON
	molog("player_number: %i\n", owner_->player_number());
	FORMAT_WARNINGS_OFF
	molog("ware_economy_: %p\n", ware_economy_);
	molog("worker_economy_: %p\n", worker_economy_);
	FORMAT_WARNINGS_ON
}

constexpr uint8_t kCurrentPacketVersionPlayerImmovable = 1;

PlayerImmovable::Loader::Loader() {
}

void PlayerImmovable::Loader::load(FileRead& fr) {
	BaseImmovable::Loader::load(fr);

	PlayerImmovable& imm = get<PlayerImmovable>();

	try {
		uint8_t packet_version = fr.unsigned_8();

		if (packet_version == kCurrentPacketVersionPlayerImmovable) {
			PlayerNumber owner_number = fr.unsigned_8();

			if (!owner_number || owner_number > egbase().map().get_nrplayers())
				throw GameDataError("owner number is %u but there are only %u players", owner_number,
				                    egbase().map().get_nrplayers());

			Player* owner = egbase().get_player(owner_number);
			if (!owner)
				throw GameDataError("owning player %u does not exist", owner_number);

			imm.owner_ = owner;
		} else {
			throw UnhandledVersionError(
			   "PlayerImmovable", packet_version, kCurrentPacketVersionPlayerImmovable);
		}
	} catch (const std::exception& e) {
		throw wexception("loading player immovable: %s", e.what());
	}
}

void PlayerImmovable::save(EditorGameBase& egbase, MapObjectSaver& mos, FileWrite& fw) {
	BaseImmovable::save(egbase, mos, fw);

	fw.unsigned_8(kCurrentPacketVersionPlayerImmovable);
	fw.unsigned_8(owner().player_number());
}
}  // namespace Widelands
