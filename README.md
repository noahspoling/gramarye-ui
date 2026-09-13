# gramarye-ui

UI layout for Gramarye games and the launcher: a thin C facade over
[Clay](https://github.com/nicbarker/clay) with Clay's official raylib renderer.
Consumed via CMake FetchContent, exactly like `gramarye-ecs` / `gramarye-libcore`.

Clay is pinned (currently **v0.14**) and fetched internally; consumers never see
Clay's build. The library also exposes `clay.h` on its public include path so you
can write `CLAY({...})` / `CLAY_TEXT(...)` layout directly.

## Consuming

```cmake
FetchContent_Declare(gramarye_ui
    GIT_REPOSITORY "https://github.com/noahspoling/gramarye-ui.git"
    GIT_TAG "main")
FetchContent_MakeAvailable(gramarye_ui)
target_link_libraries(<your_target> PRIVATE gramarye-ui)
```

`raylib` is reused from the consumer if the target already exists; otherwise it
is fetched standalone.

## Usage

```c
#include "gramarye_ui/ui.h"
#include "clay.h"

GramaryeUI_init(width, height);
Font fonts[1] = { GetFontDefault() };
GramaryeUI_set_fonts(fonts, 1);

// per frame, between BeginDrawing()/EndDrawing():
GramaryeUI_begin(dt);
CLAY({ .id = CLAY_ID("Root"),
       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
    CLAY_TEXT(CLAY_STRING("hello"),
              CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = { 255,255,255,255 } }));
}
GramaryeUI_end_and_render();

GramaryeUI_shutdown();
```

## API

| Function | Purpose |
|---|---|
| `bool GramaryeUI_init(int w, int h)` | allocate Clay arena, initialize Clay |
| `void GramaryeUI_set_fonts(Font *fonts, int count)` | font table for measure + render |
| `void GramaryeUI_begin(float dt)` | push dims/pointer/scroll, open layout |
| `void GramaryeUI_end_and_render(void)` | close layout, issue raylib draws |
| `void GramaryeUI_shutdown(void)` | free Clay arena + unload owned textures |

### Textures, nine-patches & custom draw

For image-based skinning. The library owns a small registry; ids are 1-based
(0 = none) and pointers stay stable for the program's life, so they're safe to
hand to Clay image configs. Call after the window exists (these touch the GPU).

| Function | Purpose |
|---|---|
| `int GramaryeUI_load_texture(const char *path)` | load + register; unloaded at shutdown |
| `int GramaryeUI_register_texture(Texture2D t)` | register a caller-owned texture |
| `const Texture2D *GramaryeUI_texture(int id)` | stable pointer, or NULL |
| `void GramaryeUI_clear_textures(void)` | unload owned + clear (auto on shutdown) |
| `int GramaryeUI_register_ninepatch(int tex, Rectangle src, int l,t,r,b)` | nine-patch over a texture |
| `const void *GramaryeUI_ninepatch(int id)` | opaque descriptor for the image node |
| `void GramaryeUI_set_custom_draw(fn, user)` | drawer for `custom` nodes (reserve a rect, draw your world) |

`GramaryeUI_load_texture` is path-keyed and idempotent: calling it again with a
path already owned by the registry returns the existing id instead of burning
a fresh slot, so re-entering a scene or an F5 hot-reload that re-binds the same
skin image by path repeatedly won't fill the fixed-size table (slots are
otherwise only reclaimed at shutdown).

The vendored raylib renderer (`src/gramarye_renderer_raylib.c`) started as a
faithful copy of Clay's official renderer (`renderers/raylib/clay_renderer_raylib.c`,
v0.14) and diverges in exactly three places: nine-patch drawing via
`DrawTextureNPatch` in the `IMAGE` case, dispatch to the registered
`GramaryeUI_CustomDrawFn` in the `CUSTOM` case (instead of upstream's 3D-model
demo struct), and a `TraceLog` warning instead of `exit(1)` on an unknown
command in the default case. Text/rectangle/border/scissor handling is
byte-for-byte the upstream behavior. It is included directly into
`gramarye_ui.c` (not compiled standalone) after `clay.h` and after the
nine-patch descriptor / custom-draw globals are defined.

