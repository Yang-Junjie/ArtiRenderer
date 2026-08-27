# Dear ImGui, docking 分支。拆成两个 target 是为了不让 SDL 泄进 renderer：
# artirenderer 只链 ImGui::Core（画 ImDrawData 不需要知道窗口系统），宿主程序才链
# ImGui::SDL3Backend。ArtiChoco::Renderer 同样不链 Platform，这里保持一致。
add_library(imgui_core STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    # demo 留着：它把 scissor、vtx offset、多 draw list 这些边界都走一遍，是 pass 最好的冒烟测试。
    imgui/imgui_demo.cpp
)
add_library(ImGui::Core ALIAS imgui_core)

target_compile_features(imgui_core PUBLIC cxx_std_20)
target_include_directories(imgui_core SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)
set_target_properties(imgui_core PROPERTIES FOLDER "ThirdParty")

add_library(imgui_sdl3 STATIC
    imgui/backends/imgui_impl_sdl3.cpp
)
add_library(ImGui::SDL3Backend ALIAS imgui_sdl3)

target_compile_features(imgui_sdl3 PUBLIC cxx_std_20)
target_include_directories(imgui_sdl3 SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
)
target_link_libraries(imgui_sdl3 PUBLIC ImGui::Core PRIVATE ArtiSDK::SDL3)
set_target_properties(imgui_sdl3 PROPERTIES FOLDER "ThirdParty")
