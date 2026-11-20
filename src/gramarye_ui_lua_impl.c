/*
 * Lua bridge for gramarye-ui. #included by gramarye_ui.c when GRAMARYE_UI_LUA.
 *
 * How a frame flows:
 *
 *   on_draw (Lua)   builds plain tables:  { _type="frame", id=..., layout={...}, child, child }
 *        |
 *   gramarye.ui.render(tree)  -> render_node() walks the table and declares
 *        |                       each element to Clay. Elements with an id and
 *        |                       on_click/on_hover get an entry in g_events.
 *   GramaryeUI_end_and_render -> Clay computes layout, renderer draws it.
 *        |
 *   GramaryeUI_dispatch_events -> for each g_events entry, ask Clay "is the
 *                                 pointer over this id?" and call the Lua
 *                                 callbacks. Hover set is saved for next
 *                                 frame's gramarye.ui.hovered(id).
 *
 * File layout:
 *   1. Field readers   (get_number, get_color, ... one Lua field -> C value)
 *   2. Element readers (read_layout, read_floating, ... one sub-table -> Clay config)
 *   3. Tree walk       (render_node / render_text / render_element)
 *   4. Events
 *   5. gramarye.ui.* functions
 *   6. Registration
 */
#include "lua.h"
#include "lauxlib.h"
#include "gramarye_ui/ui.h"
#include "gramarye_ui/ui_lua.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "gramarye_ui_lua_modules.h"

#define MAX_UI_EVENTS 256
#define MAX_HOVERED   64
#define MAX_ID_LEN    64

#define COUNT_OF(a) ((int32_t)(sizeof(a) / sizeof((a)[0])))

static inline Clay_String clay_cstr(const char *s) {
    return (Clay_String){ .chars = s, .length = (int32_t)strlen(s) };
}

/* ========================================================================
 * 1. Field readers
 *
 * Each reads t[key] from the table at stack index `t` and leaves the stack
 * as it found it. If the field is missing / wrong type they return false and
 * do NOT touch *out, so the caller sets the default first:
 *
 *     uint16_t gap = 8;  get_u16(L, t, "gap", &gap);
 * ===================================================================== */

static bool get_number(lua_State *L, int t, const char *key, float *out) {
    bool ok = lua_getfield(L, t, key) == LUA_TNUMBER;
    if (ok) *out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

static bool get_u16(lua_State *L, int t, const char *key, uint16_t *out) {
    float v;
    if (!get_number(L, t, key, &v)) return false;
    *out = (uint16_t)v;
    return true;
}

static bool get_bool(lua_State *L, int t, const char *key, bool *out) {
    bool ok = lua_getfield(L, t, key) != LUA_TNIL;
    if (ok) *out = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ok;
}

/* Returns NULL if missing. The pointer stays valid while the table is
 * alive (the table holds a reference to the string). */
static const char *get_string(lua_State *L, int t, const char *key) {
    const char *s = lua_getfield(L, t, key) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    lua_pop(L, 1);
    return s;
}

/* {r, g, b [, a]} at stack index v. */
static Clay_Color read_color(lua_State *L, int v) {
    v = lua_absindex(L, v);
    Clay_Color c = { 0, 0, 0, 255 };
    lua_rawgeti(L, v, 1); c.r = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, v, 2); c.g = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, v, 3); c.b = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    if (lua_rawgeti(L, v, 4) == LUA_TNUMBER) c.a = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return c;
}

static bool get_color(lua_State *L, int t, const char *key, Clay_Color *out) {
    bool ok = lua_getfield(L, t, key) == LUA_TTABLE;
    if (ok) *out = read_color(L, -1);
    lua_pop(L, 1);
    return ok;
}

/* ---- string -> enum lookups -------------------------------------------- */

typedef struct { const char *name; int32_t value; } NameValue;

static bool lookup(const NameValue *table, int32_t n, const char *name, int32_t *out) {
    if (!name) return false;
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(table[i].name, name) == 0) { *out = table[i].value; return true; }
    }
    return false;
}
#define LOOKUP(table, name, out) lookup((table), COUNT_OF(table), (name), (out))

static const NameValue ALIGN_X[] = {
    { "left",   CLAY_ALIGN_X_LEFT   },
    { "center", CLAY_ALIGN_X_CENTER },
    { "right",  CLAY_ALIGN_X_RIGHT  },
};