The border-beam effect (`GramaryeUI_set_border_beam`, up to
`GRAMARYE_UI_MAX_BORDER_BEAMS` concurrent slots) draws an SDF rounded-rect
border with two glow "heads" whose position is computed at draw time — so it
tracks the element's actual this-frame bounding box with zero extra lag —
using Euclidean distance to each head (not just distance-along-the-border),
which is what makes the glow bleed diagonally into the interior near each head
instead of staying a thin ring.

Text measurement (`Raylib_MeasureText`) walks UTF-8 codepoints rather than raw
bytes, so multibyte glyphs (×, →, …) measure correctly and match what
`DrawTextEx` renders (`GetGlyphIndex` falls back to `?` for missing glyphs on
both sides); walking bytes instead would index far out of the glyph array for
multibyte characters and inflate the measured width.

In the `IMAGE` render case, `imageData` is a pointer into gramarye-ui's static
registry: either a bare `Texture2D*` (from `GramaryeUI_texture`) or a
`GramaryeNinePatch*` (from `GramaryeUI_ninepatch`). The magic sentinel
disambiguates the two — a real `Texture2D`'s first field is its small GL id,
never the magic value.

## Lua bindings (`GRAMARYE_UI_LUA=ON`)

Build with `-DGRAMARYE_UI_LUA=ON` (needs a `lua_static` target). This compiles
`gramarye_ui_lua_impl.c` and registers, on `GramaryeUI_register_lua(L)`:

| Function | Purpose |
|---|---|
| `gramarye.ui.render(node, ...)` | render one or more element trees this frame |
| `gramarye.ui.hovered(id) -> bool` | was element hovered last frame (one-frame lag; safe during layout) |
| `gramarye.ui.screen_w() / screen_h() -> int` | actual window dimensions (not virtual resolution) |
| `gramarye.ui.mouse_pos() -> x, y` | unified pointer (touch on Android, mouse elsewhere) — same source `GramaryeUI_begin` feeds Clay; for drag interactions that need continuous position |
| `gramarye.ui.mouse_down() -> bool` | pointer held this frame (not edge-triggered; pair with a caller-side "was down last frame" check for press/release edges) |
| `gramarye.ui.load_texture(path) -> id\|nil` | caller prepends any asset prefix |
| `gramarye.ui.texture_size(id) -> w, h` | `0, 0` if the id is invalid |
| `gramarye.ui.ninepatch(tex_id, sx,sy,sw,sh, l,t,r,b) -> id\|nil` | register a nine-patch over a texture |
| `gramarye.ui.time() -> seconds` | raylib `GetTime()`; for caret blink / easing |
| `gramarye.ui.text_input() -> string` | UTF-8 characters typed this frame ("" if none); drains raylib's char queue, so call once per frame from the focused widget |
| `gramarye.ui.edit_key(name) -> bool` | pressed or auto-repeating this frame; self-contained editing keys (`backspace`, `delete`, `left`, `right`, `home`, `end`, `escape`, `enter`) for text widgets, no host input dependency |
| `gramarye.ui.measure_text(text, size [, font]) -> w, h` | measured with a registered font |
| `gramarye.ui.scroll_info(id) -> offset_y, viewport_h, content_h` | `0,0,0` if no such scroll container yet; `offset_y` grows as you scroll down; one-frame lag |
| `gramarye.ui.element_rect(id) -> x, y, w, h` | all `0` if not laid out yet; one-frame lag, same as `hovered()`/`scroll_info()`; lets Lua position an overlay (e.g. a border-pulse effect) around an element whose size isn't known ahead of layout, like a fit-sized Popup |
| `gramarye.ui.set_border_beam(slot, {beam=, base=, radius=, border_width=, speed=, glow_radius=})` | configure a border-beam slot; cheap, safe to call every frame the effect is visible; pair with a `custom` node whose kind is `border_beam_kind(slot)` |
| `gramarye.ui.border_beam_kind(slot) -> int` | the `kind` a `custom` node needs to render that beam slot |
| `gramarye.platform` | `"desktop" \| "android" \| "web"` |

