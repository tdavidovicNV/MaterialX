add_executable(CpuHotPaths "${CMAKE_CURRENT_LIST_DIR}/CpuHotPaths.cpp")
target_link_libraries(CpuHotPaths PRIVATE MaterialXGenGlsl MaterialXFormat)