static const NameValue ALIGN_Y[] = {
    { "top",    CLAY_ALIGN_Y_TOP    },
    { "center", CLAY_ALIGN_Y_CENTER },
    { "bottom", CLAY_ALIGN_Y_BOTTOM },
};

static const NameValue ATTACH_POINTS[] = {
    { "left_top",      CLAY_ATTACH_POINT_LEFT_TOP      },
    { "left_center",   CLAY_ATTACH_POINT_LEFT_CENTER   },
    { "left_bottom",   CLAY_ATTACH_POINT_LEFT_BOTTOM   },
    { "center_top",    CLAY_ATTACH_POINT_CENTER_TOP    },
    { "center_center", CLAY_ATTACH_POINT_CENTER_CENTER },
    { "center_bottom", CLAY_ATTACH_POINT_CENTER_BOTTOM },
    { "right_top",     CLAY_ATTACH_POINT_RIGHT_TOP     },
    { "right_center",  CLAY_ATTACH_POINT_RIGHT_CENTER  },
    { "right_bottom",  CLAY_ATTACH_POINT_RIGHT_BOTTOM  },
};

/* attach = "below" etc: which point of the floating element meets which
 * point of the element it's attached to. */
static const struct {
    const char                  *name;
    Clay_FloatingAttachPointType element, parent;
} ATTACH_PRESETS[] = {
    { "below",  CLAY_ATTACH_POINT_CENTER_TOP,    CLAY_ATTACH_POINT_CENTER_BOTTOM },
    { "above",  CLAY_ATTACH_POINT_CENTER_BOTTOM, CLAY_ATTACH_POINT_CENTER_TOP    },
    { "right",  CLAY_ATTACH_POINT_LEFT_CENTER,   CLAY_ATTACH_POINT_RIGHT_CENTER  },
    { "left",   CLAY_ATTACH_POINT_RIGHT_CENTER,  CLAY_ATTACH_POINT_LEFT_CENTER   },
    { "center", CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER },
};

/* ========================================================================
 * 2. Element readers: one Lua sub-table -> one Clay config struct.
 *    `v` is the stack index of that sub-table (or value).
 * ===================================================================== */

/* 100 -> fixed, {grow=true} -> grow, {pct=0.5} -> percent, else fit. */
static Clay_SizingAxis read_sizing(lua_State *L, int v) {
    v = lua_absindex(L, v);
    if (lua_type(L, v) == LUA_TNUMBER) return CLAY_SIZING_FIXED((float)lua_tonumber(L, v));
    if (lua_type(L, v) == LUA_TTABLE) {
        bool  grow = false;
        float pct;
        if (get_bool(L, v, "grow", &grow) && grow) return CLAY_SIZING_GROW(0);
        if (get_number(L, v, "pct", &pct))         return CLAY_SIZING_PERCENT(pct);
    }
    return CLAY_SIZING_FIT(0, 0);
}

/* 8 -> all sides, or {h=, v=} then {left=, right=, top=, bottom=} overrides. */
static Clay_Padding read_padding(lua_State *L, int v) {
    v = lua_absindex(L, v);
    if (lua_type(L, v) == LUA_TNUMBER) {
        uint16_t p = (uint16_t)lua_tonumber(L, v);
        return (Clay_Padding){ p, p, p, p };
    }
    uint16_t h = 0, vert = 0;
    get_u16(L, v, "h", &h);
    get_u16(L, v, "v", &vert);
    Clay_Padding pad = { .left = h, .right = h, .top = vert, .bottom = vert };
    get_u16(L, v, "left",   &pad.left);
    get_u16(L, v, "right",  &pad.right);
    get_u16(L, v, "top",    &pad.top);
    get_u16(L, v, "bottom", &pad.bottom);
    return pad;
}

/* "center" -> both axes, or {x="left|center|right", y="top|center|bottom"}. */
static Clay_ChildAlignment read_align(lua_State *L, int v) {
    v = lua_absindex(L, v);
    Clay_ChildAlignment a = { 0 };
    if (lua_type(L, v) == LUA_TSTRING) {
        if (strcmp(lua_tostring(L, v), "center") == 0) {
            a.x = CLAY_ALIGN_X_CENTER;
            a.y = CLAY_ALIGN_Y_CENTER;
        }
        return a;
    }
    int32_t x, y;
    if (LOOKUP(ALIGN_X, get_string(L, v, "x"), &x)) a.x = (Clay_LayoutAlignmentX)x;
    if (LOOKUP(ALIGN_Y, get_string(L, v, "y"), &y)) a.y = (Clay_LayoutAlignmentY)y;
    return a;
}