Element-tree node types (`_type`): `text`, `frame`, `scroll`, `image`, `custom`.
An `image` is a frame whose background is a texture (`tex=`) or nine-patch
(`nine=`) — it keeps all id / layout / border / event plumbing, so skinned
buttons stay interactive. `custom` reserves a laid-out rect (`kind=<int>`) for
`GramaryeUI_set_custom_draw`.

`layout.w` / `layout.h` sizing accepts a plain number (`CLAY_SIZING_FIXED`),
`{ grow = true }` (`GROW`), `{ pct = n }` (`PERCENT`), or nothing (`FIT`).

Any `frame`/`image`/`custom` node can carry a `floating` field to be
positioned out-of-flow (tooltips, dropdowns, modals, border-beam overlays):
`{ to = "parent" | "root", to_id = "<id>", attach = <preset> | { element =, parent = },
x =, y =, z =, passthrough = bool }`. `to_id` (attach to a specific element by
id) takes precedence over `to`. `attach` presets are `"below"`, `"above"`,
`"right"`, `"left"`, `"center"`, or an explicit
`{ element = "<point>", parent = "<point>" }` pair, where `<point>` is one of
`left_top`, `left_center`, `left_bottom`, `center_top`, `center_center`,
`center_bottom`, `right_top`, `right_center`, `right_bottom`.

A few more field formats on `frame`/`scroll`/`image` nodes: `layout.pad`
accepts a number (uniform), `{h=, v=}`, or `{left=, right=, top=, bottom=}`;
`layout.align` accepts `"center"` or `{x="center"|"right", y="center"|"bottom"}`;
`radius` accepts a number for all four corners or `{tl=, tr=, bl=, br=}` for
just some (e.g. a title bar rounded only on top so it sits flush against a
square-cornered body below it); `border` is `{color={r,g,b,a}, width=n}`
(Clay v0.14 uses one color and per-side widths). A `scroll` node's child
offset is looked up by id from the previous frame's `Clay_ScrollContainerData`
rather than `Clay_GetScrollOffset()`, because the element declaration is built
before the element opens, so the live scroll offset isn't available yet at
that point.

### Embedded Lua std layer (versioned with the C ABI)

When Lua is enabled, the library embeds its own component library + theme engine
as C byte arrays (generated by `cmake/embed_lua.cmake` from `lua/gramarye/*.lua`)
and injects them into `package.preload`, so `require("gramarye.ui")` /
`require("gramarye.theme")` resolve straight from the binary — **zero loose files,
clean on Android, and frozen in lockstep with the C ABI per tag.** Pinning one
gramarye-ui tag pins both the C behavior and the `gramarye.ui` / `gramarye.theme`
Lua API together.

- `gramarye.ui` — genre-agnostic **primitives** only: `Frame`, `Text`, `Image`,
  `ScrollFrame`, `Row`, `Column`, `Spacer`, `Separator`, `Button`, `Label`,
  `Title`, `Panel`, `Overlay` (+ `component`/sizing helpers). They return plain
  element tables for `gramarye.ui.render`. Game-domain widgets (inventory slots,
  item cards, trade rows, settings rows, header bars, …) are **not** shipped
  here — a game composes them on top of these primitives, in its own scripts,
  and wires them to its ECS/event systems. See the template's
  `assets/scripts/lib/widgets.lua` for that game-side layer.
- `gramarye.theme` — `skins` defaults (for the primitives only), `apply(overrides)`,
  and `bind_image(skin_name, { nine=id } | { tex=id })`. A game registers its own
  widget skins via `theme.apply { … }`. Image skins are strictly opt-in: with no
  binding (or a missing atlas) components render their color skin exactly as
  before — graceful fallback, no crash.

```lua
local ui    = require("gramarye.ui")
local theme = require("gramarye.theme")
local atlas = gramarye.ui.load_texture("textures/ui_atlas.png")  -- nil if absent
if atlas then
    local w, h = gramarye.ui.texture_size(atlas)
    theme.bind_image("button", { nine = gramarye.ui.ninepatch(atlas, 0,0,w,h, 12,12,12,12) })
end
-- ui.Button{ id="go", label="Go", on_click=… } now renders the nine-patch skin
```

### `gramarye.theme`

