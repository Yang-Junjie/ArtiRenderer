# 宿主侧的 ImGui 胶水（context、SDL3 后端、字体图集）。放在 samples 而不是 artirenderer 里，
# 因为 renderer 不链 Platform/SDL —— 它只认 FrameOverlay 里的一个 ImDrawData 指针。
add_library(arti_sample_common STATIC
    common/imgui_host.cpp
)
add_library(ArtiRenderer::SampleCommon ALIAS arti_sample_common)

target_compile_features(arti_sample_common PUBLIC cxx_std_20)
target_include_directories(arti_sample_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_link_libraries(arti_sample_common
    PUBLIC
        ArtiRenderer::Renderer
        ImGui::Core
    PRIVATE
        ArtiChoco::Platform
        ArtiSDK::SDL3
        ImGui::SDL3Backend
)
set_target_properties(arti_sample_common PROPERTIES FOLDER "Samples")

add_subdirectory(basic_window)
