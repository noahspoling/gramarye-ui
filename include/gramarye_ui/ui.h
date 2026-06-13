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

// Frees Clay's arena.
void GramaryeUI_shutdown(void);

// Per-frame. begin() pushes current screen dimensions, pointer, and scroll into
// Clay and opens a layout. Declare elements with CLAY(...) after this call.
// end_and_render() closes the layout and issues raylib draw calls, so it must
// run between BeginDrawing()/EndDrawing().
void GramaryeUI_begin(float dt);
void GramaryeUI_end_and_render(void);

#endif // GRAMARYE_UI_H
