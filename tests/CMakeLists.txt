function(artirenderer_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE ArtiRenderer::Renderer)
    artichoco_stage_vulkan_sdk_runtime(${name})
    set_target_properties(${name} PROPERTIES FOLDER "Tests")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

artirenderer_add_test(texture_desc_test texture_desc_test.cpp)

# 冒烟测试：无窗口模式跑几帧，确认 Application 生命周期没有崩。
if(ARTIRENDERER_BUILD_SAMPLES)
    add_test(NAME basic_window_headless_smoke
        COMMAND basic_window --headless --frames 5
    )
endif()
