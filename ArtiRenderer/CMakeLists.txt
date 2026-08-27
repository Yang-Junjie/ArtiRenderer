add_library(artirenderer STATIC
    src/renderer.cpp
)
add_library(ArtiRenderer::Renderer ALIAS artirenderer)

target_compile_features(artirenderer PUBLIC cxx_std_20)
target_include_directories(artirenderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(artirenderer PRIVATE
    ARTIRENDERER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ARTIRENDERER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ARTIRENDERER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ARTIRENDERER_VERSION_STRING="${PROJECT_VERSION}"
)
if(WIN32)
    target_compile_definitions(artirenderer PRIVATE NOMINMAX)
endif()

target_link_libraries(artirenderer
    PUBLIC
        ArtiChoco::Core
        ArtiChoco::Renderer
        ArtiSDK::GLM
)

set_target_properties(artirenderer PROPERTIES FOLDER "ArtiRenderer")