Colors are `{r, g, b, a}` (0-255 integers), matching Clay and raylib. `M.skins`
holds the default skin table for the library's own primitives only —
game-domain widgets should register their skins from the game side via
`theme.apply{...}` rather than growing this table. `M.MIN_TAP_H` is the
minimum tap-target height (56px on Android per Google's 48dp HIG plus a little
fudge, 36px elsewhere).

- `theme.apply(overrides)` — shallow-merges a `{ skin_name = { field = value } }`
  table into `M.skins`, adding the skin if it doesn't exist yet.
- `theme.bind_image(skin_name, { nine = id } | { tex = id })` — attaches a
  texture/nine-patch (registered via `gramarye.ui.*`) to a named skin; pass
  `nil` to clear a binding and fall back to the color skin. `M.images` holds
  these bindings, empty by default.

### `gramarye.anim`

Immediate-mode UI redraws the whole tree every frame, so "animation" here
means keeping a small eased value per element id and nudging it toward a
target each frame. There's no `dt` callback in `gramarye.ui`, so it derives
`dt` itself from `gramarye.ui.time()` deltas.

```lua
local anim = gramarye.require("gramarye.anim")
local t = anim.towards(props.id, hovered and 1 or 0, 14)
local bg = anim.lerp_color(skin_off.bg, skin_on.bg, t)
```

- `anim.towards(id, target, rate)` — eases `id`'s stored value toward `target`
  (usually 0 or 1) and returns it. `rate` (default 12) controls speed — higher
  is snappier; 8-16 reads as a fast, natural fade. Snaps to `target` once the
  gap is imperceptible, so values don't linger at e.g. `0.4999999` forever.
- `anim.value(id)` — reads the current value without advancing it (0 if never
  animated).
- `anim.reset(id)` — drops a stored id's state, e.g. when its element is
  destroyed.
- `anim.lerp(a, b, t)` / `anim.lerp_color(a, b, t)` — plain interpolation
  helpers.

### `gramarye.ui` component library

Genre-agnostic *primitives* only — layout, text, button, panel, image,
scroll, modal. Game-domain widgets (inventory slots, item cards, trade rows,
settings rows, …) belong in the game, composed on top of these primitives and
wired to the game's own ECS/event systems; keep this module free of game
concepts.

Components return pure Lua tables (element trees); call `gramarye.ui.render(tree)`
inside `on_draw` to push them into Clay. `gramarye.ui.hovered` lags one frame,
which is imperceptible at 60 fps.

Layout/primitive nodes map close to 1:1 with Clay element types: `Frame`,
`Text` (or `Text("string")` for a quick label), `Image` (texture/nine-patch
background, behaves like `Frame` otherwise), `ScrollFrame`, `Row`/`Column`
(children as the array part of props), `Spacer(size)` (grows to fill when
`size` is omitted), `Separator`, `Label`/`Title` (skinned text shorthand),
`Button` (hover skin + adaptive touch target; uses an image skin if one is
bound, else eases between color skins via `gramarye.anim`), `Panel` (column
layout by default, image-skinnable), `Overlay` (full-screen modal backdrop,
content centered). `M.grow()`, `M.pct_w(p)`, `M.pct_h(p)` are sizing
shorthands, and `M.component(render_fn)` wraps a plain function so games can
build composite widgets on top of these primitives:

```lua
local Slot = ui.component(function(props) return ui.Frame { ... } end)
Slot { id = "s1", item = ... }
```

Richer, stateful components:

- **Tooltip** — a floating bubble attached to a target element, shown only
  while it's hovered. Place it anywhere the target id is also declared this
  frame (e.g. at the end of the root); always returns a node (possibly empty),
  never `nil`, so it's safe inside a children array.
  ```lua
  ui.Tooltip { to_id = "save_btn", text = "Save the game" }
  ```
- **BorderPulse** — a ring traced around a rect's perimeter that fades from a
  base color to a glow color in two moving bands, built from many small
  overlapping segments (dense enough to read as a continuous blend) rather
  than a sparse chain of dots. Pure Lua overlay, no shader.
  ```lua
  ui.BorderPulse { x=100, y=80, w=320, h=220,
                    base={60,60,90,255}, glow={255,255,255,255},
                    speed=60, band=0.14 }
  ```
  `x/y/w/h` are the target's absolute screen rect; `band` is each glow zone's
  width as a fraction of the perimeter (0..0.5, wider = softer/broader fade).
