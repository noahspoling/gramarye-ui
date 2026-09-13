#ifndef GRAMARYE_UI_LUA_H
#define GRAMARYE_UI_LUA_H

struct lua_State;

void GramaryeUI_register_lua(struct lua_State *L);

void GramaryeUI_dispatch_events(struct lua_State *L);

#endif // GRAMARYE_UI_LUA_H
