# gramarye-ui

UI layout for Gramarye games and the launcher: a thin C facade over
[Clay](https://github.com/nicbarker/clay) with Clay's official raylib renderer.
Consumed via CMake FetchContent, exactly like `gramarye-ecs` / `gramarye-libcore`.

Clay is pinned (currently **v0.14**) and fetched internally; consumers never see
Clay's build. The library also exposes `clay.h` on its public include path so you
can write `CLAY({...})` / `CLAY_TEXT(...)` layout directly.

## Consuming

```cmake
FetchContent_Declare(gramarye_ui
    GIT_REPOSITORY "https://github.com/noahspoling/gramarye-ui.git"
    GIT_TAG "main")
FetchContent_MakeAvailable(gramarye_ui)
target_link_libraries(<your_target> PRIVATE gramarye-ui)
```

`raylib` is reused from the consumer if the target already exists; otherwise it
is fetched standalone.

## Usage

```c
#include "gramarye_ui/ui.h"
#include "clay.h"

GramaryeUI_init(width, height);
Font fonts[1] = { GetFontDefault() };
GramaryeUI_set_fonts(fonts, 1);

// per frame, between BeginDrawing()/EndDrawing():
GramaryeUI_begin(dt);
CLAY({ .id = CLAY_ID("Root"),
       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
    CLAY_TEXT(CLAY_STRING("hello"),
              CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = { 255,255,255,255 } }));
}
GramaryeUI_end_and_render();

GramaryeUI_shutdown();
```

## API

| Function | Purpose |
|---|---|
| `bool GramaryeUI_init(int w, int h)` | allocate Clay arena, initialize Clay |
| `void GramaryeUI_set_fonts(Font *fonts, int count)` | font table for measure + render |
| `void GramaryeUI_begin(float dt)` | push dims/pointer/scroll, open layout |
| `void GramaryeUI_end_and_render(void)` | close layout, issue raylib draws |
| `void GramaryeUI_shutdown(void)` | free Clay arena |

## Roadmap

- Lua bindings (`gramarye.ui.*`) so script-driven scenes declare UI; gated behind
  an option so the launcher can consume the C API without Lua.
- Font loading helpers (TTF table) beyond the raylib default font.
