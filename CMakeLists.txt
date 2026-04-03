cmake_minimum_required(VERSION 3.5)
project(sylar)

include(cmake/utils.cmake)  # 引入强制定义宏，修改显示文件为相对路径


set(CMAKE_VERBOSE_MAKEFILE ON) # 打印详细编译信息
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined")


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
    sylar/thread.cc
)


# 编译库


add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar) #__FILE__


# 测试程序
# add_executable(test tests/test.cc) 
# add_executable(test_config tests/test_config.cc)
add_executable(test_thread tests/test_thread.cc)

# 链接库
# force_redefine_file_macro_for_sources(test) #__FILE__
# target_link_libraries(test sylar -lyaml-cpp)

# force_redefine_file_macro_for_sources(test_config) #__FILE__
# target_link_libraries(test_config sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_thread) #__FILE__
target_link_libraries(test_thread sylar -lyaml-cpp)

# 输出路径
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)