// Single implementation translation unit for gramarye-ui.
//
// This is the one place CLAY_IMPLEMENTATION is defined. The official raylib
// renderer (clay_renderer_raylib.c) does not #include clay.h itself and its
// measure-text callback is static, so we pull both into this TU after clay.h.
// That keeps Clay's implementation, the renderer, and our facade in a single
// unit with no cross-TU visibility tricks.

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"

// ui.h brings in raylib.h (Texture2D/Rectangle) and the GramaryeUI_CustomDrawFn
// typedef, both needed by the vendored renderer below. Included before the
// renderer so those symbols are visible to it.
#include "gramarye_ui/ui.h"

// ── Internal types/state shared with the vendored renderer ──────────────────
// The renderer is textually included just below, so it sees these definitions.

// Tagged descriptor for a nine-patch. The first word is a magic sentinel so the
// renderer's IMAGE path can tell a GramaryeNinePatch* apart from a bare
// Texture2D* (a real texture's first field is its small GL id, never the magic).
#define GRAMARYE_NINEPATCH_MAGIC 0x47394E50u  // 'G9NP'
typedef struct {
    uint32_t  magic;
    Texture2D tex;
    Rectangle source;
    int left, top, right, bottom;
} GramaryeNinePatch;

// Registered by GramaryeUI_set_custom_draw, invoked by the renderer's CUSTOM case.
static GramaryeUI_CustomDrawFn g_custom_draw = NULL;
static void *g_custom_draw_user = NULL;

// Vendored raylib renderer (our copy; adds nine-patch + the custom-draw seam).
// Brings in Clay_Raylib_Render / Clay_Raylib_Close and the static
// Raylib_MeasureText callback. Requires clay.h + the definitions above first.
#include "gramarye_renderer_raylib.c"

static void *g_clay_memory = NULL;
static Font *g_fonts = NULL;
static int g_font_count = 0;

// ── Texture + nine-patch registries ─────────────────────────────────────────
// Fixed static tables: storage never moves, so pointers handed to Clay stay
// valid for the whole frame (and program).
#define GRAMARYE_UI_MAX_TEXTURES   64
#define GRAMARYE_UI_MAX_NINEPATCH  64

typedef struct { Texture2D tex; bool owned; bool used; } GramaryeUITexSlot;

static GramaryeUITexSlot g_textures[GRAMARYE_UI_MAX_TEXTURES];
static int               g_texture_count = 0;
static GramaryeNinePatch g_ninepatches[GRAMARYE_UI_MAX_NINEPATCH];
static int               g_ninepatch_count = 0;

static void gramarye_ui_on_error(Clay_ErrorData error) {
    TraceLog(LOG_ERROR, "CLAY: %.*s", error.errorText.length, error.errorText.chars);
}

bool GramaryeUI_init(int width, int height) {
    uint32_t capacity = Clay_MinMemorySize();
    g_clay_memory = malloc(capacity);
    if (!g_clay_memory) {
        TraceLog(LOG_ERROR, "gramarye-ui: failed to allocate %u bytes for Clay", capacity);
        return false;
    }
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(capacity, g_clay_memory);
    Clay_Initialize(arena,
                    (Clay_Dimensions){ (float)width, (float)height },
                    (Clay_ErrorHandler){ gramarye_ui_on_error, NULL });
    return true;
}

void GramaryeUI_set_fonts(Font *fonts, int count) {
    g_fonts = fonts;
    g_font_count = count;
    // The renderer's measure callback expects the Font* table as userData.
    Clay_SetMeasureTextFunction(Raylib_MeasureText, (void *)g_fonts);
}

// ── Texture registry ────────────────────────────────────────────────────────

static int gramarye_ui_store_texture(Texture2D tex, bool owned) {
    if (g_texture_count >= GRAMARYE_UI_MAX_TEXTURES) {
        TraceLog(LOG_WARNING, "gramarye-ui: texture table full (%d)", GRAMARYE_UI_MAX_TEXTURES);
        return 0;
    }
    int idx = g_texture_count++;
    g_textures[idx].tex   = tex;
    g_textures[idx].owned = owned;
    g_textures[idx].used  = true;
    return idx + 1;  // 1-based; 0 == none
}

