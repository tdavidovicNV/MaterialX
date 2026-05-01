file(GLOB materialx_source "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB materialx_headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h*")
list(FILTER materialx_source EXCLUDE REGEX ".*/TextureBaker\\.cpp$")
list(FILTER materialx_headers EXCLUDE REGEX ".*/TextureBaker\\.h$")

mx_add_library(MaterialXRenderSlang
    SOURCE_FILES
        ${materialx_source}
    HEADER_FILES
        ${materialx_headers}
    MTLX_MODULES
        MaterialXRenderHw
        MaterialXGenSlang
        MaterialXFormat
    EXPORT_DEFINE
        MATERIALX_RENDERSLANG_EXPORTS
    SKIP_INSTALL)

if(NOT TARGET slang-rhi)
    set(SLANG_RHI_BINARY_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>)
    add_subdirectory(${MATERIALX_SLANG_RHI_SOURCE_DIR} build)
endif()
target_link_libraries(${TARGET_NAME} PUBLIC slang-rhi)
