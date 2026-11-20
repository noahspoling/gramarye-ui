/*
 * Clay render commands -> raylib draw calls.
 *
 * Started as Clay's official raylib renderer (v0.14). Differences from
 * upstream: nine-patch support in draw_image, CUSTOM dispatches to
 * GramaryeUI_set_custom_draw, unknown commands warn instead of exit(1), and
 * text measurement walks UTF-8 codepoints.
 *
 * #included by gramarye_ui.c (needs GramaryeNinePatch + g_custom_draw).
 */
#include "raylib.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static inline Color to_raylib_color(Clay_Color c) {
    return (Color){ (unsigned char)roundf(c.r), (unsigned char)roundf(c.g),
                    (unsigned char)roundf(c.b), (unsigned char)roundf(c.a) };
}

static inline Rectangle to_raylib_rect(Clay_BoundingBox b) {
    return (Rectangle){ b.x, b.y, b.width, b.height };
}

/* ========================================================================
 * Text measurement (Clay calls this during layout)
 * ===================================================================== */

static inline Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    Font *fonts = (Font *)userData;
    Font  font  = fonts[config->fontId];
    if (!font.glyphs) font = GetFontDefault();

    float   max_line_w    = 0.0f;
    float   line_w        = 0.0f;
    int32_t line_chars    = 0;

    for (int32_t i = 0; i < text.length; line_chars++) {
        int cp_size   = 0;
        int codepoint = GetCodepointNext(&text.chars[i], &cp_size);
        i += (cp_size > 0) ? cp_size : 1;

        if (codepoint == '\n') {
            max_line_w = fmaxf(max_line_w, line_w);
            line_w     = 0;
            line_chars = 0;
            continue;
        }
        int g = GetGlyphIndex(font, codepoint);
        line_w += (font.glyphs[g].advanceX != 0)
                ? font.glyphs[g].advanceX
                : font.recs[g].width + font.glyphs[g].offsetX;
    }
    max_line_w = fmaxf(max_line_w, line_w);

    float scale = config->fontSize / (float)font.baseSize;
    return (Clay_Dimensions){
        .width  = max_line_w * scale + line_chars * config->letterSpacing,
        .height = config->fontSize,
    };
}

/* ========================================================================
 * One function per render command type
 * ===================================================================== */

static void draw_text(Clay_BoundingBox box, Clay_TextRenderData *t, Font *fonts) {
    /* Clay strings aren't NUL-terminated; raylib wants a C string. */
    static char   *buf     = NULL;
    static int32_t buf_len = 0;

    int32_t needed = t->stringContents.length + 1;
    if (needed > buf_len) {
        free(buf);
        buf     = (char *)malloc(needed);
        buf_len = needed;
    }
    memcpy(buf, t->stringContents.chars, t->stringContents.length);
    buf[t->stringContents.length] = '\0';

    DrawTextEx(fonts[t->fontId], buf, (Vector2){ box.x, box.y },
               (float)t->fontSize, (float)t->letterSpacing, to_raylib_color(t->textColor));
}

static void draw_image(Clay_BoundingBox box, Clay_ImageRenderData *img) {
    if (!img->imageData) return;

    Clay_Color tint = img->backgroundColor;
    if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0) {
        tint = (Clay_Color){ 255, 255, 255, 255 };
    }

    if (*(uint32_t *)img->imageData == GRAMARYE_NINEPATCH_MAGIC) {
        GramaryeNinePatch *np = (GramaryeNinePatch *)img->imageData;
        NPatchInfo info = {
            .source = np->source,
            .left = np->left, .top = np->top, .right = np->right, .bottom = np->bottom,
            .layout = NPATCH_NINE_PATCH,
        };
        DrawTextureNPatch(np->tex, info, to_raylib_rect(box), (Vector2){ 0, 0 }, 0,
                          to_raylib_color(tint));
    } else {
        Texture2D tex = *(Texture2D *)img->imageData;
        DrawTexturePro(tex, (Rectangle){ 0, 0, tex.width, tex.height }, to_raylib_rect(box),
                       (Vector2){ 0, 0 }, 0, to_raylib_color(tint));
    }
}

