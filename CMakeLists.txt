cmake_minimum_required(VERSION 3.25)

project(ArtiRenderer VERSION 0.1.0 LANGUAGES CXX)

option(ARTIRENDERER_BUILD_SAMPLES "Build ArtiRenderer samples" ON)
option(ARTIRENDERER_BUILD_TESTS "Build ArtiRenderer tests" ON)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

set(ARTIRENDERER_ARTICHOCO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ArtiChoco")
foreach(_arti_required_submodule
    "${ARTIRENDERER_ARTICHOCO_DIR}/CMakeLists.txt"
    "${ARTIRENDERER_ARTICHOCO_DIR}/third_party/spdlog/CMakeLists.txt"
    "${ARTIRENDERER_ARTICHOCO_DIR}/third_party/nvrhi/CMakeLists.txt"
    "${ARTIRENDERER_ARTICHOCO_DIR}/third_party/yaml-cpp/CMakeLists.txt"
    "${ARTIRENDERER_ARTICHOCO_DIR}/third_party/enkiTS/CMakeLists.txt"
    "${ARTIRENDERER_ARTICHOCO_DIR}/third_party/entt/src/entt/entt.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp"
)
    if(NOT EXISTS "${_arti_required_submodule}")
        message(FATAL_ERROR
            "Missing submodule file: ${_arti_required_submodule}\n"
            "Run: git submodule update --init --recursive")
    endif()
endforeach()

# Include the SDK module here (not only inside ArtiChoco) so that the
# ArtiSDK::* targets and artichoco_stage_vulkan_sdk_runtime() are visible to
# every directory of this project. ArtiVulkanSDK.cmake uses include_guard(GLOBAL),
# so ArtiChoco's own include() call becomes a no-op.
list(APPEND CMAKE_MODULE_PATH "${ARTIRENDERER_ARTICHOCO_DIR}/cmake")
include(ArtiVulkanSDK)

add_subdirectory(ArtiChoco)
# third_party 在 ArtiRenderer 之前：imgui_sdl3 要用 ArtiChoco 的 cmake 模块提供的 ArtiSDK::SDL3。
add_subdirectory(third_party)
add_subdirectory(ArtiRenderer)

if(ARTIRENDERER_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(ARTIRENDERER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
