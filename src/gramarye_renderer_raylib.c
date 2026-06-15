// gramarye_renderer_raylib.c — vendored raylib renderer for gramarye-ui.
//
// NOT compiled standalone. Included by gramarye_ui.c AFTER clay.h and after the
// nine-patch descriptor / custom-draw globals are defined, so it sees Clay's
// API (CLAY__MIN etc.), GramaryeNinePatch / GRAMARYE_NINEPATCH_MAGIC, and the
// g_custom_draw* hook.
//
// Started as a faithful copy of Clay's official renderer (renderers/raylib/
// clay_renderer_raylib.c, v0.14) and diverged in exactly three places:
//   1. IMAGE  — disambiguates a plain Texture2D* from a GramaryeNinePatch* via a
//               magic sentinel and draws nine-patches with DrawTextureNPatch.
//   2. CUSTOM — dispatches to the registered GramaryeUI_CustomDrawFn instead of
//               the upstream 3D-model demo struct.
//   3. default — TraceLog warns instead of exit(1) on an unknown command.
// Text/rectangle/border/scissor handling is byte-for-byte the upstream behavior.

#include "raylib.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CLAY_RECTANGLE_TO_RAYLIB_RECTANGLE(rectangle) (Rectangle) { .x = rectangle.x, .y = rectangle.y, .width = rectangle.width, .height = rectangle.height }
#define CLAY_COLOR_TO_RAYLIB_COLOR(color) (Color) { .r = (unsigned char)roundf(color.r), .g = (unsigned char)roundf(color.g), .b = (unsigned char)roundf(color.b), .a = (unsigned char)roundf(color.a) }

static inline Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    // Measure string size for Font
    Clay_Dimensions textSize = { 0 };

    float maxTextWidth = 0.0f;
    float lineTextWidth = 0;
    int maxLineCharCount = 0;
    int lineCharCount = 0;

    float textHeight = config->fontSize;
    Font* fonts = (Font*)userData;
    Font fontToUse = fonts[config->fontId];
    // Font failed to load, likely the fonts are in the wrong place relative to the execution dir.
    // RayLib ships with a default font, so we can continue with that built in one.
    if (!fontToUse.glyphs) {
        fontToUse = GetFontDefault();
    }

    float scaleFactor = config->fontSize/(float)fontToUse.baseSize;

    // Iterate UTF-8 codepoints (not raw bytes) so multibyte glyphs (×, →, …) are
    // measured correctly. GetGlyphIndex falls back to '?' for missing glyphs, so
    // measurement matches what DrawTextEx renders. Walking bytes here would index
    // far out of the glyph array for multibyte chars and inflate the width.
    for (int i = 0; i < text.length; lineCharCount++)
    {
        int cpSize = 0;
        int codepoint = GetCodepointNext(&text.chars[i], &cpSize);
        i += (cpSize > 0) ? cpSize : 1;
        if (codepoint == '\n') {
            maxTextWidth = fmax(maxTextWidth, lineTextWidth);
            maxLineCharCount = CLAY__MAX(maxLineCharCount, lineCharCount);
            lineTextWidth = 0;
            lineCharCount = 0;
            continue;
        }
        int index = GetGlyphIndex(fontToUse, codepoint);
        if (fontToUse.glyphs[index].advanceX != 0) lineTextWidth += fontToUse.glyphs[index].advanceX;
        else lineTextWidth += (fontToUse.recs[index].width + fontToUse.glyphs[index].offsetX);
    }

    maxTextWidth = fmax(maxTextWidth, lineTextWidth);
    maxLineCharCount = CLAY__MAX(maxLineCharCount, lineCharCount);

    textSize.width = maxTextWidth * scaleFactor + (lineCharCount * config->letterSpacing);
    textSize.height = textHeight;

    return textSize;
}

void Clay_Raylib_Initialize(int width, int height, const char *title, unsigned int flags) {
    SetConfigFlags(flags);
    InitWindow(width, height, title);
//    EnableEventWaiting();
}

