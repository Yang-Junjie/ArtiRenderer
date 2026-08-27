function(artirenderer_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE ArtiRenderer::Renderer)
    artichoco_stage_vulkan_sdk_runtime(${name})
    set_target_properties(${name} PROPERTIES FOLDER "Tests")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

artirenderer_add_test(texture_desc_test texture_desc_test.cpp)
artirenderer_add_test(handle_test handle_test.cpp)
artirenderer_add_test(aabb_test aabb_test.cpp)

# 冒烟测试：无窗口模式跑几帧，确认 Application 生命周期没有崩。
if(ARTIRENDERER_BUILD_SAMPLES)
    add_test(NAME basic_window_headless_smoke
        COMMAND basic_window --headless --frames 5
    )

    # ImGuiPass 需要真的 swapchain 才跑得起来（headless 没有 backbuffer 可画），所以这个用例
    # 开窗口。demo 窗口把 scissor、大网格的 vtx offset、多 draw list 都走一遍，配合 RHI 的
    # validation layer，任何一条非法 draw 都会被拦下来。
    #
    # 需要能创建 Vulkan surface 的桌面环境，纯 CI 容器里会失败 —— 打上标签方便按需排除
    # （ctest -LE requires-display）。
    add_test(NAME basic_window_imgui_smoke
        COMMAND basic_window --frames 30 --imgui-demo
    )

    # 编辑器模式：PresentPass 关掉，场景经 SceneColor 进 ImGui::Image。和上面那个用例走的是
    # 不同的 present 路径和不同的 decode_srgb 分支，所以两个都要跑。
    add_test(NAME basic_window_editor_smoke
        COMMAND basic_window --frames 30 --editor --imgui-demo
    )

    set_tests_properties(basic_window_imgui_smoke basic_window_editor_smoke
        PROPERTIES LABELS "requires-display")
endif()
