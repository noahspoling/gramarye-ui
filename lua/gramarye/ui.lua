-- gramarye/ui.lua — primitive UI component library for gramarye-ui (Clay + raylib).
--
-- Shipped embedded inside gramarye-ui and versioned with the C ABI: pinning a
-- gramarye-ui tag pins this layer too. Load it with:
--
--   local ui = gramarye.require("gramarye.ui")
--
-- SCOPE: genre-agnostic *primitives* only — layout, text, button, panel, image,
-- scroll, modal. Game-domain widgets (inventory slots, item cards, trade rows,
-- settings rows, …) belong in the game, composed on top of these primitives and
-- wired to the game's own ECS/event systems. Keep this file free of game concepts.
--
-- Components return pure Lua tables (element trees). Call gramarye.ui.render(tree)
-- inside on_draw to push them into Clay. Hover state (gramarye.ui.hovered) lags
-- one frame — imperceptible at 60 fps. Skins/image bindings live in the theme
-- module; override them there to reskin without forking:
--   local theme = gramarye.require("gramarye.theme")

local theme = require("gramarye.theme")

local M = {}

-- ─── Platform ─────────────────────────────────────────────────────────────────
local is_mobile = gramarye.platform == "android"

-- Re-exported so call sites can read ui.skins / ui.MIN_TAP_H.
M.skins     = theme.skins
M.MIN_TAP_H = theme.MIN_TAP_H

-- Image fields ({nine=}/{tex=}) bound to a skin name, or nil for color-only.
local function image_for(skin_name)
    local img = theme.images[skin_name]
    if not img then return nil end
    return img.nine, img.tex
end

-- ─── Primitive nodes ──────────────────────────────────────────────────────────
-- These map 1:1 to Clay element types. Children are the array part of props.

-- Frame: generic container with optional id, bg, radius, border, layout, events.
function M.Frame(props)
    local node = { _type = "frame" }
    for k, v in pairs(props) do
        node[k] = v
    end
    return node
end

-- Text node. Can also be called as Text("string") for quick labels.
function M.Text(props)
    if type(props) == "string" then
        return { _type = "text", text = props }
    end
    return {
        _type = "text",
        text  = props.text  or "",
        size  = props.size,
        color = props.color,
        font  = props.font,
    }
end

-- Image node: a texture or nine-patch background. `nine`/`tex` are ids from
-- gramarye.ui.ninepatch / gramarye.ui.load_texture. Behaves like a Frame (id,
-- layout, events all work), so it can hold children.
function M.Image(props)
    local node = { _type = "image" }
    for k, v in pairs(props) do node[k] = v end
    return node
end

-- Scrollable frame (v_scroll=true by default).
function M.ScrollFrame(props)
    local node = { _type = "scroll" }
    for k, v in pairs(props) do node[k] = v end
    return node
end

-- ─── Layout primitives ────────────────────────────────────────────────────────

-- Horizontal row of children.
function M.Row(props)
    local node = {
        _type  = "frame",
        id     = props.id,
        bg     = props.bg,
        radius = props.radius,
        border = props.border,
        layout = {
            w     = props.w,
            h     = props.h,
            gap   = props.gap or 8,
            pad   = props.pad,
            align = props.align,
        },
        on_click = props.on_click,
        on_hover = props.on_hover,
    }
    for i, v in ipairs(props) do node[i] = v end
    return node
end

-- Vertical column of children.
function M.Column(props)
    local node = {
        _type  = "frame",
        id     = props.id,
        bg     = props.bg,
        radius = props.radius,
        border = props.border,
        layout = {
            dir   = "column",
            w     = props.w,
            h     = props.h,
            gap   = props.gap or 8,
            pad   = props.pad,
            align = props.align,
        },
        on_click = props.on_click,
        on_hover = props.on_hover,
    }
    for i, v in ipairs(props) do node[i] = v end
    return node
end

-- Grows to fill remaining space; pass a number for a fixed gap instead.
function M.Spacer(size)
    if size and size > 0 then
        return { _type = "frame", layout = { w = size, h = size } }
    end
    return { _type = "frame", layout = { w = { grow = true }, h = { grow = true } } }
end

-- Thin horizontal/vertical separator line.
function M.Separator(props)
    props = props or {}
    local s = theme.skins.separator
    return {
        _type  = "frame",
        id     = props.id,
        bg     = props.color or s.color,
        layout = {
            w = props.vertical and (props.w or 1) or { grow = true },
            h = props.vertical and { grow = true }  or (props.h or 1),
        },
    }
