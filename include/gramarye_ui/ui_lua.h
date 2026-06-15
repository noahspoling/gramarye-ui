#ifndef GRAMARYE_UI_LUA_H
#define GRAMARYE_UI_LUA_H

// Lua bindings for gramarye-ui. Only available when the library is compiled
// with GRAMARYE_UI_LUA=ON. Consumers also need lua_static on their link line
// (the template CMakeLists.txt handles this automatically).

// Forward-declare lua_State so consumers don't need lua.h just to call these.
struct lua_State;

// Install gramarye.ui.* and gramarye.platform into an existing lua_State.
//
//   gramarye.ui.render(node, ...)  — walk an element tree into Clay this frame.
//                                    Call between GramaryeUI_begin/end_and_render.
//   gramarye.ui.hovered(id) → bool — was element <id> hovered last frame?
//                                    One-frame lag; safe to call during layout.
//   gramarye.ui.screen_w() → int  — actual window width  (not virtual resolution)
//   gramarye.ui.screen_h() → int  — actual window height
//   gramarye.platform              — "desktop" | "android" | "web"
//
// Call once, after ScriptHost_new and before loading the first scene.
void GramaryeUI_register_lua(struct lua_State *L);

// Fire on_click / on_hover Lua callbacks for all elements rendered this frame.
// Updates the hover set used by gramarye.ui.hovered() next frame.
// Call after GramaryeUI_end_and_render(), still inside BeginDrawing/EndDrawing.
void GramaryeUI_dispatch_events(struct lua_State *L);

#endif // GRAMARYE_UI_LUA_H
