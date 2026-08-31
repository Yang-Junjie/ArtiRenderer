add_library(artirenderer STATIC
    src/renderer.cpp
    src/detail/log.cpp
    src/detail/compute_program.cpp
    src/detail/mesh_vertex_layout.cpp
    src/detail/nvrhi_conversion.cpp
    src/detail/resource_registry.cpp
    src/detail/shader_paths.cpp
    src/pipeline/frame_context.cpp
    src/pipeline/linear_pass.cpp
    src/pipeline/linear_pipeline.cpp
    src/pipeline/gbuffer_targets.cpp
    src/pipeline/render_target_set.cpp
    src/pipeline/passes/clear_scene_pass.cpp
    src/pipeline/passes/debug_line_pass.cpp
    src/pipeline/passes/deferred_lighting_pass.cpp
    src/pipeline/passes/draw_resolve.cpp
    src/pipeline/passes/environment_bake_pass.cpp
    src/pipeline/passes/gbuffer_pass.cpp
    src/pipeline/passes/imgui_pass.cpp
    src/pipeline/passes/picking_pass.cpp
    src/pipeline/passes/present_pass.cpp
    src/pipeline/passes/sky_pass.cpp
    src/pipeline/passes/tonemap_pass.cpp
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
        # 只要 imgui 核心：画 ImDrawData 不需要知道窗口系统，SDL3 后端留给宿主链。
        ImGui::Core
        nvrhi_vk
)

set_target_properties(artirenderer PROPERTIES FOLDER "ArtiRenderer")

set(ARTIRENDERER_SHADERS
    src/shaders/debug_line.slang
    src/shaders/gbuffer.slang
    src/shaders/deferred_lighting.slang
    src/shaders/ibl_common.slang
    src/shaders/equirect_to_cube.slang
    src/shaders/irradiance.slang
    src/shaders/prefilter.slang
    src/shaders/brdf_lut.slang
    src/shaders/skybox.slang
    src/shaders/imgui.slang
    src/shaders/picking.slang
    src/shaders/present.slang
    src/shaders/tonemap.slang
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ARTIRENDERER_SHADERS})
target_sources(artirenderer PRIVATE ${ARTIRENDERER_SHADERS})
set_source_files_properties(${ARTIRENDERER_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)
