local theme = require("gramarye.theme")
local anim  = require("gramarye.anim")

local M = {}

local is_mobile = gramarye.platform == "android"

M.skins     = theme.skins
M.MIN_TAP_H = theme.MIN_TAP_H

local function image_for(skin_name)
    local img = theme.images[skin_name]
    if not img then return nil end
    return img.nine, img.tex
end

function M.Frame(props)
    local node = { _type = "frame" }
    for k, v in pairs(props) do
        node[k] = v
    end
    return node
end

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

function M.Image(props)
    local node = { _type = "image" }
    for k, v in pairs(props) do node[k] = v end
    return node
end

function M.ScrollFrame(props)
    local node = { _type = "scroll" }
    for k, v in pairs(props) do node[k] = v end
    return node
end

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

function M.Spacer(size)
    if size and size > 0 then
        return { _type = "frame", layout = { w = size, h = size } }
    end
    return { _type = "frame", layout = { w = { grow = true }, h = { grow = true } } }
end

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

function M.Button(props)
    local hov       = gramarye.ui.hovered(props.id)
    local off_name  = props.skin or "button"
    local hov_name  = props.hover_skin or (props.skin and (props.skin.."_hover")) or "button_hover"
    local s_off     = theme.skins[off_name] or theme.skins.button
    local s_hov     = theme.skins[hov_name] or s_off
    local h         = math.max(props.h or 40, M.MIN_TAP_H)
    local nine, tex = image_for(hov and hov_name or off_name)

    local bg, text_color
    if nine or tex then
        local s = hov and s_hov or s_off
        bg, text_color = nil, props.text_color or s.text
    else
        local t = anim.towards(props.id, hov and 1 or 0, 14)
        bg         = anim.lerp_color(s_off.bg or {0,0,0,0}, s_hov.bg or s_off.bg or {0,0,0,0}, t)
        text_color = props.text_color or anim.lerp_color(
            s_off.text or {255,255,255,255}, s_hov.text or s_off.text or {255,255,255,255}, t)
    end

    return {
        _type    = (nine or tex) and "image" or "frame",
        id       = props.id,
        bg       = (not (nine or tex)) and (props.bg or bg) or nil,
        nine     = nine,
        tex      = tex,
        radius   = props.radius or s_off.radius,
        layout   = {
            w     = props.w or { fit = true },
            h     = h,
            align = "center",
            pad   = props.pad or s_off.pad,
        },
        on_click = props.on_click,
        on_hover = props.on_hover,
        { _type = "text",
          text  = props.label or props.text or "",
          size  = props.font_size or s_off.font_size,
          color = text_color,
          font  = props.font or 0 },
    }
end

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
            passthrough = true,
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

local function point_on_rect(x, y, w, h, d)
    local perim = 2 * (w + h)
    if perim <= 0 then return x, y end
    d = d % perim
    if d < w then                     return x + d,               y
    elseif d < w + h then             return x + w,                y + (d - w)
    elseif d < w + h + w then         return x + w - (d - w - h),  y + h
    else                              return x,                    y + h - (d - w - h - w)
    end
end

local function glow_falloff(d, center, perim, band_px)
    local delta = (d - center + perim / 2) % perim - perim / 2
    if delta < -band_px or delta > band_px then return 0 end
    return 0.5 * (1 + math.cos(math.pi * delta / band_px))
end

