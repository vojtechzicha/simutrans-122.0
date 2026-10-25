# Landscaping tool icons (fork)

Toolbar icons of the fork's landscaping tools (`tool_landscape_t`, `bauer/landscaper.*`). The game adds
the tools to the toolbar that holds the raise land tool (pak128.cs: "Landscaping Tools") after a
separator, each with the menu skin of its name; a tool whose icon is missing stays out of the
toolbar.

| Skin             | Tool parameter | Tool                                                  |
|------------------|----------------|-------------------------------------------------------|
| `ForkLandLevel`  | `L`            | level an area at the first tile's height (Ctrl+click picks a height) |
| `ForkLandRaise`  | `+`            | raise an area by one level                            |
| `ForkLandLower`  | `-`            | lower an area by one level                            |
| `ForkLandRamp`   | `R`            | ramp from the first tile's height to the last one's   |
| `ForkLandSmooth` | `S`            | smooth an area                                        |

- `make_icons.py` draws `landscape_icons.png` (one row of 32x32 icons, Pillow).
- `landscape_icons.dat` names them; `tools/windows/steam-fork.sh landscape` builds them with the
  fork's makeobj (`pak32`) into the pakset folder as `fork-landscape-icons.pak`.

A menuconf.tab that names `general_tool[45]` (`TOOL_LANDSCAPE`) itself, e.g.
`toolbar[1][23]=general_tool[45],ForkLandLevel,,L`, places the tools there instead; the game then
adds none.