/* layout = { w=, h=, dir="column", gap=, pad=, align= } */
static Clay_LayoutConfig read_layout(lua_State *L, int v) {
    v = lua_absindex(L, v);
    Clay_LayoutConfig layout = { 0 };

    if (lua_getfield(L, v, "w") != LUA_TNIL) layout.sizing.width = read_sizing(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, v, "h") != LUA_TNIL) layout.sizing.height = read_sizing(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, v, "pad") != LUA_TNIL) layout.padding = read_padding(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, v, "align") != LUA_TNIL) layout.childAlignment = read_align(L, -1);
    lua_pop(L, 1);

    const char *dir = get_string(L, v, "dir");
    if (dir && strcmp(dir, "column") == 0) layout.layoutDirection = CLAY_TOP_TO_BOTTOM;

    get_u16(L, v, "gap", &layout.childGap);
    return layout;
}

/* radius = 8, or {tl=, tr=, bl=, br=} (missing corners = 0). */
static Clay_CornerRadius read_radius(lua_State *L, int v) {
    v = lua_absindex(L, v);
    if (lua_type(L, v) == LUA_TNUMBER) {
        float r = (float)lua_tonumber(L, v);
        return (Clay_CornerRadius){ r, r, r, r };
    }
    Clay_CornerRadius r = { 0 };
    if (lua_type(L, v) == LUA_TTABLE) {
        get_number(L, v, "tl", &r.topLeft);
        get_number(L, v, "tr", &r.topRight);
        get_number(L, v, "bl", &r.bottomLeft);
        get_number(L, v, "br", &r.bottomRight);
    }
    return r;
}

/* border = { color={r,g,b,a}, width=1 } */
static Clay_BorderElementConfig read_border(lua_State *L, int v) {
    v = lua_absindex(L, v);
    Clay_Color color = { 80, 80, 100, 255 };
    uint16_t   w     = 1;
    get_color(L, v, "color", &color);
    get_u16(L, v, "width", &w);
    return (Clay_BorderElementConfig){
        .color = color,
        .width = { .left = w, .right = w, .top = w, .bottom = w },
    };
}

/* floating = { to="parent"|"root", to_id="<id>", attach=<preset>|{element=,parent=},
 *              x=, y=, z=, passthrough=bool } */
static Clay_FloatingElementConfig read_floating(lua_State *L, int v) {
    v = lua_absindex(L, v);
    Clay_FloatingElementConfig fc = { 0 };

    /* What it's attached to. to_id wins over to. */
    fc.attachTo = CLAY_ATTACH_TO_PARENT;
    const char *to = get_string(L, v, "to");
    if (to && strcmp(to, "root") == 0) fc.attachTo = CLAY_ATTACH_TO_ROOT;

    const char *to_id = get_string(L, v, "to_id");
    if (to_id && to_id[0]) {
        fc.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
        fc.parentId = Clay_GetElementId(clay_cstr(to_id)).id;
    }

    /* Which points meet. */
    int attach_type = lua_getfield(L, v, "attach");
    if (attach_type == LUA_TSTRING) {
        const char *name = lua_tostring(L, -1);
        for (int32_t i = 0; i < COUNT_OF(ATTACH_PRESETS); i++) {
            if (strcmp(ATTACH_PRESETS[i].name, name) == 0) {
                fc.attachPoints.element = ATTACH_PRESETS[i].element;
                fc.attachPoints.parent  = ATTACH_PRESETS[i].parent;
                break;
            }
        }
    } else if (attach_type == LUA_TTABLE) {
        int32_t el, pa;
        if (LOOKUP(ATTACH_POINTS, get_string(L, -1, "element"), &el))
            fc.attachPoints.element = (Clay_FloatingAttachPointType)el;
        if (LOOKUP(ATTACH_POINTS, get_string(L, -1, "parent"), &pa))
            fc.attachPoints.parent = (Clay_FloatingAttachPointType)pa;
    }
    lua_pop(L, 1);

    /* Offset, stacking order, and whether clicks pass through to what's under it. */
    float z = 0;
    get_number(L, v, "x", &fc.offset.x);
    get_number(L, v, "y", &fc.offset.y);
    if (get_number(L, v, "z", &z)) fc.zIndex = (int16_t)z;

    bool passthrough = false;
    get_bool(L, v, "passthrough", &passthrough);
    fc.pointerCaptureMode = passthrough ? CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH
                                        : CLAY_POINTER_CAPTURE_MODE_CAPTURE;
    return fc;
}

