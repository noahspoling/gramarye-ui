local M = {}

local is_mobile = gramarye.platform == "android"

M.MIN_TAP_H = is_mobile and 56 or 36

M.skins = {
    button        = { bg={40,40,64,255},   text={220,220,220,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_hover  = { bg={64,64,110,255},  text={255,255,255,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_danger = { bg={90,30,30,255},   text={255,200,200,255}, radius=6,  font_size=18, pad={h=20,v=10} },
    button_danger_hover = { bg={140,40,40,255}, text={255,255,255,255}, radius=6, font_size=18, pad={h=20,v=10} },

    panel         = { bg={15,15,28,230},   radius=10 },
    panel_dark    = { bg={10,10,20,245},   radius=10 },

    label         = { color={200,200,200,255}, size=16 },
    label_dim     = { color={130,130,150,255}, size=14 },
    title         = { color={255,255,255,255}, size=28 },
    subtitle      = { color={180,180,200,255}, size=20 },
    hint          = { color={100,160,220,255}, size=14 },

    tooltip       = { bg={10,10,22,240}, text={210,210,220,255}, radius=4, font_size=14, pad={h=8,v=6} },

    textbox       = { bg={20,20,34,255}, border={60,60,85,255}, text={225,225,235,255},
                      placeholder={120,120,140,255}, radius=5, font_size=16, pad={h=10,v=6} },
    textbox_focus = { bg={26,26,44,255}, border={90,140,220,255}, border_focus={90,140,220,255} },

    list_row      = { bg={0,0,0,0}, hover={30,30,50,200}, selected={50,50,90,255},
                      text={210,210,220,255}, radius=4, pad={h=10,v=0} },

    popup         = { bg={22,22,38,250}, border={60,60,90,255},
                      bar_bg={34,34,58,255}, title_color={230,230,235,255},
                      title_size=15, radius=8 },

    dropdown        = { bg={26,26,44,255}, border={60,60,85,255}, text={220,220,230,255},
                        radius=5, font_size=16, pad={h=10,v=6} },
    dropdown_hover  = { bg={34,34,58,255}, border={90,140,220,255}, text={230,230,235,255},
                        radius=5, font_size=16, pad={h=10,v=6} },
    dropdown_list   = { bg={26,26,44,255}, border={60,60,85,255}, radius=5 },
    dropdown_row_hover = { bg={44,44,78,255} },
    dropdown_row_selected = { bg={50,50,90,255} },

    separator     = { color={50,50,72,255} },
}

M.images = {}

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

function M.bind_image(skin_name, binding)
    M.images[skin_name] = binding
end

return M
