#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include "gramarye_ui/ui.h"

#define GRAMARYE_NINEPATCH_MAGIC 0x47394E50u
typedef struct {
    uint32_t  magic;
    Texture2D tex;
    Rectangle source;
    int left, top, right, bottom;
} GramaryeNinePatch;

static GramaryeUI_CustomDrawFn g_custom_draw = NULL;
static void *g_custom_draw_user = NULL;

typedef struct {
    Color beam, base;
    float radius, border_w, speed, glow_radius;
    bool used;
} GramaryeBorderBeam;

static GramaryeBorderBeam g_beams[GRAMARYE_UI_MAX_BORDER_BEAMS];
static void gramarye_ui_draw_border_beam(int slot, Rectangle rect);

#include "gramarye_renderer_raylib.c"

static void *g_clay_memory = NULL;
static Font *g_fonts = NULL;
static int g_font_count = 0;

#define GRAMARYE_UI_MAX_TEXTURES   64
#define GRAMARYE_UI_MAX_NINEPATCH  64

typedef struct { Texture2D tex; bool owned; bool used; char path[128]; } GramaryeUITexSlot;

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
    Clay_SetMeasureTextFunction(Raylib_MeasureText, (void *)g_fonts);
}

static int gramarye_ui_store_texture(Texture2D tex, bool owned, const char *path) {
    if (g_texture_count >= GRAMARYE_UI_MAX_TEXTURES) {
        TraceLog(LOG_WARNING, "gramarye-ui: texture table full (%d)", GRAMARYE_UI_MAX_TEXTURES);
        return 0;
    }
    int idx = g_texture_count++;
    g_textures[idx].tex   = tex;
    g_textures[idx].owned = owned;
    g_textures[idx].used  = true;
    g_textures[idx].path[0] = '\0';
    if (path) snprintf(g_textures[idx].path, sizeof(g_textures[idx].path), "%s", path);
    return idx + 1;
}

int GramaryeUI_load_texture(const char *path) {
    if (path) {
        for (int i = 0; i < g_texture_count; i++) {
            if (g_textures[i].used && g_textures[i].owned && g_textures[i].path[0] &&
                strcmp(g_textures[i].path, path) == 0) {
                return i + 1;
            }
        }
    }
    Texture2D tex = LoadTexture(path);
    if (tex.id == 0) {
        TraceLog(LOG_WARNING, "gramarye-ui: failed to load texture '%s'", path ? path : "(null)");
        return 0;
    }
    int id = gramarye_ui_store_texture(tex, true, path);
    if (id == 0) UnloadTexture(tex);
    return id;
}

int GramaryeUI_register_texture(Texture2D texture) {
    return gramarye_ui_store_texture(texture, false, NULL);
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

void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user) {
    g_custom_draw      = fn;
    g_custom_draw_user = user;
}

static const char *g_beam_fs_330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform vec2 uSize;\n"
    "uniform float uRadius;\n"
    "uniform float uBorderW;\n"
    "uniform vec2 uBeam1;\n"
    "uniform vec2 uBeam2;\n"
    "uniform vec3 uBeamColor;\n"
    "uniform vec3 uBaseColor;\n"
    "uniform float uGlowRadius;\n"
    "float sdRoundBox(vec2 p, vec2 b, float r) {\n"
    "    vec2 q = abs(p) - b + r;\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = fragTexCoord * uSize;\n"
    "    vec2 halfSize = uSize * 0.5;\n"
    "    float d = sdRoundBox(p - halfSize, halfSize, uRadius);\n"
    "    float borderMask = 1.0 - smoothstep(0.0, 1.5, abs(d) - uBorderW * 0.5);\n"
    "    vec3 color = uBaseColor * borderMask;\n"
    "    float alpha = borderMask * 0.55;\n"
    "    float insideMask = 1.0 - smoothstep(-2.0, 2.0, d);\n"
    "    float g1 = exp(-length(p - uBeam1) / uGlowRadius);\n"
    "    float g2 = exp(-length(p - uBeam2) / uGlowRadius);\n"
    "    float glow = clamp(g1 + g2, 0.0, 1.0) * insideMask;\n"
    "    color += uBeamColor * glow;\n"
    "    float core1 = (1.0 - smoothstep(0.0, uBorderW * 1.5, abs(d))) * exp(-length(p - uBeam1) / (uGlowRadius * 0.35));\n"
    "    float core2 = (1.0 - smoothstep(0.0, uBorderW * 1.5, abs(d))) * exp(-length(p - uBeam2) / (uGlowRadius * 0.35));\n"
    "    color += uBeamColor * (core1 + core2) * 1.5;\n"
    "    alpha = max(alpha, max(glow, max(core1, core2)));\n"
    "    finalColor = vec4(color, alpha);\n"
    "}\n";