static void draw_rectangle(Clay_BoundingBox box, Clay_RectangleRenderData *r) {
    Color color = to_raylib_color(r->backgroundColor);
    if (r->cornerRadius.topLeft > 0) {
        float shortest  = box.width > box.height ? box.height : box.width;
        float roundness = (r->cornerRadius.topLeft * 2) / shortest;
        DrawRectangleRounded(to_raylib_rect(box), roundness, 8, color);
    } else {
        DrawRectangle(box.x, box.y, box.width, box.height, color);
    }
}

/* Four straight edges (shortened by the corner radii) + four corner arcs. */
static void draw_border(Clay_BoundingBox box, Clay_BorderRenderData *b) {
    Color             color = to_raylib_color(b->color);
    Clay_CornerRadius cr    = b->cornerRadius;
    Clay_BorderWidth  w     = b->width;
    float x = box.x, y = box.y, bw = box.width, bh = box.height;

    if (w.left > 0)
        DrawRectangle(roundf(x), roundf(y + cr.topLeft),
                      w.left, roundf(bh - cr.topLeft - cr.bottomLeft), color);
    if (w.right > 0)
        DrawRectangle(roundf(x + bw - w.right), roundf(y + cr.topRight),
                      w.right, roundf(bh - cr.topRight - cr.bottomRight), color);
    if (w.top > 0)
        DrawRectangle(roundf(x + cr.topLeft), roundf(y),
                      roundf(bw - cr.topLeft - cr.topRight), w.top, color);
    if (w.bottom > 0)
        DrawRectangle(roundf(x + cr.bottomLeft), roundf(y + bh - w.bottom),
                      roundf(bw - cr.bottomLeft - cr.bottomRight), w.bottom, color);

    if (cr.topLeft > 0)
        DrawRing((Vector2){ roundf(x + cr.topLeft), roundf(y + cr.topLeft) },
                 roundf(cr.topLeft - w.top), cr.topLeft, 180, 270, 10, color);
    if (cr.topRight > 0)
        DrawRing((Vector2){ roundf(x + bw - cr.topRight), roundf(y + cr.topRight) },
                 roundf(cr.topRight - w.top), cr.topRight, 270, 360, 10, color);
    if (cr.bottomLeft > 0)
        DrawRing((Vector2){ roundf(x + cr.bottomLeft), roundf(y + bh - cr.bottomLeft) },
                 roundf(cr.bottomLeft - w.bottom), cr.bottomLeft, 90, 180, 10, color);
    if (cr.bottomRight > 0)
        DrawRing((Vector2){ roundf(x + bw - cr.bottomRight), roundf(y + bh - cr.bottomRight) },
                 roundf(cr.bottomRight - w.bottom), cr.bottomRight, 0.1f, 90, 10, color);
}

static void draw_custom(Clay_BoundingBox box, Clay_CustomRenderData *c) {
    if (!g_custom_draw) return;
    int kind = (int)(intptr_t)c->customData;
    g_custom_draw(kind, box.x, box.y, box.width, box.height, g_custom_draw_user);
}

/* ========================================================================
 * Dispatch
 * ===================================================================== */

static void Clay_Raylib_Render(Clay_RenderCommandArray commands, Font *fonts) {
    for (int32_t i = 0; i < commands.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        Clay_BoundingBox box = {
            roundf(cmd->boundingBox.x),     roundf(cmd->boundingBox.y),
            roundf(cmd->boundingBox.width), roundf(cmd->boundingBox.height),
        };

        switch (cmd->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_TEXT:      draw_text(box, &cmd->renderData.text, fonts);   break;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:     draw_image(box, &cmd->renderData.image);        break;
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: draw_rectangle(box, &cmd->renderData.rectangle); break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:    draw_border(box, &cmd->renderData.border);      break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:    draw_custom(box, &cmd->renderData.custom);      break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            EndScissorMode();
            break;
        default:
            TraceLog(LOG_WARNING, "gramarye-ui: unhandled render command type %d", (int)cmd->commandType);
            break;
        }
    }
}
