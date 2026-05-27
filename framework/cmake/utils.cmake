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