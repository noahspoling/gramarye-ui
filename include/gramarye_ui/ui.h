#ifndef GRAMARYE_UI_H
#define GRAMARYE_UI_H

#include <stdbool.h>
#include "raylib.h"

// gramarye-ui: a thin facade over Clay (https://github.com/nicbarker/clay) with
// Clay's official raylib renderer. Owns Clay's memory arena and the per-frame
// lifecycle so consumers (games, launcher) only deal with: init once, set the
// font table, then begin -> declare layout with CLAY()/CLAY_TEXT() -> render.
//
// To declare layout, a consumer includes "clay.h" (exposed on this target's
// PUBLIC include path) and writes Clay macros between GramaryeUI_begin() and
// GramaryeUI_end_and_render(). The implementation TU is the only place that
// defines CLAY_IMPLEMENTATION.

// Allocates Clay's arena (sized by Clay_MinMemorySize) and initializes Clay at
// the given layout dimensions. Returns false on allocation failure.
bool GramaryeUI_init(int width, int height);

// Registers the font table used for both text measurement and rendering. Index
// matches Clay's text-config fontId. Must be called before the first frame.
// `fonts` must outlive the UI (typically a static array in the caller).
void GramaryeUI_set_fonts(Font *fonts, int count);

// Frees Clay's arena (and unloads any textures gramarye-ui loaded itself).
void GramaryeUI_shutdown(void);

// ── Texture registry ────────────────────────────────────────────────────────
// gramarye-ui owns a small table of textures the UI can reference by id. ids are
// 1-based; 0 always means "none". Must be called after the window exists (these
// touch the GPU), same as set_fonts.

// Loads a texture from `path` and registers it. Returns an id >= 1, or 0 on
// failure. gramarye-ui unloads this texture at shutdown.
int  GramaryeUI_load_texture(const char *path);

// Registers an already-loaded texture the caller owns. Returns an id >= 1, or 0
// if the table is full. gramarye-ui will NOT unload caller-owned textures.
int  GramaryeUI_register_texture(Texture2D texture);

// Returns a stable pointer to the stored Texture2D for `id`, or NULL if invalid.
// The pointer is valid for the texture's lifetime (the backing storage never
// moves), so it is safe to hand to a Clay image config that outlives the frame.
const Texture2D *GramaryeUI_texture(int id);

// Unloads every texture gramarye-ui loaded itself and clears the table. Called
// automatically by GramaryeUI_shutdown.
void GramaryeUI_clear_textures(void);

// ── Nine-patch registry ─────────────────────────────────────────────────────
// A nine-patch describes how to stretch a sub-rect of a registered texture with
// fixed corners / tiled edges. Register one over an existing texture id; `src`
// is the atlas sub-rect (use {0,0,texW,texH} for the whole texture) and the four
// insets are the border widths in pixels. Returns an id >= 1, or 0 on failure.
int GramaryeUI_register_ninepatch(int texture_id, Rectangle src,
                                  int left, int top, int right, int bottom);

// Returns a stable opaque pointer for nine-patch `id` (the descriptor the image
// node hands to Clay), or NULL if invalid.
const void *GramaryeUI_ninepatch(int id);

// ── Custom draw seam ────────────────────────────────────────────────────────
// A `custom` UI node reserves a laid-out rect and, at render time, invokes this
// callback with a game-defined `kind` token and the computed rect. Lets a game
// draw its world (map / minimap / portrait) inside the UI layout. Pass NULL to
// clear.
typedef void (*GramaryeUI_CustomDrawFn)(int kind, float x, float y,
                                        float w, float h, void *user);
void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user);

// Per-frame. begin() pushes current screen dimensions, pointer, and scroll into
// Clay and opens a layout. Declare elements with CLAY(...) after this call.
// end_and_render() closes the layout and issues raylib draw calls, so it must
// run between BeginDrawing()/EndDrawing().
void GramaryeUI_begin(float dt);
void GramaryeUI_end_and_render(void);

#endif // GRAMARYE_UI_H
