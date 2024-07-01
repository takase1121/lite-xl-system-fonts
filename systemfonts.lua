--mod-version:3

local systemfonts = require "libraries.systemfonts"

local r = { draw_text = renderer.draw_text }

systemfonts.setup(r, renderer.font)
renderer.draw_text = systemfonts.draw_text

return systemfonts