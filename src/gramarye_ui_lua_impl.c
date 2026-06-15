// gramarye_ui_lua_impl.c — Lua bindings for gramarye-ui.
//
// NOT compiled as a standalone source file. Included at the end of gramarye_ui.c
// after CLAY_IMPLEMENTATION so Clay's static internal config allocators
// (CLAY_TEXT_CONFIG, CLAY_SCROLL_CONFIG, CLAY_BORDER_CONFIG, etc.) are visible.
//
// Public surface: GramaryeUI_register_lua / GramaryeUI_dispatch_events.

#include "lua.h"
#include "lauxlib.h"
#include "gramarye_ui/ui.h"      // texture / nine-patch registry used by the walker
#include "gramarye_ui/ui_lua.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

// gramarye-ui's Lua std layer, embedded as byte arrays by cmake/embed_lua.cmake.
// Defines GramaryeLuaModule + g_lua_modules[]. Only present when GRAMARYE_UI_LUA.
#include "gramarye_ui_lua_modules.h"

// ---------------------------------------------------------------------------
// Event registry — rebuilt every frame during gramarye.ui.render() calls
// ---------------------------------------------------------------------------

#define MAX_UI_EVENTS 256
#define MAX_HOVERED   64

typedef struct {
    char id_str[64];  // copied from Lua (Lua strings are transient on stack)
    int  on_click;    // Lua registry ref, or LUA_NOREF
    int  on_hover;    // Lua registry ref, or LUA_NOREF
} UIEventEntry;

static UIEventEntry g_events[MAX_UI_EVENTS];
static int          g_event_count   = 0;
static uint32_t     g_hovered_prev[MAX_HOVERED];
static int          g_hovered_count = 0;
static bool         g_ptr_was_down  = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline Clay_String clay_cstr(const char *s) {
    return (Clay_String){ .chars = s, .length = (int32_t)strlen(s) };
}

static Clay_Color color_from_lua(lua_State *L, int idx) {
    Clay_Color c = {0, 0, 0, 255};
    if (lua_type(L, idx) != LUA_TTABLE) return c;
    lua_rawgeti(L, idx, 1); c.r = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, idx, 2); c.g = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, idx, 3); c.b = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, idx, 4);
    if (lua_isnumber(L, -1)) c.a = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return c;
}

// Number → CLAY_SIZING_FIXED, {grow=true} → GROW, {pct=n} → PERCENT, else FIT.
static Clay_SizingAxis sizing_from_lua(lua_State *L, int idx) {
    if (lua_isnumber(L, idx))
        return CLAY_SIZING_FIXED((float)lua_tonumber(L, idx));
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "grow");
        bool grow = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (grow) return CLAY_SIZING_GROW(0);

        lua_getfield(L, idx, "pct");
        bool has_pct = lua_isnumber(L, -1);
        float pct = has_pct ? (float)lua_tonumber(L, -1) : 0.f;
        lua_pop(L, 1);
        if (has_pct) return CLAY_SIZING_PERCENT(pct);
    }
    return CLAY_SIZING_FIT(0, 0);
}

// Attach-point name ("left_top", "center_bottom", …) → Clay enum. Default top-left.
static Clay_FloatingAttachPointType attach_point_from_name(const char *n) {
    if (!n) return CLAY_ATTACH_POINT_LEFT_TOP;
    if (strcmp(n, "left_top")      == 0) return CLAY_ATTACH_POINT_LEFT_TOP;
    if (strcmp(n, "left_center")   == 0) return CLAY_ATTACH_POINT_LEFT_CENTER;
    if (strcmp(n, "left_bottom")   == 0) return CLAY_ATTACH_POINT_LEFT_BOTTOM;
    if (strcmp(n, "center_top")    == 0) return CLAY_ATTACH_POINT_CENTER_TOP;
    if (strcmp(n, "center_center") == 0) return CLAY_ATTACH_POINT_CENTER_CENTER;
    if (strcmp(n, "center_bottom") == 0) return CLAY_ATTACH_POINT_CENTER_BOTTOM;
    if (strcmp(n, "right_top")     == 0) return CLAY_ATTACH_POINT_RIGHT_TOP;
    if (strcmp(n, "right_center")  == 0) return CLAY_ATTACH_POINT_RIGHT_CENTER;
    if (strcmp(n, "right_bottom")  == 0) return CLAY_ATTACH_POINT_RIGHT_BOTTOM;
    return CLAY_ATTACH_POINT_LEFT_TOP;
}

