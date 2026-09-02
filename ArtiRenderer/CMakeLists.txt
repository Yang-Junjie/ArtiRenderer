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
    src/pipeline/shadow_cascades.cpp
    src/pipeline/shadow_targets.cpp
    src/pipeline/passes/clear_scene_pass.cpp
    src/pipeline/passes/debug_line_pass.cpp
    src/pipeline/passes/deferred_lighting_pass.cpp
    src/pipeline/passes/draw_resolve.cpp
    src/pipeline/passes/environment_bake_pass.cpp
    src/pipeline/passes/gbuffer_pass.cpp
    src/pipeline/passes/imgui_pass.cpp
    src/pipeline/passes/picking_pass.cpp
    src/pipeline/passes/present_pass.cpp
    src/pipeline/passes/shadow_pass.cpp
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
# 内建 slang 源码目录。CACHE INTERNAL 是必须的：artirenderer_stage_shaders() 在**调用点**
# 展开，而调用点在 Runtime / Tools 的目录作用域里，那里看不见这里的普通变量。
set(ARTIRENDERER_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders" CACHE INTERNAL
    "ArtiRenderer builtin Slang sources")

target_compile_definitions(artirenderer PRIVATE
    ARTIRENDERER_VERSION_STRING="${PROJECT_VERSION}"
    # 着色器是运行期编译的，所以 .slang 必须在运行时存在。这个宏是**回落**路径（源码树），
    # 优先用的是 exe 旁边由 artirenderer_stage_shaders() 拷过去的那一份 ——
    # 两段查找见 src/detail/shader_paths.h。
    #
    # 保留源码树这条回落是刻意的：开发期改 shader 不用重新编译 C++。
    ARTIRENDERER_SHADER_DIR="${ARTIRENDERER_SHADER_SOURCE_DIR}"
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
    src/shaders/shadow_depth.slang
    src/shaders/skybox.slang
    src/shaders/imgui.slang
    src/shaders/picking.slang
    src/shaders/present.slang
    src/shaders/tonemap.slang
)
# staging 函数要把这些文件列成显式依赖（改一个 .slang 就重新拷），而它在其他目录作用域里
# 被调用，所以要 CACHE INTERNAL 的绝对路径。
set(_arti_shader_files "")
foreach(_arti_shader IN LISTS ARTIRENDERER_SHADERS)
    list(APPEND _arti_shader_files "${CMAKE_CURRENT_SOURCE_DIR}/${_arti_shader}")
endforeach()
set(ARTIRENDERER_SHADER_FILES "${_arti_shader_files}" CACHE INTERNAL
    "ArtiRenderer builtin Slang source files (absolute)")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ARTIRENDERER_SHADERS})
target_sources(artirenderer PRIVATE ${ARTIRENDERER_SHADERS})
set_source_files_properties(${ARTIRENDERER_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

# 把内建 shader 拷到 target 旁边的 shaders/。由**消费方 exe** 调用，和
# artichoco_stage_vulkan_sdk_runtime() 一个形状 —— 每个 exe 自己决定要不要，而不是做成一个
# 全局 custom target（那会把路径耦合到 CMAKE_RUNTIME_OUTPUT_DIRECTORY 上）。
#
# 着色器是运行期编译的，所以 .slang 必须跟着二进制走。shaderPath() 优先用这一份，
# 没有才回落到 ARTIRENDERER_SHADER_DIR（源码树），见 src/detail/shader_paths.h。
#
# **走 stamp + custom target 而不是 POST_BUILD**：POST_BUILD 只在 target 重新链接时才跑，而改一个
# .slang 不会导致重链（shader 是 HEADER_FILE_ONLY 的源文件）。那样 staged 的那份会悄悄变旧，
# 而运行时优先用的正是它 —— 表现成「改了 shader 却没生效」，还会让人以为是 Slang 缓存。
# stamp 把 shader 文件列成显式依赖，改哪个都会重新拷。
function(artirenderer_stage_shaders target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot stage shaders for unknown target: ${target}")
    endif()

    # 整目录拷，而不是按 ARTIRENDERER_SHADERS 逐个拷：漏登记一个 .slang 只会让 IDE 分组少一项，
    # 漏拷一个却会让运行时找不到文件（而且 shaderPath() 已经选定了 staged 根，不会再回落）。
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(_arti_copy_directory copy_directory_if_different)
    else()
        set(_arti_copy_directory copy_directory)
    endif()

    set(_arti_stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders.stamp")
    add_custom_command(
        OUTPUT "${_arti_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E ${_arti_copy_directory}
            "${ARTIRENDERER_SHADER_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_arti_stamp}"
        DEPENDS ${ARTIRENDERER_SHADER_FILES}
        COMMENT "Staging ArtiRenderer shaders next to ${target}"
        VERBATIM
    )
    add_custom_target("${target}_stage_shaders" DEPENDS "${_arti_stamp}")
    add_dependencies("${target}" "${target}_stage_shaders")
endfunction()