/* scroll node: v_scroll (default true), h_scroll (default false).
 * The child offset comes from last frame's scroll data, because this
 * declaration is built before the element opens. */
static Clay_ClipElementConfig read_scroll(lua_State *L, int t, Clay_ElementId id) {
    bool v_scroll = true, h_scroll = false;
    get_bool(L, t, "v_scroll", &v_scroll);
    get_bool(L, t, "h_scroll", &h_scroll);

    Clay_Vector2 offset = { 0, 0 };
    if (id.id != 0) {
        Clay_ScrollContainerData d = Clay_GetScrollContainerData(id);
        if (d.found && d.scrollPosition) offset = *d.scrollPosition;
    }
    return (Clay_ClipElementConfig){
        .horizontal = h_scroll, .vertical = v_scroll, .childOffset = offset,
    };
}

/* image node: nine=<id> (preferred) or tex=<id>, plus tint= and aspect=. */
static void read_image(lua_State *L, int t, Clay_ElementDeclaration *decl) {
    float       id   = 0;
    const void *data = NULL;
    if (get_number(L, t, "nine", &id)) data = GramaryeUI_ninepatch((int)id);
    if (!data && get_number(L, t, "tex", &id)) data = GramaryeUI_texture((int)id);
    if (!data) return;

    decl->image.imageData = (void *)data;
    get_color(L, t, "tint", &decl->backgroundColor);
    get_number(L, t, "aspect", &decl->aspectRatio.aspectRatio);
}

/* ========================================================================
 * 3. Tree walk
 * ===================================================================== */

typedef enum {
    NODE_UNKNOWN,
    NODE_TEXT,    /* text, size, font, color                                */
    NODE_FRAME,   /* box: id, layout, bg, radius, border, floating, events  */
    NODE_SCROLL,  /* frame + v_scroll / h_scroll                            */
    NODE_IMAGE,   /* frame + nine / tex / tint / aspect                     */
    NODE_CUSTOM,  /* frame + kind -> GramaryeUI_set_custom_draw callback    */
} NodeType;

static const NameValue NODE_TYPES[] = {
    { "text",   NODE_TEXT   },
    { "frame",  NODE_FRAME  },
    { "scroll", NODE_SCROLL },
    { "image",  NODE_IMAGE  },
    { "custom", NODE_CUSTOM },
};

static void register_events(lua_State *L, int t, const char *id);
static void render_node(lua_State *L, int t);

static void render_children(lua_State *L, int t) {
    int32_t n = (int32_t)luaL_len(L, t);
    for (int32_t i = 1; i <= n; i++) {
        if (lua_rawgeti(L, t, i) == LUA_TTABLE) render_node(L, lua_gettop(L));
        lua_pop(L, 1);
    }
}

static void render_text(lua_State *L, int t) {
    const char *text      = get_string(L, t, "text");
    uint16_t    font_size = 18, font_id = 0;
    Clay_Color  color     = { 220, 220, 220, 255 };
    get_u16(L, t, "size", &font_size);
    get_u16(L, t, "font", &font_id);
    get_color(L, t, "color", &color);

    Clay__OpenTextElement(clay_cstr(text ? text : ""),
                          CLAY_TEXT_CONFIG({ .fontSize  = font_size,
                                             .fontId    = font_id,
                                             .textColor = color }));
}

