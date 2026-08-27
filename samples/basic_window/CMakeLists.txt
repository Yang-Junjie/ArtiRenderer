add_executable(basic_window
    application.cpp
    basic_window_layer.cpp
)

target_include_directories(basic_window PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(basic_window
    PRIVATE
        ArtiRenderer::Renderer
        ArtiRenderer::SampleCommon
        ArtiChoco::Platform
        ArtiSDK::GLM
)

artichoco_stage_vulkan_sdk_runtime(basic_window)
set_target_properties(basic_window PROPERTIES FOLDER "Samples")
