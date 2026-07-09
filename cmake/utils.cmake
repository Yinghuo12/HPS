# 该函数用于为目标的源文件重定义 __FILE__ 宏，使其显示为相对路径而非绝对路径
function(force_redefine_file_macro_for_sources targetname)
    # 获取目标的源文件列表
    get_target_property(source_files "${targetname}" SOURCES)
    
    foreach(sourcefile ${source_files})
        # 获取当前源文件的编译定义列表
        get_property(defs SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS)
        
        # 获取源文件的绝对路径
        get_filename_component(filepath "${sourcefile}" ABSOLUTE)
        
        # 计算相对于项目根目录的相对路径
        string(REPLACE ${PROJECT_SOURCE_DIR}/ "" relpath ${filepath})
        
        # 追加 __FILE__ 宏定义，将其值设为相对路径
        list(APPEND defs "__FILE__=\"${relpath}\"")
        
        # 将更新后的编译定义重新设置给源文件
        set_property(
            SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS ${defs}
        )
    endforeach()
endfunction()

# 各子目录通过此函数把源文件累积到全局属性 sylar_sources（可跨 add_subdirectory 边界，
# 克服 function+PARENT_SCOPE 仅回传一层、无法穿透子目录作用域的问题）。
# 根 CMakeLists.txt 在 add_library 前用 get_property 取回。路径为仓库相对路径
# （如 sylar/core/log.cc），与原扁平 LIB_SRC 形态一致，__FILE__ 与 .o 命名行为不变。
function(sylar_add_sources)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_LIST_DIR}")
    foreach(_src ${ARGN})
        if(_rel)
            set(_full "${_rel}/${_src}")
        else()
            set(_full "${_src}")
        endif()
        set_property(GLOBAL APPEND PROPERTY sylar_sources "${_full}")
    endforeach()
endfunction()