static Shader g_beam_shader = { 0 };
static bool   g_beam_shader_attempted = false;
static int    g_beam_loc_size, g_beam_loc_radius, g_beam_loc_borderw,
              g_beam_loc_beam1, g_beam_loc_beam2, g_beam_loc_beamcolor,
              g_beam_loc_basecolor, g_beam_loc_glowradius;

static void gramarye_ui_ensure_beam_shader(void) {
    if (g_beam_shader_attempted) return;
    g_beam_shader_attempted = true;
    g_beam_shader = LoadShaderFromMemory(NULL, g_beam_fs_330);
    if (g_beam_shader.id == 0) {
        TraceLog(LOG_WARNING, "gramarye-ui: border-beam shader failed to compile; effect disabled");
        return;
    }
    g_beam_loc_size       = GetShaderLocation(g_beam_shader, "uSize");
    g_beam_loc_radius     = GetShaderLocation(g_beam_shader, "uRadius");
    g_beam_loc_borderw    = GetShaderLocation(g_beam_shader, "uBorderW");
    g_beam_loc_beam1      = GetShaderLocation(g_beam_shader, "uBeam1");
    g_beam_loc_beam2      = GetShaderLocation(g_beam_shader, "uBeam2");
    g_beam_loc_beamcolor  = GetShaderLocation(g_beam_shader, "uBeamColor");
    g_beam_loc_basecolor  = GetShaderLocation(g_beam_shader, "uBaseColor");
    g_beam_loc_glowradius = GetShaderLocation(g_beam_shader, "uGlowRadius");
}

void GramaryeUI_set_border_beam(int slot, Color beam_color, Color base_color,
                                float radius, float border_w,
                                float speed, float glow_radius) {
    if (slot < 0 || slot >= GRAMARYE_UI_MAX_BORDER_BEAMS) return;
    g_beams[slot] = (GramaryeBorderBeam){
        .beam = beam_color, .base = base_color, .radius = radius,
        .border_w = border_w, .speed = speed, .glow_radius = glow_radius,
        .used = true,
    };
}

static Vector2 gramarye_ui_point_on_rect(float w, float h, float d) {
    float perim = 2 * (w + h);
    if (perim <= 0) return (Vector2){ 0, 0 };
    d = fmodf(d, perim);
    if (d < 0) d += perim;
    if (d < w)             return (Vector2){ d, 0 };
    if (d < w + h)         return (Vector2){ w, d - w };
    if (d < w + h + w)     return (Vector2){ w - (d - w - h), h };
    return (Vector2){ 0, h - (d - w - h - w) };
}

static void gramarye_ui_draw_border_beam(int slot, Rectangle rect) {
    if (slot < 0 || slot >= GRAMARYE_UI_MAX_BORDER_BEAMS || !g_beams[slot].used) return;
    gramarye_ui_ensure_beam_shader();
    if (g_beam_shader.id == 0) return;

    GramaryeBorderBeam *cfg = &g_beams[slot];
    float perim = 2 * (rect.width + rect.height);
    float t     = (float)GetTime();
    float d1    = fmodf(t * cfg->speed, perim);
    float d2    = fmodf(d1 + perim * 0.5f, perim);
    Vector2 beam1 = gramarye_ui_point_on_rect(rect.width, rect.height, d1);
    Vector2 beam2 = gramarye_ui_point_on_rect(rect.width, rect.height, d2);

    Vector2 size = { rect.width, rect.height };
    Vector3 beamColor = { cfg->beam.r / 255.0f, cfg->beam.g / 255.0f, cfg->beam.b / 255.0f };
    Vector3 baseColor = { cfg->base.r / 255.0f, cfg->base.g / 255.0f, cfg->base.b / 255.0f };

    SetShaderValue(g_beam_shader, g_beam_loc_size, &size, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_beam_shader, g_beam_loc_radius, &cfg->radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_beam_shader, g_beam_loc_borderw, &cfg->border_w, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_beam_shader, g_beam_loc_beam1, &beam1, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_beam_shader, g_beam_loc_beam2, &beam2, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_beam_shader, g_beam_loc_beamcolor, &beamColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(g_beam_shader, g_beam_loc_basecolor, &baseColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(g_beam_shader, g_beam_loc_glowradius, &cfg->glow_radius, SHADER_UNIFORM_FLOAT);

    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(g_beam_shader);
    DrawRectangleRec(rect, WHITE);
    EndShaderMode();
    EndBlendMode();
}

void GramaryeUI_shutdown(void) {
    GramaryeUI_clear_textures();
    if (g_beam_shader.id != 0) UnloadShader(g_beam_shader);
    g_beam_shader = (Shader){ 0 };
    g_beam_shader_attempted = false;
    memset(g_beams, 0, sizeof(g_beams));
    free(g_clay_memory);
    g_clay_memory = NULL;
    g_fonts = NULL;
    g_font_count = 0;
}

void GramaryeUI_begin(float dt) {
    Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(),
                                                (float)GetScreenHeight() });
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