/* Every non-text node is a box; the type only adds one extra config. */
static void render_element(lua_State *L, int t, NodeType type) {
    Clay_ElementDeclaration decl = { 0 };

    const char *id = get_string(L, t, "id");
    bool has_id = id && id[0];
    if (has_id) decl.id = Clay_GetElementId(clay_cstr(id));

    if (lua_getfield(L, t, "layout")   == LUA_TTABLE) decl.layout       = read_layout(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, t, "radius")   != LUA_TNIL)   decl.cornerRadius = read_radius(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, t, "border")   == LUA_TTABLE) decl.border       = read_border(L, -1);
    lua_pop(L, 1);
    if (lua_getfield(L, t, "floating") == LUA_TTABLE) decl.floating     = read_floating(L, -1);
    lua_pop(L, 1);
    get_color(L, t, "bg", &decl.backgroundColor);

    switch (type) {
    case NODE_SCROLL:
        decl.clip = read_scroll(L, t, decl.id);
        break;
    case NODE_IMAGE:
        read_image(L, t, &decl);
        break;
    case NODE_CUSTOM: {
        float kind = 0;
        get_number(L, t, "kind", &kind);
        decl.custom.customData = (void *)(intptr_t)kind;
        break;
    }
    default:
        break;
    }

    if (has_id) register_events(L, t, id);

    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);
    render_children(L, t);
    Clay__CloseElement();
}

static void render_node(lua_State *L, int t) {
    if (lua_type(L, t) != LUA_TTABLE) return;

    int32_t type = NODE_UNKNOWN;
    LOOKUP(NODE_TYPES, get_string(L, t, "_type"), &type);

    if (type == NODE_TEXT)         render_text(L, t);
    else if (type != NODE_UNKNOWN) render_element(L, t, (NodeType)type);
}

/* ========================================================================
 * 4. Events
 *
 * Callbacks are collected while walking the tree (Lua functions -> registry
 * refs) and fired after layout, once Clay knows where everything is. Refs
 * are released every frame; the tree is rebuilt next frame anyway.
 * ===================================================================== */

typedef struct {
    char id[MAX_ID_LEN];  /* copied: the Lua string may be collected before dispatch */
    int  on_click;        /* registry refs, LUA_NOREF if absent */
    int  on_hover;
} UIEventEntry;

static UIEventEntry g_events[MAX_UI_EVENTS];
static int32_t      g_event_count  = 0;
static uint32_t     g_hovered[MAX_HOVERED];  /* ids hovered last frame */
static int32_t      g_hovered_count = 0;
static bool         g_ptr_was_down  = false;

/* Strings inside this frame's trees must outlive gramarye.ui.render(): Clay
 * keeps pointers to text until end_and_render draws it. This registry table
 * holds the trees until dispatch_events. */
static int g_frame_trees = LUA_NOREF;

static int ref_function_field(lua_State *L, int t, const char *key) {
    if (lua_getfield(L, t, key) == LUA_TFUNCTION) return luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    return LUA_NOREF;
}

/* Every element with an id is tracked (even without callbacks) so that
 * gramarye.ui.hovered(id) works for it next frame. */
static void register_events(lua_State *L, int t, const char *id) {
    if (g_event_count >= MAX_UI_EVENTS) return;
    UIEventEntry *e = &g_events[g_event_count++];
    snprintf(e->id, sizeof(e->id), "%s", id);
    e->on_click = ref_function_field(L, t, "on_click");
    e->on_hover = ref_function_field(L, t, "on_hover");
}

/* Error handler: append a stack traceback so the log shows where it failed. */
static int on_callback_error(lua_State *L) {
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

static void call_and_release(lua_State *L, int *ref, bool fire, const char *what, const char *id) {
    if (*ref == LUA_NOREF) return;
    if (fire) {
        lua_pushcfunction(L, on_callback_error);
        lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
        if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
            TraceLog(LOG_ERROR, "gramarye-ui %s [%s]: %s", what, id, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1); /* error handler */
    }
    luaL_unref(L, LUA_REGISTRYINDEX, *ref);
    *ref = LUA_NOREF;
}

void GramaryeUI_dispatch_events(lua_State *L) {
    bool down    = pointer_down();
    bool clicked = g_ptr_was_down && !down;  /* click = release */
    g_ptr_was_down = down;

    g_hovered_count = 0;
    for (int32_t i = 0; i < g_event_count; i++) {
        UIEventEntry *e   = &g_events[i];
        Clay_ElementId id = Clay_GetElementId(clay_cstr(e->id));
        bool hovered      = Clay_PointerOver(id);

        if (hovered && g_hovered_count < MAX_HOVERED) g_hovered[g_hovered_count++] = id.id;
        if (!L) continue;

        call_and_release(L, &e->on_hover, hovered,            "on_hover", e->id);
        call_and_release(L, &e->on_click, hovered && clicked, "on_click", e->id);
    }
    g_event_count = 0;

    if (L && g_frame_trees != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, g_frame_trees);
        g_frame_trees = LUA_NOREF;
    }
}

