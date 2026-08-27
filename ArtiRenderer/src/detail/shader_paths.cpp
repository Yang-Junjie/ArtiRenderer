#include "shader_paths.h"

#ifndef ARTIRENDERER_SHADER_DIR
#error "ARTIRENDERER_SHADER_DIR must be provided by the build system."
#endif

namespace arti::rendering::detail {

std::filesystem::path shaderPath(std::string_view name)
{
    return std::filesystem::path{ ARTIRENDERER_SHADER_DIR } / name;
}

} // namespace arti::rendering::detail