end

-- ─── Text primitives ──────────────────────────────────────────────────────────

-- Styled label (shorthand for Text with a skin).
function M.Label(props)
    if type(props) == "string" then
        local s = theme.skins.label
        return { _type = "text", text = props, size = s.size, color = s.color }
    end
    local s = theme.skins[props.skin or "label"]
    return {
        _type = "text",
        text  = props.text or props[1] or "",
        size  = props.size  or s.size,
        color = props.color or s.color,
        font  = props.font  or 0,
    }
end

-- Large heading text.
function M.Title(props)
    if type(props) == "string" then
        local s = theme.skins.title
        return { _type = "text", text = props, size = s.size, color = s.color }
    end
    local s = theme.skins[props.skin or "title"]
    return {
        _type = "text",
        text  = props.text or props[1] or "",
        size  = props.size  or s.size,
        color = props.color or s.color,
    }
end

-- ─── Interactive / container primitives ───────────────────────────────────────

-- Styled button with hover skin, adaptive touch target. Uses a nine-patch /
-- texture skin when one is bound for its skin name, otherwise the color skin.
function M.Button(props)
    local hov    = gramarye.ui.hovered(props.id)
    local s_name = hov and (props.hover_skin or props.skin and (props.skin.."_hover") or "button_hover")
                       or (props.skin or "button")
    local s      = theme.skins[s_name] or theme.skins.button
    local h      = math.max(props.h or 40, M.MIN_TAP_H)
    local nine, tex = image_for(s_name)
    return {
        _type    = (nine or tex) and "image" or "frame",
        id       = props.id,
        bg       = (not (nine or tex)) and (props.bg or s.bg) or nil,
        nine     = nine,
        tex      = tex,
        radius   = props.radius or s.radius,
        layout   = {
            w     = props.w or { fit = true },
            h     = h,
            align = "center",
            pad   = props.pad or s.pad,
        },
        on_click = props.on_click,
        on_hover = props.on_hover,
        { _type = "text",
          text  = props.label or props.text or "",
          size  = props.font_size or s.font_size,
          color = props.text_color or s.text,
          font  = props.font or 0 },
    }
end

-- Styled container with column layout by default. Image-skinnable.
function M.Panel(props)
    local s_name    = props.skin or "panel"
    local s         = theme.skins[s_name]
    local nine, tex = image_for(s_name)
    local node = {
        _type  = (nine or tex) and "image" or "frame",
        id     = props.id,
        bg     = (not (nine or tex)) and (props.bg or s.bg) or nil,
        nine   = nine,
        tex    = tex,
        radius = props.radius or s.radius,
        layout = props.layout or {
            dir = "column",
            w   = props.w,
            h   = props.h,
            pad = props.pad or 16,
            gap = props.gap or 12,
        },
        on_click = props.on_click,
    }
    for i, v in ipairs(props) do node[i] = v end
    return node
end

-- Full-screen overlay (modal backdrop), with content centred.
function M.Overlay(props)
    local node = {
        _type  = "frame",
        id     = props.id or "overlay",
        bg     = props.bg or {0, 0, 0, 180},
        layout = { w = { grow = true }, h = { grow = true }, align = "center" },
    }
    for i, v in ipairs(props) do node[i] = v end
    return node
end

-- ─── Tooltip ──────────────────────────────────────────────────────────────────
-- A floating bubble attached to a target element, shown only while it's hovered.
--   ui.Tooltip { to_id = "save_btn", text = "Save the game" }
-- Place it anywhere the target id is also declared this frame (e.g. at the end of
-- the root). Always returns a (possibly empty) floating node — never nil — so it
-- is safe to drop into a children array without truncating siblings.
function M.Tooltip(props)
    local target = props.to_id or props.target
    local node = {
        _type = "frame",
        floating = {
            to_id       = target,
            attach      = props.attach or "below",
            x           = props.x or 0,
            y           = props.y or 6,
            z           = props.z or 1000,
            passthrough = true,   -- never steal hover from the target
        },
    }
    if target and gramarye.ui.hovered(target) then
        local s = theme.skins.tooltip
        node.bg     = props.bg or s.bg
        node.radius = props.radius or s.radius
        node.layout = { pad = props.pad or s.pad or { h = 8, v = 6 } }
        if props.text then
            node[1] = { _type = "text", text = props.text,
                        size = props.font_size or s.font_size, color = props.text_color or s.text }
        else
            for i, v in ipairs(props) do node[i] = v end
        end
    end
    return node