// A MALLOC'd buffer, that we keep modifying inorder to save from so many Malloc and Free Calls.
// Call Clay_Raylib_Close() to free
static char *temp_render_buffer = NULL;
static int temp_render_buffer_len = 0;

// Call after closing the window to clean up the render buffer
void Clay_Raylib_Close()
{
    if(temp_render_buffer) free(temp_render_buffer);
    temp_render_buffer_len = 0;

    CloseWindow();
}


void Clay_Raylib_Render(Clay_RenderCommandArray renderCommands, Font* fonts)
{
    for (int j = 0; j < renderCommands.length; j++)
    {
        Clay_RenderCommand *renderCommand = Clay_RenderCommandArray_Get(&renderCommands, j);
        Clay_BoundingBox boundingBox = {roundf(renderCommand->boundingBox.x), roundf(renderCommand->boundingBox.y), roundf(renderCommand->boundingBox.width), roundf(renderCommand->boundingBox.height)};
        switch (renderCommand->commandType)
        {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData *textData = &renderCommand->renderData.text;
                Font fontToUse = fonts[textData->fontId];

                int strlen = textData->stringContents.length + 1;

                if(strlen > temp_render_buffer_len) {
                    // Grow the temp buffer if we need a larger string
                    if(temp_render_buffer) free(temp_render_buffer);
                    temp_render_buffer = (char *) malloc(strlen);
                    temp_render_buffer_len = strlen;
                }

                // Raylib uses standard C strings so isn't compatible with cheap slices, we need to clone the string to append null terminator
                memcpy(temp_render_buffer, textData->stringContents.chars, textData->stringContents.length);
                temp_render_buffer[textData->stringContents.length] = '\0';
                DrawTextEx(fontToUse, temp_render_buffer, (Vector2){boundingBox.x, boundingBox.y}, (float)textData->fontSize, (float)textData->letterSpacing, CLAY_COLOR_TO_RAYLIB_COLOR(textData->textColor));

                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                // imageData is a pointer into gramarye-ui's static registry: either a
                // bare Texture2D* (from GramaryeUI_texture) or a GramaryeNinePatch*
                // (from GramaryeUI_ninepatch). The magic sentinel disambiguates — a
                // real Texture2D's first field is its small GL id, never the magic.
                void *imageData = renderCommand->renderData.image.imageData;
                Clay_Color tintColor = renderCommand->renderData.image.backgroundColor;
                if (tintColor.r == 0 && tintColor.g == 0 && tintColor.b == 0 && tintColor.a == 0) {
                    tintColor = (Clay_Color) { 255, 255, 255, 255 };
                }
                if (!imageData) break;
                if (*(uint32_t *)imageData == GRAMARYE_NINEPATCH_MAGIC) {
                    GramaryeNinePatch *np = (GramaryeNinePatch *)imageData;
                    NPatchInfo info = {
                        .source = np->source,
                        .left = np->left, .top = np->top, .right = np->right, .bottom = np->bottom,
                        .layout = NPATCH_NINE_PATCH
                    };
                    DrawTextureNPatch(np->tex, info,
                        (Rectangle){boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height},
                        (Vector2){0, 0}, 0, CLAY_COLOR_TO_RAYLIB_COLOR(tintColor));
                } else {
                    Texture2D imageTexture = *(Texture2D *)imageData;
                    DrawTexturePro(
                        imageTexture,
                        (Rectangle) { 0, 0, imageTexture.width, imageTexture.height },
                        (Rectangle){boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height},
                        (Vector2) {},
                        0,
                        CLAY_COLOR_TO_RAYLIB_COLOR(tintColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                BeginScissorMode((int)roundf(boundingBox.x), (int)roundf(boundingBox.y), (int)roundf(boundingBox.width), (int)roundf(boundingBox.height));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                EndScissorMode();
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *config = &renderCommand->renderData.rectangle;
                if (config->cornerRadius.topLeft > 0) {
                    float radius = (config->cornerRadius.topLeft * 2) / (float)((boundingBox.width > boundingBox.height) ? boundingBox.height : boundingBox.width);
                    DrawRectangleRounded((Rectangle) { boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height }, radius, 8, CLAY_COLOR_TO_RAYLIB_COLOR(config->backgroundColor));
                } else {
                    DrawRectangle(boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height, CLAY_COLOR_TO_RAYLIB_COLOR(config->backgroundColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *config = &renderCommand->renderData.border;
                // Left border
                if (config->width.left > 0) {
                    DrawRectangle((int)roundf(boundingBox.x), (int)roundf(boundingBox.y + config->cornerRadius.topLeft), (int)config->width.left, (int)roundf(boundingBox.height - config->cornerRadius.topLeft - config->cornerRadius.bottomLeft), CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                // Right border
                if (config->width.right > 0) {
                    DrawRectangle((int)roundf(boundingBox.x + boundingBox.width - config->width.right), (int)roundf(boundingBox.y + config->cornerRadius.topRight), (int)config->width.right, (int)roundf(boundingBox.height - config->cornerRadius.topRight - config->cornerRadius.bottomRight), CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                // Top border
                if (config->width.top > 0) {
                    DrawRectangle((int)roundf(boundingBox.x + config->cornerRadius.topLeft), (int)roundf(boundingBox.y), (int)roundf(boundingBox.width - config->cornerRadius.topLeft - config->cornerRadius.topRight), (int)config->width.top, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                // Bottom border
                if (config->width.bottom > 0) {
                    DrawRectangle((int)roundf(boundingBox.x + config->cornerRadius.bottomLeft), (int)roundf(boundingBox.y + boundingBox.height - config->width.bottom), (int)roundf(boundingBox.width - config->cornerRadius.bottomLeft - config->cornerRadius.bottomRight), (int)config->width.bottom, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                if (config->cornerRadius.topLeft > 0) {
                    DrawRing((Vector2) { roundf(boundingBox.x + config->cornerRadius.topLeft), roundf(boundingBox.y + config->cornerRadius.topLeft) }, roundf(config->cornerRadius.topLeft - config->width.top), config->cornerRadius.topLeft, 180, 270, 10, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                if (config->cornerRadius.topRight > 0) {
                    DrawRing((Vector2) { roundf(boundingBox.x + boundingBox.width - config->cornerRadius.topRight), roundf(boundingBox.y + config->cornerRadius.topRight) }, roundf(config->cornerRadius.topRight - config->width.top), config->cornerRadius.topRight, 270, 360, 10, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                if (config->cornerRadius.bottomLeft > 0) {
                    DrawRing((Vector2) { roundf(boundingBox.x + config->cornerRadius.bottomLeft), roundf(boundingBox.y + boundingBox.height - config->cornerRadius.bottomLeft) }, roundf(config->cornerRadius.bottomLeft - config->width.bottom), config->cornerRadius.bottomLeft, 90, 180, 10, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                if (config->cornerRadius.bottomRight > 0) {
                    DrawRing((Vector2) { roundf(boundingBox.x + boundingBox.width - config->cornerRadius.bottomRight), roundf(boundingBox.y + boundingBox.height - config->cornerRadius.bottomRight) }, roundf(config->cornerRadius.bottomRight - config->width.bottom), config->cornerRadius.bottomRight, 0.1, 90, 10, CLAY_COLOR_TO_RAYLIB_COLOR(config->color));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                // customData carries a small-int "kind" token set by the `custom`
                // walker node; the game registers a drawer via GramaryeUI_set_custom_draw.
                if (g_custom_draw) {
                    int kind = (int)(intptr_t)renderCommand->renderData.custom.customData;
                    g_custom_draw(kind, boundingBox.x, boundingBox.y, boundingBox.width, boundingBox.height, g_custom_draw_user);
                }
                break;
            }
            default: {
                TraceLog(LOG_WARNING, "gramarye-ui: unhandled render command type %d", (int)renderCommand->commandType);
                break;
            }
        }
    }
}