// ---------------------------------------------------------------------------
// Tree walker — visits Lua element tree and calls Clay
// ---------------------------------------------------------------------------

static void render_node(lua_State *L, int idx);

static void render_children(lua_State *L, int parent_idx) {
    int n = (int)luaL_len(L, parent_idx);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, parent_idx, i);
        if (lua_istable(L, -1)) render_node(L, lua_gettop(L));
        lua_pop(L, 1);
    }
}

static void render_node(lua_State *L, int idx) {
    if (lua_type(L, idx) != LUA_TTABLE) return;

    lua_getfield(L, idx, "_type");
    const char *type = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (!type) return;

    // ── TEXT ────────────────────────────────────────────────────────────────
    if (strcmp(type, "text") == 0) {
        lua_getfield(L, idx, "text");
        const char *txt = lua_tostring(L, -1);
        lua_pop(L, 1);

        uint16_t font_size = 18, font_id = 0;
        Clay_Color color = {220, 220, 220, 255};

        lua_getfield(L, idx, "size");
        if (lua_isnumber(L, -1)) font_size = (uint16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, idx, "font");
        if (lua_isnumber(L, -1)) font_id = (uint16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, idx, "color");
        if (!lua_isnil(L, -1)) color = color_from_lua(L, lua_gettop(L));
        lua_pop(L, 1);

        Clay__OpenTextElement(
            clay_cstr(txt ? txt : ""),
            CLAY_TEXT_CONFIG({ .fontSize = font_size,
                               .fontId   = font_id,
                               .textColor = color })
        );
        return;
    }

    // ── FRAME / SCROLL / IMAGE ───────────────────────────────────────────────
    // An `image` is a frame whose background is a texture/nine-patch instead of a
    // solid colour: it shares all id/layout/border/event plumbing, and only adds
    // the image config below. That keeps skinned buttons fully interactive.
    if (strcmp(type, "frame") == 0 || strcmp(type, "scroll") == 0 ||
        strcmp(type, "image") == 0) {
        Clay_ElementDeclaration decl;
        memset(&decl, 0, sizeof(decl));

        // ID
        char id_copy[64] = {0};
        lua_getfield(L, idx, "id");
        const char *id_str = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (id_str && id_str[0]) {
            strncpy(id_copy, id_str, 63);
            decl.id = Clay_GetElementId(clay_cstr(id_copy));
        }

        // Layout
        lua_getfield(L, idx, "layout");
        if (lua_istable(L, -1)) {
            int li = lua_gettop(L);
            Clay_LayoutConfig layout = {0};

            lua_getfield(L, li, "w");
            if (!lua_isnil(L, -1)) layout.sizing.width = sizing_from_lua(L, lua_gettop(L));
            lua_pop(L, 1);

            lua_getfield(L, li, "h");
            if (!lua_isnil(L, -1)) layout.sizing.height = sizing_from_lua(L, lua_gettop(L));
            lua_pop(L, 1);

            lua_getfield(L, li, "dir");
            const char *dir = lua_tostring(L, -1);
            if (dir && strcmp(dir, "column") == 0)
                layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
            lua_pop(L, 1);

            lua_getfield(L, li, "gap");
            if (lua_isnumber(L, -1)) layout.childGap = (uint16_t)lua_tointeger(L, -1);
            lua_pop(L, 1);

            // pad: number (uniform) or {h, v} or {left, right, top, bottom}
            lua_getfield(L, li, "pad");
            if (!lua_isnil(L, -1)) {
                if (lua_isnumber(L, -1)) {
                    uint16_t p = (uint16_t)lua_tointeger(L, -1);
                    layout.padding = (Clay_Padding){ p, p, p, p };
                } else if (lua_istable(L, -1)) {
                    int pi = lua_gettop(L);
                    uint16_t ph = 0, pv = 0;
                    lua_getfield(L, pi, "h"); if (lua_isnumber(L,-1)) ph=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    lua_getfield(L, pi, "v"); if (lua_isnumber(L,-1)) pv=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    uint16_t l=ph, r=ph, t=pv, b=pv;
                    lua_getfield(L, pi, "left");   if(lua_isnumber(L,-1)) l=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    lua_getfield(L, pi, "right");  if(lua_isnumber(L,-1)) r=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    lua_getfield(L, pi, "top");    if(lua_isnumber(L,-1)) t=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    lua_getfield(L, pi, "bottom"); if(lua_isnumber(L,-1)) b=(uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
                    layout.padding = (Clay_Padding){ l, r, t, b };
                }
            }
            lua_pop(L, 1);

            // align: "center" or {x="center", y="center"}
            lua_getfield(L, li, "align");
            if (!lua_isnil(L, -1)) {
                if (lua_isstring(L, -1)) {
                    if (strcmp(lua_tostring(L,-1), "center") == 0)
                        layout.childAlignment = (Clay_ChildAlignment){
                            CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER };
                } else if (lua_istable(L, -1)) {
                    int ai = lua_gettop(L);
                    lua_getfield(L, ai, "x");
                    const char *ax = lua_tostring(L, -1);
                    if (ax) {
                        if (strcmp(ax,"center")==0) layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
                        else if (strcmp(ax,"right")==0) layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
                    }
                    lua_pop(L, 1);
                    lua_getfield(L, ai, "y");
                    const char *ay = lua_tostring(L, -1);
                    if (ay) {
                        if (strcmp(ay,"center")==0) layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
                        else if (strcmp(ay,"bottom")==0) layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            decl.layout = layout;
        }
        lua_pop(L, 1); // pop layout value (table or nil)

        // Background colour
        lua_getfield(L, idx, "bg");
        if (!lua_isnil(L, -1)) decl.backgroundColor = color_from_lua(L, lua_gettop(L));
        lua_pop(L, 1);

        // Uniform corner radius
        lua_getfield(L, idx, "radius");
        if (lua_isnumber(L, -1)) {
            float r = (float)lua_tonumber(L, -1);
            decl.cornerRadius = (Clay_CornerRadius){ r, r, r, r };
        }
        lua_pop(L, 1);

        // Scroll: a scrolling container is a clip element whose child offset
        // tracks Clay's scroll state (v_scroll defaults true, h_scroll false).
        if (strcmp(type, "scroll") == 0) {
            bool vs = true, hs = false;
            lua_getfield(L, idx, "v_scroll"); if (!lua_isnil(L,-1)) vs = lua_toboolean(L,-1); lua_pop(L,1);
            lua_getfield(L, idx, "h_scroll"); if (!lua_isnil(L,-1)) hs = lua_toboolean(L,-1); lua_pop(L,1);
            decl.clip = (Clay_ClipElementConfig){
                .horizontal = hs, .vertical = vs, .childOffset = Clay_GetScrollOffset(),
            };
        }

        // Border: {color={r,g,b,a}, width=n} — v0.14 uses one color + per-side widths.
        lua_getfield(L, idx, "border");
        if (lua_istable(L, -1)) {
            int bi = lua_gettop(L);
            Clay_Color bc = {80, 80, 100, 255};
            uint16_t bw = 1;
            lua_getfield(L, bi, "color"); if (!lua_isnil(L,-1)) bc = color_from_lua(L, lua_gettop(L)); lua_pop(L,1);
            lua_getfield(L, bi, "width"); if (lua_isnumber(L,-1)) bw = (uint16_t)lua_tointeger(L,-1); lua_pop(L,1);
            decl.border = (Clay_BorderElementConfig){
                .color = bc,
                .width = { .left = bw, .right = bw, .top = bw, .bottom = bw },
            };
        }
        lua_pop(L, 1);

        // Image background: resolving `nine` (a nine-patch id) or `tex` (a texture
        // id) turns this element into an IMAGE render command. `nine` wins. An
        // invalid/absent id leaves it a plain frame — graceful opt-in fallback.
        if (strcmp(type, "image") == 0) {
            const void *image_data = NULL;
            lua_getfield(L, idx, "nine");
            if (lua_isnumber(L, -1)) image_data = GramaryeUI_ninepatch((int)lua_tointeger(L, -1));
            lua_pop(L, 1);
            if (!image_data) {
                lua_getfield(L, idx, "tex");
                if (lua_isnumber(L, -1)) image_data = GramaryeUI_texture((int)lua_tointeger(L, -1));
                lua_pop(L, 1);
            }
            if (image_data) {
                decl.image.imageData = (void *)image_data;
                // Clay treats backgroundColor as a tint for image elements;
                // `tint` overrides any `bg` set above.
                lua_getfield(L, idx, "tint");
                if (!lua_isnil(L, -1)) decl.backgroundColor = color_from_lua(L, lua_gettop(L));
                lua_pop(L, 1);
                lua_getfield(L, idx, "aspect");
                if (lua_isnumber(L, -1)) decl.aspectRatio.aspectRatio = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }

        // Floating: lifts the element out of flow and layers it over siblings in z
        // order (tooltips, dropdowns, modals). { to="parent"|"root" | to_id="<id>",
        //   attach=<preset>|{element=,parent=}, x=, y=, z=, passthrough=bool }
        lua_getfield(L, idx, "floating");
        if (lua_istable(L, -1)) {
            int fi = lua_gettop(L);
            Clay_FloatingElementConfig fc; memset(&fc, 0, sizeof(fc));
            fc.attachTo = CLAY_ATTACH_TO_PARENT;   // default if any floating config present

            lua_getfield(L, fi, "to");
            const char *to = lua_tostring(L, -1);
            if (to) {
                if (strcmp(to, "root") == 0)        fc.attachTo = CLAY_ATTACH_TO_ROOT;
                else if (strcmp(to, "parent") == 0) fc.attachTo = CLAY_ATTACH_TO_PARENT;
            }
            lua_pop(L, 1);

            lua_getfield(L, fi, "to_id");
            const char *to_id = lua_tostring(L, -1);
            if (to_id && to_id[0]) {
                fc.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
                fc.parentId = Clay_GetElementId(clay_cstr(to_id)).id;
            }
            lua_pop(L, 1);

            // attach: preset string or explicit { element=, parent= }
            lua_getfield(L, fi, "attach");
            if (lua_isstring(L, -1)) {
                const char *a = lua_tostring(L, -1);
                Clay_FloatingAttachPointType el = CLAY_ATTACH_POINT_LEFT_TOP, pa = CLAY_ATTACH_POINT_LEFT_TOP;
                if      (strcmp(a, "below") == 0) { el = CLAY_ATTACH_POINT_CENTER_TOP;    pa = CLAY_ATTACH_POINT_CENTER_BOTTOM; }
                else if (strcmp(a, "above") == 0) { el = CLAY_ATTACH_POINT_CENTER_BOTTOM; pa = CLAY_ATTACH_POINT_CENTER_TOP; }
                else if (strcmp(a, "right") == 0) { el = CLAY_ATTACH_POINT_LEFT_CENTER;   pa = CLAY_ATTACH_POINT_RIGHT_CENTER; }
                else if (strcmp(a, "left")  == 0) { el = CLAY_ATTACH_POINT_RIGHT_CENTER;  pa = CLAY_ATTACH_POINT_LEFT_CENTER; }
                else if (strcmp(a, "center")== 0) { el = CLAY_ATTACH_POINT_CENTER_CENTER; pa = CLAY_ATTACH_POINT_CENTER_CENTER; }
                fc.attachPoints = (Clay_FloatingAttachPoints){ .element = el, .parent = pa };
            } else if (lua_istable(L, -1)) {
                int ai = lua_gettop(L);
                lua_getfield(L, ai, "element");
                fc.attachPoints.element = attach_point_from_name(lua_tostring(L, -1));
                lua_pop(L, 1);
                lua_getfield(L, ai, "parent");
                fc.attachPoints.parent = attach_point_from_name(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);

            lua_getfield(L, fi, "x"); if (lua_isnumber(L, -1)) fc.offset.x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, fi, "y"); if (lua_isnumber(L, -1)) fc.offset.y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            lua_getfield(L, fi, "z"); if (lua_isnumber(L, -1)) fc.zIndex = (int16_t)lua_tointeger(L, -1); lua_pop(L, 1);

            lua_getfield(L, fi, "passthrough");
            fc.pointerCaptureMode = lua_toboolean(L, -1)
                ? CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH : CLAY_POINTER_CAPTURE_MODE_CAPTURE;
            lua_pop(L, 1);

            decl.floating = fc;
        }
        lua_pop(L, 1);

        // Register Lua event callbacks for this element (by ID)
        if (id_copy[0] && g_event_count < MAX_UI_EVENTS) {
            UIEventEntry *e = &g_events[g_event_count++];
            strncpy(e->id_str, id_copy, 63);
            e->on_click = LUA_NOREF;
            e->on_hover = LUA_NOREF;

            lua_getfield(L, idx, "on_click");
            if (lua_isfunction(L,-1)) { lua_pushvalue(L,-1); e->on_click = luaL_ref(L, LUA_REGISTRYINDEX); }
            lua_pop(L, 1);

            lua_getfield(L, idx, "on_hover");
            if (lua_isfunction(L,-1)) { lua_pushvalue(L,-1); e->on_hover = luaL_ref(L, LUA_REGISTRYINDEX); }
            lua_pop(L, 1);
        }

        Clay__OpenElement();
        Clay__ConfigureOpenElement(decl);
        render_children(L, idx);
        Clay__CloseElement();
        return;
    }

    // ── CUSTOM (seam) ─────────────────────────────────────────────────────────
    // { _type="custom", kind=<int>, layout={ w=, h= } }. Reserves a laid-out rect;
    // at render time the registered GramaryeUI_CustomDrawFn draws into it.
    if (strcmp(type, "custom") == 0) {
        int kind = 0;
        lua_getfield(L, idx, "kind");
        if (lua_isnumber(L, -1)) kind = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        Clay_ElementDeclaration decl;
        memset(&decl, 0, sizeof(decl));
        decl.custom.customData = (void *)(intptr_t)kind;

        lua_getfield(L, idx, "layout");
        if (lua_istable(L, -1)) {
            int li = lua_gettop(L);
            lua_getfield(L, li, "w");
            if (!lua_isnil(L, -1)) decl.layout.sizing.width = sizing_from_lua(L, lua_gettop(L));
            lua_pop(L, 1);
            lua_getfield(L, li, "h");
            if (!lua_isnil(L, -1)) decl.layout.sizing.height = sizing_from_lua(L, lua_gettop(L));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        Clay__OpenElement();
        Clay__ConfigureOpenElement(decl);
        render_children(L, idx);
        Clay__CloseElement();
        return;
    }
}

// ---------------------------------------------------------------------------
// Lua bindings: gramarye.ui.*
// ---------------------------------------------------------------------------

// gramarye.ui.render(node, ...)  — render one or more element trees this frame
static int l_ui_render(lua_State *L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) render_node(L, i);
    return 0;
}

// gramarye.ui.hovered(id) → bool  — one-frame lag; safe to call during layout
static int l_ui_hovered(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    Clay_ElementId eid = Clay_GetElementId(clay_cstr(id));
    for (int i = 0; i < g_hovered_count; i++) {
        if (g_hovered_prev[i] == eid.id) { lua_pushboolean(L, 1); return 1; }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// gramarye.ui.screen_w() / screen_h()  — actual window dimensions (not virtual)
static int l_ui_screen_w(lua_State *L) { lua_pushinteger(L, GetScreenWidth());  return 1; }
static int l_ui_screen_h(lua_State *L) { lua_pushinteger(L, GetScreenHeight()); return 1; }

// gramarye.ui.load_texture(path) → id | nil  — caller prepends any asset prefix.
static int l_ui_load_texture(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int id = GramaryeUI_load_texture(path);
    if (id == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, id);
    return 1;
}

// gramarye.ui.texture_size(id) → w, h  (0, 0 if the id is invalid)
static int l_ui_texture_size(lua_State *L) {
    const Texture2D *t = GramaryeUI_texture((int)luaL_checkinteger(L, 1));
    lua_pushinteger(L, t ? t->width  : 0);
    lua_pushinteger(L, t ? t->height : 0);
    return 2;
}

// gramarye.ui.ninepatch(tex_id, sx,sy,sw,sh, l,t,r,b) → id | nil
static int l_ui_ninepatch(lua_State *L) {
    int tex_id = (int)luaL_checkinteger(L, 1);
    Rectangle src = { (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5) };
    int id = GramaryeUI_register_ninepatch(tex_id, src,
                (int)luaL_checkinteger(L, 6), (int)luaL_checkinteger(L, 7),
                (int)luaL_checkinteger(L, 8), (int)luaL_checkinteger(L, 9));
    if (id == 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, id);
    return 1;
}

// gramarye.ui.time() → seconds since start (raylib GetTime); for caret blink / anim.
static int l_ui_time(lua_State *L) { lua_pushnumber(L, GetTime()); return 1; }

// gramarye.ui.text_input() → UTF-8 string of characters typed this frame ("" if none).
// Drains raylib's char queue, so call it once per frame from the focused widget.
static int l_ui_text_input(lua_State *L) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    int cp;
    while ((cp = GetCharPressed()) != 0) {
        int n = 0;
        const char *u = CodepointToUTF8(cp, &n);
        if (u && n > 0) luaL_addlstring(&b, u, (size_t)n);
    }
    luaL_pushresult(&b);
    return 1;
}

// gramarye.ui.edit_key(name) → bool: pressed or auto-repeating this frame.
// Self-contained editing keys for text widgets (no host input dependency).
static int l_ui_edit_key(lua_State *L) {
    const char *n = luaL_checkstring(L, 1);
    int key = 0;
    if      (strcmp(n, "backspace") == 0) key = KEY_BACKSPACE;
    else if (strcmp(n, "delete")    == 0) key = KEY_DELETE;
    else if (strcmp(n, "left")      == 0) key = KEY_LEFT;
    else if (strcmp(n, "right")     == 0) key = KEY_RIGHT;
    else if (strcmp(n, "home")      == 0) key = KEY_HOME;
    else if (strcmp(n, "end")       == 0) key = KEY_END;
    else if (strcmp(n, "escape")    == 0) key = KEY_ESCAPE;
    else if (strcmp(n, "enter")     == 0) {
        lua_pushboolean(L, IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
        return 1;
    } else { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, IsKeyPressed(key) || IsKeyPressedRepeat(key));
    return 1;
}

// gramarye.ui.measure_text(text, size [, font]) → width, height (registered font).
static int l_ui_measure_text(lua_State *L) {
    const char *txt = luaL_optstring(L, 1, "");
    float size = (float)luaL_checknumber(L, 2);
    int font = (int)luaL_optinteger(L, 3, 0);
    Font f = (g_fonts && font >= 0 && font < g_font_count && g_fonts[font].glyphs)
             ? g_fonts[font] : GetFontDefault();
    Vector2 m = MeasureTextEx(f, txt, size, 0.0f);
    lua_pushnumber(L, m.x);
    lua_pushnumber(L, m.y);
    return 2;
}

// gramarye.ui.scroll_info(id) → offset_y, viewport_h, content_h (0,0,0 if no such
// scroll container yet). offset_y grows as you scroll down. One-frame lag.
static int l_ui_scroll_info(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    Clay_ScrollContainerData d = Clay_GetScrollContainerData(Clay_GetElementId(clay_cstr(id)));
    if (!d.found || !d.scrollPosition) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        return 3;
    }
    lua_pushnumber(L, -d.scrollPosition->y);
    lua_pushnumber(L, d.scrollContainerDimensions.height);
    lua_pushnumber(L, d.contentDimensions.height);
    return 3;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Inject the embedded modules into package.preload so require("gramarye.ui") /
// require("gramarye.theme") resolve straight from the binary — no filesystem.
// Chunks run lazily on first require, so insertion order doesn't matter.
static void gramarye_ui_preload_modules(lua_State *L) {
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, "preload");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    int n = (int)(sizeof(g_lua_modules) / sizeof(g_lua_modules[0]));
    for (int i = 0; i < n; i++) {
        const GramaryeLuaModule *m = &g_lua_modules[i];
        char chunk[128];
        snprintf(chunk, sizeof(chunk), "@gramarye-ui:%s", m->name);
        if (luaL_loadbuffer(L, m->src, (size_t)m->len, chunk) == LUA_OK) {
            lua_setfield(L, -2, m->name);   // package.preload[name] = chunk
        } else {
            TraceLog(LOG_ERROR, "gramarye-ui: embedded module '%s' failed: %s",
                     m->name, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);  // preload, package
}

void GramaryeUI_register_lua(lua_State *L) {
    const char *platform =
#if defined(__ANDROID__)
        "android";
#elif defined(__EMSCRIPTEN__)
        "web";
#else
        "desktop";
#endif

    lua_getglobal(L, "gramarye");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    // gramarye.platform = "desktop" | "android" | "web"
    lua_pushstring(L, platform);
    lua_setfield(L, -2, "platform");

    // gramarye.ui = { render, hovered, screen_w, screen_h }
    static const luaL_Reg ui_fns[] = {
        {"render",       l_ui_render},
        {"hovered",      l_ui_hovered},
        {"screen_w",     l_ui_screen_w},
        {"screen_h",     l_ui_screen_h},
        {"load_texture", l_ui_load_texture},
        {"texture_size", l_ui_texture_size},
        {"ninepatch",    l_ui_ninepatch},
        {"time",         l_ui_time},
        {"text_input",   l_ui_text_input},
        {"edit_key",     l_ui_edit_key},
        {"measure_text", l_ui_measure_text},
        {"scroll_info",  l_ui_scroll_info},
        {NULL, NULL}
    };
    lua_newtable(L);
    luaL_setfuncs(L, ui_fns, 0);
    lua_setfield(L, -2, "ui");

    lua_pop(L, 1); // pop gramarye

    // Make the embedded gramarye.ui / gramarye.theme modules requirable.
    gramarye_ui_preload_modules(L);
}

// Call after GramaryeUI_end_and_render() each frame.
// Fires on_click / on_hover callbacks, updates hover set for next frame,
// and releases Lua refs allocated during render.
void GramaryeUI_dispatch_events(lua_State *L) {
#if defined(__ANDROID__)
    int tc = GetTouchPointCount();
    bool ptr_down = tc > 0;
#else
    bool ptr_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
#endif

    bool clicked    = g_ptr_was_down && !ptr_down;
    g_ptr_was_down  = ptr_down;

    uint32_t new_hovered[MAX_HOVERED];
    int      nhc = 0;

    for (int i = 0; i < g_event_count; i++) {
        UIEventEntry *e = &g_events[i];
        Clay_ElementId eid = Clay_GetElementId(clay_cstr(e->id_str));
        bool hovered = Clay_PointerOver(eid);

        if (hovered && nhc < MAX_HOVERED) new_hovered[nhc++] = eid.id;

        if (L) {
            if (hovered && e->on_hover != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, e->on_hover);
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    TraceLog(LOG_WARNING, "CLAY on_hover [%s]: %s", e->id_str, lua_tostring(L,-1));
                    lua_pop(L, 1);
                }
            }
            if (hovered && clicked && e->on_click != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, e->on_click);
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    TraceLog(LOG_WARNING, "CLAY on_click [%s]: %s", e->id_str, lua_tostring(L,-1));
                    lua_pop(L, 1);
                }
            }
        }

        if (e->on_click != LUA_NOREF) { luaL_unref(L, LUA_REGISTRYINDEX, e->on_click); e->on_click = LUA_NOREF; }
        if (e->on_hover != LUA_NOREF) { luaL_unref(L, LUA_REGISTRYINDEX, e->on_hover); e->on_hover = LUA_NOREF; }
    }

    g_event_count   = 0;
    g_hovered_count = nhc;
    if (nhc > 0) memcpy(g_hovered_prev, new_hovered, nhc * sizeof(uint32_t));
}