function M.BorderPulse(props)
    local x, y, w, h = props.x, props.y, props.w, props.h
    if not (x and y and w and h) or w <= 0 or h <= 0 then
        return { _type = "frame", layout = { w = 0, h = 0 } }
    end
    local base  = props.base  or { 60, 60, 90, 255 }
    local glow  = props.glow  or { 255, 255, 255, 255 }
    local speed = props.speed or 60
    local band  = props.band  or 0.14
    local step  = props.step  or 5
    local width = props.width or 3
    local z     = props.z or 850

    local perim   = 2 * (w + h)
    local band_px = math.max(band * perim, 1)
    local head1   = (gramarye.ui.time() * speed) % perim
    local head2   = (head1 + perim / 2) % perim
    local seg_sz  = math.max(width, step * 1.4)

    local node = { _type = "frame", layout = { w = 0, h = 0 } }
    local n = 0
    local count = math.min(math.floor(perim / step), 400)
    for i = 0, count - 1 do
        local d = i * step
        local t = math.max(glow_falloff(d, head1, perim, band_px),
                            glow_falloff(d, head2, perim, band_px))
        if t > 0.02 then
            local px, py = point_on_rect(x, y, w, h, d)
            n = n + 1
            node[n] = {
                _type    = "frame",
                bg       = { base[1] + (glow[1]-base[1])*t,
                             base[2] + (glow[2]-base[2])*t,
                             base[3] + (glow[3]-base[3])*t,
                             base[4] + (glow[4]-base[4])*t },
                radius   = seg_sz / 2,
                floating = { to = "root", x = px - seg_sz / 2, y = py - seg_sz / 2,
                             z = z, passthrough = true },
                layout   = { w = seg_sz, h = seg_sz },
            }
        end
    end
    return node
end

local beam_slots     = {}
local beam_next_slot = 0

local function beam_slot_for(id)
    local slot = beam_slots[id]
    if not slot then
        slot = beam_next_slot
        beam_next_slot = beam_next_slot + 1
        beam_slots[id] = slot
    end
    return slot
end

function M.BorderBeam(props)
    local x, y, w, h = props.x, props.y, props.w, props.h
    if not (x and y and w and h) or w <= 0 or h <= 0 then
        return { _type = "frame", layout = { w = 0, h = 0 } }
    end
    local slot = beam_slot_for(props.id or "borderbeam")

    gramarye.ui.set_border_beam(slot, {
        beam         = props.beam or { 255, 255, 255, 255 },
        base         = props.base or { 60, 60, 90, 255 },
        radius       = props.radius or 8,
        border_width = props.border_width or 1,
        speed        = props.speed or 60,
        glow_radius  = props.glow_radius or 40,
    })

    return {
        _type    = "custom",
        kind     = gramarye.ui.border_beam_kind(slot),
        floating = { to = "root", x = x, y = y, z = props.z or 850, passthrough = true },
        layout   = { w = w, h = h },
    }
end

local popup_state = {}

function M.Popup(props)
    local id = props.id
    local st = popup_state[id]
    if not st then
        st = { x = props.x or 40, y = props.y or 40, dragging = false,
               grab_dx = 0, grab_dy = 0, was_down = false }
        popup_state[id] = st
    end

    local bar_id      = id .. "_titlebar"
    local mx, my      = gramarye.ui.mouse_pos()
    local down        = gramarye.ui.mouse_down()

    if not down then
        st.dragging = false
    elseif st.dragging then
        st.x = mx - st.grab_dx
        st.y = my - st.grab_dy
    elseif not st.was_down and gramarye.ui.hovered(bar_id) then
        st.dragging = true
        st.grab_dx  = mx - st.x
        st.grab_dy  = my - st.y
    end
    st.was_down = down

    local w  = props.w or 320
    local sw, sh = gramarye.ui.screen_w(), gramarye.ui.screen_h()
    st.x = math.max(-(w - 40), math.min(st.x, sw - 40))
    st.y = math.max(0, math.min(st.y, sh - 24))

    local s = theme.skins.popup
    local r = props.radius or s.radius

    local bar = {
        _type  = "frame",
        id     = bar_id,
        bg     = s.bar_bg,
        radius = { tl = r, tr = r, bl = 0, br = 0 },
        layout = { w = { grow = true }, h = 32, pad = { h = 10, v = 0 },
                   gap = 8, align = { y = "center" } },
        { _type = "text", text = props.title or "", size = s.title_size,
          color = s.title_color },
        M.Spacer(),
    }
    if props.on_close then
        bar[#bar + 1] = {
            _type    = "frame",
            id       = id .. "_close",
            layout   = { w = 20, h = 20, align = "center" },
            on_click = props.on_close,
            { _type = "text", text = "x", size = 15, color = s.title_color },
        }
    end

    local body = {
        _type  = "frame",
        layout = { dir = "column", w = { grow = true }, h = props.body_h,
                   pad = props.pad or 12, gap = props.gap or 8 },
    }
    for i, v in ipairs(props) do body[i] = v end

    local win = {
        _type    = "frame",
        id       = id,
        bg       = props.bg or s.bg,
        radius   = props.radius or s.radius,
        border   = { color = s.border, width = 1 },
        floating = { to = "root", x = st.x, y = st.y, z = props.z or 400 },
        layout   = { dir = "column", w = w, h = props.h },
        bar, body,
    }
    if not props.pulse then return win end

    local pp = type(props.pulse) == "table" and props.pulse or {}
    local rx, ry, rw, rh = gramarye.ui.element_rect(id)
    return {
        _type = "frame", layout = { w = 0, h = 0 },
        win,
        M.BorderBeam {
            id = id, x = rx, y = ry, w = rw, h = rh, radius = r,
            base = pp.base or s.border, beam = pp.beam,
            speed = pp.speed, glow_radius = pp.glow_radius,
            border_width = pp.border_width, z = pp.z,
        },
    }
