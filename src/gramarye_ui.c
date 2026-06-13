// Single implementation translation unit for gramarye-ui.
//
// This is the one place CLAY_IMPLEMENTATION is defined. The official raylib
// renderer (clay_renderer_raylib.c) does not #include clay.h itself and its
// measure-text callback is static, so we pull both into this TU after clay.h.
// That keeps Clay's implementation, the renderer, and our facade in a single
// unit with no cross-TU visibility tricks.

#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"

// Brings in Clay_Raylib_Render / Clay_Raylib_Close and the static
// Raylib_MeasureText callback. Requires clay.h + raylib.h to be included first
// (raylib.h is pulled in by the renderer).
#include "clay_renderer_raylib.c"

#include "gramarye_ui/ui.h"

static void *g_clay_memory = NULL;
static Font *g_fonts = NULL;
static int g_font_count = 0;

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

void GramaryeUI_shutdown(void) {
    free(g_clay_memory);
    g_clay_memory = NULL;
    g_fonts = NULL;
    g_font_count = 0;
}

void GramaryeUI_begin(float dt) {
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(),
                                                (float)GetScreenHeight() });
    Vector2 mouse = GetMousePosition();
    Clay_SetPointerState((Clay_Vector2){ mouse.x, mouse.y },
                         IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    Vector2 wheel = GetMouseWheelMoveV();
    Clay_UpdateScrollContainers(true, (Clay_Vector2){ wheel.x, wheel.y }, dt);
    Clay_BeginLayout();
}

void GramaryeUI_end_and_render(void) {
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, g_fonts);
}
