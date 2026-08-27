#pragma once

#include <filesystem>
#include <string_view>

namespace arti::rendering::detail {

// 内建 slang 源码的路径。
//
// 现在直接指向源码树（ARTIRENDERER_SHADER_DIR 由 CMake 注入），好处是改
// shader 不用重新编译 C++；代价是二进制不可搬移。真要发布的时候需要一个
// 资产打包步骤，届时改这一个函数即可。
 std::filesystem::path shaderPath(std::string_view name);

} // namespace arti::rendering::detail
