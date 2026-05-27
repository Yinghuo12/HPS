# DDT Client - 依赖管理
# 优先级: 系统安装 > 本地 thirdparty > FetchContent 下载

include(FetchContent)

# 辅助: 获取 thirdparty 根目录
function(_ddt_tp_dir RESULT)
    if(DEFINED DDT_TP)
        set(${RESULT} ${DDT_TP} PARENT_SCOPE)
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/thirdparty)
        set(${RESULT} ${CMAKE_SOURCE_DIR}/thirdparty PARENT_SCOPE)
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/ddt/client/thirdparty)
        set(${RESULT} ${CMAKE_SOURCE_DIR}/ddt/client/thirdparty PARENT_SCOPE)
    else()
        set(${RESULT} "" PARENT_SCOPE)
    endif()
endfunction()

# --- GLFW ---
function(fetch_glfw)
    # 1. 系统 find_package
    find_package(glfw3 QUIET)
    if(glfw3_FOUND)
        message(STATUS "GLFW: system installed")
        return()
    endif()
    # 2. macOS Homebrew
    if(APPLE AND EXISTS /opt/homebrew/lib/libglfw.dylib)
        message(STATUS "GLFW: Homebrew")
        add_library(glfw SHARED IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION /opt/homebrew/lib/libglfw.dylib
            INTERFACE_INCLUDE_DIRECTORIES /opt/homebrew/include
        )
        return()
    endif()
    # 3. 本地 thirdparty/glfw 源码
    _ddt_tp_dir(TP_DIR)
    if(TP_DIR AND EXISTS ${TP_DIR}/glfw/CMakeLists.txt)
        message(STATUS "GLFW: local source at ${TP_DIR}/glfw")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
        add_subdirectory(${TP_DIR}/glfw ${CMAKE_BINARY_DIR}/_glfw)
        return()
    endif()
    # 4. FetchContent (需要网络)
    message(STATUS "GLFW: fetching via FetchContent (needs internet)")
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.3.10
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
    FetchContent_MakeAvailable(glfw)
endfunction()

# --- GLM ---
function(fetch_glm)
    # 1. 系统 find_package
    find_package(glm QUIET)
    if(glm_FOUND)
        message(STATUS "GLM: system installed")
        return()
    endif()
    # 2. 本地 thirdparty/glm 头文件
    _ddt_tp_dir(TP_DIR)
    if(TP_DIR AND EXISTS ${TP_DIR}/glm/glm/glm.hpp)
        message(STATUS "GLM: local headers at ${TP_DIR}/glm")
        include_directories(${TP_DIR}/glm)
        return()
    endif()
    # 3. 系统 include 路径
    find_path(GLM_INCLUDE_DIR glm/glm.hpp
        HINTS /usr/local/include /opt/homebrew/include /usr/include)
    if(GLM_INCLUDE_DIR)
        message(STATUS "GLM: found at ${GLM_INCLUDE_DIR}")
        include_directories(${GLM_INCLUDE_DIR})
        return()
    endif()
    # 4. FetchContent
    message(STATUS "GLM: fetching via FetchContent (needs internet)")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)
endfunction()

# --- Protobuf ---
function(fetch_protobuf)
    # 1. 系统 find_package
    find_package(Protobuf QUIET)
    if(Protobuf_FOUND)
        message(STATUS "Protobuf: system installed (${Protobuf_VERSION})")
        return()
    endif()
    # 2. macOS Homebrew
    if(APPLE AND EXISTS /opt/homebrew/lib/libprotobuf.dylib)
        find_library(PROTOBUF_LIB protobuf PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
        find_path(PROTOBUF_INCLUDE google/protobuf/message.h PATHS /opt/homebrew/include NO_DEFAULT_PATH)
        if(PROTOBUF_LIB AND PROTOBUF_INCLUDE)
            message(STATUS "Protobuf: Homebrew")
            add_library(protobuf::libprotobuf SHARED IMPORTED)
            set_target_properties(protobuf::libprotobuf PROPERTIES
                IMPORTED_LOCATION ${PROTOBUF_LIB}
                INTERFACE_INCLUDE_DIRECTORIES ${PROTOBUF_INCLUDE}
            )
            return()
        endif()
    endif()
    # 3. Linux 系统路径
    if(EXISTS /usr/lib/aarch64-linux-gnu/libprotobuf.so OR EXISTS /usr/lib/x86_64-linux-gnu/libprotobuf.so)
        find_library(PROTOBUF_LIB protobuf)
        find_path(PROTOBUF_INCLUDE google/protobuf/message.h)
        if(PROTOBUF_LIB AND PROTOBUF_INCLUDE)
            message(STATUS "Protobuf: system lib at ${PROTOBUF_LIB}")
            add_library(protobuf::libprotobuf SHARED IMPORTED)
            set_target_properties(protobuf::libprotobuf PROPERTIES
                IMPORTED_LOCATION ${PROTOBUF_LIB}
                INTERFACE_INCLUDE_DIRECTORIES ${PROTOBUF_INCLUDE}
            )
            return()
        endif()
    endif()
    # 4. 本地 thirdparty/protobuf_src 源码
    _ddt_tp_dir(TP_DIR)
    if(TP_DIR AND EXISTS ${TP_DIR}/protobuf_src/src/google/protobuf/message.h)
        message(STATUS "Protobuf: local source at ${TP_DIR}/protobuf_src")
        set(protobuf_BUILD_TESTS OFF CACHE BOOL "")
        set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "")
        set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "")
        set(protobuf_WITH_ZLIB OFF CACHE BOOL "")
        set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "")
        add_subdirectory(${TP_DIR}/protobuf_src ${CMAKE_BINARY_DIR}/_protobuf)
        return()
    endif()
    # 5. FetchContent (最后的手段，需要网络)
    message(STATUS "Protobuf: fetching via FetchContent (needs internet, slow)")
    FetchContent_Declare(protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG        v3.21.12
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake
    )
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "")
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "")
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "")
    FetchContent_MakeAvailable(protobuf)