int GramaryeUI_load_texture(const char *path) {
    Texture2D tex = LoadTexture(path);
    if (tex.id == 0) {
        TraceLog(LOG_WARNING, "gramarye-ui: failed to load texture '%s'", path ? path : "(null)");
        return 0;
    }
    int id = gramarye_ui_store_texture(tex, true);
    if (id == 0) UnloadTexture(tex);  // table full: don't leak the GPU handle
    return id;
}

int GramaryeUI_register_texture(Texture2D texture) {
    return gramarye_ui_store_texture(texture, false);
}

const Texture2D *GramaryeUI_texture(int id) {
    if (id < 1 || id > g_texture_count || !g_textures[id - 1].used) return NULL;
    return &g_textures[id - 1].tex;
}

void GramaryeUI_clear_textures(void) {
    for (int i = 0; i < g_texture_count; i++) {
        if (g_textures[i].used && g_textures[i].owned) UnloadTexture(g_textures[i].tex);
    }
    memset(g_textures, 0, sizeof(g_textures));
    g_texture_count = 0;
    memset(g_ninepatches, 0, sizeof(g_ninepatches));
    g_ninepatch_count = 0;
}

// ── Nine-patch registry ─────────────────────────────────────────────────────

int GramaryeUI_register_ninepatch(int texture_id, Rectangle src,
                                  int left, int top, int right, int bottom) {
    const Texture2D *tex = GramaryeUI_texture(texture_id);
    if (!tex) return 0;
    if (g_ninepatch_count >= GRAMARYE_UI_MAX_NINEPATCH) {
        TraceLog(LOG_WARNING, "gramarye-ui: nine-patch table full (%d)", GRAMARYE_UI_MAX_NINEPATCH);
        return 0;
    }
    int idx = g_ninepatch_count++;
    g_ninepatches[idx] = (GramaryeNinePatch){
        .magic = GRAMARYE_NINEPATCH_MAGIC, .tex = *tex, .source = src,
        .left = left, .top = top, .right = right, .bottom = bottom,
    };
    return idx + 1;
}

const void *GramaryeUI_ninepatch(int id) {
    if (id < 1 || id > g_ninepatch_count) return NULL;
    return &g_ninepatches[id - 1];
}

// ── Custom draw seam ────────────────────────────────────────────────────────

void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user) {
    g_custom_draw      = fn;
    g_custom_draw_user = user;
}

void GramaryeUI_shutdown(void) {
    GramaryeUI_clear_textures();
    free(g_clay_memory);
    g_clay_memory = NULL;
    g_fonts = NULL;
    g_font_count = 0;
}

void GramaryeUI_begin(float dt) {
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(),
                                                (float)GetScreenHeight() });
    // Unified pointer: touch on Android, mouse elsewhere.
#if defined(__ANDROID__)
    int tc = GetTouchPointCount();
    Clay_Vector2 ptr = tc > 0
        ? (Clay_Vector2){ GetTouchPosition(0).x, GetTouchPosition(0).y }
        : (Clay_Vector2){ -1.0f, -1.0f };
    bool ptr_down = tc > 0;
#else
    Vector2 mouse = GetMousePosition();
    Clay_Vector2 ptr = { mouse.x, mouse.y };
    bool ptr_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
#endif
    Clay_SetPointerState(ptr, ptr_down);
    Vector2 wheel = GetMouseWheelMoveV();
    Clay_UpdateScrollContainers(true, (Clay_Vector2){ wheel.x, wheel.y }, dt);
    Clay_BeginLayout();
}

void GramaryeUI_end_and_render(void) {
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, g_fonts);
}

#ifdef GRAMARYE_UI_LUA
#include "gramarye_ui_lua_impl.c"
#endif
