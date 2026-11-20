/*
 * gramarye-ui core: owns Clay's memory, the texture/nine-patch registry, and
 * the per-frame begin/end. Drawing lives in gramarye_renderer_raylib.c; the
 * Lua bridge lives in gramarye_ui_lua_impl.c. Both are #included below so
 * they can see this file's static state (single translation unit).
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include "gramarye_ui/ui.h"

/* ========================================================================
 * State
 * ===================================================================== */

#define GRAMARYE_UI_MAX_TEXTURES  64
#define GRAMARYE_UI_MAX_NINEPATCH 64

/* First field of a nine-patch descriptor. Clay image nodes carry a bare
 * `void *`; the renderer reads the first 4 bytes to tell a nine-patch apart
 * from a plain Texture2D (whose first field is a small GL id). */
#define GRAMARYE_NINEPATCH_MAGIC 0x47394E50u

typedef struct {
    uint32_t  magic;
    Texture2D tex;
    Rectangle source;
    int32_t   left, top, right, bottom;
} GramaryeNinePatch;

typedef struct {
    Texture2D tex;
    bool      owned;      /* we loaded it, so we unload it */
    bool      used;
    char      path[512];  /* for de-duplicating load_texture(); fits absolute mount paths */
} GramaryeTextureSlot;

static void   *g_clay_memory = NULL;
static Font   *g_fonts       = NULL;
static int32_t g_font_count  = 0;

static GramaryeTextureSlot g_textures[GRAMARYE_UI_MAX_TEXTURES];
static int32_t             g_texture_count = 0;
static GramaryeNinePatch   g_ninepatches[GRAMARYE_UI_MAX_NINEPATCH];
static int32_t             g_ninepatch_count = 0;

static GramaryeUI_CustomDrawFn g_custom_draw      = NULL;
static void                   *g_custom_draw_user = NULL;

/* Needs the state above. */
#include "gramarye_renderer_raylib.c"

/* ========================================================================
 * Lifecycle
 * ===================================================================== */

static void on_clay_error(Clay_ErrorData error) {
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
                    (Clay_ErrorHandler){ on_clay_error, NULL });
    return true;
}

void GramaryeUI_set_fonts(Font *fonts, int count) {
    g_fonts      = fonts;
    g_font_count = count;
    Clay_SetMeasureTextFunction(Raylib_MeasureText, (void *)g_fonts);
}

void GramaryeUI_shutdown(void) {
    GramaryeUI_clear_textures();
    free(g_clay_memory);
    g_clay_memory = NULL;
    g_fonts       = NULL;
    g_font_count  = 0;
}

/* ========================================================================
 * Per frame
 * ===================================================================== */

/* One pointer for both platforms: first touch on Android, mouse elsewhere.
 * Off-screen (-1,-1) when nothing is touching. */
static Vector2 pointer_position(void) {
#if defined(__ANDROID__)
    return GetTouchPointCount() > 0 ? GetTouchPosition(0) : (Vector2){ -1.0f, -1.0f };
#else
    return GetMousePosition();
#endif
}

static bool pointer_down(void) {
#if defined(__ANDROID__)
    return GetTouchPointCount() > 0;
#else
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT);
#endif
}

void GramaryeUI_begin(float dt) {
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(),
                                                (float)GetScreenHeight() });
    Vector2 ptr = pointer_position();
    Clay_SetPointerState((Clay_Vector2){ ptr.x, ptr.y }, pointer_down());

    Vector2 wheel = GetMouseWheelMoveV();
    Clay_UpdateScrollContainers(true, (Clay_Vector2){ wheel.x, wheel.y }, dt);
    Clay_BeginLayout();
}

void GramaryeUI_end_and_render(void) {
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, g_fonts);
}

/* ========================================================================
 * Textures
 * ===================================================================== */

static int store_texture(Texture2D tex, bool owned, const char *path) {
    if (g_texture_count >= GRAMARYE_UI_MAX_TEXTURES) {
        TraceLog(LOG_WARNING, "gramarye-ui: texture table full (%d)", GRAMARYE_UI_MAX_TEXTURES);
        return 0;
    }
    GramaryeTextureSlot *slot = &g_textures[g_texture_count++];
    slot->tex     = tex;
    slot->owned   = owned;
    slot->used    = true;
    slot->path[0] = '\0';
    if (path) snprintf(slot->path, sizeof(slot->path), "%s", path);
    return g_texture_count; /* 1-based id */
}

/* Path-keyed: loading the same path twice returns the same id, so scene
 * re-entry / hot reload doesn't fill the table. */
int GramaryeUI_load_texture(const char *path) {
    if (path) {
        for (int32_t i = 0; i < g_texture_count; i++) {
            GramaryeTextureSlot *slot = &g_textures[i];
            if (slot->used && slot->owned && strcmp(slot->path, path) == 0) return i + 1;
        }
    }
    Texture2D tex = LoadTexture(path);
    if (tex.id == 0) {
        TraceLog(LOG_WARNING, "gramarye-ui: failed to load texture '%s'", path ? path : "(null)");
        return 0;
    }
    int id = store_texture(tex, true, path);
    if (id == 0) UnloadTexture(tex);
    return id;
}

int GramaryeUI_register_texture(Texture2D texture) {
    return store_texture(texture, false, NULL);
}

const Texture2D *GramaryeUI_texture(int id) {
    if (id < 1 || id > g_texture_count || !g_textures[id - 1].used) return NULL;
    return &g_textures[id - 1].tex;
}

void GramaryeUI_clear_textures(void) {
    for (int32_t i = 0; i < g_texture_count; i++) {
        if (g_textures[i].used && g_textures[i].owned) UnloadTexture(g_textures[i].tex);
    }
    memset(g_textures, 0, sizeof(g_textures));
    g_texture_count = 0;
    memset(g_ninepatches, 0, sizeof(g_ninepatches));
    g_ninepatch_count = 0;
}

/* ========================================================================
 * Nine-patches
 * ===================================================================== */

int GramaryeUI_register_ninepatch(int texture_id, Rectangle src,
                                  int left, int top, int right, int bottom) {
    const Texture2D *tex = GramaryeUI_texture(texture_id);
    if (!tex) return 0;
    if (g_ninepatch_count >= GRAMARYE_UI_MAX_NINEPATCH) {
        TraceLog(LOG_WARNING, "gramarye-ui: nine-patch table full (%d)", GRAMARYE_UI_MAX_NINEPATCH);
        return 0;
    }
    g_ninepatches[g_ninepatch_count++] = (GramaryeNinePatch){
        .magic = GRAMARYE_NINEPATCH_MAGIC, .tex = *tex, .source = src,
        .left = left, .top = top, .right = right, .bottom = bottom,
    };
    return g_ninepatch_count; /* 1-based id */
}

const void *GramaryeUI_ninepatch(int id) {
    if (id < 1 || id > g_ninepatch_count) return NULL;
    return &g_ninepatches[id - 1];
}

/* ========================================================================
 * Custom draw
 * ===================================================================== */

void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user) {
    g_custom_draw      = fn;
    g_custom_draw_user = user;
}

#ifdef GRAMARYE_UI_LUA
#include "gramarye_ui_lua_impl.c"
#endif