/* ========================================================================
 * 5. gramarye.ui.* functions
 * ===================================================================== */

/* gramarye.ui.render(tree, ...) */
static int l_ui_render(lua_State *L) {
    int32_t n = lua_gettop(L);

    if (g_frame_trees == LUA_NOREF) {
        lua_newtable(L);
        g_frame_trees = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_frame_trees);
    for (int32_t i = 1; i <= n; i++) {
        lua_pushvalue(L, i);
        lua_rawseti(L, -2, luaL_len(L, -2) + 1);
    }
    lua_pop(L, 1);

    for (int32_t i = 1; i <= n; i++) render_node(L, i);
    return 0;
}

/* True if the element with this id was under the pointer last frame. */
static int l_ui_hovered(lua_State *L) {
    uint32_t id = Clay_GetElementId(clay_cstr(luaL_checkstring(L, 1))).id;
    bool found = false;
    for (int32_t i = 0; i < g_hovered_count && !found; i++) found = g_hovered[i] == id;
    lua_pushboolean(L, found);
    return 1;
}

static int l_ui_screen_w(lua_State *L) { lua_pushinteger(L, GetScreenWidth());  return 1; }
static int l_ui_screen_h(lua_State *L) { lua_pushinteger(L, GetScreenHeight()); return 1; }
static int l_ui_time(lua_State *L)     { lua_pushnumber(L, GetTime());          return 1; }

static int l_ui_mouse_pos(lua_State *L) {
    Vector2 p = pointer_position();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}

static int l_ui_mouse_down(lua_State *L) {
    lua_pushboolean(L, pointer_down());
    return 1;
}

static int push_id_or_nil(lua_State *L, int id) {
    if (id == 0) lua_pushnil(L);
    else         lua_pushinteger(L, id);
    return 1;
}

static int l_ui_load_texture(lua_State *L) {
    return push_id_or_nil(L, GramaryeUI_load_texture(luaL_checkstring(L, 1)));
}

static int l_ui_texture_size(lua_State *L) {
    const Texture2D *t = GramaryeUI_texture((int)luaL_checkinteger(L, 1));
    lua_pushinteger(L, t ? t->width  : 0);
    lua_pushinteger(L, t ? t->height : 0);
    return 2;
}

/* ninepatch(tex, sx, sy, sw, sh, left, top, right, bottom) */
static int l_ui_ninepatch(lua_State *L) {
    Rectangle src = { (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5) };
    int id = GramaryeUI_register_ninepatch((int)luaL_checkinteger(L, 1), src,
                (int)luaL_checkinteger(L, 6), (int)luaL_checkinteger(L, 7),
                (int)luaL_checkinteger(L, 8), (int)luaL_checkinteger(L, 9));
    return push_id_or_nil(L, id);
}

/* Characters typed this frame as UTF-8. Drains raylib's queue: call once. */
static int l_ui_text_input(lua_State *L) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    int cp;
    while ((cp = GetCharPressed()) != 0) {
        int len = 0;
        const char *utf8 = CodepointToUTF8(cp, &len);
        if (utf8 && len > 0) luaL_addlstring(&b, utf8, (size_t)len);
    }
    luaL_pushresult(&b);
    return 1;
}

static const NameValue EDIT_KEYS[] = {
    { "backspace", KEY_BACKSPACE },
    { "delete",    KEY_DELETE    },
    { "left",      KEY_LEFT      },
    { "right",     KEY_RIGHT     },
    { "home",      KEY_HOME      },
    { "end",       KEY_END       },
    { "escape",    KEY_ESCAPE    },
};

/* Pressed or auto-repeating this frame. "enter" also accepts keypad enter. */
static int l_ui_edit_key(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    int32_t key;
    bool pressed = false;
    if (strcmp(name, "enter") == 0) {
        pressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    } else if (LOOKUP(EDIT_KEYS, name, &key)) {
        pressed = IsKeyPressed(key) || IsKeyPressedRepeat(key);
    }
    lua_pushboolean(L, pressed);
    return 1;
}