endfunction()

# --- ImGui ---
function(setup_imgui)
    _ddt_tp_dir(TP_DIR)
    if(TP_DIR AND EXISTS ${TP_DIR}/imgui/imgui.h)
        set(IMGUI_DIR ${TP_DIR}/imgui)
    else()
        message(FATAL_ERROR "ImGui not found in thirdparty/")
    endif()
    message(STATUS "ImGui: local at ${IMGUI_DIR}")
endfunction()

# --- FreeType ---
function(fetch_freetype)
    # 1. 系统 find_package
    find_package(Freetype QUIET)
    if(Freetype_FOUND)
        message(STATUS "FreeType: system installed")
        return()
    endif()
    # 2. macOS Homebrew
    if(APPLE AND EXISTS /opt/homebrew/lib/libfreetype.dylib)
        find_library(FREETYPE_LIB freetype2 freetype PATHS /opt/homebrew/lib NO_DEFAULT_PATH)
        find_path(FREETYPE_INCLUDE freetype2/freetype/freetype.h PATHS /opt/homebrew/include NO_DEFAULT_PATH)
        if(FREETYPE_LIB AND FREETYPE_INCLUDE)
            message(STATUS "FreeType: Homebrew at ${FREETYPE_LIB}")
            add_library(Freetype::Freetype SHARED IMPORTED)
            set_target_properties(Freetype::Freetype PROPERTIES
                IMPORTED_LOCATION ${FREETYPE_LIB}
                INTERFACE_INCLUDE_DIRECTORIES ${FREETYPE_INCLUDE}
            )
            return()
        endif()
    endif()
    # 3. 本地 thirdparty/freetype 源码
    _ddt_tp_dir(TP_DIR)
    if(TP_DIR AND EXISTS ${TP_DIR}/freetype/CMakeLists.txt)
        message(STATUS "FreeType: local source at ${TP_DIR}/freetype")
        set(FT_WITH_HARFBUZZ OFF CACHE BOOL "")
        set(FT_WITH_PNG OFF CACHE BOOL "")
        set(FT_WITH_ZLIB OFF CACHE BOOL "")
        set(FT_WITH_BZIP2 OFF CACHE BOOL "")
        set(SKIP_INSTALL_ALL ON CACHE BOOL "")
        add_subdirectory(${TP_DIR}/freetype ${CMAKE_BINARY_DIR}/_freetype)
        if(NOT TARGET Freetype::Freetype AND TARGET freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()
        return()
    endif()
    # 4. FetchContent (静态编译，运行时不需要)
    message(STATUS "FreeType: fetching via FetchContent (static, no runtime dep)")
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE
    )
    set(FT_WITH_HARFBUZZ OFF CACHE BOOL "")
    set(FT_WITH_PNG OFF CACHE BOOL "")
    set(FT_WITH_ZLIB OFF CACHE BOOL "")
    set(FT_WITH_BZIP2 OFF CACHE BOOL "")
    set(SKIP_INSTALL_ALL ON CACHE BOOL "")
    FetchContent_MakeAvailable(freetype)
    if(NOT TARGET Freetype::Freetype AND TARGET freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
endfunction()

# --- 一次性调用所有依赖 ---
function(setup_all_dependencies)
    fetch_glfw()
    fetch_glm()
    if(NOT DDT_PROTOBUF_BUILTIN)
        fetch_protobuf()
    endif()
    fetch_freetype()
    setup_imgui()
endfunction()
