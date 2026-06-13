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

add_library(gramarye-ui STATIC src/gramarye_ui.c)

target_include_directories(gramarye-ui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${clay_SOURCE_DIR}"                       # clay.h for consumers' CLAY() macros
    PRIVATE
        "${clay_SOURCE_DIR}/renderers/raylib"      # clay_renderer_raylib.c (impl only)
)

target_link_libraries(gramarye-ui PUBLIC raylib)
