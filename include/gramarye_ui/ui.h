#ifndef GRAMARYE_UI_H
#define GRAMARYE_UI_H

#include <stdbool.h>
#include "raylib.h"

/* ---- Lifecycle --------------------------------------------------------- */

bool GramaryeUI_init(int width, int height);
void GramaryeUI_set_fonts(Font *fonts, int count);
void GramaryeUI_shutdown(void);

/* ---- Per frame: begin -> (build layout) -> end_and_render -------------- */

void GramaryeUI_begin(float dt);
void GramaryeUI_end_and_render(void);

/* ---- Textures & nine-patches (ids are 1-based, 0 = none) --------------- */

int              GramaryeUI_load_texture(const char *path);
int              GramaryeUI_register_texture(Texture2D texture);
const Texture2D *GramaryeUI_texture(int id);
void             GramaryeUI_clear_textures(void);

int         GramaryeUI_register_ninepatch(int texture_id, Rectangle src,
                                          int left, int top, int right, int bottom);
const void *GramaryeUI_ninepatch(int id);

/* ---- Custom draw --------------------------------------------------------
 * A `custom` node reserves a laid-out rect tagged with an int `kind`. At
 * render time the renderer calls this callback with that kind + rect, so the
 * game can draw anything (world view, scope, shader preview) inside the UI. */

typedef void (*GramaryeUI_CustomDrawFn)(int kind, float x, float y,
                                        float w, float h, void *user);
void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user);

#endif // GRAMARYE_UI_H
