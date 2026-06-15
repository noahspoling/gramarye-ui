cmake_minimum_required(VERSION 3.20)
project(gramarye_ui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

# Pinned Clay. SOURCE_SUBDIR points at a directory with no CMakeLists.txt so
# FetchContent_MakeAvailable populates the sources without running Clay's own
# CMake (which would build its examples). We only need clay.h + the renderer.
FetchContent_Declare(
    clay
    GIT_REPOSITORY "https://github.com/nicbarker/clay.git"
    GIT_TAG "v0.14"
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR "_gramarye_no_cmake_"
)
FetchContent_MakeAvailable(clay)

# raylib: reuse the consumer's target if present (the template already fetches
# it); otherwise fetch a default so this library also builds standalone.
if(NOT TARGET raylib)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
        GIT_TAG "master"
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
endif()

option(GRAMARYE_UI_LUA "Compile Lua bindings (gramarye.ui.* API, gramarye.platform)" OFF)

if(GRAMARYE_UI_LUA)
    if(NOT TARGET lua_static)
        message(FATAL_ERROR
            "GRAMARYE_UI_LUA=ON requires the lua_static target. "
            "Add walterschell/Lua via FetchContent before gramarye-ui.")
    endif()
endif()

add_library(gramarye-ui STATIC src/gramarye_ui.c)

target_include_directories(gramarye-ui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${clay_SOURCE_DIR}"                       # clay.h for consumers' CLAY() macros
    PRIVATE
        # src/ holds the vendored renderer + lua impl, both #included into
        # gramarye_ui.c (not compiled as standalone TUs).
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(gramarye-ui PUBLIC raylib)

if(GRAMARYE_UI_LUA)
    target_compile_definitions(gramarye-ui PRIVATE GRAMARYE_UI_LUA=1)
    # PRIVATE: ui_lua.h only forward-declares lua_State; consumers already have lua_static.
    target_link_libraries(gramarye-ui PRIVATE lua_static)
    # Expose Lua headers for the included impl file compiled as part of gramarye_ui.c.
    target_include_directories(gramarye-ui PRIVATE
        $<TARGET_PROPERTY:lua_static,INTERFACE_INCLUDE_DIRECTORIES>)

    # Embed gramarye-ui's Lua std layer as a C header so games get C + Lua in one
    # versioned tag with zero loose files (clean on Android). Regenerated whenever
    # a lua/ source changes (via CMAKE_CONFIGURE_DEPENDS below).
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_lua.cmake")
    set(GRAMARYE_LUA_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(GRAMARYE_LUA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/gramarye/theme.lua"
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/gramarye/ui.lua")
    gramarye_embed_lua("${GRAMARYE_LUA_GEN_DIR}/gramarye_ui_lua_modules.h"
        "gramarye.theme" "${CMAKE_CURRENT_SOURCE_DIR}/lua/gramarye/theme.lua"
        "gramarye.ui"    "${CMAKE_CURRENT_SOURCE_DIR}/lua/gramarye/ui.lua")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GRAMARYE_LUA_SOURCES})
    target_include_directories(gramarye-ui PRIVATE "${GRAMARYE_LUA_GEN_DIR}")
endif()