end

local dropdown_open = {}

function M.Dropdown(props)
    local id    = props.id
    local items = props.items or {}
    local open  = dropdown_open[id] or false
    local hov   = gramarye.ui.hovered(id)
    local s     = theme.skins[hov and "dropdown_hover" or "dropdown"]
    local h     = math.max(props.h or 36, M.MIN_TAP_H)
    local label = items[props.selected] or props.placeholder or ""

    local trigger = {
        _type  = "frame",
        id     = id,
        bg     = s.bg,
        radius = s.radius,
        border = { color = s.border, width = 1 },
        layout = { w = props.w or 200, h = h, pad = s.pad,
                   align = { y = "center" }, gap = 8 },
        on_click = function() dropdown_open[id] = not open end,
        { _type = "text", text = label, size = props.font_size or s.font_size,
          color = s.text },
        M.Spacer(),
        { _type = "text", text = open and "^" or "v",
          size = props.font_size or s.font_size, color = s.text },
    }
    if not open then return trigger end

    local ls = theme.skins.dropdown_list
    local rows = { _type = "frame", layout = { dir = "column", w = { grow = true } } }
    for i, item in ipairs(items) do
        local row_id  = id .. "_opt_" .. i
        local row_hov = gramarye.ui.hovered(row_id)
        local sel     = props.selected == i
        rows[i] = {
            _type  = "frame",
            id     = row_id,
            bg     = sel and theme.skins.dropdown_row_selected.bg
                   or row_hov and theme.skins.dropdown_row_hover.bg
                   or nil,
            layout = { w = { grow = true }, h = h, pad = s.pad, align = { y = "center" } },
            on_click = function()
                dropdown_open[id] = false
                if props.on_select then props.on_select(i, item) end
            end,
            { _type = "text", text = item, size = props.font_size or s.font_size,
              color = s.text },
        }
    end
    local list = {
        _type    = "frame",
        bg       = ls.bg,
        radius   = ls.radius,
        border   = { color = ls.border, width = 1 },
        floating = { to_id = id, attach = { element = "left_top", parent = "left_bottom" }, z = 900 },
        layout   = { dir = "column", w = props.w or 200 },
        rows,
    }
    local backdrop = {
        _type    = "frame",
        floating = { to = "root", x = 0, y = 0, z = 800 },
        layout   = { w = { grow = true }, h = { grow = true } },
        on_click = function() dropdown_open[id] = false end,
    }
    return { _type = "frame", trigger, backdrop, list }
end

local function visible_window(scroll_id, total, row_h, fallback_vp)
    local off, vp = gramarye.ui.scroll_info(scroll_id)
    if not vp or vp <= 0 then vp = fallback_vp end
    local first = math.max(1, math.floor(off / row_h))
    local last  = math.min(total, first + math.ceil(vp / row_h) + 1)
    return first, last
end

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

local tb_active = nil
local tb_cursor = {}

local function u_len(s) return utf8.len(s) or #s end
local function u_sub(s, i, j)
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

function M.grow()        return { grow = true }  end
function M.pct_w(p)      return { pct = p }       end
function M.pct_h(p)      return { pct = p }       end

function M.component(render_fn)
    return function(props) return render_fn(props) end
end

return M
