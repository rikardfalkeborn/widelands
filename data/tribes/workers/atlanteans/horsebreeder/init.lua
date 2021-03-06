dirname = path.dirname(__file__)

animations = {
   idle = {
      pictures = path.list_files(dirname .. "idle_??.png"),
      hotspot = { 8, 23 },
   }
}
add_directional_animation(animations, "walk", dirname, "walk", {35, 28}, 10)

tribes:new_worker_type {
   msgctxt = "atlanteans_worker",
   name = "atlanteans_horsebreeder",
   -- TRANSLATORS: This is a worker name used in lists of workers
   descname = pgettext("atlanteans_worker", "Horse Breeder"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   vision_range = 2,

   buildcost = {
      atlanteans_carrier = 1
   },

   animations = animations,
}