end

-- ─── Virtualized, pull-model List & Grid ──────────────────────────────────────
-- Data stays in the game's C/ECS code: pass a row `count` and a `cell(i)` callback
-- that reads row i. Only the rows visible in the scroll viewport are laid out, so
-- a 100k-row list costs ~viewport-many cell() calls per frame. Indices are 1-based.

local function visible_window(scroll_id, total, row_h, fallback_vp)
    local off, vp = gramarye.ui.scroll_info(scroll_id)
    if not vp or vp <= 0 then vp = fallback_vp end
    local first = math.max(1, math.floor(off / row_h))            -- 1 row of overscan
    local last  = math.min(total, first + math.ceil(vp / row_h) + 1)
    return first, last
end

-- List: vertical, one cell per row, with hover + selection.
--   ui.List { id="units", count=C.count(), row_h=28, h=240, selected=sel,
--             on_select=function(i) ... end,
--             cell=function(i, st) return ui.Label(C.name(i)) end }
function M.List(props)
    local s       = theme.skins.list_row
    local count   = props.count or 0
    local row_h   = props.row_h or 32
    local base_id = props.id or "list"
    local scroll  = props.h ~= nil

    local first, last = 1, count
    if scroll and count > 0 then first, last = visible_window(base_id, count, row_h, props.h) end

    local node = {
        _type  = scroll and "scroll" or "frame",
        id     = base_id,
        bg     = props.bg,
        v_scroll = true,
        layout = { dir = "column", w = props.w or { grow = true }, h = props.h },
    }
    local n = 0
    if first > 1 then n = n + 1; node[n] = { _type = "frame", layout = { h = (first - 1) * row_h } } end
    for i = first, last do
        local row_id = base_id .. "_row_" .. i
        local hov = gramarye.ui.hovered(row_id)
        local sel = props.selected == i
        local row = {
            _type  = "frame",
            id     = row_id,
            bg     = sel and s.selected or hov and s.hover or s.bg,
            radius = s.radius,
            layout = { w = { grow = true }, h = row_h,
                       pad = s.pad or { h = 10, v = 0 }, align = { y = "center" } },
            on_click = props.on_select and function() props.on_select(i) end or nil,
        }
        if props.cell then
            local c = props.cell(i, { hovered = hov, selected = sel })
            if c then row[1] = c end
        end
        n = n + 1; node[n] = row
    end
    if last < count then n = n + 1; node[n] = { _type = "frame", layout = { h = (count - last) * row_h } } end
    return node
end

-- Grid: `cols` cells per row. Small grids (no `h`) render every row; give an `h`
-- to make it a virtualized scroll container.
--   ui.Grid { id="inv", count=C.count(), cols=6, cell_h=56, gap=8,
--             cell=function(i) return SlotFor(i) end }
function M.Grid(props)
    local count   = props.count or 0
    local cols    = props.cols or 1
    local cell_h  = props.cell_h or 56
    local gap     = props.gap or 8
    local rows_n  = math.ceil(count / cols)
    local row_h   = cell_h + gap
    local base_id = props.id or "grid"
    local scroll  = props.h ~= nil

    local first, last = 1, rows_n
    if scroll and rows_n > 0 then first, last = visible_window(base_id, rows_n, row_h, props.h) end

    local node = {
        _type  = scroll and "scroll" or "frame",
        id     = base_id,
        v_scroll = true,
        layout = { dir = "column", w = props.w or { grow = true }, h = props.h, gap = gap },
    }
    local n = 0
    if first > 1 then n = n + 1; node[n] = { _type = "frame", layout = { h = (first - 1) * row_h } } end
    for r = first, last do
        local row = { _type = "frame", layout = { gap = gap, h = cell_h } }
        local m = 0
        for c = 1, cols do
            local i = (r - 1) * cols + c
            if i <= count and props.cell then
                local cell = props.cell(i)
                if cell then m = m + 1; row[m] = cell end
            end
        end
        n = n + 1; node[n] = row
    end
    if last < rows_n then n = n + 1; node[n] = { _type = "frame", layout = { h = (rows_n - last) * row_h } } end
    return node
end

-- ─── TextBox (single-line, controlled) ────────────────────────────────────────
-- Value lives with the caller (controlled input): pass `value`, update it in
-- `on_change`. Focus/cursor state is module-local. UTF-8 aware via Lua's utf8 lib.
--   ui.TextBox { id="name", value=state.name, placeholder="Name",
--                on_change=function(s) state.name = s end }

