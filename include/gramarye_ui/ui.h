#ifndef GRAMARYE_UI_H
#define GRAMARYE_UI_H

#include <stdbool.h>
#include "raylib.h"

bool GramaryeUI_init(int width, int height);

void GramaryeUI_set_fonts(Font *fonts, int count);

void GramaryeUI_shutdown(void);

int  GramaryeUI_load_texture(const char *path);

int  GramaryeUI_register_texture(Texture2D texture);

const Texture2D *GramaryeUI_texture(int id);

void GramaryeUI_clear_textures(void);

int GramaryeUI_register_ninepatch(int texture_id, Rectangle src,
                                  int left, int top, int right, int bottom);

const void *GramaryeUI_ninepatch(int id);

#define GRAMARYE_UI_MAX_BORDER_BEAMS 16
#define GRAMARYE_UI_BEAM_KIND_BASE 1000000

void GramaryeUI_set_border_beam(int slot, Color beam_color, Color base_color,
                                float radius, float border_w,
                                float speed, float glow_radius);

typedef void (*GramaryeUI_CustomDrawFn)(int kind, float x, float y,
                                        float w, float h, void *user);
void GramaryeUI_set_custom_draw(GramaryeUI_CustomDrawFn fn, void *user);

void GramaryeUI_begin(float dt);
void GramaryeUI_end_and_render(void);

#endif // GRAMARYE_UI_H
