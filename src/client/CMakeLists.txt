cmake_minimum_required(VERSION 3.14)
project(ddt_client)

include(FetchContent)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Wno-unused-function -Wno-deprecated-declarations -DIMGUI_IMPL_OPENGL_LOADER_GLAD")

# OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# GLFW: thirdparty > system > FetchContent
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/glfw/src/CMakeLists.txt")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/glfw thirdparty_glfw)
    message(STATUS "GLFW: building from thirdparty/")
else()
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        message(STATUS "GLFW: downloading...")
        FetchContent_Declare(glfw
            URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    endif()
endif()

# Protobuf
find_package(Protobuf REQUIRED)

# FreeType: thirdparty > system > FetchContent
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/freetype/CMakeLists.txt")
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/freetype thirdparty_freetype)
    set(FREETYPE_LIB freetype)
    message(STATUS "FreeType: building from thirdparty/")
else()
    find_package(Freetype QUIET)
    if(NOT Freetype_FOUND)
        message(STATUS "FreeType: downloading...")
        FetchContent_Declare(freetype
            URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
        )
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(freetype)
        set(FREETYPE_LIB freetype)
    else()
        set(FREETYPE_LIB Freetype::Freetype)
    endif()
endif()

# 头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/logic
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/battle
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_SOURCE_DIR}/thirdparty/glad/include
    ${CMAKE_SOURCE_DIR}/thirdparty/stb
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/thirdparty/include
    ${CMAKE_SOURCE_DIR}/thirdparty/include/jsoncpp
    ${CMAKE_BINARY_DIR}/proto
)

# 第三方源文件
set(GLAD_SRC ${CMAKE_SOURCE_DIR}/thirdparty/glad/src/glad.c)

# ImGui
set(IMGUI_SRC
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

# Protobuf 生成目录
set(DDT_PB_DIR ${CMAKE_BINARY_DIR}/proto)

# 客户端源文件
set(CLIENT_SRC
    main.cc
    logic/game.cc
    logic/game_hud.cc
    logic/game_network.cc
    logic/game_renderer.cc
    logic/game_object.cc
    logic/projectile.cc
    network/network_client.cc
    network/ws_client.cc
    battle/particle_system.cc
    render/shader.cc
    render/texture.cc
    render/resource_manager.cc
    render/sprite_renderer.cc
    render/sprite_batch.cc
    render/terrain.cc
    render/text_renderer.cc
    render/camera.cc
    ${DDT_PB_DIR}/ddt.pb.cc
    ${IMGUI_SRC}
    ${GLAD_SRC}
)

add_executable(ddt_client ${CLIENT_SRC})
add_dependencies(ddt_client ddt_pb)
set_target_properties(ddt_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

target_link_libraries(ddt_client
    OpenGL::GL
    glfw
    protobuf
    ${FREETYPE_LIB}
    sylar
    ddt_physics_common
    yaml-cpp
    ssl
    crypto
    jsoncpp
    m
    dl
    pthread
)
