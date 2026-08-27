add_library(artirenderer STATIC
    src/renderer.cpp
    src/detail/log.cpp
    src/detail/mesh_vertex_layout.cpp
    src/detail/nvrhi_conversion.cpp
    src/detail/resource_registry.cpp
    src/detail/shader_paths.cpp
    src/pipeline/frame_context.cpp
    src/pipeline/linear_pass.cpp
    src/pipeline/linear_pipeline.cpp
    src/pipeline/pass_blackboard.cpp
    src/pipeline/passes/forward_opaque_pass.cpp
    src/pipeline/passes/present_pass.cpp
)
add_library(ArtiRenderer::Renderer ALIAS artirenderer)

target_compile_features(artirenderer PUBLIC cxx_std_20)
target_include_directories(artirenderer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/detail"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline"
)
target_compile_definitions(artirenderer PRIVATE
    ARTIRENDERER_VERSION_STRING="${PROJECT_VERSION}"
    # 内建 slang 直接从源码树读，改 shader 不用重新编译 C++。
    # 代价是二进制不可搬移，见 src/detail/shader_paths.h。
    ARTIRENDERER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
)
if(WIN32)
    target_compile_definitions(artirenderer PRIVATE NOMINMAX)
endif()

target_link_libraries(artirenderer
    PUBLIC
        ArtiChoco::Core
        ArtiChoco::Renderer
        ArtiSDK::GLM
    PRIVATE
        ArtiSDK::Slang
        nvrhi_vk
)

set_target_properties(artirenderer PROPERTIES FOLDER "ArtiRenderer")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/forward_unlit.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present.slang
)
target_sources(artirenderer PRIVATE
    src/shaders/forward_unlit.slang
    src/shaders/present.slang
)
set_source_files_properties(
    src/shaders/forward_unlit.slang
    src/shaders/present.slang
    PROPERTIES HEADER_FILE_ONLY TRUE
)