static int l_ui_measure_text(lua_State *L) {
    const char *text = luaL_optstring(L, 1, "");
    float size       = (float)luaL_checknumber(L, 2);
    int   font       = (int)luaL_optinteger(L, 3, 0);
    bool  valid      = g_fonts && font >= 0 && font < g_font_count && g_fonts[font].glyphs;
    Vector2 m = MeasureTextEx(valid ? g_fonts[font] : GetFontDefault(), text, size, 0.0f);
    lua_pushnumber(L, m.x);
    lua_pushnumber(L, m.y);
    return 2;
}

/* -> offset_y, viewport_h, content_h (all 0 if unknown). One-frame lag. */
static int l_ui_scroll_info(lua_State *L) {
    Clay_ScrollContainerData d =
        Clay_GetScrollContainerData(Clay_GetElementId(clay_cstr(luaL_checkstring(L, 1))));
    bool ok = d.found && d.scrollPosition;
    lua_pushnumber(L, ok ? -d.scrollPosition->y            : 0);
    lua_pushnumber(L, ok ? d.scrollContainerDimensions.height : 0);
    lua_pushnumber(L, ok ? d.contentDimensions.height      : 0);
    return 3;
}

/* -> x, y, w, h of an element as laid out last frame (all 0 if unknown). */
static int l_ui_element_rect(lua_State *L) {
    Clay_ElementData d = Clay_GetElementData(Clay_GetElementId(clay_cstr(luaL_checkstring(L, 1))));
    Clay_BoundingBox b = d.found ? d.boundingBox : (Clay_BoundingBox){ 0 };
    lua_pushnumber(L, b.x);
    lua_pushnumber(L, b.y);
    lua_pushnumber(L, b.width);
    lua_pushnumber(L, b.height);
    return 4;
}

/* ========================================================================
 * 6. Registration
 * ===================================================================== */

/* Make the embedded lua/gramarye/*.lua files require()-able. */
static void preload_embedded_modules(lua_State *L) {
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, "preload");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    for (int32_t i = 0; i < COUNT_OF(g_lua_modules); i++) {
        const GramaryeLuaModule *m = &g_lua_modules[i];
        char chunk[128];
        snprintf(chunk, sizeof(chunk), "@gramarye-ui:%s", m->name);
        if (luaL_loadbuffer(L, m->src, (size_t)m->len, chunk) == LUA_OK) {
            lua_setfield(L, -2, m->name);
        } else {
            TraceLog(LOG_ERROR, "gramarye-ui: embedded module '%s' failed: %s",
                     m->name, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

void GramaryeUI_register_lua(lua_State *L) {
#if defined(__ANDROID__)
    const char *platform = "android";
#elif defined(__EMSCRIPTEN__)
    const char *platform = "web";
#else
    const char *platform = "desktop";
#endif

    lua_getglobal(L, "gramarye");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_pushstring(L, platform);
    lua_setfield(L, -2, "platform");

    static const luaL_Reg ui_fns[] = {
        /* building */
        { "render",       l_ui_render       },
        /* pointer & layout queries (one-frame lag) */
        { "hovered",      l_ui_hovered      },
        { "mouse_pos",    l_ui_mouse_pos    },
        { "mouse_down",   l_ui_mouse_down   },
        { "element_rect", l_ui_element_rect },
        { "scroll_info",  l_ui_scroll_info  },
        { "screen_w",     l_ui_screen_w     },
        { "screen_h",     l_ui_screen_h     },
        /* text editing */
        { "text_input",   l_ui_text_input   },
        { "edit_key",     l_ui_edit_key     },
        { "measure_text", l_ui_measure_text },
        /* assets */
        { "load_texture", l_ui_load_texture },
        { "texture_size", l_ui_texture_size },
        { "ninepatch",    l_ui_ninepatch    },
        /* misc */
        { "time",         l_ui_time         },
        { NULL, NULL }
    };
    lua_newtable(L);
    luaL_setfuncs(L, ui_fns, 0);
    lua_setfield(L, -2, "ui");
    lua_pop(L, 1);

    preload_embedded_modules(L);
}