- **BorderBeam** — shader-backed traveling border glow (real additive GPU
  blending via `gramarye.ui.set_border_beam` + a `custom` node — see
  `gramarye_ui.c`'s border-beam shader for the math), softer and brighter than
  `BorderPulse`. `id` should be stable per on-screen instance: it picks a beam
  slot and keeps it for the id's lifetime (slots aren't reclaimed, and there
  are only `GRAMARYE_UI_MAX_BORDER_BEAMS` (16) of them — meant for a handful of
  concurrently-highlighted panels, not one per list row). If the shader failed
  to compile (logged once as a warning), this silently draws nothing.
  ```lua
  ui.BorderBeam { id="win", x=100, y=80, w=320, h=220, radius=8,
                   beam={255,255,255,255}, base={60,60,90,255},
                   speed=60, glow_radius=40 }
  ```
- **Popup** — a free-floating, draggable panel positioned in screen space
  (drag by its title bar). Position/drag state persists per id across frames
  and across the popup being hidden and shown again — the caller owns
  visibility (don't render it while closed).
  ```lua
  ui.Popup { id = "inv_win", title = "Inventory", x = 120, y = 80, w = 320,
             on_close = function() state.show_inv = false end,
             ui.Label "contents go here" }
  ```
  `pulse = true` (or a props table for `BorderPulse`, e.g. `pulse = { color=... }`)
  wraps it with a traveling border glow sized off the popup's actual on-screen
  rect via `gramarye.ui.element_rect()` — accurate even when `h` is left to
  content-fit; the very first frame it's shown the rect isn't laid out yet, so
  the pulse silently sits out that frame. Only the title bar's top corners are
  rounded to match the popup's radius — Clay doesn't clip a child's background
  to its parent's radius, so a fully-square bar would visibly overhang the
  outer frame's rounded top corners. Dragging is clamped so the title bar (the
  only way to drag the popup back) always stays reachable on-screen.
- **Dropdown** — select-one from a flat list of strings. Click the trigger to
  open a floating option list below it; click a value (or click outside) to
  close. A full-screen backdrop behind the open list catches outside clicks;
  it's rendered after the trigger in the tree so clicking the trigger itself
  (toggle-to-close) still lands consistently.
  ```lua
  ui.Dropdown { id = "difficulty", items = {"Easy","Normal","Hard"},
                selected = 2, on_select = function(i, v) ... end, w = 160 }
  ```
- **List / Grid** — virtualized, pull-model containers. Data stays in the
  game's C/ECS code: pass a row `count` and a `cell(i)` callback that reads
  row `i` (1-based). Only the rows visible in the scroll viewport are laid
  out, so a 100k-row list costs roughly viewport-many `cell()` calls per
  frame.
  ```lua
  ui.List { id="units", count=C.count(), row_h=28, h=240, selected=sel,
            on_select=function(i) ... end,
            cell=function(i, st) return ui.Label(C.name(i)) end }
  ui.Grid { id="inv", count=C.count(), cols=6, cell_h=56, gap=8,
            cell=function(i) return SlotFor(i) end }
  ```
  `Grid` renders every row when no `h` is given; passing `h` makes it a
  virtualized scroll container, laying out `cols` cells per row.
- **TextBox** — single-line, controlled text input. Value lives with the
  caller: pass `value`, update it in `on_change`. Focus/cursor state is
  module-local; UTF-8 aware via Lua's `utf8` library.
  ```lua
  ui.TextBox { id="name", value=state.name, placeholder="Name",
               on_change=function(s) state.name = s end }
  ```

## Roadmap

Shipped here: image / nine-patch skinning, the embedded versioned Lua layer, and
a custom-draw seam. Next, building on the same renderer and registry foundation:

- Floating elements / tooltips (Clay `Clay_FloatingElementConfig`).
- An animation/tween state store (Clay is layout-only / immediate).
- A Lua-facing custom-draw API to embed game worlds (map / minimap) in the layout.
- Focus / keyboard / gamepad routing beyond the pointer-only event model.
- Rich text + text input; grid / wrap containers + list virtualization.
- Font loading helpers (TTF table) beyond the raylib default font.
