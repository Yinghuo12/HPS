cmake_minimum_required(VERSION 3.5)
project(sylar)

# set(CMAKE_VERBOSE_MAKEFILE ON)
# set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function")


# 头文件
include_directories(.)
include_directories(./include)
include_directories(./include/boost)

# 库目录
link_directories(./lib)


# 源文件
set(LIB_SRC
    sylar/log.cc
    sylar/util.cc
    sylar/config.cc
)


# 编译库
add_library(sylar SHARED ${LIB_SRC})
add_library(sylar_static STATIC ${LIB_SRC})
SET_TARGET_PROPERTIES(sylar_static PROPERTIES OUTPUT_NAME "sylar")


# 测试程序
# add_executable(test tests/test.cc ) 
add_executable(test_config tests/test_config.cc)

# 链接库
# target_link_libraries(test sylar)
target_link_libraries(test_config sylar -lyaml-cpp)

# 输出路径
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)