> [!CAUTION]
> This plugin is very much in alpha. Do not expect things to work or to be stable at all.
> **DO NOT USE THIS PLUGIN IF YOU INTEND TO EDIT AND SAVE NON-LATIN FILES**.

This plugin lets you use all the fonts installed on your system in Lite XL without
specifying their filenames!

To use it, just install the plugins (build WIP) and add the following in `init.lua`:

```lua
local style = require "core.style"
local systemfonts = require "plugins.systemfonts"
style.code_font = systemfonts.load("JetBrains Mono", 14 * SCALE)
```

This would allow you to load JetBrains Mono from your system, or other related fonts
if JetBrains Mono is not installed. Use `monospace`, `sans-serif` for other font types.
Refer to [fontconfig documentation].

## Unicode Support

There is a very limited Unicode support since the font width and height are still very off.
Some of these problems may have workarounds, some may not.

## How it works

This plugin uses [fontconfig] to enumerate system fonts in an efficient way.
It provides a mostly-compatible `systemfonts.font` that replaces `renderer.font`,
and replaces `renderer.draw_text` with a version of it that supports `systemfonts.font`.
Everything is done in C to maximize performance (because replacing `renderer.draw_text`
is very expensive).


[fontconfig]: https://www.freedesktop.org/wiki/Software/fontconfig/
[fontconfig documentation]: https://fontconfig.pages.freedesktop.org/fontconfig/fontconfig-user.html