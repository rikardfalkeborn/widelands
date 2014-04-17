include "scripting/formatting.lua"
include "scripting/format_help.lua"

set_textdomain("tribe_barbarians")

return {
   func = function(building_description)
	return

		--rt(h1(_"The Barbarian Deeper Gold Mine")) ..
	--Lore Section
		--text identical to goldmine
	building_help_lore_string("barbarians", building_description, _[[‘Soft and supple.<br> And yet untouched by time and weather.<br> Rays of sun, wrought into eternity ...’]], _[[Excerpt from ‘Our Treasures Underground’,<br> a traditional Barbarian song.]]) ..

	--General Section
	building_help_general_string("barbarians", building_description, "goldore",
		_"Digs gold ore out of the ground in mountain terrain.",
		_"This mine exploits all of the resource down to the deepest level. But even after having done so, it will still have a %s chance of finding some more gold ore.":bformat("10%"),
		"2") ..

	--Dependencies
	building_help_inputs("barbarians", building_description, {"big_inn"}, "meal") ..
-- TODO the following line causes a crash
--	building_help_outputs("barbarians", building_description, {"smelting_works"}) ..

	--Workers Section
	building_help_crew_string("barbarians", building_description, {"master-miner", "chief-miner", "miner"}, "pick") ..

	--Building Section
	building_help_building_section("barbarians", building_description, "deep_goldmine", {"goldmine", "deep_goldmine"}) ..

	--Production Section
		rt(h2(_"Production")) ..
		text_line(_"Performance:", _"If the food supply is steady, this mine can produce gold ore in %s on average.":bformat("18.5s"))
  end
}
