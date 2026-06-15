-- gramarye/theme.lua — skin registry + theming for the gramarye.ui component lib.
--
-- Shipped embedded inside gramarye-ui (versioned with the C ABI). A game tweaks
-- the look without forking the library:
--
--   local theme = gramarye.require("gramarye.theme")
--   theme.apply { button = { radius = 10 } }          -- override skin fields
--   local atlas = gramarye.ui.load_texture("textures/ui_atlas.png")
--   local np    = gramarye.ui.ninepatch(atlas, 0,0,48,48, 12,12,12,12)
--   theme.bind_image("button", { nine = np })          -- opt-in image skin
--
-- Colors are {r, g, b, a} (0–255 integers), matching Clay and raylib.

local M = {}

local is_mobile = gramarye.platform == "android"

-- Minimum tap-target height (Google HIG: 48dp; a little fudge on top).
M.MIN_TAP_H = is_mobile and 56 or 36

-- ─── Skin registry ────────────────────────────────────────────────────────────
-- Skins for the library *primitives* only. Game-domain widgets register their own
-- skins from the game side via M.apply { ... } — keep this table genre-agnostic.
M.skins = {
    -- Buttons
    button        = { bg={40,40,64,255},   text={220,220,220,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_hover  = { bg={64,64,110,255},  text={255,255,255,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_danger = { bg={90,30,30,255},   text={255,200,200,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_danger_hover = { bg={140,40,40,255}, text={255,255,255,255}, radius=6, font_size=18, pad={h=20,v=10} },

    -- Panels / containers
    panel         = { bg={15,15,28,230},   radius=10 },
    panel_dark    = { bg={10,10,20,245},   radius=10 },

    -- Text styles
    label         = { color={200,200,200,255}, size=16 },
    label_dim     = { color={130,130,150,255}, size=14 },
    title         = { color={255,255,255,255}, size=28 },
    subtitle      = { color={180,180,200,255}, size=20 },
    hint          = { color={100,160,220,255}, size=14 },

    -- Tooltip (floating hint bubble)
    tooltip       = { bg={10,10,22,240}, text={210,210,220,255}, radius=4, font_size=14, pad={h=8,v=6} },

    -- Text input
    textbox       = { bg={20,20,34,255}, border={60,60,85,255}, text={225,225,235,255},
                      placeholder={120,120,140,255}, radius=5, font_size=16, pad={h=10,v=6} },
    textbox_focus = { bg={26,26,44,255}, border={90,140,220,255}, border_focus={90,140,220,255} },

    -- Selectable list rows
    list_row      = { bg={0,0,0,0}, hover={30,30,50,200}, selected={50,50,90,255},
                      text={210,210,220,255}, radius=4, pad={h=10,v=0} },

    -- Misc
    separator     = { color={50,50,72,255} },
}

-- ─── Image bindings ───────────────────────────────────────────────────────────
-- skin name → { nine = <ninepatch id> } or { tex = <texture id> }. Empty by
-- default: with no bindings every component renders its color skin exactly as
-- before. Image skins are strictly opt-in.
M.images = {}

-- Shallow-merge `overrides` (a table of skin_name → {field=value}) into M.skins.
function M.apply(overrides)
    for name, fields in pairs(overrides) do
        local dst = M.skins[name]
        if dst then
            for k, v in pairs(fields) do dst[k] = v end
        else
            M.skins[name] = fields
        end
    end
end

-- Attach a texture / nine-patch (registered via gramarye.ui.*) to a named skin.
-- Pass nil to clear a binding (fall back to the color skin).
function M.bind_image(skin_name, binding)
    M.images[skin_name] = binding
end

return M