local tb_active = nil   -- id of the focused TextBox, or nil
local tb_cursor = {}    -- id -> cursor position (codepoints before the caret)

local function u_len(s) return utf8.len(s) or #s end
local function u_sub(s, i, j)               -- 1-based codepoint range, inclusive
    if i < 1 then i = 1 end
    local n = u_len(s)
    if j == nil or j > n then j = n end
    if i > j then return "" end
    local a = utf8.offset(s, i)
    local b = utf8.offset(s, j + 1)
    return s:sub(a, (b and b - 1) or #s)
end

function M.TextBox(props)
    local id  = props.id
    local s   = theme.skins.textbox
    local sf  = theme.skins.textbox_focus or s
    local fs  = props.font_size or s.font_size or 16
    local font = props.font or 0
    local focused = (tb_active == id)
    local value = props.value or ""
    local clen  = u_len(value)
    local cur   = tb_cursor[id]
    if cur == nil or cur > clen then cur = clen end

    if focused then
        local changed = false
        local typed = gramarye.ui.text_input()
        if typed ~= "" then
            value = u_sub(value, 1, cur) .. typed .. u_sub(value, cur + 1, clen)
            cur, clen, changed = cur + u_len(typed), u_len(value), true
        end
        if gramarye.ui.edit_key("backspace") and cur > 0 then
            value = u_sub(value, 1, cur - 1) .. u_sub(value, cur + 1, clen)
            cur, clen, changed = cur - 1, u_len(value), true
        end
        if gramarye.ui.edit_key("delete") and cur < clen then
            value = u_sub(value, 1, cur) .. u_sub(value, cur + 2, clen)
            clen, changed = u_len(value), true
        end
        if gramarye.ui.edit_key("left")  and cur > 0    then cur = cur - 1 end
        if gramarye.ui.edit_key("right") and cur < clen then cur = cur + 1 end
        if gramarye.ui.edit_key("home")  then cur = 0 end
        if gramarye.ui.edit_key("end")   then cur = clen end
        if gramarye.ui.edit_key("enter") then
            if props.on_submit then props.on_submit(value) end
            tb_active = nil
        elseif gramarye.ui.edit_key("escape") then
            tb_active = nil
        end
        tb_cursor[id] = cur
        if changed and props.on_change then props.on_change(value) end
    end

    local is_empty = (value == "")
    local shown = props.password and string.rep("*", clen) or (is_empty and (props.placeholder or "") or value)
    local children = {
        { _type = "text", text = shown, size = fs, font = font,
          color = is_empty and (s.placeholder or {120,120,140,255}) or (props.text_color or s.text) },
    }
    -- Blinking caret at the cursor position (out of flow; doesn't shift the text).
    if focused and (gramarye.ui.time() % 1.0) < 0.5 then
        local before = props.password and string.rep("*", cur) or u_sub(value, 1, cur)
        local cx = select(1, gramarye.ui.measure_text(before, fs, font))
        local padh = (s.pad and s.pad.h) or 8
        children[#children + 1] = {
            _type = "frame",
            bg    = props.text_color or s.text,
            floating = { to = "parent", attach = "left", x = padh + cx, y = 0, z = 1 },
            layout = { w = 2, h = fs },
        }
    end

    return {
        _type  = "frame",
        id     = id,
        bg     = (focused and sf.bg) or s.bg,
        radius = props.radius or s.radius,
        border = { color = focused and (sf.border_focus or sf.border) or s.border,
                   width = focused and 2 or 1 },
        layout = { w = props.w or 200, h = math.max(props.h or 0, M.MIN_TAP_H),
                   pad = s.pad or { h = 10, v = 6 }, align = { y = "center" } },
        on_click = function()
            tb_active = id
            tb_cursor[id] = u_len(props.value or "")
        end,
        table.unpack(children),
    }
end

-- ─── Sizing helpers ───────────────────────────────────────────────────────────

function M.grow()        return { grow = true }  end
function M.pct_w(p)      return { pct = p }       end
function M.pct_h(p)      return { pct = p }       end

-- ─── Component constructor ────────────────────────────────────────────────────
-- Wrap a render function so games can build their own composite widgets on top
-- of these primitives:
--   local Slot = ui.component(function(props) return ui.Frame { ... } end)
--   Slot { id = "s1", item = ... }

function M.component(render_fn)
    return function(props) return render_fn(props) end
end

return M
